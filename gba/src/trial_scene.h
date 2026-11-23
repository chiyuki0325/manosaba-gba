#ifndef TRIAL_SCENE_H
#define TRIAL_SCENE_H
#include "trial_cast.h"
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_bg_palette_ptr.h"
namespace manosaba {
class trial_scene
{
public:
    void protect() const;
    void init();
    void reset();
    bool release();
    bool release_actors();
    void request_background(int view,bool demo=false,int layout=0);
    void visible(bool enabled);
    bool active() const {return _bg.has_value();}
    void request(int actor,int view,bool demo=false,int layout=0,const uint16_t* poses=nullptr,int reserve_scale=256,int source_row=1,bool horizontal=false);
    void request_pan(int motion,unsigned frame,bool witch_hiro,const uint16_t* poses=nullptr,int actor=0);
    int scroll() const {return _tracking ? _track_x : _x;}
    int window_x() const {return _x;}
    int background() const {return _view;}
    void update();
    void tint(palette_effect effect);
    void shake(int x,int y);
    void actor_shake(int x,int y){_actor_shake_x=int8_t(x);_actor_shake_y=int8_t(y);}
    void heat(bool enabled,bool animate);
    int commit(int bytes);
    bool ready() const {return !_clearing && !_pan_reload && !_formal_reload && (_published==_key || (_pan && _published>=400)) && _view==_target && !_map_pending;}
private:
    bn::optional<bn::regular_bg_tiles_ptr> _tiles[2];
    bn::optional<bn::bg_palette_ptr> _palette;
    bn::optional<bn::regular_bg_ptr> _bg;
    bn::optional<trial_cast> _cast;
    graphic_asset _assets[3];
    trial_actor_placement _positions[2][3];
    trial_actor_piece _pose_pieces[2][3][64];
    int _front=0,_view=-1,_target=-1,_cursor=0,_start=0,_origin=0,_first=0,_x=8,_queued=-1;
    int _key=-1,_published=-1,_cast_bank=0,_published_bank=0,_direction=1;
    int _pan_x=0,_motion=-1,_pan_generation=0;
    unsigned _motion_frame=0;
    int _pose_revision=0,_pose_images[3]={-1,-1,-1};
    int _vertical=0,_source_row=1,_load_row=1,_offset_x=0,_offset_y=0,_scale=256;
    bool _compound=false,_formal_reload=false,_horizontal=false,_load_horizontal=false,_tracking=false;
    int _track_x=0,_horizontal_x=40;
    int8_t _shake_x=0,_shake_y=0;
    int8_t _actor_shake_x=0,_actor_shake_y=0;
    bool _heat=false,_heat_dma=false;
    uint8_t _heat_phase=0,_heat_front=0;
    bool _map_pending=false,_demo=false,_clearing=false,_background_only=false,_pan=false,_pan_reload=false;
    void column(int physical,int source);
    void position();
    void placement(int slot,const trial_actor_placement& value);
    int target_bytes() const {return _target>=9 && !_load_horizontal ? 630*32 : 640*32;}
};
}
#endif
