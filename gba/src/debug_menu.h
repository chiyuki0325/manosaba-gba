#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include "scenes.h"

namespace manosaba
{
class text_canvas;

enum class menu_action
{
    none,
    redraw,
    enter,
    resume,
    pause
};

// Navigation only; pages and playback belong to application.
class debug_menu
{
  public:
    void open_scene(scene_id scene)
    {
        _scene = scene;
        _selected = int(scene);
        _open = false;
        _has_scene = true;
    }
    menu_action update(bool scene_buttons = true);
    void draw(text_canvas& canvas) const;
    bool open() const
    {
        return _open;
    }
    void show()
    {
        _selected = int(_scene);
        _open = true;
    }
    scene_id scene() const
    {
        return _scene;
    }

  private:
    int _selected = 0;
    scene_id _scene = scene_id::title;
    bool _open = true;
    bool _has_scene = false;

    menu_action enter();
};
} // namespace manosaba

#endif
