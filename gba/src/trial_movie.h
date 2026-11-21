#ifndef TRIAL_MOVIE_H
#define TRIAL_MOVIE_H
#include "trial_cast.h"
namespace manosaba {
class trial_movie
{
public:
    void init(int kind,int variant,int frame=0);
    void reset();
    bool release();
    void visible(bool enabled);
    palette_effect background_effect(palette_effect overlay) const;
    void tint(palette_effect overlay);
    bool active() const {return _active;}
    bool ready() const {return _ready;}
    bool update(bool display_ready);
    int commit(int bytes);
    int frame() const {return _frame;}
private:
    bn::optional<trial_cast> _cast;
    bn::optional<trial_cast> _glass;
    graphic_asset _assets[3];
    graphic_asset _glass_assets[3];
    trial_actor_placement _positions[3];
    trial_actor_placement _glass_positions[3];
    bn::vector<bn::sprite_ptr,3> _shards;
    bn::vector<bn::sprite_tiles_ptr,3> _shard_tiles;
    bn::optional<bn::sprite_affine_mat_ptr> _shard_matrix[3];
    int _kind=0,_frame=0,_flash=-1,_alpha=-1,_glint=-1,_shard_piece=0,_shard_tile=0;
    bool _active=false,_ready=false;
};
}
#endif
