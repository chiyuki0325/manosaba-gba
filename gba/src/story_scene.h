#ifndef STORY_SCENE_H
#define STORY_SCENE_H
#include "story_vm.h"
#include "image_scene.h"
#include "dialogue_presenter.h"
#include "text_canvas.h"
#include "trial_player.h"
#include "witch_book.h"
#include "dialogue_history_view.h"
#include "save_system.h"
#include "music_player.h"
#include "sfx_player.h"
#include "credits_player.h"
#include "execution_player.h"
namespace manosaba
{
class text_canvas;
class story_scene
{
  public:
    void protect() const
    {
        _images.protect();
        _trial.protect();
    }
    story_scene();
    void reset();
    void start_trial();
    void new_game();
    void open_chapters();
    save_system& progress()
    {
        return _saves;
    }
    bool settings_requested()
    {
        bool value = _settings_requested;
        _settings_requested = false;
        return value;
    }
    void settings_closed()
    {
        _saves.settings_closed();
        _dialogue.reset();
    }
    bool update(bool display_ready = true);
    void draw(text_canvas& canvas);
    bool painting() const;
    void published(bool ready);
    void background_io()
    {
        _saves.pump();
    }
    void persistent_tick()
    {
        _saves.tick(*this, false);
        _saves.pump();
    }
    const save_system& progress() const
    {
        return _saves;
    }
    void set_visible(bool visible);
    void render(text_canvas& canvas);
    int commit(int bytes)
    {
        return overlay() ? bytes : !_selecting && _vm.trial() ? _trial.commit(bytes) : _images.commit(bytes);
    }
    bool owns_buttons() const
    {
        return !_selecting;
    }
    bool paused() const
    {
        return _paused;
    }
    bool test_music() const
    {
        return _test_music;
    }
    void test_music(bool enabled)
    {
        _test_music = enabled;
    }
    int snapshot(uint8_t* destination, int size, bool retry = false);
    bool restore_snapshot(uint8_t* source, int size);
    void restore_play_time(uint32_t frames)
    {
        _play_frames = frames;
    }
    save_metadata save_info() const;
    bool auto_save_ready() const;
    bool retry_identity(int& script, unsigned& pc) const;
    bool save_allowed() const
    {
        return !_test_music && !_selecting && story_scripts[_vm.script()].mode < 2;
    }
    bool hiro() const;
    int script() const
    {
        return _vm.script();
    }
    int bad_end_id() const
    {
        return _vm.bad_end() ? _vm.bad_id() : 0;
    }
    int reading_book_entry() const
    {
        return _book.reading_entry();
    }

  private:
    story_vm _vm;
    image_scene _images;
    trial_player _trial{_vm, _images};
    witch_book _book{_vm};
    dialogue_history_view _history{_vm};
    dialogue_presenter _dialogue;
    credits_player _credits;
    execution_player _execution;
    advance_button _advance;
    save_system _saves;
    music_checkpoint _saved_music;
    sfx_checkpoint _saved_sfx;
    bool _settings_requested = false;
    bool _restoring = false, _restore_canvas = false;
    int _active_stage = -1;
    uint32_t _play_frames = 0;
    int _selected = 0;
    int _first_script = 0, _script_count = 0;
    int _choice = 0, _first_choice = 0;
    bool _selecting = true;
    bool _visible = false;
#ifdef MANOSABA_MUSIC_STRESS
    bool _test_music = true;
#else
    bool _test_music = false;
#endif
    bool _paused = false, _overlay_suspended = false, _overlay_released = false, _overlay_opening = false;
    void progressed();
    void select_current_script();
    const dialogue_layout& layout() const;
    bool fast_forward() const;
    void prepare_page();
    bool overlay() const
    {
        return _paused || _book.active() || _history.active();
    }
    void suspend_overlay();
    void resume_overlay();
};
} // namespace manosaba
#endif
