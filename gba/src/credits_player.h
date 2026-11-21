#ifndef MANOSABA_CREDITS_PLAYER_H
#define MANOSABA_CREDITS_PLAYER_H
#include "credits_data.h"
#include "bn_sprite_ptr.h"
#include "bn_vector.h"
namespace manosaba {
class text_canvas;
class credits_player
{
public:
    void begin(int act,bool story=false);
    void suspend();
    bool update(bool ready);
    void draw(text_canvas& canvas);
    bool done() const {return _phase==finished;}
    bool shown() const {return _shown;}
private:
    enum phase {forward,hold,reverse,ending,finished};
    phase _phase=forward;
    const credits_entry* _entry=nullptr;
    int _act=2,_pixel=0,_first=0,_last=-1,_fill=0,_wait=0,_reverse_frame=0,_reverse_from=0;
    bool _opened=false,_shown=false;
    bn::vector<bn::sprite_ptr,40> _noise;
    int _noise_tick=-1;
    void paint(text_canvas& canvas,int row);
    void draw_noise();
};
}
#endif
