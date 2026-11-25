#pragma once
#include "bn_optional.h"
#include "bn_affine_bg_ptr.h"
#include "bn_affine_bg_tiles_ptr.h"
#include "bn_bg_palette_ptr.h"
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
namespace manosaba
{
class text_canvas;
class save_system;
class gallery
{
  public:
    void protect() const;
    void open(bool demo = false);
    void suspend();
    bool update(bool ready, const save_system& progress);
    void draw(text_canvas& canvas, const save_system& progress);
    int commit(int bytes);
    bool painting() const
    {
        return _paint < 3 || (!_mode && _cell < 6);
    }
    bool done() const
    {
        return _done;
    }

  private:
    bn::optional<bn::affine_bg_ptr> _bg;
    bn::optional<bn::regular_bg_ptr> _regular;
    bn::optional<bn::regular_bg_tiles_ptr> _regular_tiles;
    bn::optional<bn::affine_bg_tiles_ptr> _tiles;
    bn::optional<bn::bg_palette_ptr> _palette;
    int _selected = 0, _previous = -1, _image = -1, _mode = 0, _paint = 0, _cell = 0, _row = 0, _upload = 0, _pan = 0;
    unsigned _revision = 0;
    bool _done = false, _native = false, _demo = false;
    bool _map_pending = false;
    int _window = -1024;
    bool seen(int image, const save_system& progress) const;
    int page() const
    {
        return _selected / 6;
    }
    int unlocked(int group, const save_system& progress) const;
    void frame(text_canvas& canvas, int cell, const save_system& progress);
    void position();
    void controls(text_canvas& canvas, bool grid);
};
} // namespace manosaba
