#ifndef MANOSABA_COURT_EFFECT_H
#define MANOSABA_COURT_EFFECT_H
#include "story_format.h"
#include "save_archive.h"
namespace manosaba {
class court_shake
{
public:
    void start(trial_shake_spec spec){_spec=spec;_left=spec.frames;_phase=0;}
    void tick(unsigned frames)
    {
        if(!_left)return;
        _left=uint16_t(frames<_left ? _left-frames : 0);
        _phase=uint16_t((_phase+frames)%_spec.period);
    }
    int x() const {return offset(_spec.x,0);}
    int y() const {return offset(_spec.y,2);}
    unsigned left() const {return _left;}
    void archive(save_archive& a)
    {
        a.field(_spec.frames);a.field(_spec.x);a.field(_spec.y);a.field(_spec.period);a.field(_spec.damping);a.field(_left);
        if(a.reading())_phase=0;
    }
private:
    trial_shake_spec _spec{0,0,0,4,0};
    uint16_t _left=0,_phase=0;
    int offset(int amplitude,int shift) const
    {
        if(!_left || !amplitude)return 0;
        constexpr int8_t wave[]={0,1,1,0,0,-1,-1,0};
        int value=amplitude*wave[(_phase*8/_spec.period+shift)&7];
        return _spec.damping ? value*(255-_spec.damping+_spec.damping*_left/_spec.frames)/255 : value;
    }
};
}
#endif
