#pragma once
namespace manosaba {
class text_canvas;
class warning_screen
{
public:
    void draw(text_canvas& canvas);
    bool update(text_canvas& canvas);
    bool active() const {return _remaining>0;}
private:
    int _line=0,_remaining=300;
};
}
