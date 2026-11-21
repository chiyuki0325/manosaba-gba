#ifndef TRIAL_CAST_H
#define TRIAL_CAST_H
#include "actor_crop.h"
#include "graphics_data.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_sprite_affine_mat_ptr.h"
#include "bn_optional.h"
#include "bn_vector.h"
#include "bn_tile.h"
#include "screen_effect.h"
namespace manosaba
{
// Uses the same asset descriptors, tile dictionary and palettes as image_scene.
class trial_cast
{
public:
    trial_cast(const graphic_asset* assets,const uint8_t* bytes,int actors=3,int priority=1,int z_order=0);
    void request(int view,const trial_actor_placement* placement);
    void request(int view,const trial_actor_placement* placement,const graphic_asset* assets);
    void prepare();
    int commit(int bytes);
    bool ready(int view) const {return _target==view && _ready;}
    void publish(int view);
    void position(int scroll,int count);
    void transform(int x,int y,int scale);
    void blend(bool enabled);
    void visible(bool enabled);
    void tint(palette_effect effect);
    bool clear_step();
    const bn::sprite_palette_ptr& palette(int bank) const {return _palettes[bank];}
    int tile_bytes() const;
    int sprite_count() const;
private:
    struct cast_piece {uint8_t actor,index;bool shared;};
    struct cast_buffer
    {
        bn::vector<bn::sprite_ptr,64> sprites;
        bn::vector<bn::sprite_tiles_ptr,64> tiles;
        bn::vector<cast_piece,64> pieces;
        const trial_actor_placement* placement=nullptr;
        graphic_asset assets[3];
        void clear(){sprites.clear();tiles.clear();pieces.clear();}
    };
    const graphic_asset* _assets;
    const uint8_t* _bytes;
    int _actors,_priority,_z_order;
    bn::tile _scratch[64];
    cast_buffer _buffers[2];
    bn::vector<bn::sprite_palette_ptr,12> _palettes;
    bn::optional<bn::sprite_affine_mat_ptr> _matrix[2][3];
    int _front=0,_target=-1,_view=-1,_actor=0,_part=0,_upload_part=0,_upload_tile=0;
    int _scratch_count=0,_scratch_read=0,_display_scroll=-32768,_display_count=-1;
    int _offset_x=0,_offset_y=0,_scale=256;
    bool _transform_dirty=true;
    bool _allocated=false,_ready=false,_retiring=false;
};
}
#endif
