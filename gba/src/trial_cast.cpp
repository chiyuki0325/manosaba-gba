#include "trial_cast.h"
#include "trial_alignment.h"
#include "bn_sprite_builder.h"
#include "bn_sprite_shape_size.h"
#include "bn_sprite_palette_item.h"
#include "bn_memory.h"
#include "bn_tile.h"
namespace manosaba
{
void trial_cast::tint(palette_effect effect)
{
    for(auto& palette:_palettes)palette.set_fade(bn::color(effect.color),bn::fixed::from_data(effect.amount*16));
}
trial_cast::trial_cast(const graphic_asset* assets,const uint8_t* bytes,int actors,int priority,int z_order) :
    _assets(assets),_bytes(bytes),_actors(actors),_priority(priority),_z_order(z_order)
{
    for(int actor=0;actor<_actors;++actor)
        for(int bank=0;bank<4;++bank)
            _palettes.push_back(bn::sprite_palette_ptr::create_new(bn::sprite_palette_item(
                bn::span<const bn::color>(reinterpret_cast<const bn::color*>(assets[actor].palette+bank*16),16),bn::bpp_mode::BPP_4)));
}
void trial_cast::request(int view,const trial_actor_placement* placement)
{
    request(view,placement,_assets);
}
void trial_cast::request(int view,const trial_actor_placement* placement,const graphic_asset* assets)
{
    if(view==_target)return;
    _target=view;_actor=_part=_upload_part=_upload_tile=_scratch_count=_scratch_read=0;_allocated=_ready=false;
    auto& back=_buffers[_front^1];if(!_retiring)back.clear();back.placement=placement;
    for(int a=0;a<3;++a)back.assets[a]=assets[a];
    for(int a=0;a<_actors;++a)
    {
        auto& matrix=_matrix[_front^1][a];
        if(!matrix)matrix=bn::sprite_affine_mat_ptr::create();
        matrix->set_scale(bn::fixed::from_data(placement[a].scale*16));
    }
}
void trial_cast::prepare()
{
    if(_retiring)
    {
        auto& old=_buffers[_front^1];int budget=4;
        while(!old.pieces.empty() && budget--){old.sprites.pop_back();old.tiles.pop_back();old.pieces.pop_back();}
        _retiring=!old.pieces.empty();return;
    }
    if(_ready || _target<0)return;
    auto& back=_buffers[_front^1];int budget=4;
    while(!_allocated && _actor<_actors && budget)
    {
        const auto& asset=back.assets[_actor];const auto& placement=back.placement[_actor];
        while(_part<placement.count && budget)
        {
            int index=_part++;const auto& piece=placement.pieces[index];const auto& p=asset.parts[piece.part];
            const auto& front=_buffers[_front];int shared=-1;
            for(int i=0;i<front.pieces.size();++i)
            {
                auto old=front.pieces[i];if(old.actor!=_actor)continue;
                if(front.assets[_actor].parts!=asset.parts)continue;
                const auto& before=front.placement[_actor];const auto& shape=before.pieces[old.index];
                if(shape.part!=piece.part || shape.x!=piece.x || shape.y!=piece.y || shape.width!=piece.width || shape.height!=piece.height)continue;
                bool equal=true;int top=p.y+piece.y*8,bottom=top+piece.height*8;
                for(int x=p.x+piece.x*8;x<bn::min(int(asset.width),p.x+(piece.x+piece.width)*8);++x)
                    if(bn::clamp(before.cutoff(x),top,bottom)!=bn::clamp(placement.cutoff(x),top,bottom)){equal=false;break;}
                if(equal){shared=i;break;}
            }
            back.pieces.push_back({uint8_t(_actor),uint8_t(index),shared>=0});
            if(shared>=0)back.tiles.push_back(front.tiles[shared]);
            else back.tiles.push_back(bn::sprite_tiles_ptr::allocate(piece.width*piece.height,bn::bpp_mode::BPP_4));
            bn::sprite_builder builder(bn::sprite_shape_size(piece.width*8,piece.height*8),back.tiles.back(),_palettes[_actor*4+p.bank]);
            builder.set_affine_mat(*_matrix[_front^1][_actor]).set_bg_priority(_priority).set_z_order(_z_order).set_visible(false);
            back.sprites.push_back(bn::sprite_ptr::create(builder));--budget;
        }
        if(_part==placement.count){++_actor;_part=0;}
    }
    _allocated=_actor==_actors;
    while(_upload_part<back.pieces.size() && back.pieces[_upload_part].shared)++_upload_part;
    if(_allocated && _upload_part==back.pieces.size())_ready=true;
    if(!_allocated || _scratch_count)return;
    // Decode outside VBlank; its callback only copies the prepared tile batch.
    int part=_upload_part,tile=_upload_tile;
    while(part<back.pieces.size() && _scratch_count<64)
    {
        if(back.pieces[part].shared){++part;continue;}
        auto piece=back.pieces[part];const auto& asset=back.assets[piece.actor];
        const auto& shape=back.placement[piece.actor].pieces[piece.index];const auto& p=asset.parts[shape.part];
        actor_crop crop{back.placement[piece.actor].bottom,asset.width,back.placement[piece.actor].limit};
        int count=shape.width*shape.height;
        while(tile<count && _scratch_count<64)
        {
            int tx=shape.x+tile%shape.width,ty=shape.y+tile/shape.width;
            const auto& source=*reinterpret_cast<const bn::tile*>(_bytes+p.tiles[ty*(p.width/8)+tx]);
            int x=p.x+tx*8,y=p.y+ty*8;
            auto& target=_scratch[_scratch_count++];int low=asset.height,high=0;
            for(int u=x;u<x+8 && u<asset.width;++u){low=bn::min(low,crop.at(u));high=bn::max(high,crop.at(u));}
            if(y+8<=low)target=source;
            else if(y>=high)for(auto& row:target.data)row=0;
            else for(int row=0;row<8;++row)target.data[row]=source.data[row]&crop.mask(x,y+row);
            ++tile;
        }
        if(tile==count){++part;tile=0;}
    }
}
int trial_cast::commit(int bytes)
{
    if(!_allocated || _ready || !_scratch_count)return bytes;
    auto& back=_buffers[_front^1];int available=bn::min(bytes,2048);
    while(_upload_part<back.pieces.size() && available>=32 && _scratch_read<_scratch_count)
    {
        if(back.pieces[_upload_part].shared){++_upload_part;continue;}
        auto piece=back.pieces[_upload_part];const auto& shape=back.placement[piece.actor].pieces[piece.index];
        int count=shape.width*shape.height,amount=bn::min(bn::min(count-_upload_tile,available/32),_scratch_count-_scratch_read);
        auto vram=back.tiles[_upload_part].vram().value();bn::memory::copy(_scratch[_scratch_read],amount,vram[_upload_tile]);
        _scratch_read+=amount;_upload_tile+=amount;available-=amount*32;bytes-=amount*32;
        if(_upload_tile==count){++_upload_part;_upload_tile=0;}
    }
    if(_scratch_read==_scratch_count)_scratch_read=_scratch_count=0;
    while(_upload_part<back.pieces.size() && back.pieces[_upload_part].shared)++_upload_part;
    _ready=_upload_part==back.pieces.size();return bytes;
}
void trial_cast::publish(int view)
{
    for(auto& sprite:_buffers[_front].sprites)sprite.set_visible(false);
    _retiring=!_buffers[_front].sprites.empty();_front^=1;_view=view;_display_scroll=-32768;_transform_dirty=true;
    for(int a=0;a<_actors;++a)for(int bank=0;bank<4;++bank)
        _palettes[a*4+bank].set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(_buffers[_front].assets[a].palette+bank*16),16));
}
void trial_cast::transform(int x,int y,int scale)
{
    if(_view<0 || (!_transform_dirty && x==_offset_x && y==_offset_y && scale==_scale))return;
    _offset_x=x;_offset_y=y;_scale=scale;_display_scroll=-32768;_transform_dirty=false;
    for(int actor=0;actor<_actors;++actor)
        _matrix[_front][actor]->set_scale(bn::fixed::from_data(_buffers[_front].placement[actor].scale*scale/16));
}
void trial_cast::position(int scroll,int count)
{
    if(_view<0 || (scroll==_display_scroll && count==_display_count))return;
    _display_scroll=scroll;_display_count=count;
    auto& front=_buffers[_front];
    for(int i=0;i<front.pieces.size();++i)
    {
        auto piece=front.pieces[i];const auto& asset=front.assets[piece.actor];const auto& placement=front.placement[piece.actor];
        const auto& shape=placement.pieces[piece.index];const auto& p=asset.parts[shape.part];
        int scale=placement.scale*_scale/256;
        int x=placement.x+_offset_x+(p.x+shape.x*8+shape.width*4-asset.pivot_x)*scale/256-scroll-120;
        int y=placement.y+_offset_y+(p.y+shape.y*8+shape.height*4-asset.pivot_y)*scale/256-80;
        if(placement.alignment && _scale==256)
        {
            unsigned shift=trial_alignment_offsets[placement.alignment+piece.index];
            x+=int(shift&7)-2;
            y+=int(shift>>3)-2;
        }
        front.sprites[i].set_visible(piece.actor<count);
        front.sprites[i].set_position(x,y);
    }
}
int trial_cast::tile_bytes() const
{
    int bytes=0;for(const auto& tiles:_buffers[_front].tiles)bytes+=tiles.tiles_count()*32;return bytes;
}
int trial_cast::sprite_count() const {return _buffers[_front].sprites.size();}
void trial_cast::blend(bool enabled)
{
    for(auto& sprite:_buffers[_front].sprites)sprite.set_blending_enabled(enabled);
}
void trial_cast::visible(bool enabled)
{
    auto& front=_buffers[_front];
    for(int i=0;i<front.sprites.size();++i)front.sprites[i].set_visible(enabled && front.pieces[i].actor<_display_count);
}
bool trial_cast::clear_step()
{
    if(_target>=0){visible(false);_target=_view=-1;_allocated=_ready=false;_scratch_count=0;}
    int budget=4;
    for(auto& buffer:_buffers)
        while(!buffer.pieces.empty() && budget){buffer.sprites.pop_back();buffer.tiles.pop_back();buffer.pieces.pop_back();--budget;}
    return _buffers[0].pieces.empty() && _buffers[1].pieces.empty();
}
}
