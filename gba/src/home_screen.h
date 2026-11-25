#pragma once
#include "dialogue_page.h"
namespace manosaba
{
class text_canvas;
class save_system;
enum class home_action
{
    none,
    start,
    new_game,
    load,
    gallery,
    music,
    settings,
    disable_breaks,
    debug,
    back
};
class home_screen
{
  public:
    void open(int page, bool hiro = false, int selected = 0);
    void title();
    void repaint();
    home_action update(text_canvas& canvas, save_system& saves);
    void draw(text_canvas& canvas, const save_system& saves);
    bool painting() const
    {
        return _paint < 2 || _dirty;
    }
    int page() const
    {
        return _page;
    }
    bool hiro() const
    {
        return _hiro;
    }

  private:
    int _page = 0, _selected = 0, _paint = 0, _dirty = 0, _fade = 32;
    bool _hiro = false, _music = false, _shown = false;
    advance_button _advance;
};
} // namespace manosaba
