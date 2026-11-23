#include "video_upload.h"
#include "trial_scene.h"
#include "trial_data.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_bg_palette_item.h"
#include "bn_memory.h"
#include "bn_tile.h"
#include "bn_size.h"
#include "bn_hdma.h"
#include "court_heat.h"
#include <cstring>
namespace manosaba {
void trial_scene::protect() const
{if(_bg)video_upload::protect(*_bg);}

namespace {
BN_DATA_EWRAM_BSS bn::regular_bg_map_cell trial_cells[1024];
alignas(4) BN_DATA_EWRAM_BSS uint16_t heat_rows[2][160];
constexpr int rows=20,column_bytes=rows*32,view_bytes=32*column_bytes;
int rounded(int value,int divisor){return value>=0 ? (value+divisor/2)/divisor : -((-value+divisor/2)/divisor);}
int image(int actor,const uint16_t* poses){return poses && poses[actor]!=65535 ? poses[actor] : trial_actor_images[actor];}
}
void trial_scene::init()
{
    _clearing=false;
    _tiles[0]=bn::regular_bg_tiles_ptr::allocate(640,bn::bpp_mode::BPP_4,true);
    _tiles[1]=bn::regular_bg_tiles_ptr::allocate(640,bn::bpp_mode::BPP_4,true);
    _palette=bn::bg_palette_ptr::create(bn::bg_palette_item(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(trial_bg_palette),240),bn::bpp_mode::BPP_4));
    _bg=bn::regular_bg_ptr::create(8,48,bn::regular_bg_map_ptr::allocate(bn::size(32,32),*_tiles[0],*_palette));
    _bg->set_priority(2);_bg->set_visible(false);
    _bg->set_blending_bottom_enabled(true);
    for(int a=0;a<3;++a)_assets[a]=graphic_assets[trial_demo_images[a]];
    _background_only=_pan=_pan_reload=false;_motion=-1;_pan_generation=0;
    _view=_target=_key=_published=-1;_front=0;_published_bank=0;_queued=-1;_map_pending=false;
    _compound=_formal_reload=_horizontal=_load_horizontal=_tracking=false;_vertical=_offset_x=_offset_y=0;_source_row=_load_row=1;_scale=256;
}
void trial_scene::reset()
{
    heat(false,false);
    _cast.reset();_bg.reset();_palette.reset();for(auto& tile:_tiles)tile.reset();_key=_published=_view=_target=-1;_clearing=_map_pending=false;
}
bool trial_scene::release()
{
    if(!_bg)return true;
    _clearing=true;
    if(_cast && !_cast->clear_step())return false;
    reset();return true;
}
void trial_scene::visible(bool enabled)
{
    if(!enabled)heat(false,false);
    if(_bg){_bg->set_visible(enabled && _view>=0);if(_cast)_cast->visible(enabled && !_background_only);}
}
void trial_scene::tint(palette_effect effect)
{
    auto background=_heat ? effect_over({court_heat_color,court_heat_amount},effect) : effect;
    if(_palette)_palette->set_fade(bn::color(background.color),bn::fixed::from_data(background.amount*16));
    if(_cast)_cast->tint(effect);
}
void trial_scene::heat(bool enabled,bool animate)
{
    _heat=enabled && _bg && _bg->visible() && _view>=0;
    if(!_heat)
    {
        if(_heat_dma){bn::hdma::stop();_heat_dma=false;}
        return;
    }
    auto id=_bg->hw_id();if(!id)return;
    if(animate)_heat_phase=uint8_t((_heat_phase+1)&63);
    _heat_front^=1;auto* rows=heat_rows[_heat_front];
    court_heat_rows(rows,8-_bg->x().right_shift_integer(),_heat_phase);
    auto* target=reinterpret_cast<uint16_t*>(0x04000010+4*id.value());
    // Low-priority DMA3: music FIFO on DMA1/2 wins arbitration. Butano disables
    // HDMA for VBlank uploads, then rearms it after committing the BG registers.
    bn::hdma::start(bn::span<const uint16_t>(rows,160),*target);_heat_dma=true;
}
void trial_scene::shake(int x,int y)
{
    if(x==_shake_x && y==_shake_y)return;
    _shake_x=int8_t(x);_shake_y=int8_t(y);
    if(_bg && _view>=0)position();
}
bool trial_scene::release_actors()
{
    if(_cast && !_cast->clear_step())return false;
    _cast.reset();return true;
}
void trial_scene::request_background(int view,bool demo,int layout)
{
    _background_only=true;_pan=_pan_reload=_compound=_tracking=false;_motion=-1;_demo=demo;
    if(_view>=0 && _view==_target){_key=300+_view;return;}
    int stage=trial_camera_layouts[layout].background-7;
    int background=!demo && view>=4 ? 9+stage*9+view-4 : !demo && view<3 ? view+4 : view;
    _key=300+background;
    if(background!=_target){_target=background;_cursor=0;_start=view==3 ? 48 : 0;}
}
void trial_scene::placement(int slot,const trial_actor_placement& value)
{
    const auto& asset=_assets[slot];auto& target=_positions[_cast_bank][slot];target=value;
    auto* pieces=_pose_pieces[_cast_bank][slot];target.pieces=pieces;target.count=0;
    int bottom=bn::min(int(asset.height),int(value.limit));
    // Split only the visible rectangle of each existing OBJ part; no second image.
    static constexpr uint8_t shapes[][2]={{8,8},{8,4},{4,8},{4,4},{4,2},{2,4},{2,2},{4,1},{1,4},{2,1},{1,2},{1,1}};
    for(int part=0;part<asset.count;++part)
    {
        const auto& source=asset.parts[part];int width=source.width/8,height=bn::min(int(source.height)/8,(bottom-source.y+7)/8);
        for(int y=0;y<height;)
        {
            int step=1;
            for(int x=0;x<width;)
            {
                for(const auto& shape:shapes)if(x+shape[0]<=width && y+shape[1]<=height)
                {pieces[target.count++]={uint8_t(part),uint8_t(x),uint8_t(y),shape[0],shape[1]};x+=shape[0];step=shape[1];break;}
            }
            y+=step;
        }
    }
}
void trial_scene::request(int actor,int view,bool demo,int layout,const uint16_t* poses,int reserve_scale,int source_row,bool horizontal)
{
    if(!_cast){_cast.emplace(_assets,graphics_bytes);_key=_published=-1;}
    bool was_pan=_pan;
    _background_only=_pan=_pan_reload=_compound=_tracking=false;_motion=-1;_vertical=_offset_x=_offset_y=0;_scale=256;
    int actor_image=actor==255 ? trial_actor_images[0] : image(actor,poses);
    int style=int(actor==255)|int(reserve_scale!=256)<<1|int(source_row==2)<<2|int(horizontal)<<3;
    int key=demo ? 100+view : 1000000+(actor_image+graphic_asset_count*(view+13*layout))*16+style;
    if(!was_pan && key==_key)return;
    _key=key;_demo=demo;
    int stage=trial_camera_layouts[layout].background-7;
    int background=!demo && view>=4 ? 9+stage*9+view-4 : !demo && view<3 ? view+4 : view;
    if(background!=_target || (background>=9 && (source_row!=_load_row || horizontal!=_load_horizontal)))
    {_formal_reload=background>=9 && background==_view;_target=background;_cursor=0;_start=horizontal ? bn::clamp(_horizontal_x/8-1,0,int(trial_backgrounds[background].columns)-32) : view==3 ? 48 : 0;}
    _load_row=source_row;_load_horizontal=horizontal;
    _cast_bank=_published_bank^1;auto* positions=_positions[_cast_bank];
    if(demo)
    {
        for(int a=0;a<3;++a){_assets[a]=graphic_assets[trial_demo_images[a]];positions[a]=trial_demo_positions[view][a];}
    }
    else
    {
        _assets[0]=graphic_assets[actor_image];
        if(actor==255)positions[0]=trial_empty_position;
        else if(view>=4)
        {
            const auto& source=trial_view_positions[stage][view-4];
            int scale=bn::min(512,source.scale*300/_assets[0].height);
            int limit=(source.cutoff-source.y)*256*256/(scale*reserve_scale)+_assets[0].pivot_y;
            placement(0,{nullptr,nullptr,source.x,int16_t(source.y),uint16_t(scale),0,int16_t(limit)});
        }
        else if(!poses)positions[0]=trial_actor_positions[actor][view];
        else
        {
            auto source=trial_actor_positions[actor][view];const auto& base=graphic_assets[trial_actor_images[actor]];
            source.limit=source.bottom[base.width/2]+_assets[0].pivot_y-base.pivot_y;source.bottom=nullptr;placement(0,source);
        }
        for(int a=1;a<3;++a){_assets[a]=graphic_assets[trial_demo_images[a]];positions[a]=trial_empty_position;}
    }
    _cast->request(key,positions,_assets);
}
void trial_scene::request_pan(int motion,unsigned frame,bool witch_hiro,const uint16_t* poses,int actor)
{
    const auto& camera=trial_camera_motions[motion];const auto& layout=trial_camera_layouts[camera.layout];
    if((camera.mode==1 || camera.mode==2) && frame>=camera.frames)
    {
        actor=camera.actor;if(actor==1 && witch_hiro)actor=16;
        request(actor,camera.end_view,false,camera.layout,poses);_tracking=true;_track_x=camera.to;return;
    }
    if(camera.mode==3)
    {
        actor=camera.actor;if(actor==1 && witch_hiro)actor=16;
        int progress=int(frame)*4096/camera.frames;
        if(camera.ease)progress=4096-(4096-progress)*(4096-progress)/4096;
        int delta=bn::clamp((camera.to-camera.from)*298/326,-32,32);
        _horizontal_x=40+delta*progress/4096;
        request(actor,camera.start_view,false,camera.layout,poses,256,2,true);
        _tracking=true;_track_x=camera.from+(camera.to-camera.from)*progress/4096;
        return;
    }
    if(camera.mode==1)
    {
        actor=camera.actor;if(actor==1 && witch_hiro)actor=16;
        int stage=layout.background-7;
        const auto& from=trial_view_positions[stage][camera.start_view-4];const auto& to=trial_view_positions[stage][camera.end_view-4];
        int end_scale=to.scale*256/from.scale;
        request(actor,camera.start_view,false,camera.layout,poses,bn::min(256,end_scale),camera.rise<0 ? 2 : 1);
        _compound=true;int progress=int(frame)*4096/camera.frames;
        // SmoothStep is the inexpensive approximation of the source sine easing.
        progress=progress*progress/4096*(12288-2*progress)/4096;
        _vertical=camera.rise*progress/4096;
        _offset_x=(to.x-from.x)*progress/4096;_offset_y=(to.y-from.y)*progress/4096;
        _scale=256+(end_scale-256)*progress/4096;
        _tracking=true;_track_x=camera.from+(camera.to-camera.from)*progress/4096;
        return;
    }
    if(!_cast){_cast.emplace(_assets,graphics_bytes);_key=_published=-1;}
    _pan=true;_demo=_background_only=_compound=_tracking=false;_vertical=_offset_x=_offset_y=0;_scale=256;_load_horizontal=false;
    int progress=int(frame)*4096/camera.frames;
    if(camera.ease==1)progress=4096-(4096-progress)*(4096-progress)/4096;
    else if(camera.ease==2)progress=progress*progress/4096*(12288-2*progress)/4096;
    _pan_x=camera.from+(camera.to-camera.from)*progress/4096;
    if(motion!=_motion || frame<_motion_frame)
    {_motion=motion;_target=layout.background;_cursor=0;_start=_pan_x/8;_pan_reload=true;_published=-1;_pan_generation^=1;}
    _motion_frame=frame;
    int center=rounded((_pan_x+120-1024)*layout.seats,2048);
    _cast_bank=_published_bank^1;auto* positions=_positions[_cast_bank];
    int images[3]={-1,-1,-1};
    for(int seat=center-1;seat<=center+1;++seat)
    {int slot=(seat%3+3)%3,who=layout.actors[(seat%layout.seats+layout.seats)%layout.seats];if(who==1 && witch_hiro)who=16;if(who!=255)images[slot]=image(who,poses);}
    for(int slot=0;slot<3;++slot)if(images[slot]!=_pose_images[slot]){_pose_images[slot]=images[slot];++_pose_revision;}
    int key=400+motion*128+_pan_generation*64+(center+16)*2+int(witch_hiro)+_pose_revision*16384;if(key==_key)return;
    _key=key;
    for(int seat=center-1;seat<=center+1;++seat)
    {
        int slot=(seat%3+3)%3,who=layout.actors[(seat%layout.seats+layout.seats)%layout.seats];
        if(who==255){positions[slot]=trial_empty_position;continue;}
        if(who==1 && witch_hiro)who=16;
        _assets[slot]=graphic_assets[images[slot]];positions[slot]=trial_pan_positions[layout.background-7][who];
        if(poses)
        {
            auto source=positions[slot];const auto& base=graphic_assets[trial_actor_images[who]];
            source.limit=source.bottom[base.width/2]+_assets[slot].pivot_y-base.pivot_y;source.bottom=nullptr;placement(slot,source);
        }
        positions[slot].x+=rounded(seat*2048,layout.seats);
    }
    _cast->request(key,positions,_assets);
}
void trial_scene::column(int physical,int source)
{
    for(int row=0;row<rows;++row)trial_cells[row*32+physical]=uint16_t(physical*rows+row)|(trial_backgrounds[_view].banks[(_view>=7 ? source&255 : source)*24+row+2]<<12);
}
void trial_scene::position(){bool flat=_view>=9 && !_horizontal;_bg->set_position((flat ? 8 : 8-((_x-_origin*8)&255))+_shake_x,(flat ? 48-(2-_source_row)*8+_vertical : 48)+_shake_y);}
void trial_scene::update()
{
    if(_clearing)return;
    if(_cast && !_background_only)_cast->prepare();
    if(_cursor==target_bytes() && (_background_only || _cast->ready(_key)) && _published!=_key)
    {
        if(_view!=_target || _pan_reload || _formal_reload)
        {
            _pan_reload=_formal_reload=false;_source_row=_load_row;_horizontal=_load_horizontal;
            _front^=1;_view=_target;_origin=_first=_start;_x=_view>=9 && !_horizontal ? 40 : _start*8+8;_queued=-1;
            std::memset(trial_cells,0,sizeof(trial_cells));
            if(_view>=9 && !_horizontal)
            {
                for(int y=0;y<21;++y)
                {
                    for(int x=0;x<30;++x)trial_cells[y*32+x]=uint16_t(y*30+x)|(trial_backgrounds[_view].banks[(x+5)*24+y+_source_row]<<12);
                    // Mirror existing edge tiles into unused map columns: a 1–2px
                    // heat/shake offset exposes no black strip and needs no tiles.
                    trial_cells[y*32+30]=trial_cells[y*32+29]^0x400;
                    trial_cells[y*32+31]=trial_cells[y*32]^0x400;
                }
            }
            else for(int c=0;c<32;++c)column(c,_start+c);
            _bg->set_tiles(*_tiles[_front]);_map_pending=true;_bg->set_visible(true);position();
        }
        if(!_background_only){_cast->publish(_key);_published_bank=_cast_bank;}
        _published=_key;
    }
    if(!_background_only && _demo && _view==3 && _published==_key)
    {
        if(_x>=776)_direction=-1;
        if(_x<=8)_direction=1;
        _x+=_direction;
        int wanted=bn::clamp(_x/8-(_direction<0),0,int(trial_backgrounds[3].columns)-32);
        if(wanted>_first){_queued=_first+32;++_first;}
        else if(wanted<_first){--_first;_queued=_first;}
        position();
    }
    if(_pan && _view==_target && !_pan_reload)
    {
        int wanted=_pan_x/8-(_pan_x<_x);
        if(wanted>_first){_queued=_first+32;++_first;}
        else if(wanted<_first){--_first;_queued=_first;}
        _x=_pan_x;position();
    }
    if(_horizontal && _view==_target && _tracking && !_formal_reload && !_background_only)
    {
        int wanted=bn::clamp(_horizontal_x/8-(_horizontal_x<_x),0,int(trial_backgrounds[_view].columns)-32);
        if(wanted>_first){_queued=_first+32;++_first;}
        else if(wanted<_first){--_first;_queued=_first;}
        _x=_horizontal_x;
    }
    if(_view>=9)position();
    if(_cast && !_background_only)
    {
        int scale=_compound && _published>=0 ? bn::min(_scale,512*256/_positions[_published_bank][0].scale) : _scale;
        _cast->transform(_offset_x+_shake_x+_actor_shake_x,_offset_y+_shake_y+_actor_shake_y,scale);_cast->position(_x,_demo || _pan ? 3 : 1);
    }
}
int trial_scene::commit(int bytes)
{
    if(!_bg || _clearing)return bytes;
    int available=bn::min(bytes,2048),used=0;
    if(_map_pending && available>=2048)
    {
        auto map=_bg->map();auto vram=map.vram().value();int offset=map.tiles_offset(),bank=map.palette_banks_offset();
        for(int i=0;i<1024;++i){unsigned cell=trial_cells[i];vram[i]=((cell>>12)+bank)<<12|(cell&0xc00)|((cell&1023)+offset);}
        _map_pending=false;available-=2048;used+=2048;
    }
    if(_queued>=0 && available>=column_bytes+rows*2)
    {
        int col=(_queued-_origin)&31;auto dest=_tiles[_front]->vram().value();
        auto* source=reinterpret_cast<const bn::tile*>(trial_backgrounds[_view].tiles+(((_view>=7 ? _queued&255 : _queued)*768+64)/4));
        bn::memory::copy(*source,rows,dest[col*rows]);column(col,_queued);
        auto map=_bg->map();auto vram=map.vram().value();int offset=map.tiles_offset(),bank=map.palette_banks_offset();
        for(int row=0;row<rows;++row){unsigned cell=trial_cells[row*32+col];vram[row*32+col]=((cell>>12)+bank)<<12|((cell&1023)+offset);}
        used+=column_bytes+rows*2;available-=column_bytes+rows*2;_queued=-1;
    }
    while(_target>=0 && _cursor<target_bytes() && available>=32)
    {
        if(_target>=9 && !_load_horizontal)
        {
            auto dest=_tiles[_front^1]->vram().value();int tile=_cursor/32,x=tile%30,y=tile/30;
            auto* source=reinterpret_cast<const bn::tile*>(trial_backgrounds[_target].tiles+((x+5)*768+(y+_load_row)*32)/4);
            bn::memory::copy(*source,1,dest[tile]);_cursor+=32;available-=32;used+=32;continue;
        }
        auto dest=_tiles[_front^1]->vram().value();int col=_cursor/column_bytes,within=_cursor%column_bytes;
        int amount=bn::min(available/32*32,column_bytes-within);
        auto* source=reinterpret_cast<const bn::tile*>(trial_backgrounds[_target].tiles+(((_target>=7 ? (_start+col)&255 : _start+col)*768+64+within)/4));
        bn::memory::copy(*source,amount/32,dest[_cursor/32]);_cursor+=amount;available-=amount;used+=amount;
    }
    if(_cast && !_background_only){int remaining=_cast->commit(available);used+=available-remaining;}
    return bytes-used;
}
}
