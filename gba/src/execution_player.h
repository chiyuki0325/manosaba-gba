#ifndef MANOSABA_EXECUTION_PLAYER_H
#define MANOSABA_EXECUTION_PLAYER_H
namespace manosaba {
class text_canvas;
class execution_player {
public:
    void begin();
    void suspend();
    bool update(bool held,bool ready);
    void draw(text_canvas& canvas);
    bool shown() const {return _shown;}
    bool done() const {return _charge==2000 && !_delay;}
private:
    int _charge=0,_delay=96,_draw_level=0,_pose=0;
    bool _pressed=false,_armed=false,_opened=false,_shown=false,_sound=false,_checked=false;
    void charging(bool enabled);
};
}
#endif
