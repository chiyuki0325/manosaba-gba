#include "story_scene.h"
#include "text_canvas.h"
#include "bn_keypad.h"
#include "bn_core.h"
#include "bn_string.h"
#include "story_ui_data.h"
#include "story_layout.h"
#include "choice_window.h"
#include "music_player.h"
#include "sfx_player.h"
#include "sfx_ui.h"
#include "trial_runtime.h"
#include "save_archive.h"
#include "save_checkpoint.h"
#include "save_runtime.h"
#include "book_theme.h"
#include "bn_string_view.h"
#include <cstring>

namespace manosaba
{
story_scene::story_scene()
{
    _vm.bind_music(
        [](void* context, const music_command& command)
        {
            auto& self = *static_cast<story_scene*>(context);
            if (self._test_music)
                return false;
            if (command.path && bn::string_view(command.path).find("Ambi_") == 0 &&
                bn::string_view(command.path) != "Ambi_3_1")
            {
                sfx_command sound;
                sound.cue = sfx_player::find(command.path);
                sound.volume = command.volume;
                sound.loop = command.loop;
                sound.stop = command.stop;
                sound.ambient = true;
                sound.fade_frames = command.stop ? command.duration_frames : command.fade_in_frames;
                sfx_player::execute(sound);
                return false;
            }
            if (command.stop && !(command.parameters & music_has_path))
            {
                sfx_command sound;
                sound.stop = sound.ambient = true;
                sound.fade_frames = command.duration_frames;
                sfx_player::execute(sound);
            }
            music_player::execute(command);
            return command.wait && music_player::busy();
        },
        this);
    _vm.bind_sfx([](void*, const sfx_command& command) { sfx_player::execute(command); }, this);
    _saves.init();
}

bool story_scene::hiro() const
{
    return hiro_book_theme(_vm);
}
void story_scene::published(bool ready)
{
    _saves.observe(*this, ready);
    _book.observe(_paused, ready);
    _history.observe(ready);
    save_runtime.pc = _vm.position();
    save_runtime.restoring = unsigned(_restoring);
}
int story_scene::snapshot(uint8_t* destination, int size, bool)
{
    save_archive a(destination, size, false);
    uint8_t flags = _vm.state() == story_state::stage && (_vm.trial() ? _trial.stage_pending() : _active_stage < 0)
                        ? save_stage_pending
                        : 0;
    _vm.checkpoint(a, flags);
    _images.checkpoint(a, _vm.graphical_bank(), _vm.trial() ? 0 : _vm.visual_frame(), !_vm.trial());
    auto music = _restoring ? _saved_music : music_player::checkpoint();
    music.archive(a);
    auto sounds = _restoring ? _saved_sfx : sfx_player::checkpoint();
    sounds.archive(a);
    return a.good() && a.size() == save_checkpoint_bytes ? a.size() : -1;
}
bool story_scene::restore_snapshot(uint8_t* source, int size)
{
    _book.close();
    _history.close();
    _images.reset();
    _trial.reset();
    _dialogue.reset();
    _credits.suspend();
    _execution.suspend();
    uint8_t flags = 0;
    save_archive a(source, size, true);
    if (size != save_checkpoint_bytes)
        return false;
    flags = _vm.checkpoint(a);
    if (!a.good())
        return false;
    _images.checkpoint(a, _vm.graphical_bank(), _vm.trial() ? 0 : _vm.visual_frame(), !_vm.trial());
    _saved_music.archive(a);
    _saved_sfx.archive(a);
    if (!a.good() || a.size() != size)
        return false;
    if ((flags & save_stage_pending) && _vm.stage_event() >= 0)
    {
        _images.apply(_vm.stage_event());
        _images.settle();
    }
    _active_stage = _vm.state() == story_state::stage ? _vm.stage_event() : -1;
    _choice = _first_choice = 0;
    _first_script = _vm.trial() ? story_trial_first : 0;
    _script_count = _vm.trial() ? story_trial_count : story_selectable_count;
    _book.reset_view();
    _history.close();
    _dialogue.original_breaks(_saves.original_line_breaks());
    _trial.original_breaks(_saves.original_line_breaks());
    _dialogue.begin(_vm.text(), layout());
    _dialogue.finish();
    _trial.restored();
    if (_vm.state() == story_state::credits)
        _credits.begin(_vm.credits_act(), true);
    if (_vm.state() == story_state::execution)
        _execution.begin();
    _selecting = _paused = false;
    _overlay_suspended = _overlay_opening = false;
    _advance.reset();
    _restoring = _restore_canvas = true;
    return true;
}
save_metadata story_scene::save_info() const
{
    save_metadata info;
    info.script = uint16_t(_vm.script());
    info.hero = uint8_t(hiro());
    info.trial = uint8_t(_vm.trial());
    info.play_frames = _play_frames;
    info.trial_left = _vm.trial_left();
    if (_vm.reenact() && _vm.reenact_scene() >= 0)
        info.preview = uint16_t(0x8000 | _vm.reenact_scene());
    else if (_vm.trial())
        info.preview = uint16_t(0x4000 | (_vm.trial_layout() != 0));
    else
    {
        for (int i = 0; i < stage_entity_count; ++i)
        {
            int image = _images.player().entity(i).image;
            if (image >= 0 && image < graphic_asset_count && graphic_assets[image].kind == graphic_kind::bg)
            {
                info.preview = uint16_t(image);
                break;
            }
        }
    }
    const char* text = _vm.text();
    int out = 0;
    while (*text && out < 93)
    {
        unsigned byte = uint8_t(*text);
        int count = byte < 128 ? 1 : byte < 224 ? 2 : byte < 240 ? 3 : 4;
        if (out + count >= int(sizeof(info.summary)))
            break;
        if (byte == '\n' || byte == '\r')
        {
            info.summary[out++] = ' ';
            ++text;
        }
        else if (byte == 0xee && uint8_t(text[1]) >= 0x80 && uint8_t(text[1]) <= 0x83)
            text += count;
        else
        {
            std::memcpy(info.summary + out, text, unsigned(count));
            out += count;
            text += count;
        }
    }
    info.summary[out] = 0;
    return info;
}
bool story_scene::auto_save_ready() const
{
    if (!save_allowed() || overlay() || _restoring || _vm.bad_end())
        return false;
    const char* path = story_scripts[_vm.script()].path;
    if (bn::string_view(path).find("_Adv") == bn::string_view::npos &&
        bn::string_view(path).find("_Trial") == bn::string_view::npos)
        return false;
    return _vm.state() == story_state::text || _vm.state() == story_state::debate;
}
bool story_scene::retry_identity(int& script, unsigned& pc) const
{
    if (_selecting || _restoring || _test_music || overlay())
        return false;
    bool trial = _vm.state() == story_state::title && _vm.cutin_kind() == 0 && story_scripts[_vm.script()].mode == 1;
    bool choice = !_vm.trial() && _vm.state() == story_state::menu && !_vm.map_menu();
    if (!trial && !choice)
        return false;
    script = _vm.script();
    pc = _vm.source_position();
    return true;
}

void story_scene::reset()
{
    _saves.new_story();
    _restoring = _restore_canvas = false;
    _play_frames = 0;
    music_player::reset();
    sfx_player::reset();
    _book.close();
    _history.close();
    _paused = _overlay_suspended = _overlay_released = _overlay_opening = false;
    _images.reset();
    _credits.suspend();
    _execution.suspend();
    _trial.reset();
    _active_stage = -1;
    _selected = 0;
    _first_script = 0;
    _script_count = story_selectable_count;
    _selecting = true;
    _dialogue.reset();
    _advance.reset();
}

void story_scene::new_game()
{
    reset();
    _selecting = false;
    _vm.start(0);
    progressed();
}
void story_scene::open_chapters()
{
    _book.close();
    _history.close();
    _paused = false;
    select_current_script();
    _selecting = true;
    music_player::reset();
    sfx_player::reset();
    _saves.new_story();
    _overlay_suspended = false;
    _images.reset();
    _trial.reset();
    _dialogue.reset();
    _advance.reset();
}
void story_scene::start_trial()
{
    reset();
    _first_script = story_trial_first;
    _script_count = story_trial_count;
}

void story_scene::select_current_script()
{
    int index = _vm.script() - _first_script;
    if (index >= 0 && index < _script_count)
        _selected = index;
}

void story_scene::set_visible(bool visible)
{
    if (visible != _visible)
    {
        _dialogue.repaint();
        if (visible)
            _advance.reset();
        else
        {
            _trial.suspend();
            _credits.suspend();
            _execution.suspend();
        }
    }
    _visible = visible;
    if (!_vm.trial() || _selecting)
        _images.set_visible(visible && !overlay() && !_selecting && !_vm.bad_end() && _vm.visual_frame() >= 0 &&
                            _vm.state() != story_state::menu);
}

void story_scene::suspend_overlay()
{
    if (_overlay_suspended)
        return;
    _images.set_visible(false);
    _trial.suspend();
    _credits.suspend();
    _execution.suspend();
    _overlay_suspended = _overlay_opening = true;
    _overlay_released = false;
    _dialogue.repaint();
    _advance.reset();
}
void story_scene::resume_overlay()
{
    _overlay_suspended = _overlay_opening = false;
    _dialogue.repaint();
    _advance.reset();
    if (_book.active())
        _book.repaint();
    if (_history.active())
        _history.repaint();
}

void story_scene::progressed()
{
    _choice = _first_choice = 0;
    _dialogue.reset();
    _advance.reset();
    if (_vm.state() == story_state::credits)
    {
        _images.reset();
        _trial.reset();
        _credits.begin(_vm.credits_act(), true);
        return;
    }
    if (_vm.state() == story_state::execution)
        _execution.begin();
    if (_vm.trial())
    {
        _images.reset();
        _trial.reset();
        return;
    }
    if (_vm.bad_end())
    {
        _images.reset();
        return;
    }
    _images.set_bank(_vm.graphical_bank());
    if (_vm.visual_frame() >= 0)
        _images.request(_vm.visual_frame());
    else
        _images.reset();
}
const dialogue_layout& story_scene::layout() const
{
    return _vm.visual_frame() >= 0 ? graphical_dialogue : story_dialogue;
}

void story_scene::prepare_page()
{
    _dialogue.original_breaks(_saves.original_line_breaks());
    _dialogue.speed(fast_forward() ? 2 : _saves.dialogue_speed());
    _dialogue.begin(_vm.text(), layout());
    _advance.reset();
}

bool story_scene::fast_forward() const
{
    return !_selecting && !_restoring && _vm.state() == story_state::text && bn::keypad::r_held();
}

void story_scene::render(text_canvas& canvas)
{
    int fast = 0;
    if (_visible && !_selecting && !_restoring && !overlay())
        fast = _vm.trial() ? _trial.fast_forward_icon() : fast_forward() ? 216 : 0;
    canvas.fast_forward(fast);
    if (!_selecting && _vm.state() == story_state::credits)
    {
        canvas.toast(0);
        return;
    }
    if (!_selecting && !overlay() && _vm.state() == story_state::execution)
    {
        if (!_images.ready())
            _images.render(canvas);
        if (_images.ready())
            _images.dim(true);
        canvas.toast(0);
        return;
    }
    if (!overlay() && !_selecting && _vm.trial())
        _trial.render(canvas);
    if (!overlay() && (!_vm.trial() || _selecting))
        _images.render(canvas);
    if (_visible && !_selecting && !overlay() && !_test_music)
        for (int plane = 0; plane < 2; ++plane)
            _saves.unlock_gallery_asset(_images.visible_background(plane));
    auto state = _vm.state();
    bool readable = state == story_state::text || state == story_state::debate || state == story_state::stage ||
                    state == story_state::running || state == story_state::camera || state == story_state::audio ||
                    state == story_state::screen;
    int notification = _vm.toast();
    if (!notification && _vm.live())
        notification = 3;
    canvas.toast(_visible && !_selecting && !_restoring && !overlay() && readable ? notification : 0);
}

bool story_scene::update(bool display_ready)
{
    _dialogue.original_breaks(_saves.original_line_breaks());
    _trial.dialogue_speed(_saves.dialogue_speed());
    _trial.original_breaks(_saves.original_line_breaks());
    if (!_selecting && !_restoring && !_test_music && story_scripts[_vm.script()].mode < 2)
        _saves.unlock_music(music_player::collection_key());
    if (!_selecting && !_restoring && !overlay())
    {
        int frames = 1 + bn::core::last_missed_frames();
        if (!_vm.bad_end())
            _play_frames += unsigned(frames);
        if (display_ready)
            _vm.tick_toast(frames);
    }
    _saves.tick(*this);
    save_runtime.pc = _vm.position();
    save_runtime.restoring = unsigned(_restoring);
    if (_restoring)
    {
        bool ready = display_ready && !_restore_canvas;
        bool redraw = _restore_canvas;
        if (_book.active())
        {
            redraw |= _book.dirty();
            ready = ready && !_book.dirty();
        }
        else if (_history.active())
        {
            redraw |= _history.dirty();
            ready = ready && !_history.dirty();
        }
        else if (_vm.state() == story_state::credits)
        {
            redraw = true;
            ready = ready && _credits.shown();
        }
        else if (_vm.state() == story_state::execution)
        {
            _images.update(0);
            redraw = true;
            ready = ready && _images.ready() && _execution.shown();
        }
        else if (_vm.trial())
        {
            ready = _trial.rebuild_ready(ready);
            redraw |= _trial.rebuild_draw();
        }
        else if (_vm.visual_frame() >= 0 && _vm.state() != story_state::menu)
        {
            _images.update(0);
            redraw |= _dialogue.redrawing();
            ready = ready && _images.ready() && !_dialogue.redrawing();
        }
        if (ready && !bn::keypad::a_held() && !bn::keypad::b_held() && !bn::keypad::start_held())
        {
            music_player::restore(_saved_music);
            sfx_player::restore(_saved_sfx);
            _restoring = false;
            _advance.reset();
        }
        return redraw;
    }
    trial_runtime.pc = _vm.position();
    trial_runtime.vm_state = unsigned(_vm.state());
    trial_runtime.vm_script = unsigned(_vm.script());
    trial_runtime.vm_hp = unsigned(_vm.hp());
    _book.observe(_paused, display_ready);
    _history.observe(display_ready);
    if (!_selecting && !_paused && bn::keypad::start_pressed())
    {
        play_ui_sound(ui_sound::confirm);
        _paused = true;
        _saves.open_pause(_vm.bad_end());
        suspend_overlay();
        return true;
    }
    if (_paused)
    {
        auto result = _saves.update(*this, display_ready);
        if (result == save_ui_result::resume)
        {
            _paused = false;
            _restore_canvas = true;
            resume_overlay();
            return true;
        }
        if (result == save_ui_result::loaded)
            return true;
        if (result == save_ui_result::settings)
        {
            _settings_requested = true;
            return false;
        }
        if (result == save_ui_result::chapters)
        {
            open_chapters();
            return true;
        }
        return _saves.dirty();
    }
    if (_book.active())
    {
        if (bn::keypad::l_pressed() || bn::keypad::r_pressed())
            play_ui_sound(ui_sound::tab);
        else if (bn::keypad::up_pressed() || bn::keypad::down_pressed() || bn::keypad::left_pressed() ||
                 bn::keypad::right_pressed())
            play_ui_sound(ui_sound::move);
        auto result = _book.update(display_ready);
        if (result == book_result::none)
        {
            if (bn::keypad::a_pressed())
                play_ui_sound(ui_sound::confirm);
            else if (bn::keypad::b_pressed())
                play_ui_sound(ui_sound::cancel);
        }
        if (result != book_result::none)
        {
            bool forced = _book.forced();
            int choice = _book.count() ? _book.source() : 0;
            play_ui_sound(ui_sound::close_book);
            _book.close();
            resume_overlay();
            if (forced)
            {
                if (result == book_result::submit)
                {
                    if (_vm.map_menu())
                        _vm.choose(choice);
                    else
                        _vm.choose_item(choice);
                }
                else
                    _vm.cancel_inventory();
                if (_vm.trial())
                    _trial.external_progress();
                else
                    progressed();
            }
            return true;
        }
        return _book.dirty();
    }
    if (_history.active())
    {
        if (_history.update())
        {
            _history.close();
            resume_overlay();
            return true;
        }
        return _history.dirty();
    }
    if (!_selecting && _vm.state() == story_state::execution)
    {
        _images.update(0);
        bool changed = _execution.update(bn::keypad::a_held(), display_ready && _images.ready());
        if (_execution.done())
        {
            _execution.suspend();
            _images.dim(false);
            _restore_canvas = true;
            _vm.advance();
            progressed();
            return true;
        }
        return changed;
    }
    if (!_selecting && _vm.state() == story_state::credits)
    {
        bool changed = _credits.update(display_ready);
        if (_credits.done())
        {
            _credits.suspend();
            _restore_canvas = true;
            _vm.advance();
            progressed();
            return true;
        }
        return changed;
    }
    if (!_selecting && bn::keypad::l_pressed())
    {
        _history.open();
        suspend_overlay();
        return true;
    }
    if (!_selecting && (bn::keypad::select_pressed() || _vm.state() == story_state::inventory || _vm.map_menu()))
    {
        bool forced = _vm.state() == story_state::inventory || _vm.map_menu();
        play_ui_sound(ui_sound::open_book);
        _book.open(forced);
        suspend_overlay();
        return true;
    }
    if (!_selecting && _vm.state() == story_state::audio)
    {
        if (music_player::busy())
            return false;
        _vm.advance();
        progressed();
        return true;
    }
    if (!_selecting && _vm.trial())
    {
        bool changed = _trial.update(display_ready);
        if (_trial.exited())
        {
            select_current_script();
            _selecting = true;
            _trial.reset();
            music_player::reset();
            sfx_player::reset();
            return true;
        }
        if (!_vm.trial())
        {
            _trial.reset();
            progressed();
            return true;
        }
        return changed;
    }

    if (!_selecting && !_vm.bad_end() && _vm.visual_frame() >= 0 && _vm.state() != story_state::menu)
    {
        _images.update(1 + bn::core::last_missed_frames(), bn::keypad::a_pressed());
        bool dispatched = _vm.state() == story_state::stage;
        for (int budget = 0; budget < 8 && _vm.state() == story_state::stage; ++budget)
        {
            bool applied = _active_stage < 0;
            if (applied)
            {
                _active_stage = _vm.stage_event();
                _images.apply(_active_stage);
            }
            if (_images.blocked())
                return applied;
            _active_stage = -1;
            _vm.advance();
            progressed();
        }
        if (dispatched)
            return true;
    }
    if (_dialogue.redrawing())
        return true;
    bool loading = !_selecting && !_vm.bad_end() && _vm.state() != story_state::menu && _vm.visual_frame() >= 0 &&
                   !_images.ready();
    bool readable = _selecting || _vm.bad_end() || _vm.state() == story_state::menu || _dialogue.prepared();
    bool fast = fast_forward();
    if (!fast)
        _dialogue.reset_fast_forward();
    if (_dialogue.speed(fast ? 2 : _saves.dialogue_speed()) && !_selecting && _vm.state() == story_state::text)
        return true;
    int frames = 1 + bn::core::last_missed_frames();
    _dialogue.frame(display_ready && !loading);
    bool advance = _advance.update(bn::keypad::a_held(), display_ready && !loading && readable, frames);
    if (_selecting)
    {
        int previous = _selected;
        if (bn::keypad::up_pressed())
        {
            _selected = (_selected + _script_count - 1) % _script_count;
        }
        else if (bn::keypad::down_pressed())
        {
            _selected = (_selected + 1) % _script_count;
        }
        else if (bn::keypad::left_pressed())
        {
            _selected = _selected >= 6 ? _selected - 6 : _script_count - 1;
        }
        else if (bn::keypad::right_pressed())
        {
            _selected = _selected + 6 < _script_count ? _selected + 6 : 0;
        }
        if (advance)
        {
            _saves.new_story();
            _play_frames = 0;
            _active_stage = -1;
            music_player::reset();
            sfx_player::reset();
            if (_test_music)
                music_player::play_test_track();
            _vm.start(_first_script + _selected);
            _selecting = false;
            progressed();
            return true;
        }
        return previous != _selected;
    }
    if (_vm.bad_end())
    {
        if (advance)
        {
            _paused = true;
            _saves.open_pause(true);
            suspend_overlay();
            return true;
        }
        return false;
    }
    if (_vm.state() == story_state::stage)
        return true;
    if (_vm.state() == story_state::running)
    {
        _vm.advance();
        progressed();
        return _vm.state() != story_state::running;
    }
    if (_vm.state() == story_state::menu)
    {
        int previous = _choice;
        if (bn::keypad::up_pressed())
        {
            _choice = (_choice + _vm.option_count() - 1) % _vm.option_count();
        }
        else if (bn::keypad::down_pressed())
        {
            _choice = (_choice + 1) % _vm.option_count();
        }
        if (advance)
        {
            _vm.choose(_choice);
            progressed();
            return true;
        }
        return previous != _choice;
    }
    if (fast && readable && display_ready && !loading && _dialogue.fast_forward(frames))
    {
        _vm.advance();
        progressed();
        return true;
    }
    if (advance)
    {
        auto result = _dialogue.advance(_vm.text(), layout());
        if (result == dialogue_advance::wait)
            return false;
        if (result == dialogue_advance::page)
            _advance.reset();
        if (result != dialogue_advance::script)
            return true;
        if (_vm.state() == story_state::end)
        {
            select_current_script();
            _selecting = true;
            music_player::reset();
            sfx_player::reset();
            _images.reset();
        }
        else
        {
            _vm.advance();
            progressed();
        }
        return true;
    }
    if (display_ready && !loading && _dialogue.prepared())
    {
        if (_dialogue.tick())
            return true;
    }
    return false;
}

bool story_scene::painting() const
{
    if (overlay())
        return !_overlay_released || (_paused             ? _saves.painting()
                                      : _history.active() ? _history.painting()
                                                          : _book.painting());
    if (!_selecting && _vm.trial())
        return _trial.painting();
    return _dialogue.changed() || _dialogue.redrawing();
}
void story_scene::draw(text_canvas& canvas)
{
    if (_restore_canvas)
    {
        canvas.release();
        _restore_canvas = false;
    }
    if (!_selecting && !_book.active() && !_history.active() &&
        (_vm.state() == story_state::inventory || _vm.map_menu()))
    {
        _book.open(true);
        suspend_overlay();
    }
    set_visible(_visible);
    if (overlay())
    {
        _book.observe(_paused, false);
        _history.observe(false);
        _overlay_opening = false;
        if (!_overlay_released)
        {
            if (!_trial.release())
                return;
            _overlay_released = true;
        }
        if (_paused)
        {
            _saves.draw(canvas, *this);
            canvas.present();
        }
        else if (_history.active())
            _history.draw(canvas);
        else
            _book.draw(canvas);
        return;
    }
    if (!_selecting && _vm.state() == story_state::credits)
    {
        _credits.draw(canvas);
        return;
    }
    if (!_selecting && _vm.state() == story_state::execution)
    {
        _execution.draw(canvas);
        return;
    }
    if (!_selecting && _vm.state() == story_state::audio)
        return;
    if (!_selecting && _vm.trial())
    {
        _trial.draw(canvas);
        return;
    }
    if (!_selecting && _vm.bad_end())
    {
        canvas.bad_end();
        return;
    }
    _images.invalidate();
    if (_dialogue.changed() || _dialogue.redrawing())
    {
        _dialogue.draw(canvas, _vm.text(), layout(), _dialogue.redrawing(), 16, nullptr, 0, _vm.text_style());
        if (_vm.visual_frame() >= 0)
            _dialogue.prompt(canvas, _vm.text());
        canvas.present();
        return;
    }
    if (!_selecting && _vm.state() != story_state::menu && _vm.state() != story_state::running &&
        _vm.state() != story_state::stage && !_dialogue.prepared())
        prepare_page();
    if (!_selecting && _vm.visual_frame() >= 0 && _vm.state() != story_state::menu)
    {
        canvas.decoration(story_ui_tiles);
        if (_vm.state() != story_state::stage && _vm.state() != story_state::running)
        {
            const char* speaker = _vm.state() == story_state::text ? _vm.speaker() : "系统";
            dialogue_presenter::speaker(canvas, speaker);
            _dialogue.draw(canvas, _vm.text(), layout(), true, 8, nullptr, 0, _vm.text_style());
            _dialogue.prompt(canvas, _vm.text());
        }
        canvas.present();
        return;
    }
    if (!_selecting && _vm.state() == story_state::menu)
    {
        canvas.choice_screen();
        int heights[8];
        for (int i = 0; i < _vm.option_count(); ++i)
            heights[i] = 40;
        auto window = visible_choices(heights, _vm.option_count(), _choice, _first_choice, 0, 144);
        _first_choice = window.first;
        int top = (168 - window.height) / 16 * 8;
        for (int index = window.first; index < window.first + window.count; ++index)
            canvas.choice_button(top + (index - window.first) * 40, _vm.option(index).text, index == _choice,
                                 _vm.option_bad(index) && _saves.branch_hints());
        if (_vm.option_count() > 3)
        {
            auto page = bn::to_string<8>(_choice + 1);
            page += "/";
            page += bn::to_string<8>(_vm.option_count());
            canvas.text(111, 145, page.c_str(), text_canvas::white);
        }
        canvas.present();
        return;
    }
    canvas.clear();
    if (_selecting)
    {
        canvas.text(12, 8, _first_script ? "选择审判章节" : "选择故事剧本", text_canvas::accent);
        auto page = bn::to_string<16>(_selected / 6 + 1);
        page += "/";
        page += bn::to_string<16>((_script_count + 5) / 6);
        canvas.text(192, 8, page.c_str(), text_canvas::muted);
        canvas.rectangle(12, 25, 216, 1, text_canvas::selection);
        int first = _selected / 6 * 6;
        for (int index = first; index < first + 6 && index < _script_count; ++index)
        {
            int y = 32 + (index - first) * 15;
            if (index == _selected)
            {
                canvas.rectangle(10, y - 1, 220, 14, text_canvas::selection);
                canvas.text(12, y, ">", text_canvas::accent);
            }
            canvas.text(24, y, story_scripts[_first_script + index].title);
        }
        canvas.text(12, 128, "上下选择 左右翻页 A播放", text_canvas::muted);
        canvas.text(12, 144, "Start/B 菜单 L/R 切换画面", text_canvas::muted);
    }
    else
    {
        canvas.text(12, 4, story_scripts[_vm.script()].title, text_canvas::accent);
        canvas.text(12, 20, "背景占位", text_canvas::muted);
        canvas.text(66, 20, _vm.backdrop(), text_canvas::muted, 228, 32);
        canvas.text(12, 36, "人物占位", text_canvas::muted);
        canvas.text(66, 36, _vm.character(), text_canvas::muted, 228, 48);
        canvas.text(12, 52, "音乐占位", text_canvas::muted);
        canvas.text(66, 52, _vm.music(), text_canvas::muted, 228, 64);
        canvas.rectangle(12, 69, 216, 1, text_canvas::selection);
        if (_vm.state() != story_state::running)
        {
            canvas.text(12, 74, _vm.state() == story_state::text ? _vm.speaker() : "系统", text_canvas::accent);
            _dialogue.draw(canvas, _vm.text(), layout(), true, 16, nullptr, 0, _vm.text_style());
            canvas.text(12, 144,
                        !_dialogue.complete()                ? "A 补全本页  Select 剧本列表"
                        : _vm.text()[_dialogue.page().end()] ? "A 下一页  Select 剧本列表"
                        : _vm.state() == story_state::end    ? "A 返回列表  Select 剧本列表"
                                                             : "A 继续  Select 剧本列表",
                        text_canvas::muted);
        }
    }
    canvas.present();
}
} // namespace manosaba
