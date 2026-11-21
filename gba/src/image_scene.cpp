#include "image_scene.h"
#include "graphics_decode.h"
#include "save_archive.h"
#include "text_canvas.h"
#include "bn_affine_bg_map_item.h"
#include "bn_affine_bg_map_ptr.h"
#include "bn_regular_bg_map_item.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_bg_palette_item.h"
#include "bn_sprite_palette_item.h"
#include "bn_sprite_builder.h"
#include "bn_sprite_shape_size.h"
#include "bn_blending.h"
#include "bn_blending_transparency_attributes.h"
#include "bn_sprites.h"
#include "bn_core.h"
#include "bn_math.h"
#include "bn_memory.h"
#include "bn_array.h"
#include "bn_rect_window.h"
#include "bn_window.h"
#include "bn_utility.h"
#include "bn_tile.h"
#include <cstring>
#ifdef MANOSABA_ACTOR_BAKED
#include "../build-actor-baked/actor_variant_map.h"
#endif
namespace manosaba
{
BN_DATA_EWRAM graphics_profile graphics_performance={0,0,0,0,-1,-1,0,0,0,0,0,0,0};
namespace
{
BN_DATA_EWRAM_BSS bn::tile actor_pixels[64];
BN_DATA_EWRAM_BSS bn::tile bg_pixels[2][512];
auto& top_cells=graphic_map_cells;
BN_DATA_EWRAM_BSS bn::regular_bg_map_cell lower_cells[1024];
BN_DATA_EWRAM_BSS bn::color background_colors[240];
BN_DATA_EWRAM_BSS bn::fixed panel_scroll_rows[160];
BN_DATA_EWRAM_BSS bn::fixed panel_scroll_x_rows[160];
BN_DATA_EWRAM_BSS bn::tile panel_tiles_data[8];
constexpr auto panel_map_cells()
{
    bn::array<bn::regular_bg_map_cell,1024> cells{};
    for(int y=13;y<32;++y)for(int x=0;x<32;++x)cells[y*32+x]=y==13 ? 1 : y==14 ? 2 : 3;
    return cells;
}
constexpr auto panel_cells=panel_map_cells();
BN_DATA_EWRAM_BSS bn::blending_transparency_attributes alpha_rows[160];
BN_DATA_EWRAM_BSS bn::pair<bn::fixed,bn::fixed> clip_rows[2][160];
constexpr uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
constexpr unsigned opacity_masks[17][4]={{0u,0u,0u,0u},{983055u,0u,0u,0u},{983055u,0u,251662080u,0u},{252645135u,0u,251662080u,0u},{252645135u,0u,252645135u,0u},{252645135u,15728880u,252645135u,0u},{252645135u,15728880u,252645135u,4026593280u},{252645135u,4042322160u,252645135u,4026593280u},{252645135u,4042322160u,252645135u,4042322160u},{268374015u,4042322160u,252645135u,4042322160u},{268374015u,4042322160u,4279238415u,4042322160u},{4294967295u,4042322160u,4279238415u,4042322160u},{4294967295u,4042322160u,4294967295u,4042322160u},{4294967295u,4043305215u,4294967295u,4042322160u},{4294967295u,4043305215u,4294967295u,4293984240u},{4294967295u,4294967295u,4294967295u,4293984240u},{4294967295u,4294967295u,4294967295u,4294967295u}};
int scalar(const stage_entity& entity,stage_property p) { return entity.get(p); }
bn::fixed fx(int value) { return bn::fixed::from_data(value*16); }
uint64_t material_key(const stage_entity& entity)
{
    uint64_t value=0;
    for(auto prop:{stage_property::tint,stage_property::red,stage_property::green,stage_property::blue})value=(value<<9)|unsigned(bn::clamp(entity.get(prop),0,256));
    return value;
}
uint16_t tinted(uint16_t rgb,const stage_entity& entity)
{
    int light=bn::clamp(entity.get(stage_property::tint),0,256);
    int r=(rgb&31)*light*bn::clamp(entity.get(stage_property::red),0,256)/65536;
    int g=((rgb>>5)&31)*light*bn::clamp(entity.get(stage_property::green),0,256)/65536;
    int b=((rgb>>10)&31)*light*bn::clamp(entity.get(stage_property::blue),0,256)/65536;
    return uint16_t(r|(g<<5)|(b<<10));
}
struct point { int x,y; };
point camera_point(const stage_entity& camera,int x,int y)
{
    if(!scalar(camera,stage_property::angle) && scalar(camera,stage_property::scale)==256)
        return {x-scalar(camera,stage_property::x),y-scalar(camera,stage_property::y)};
    int dx=x-120*256-scalar(camera,stage_property::x),dy=y-80*256-scalar(camera,stage_property::y);
    auto angle=fx(scalar(camera,stage_property::angle));
    int c=bn::degrees_lut_cos_safe(angle).data(),s=bn::degrees_lut_sin_safe(angle).data();
    int z=scalar(camera,stage_property::scale);
    return {120*256+int((int64_t(dx)*c-int64_t(dy)*s)*z/(4096*256)),
            80*256+int((int64_t(dx)*s+int64_t(dy)*c)*z/(4096*256))};
}
struct actor_transform
{
    point center;
    int c,s;
    point part(const graphic_asset& asset,const graphic_part& part) const
    {
        int dx=part.x+part.width/2-asset.pivot_x,dy=part.y+part.height/2-asset.pivot_y;
        return {center.x+dx*c-dy*s,center.y+dx*s+dy*c};
    }
};
actor_transform transform(const stage_entity& entity,const stage_entity& camera,int nominal_scale=256,int baked_angle=0)
{
    int degrees=entity.get(stage_property::angle)+camera.get(stage_property::angle)-baked_angle;
    int scale=entity.get(stage_property::scale)*camera.get(stage_property::scale)/256;
    if(nominal_scale!=256)scale=scale*256/nominal_scale;
    auto center=camera_point(camera,entity.get(stage_property::x),entity.get(stage_property::y));
    if(!degrees)return {center,scale,0};
    auto angle=fx(degrees);
    return {center,
            bn::degrees_lut_cos_safe(angle).data()*scale/4096,bn::degrees_lut_sin_safe(angle).data()*scale/4096};
}

bool part_visible(const actor_transform& t,const graphic_asset& asset,const graphic_part& part,int margin)
{
    auto center=t.part(asset,part);
    int half_x=(bn::abs(t.c)*part.width+bn::abs(t.s)*part.height)/2;
    int half_y=(bn::abs(t.s)*part.width+bn::abs(t.c)*part.height)/2;
    return center.x+half_x> -margin*256 && center.x-half_x<(240+margin)*256 &&
           center.y+half_y> -margin*256 && center.y-half_y<(160+margin)*256;
}

uint32_t geometry_key(const stage_entity& entity,const stage_entity& relative,int id)
{
    uint32_t key=uint32_t(id+2);
    for(auto prop:{stage_property::x,stage_property::y,stage_property::scale,stage_property::angle})
    {key=(key^uint32_t(entity.get(prop)))*16777619u;key=(key^uint32_t(relative.get(prop)))*16777619u;}
    return key ? key : 1;
}
struct actor_cache_keys {uint32_t clip,viewport;};
// Cache bookkeeping belongs in ROM; keep the scarce IWRAM for pixel decoding.
__attribute__((noinline))
actor_cache_keys cache_keys(const stage_player& player,int entity,int clip,int slot)
{
    uint32_t geometry=clip>=0 ? geometry_key(player.entity(entity),player.entity(clip),clip)^
        (unsigned(player.entity(clip).image+1)*16777619u) : 0;
    int phase=clip>=0 ? bn::clamp(player.entity(clip).get(stage_property::opacity)/16,0,16) : 0;
    // Actor alpha never changes masked pixels; only full masks cull blocks.
    return {geometry^(unsigned(phase)*2654435761u),
        geometry_key(player.entity(entity),player.entity(stage_camera),slot)^geometry^(unsigned(phase==16)*2654435761u)};
}
__attribute__((noinline))
bn::sprite_tiles_ptr allocate_actor_tiles(const graphic_asset& asset,const graphic_part& part,
    const bn::vector<int,64>& indices,const bn::vector<bn::sprite_tiles_ptr,64>& tiles,bool silhouette)
{
    if(silhouette && part.silhouette_group)for(int i=0;i<indices.size();++i)
        if(asset.parts[indices[i]].silhouette_group==part.silhouette_group)return tiles[i];
    return bn::sprite_tiles_ptr::allocate(part.width*part.height/64,bn::bpp_mode::BPP_4);
}
struct source_clip
{
    int u,v,du,dv,width,height,phase;
    bool intersects(const graphic_asset& asset,const graphic_part& part) const
    {
        if(phase<16)return true;
        int x=part.x-asset.pivot_x,y=part.y-asset.pivot_y;
        int u0=u+x*du-y*dv,v0=v+x*dv+y*du;
        int ux=(part.width-1)*du,uy=-(part.height-1)*dv;
        int vx=(part.width-1)*dv,vy=(part.height-1)*du;
        return u0+bn::max(0,ux)+bn::max(0,uy)>=0 && u0+bn::min(0,ux)+bn::min(0,uy)<width &&
               v0+bn::max(0,vx)+bn::max(0,vy)>=0 && v0+bn::min(0,vx)+bn::min(0,vy)<height;
    }
    unsigned mask(int x,int y) const
    {
        int u0=u+x*du-y*dv,v0=v+x*dv+y*du;
        int u1=u0+7*du,v1=v0+7*dv;
        bool a=unsigned(u0)<unsigned(width),b=unsigned(v0)<unsigned(height);
        if(a && b && unsigned(u1)<unsigned(width) && unsigned(v1)<unsigned(height))return ~0u;
        unsigned outside=~opacity_masks[phase][unsigned(y)&3];
        if((u0<0 && u1<0) || (v0<0 && v1<0) || (u0>=width && u1>=width) || (v0>=height && v1>=height))return outside;
        unsigned inside=0;
        for(int i=0;i<8;++i,u0+=du,v0+=dv)
            if(unsigned(u0)<unsigned(width) && unsigned(v0)<unsigned(height))inside|=15u<<(i*4);
        return inside|outside;
    }
};
source_clip actor_clip(const stage_entity& actor,const stage_entity& rectangle,const graphic_asset& shape,int nominal_scale=256,int baked_angle=0)
{
    int scale=bn::max(1,rectangle.get(stage_property::scale));
    int actor_scale=actor.get(stage_property::scale);
    if(nominal_scale!=256)actor_scale=actor_scale*256/nominal_scale;
    auto angle=fx(actor.get(stage_property::angle)-rectangle.get(stage_property::angle)-baked_angle);
    int c=bn::degrees_lut_cos_safe(angle).data(),s=bn::degrees_lut_sin_safe(angle).data();
    auto inverse=fx(rectangle.get(stage_property::angle));
    int ic=bn::degrees_lut_cos_safe(inverse).data(),is=bn::degrees_lut_sin_safe(inverse).data();
    int dx=actor.get(stage_property::x)-rectangle.get(stage_property::x),dy=actor.get(stage_property::y)-rectangle.get(stage_property::y);
    return {shape.width*128+int((int64_t(dx)*ic+int64_t(dy)*is)/16/scale),
            shape.height*128+int((-int64_t(dx)*is+int64_t(dy)*ic)/16/scale),
            c*actor_scale/16/scale,s*actor_scale/16/scale,
            shape.width*256,shape.height*256,bn::clamp(rectangle.get(stage_property::opacity)/16,0,16)};
}

// Run on geometry changes, outside the scarce IWRAM decoding routine.
__attribute__((noinline))
bool part_needed(const actor_transform& placement,const graphic_asset& asset,const graphic_part& part,const source_clip& clipping)
{
    return part_visible(placement,asset,part,0) && clipping.intersects(asset,part);
}

bool covers_screen(const stage_entity& entity,const stage_entity& camera,const graphic_asset& asset)
{
    auto t=transform(entity,camera);
    int64_t length=int64_t(t.c)*t.c+int64_t(t.s)*t.s;
    for(int corner=0;corner<4;++corner)
    {
        int dx=(corner&1 ? 240*256 : 0)-t.center.x,dy=(corner&2 ? 160*256 : 0)-t.center.y;
        int64_t u=int64_t(dx)*t.c+int64_t(dy)*t.s,v=-int64_t(dx)*t.s+int64_t(dy)*t.c;
        if(bn::abs(u)>asset.width*length/2 || bn::abs(v)>asset.height*length/2)return false;
    }
    return true;
}

}
void image_scene::archive(save_archive& a)
{
    int bank=_bank_id,frame=_frame;a.field(bank);a.field(frame);a.field(_double_buffer);
    if(a.reading()){reset();set_bank(bank,_double_buffer);_frame=frame;}
    _player.archive(a);
}
void image_scene::reset()
{
    _dim=false;
    _selection_key=0;
    _alpha_hbe.reset();_last_alpha=-1;
    bn::rect_window::internal().set_visible(false);bn::rect_window::external().set_visible(false);
    release_panel();
    for(auto& actor:_actors){actor.sprites.clear();actor.tiles.clear();actor.palettes.clear();actor.matrix.reset();actor.part_indices.clear();actor.image=-1;actor.cursor=actor.decoded=actor.allocating=actor.published_parts=0;}
    for(auto& bg:_backgrounds){bg.clip_hbe.reset();bg.regular.reset();bg.affine.reset();bg.regular_tiles.reset();for(auto& t:bg.affine_tiles)t.reset();bg.image=bg.target=-1;bg.bytes=bg.cursor=bg.decoded=0;bg.publish=false;}
    _palette.reset();_player.reset(nullptr,nullptr);_bank=nullptr;_bank_id=_frame=-1;
    _allocate_delay=1;_ready=_prepared=_loading=false;
}
void image_scene::set_bank(int bank,bool double_buffer)
{
    _double_buffer=double_buffer;
    if(bank==_bank_id)return;
    _bank_id=bank;_bank=bank>=0 ? &graphics_banks[bank] : nullptr;
    _player.reset(nullptr,nullptr);
    if(_bank)_player.set_program(_bank->keys,_bank->commands);
    graphics_performance.bank=bank;
    _loading=true;_ready=false;
}
void image_scene::set_visible(bool visible)
{
    if(visible==_visible)return;
    _visible=visible;
    if(visible){_loading=true;_ready=false;}
    else
    {
        _dim=false;
        _alpha_hbe.reset();_last_alpha=-1;
        bn::rect_window::internal().set_visible(false);bn::rect_window::external().set_visible(false);
        release_panel();
        for(auto& bg:_backgrounds){bg.clip_hbe.reset();bg.regular.reset();bg.affine.reset();bg.regular_tiles.reset();for(auto& t:bg.affine_tiles)t.reset();bg.image=bg.target=-1;bg.bytes=bg.cursor=bg.decoded=0;bg.publish=false;}
        for(auto& actor:_actors){actor.sprites.clear();actor.tiles.clear();actor.palettes.clear();actor.matrix.reset();actor.part_indices.clear();actor.image=-1;actor.cursor=actor.decoded=actor.allocating=actor.published_parts=0;}
        _palette.reset();_allocate_delay=1;_ready=false;_prepared=false;
    }
}
void image_scene::release_panel()
{
    _panel_hbe.reset();_panel_scroll_hbe.reset();_panel_scroll_x_hbe.reset();_panel_rows.clear();
    _panel_map.reset();_panel_tiles.reset();
}
void image_scene::update_panel(bool printer,int priority)
{
    auto& lower=_backgrounds[0];
    lower.regular->set_blending_top_enabled(printer);
    if(!printer)
    {
        _panel_hbe.reset();_panel_scroll_hbe.reset();_panel_scroll_x_hbe.reset();_panel_image=-2;
        return;
    }
    int has_image=lower.image>=0;
    if(has_image!=_panel_image || priority!=_panel_priority)
    {
        const auto& upper_map=has_image ? lower.regular->map() : *_panel_map;
        if(_panel_rows.empty())
            for(int y=0;y<160;++y)_panel_rows.emplace_back(y<104 ? upper_map : *_panel_map,y<104 ? priority : 0,false);
        else
            for(int y=0;y<104;++y){_panel_rows[y].set_map(upper_map);_panel_rows[y].set_priority(priority);}
        if(_panel_hbe)_panel_hbe->reload_attributes_ref();
        else _panel_hbe=bn::regular_bg_attributes_hbe_ptr::create(*lower.regular,bn::span<const bn::regular_bg_attributes>(_panel_rows.data(),160));
        _panel_image=has_image;_panel_priority=priority;
    }
    auto x=lower.regular->x();
    if(!_panel_scroll_x_hbe || x!=_panel_x)
    {
        // HBE deltas add to hardware scroll, which runs opposite to BG position.
        for(int y=0;y<160;++y)panel_scroll_x_rows[y]=y<104 ? bn::fixed(0) : x-8;
        if(_panel_scroll_x_hbe)_panel_scroll_x_hbe->reload_deltas_ref();
        else _panel_scroll_x_hbe=bn::regular_bg_position_hbe_ptr::create_horizontal(*lower.regular,bn::span<const bn::fixed>(panel_scroll_x_rows,160));
        _panel_x=x;
    }
    auto position=lower.regular->y();
    if(!_panel_scroll_hbe || position!=_panel_y)
    {
        for(int y=0;y<160;++y)panel_scroll_rows[y]=y<104 ? bn::fixed(0) : position-48;
        if(_panel_scroll_hbe)_panel_scroll_hbe->reload_deltas_ref();
        else _panel_scroll_hbe=bn::regular_bg_position_hbe_ptr::create_vertical(*lower.regular,bn::span<const bn::fixed>(panel_scroll_rows,160));
        _panel_y=position;
    }
}
int image_scene::visible_background(int plane) const
{
    const auto& bg=_backgrounds[plane];
    const auto& overlay=_player.entity(stage_overlay);
    if(overlay.image>=0 && overlay.get(stage_property::opacity)>=256 && !overlay.maskable)return -1;
    if(!_visible || !_ready || bg.entity<0 || bg.image<0)return -1;
    if(bg.affine && !bg.affine->visible())return -1;
    if(bg.regular && !bg.regular->visible())return -1;
    return _player.entity(bg.entity).get(stage_property::opacity)>0 ? bg.image : -1;
}
void image_scene::allocate()
{
    for(auto& color:background_colors)color=bn::color(0,0,0);
    _palette=bn::bg_palette_ptr::create(bn::bg_palette_item(bn::span<const bn::color>(background_colors,240),bn::bpp_mode::BPP_8));
    auto& lower=_backgrounds[0];auto& top=_backgrounds[1];
    lower.regular_tiles=bn::regular_bg_tiles_ptr::allocate(256,bn::bpp_mode::BPP_8,false);
    lower.regular=bn::regular_bg_ptr::create(0,0,bn::regular_bg_map_ptr::allocate(
        bn::size(32,32),*lower.regular_tiles,*_palette));
    lower.regular->set_priority(3);lower.regular->set_blending_bottom_enabled(true);lower.regular->set_visible(false);
    top.affine_tiles[0]=bn::affine_bg_tiles_ptr::allocate(512,false);
    top.affine_tiles[1]=_double_buffer ? bn::affine_bg_tiles_ptr::allocate(512,false) : *top.affine_tiles[0];
    top.affine=bn::affine_bg_ptr::create(0,0,bn::affine_bg_map_ptr::create(
        bn::affine_bg_map_item(top_cells[0],bn::size(64,64)),*top.affine_tiles[0],*_palette));
    top.affine->set_priority(2);top.affine->set_blending_bottom_enabled(true);top.affine->set_visible(false);
    // BG-over-OBJ alpha can retain a character underneath the dialogue panel.
    // A semi-transparent OBJ panel cannot blend with another OBJ on the GBA.
    _palette->set_color(225,bn::color(1,1,1));
    _panel_tiles=bn::regular_bg_tiles_ptr::allocate(8,bn::bpp_mode::BPP_8,true);
    auto panel_vram=_panel_tiles->vram().value();
    auto* panel_pixels=reinterpret_cast<uint8_t*>(panel_tiles_data);
    std::memset(panel_pixels,0,256);
    for(int tile=1;tile<4;++tile)for(int y=0;y<8;++y)for(int x=0;x<8;++x)
    {
        int row=(tile-1)*8+y;
        if(row>=16 || bayer[y%4][x%4]<row+1)panel_pixels[tile*64+y*8+x]=225;
    }
    bn::memory::copy(panel_tiles_data[0],8,panel_vram[0]);
    _panel_map=bn::regular_bg_map_ptr::create(bn::regular_bg_map_item(panel_cells[0],bn::size(32,32)),*_panel_tiles,*_palette);
    _panel_image=-2;_panel_priority=-1;_panel_y=_panel_x=-1024;
    bn::sprites::set_blending_bottom_enabled(true);
    bn::blending::set_transparency_alpha(bn::fixed(.625));
    _prepared=true;
}
bool image_scene::background_backend(bool regular)
{
    auto& bg=_backgrounds[1];
    if((regular && bg.regular) || (!regular && bg.affine))return true;
    if(bg.regular || bg.affine)
    {
        bg.clip_hbe.reset();bg.clip_key=0;bg.regular.reset();bg.affine.reset();
        bg.regular_tiles.reset();for(auto& tiles:bg.affine_tiles)tiles.reset();
        bg.image=bg.target=-1;bg.bytes=bg.cursor=bg.decoded=0;bg.publish=bg.map_pending=false;
        bg.window_x=bg.window_y=-1024;
        // Butano reclaims released tile/map blocks at the next core update.
        return false;
    }
    if(regular)
    {
        bg.regular_tiles=bn::regular_bg_tiles_ptr::allocate(704,bn::bpp_mode::BPP_8,false);
        bg.regular=bn::regular_bg_ptr::create(8,48,bn::regular_bg_map_ptr::allocate(bn::size(32,32),*bg.regular_tiles,*_palette));
        bg.regular->set_priority(2);bg.regular->set_blending_bottom_enabled(true);bg.regular->set_visible(false);
    }
    else
    {
        bg.affine_tiles[0]=bn::affine_bg_tiles_ptr::allocate(512,false);
        bg.affine_tiles[1]=_double_buffer ? bn::affine_bg_tiles_ptr::allocate(512,false) : *bg.affine_tiles[0];bg.front=0;
        bg.affine=bn::affine_bg_ptr::create(0,0,bn::affine_bg_map_ptr::create(
            bn::affine_bg_map_item(top_cells[0],bn::size(64,64)),*bg.affine_tiles[0],*_palette));
        bg.affine->set_priority(2);bg.affine->set_blending_bottom_enabled(true);bg.affine->set_visible(false);
    }
    return true;
}
__attribute__((noinline))
bool image_scene::select_background_backend(int entity)
{
    int image=entity>=0 ? image_for(entity) : -1;
    bool regular=image>=0 && _player.entity(entity).regular_cg;
    if(regular)
    {
        const auto& camera=_player.entity(stage_camera);const auto& state=_player.entity(entity);
        regular=state.get(stage_property::scale)*camera.get(stage_property::scale)==65536 &&
                state.get(stage_property::angle)+camera.get(stage_property::angle)==0;
    }
    if(image<0)regular=bool(_backgrounds[1].regular);
    return background_backend(regular);
}
void image_scene::regular_position(int image,int x,int y)
{
    auto& bg=_backgrounds[1];const auto& asset=graphic_assets[image];const auto& data=*regular_graphic(image);
    int left=(asset.width*128-x)/256+data.origin,top=(asset.height*128-y)/256;
    int column=left>>3,row=top>>3;
    if(column!=bg.window_x || row!=bg.window_y)
    {
        regular_window(data,column,row,reinterpret_cast<uint16_t*>(top_cells));
        bg.window_x=column;bg.window_y=row;bg.map_pending=true;
    }
    bg.regular->set_position(8-(left&7),48-(top&7));
}
__attribute__((noinline))
bool image_scene::begin_background(int plane,int entity,int image,int variant)
{
    auto& bg=_backgrounds[plane];bg.target=image;bg.target_variant=variant;bg.cursor=bg.decoded=0;
    if(graphic_assets[image].kind==graphic_kind::solid)
    {
        auto* tile=reinterpret_cast<uint8_t*>(bg_pixels[plane]);
        if(entity==stage_overlay)
        {
            bg.bytes=bg.decoded=64;
            for(int y=0;y<8;++y)for(int x=0;x<8;++x)tile[y*8+x]=bayer[y%4][x%4]<variant ? 224 : 0;
        }
        else {bg.bytes=bg.decoded=128;std::memset(tile,0,64);std::memset(tile+64,plane ? 113 : 1,64);}
        return true;
    }
    const auto& data=graphic_assets[image].variants[variant];
    bg.bytes=(plane && bg.regular ? regular_graphic(image)->count : data.count)*64;
    return false;
}
int image_scene::image_for(int entity) const
{
    int image=_player.entity(entity).image;
    if(image>=0 && graphic_assets[image].kind==graphic_kind::animation)
    {
        const auto& animation=graphic_assets[image];
        image=animation.frames[(_player.clock()*animation.fps/60)%animation.frame_count];
    }
#ifdef MANOSABA_ACTOR_BAKED
    if(entity<stage_actor_count)image=actor_bake::image(image);
#endif
    return image;
}
void image_scene::apply(int event)
{
    int previous[stage_entity_count];
    for(int id=0;id<stage_entity_count;++id)previous[id]=_player.entity(id).image;
    _player.apply(_bank->events[event]);
    graphics_performance.event=event;
    for(int index=0;index<_bank->events[event].count;++index)
        if(_bank->commands[_bank->events[event].first+index].kind==stage_kind::image)
        {int target=_bank->commands[_bank->events[event].first+index].target;if(previous[target]!=_player.entity(target).image)_loading=true;}
}
bool image_scene::update(int frames,bool input)
{
    if(_allocate_delay)--_allocate_delay;
    if(! _loading)_player.tick(frames,input);
    for(int plane=0;plane<2;++plane)
    {
        auto& bg=_backgrounds[plane];
        if(bg.publish)
        {
            bg.image=bg.target;bg.variant=bg.target_variant;bg.publish=false;bg.material=~uint64_t(0);
            if(graphic_assets[bg.image].kind==graphic_kind::bg)
                for(int color=1;color<112;++color)_palette->set_color(plane*112+color,bn::color(graphic_assets[bg.image].palette[color]));
            if(plane && bg.regular)
            {
                bg.window_x=bg.window_y=-1024;
            }
            else if(plane)
            {
                bg.front^=1;bg.affine->set_tiles(*bg.affine_tiles[bg.front]);
                if(graphic_assets[bg.image].kind==graphic_kind::solid)
                {
                    std::memset(top_cells,0,sizeof(top_cells));
                    for(int y=22;y<42;++y)for(int x=17;x<47;++x)top_cells[y*64+x]=1;
                }
                else
                {
                    const auto& variant=graphic_assets[bg.image].variants[bg.variant];
                    expand_affine_map(graphic_assets[bg.image],variant,top_cells);
                }
                auto map=bg.affine->map();map.reload_cells_ref();
            }
            else
            {
                const auto& variant=graphic_assets[bg.image].variants[bg.variant];
                std::memcpy(lower_cells,variant.map,sizeof(lower_cells));
                bg.map_pending=true;
            }
        }
    }
    return false;
}
bool image_scene::retire_actor(int slot,int image,uint32_t viewport_key)
{
    auto& actor=_actors[slot];int entity=_actor_entities[slot];
    int clip=image>=0 ? clip_for(entity) : -1;
    bool silhouette=image>=0 && clip<0 && _player.entity(entity).get(stage_property::tint)==0;
    if(image>=0 && actor.image==image && actor.silhouette_shared==silhouette && !actor.allocating)
    {
        if(viewport_key!=actor.viewport_key)
        {
            const auto& asset=graphic_assets[image];auto placement=transform(_player.entity(entity),_player.entity(stage_camera),asset.nominal_scale,int16_t(asset.color));
            source_clip clipping{};
            if(clip>=0)clipping=actor_clip(_player.entity(entity),_player.entity(clip),graphic_assets[_player.entity(clip).image],asset.nominal_scale,int16_t(asset.color));
            auto needed=[&](int index){return part_needed(placement,asset,asset.parts[index],clipping);};
            int previous_bytes=0;for(const auto& tiles:actor.tiles)previous_bytes+=tiles.tiles_count()*32;
            bool uploading=actor.cursor<previous_bytes,removed=false;
            uint32_t loaded[8]={};
            for(int i=actor.part_indices.size()-1;i>=0;--i)
            {
                int index=actor.part_indices[i];
                if(needed(index))loaded[index>>5]|=1u<<(index&31);
                else
                {
                    actor.sprites.erase(actor.sprites.begin()+i);actor.tiles.erase(actor.tiles.begin()+i);actor.part_indices.erase(actor.part_indices.begin()+i);
                    if(i<actor.published_parts)--actor.published_parts;
                    removed=true;
                }
            }
            // Removing a block changes offsets in the shared upload stream.
            // Discard its pending chunk before reusing those offsets.
            if(removed && uploading){actor.cursor=actor.decoded=0;actor.phase=-1;}
            if(removed)actor.display_key=0;
            for(int part=0;part<asset.count;++part)
                if(!(loaded[part>>5]&(1u<<(part&31))) && needed(part))
                {actor.allocating=3;actor.part_cursor=0;actor.phase=-1;_loading=true;break;}
            actor.viewport_key=viewport_key;
            if(removed || actor.allocating)return true; // Flush all retirements before allocating.
        }
    }
    if(actor.image!=image || actor.silhouette_shared!=silhouette || actor.allocating==4)
    {
        _loading=true;
        if(actor.allocating!=4)for(auto& sprite:actor.sprites)sprite.set_visible(false);
        actor.allocating=4;
        while(!actor.sprites.empty() && _allocation_budget)
        {
            actor.sprites.pop_back();actor.tiles.pop_back();actor.part_indices.pop_back();--_allocation_budget;
        }
        actor.published_parts=bn::min(actor.published_parts,actor.sprites.size());
        if(!actor.sprites.empty())return true;
        actor.palettes.clear();actor.matrix.reset();
        actor.image=image;actor.material=~uint64_t(0);actor.phase=-1;actor.cursor=actor.decoded=0;actor.published_parts=0;actor.viewport_key=actor.display_key=0;
        actor.silhouette_shared=silhouette;
        actor.allocating=image>=0 ? 1 : 0;actor.part_cursor=0;
        return true;
    }
    return false;
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void image_scene::prepare_actor(int slot,int image,uint32_t clip_key,uint32_t viewport_key)
{
    auto& actor=_actors[slot];int entity=_actor_entities[slot];
    int clip=image>=0 ? clip_for(entity) : -1;
    bool silhouette=actor.silhouette_shared;
    if(actor.allocating)
    {
        const auto& asset=graphic_assets[image];
        if(actor.allocating==1)
        {
        if(_allocation_budget<4)return;
        _allocation_budget=0;
        for(int bank=0;bank<4;++bank)actor.palettes.push_back(bn::sprite_palette_ptr::create_new(bn::sprite_palette_item(
            bn::span<const bn::color>(reinterpret_cast<const bn::color*>(asset.palette+bank*16),16),bn::bpp_mode::BPP_4)));
        actor.matrix=bn::sprite_affine_mat_ptr::create();
        actor.allocating=2;
        }
        auto placement=transform(_player.entity(entity),_player.entity(stage_camera),asset.nominal_scale,int16_t(asset.color));
#if defined(MANOSABA_ACTOR_REGULAR) || defined(MANOSABA_ACTOR_SCALE_SNAP) || defined(MANOSABA_ACTOR_BAKED)
        bool affine=placement.c!=256 || placement.s!=0;
#else
        constexpr bool affine=true;
#endif
        source_clip clipping{};
        if(clip>=0)clipping=actor_clip(_player.entity(entity),_player.entity(clip),graphic_assets[_player.entity(clip).image],asset.nominal_scale,int16_t(asset.color));
        while(actor.part_cursor<asset.count && _allocation_budget>0)
        {
            int part=actor.part_cursor++;
            const auto& p=asset.parts[part];
            bool loaded=false;
            if(actor.allocating==3)for(int index:actor.part_indices)if(index==part){loaded=true;break;}
            if(loaded || !part_needed(placement,asset,p,clipping))continue;
            actor.tiles.push_back(allocate_actor_tiles(asset,p,actor.part_indices,actor.tiles,silhouette));
            actor.part_indices.push_back(part);
            bn::sprite_builder builder(bn::sprite_shape_size(p.width,p.height),actor.tiles.back(),actor.palettes[p.bank]);
            if(affine)builder.set_affine_mat(*actor.matrix);
            builder.set_bg_priority(2).set_visible(false);
            actor.sprites.push_back(bn::sprite_ptr::create(builder));--_allocation_budget;
        }
        if(actor.part_cursor<asset.count)return;
        actor.allocating=0;
        actor.viewport_key=viewport_key;
    }
    if(image<0)return;
    const auto& asset=graphic_assets[image];
    int phase=16; // Opacity uses native OBJ alpha; tile pixels change only with the pose/clip.
    int total=0;for(const auto& part:actor.tiles)total+=part.tiles_count()*32;
    if(phase==actor.phase && actor.clip_key==clip_key && actor.decoded>=total)return;
    if((phase!=actor.phase || actor.clip_key!=clip_key) && (actor.phase<0 || actor.cursor>=total))
    {
        actor.phase=phase;actor.clip_key=clip_key;actor.cursor=actor.decoded=0;
    }
    if(!_decode_budget)return;
    source_clip clipping{};
    if(clip>=0)clipping=actor_clip(_player.entity(entity),_player.entity(clip),graphic_assets[_player.entity(clip).image],asset.nominal_scale,int16_t(asset.color));
    int at=0,first=actor.decoded/32,end=bn::min(total/32,first+_decode_budget);
    for(int part=0;part<actor.part_indices.size() && at<end;++part)
    {
        const auto& p=asset.parts[actor.part_indices[part]];int count=p.width*p.height/64;
        for(int tile=0;tile<count && at<end;++tile,++at)
        {
            if(at<first)continue;
            auto& target=actor_pixels[_scratch_used+at-first];
            const auto& source=*reinterpret_cast<const bn::tile*>(graphics_bytes+p.tiles[tile]);
            if(clip<0)target=source;
            else for(int y=0;y<8;++y)
            {
                unsigned mask=clipping.mask(p.x+tile%(p.width/8)*8-asset.pivot_x,p.y+tile/(p.width/8)*8+y-asset.pivot_y);
                target.data[y]=source.data[y]&mask;
            }
        }
    }
    actor.scratch_first=first;actor.scratch_offset=_scratch_used;
    actor.decoded=end*32;_decode_budget-=end-first;_scratch_used+=end-first;
}
int image_scene::clip_for(int entity) const
{
    if(entity<0 || entity>=stage_overlay || !_player.entity(entity).maskable)return -1;
    int selected=-1;
    for(int id=stage_mask_first;id<stage_entity_count;++id)
    {
        const auto& mask=_player.entity(id);
        if(mask.image>=0 && mask.get(stage_property::opacity)>0 && (mask.order+1)/20==_player.entity(entity).order/20)selected=id;
    }
    return selected;
}
void image_scene::clip_background(int plane,int entity)
{
    auto& bg=_backgrounds[plane];int clip=clip_for(entity);
    auto window=plane ? bn::rect_window::internal() : bn::rect_window::external();
    if(clip<0)
    {
        bg.clip_hbe.reset();bg.clip_key=0;window.set_visible(false);
        if(bg.affine){bn::window::outside().set_show_bg(*bg.affine,true);bn::window::internal().set_show_bg(*bg.affine,true);bn::window::external().set_show_bg(*bg.affine,true);}
        else {bn::window::outside().set_show_bg(*bg.regular,true);bn::window::internal().set_show_bg(*bg.regular,true);bn::window::external().set_show_bg(*bg.regular,true);}
        return;
    }
    const auto& rectangle=_player.entity(clip);const auto& camera=_player.entity(stage_camera);
    int phase=bn::clamp(rectangle.get(stage_property::opacity)/16,0,16);
    uint32_t key=geometry_key(rectangle,camera,clip) ^ unsigned(!plane && _player.printer_visible()) ^
        (unsigned(rectangle.image+1)*16777619u) ^ (unsigned(phase)*2654435761u);
    if(bg.clip_hbe && bg.clip_key==key)return;
    const auto& shape=graphic_assets[rectangle.image];auto t=transform(rectangle,camera);
    point corners[4];
    for(int i=0;i<4;++i)
    {
        int x=(i==1 || i==2 ? shape.width : -shape.width)/2,y=(i>=2 ? shape.height : -shape.height)/2;
        corners[i]={t.center.x+x*t.c-y*t.s,t.center.y+x*t.s+y*t.c};
    }
    // The HBE consumes a converted register buffer. Build spans directly in
    // its existing source rows instead of duplicating 1,280 bytes on the stack.
    auto& rows=clip_rows[plane];
    for(auto& row:rows){row.first=240;row.second=0;}
    for(int edge=0;edge<4;++edge)
    {
        point a=corners[edge],b=corners[(edge+1)%4];if(a.y>b.y){point temp=a;a=b;b=temp;}
        if(a.y==b.y)continue;
        int first=bn::clamp((a.y+127)/256,0,160),end=bn::clamp((b.y+127)/256,0,160);
        int64_t step=int64_t(b.x-a.x)*65536/(b.y-a.y);
        int64_t x=int64_t(a.x)*256+int64_t(first*256+128-a.y)*step/256;
        for(int y=first;y<end;++y,x+=step)
        {
            int at=bn::clamp(int((x+32767)/65536),0,240);
            rows[y].first=bn::min(rows[y].first,bn::fixed(at));
            rows[y].second=bn::max(rows[y].second,bn::fixed(at));
        }
    }
    for(int y=0;y<160;++y)
    {
        // Partial opacity changes only the 160 scanline spans, never BG pixel data.
        auto& row=rows[y];
        if(bayer[y%4][0]>=phase || (!plane && _player.printer_visible() && y>=104)){row.first=0;row.second=240;}
        if(row.first>=row.second)row.first=row.second=0;
    }
    window.set_boundaries(-80,-120,80,-120);window.set_visible(true);
    if(bg.affine){bn::window::outside().set_show_bg(*bg.affine,false);bn::window::external().set_show_bg(*bg.affine,false);window.set_show_bg(*bg.affine,true);}
    else {bn::window::outside().set_show_bg(*bg.regular,false);bn::window::internal().set_show_bg(*bg.regular,false);window.set_show_bg(*bg.regular,true);}
    if(!bg.clip_hbe)bg.clip_hbe=bn::rect_window_boundaries_hbe_ptr::create_horizontal(window,bn::span<const bn::pair<bn::fixed,bn::fixed>>(clip_rows[plane],160));
    else bg.clip_hbe->reload_deltas_ref();
    bg.clip_key=key;
}

// Geometry admission is cold work: keep it out of the IWRAM pixel path.
__attribute__((noinline))
void image_scene::select_actor_cache(int* images,const uint32_t* viewport_keys)
{
#if defined(MANOSABA_DISABLE_PORTRAIT_CACHE) || \
    !(defined(MANOSABA_ACTOR_REGULAR) || defined(MANOSABA_ACTOR_SCALE_SNAP) || defined(MANOSABA_ACTOR_BAKED))
    return;
#endif
    uint32_t selection=2166136261u;
    for(int slot=0;slot<3;++slot)
    {
        int id=_actor_entities[slot];
        selection=(selection^viewport_keys[slot]^unsigned(images[slot]+2)^unsigned(!_player.entity(id).get(stage_property::tint))^
            (unsigned(_player.geometry_animating(id))<<30))*16777619u;
    }
    selection^=unsigned(_player.geometry_animating(stage_camera))<<29;
    if(selection==_selection_key)
        for(int slot=0;slot<3;++slot)images[slot]=_selected_actor_images[slot];
    else
    {
    // A fixed scale/angle can use a bake while the actor translates.
    int candidates[3]={-1,-1,-1};bool found=false;
    const auto& camera=_player.entity(stage_camera);
    for(int slot=0;slot<3;++slot)
    {
        int image=images[slot],id=_actor_entities[slot];
        if(image<0 || id>=stage_actor_count)continue;
        const auto& entity=_player.entity(id);
        if(!entity.get(stage_property::tint) || entity.image!=image)continue;
        bool enter=!_player.affine_animating(id) && !_player.affine_animating(stage_camera);int best_phase=513;
        int scale=entity.get(stage_property::scale)*camera.get(stage_property::scale)/256;
        int angle=(entity.get(stage_property::angle)+camera.get(stage_property::angle))%(360*256);
        if(angle>=180*256)angle-=360*256;
        if(angle< -180*256)angle+=360*256;
        auto center=camera_point(camera,entity.get(stage_property::x),entity.get(stage_property::y));
        uint32_t coverage=1;
        for(int value:{scale,angle,center.x>>11,center.y>>13})coverage=(coverage^unsigned(value))*16777619u;
        if(!coverage)coverage=1;
        for(int c=0;c<graphic_cache_entry_count;++c)
        {
            const auto& e=graphic_cache_entries[c];
            if(e.base!=image || e.scale!=scale || e.angle!=angle || (e.coverage_key && e.coverage_key!=coverage))continue;
            // Keep an existing bake through translations; switching the same
            // pose in/out on every fractional position would reload its tiles.
            if(e.image==_actors[slot].image){candidates[slot]=e.image;found=true;break;}
            int phase=bn::abs(int(int8_t(e.phase_x-(center.x&255))))+bn::abs(int(int8_t(e.phase_y-(center.y&255))));
            if(enter && phase<best_phase)
            {candidates[slot]=e.image;found=true;best_phase=phase;}
        }
    }
    if(found)
    {
    auto resident=[&](int slot,int image,int& parts)
    {
        int bytes=0;parts=0;if(image<0)return bytes;
        const auto& a=graphic_assets[image];int entity=_actor_entities[slot],clip=clip_for(entity);
        auto t=transform(_player.entity(entity),_player.entity(stage_camera),a.nominal_scale,int16_t(a.color));
        source_clip clipping{};
        if(clip>=0)clipping=actor_clip(_player.entity(entity),_player.entity(clip),graphic_assets[_player.entity(clip).image],a.nominal_scale,int16_t(a.color));
        for(int p=0;p<a.count;++p)if(part_needed(t,a,a.parts[p],clipping)){bytes+=a.parts[p].width*a.parts[p].height/2;++parts;}
        return bytes;
    };
    int resident_bytes=0,resident_parts=0,slot_bytes[3],slot_parts[3];
    for(int slot=0;slot<3;++slot){slot_bytes[slot]=resident(slot,images[slot],slot_parts[slot]);resident_bytes+=slot_bytes[slot];resident_parts+=slot_parts[slot];}
    for(int slot=0;slot<3;++slot)if(candidates[slot]>=0)
    {
        int part_count,bytes=resident(slot,candidates[slot],part_count);
        if(part_count<=64 && resident_bytes-slot_bytes[slot]+bytes<=32768 && resident_parts-slot_parts[slot]+part_count<=128)
        {images[slot]=candidates[slot];resident_bytes+=bytes-slot_bytes[slot];resident_parts+=part_count-slot_parts[slot];}
    }
    }
    _selection_key=selection;
    for(int slot=0;slot<3;++slot)_selected_actor_images[slot]=images[slot];
    }
}

#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void image_scene::prepare()
{
    int backgrounds[8],count=0;
    for(int id=3;id<stage_camera;++id)
    {
        int image=image_for(id);
        if(image>=0 && (graphic_assets[image].kind==graphic_kind::bg || graphic_assets[image].kind==graphic_kind::solid) && _player.entity(id).get(stage_property::opacity)>0)backgrounds[count++]=id;
    }
    for(int i=1;i<count;++i){int value=backgrounds[i],j=i;while(j && _player.entity(backgrounds[j-1]).order>_player.entity(value).order){backgrounds[j]=backgrounds[j-1];--j;}backgrounds[j]=value;}
    int wanted[2]={count>1 ? backgrounds[count-2] : -1,count ? backgrounds[count-1] : -1};
    int overlay=image_for(stage_overlay);const auto& overlay_state=_player.entity(stage_overlay);
    if(overlay>=0 && overlay_state.get(stage_property::opacity)>0)wanted[0]=stage_overlay;
    auto opaque_cover=[&](int id)
    {
        return id>=0 && _player.entity(id).get(stage_property::opacity)>=256 &&
            _player.entity(id).get(stage_property::reveal)>=256 && clip_for(id)<0 &&
            covers_screen(_player.entity(id),_player.entity(stage_camera),graphic_assets[image_for(id)]);
    };
    // Once the new static background is published and opaque, stop maintaining
    // the scenery behind it. The lower BG is then used only for the dialogue panel.
    bool top_covered=opaque_cover(wanted[1]);
    // Keep a hidden regular backend until actual scenery needs another renderer.
    if(!select_background_backend(wanted[1])){_loading=true;_ready=false;return;}
    if(wanted[0]!=stage_overlay && top_covered &&
       !_player.geometry_animating(wanted[1]) && !_player.geometry_animating(stage_camera) &&
       _backgrounds[1].image==image_for(wanted[1]))wanted[0]=-1;
    bool pending=false;
    for(int plane=0;plane<2;++plane)
    {
        auto& bg=_backgrounds[plane];int entity=wanted[plane];bg.entity=entity;int image=entity>=0 ? image_for(entity) : -1,variant=0;
        if(entity==stage_overlay)
        {
            if(overlay_state.maskable && overlay_state.mask>=0)
            {
                image=_bank->masks[overlay_state.mask];
                variant=bn::clamp(overlay_state.get(stage_property::opacity),0,256)*(graphic_assets[image].count-1)/256;
            }
            else variant=bn::clamp(overlay_state.get(stage_property::opacity)/16,0,16);
            _palette->set_color(224,bn::color(tinted(graphic_assets[overlay].color,overlay_state)));
        }
        else if(image>=0 && graphic_assets[image].kind==graphic_kind::bg)
        {
            const auto& asset=graphic_assets[image];
            if(plane)variant=bn::clamp(_player.entity(entity).get(stage_property::blur),0,256)*(asset.count-1)/256;
            else
            {
                int distance=0x7fffffff;variant=asset.count;
                for(int v=0;v<asset.base_count;++v){int delta=bn::abs(int(asset.variants[asset.count+v].scale)-_player.entity(entity).get(stage_property::scale));if(delta<distance){distance=delta;variant=asset.count+v;}}
            }
        }
        if(image!=bg.image || variant!=bg.variant)
        {
            if(image>=0 && (bg.target!=image || bg.target_variant!=variant) && bg.cursor==bg.bytes)
            {
                if(begin_background(plane,entity,image,variant)){pending=true;continue;}
            }
            if(bg.target>=0 && bg.decoded<bg.bytes)
            {
                const auto& data=graphic_assets[bg.target].variants[bg.target_variant];
                // Reuse the existing 16KiB staging buffer as a ring for larger dictionaries.
                int start=bg.decoded%16384;
                int end=bn::min(bg.bytes,bn::min(bg.decoded+2048,bg.cursor+16384));
                end=bn::min(end,bg.decoded+16384-start);
                auto* destination=reinterpret_cast<uint32_t*>(bg_pixels[plane])+start/4;
                for(int tile=bg.decoded/64;tile<end/64;++tile)
                {
                    unsigned reference=data.tiles[tile];
                    auto* source=reinterpret_cast<const uint32_t*>(graphics_bytes+(reference&0x7fffffff));
                    for(int word=0;word<16;++word)
                    {
                        unsigned value=source[word];
                        if(plane)
                        {
                            if(reference&0x80000000)value+=0x70707070;
                            else {unsigned converted=0;for(int byte=0;byte<4;++byte){unsigned c=(value>>(byte*8))&255;if(c)converted|=(c+112)<<(byte*8);}value=converted;}
                        }
                        *destination++=value;
                    }
                }
                bg.decoded=end;
            }
            pending|=image>=0;
        }
        if(image<0){bg.image=bg.target=-1;bg.bytes=bg.cursor=bg.decoded=0;bg.publish=false;}
    }
    int actor_images[3];_scene_proxy=false;
    bool covered[2]={opaque_cover(wanted[0]),top_covered};
    for(int slot=0;slot<3;++slot)
    {
        _actor_entities[slot]=slot;int image=image_for(slot);
        bool hidden=_player.entity(slot).get(stage_property::opacity)<=0;
        for(int plane=0;plane<2;++plane)
            hidden|=covered[plane] && _player.entity(slot).order<_player.entity(wanted[plane]).order;
        actor_images[slot]=image>=0 && graphic_assets[image].kind==graphic_kind::obj && !hidden ? image : -1;
    }
    // Three scenery layers occur in a few CG compositions. Use spare OBJ slots
    // for the covered lower artwork; this never adds a fourth character.
    if(wanted[0]!=stage_overlay && !covered[0] && !covered[1])
        for(int i=count-3;i>=0;--i)
        {
            int entity=backgrounds[i];const auto& asset=graphic_assets[image_for(entity)];
            if(!asset.frame_count)continue;
            int slot=0;while(slot<3 && actor_images[slot]>=0)++slot;
            if(slot==3)break;
            int blur=bn::clamp(_player.entity(entity).get(stage_property::blur),0,256)*(asset.frame_count-1)/256;
            _actor_entities[slot]=entity;actor_images[slot]=asset.frames[blur];_scene_proxy=true;
        }
    uint32_t clip_keys[3],viewport_keys[3];
    for(int slot=0;slot<3;++slot)
    {
        int id=_actor_entities[slot];auto keys=cache_keys(_player,id,clip_for(id),slot);
        clip_keys[slot]=keys.clip;viewport_keys[slot]=keys.viewport;
    }
    select_actor_cache(actor_images,viewport_keys);
    // Retire every obsolete image/block before any actor can allocate. Returning
    // yields a core update so Butano also reclaims its deferred VRAM blocks.
    bool retired=false;
    for(int slot=0;slot<3;++slot)retired|=retire_actor(slot,actor_images[slot],viewport_keys[slot]);
    if(retired){_loading=true;_ready=false;return;}
    for(int slot=0;slot<3;++slot)
    {
        int image=actor_images[slot];prepare_actor(slot,image,clip_keys[slot],viewport_keys[slot]);
        pending|=_actors[slot].allocating!=0;
        if(image>=0)
        {
            int bytes=0;for(const auto& p:_actors[slot].tiles)bytes+=p.tiles_count()*32;
            pending|=_actors[slot].allocating || _actors[slot].cursor<bytes;
        }
    }
    _loading=_loading && pending;_ready=!_loading;
}
void image_scene::positions()
{
    if(!_backgrounds[1].affine && !_backgrounds[1].regular)return;
    const auto& camera=_player.entity(stage_camera);
    int actor_alpha=256;
    int top_order=_backgrounds[1].entity>=0 ? _player.entity(_backgrounds[1].entity).order : -1;
    for(int slot=0;slot<3;++slot)
    {
        auto& actor=_actors[slot];if(actor.image<0 || !actor.matrix || actor.allocating==4)continue;
        const auto& entity=_player.entity(_actor_entities[slot]);const auto& asset=graphic_assets[actor.image];
        uint64_t material=material_key(entity);
        if(actor.material!=material)
        {
            for(int bank=0;bank<4;++bank)
            {
                bn::color colors[16];colors[0]=bn::color(0);
                for(int color=1;color<16;++color)colors[color]=bn::color(tinted(asset.palette[bank*16+color],entity));
                actor.palettes[bank].set_colors(bn::span<const bn::color>(colors,16));
            }
            actor.material=material;
        }
        int opacity=bn::clamp(entity.get(stage_property::opacity),0,256);
        if(opacity>0)actor_alpha=bn::min(actor_alpha,opacity);
        unsigned alpha_state=opacity<=0 ? 0 : opacity<256 ? 1 : 2;
        auto display_key=geometry_key(entity,camera,entity.order)^(unsigned(actor.published_parts)<<24)^(unsigned(top_order+1)<<16)^(unsigned(_scene_proxy)<<15)^
            (alpha_state*2654435761u);
        if(display_key==actor.display_key)continue;
        actor.display_key=display_key;
        int scale=entity.get(stage_property::scale)*camera.get(stage_property::scale)/256;
        if(asset.nominal_scale!=256)scale=scale*256/asset.nominal_scale;
        int angle=entity.get(stage_property::angle)+camera.get(stage_property::angle)-int16_t(asset.color);
#if defined(MANOSABA_ACTOR_REGULAR) || defined(MANOSABA_ACTOR_SCALE_SNAP) || defined(MANOSABA_ACTOR_BAKED)
        bool affine=scale!=256 || angle%(360*256)!=0;
#endif
        actor.matrix->set_scale(fx(bn::max(1,scale)));
        actor.matrix->set_rotation_angle_safe(-fx(angle));
        auto placement=transform(entity,camera,asset.nominal_scale,int16_t(asset.color));
        for(int i=0;i<actor.sprites.size();++i)
        {
#if defined(MANOSABA_ACTOR_REGULAR) || defined(MANOSABA_ACTOR_SCALE_SNAP) || defined(MANOSABA_ACTOR_BAKED)
            if(affine)
            {
                if(!actor.sprites[i].affine_mat())actor.sprites[i].set_affine_mat(*actor.matrix);
            }
            else if(actor.sprites[i].affine_mat())actor.sprites[i].remove_affine_mat();
#endif
            auto point=placement.part(asset,asset.parts[actor.part_indices[i]]);
            actor.sprites[i].set_position(fx(point.x-120*256),fx(point.y-80*256));
            int priority=top_order>entity.order ? 3 : (_scene_proxy ? 1 : 2);
            if(_scene_proxy && _backgrounds[0].entity>=0 && entity.order>=_player.entity(_backgrounds[0].entity).order && entity.order<top_order)priority=2;
            actor.sprites[i].set_bg_priority(priority);
            actor.sprites[i].set_z_order(-entity.order);
            actor.sprites[i].set_blending_enabled(opacity<256);
            actor.sprites[i].set_visible(_visible && i<actor.published_parts && entity.get(stage_property::opacity)>0);
        }
    }
    int top_entity=_backgrounds[1].entity,lower_entity=_backgrounds[0].entity;
    int alpha=actor_alpha;
    for(int plane=0;plane<2;++plane)
    {
        int entity=plane ? top_entity : lower_entity;
        if(entity<0 || _backgrounds[plane].image<0 || entity==stage_overlay)continue;
        auto& bg=_backgrounds[plane];const auto& asset=graphic_assets[bg.image];uint64_t material=material_key(_player.entity(entity));
        if(bg.material!=material)
        {
            if(asset.kind==graphic_kind::solid)_palette->set_color(plane*112+1,bn::color(tinted(asset.color,_player.entity(entity))));
            else if(asset.kind==graphic_kind::bg)for(int color=1;color<112;++color)_palette->set_color(plane*112+color,bn::color(tinted(asset.palette[color],_player.entity(entity))));
            bg.material=material;
        }
    }
    auto& upper=_backgrounds[1];
    if(top_entity>=0 && upper.image>=0)
    {
        const auto& entity=_player.entity(top_entity);auto point=camera_point(camera,entity.get(stage_property::x),entity.get(stage_property::y));
        bool settled=upper.image==upper.target && upper.variant==upper.target_variant;
        if(upper.regular)
        {
            regular_position(upper.image,point.x,point.y);
            upper.regular->set_priority(_scene_proxy ? 1 : 2);
            upper.regular->set_blending_top_enabled(entity.get(stage_property::opacity)<256);
            upper.regular->set_visible(_visible && settled);
        }
        else
        {
            auto& bg=*upper.affine;bg.set_priority(_scene_proxy ? 1 : 2);bg.set_position(fx(point.x-120*256),fx(point.y-80*256));
            bg.set_scale(fx(entity.get(stage_property::scale)*camera.get(stage_property::scale)/256));
            bg.set_rotation_angle_safe(-fx(entity.get(stage_property::angle)+camera.get(stage_property::angle)));
            bg.set_visible(_visible && (_double_buffer || settled));bg.set_blending_top_enabled(entity.get(stage_property::opacity)<256);
        }
        alpha=bn::min(alpha,bn::clamp(entity.get(stage_property::opacity),0,256));
    }
    else {if(upper.affine)upper.affine->set_visible(false);if(upper.regular)upper.regular->set_visible(false);}
    auto& lower=*_backgrounds[0].regular;
    // The panel replaces this BG below scanline 104. Keep a full-screen
    // overlay's own map and scroll there too, or it reveals the old scenery.
    bool printer=_player.printer_visible() && lower_entity!=stage_overlay;
    int lower_priority=3;
    if(_backgrounds[0].image>=0)
    {
        bool mask=_backgrounds[0].entity==stage_overlay;
        lower_priority=mask ? 1 : _scene_proxy ? 2 : 3;
        lower.set_priority(lower_priority);
        if(mask)lower.set_position(8,48);
        else if(lower_entity>=0)lower.set_position(fx(_player.entity(lower_entity).get(stage_property::x)-120*256),fx(_player.entity(lower_entity).get(stage_property::y)-80*256));
        lower.set_visible(_visible);
    }
    else {lower.set_position(8,48);lower.set_priority(3);lower.set_visible(_visible && printer);}
    clip_background(0,_backgrounds[0].entity);clip_background(1,_backgrounds[1].entity);
    if(printer)bn::window::internal().set_show_bg(lower,true);
    update_panel(printer,lower_priority);
    // Both BLDALPHA coefficients are identical within each Q8/16 step.
    int alpha_step=alpha/16;
    if(alpha_step!=_last_alpha || printer!=_last_printer)
    {
        if(printer)
        {
            for(int y=0;y<160;++y)alpha_rows[y].set_transparency_alpha(fx(y>=104 ? 160 : alpha));
            if(_alpha_hbe)_alpha_hbe->reload_attributes_ref();
            else _alpha_hbe=bn::blending_transparency_attributes_hbe_ptr::create(bn::span<const bn::blending_transparency_attributes>(alpha_rows,160));
        }
        else
        {
            _alpha_hbe.reset();
            bn::blending::set_transparency_alpha(fx(printer ? 160 : alpha));
        }
        _last_alpha=alpha_step;_last_printer=printer;
    }

}
void image_scene::render(const text_canvas&)
{
    if(! _visible || ! _bank || _frame<0 || _allocate_delay)return;
    unsigned start=unsigned(bn::core::current_cpu_ticks());
    if(!_prepared)allocate();
    else
    {
        _allocation_budget=4;_decode_budget=_loading ? 32 : 64;
        for(const auto& actor:_actors)if(actor.cursor<actor.decoded)_decode_budget=0;
        if(_decode_budget)_scratch_used=0;
        prepare();positions();
    }
    graphics_performance.loading=_loading;graphics_performance.clock=_player.clock();
    unsigned ticks=unsigned(bn::core::current_cpu_ticks())-start;
    graphics_performance.cpu_ticks=ticks;if(ticks>graphics_performance.max_cpu_ticks)graphics_performance.max_cpu_ticks=ticks;
    graphics_performance.missed_frames+=unsigned(bn::core::last_missed_frames());++graphics_performance.frames;
}
bool image_scene::has_content() const
{
    bool moving=_player.animating();
    for(int id=0;id<stage_camera;++id)
        if(_player.entity(id).image>=0 && (_player.entity(id).get(stage_property::opacity)>0 || moving))return true;
    return false;
}
void image_scene::tint(palette_effect effect)
{
    auto color=bn::color(effect.color);auto amount=bn::fixed::from_data(effect.amount*16);
    if(_palette)_palette->set_fade(color,amount);
    for(auto& actor:_actors)for(auto& palette:actor.palettes)palette.set_fade(color,amount);
}
void image_scene::dim(bool enabled)
{
    if(!_palette || enabled==_dim)return;
    _dim=enabled;bn::fixed amount=enabled ? bn::fixed(0.5) : bn::fixed(0);
    _palette->set_fade_color(bn::color(0,0,0));_palette->set_fade_intensity(amount);
    for(auto& actor:_actors)for(auto& palette:actor.palettes)
    {palette.set_fade_color(bn::color(0,0,0));palette.set_fade_intensity(amount);}
}
int image_scene::commit(int bytes)
{
    if(! _visible || ! _prepared)return bytes;
    for(int plane=0;plane<2;++plane)
    {
        auto& bg=_backgrounds[plane];
        if(bg.regular && bg.map_pending && bytes>=2048)
        {
            auto owner=bg.regular->map();auto map=owner.vram().value();
            const auto* cells=plane ? reinterpret_cast<const uint16_t*>(top_cells) : lower_cells;
            bn::memory::copy(cells[0],1024,map[0]);bytes-=2048;bg.map_pending=false;
        }
        if(!plane && bg.target>=0 && (graphic_assets[bg.target].kind==graphic_kind::mask || graphic_assets[bg.target].kind==graphic_kind::solid) && bg.cursor<bg.bytes)
        {
            bool columns=graphic_assets[bg.target].kind==graphic_kind::mask && !graphic_assets[bg.target].variants[bg.target_variant].map;
            bool same_columns=columns && bg.image==bg.target && !graphic_assets[bg.image].variants[bg.variant].map;
            int map_bytes=same_columns ? 0 : 2048;
            if(bg.decoded<bg.bytes || bytes<bg.bytes+map_bytes)continue;
            auto destination=bg.regular_tiles->vram().value();
            if(same_columns)
            {
                const auto* previous=graphic_assets[bg.image].variants[bg.variant].tiles;
                const auto* next=graphic_assets[bg.target].variants[bg.target_variant].tiles;
                for(int tile=0;tile<bg.bytes/64;++tile)if(previous[tile]!=next[tile])
                {bn::memory::copy(bg_pixels[0][tile*2],2,destination[tile*2]);bytes-=64;}
            }
            else {bn::memory::copy(bg_pixels[0][0],bg.bytes/32,destination[0]);bytes-=bg.bytes;}
            if(graphic_assets[bg.target].kind==graphic_kind::solid)
            {
                std::memset(lower_cells,0,sizeof(lower_cells));
                if(bg.entity!=stage_overlay)for(int y=6;y<26;++y)for(int x=1;x<31;++x)lower_cells[y*32+x]=1;
            }
            else if(columns && !same_columns)
            {
                std::memset(lower_cells,0,sizeof(lower_cells));
                for(int y=0;y<20;++y)for(int x=0;x<30;++x)lower_cells[y*32+x]=x+1;
            }
            else if(!columns)
            {
                const auto& variant=graphic_assets[bg.target].variants[bg.target_variant];
                std::memcpy(lower_cells,variant.map,sizeof(lower_cells));
            }
            if(map_bytes)
            {
                auto owner=bg.regular->map();auto map=owner.vram().value();
                bn::memory::copy(lower_cells[0],1024,map[0]);bytes-=2048;
            }
            bg.cursor=bg.bytes;bg.image=bg.target;bg.variant=bg.target_variant;
            continue;
        }
        int available=bn::min(bytes/32,(bg.decoded-bg.cursor)/32);
        available=bn::min(available,(16384-bg.cursor%16384)/32);
        if(available>0)
        {
            auto destination=bg.regular ? bg.regular_tiles->vram().value() : bg.affine_tiles[bg.front^1]->vram().value();
            bn::memory::copy(bg_pixels[plane][(bg.cursor%16384)/32],available,destination[bg.cursor/32]);
            bg.cursor+=available*32;bytes-=available*32;
            if(bg.cursor==bg.bytes)bg.publish=true;
        }
    }
    for(int slot=0;slot<3;++slot)
    {
        auto& actor=_actors[slot];if(actor.allocating || actor.cursor>=actor.decoded)continue;int offset=0;
        for(int part=0;part<actor.tiles.size();++part)
        {
            int size=actor.tiles[part].tiles_count()*32;
            if(actor.cursor<offset+size && actor.cursor<actor.decoded && bytes>=32)
            {
                int begin=bn::max(actor.cursor-offset,0);int count=bn::min(bytes/32,bn::min(size-begin,actor.decoded-actor.cursor)/32);
                auto destination=actor.tiles[part].vram().value();
                bn::memory::copy(actor_pixels[actor.scratch_offset+(offset+begin)/32-actor.scratch_first],count,destination[begin/32]);
                actor.cursor=offset+begin+count*32;bytes-=count*32;
            }
            offset+=size;
            if(actor.cursor>=offset)actor.published_parts=bn::max(actor.published_parts,part+1);
        }
    }
    return bytes;
}
}
