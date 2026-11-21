#include "text_reader.h"
#include "text_canvas.h"
#include "bn_keypad.h"
#include "bn_algorithm.h"
namespace manosaba
{
void text_reader::begin(int top,int visible,int total,int first)
{
    _top=top;_visible=visible;_total=total;_scroll=first;_pixel=first*14;
    _first=first;_last=first+visible-1;_line=0;_repeat_direction=_repeat_delay=0;_phase=open;
}
bool text_reader::update()
{
    if(_phase!=settled && _phase!=motion)return false;
    int direction=bn::keypad::down_held() ? 1 : bn::keypad::up_held() ? -1 : 0,step=0;
    if(direction)
    {
        if(direction!=_repeat_direction){step=direction;_repeat_direction=direction;_repeat_delay=12;}
        else if(--_repeat_delay<=0){step=direction;_repeat_delay=7;}
    }
    else _repeat_direction=_repeat_delay=0;
    bool jump=bn::keypad::left_pressed() || bn::keypad::right_pressed();
    if(jump)step=bn::keypad::right_pressed() ? _visible : -_visible;
    int next=bn::clamp(_scroll+step,0,bn::max(0,_total-_visible));
    if(next==_scroll)return false;
    _scroll=next;
    _step_pixels=jump ? 14 : 2;_phase=motion;
    return true;
}
void text_reader::draw(text_canvas& canvas,void* context,paint_line line)
{
    _yield=false;
    switch(_phase)
    {
    case open:
        if(!canvas.ready()){_yield=true;return;}
        canvas.reader_open(_top,_visible*14);_phase=fill;return;
    case fill:
        line(context,canvas,_scroll+_line++);
        if(_line==_visible)_phase=reveal;
        return;
    case reveal:
        if(!canvas.ready()){_yield=true;return;}
        canvas.reader_position(_pixel);canvas.reader_show();_phase=settled;_yield=true;return;
    case motion:
    {
        int target=_scroll*14,next=_pixel+bn::clamp(target-_pixel,-_step_pixels,_step_pixels);
        int first=next/14,last=(next+_visible*14-1)/14;
        if(first<_first)line(context,canvas,first);
        else if(last>_last)line(context,canvas,last);
        _first=first;_last=last;_pixel=next;canvas.reader_position(next);
        if(next==target)_phase=settled;
        _yield=true;return;
    }
    default:return;
    }
}
}
