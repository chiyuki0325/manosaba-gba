#include "application.h"
#include "bn_core.h"
#include "bn_keypad.h"
#include "music_player.h"
#include "sfx_player.h"
#include "sfx_ui.h"
#include "home_runtime.h"
#if MANOSABA_DEBUG_MENU
// Host tests poke this before pressing Start to jump straight to the debug menu.
extern "C" {BN_DATA_EWRAM_BSS unsigned char debug_hotkey=0;}
#endif
namespace manosaba
{
namespace
{
application* current;
void vblank_commit() { current->commit(); }
}
application::application()
{
    current = this;
    bn::core::set_vblank_callback(vblank_commit);
    _menu.open_scene(scene_id::title);
    home_runtime.screen=1;
    _warning.draw(_canvas);
}
bool application::gallery_page() const
{
    return _menu.scene()==scene_id::gallery || _menu.scene()==scene_id::gallery_demo;
}
bool application::story_page() const
{
    return _menu.scene() == scene_id::script || _menu.scene()==scene_id::trial;
}
void application::handle_home(home_action action)
{
    if(action==home_action::none)return;
    save_system& saves=_story.progress();
    switch(action)
    {
    case home_action::start:
        _home.open(1);_redraw=true;break;
    case home_action::settings:
        _home.open(2);saves.editing_settings(true);_redraw=true;break;
    case home_action::back:
        if(_home.page()==2){saves.editing_settings(false);_home.open(1);}
        else _home.title();
        _redraw=true;break;
    case home_action::new_game:
        if(saves.has_saves()){_new_game_confirm=true;_new_game_ok=false;_redraw=true;break;}
        _home_started=false;_debug_origin=false;_menu.open_scene(scene_id::script);_story.new_game();_redraw=true;break;
    case home_action::load:
        saves.editing_settings(false);saves.open_load();_home_load=true;_redraw=true;break;
    case home_action::gallery:
        _debug_origin=false;_menu.open_scene(scene_id::gallery);_gallery.open(false);_redraw=true;break;
    case home_action::music:
        _debug_origin=false;_menu.open_scene(scene_id::music);_music_room.open();_redraw=true;break;
#if MANOSABA_DEBUG_MENU
    case home_action::debug:
        _menu.show();_debug_from_home=true;_redraw=true;break;
#endif
    default:
        break;
    }
}
void application::close_settings()
{
    _pause_settings=false;_story.settings_closed();_story.progress().editing_settings(false);_redraw=true;
}
void application::draw_new_game_confirm()
{
    _canvas.rectangle(24,42,192,68,text_canvas::background);
    _canvas.rectangle(24,42,192,1,text_canvas::muted);
    _canvas.text(48,49,"已有存档",text_canvas::accent);
    _canvas.text(40,68,"开始新游戏会覆盖自动存档",text_canvas::white);
    _canvas.rectangle(41,84,68,20,_new_game_ok ? text_canvas::background : text_canvas::selection);
    _canvas.rectangle(129,84,68,20,_new_game_ok ? text_canvas::selection : text_canvas::background);
    _canvas.text(62,88,"取消",_new_game_ok ? text_canvas::muted : text_canvas::white,109,104);
    _canvas.text(150,88,"确定",_new_game_ok ? text_canvas::white : text_canvas::muted,197,104);
}
void application::draw()
{
    unsigned begin=unsigned(bn::core::current_cpu_ticks());
    bool overlay=_pause_settings || _home_load;
#if MANOSABA_DEBUG_MENU
    if(_menu.open() || _menu.scene()!=scene_id::ending)_ending.suspend();
#endif
    if(_menu.open() || !gallery_page())_gallery.suspend();
    if(_menu.open() || _menu.scene()!=scene_id::music || _debug_origin)_music_room.suspend();
    _story.set_visible(!_menu.open() && story_page() && !overlay);
#if MANOSABA_DEBUG_MENU
    if(_menu.open()) _menu.draw(_canvas);
    else
#endif
    if(_pause_settings){_home.draw(_canvas,_story.progress());_canvas.present();}
    else if(_home_load){_story.progress().draw(_canvas,_story);_canvas.present();}
    else if(story_page()) _story.draw(_canvas);
    else if(gallery_page())_gallery.draw(_canvas,_story.progress());
    else if(_menu.scene()==scene_id::title)
    {
        if(_home_started)
        {
            _home.draw(_canvas,_story.progress());
            if(_new_game_confirm && !_home.painting())draw_new_game_confirm();
            _canvas.present();
        }
        else {_canvas.clear();_canvas.present();}
    }
#if MANOSABA_DEBUG_MENU
    else if(_menu.scene()==scene_id::ending)_ending.draw(_canvas);
#endif
    else if(_menu.scene()==scene_id::music)
    {
#if MANOSABA_DEBUG_MENU
        if(_debug_origin)_music_demo.draw(_canvas);else
#endif
        _music_room.draw(_canvas,_story.progress());
    }
#if MANOSABA_DEBUG_MENU
    else if(_menu.scene()==scene_id::sound)_sfx_demo.draw(_canvas);
    else if(_menu.scene()==scene_id::music_stress)
    {
        _canvas.clear();
        _canvas.text(12,8,"音乐压力测试",text_canvas::accent);
        _canvas.text(12,36,"固定播放完整 HirO",text_canvas::muted);
        _canvas.text(12,52,"从章节选择器进入实际场景",text_canvas::muted);
        _canvas.rectangle(10,79+_stress_choice*24,220,16,text_canvas::selection);
        _canvas.text(24,80,"故事章节");
        _canvas.text(24,104,"审判章节");
        _canvas.text(12,144,"上下选择 A进入 Start菜单",text_canvas::muted);
        _canvas.present();
    }
    else draw_scene(_canvas, _menu.scene());
#endif
    if(!_menu.open() && story_page() && !overlay) _story.render(_canvas);
    else _canvas.toast(0);
    graphics_performance.ui_ticks=unsigned(bn::core::current_cpu_ticks())-begin;
}
void application::commit()
{
    int remaining = _canvas.commit(4096);
    if(!_menu.open() && story_page() && !_pause_settings && !_home_load) _story.commit(remaining);
    else if(!_menu.open() && gallery_page())_gallery.commit(remaining);
}
void application::update()
{
    sfx_player::update(1+bn::core::last_missed_frames());
    music_player::update(sfx_player::foreground_active());
    _canvas.prepare();
    if(!story_page() || _menu.open() || _pause_settings || _home_load)_story.persistent_tick();
    graphics_performance.ui_ticks=0;
    graphics_performance.core_ticks=unsigned(bn::core::last_cpu_ticks());
    if(graphics_performance.core_ticks>graphics_performance.max_core_ticks)graphics_performance.max_core_ticks=graphics_performance.core_ticks;
    unsigned vblank=unsigned(bn::core::last_vblank_ticks());
    if(vblank>graphics_performance.max_vblank_ticks)graphics_performance.max_vblank_ticks=vblank;
    if(_warning.active())
    {
        if(!_warning.update(_canvas))return;
        home_runtime.screen=0;
        _redraw=true;
    }
#if MANOSABA_DEBUG_MENU
    if(!_menu.open() && debug_hotkey && bn::keypad::start_pressed())
    {
        unsigned request=debug_hotkey;
        debug_hotkey=0;
        if(request==2 && story_page())_story.open_chapters();
        else _menu.show();
        _redraw=true;draw();_redraw=false;
        return;
    }
    auto action=menu_action::none;
    if(_menu.open() || (_debug_origin && !story_page()))
        action=_menu.update(_menu.scene()!=scene_id::sound && _menu.scene()!=scene_id::ending && !gallery_page() && _menu.scene()!=scene_id::title && (!story_page() || !_story.owns_buttons()));
    if(action == menu_action::enter)
    {
        sfx_player::reset();
#ifdef MANOSABA_MUSIC_STRESS
        _story.test_music(true);
#else
        _story.test_music(false);
#endif
        _debug_origin=true;
        if(_menu.scene() == scene_id::script) _story.reset();
        else if(_menu.scene()==scene_id::trial)_story.start_trial();
        else if(_menu.scene()==scene_id::music)_music_demo.reset();
        else if(_menu.scene()==scene_id::sound)_sfx_demo.reset();
        else if(gallery_page())_gallery.open(_menu.scene()==scene_id::gallery_demo);
        else if(_menu.scene()==scene_id::ending)
        {
            _ending.begin(2);
            if(!music_player::playing())
            {
                music_command command;command.path="Song_4_1";command.loop=false;command.parameters=music_has_path|music_has_loop;
                music_player::execute(command);
            }
        }
        else if(_menu.scene()==scene_id::music_stress){_stress_choice=0;music_player::reset();}
        else if(_menu.scene()==scene_id::title){_home_started=true;_home.open(0,_story.progress().has_hiro_save());}
        else if(_menu.scene()!=scene_id::title)music_player::reset();
        play_ui_sound(ui_sound::confirm);
    }
    if(action==menu_action::resume && _debug_from_home)
    {
        _debug_origin=false;_debug_from_home=false;
        if(_menu.scene()==scene_id::title)_home.repaint();
    }
    if(action != menu_action::none)
    {
        _redraw=true;
        draw();_redraw=false;
        return;
    }
    if(! _menu.open() && _debug_origin && !story_page() && bn::keypad::start_pressed())
    {
        _menu.show();_redraw=true;draw();_redraw=false;return;
    }
#endif
    if(! _menu.open())
    {
        if(_menu.scene()==scene_id::title)
        {
            if(!_home_started)
            {
                if(_story.progress().settings_ready())
                {_home.open(0,_story.progress().has_hiro_save());_home_started=true;_redraw=true;}
            }
            else if(_home_load)
            {
                auto result=_story.progress().update(_story,_canvas.ready());
                if(result==save_ui_result::loaded)
                {
                    _home_load=false;_menu.open_scene(scene_id::script);_redraw=true;
                }
                else if(result==save_ui_result::resume)
                {
                    _home_load=false;_home.open(1,false,1);_redraw=true;
                }
                else _redraw=true;
            }
            else
            {
                if(_new_game_confirm)
                {
                    if(bn::keypad::left_pressed() || bn::keypad::right_pressed())
                    {
                        _new_game_ok=!_new_game_ok;play_ui_sound(ui_sound::move);_redraw=true;
                    }
                    if(bn::keypad::b_pressed())
                    {
                        _new_game_confirm=false;_home.repaint();_redraw=true;play_ui_sound(ui_sound::cancel);
                    }
                    else if(bn::keypad::a_pressed())
                    {
                        play_ui_sound(ui_sound::confirm);
                        if(_new_game_ok)
                        {
                            _new_game_confirm=false;_home_started=false;_debug_origin=false;
                            _menu.open_scene(scene_id::script);_story.new_game();_redraw=true;
                        }
                        else {_new_game_confirm=false;_home.repaint();_redraw=true;}
                    }
                }
                else
                {
                    handle_home(_home.update(_canvas,_story.progress()));
                    _redraw|=_home.painting();
                }
            }
        }
        else if(_pause_settings)
        {
            auto result=_home.update(_canvas,_story.progress());
            _redraw|=_home.painting();
            if(result==home_action::back)close_settings();
#if MANOSABA_DEBUG_MENU
            else if(result==home_action::debug)
            {
                close_settings();_menu.show();_debug_from_home=true;
            }
#endif
        }
        else if(gallery_page())
        {
            _redraw|=_gallery.update(_canvas.ready(),_story.progress());
            if(_gallery.done())
            {
                _gallery.suspend();
#if MANOSABA_DEBUG_MENU
                if(_menu.scene()==scene_id::gallery_demo)_menu.show();
                else
#endif
                {_debug_origin=false;_menu.open_scene(scene_id::title);_home.open(1,false,2);}
                _redraw=true;
            }
        }
        else if(story_page())
        {
            _redraw|=_story.update(_canvas.ready());
            if(_story.settings_requested()){_pause_settings=true;_home.open(2);_story.progress().editing_settings(true);_redraw=true;}
#if !MANOSABA_DEBUG_MENU
            if(!_story.owns_buttons())
            {_menu.open_scene(scene_id::title);_home_started=true;_home.open(0,_story.progress().has_hiro_save());_redraw=true;}
#endif
        }
        else if(_menu.scene()==scene_id::music)
        {
            if(!_debug_origin && bn::keypad::b_pressed())
            {
                music_command command;command.path="Bgm_36_1";command.loop=true;command.parameters=music_has_path|music_has_loop;
                music_player::execute(command);_menu.open_scene(scene_id::title);_home.open(1,false,3);_redraw=true;
            }
#if MANOSABA_DEBUG_MENU
            else if(_debug_origin)_redraw|=_music_demo.update();
#endif
            else _redraw|=_music_room.update(_canvas.ready(),_story.progress());
        }
#if MANOSABA_DEBUG_MENU
        else if(_menu.scene()==scene_id::sound)_redraw|=_sfx_demo.update();
        else if(_menu.scene()==scene_id::ending)
        {
            _redraw|=_ending.update(_canvas.ready());
            if(_ending.done() || bn::keypad::b_pressed()){_menu.show();_redraw=true;}
        }
        else if(_menu.scene()==scene_id::music_stress)
        {
            if(bn::keypad::up_pressed() || bn::keypad::down_pressed())
            { _stress_choice^=1; _redraw=true; }
            if(bn::keypad::a_pressed())
            {
                _menu.open_scene(_stress_choice ? scene_id::trial : scene_id::script);
                _story.test_music(true);
                if(_stress_choice)_story.start_trial();else _story.reset();
                _redraw=true;
            }
        }
#endif
    }
    if(_redraw){draw();_redraw=false;}
    else if(!_menu.open() && story_page() && !_pause_settings)_story.render(_canvas);
    if(story_page())_story.background_io();
}
}
