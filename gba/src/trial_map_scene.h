#ifndef MANOSABA_TRIAL_MAP_SCENE_H
#define MANOSABA_TRIAL_MAP_SCENE_H
#include "trial_map_data.h"
#include "text_canvas.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_vector.h"
namespace manosaba {
class trial_map_scene {
public:
    void reset();
    void suspend();
    void request(int segment,bool settled=false);
    void close();
    void update(bool ready,int frames);
    void draw(text_canvas& canvas);
    bool active() const {return _segment>=0;}
    bool ready() const {return _canvas && _opened && _loaded==frame().floor && !_canvas->map_pending() && _sprites.size()==trial_map_segments[_segment].pins;}
    bool done() const {return ready() && _clock>=trial_map_segments[_segment].ticks;}
    bool closed() const {return _closing && !_close_left;}
    int clock() const {return _clock;}
private:
    text_canvas* _canvas=nullptr;
    bn::vector<bn::sprite_ptr,7> _sprites;
    bn::vector<bn::sprite_palette_ptr,7> _palettes;
    int _segment=-1,_clock=0,_loaded=-1,_loading=-1,_block=0,_close_left=0;
    bool _opened=false,_cells=false,_closing=false;
    const trial_map_frame& frame() const;
    int interpolate(int from,int to) const {return from+(to-from)*(_clock%6)/6;}
    void position();
};
}
#endif
