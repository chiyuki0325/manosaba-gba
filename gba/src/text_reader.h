#ifndef TEXT_READER_H
#define TEXT_READER_H
namespace manosaba
{
class text_canvas;
class text_reader
{
public:
    using paint_line=void(*)(void*,text_canvas&,int);
    void reset(){_phase=idle;}
    void begin(int top,int visible,int total,int first=0);
    bool update();
    void draw(text_canvas& canvas,void* context,paint_line line);
    bool ready() const {return _phase==settled;}
    bool yielded() const {return _yield;}
    bool moving() const {return _phase==motion;}
    int first() const {return _scroll;}
private:
    enum phase {idle,open,fill,reveal,settled,motion};
    phase _phase=idle;
    int _top=0,_visible=0,_total=0,_scroll=0,_pixel=0,_first=0,_last=0,_line=0;
    int _repeat_direction=0,_repeat_delay=0;
    int _step_pixels=2;
    bool _yield=false;
};
}
#endif
