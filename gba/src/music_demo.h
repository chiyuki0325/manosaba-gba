#ifndef MUSIC_DEMO_H
#define MUSIC_DEMO_H
namespace manosaba
{
class text_canvas;
class music_demo
{
public:
    void reset();
    bool update();
    void draw(text_canvas& canvas) const;
private:
    int _selected = 0;
    int _playing = -1;
};
}
#endif
