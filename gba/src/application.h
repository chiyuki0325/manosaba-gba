#ifndef APPLICATION_H
#define APPLICATION_H
#include "build_config.h"
#include "debug_menu.h"
#include "story_scene.h"
#include "text_canvas.h"
#include "music_demo.h"
#include "music_room.h"
#include "sfx_demo.h"
#include "credits_player.h"
#include "gallery.h"
#include "home_screen.h"
#include "warning_screen.h"
namespace manosaba
{
class application
{
  public:
    application();
    void update();
    void commit();

  private:
    text_canvas _canvas;
    debug_menu _menu;
    story_scene _story;
    music_room _music_room;
#if MANOSABA_DEBUG_MENU
    music_demo _music_demo;
    sfx_demo _sfx_demo;
    credits_player _ending;
#endif
    gallery _gallery;
    home_screen _home;
    warning_screen _warning;
#if MANOSABA_DEBUG_MENU
    int _stress_choice = 0;
#endif
    bool _redraw = false;
    bool _home_started = false, _home_load = false, _pause_settings = false;
    bool _debug_origin = false, _debug_from_home = false;
    int _home_confirm = 0;
    bool _confirm_ok = false;
    void draw();
    void draw_home_confirm();
    void update_home_confirm();
    bool story_page() const;
    bool gallery_page() const;
    void handle_home(home_action action);
    void close_settings();
};
} // namespace manosaba
#endif
