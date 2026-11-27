#ifndef DIALOGUE_PRESENTER_H
#define DIALOGUE_PRESENTER_H
#include "dialogue_page.h"
#include "dialogue_playback.h"
#include "text_canvas.h"

namespace manosaba
{
struct story_link;
enum class dialogue_advance
{
    wait,
    finish,
    page,
    script
};

// Shared dialogue state; the scene owns its background, input gate and clocks.
class dialogue_presenter
{
  public:
    void archive(save_archive& data, const char* text);
    void reset()
    {
        _prepared = false;
        _guard.restart();
        repaint();
    }
    void repaint()
    {
        _step = reveal_step::none;
        _redrawing = false;
        _drawn = _page.start();
        _playback_frames = 0;
    }
    void begin(const char* text, const dialogue_layout& box, int offset = 0);
    void restore(const dialogue_page& page)
    {
        _page = page;
        _prepared = true;
        _short_second_page = false;
        _guard.restart();
        repaint();
    }
    const dialogue_page& page() const
    {
        return _page;
    }
    bool prepared() const
    {
        return _prepared;
    }
    bool complete() const
    {
        return _page.complete();
    }
    bool redrawing() const
    {
        return _redrawing;
    }
    bool changed() const
    {
        return _step != reveal_step::none;
    }
    bool original_breaks(bool value)
    {
        if (value == _original_breaks)
            return false;
        _original_breaks = value;
        reset();
        return true;
    }
    bool speed(int value)
    {
        if (value != _speed)
        {
            _speed = value;
            _delay = 0;
        }
        if (_speed != 2 || !_prepared || complete())
            return false;
        finish();
        return true;
    }
    void playback(dialogue_playback_mode mode)
    {
        if (mode != _playback)
        {
            _playback = mode;
            _playback_frames = 0;
        }
    }
    bool timed_advance(int frames)
    {
        if (_playback == dialogue_playback_mode::normal || !complete() || !can_advance())
            return false;
        _playback_frames += frames;
        int interval = _short_second_page ? int(dialogue_playback_mode::fast) : int(_playback);
        return _playback_frames >= interval;
    }
    bool tick()
    {
        if (_speed == 0 && (++_delay & 1))
            return false;
        _step = _page.tick();
        return changed();
    }
    void finish()
    {
        _page.finish();
        _guard.restart();
        repaint();
    }
    void frame(bool ready)
    {
        if (ready && _prepared)
            _guard.tick();
    }
    bool can_advance() const
    {
        return (_speed == 2 || _guard.ready()) && !_redrawing && _step == reveal_step::none &&
               _drawn == _page.visible();
    }
    void next_page(const char* text, const dialogue_layout& box)
    {
        bool second = _page.start() == 0;
        begin(text, box, _page.end());
        _short_second_page = second && _page.single_line();
    }
    dialogue_advance advance(const char* text, const dialogue_layout& box, bool timed);
    void draw(text_canvas& canvas, const char* text, const dialogue_layout& box, bool full, int budget = 0,
              const story_link* links = nullptr, int count = 0, const story_text_style* style = nullptr);
    void prompt(text_canvas& canvas, const char* text) const;
    static void speaker(text_canvas& canvas, const char* name, bool show_narrator = false);

  private:
    dialogue_page _page;
    sentence_guard _guard;
    text_canvas::cursor _cursor{}, _last_cursor{};
    int _speed = 1, _delay = 0;
    int _playback_frames = 0;
    dialogue_playback_mode _playback = dialogue_playback_mode::normal;
    int _drawn = 0, _last_begin = 0, _last_end = 0;
    reveal_step _step = reveal_step::none;
    bool _prepared = false, _redrawing = false, _original_breaks = true, _short_second_page = false;
    static void range(text_canvas& canvas, text_canvas::cursor& cursor, const char* text, const dialogue_layout& box,
                      int first, int last, text_canvas::ink ink, const story_link* links, int count,
                      const story_text_style* style, bool original_breaks);
};
} // namespace manosaba
#endif
