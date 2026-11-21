#include "trial_movie.h"
#include "trial_cutin_data.h"
#include "sfx_data.h"
#include "sfx_player.h"
#include "bn_sprite_builder.h"
#include "bn_sprite_shape_size.h"
#include "bn_memory.h"
#include "bn_blending.h"
namespace manosaba {
namespace {
constexpr int shard_start(int particle){return 64+particle*7;}
constexpr int shard_vx[3]={-3,3,-2};
constexpr int shard_vy[3]={-3,-2,-1};
}
void trial_movie::init(int kind,int variant,int frame)
{
    _kind=kind;_shard_piece=_shard_tile=0;_frame=frame;_flash=_alpha=_glint=-1;_ready=false;_active=true;
    const auto& asset=trial_cutins[kind];int pose=variant>0 ? variant-1 : 0;
    for(int a=0;a<3;++a)
    {
        _assets[a]=asset.poses[pose];_positions[a]=asset.positions[pose];
        _glass_assets[a]=*asset.glass;_glass_positions[a]=*asset.glass_position;
        if(a){_positions[a].count=0;_glass_positions[a].count=0;}
    }
    _cast.emplace(_assets,trial_cutin_bytes,1,0,-4);_cast->request(0,_positions);
    _glass.emplace(_glass_assets,trial_cutin_bytes,1,0,0);_glass->request(0,_glass_positions);
    for(int shape=0;shape<3;++shape)_shard_tiles.push_back(bn::sprite_tiles_ptr::allocate(16,bn::bpp_mode::BPP_4));
    for(int particle=0;particle<3;++particle)
    {
        auto& matrix=_shard_matrix[particle];matrix=bn::sprite_affine_mat_ptr::create();
        const auto& part=asset.shards[particle].parts[0];
        bn::sprite_builder builder(bn::sprite_shape_size(32,32),_shard_tiles[particle],_glass->palette(part.bank));
        builder.set_affine_mat(*matrix).set_bg_priority(0).set_z_order(-8).set_visible(false);
        _shards.push_back(bn::sprite_ptr::create(builder));
    }
}
void trial_movie::reset()
{
    if(!_active)return;
    _shards.clear();_shard_tiles.clear();for(auto& matrix:_shard_matrix)matrix.reset();
    _cast.reset();_glass.reset();_ready=_active=false;
    bn::blending::set_transparency_alpha(1);
}
bool trial_movie::release()
{
    if(!_active)return true;
    bool hero=_cast->clear_step(),glass=_glass->clear_step();int budget=4;
    while(!_shards.empty() && budget--)_shards.pop_back();
    if(!hero || !glass || !_shards.empty())return false;
    reset();return true;
}
void trial_movie::visible(bool enabled)
{
    if(!_active)return;
    _cast->visible(enabled && _ready);_glass->visible(enabled && _ready);
    for(int i=0;i<_shards.size();++i)_shards[i].set_visible(enabled && _ready && _frame>=shard_start(i) && _frame<shard_start(i)+110);
}
palette_effect trial_movie::background_effect(palette_effect overlay) const
{
    return effect_over({32767,uint16_t(bn::max(0,_flash)*16)},overlay);
}
void trial_movie::tint(palette_effect overlay)
{
    if(!_active)return;
    auto effect=background_effect(overlay);_cast->tint(effect);
    _glass->tint(effect_over({32767,uint16_t(bn::max(0,_glint)*16)},effect));
}
bool trial_movie::update(bool display_ready)
{
    _cast->prepare();_glass->prepare();const auto& asset=trial_cutins[_kind];
    if(!_ready)
    {
        if(_cast->ready(0) && _glass->ready(0) && _shard_piece==3)
        {_cast->publish(0);_glass->publish(0);_ready=true;}
        else return false;
    }
    if(!display_ready)return false;
    if(_frame>=asset.count)return true;
    for(int i=0;i<sfx_cutin_event_count;++i)
        if(_frame==sfx_cutin_events[i].frame)
        {sfx_command sound;sound.cue=sfx_cutin_events[i].cue;sound.volume=sfx_cutin_events[i].volume;sfx_player::execute(sound);}
    const auto& frame=asset.frames[_frame];_cast->position(8-frame.x,1);_glass->position(8,1);
    if(frame.alpha!=_alpha)
    {
        _alpha=frame.alpha;_cast->blend(_alpha<16);_glass->blend(_alpha<16);
        bn::blending::set_transparency_alpha(bn::fixed::from_data(_alpha*256));
    }
    _flash=frame.flash;
    int glint=_frame>=64 && _frame<100 ? (100-_frame)/6 : 0;
    if(glint!=_glint)
    {
        _glint=glint;
    }
    for(int i=0;i<_shards.size();++i)
    {
        int elapsed=_frame-shard_start(i);bool shown=elapsed>=0 && elapsed<110;
        _shards[i].set_visible(shown);
        if(shown)
        {
            _shard_matrix[i]->set_rotation_angle((_frame*(i%2 ? 5 : 3)+i*29)%360);
            _shards[i].set_position((i-1)*18+elapsed*shard_vx[i]/2,
                -18+elapsed*shard_vy[i]/2+elapsed*elapsed/100);
        }
    }
    ++_frame;return false;
}
int trial_movie::commit(int bytes)
{
    if(!_active)return bytes;
    int available=bn::min(bytes,2048),budget=available;const auto& asset=trial_cutins[_kind];
    available=_cast->commit(available);available=_glass->commit(available);
    while(_shard_piece<3 && available>=32)
    {
        const auto& part=asset.shards[_shard_piece].parts[0];
        auto vram=_shard_tiles[_shard_piece].vram().value();
        bn::memory::copy(*reinterpret_cast<const bn::tile*>(trial_cutin_bytes+part.tiles[_shard_tile]),1,vram[_shard_tile]);
        available-=32;++_shard_tile;
        if(_shard_tile==part.width*part.height/64){++_shard_piece;_shard_tile=0;}
    }
    return bytes-budget+available;
}
}
