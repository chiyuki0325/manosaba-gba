#ifndef DIALOGUE_PAGE_H
#define DIALOGUE_PAGE_H
#include "story_layout.h"

namespace manosaba
{
class save_archive;
enum class reveal_step { none, append, brighten };

class sentence_guard
{
public:
    void restart(){_frames=18;}
    void tick(){if(_frames)--_frames;}
    bool ready() const {return !_frames;}
private:
    int _frames=0;
};

class dialogue_page
{
public:
    void archive(save_archive& data,const char* text);
    void begin(const char* text, int offset, const dialogue_layout& box,bool original_breaks=true);
    reveal_step tick();
    void finish() { _visible = _end; _fading = false; }
    bool complete() const { return _visible == _end && ! _fading; }
    bool fading() const { return _fading; }
    int start() const { return _start; }
    int end() const { return _end; }
    int visible() const { return _visible; }
    int glyph_begin() const { return _glyph_begin; }
private:
    const char* _text = nullptr;
    int _start = 0, _end = 0, _visible = 0, _glyph_begin = 0;
    bool _fading = false;
};

// One action per physical press, with release qualification across page changes.
class advance_button
{
public:
    void reset();
    bool update(bool held, bool ready,int frames=1);
private:
    int _released = 0, _cooldown = 0;
    bool _held = false, _armed = false;
};
}
#endif
