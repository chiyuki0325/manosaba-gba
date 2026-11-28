#ifndef MANOSABA_SAVE_SYSTEM_H
#define MANOSABA_SAVE_SYSTEM_H
#include "save_store.h"
#include "gallery_data.h"
#include "music_room_data.h"
#include "dialogue_page.h"
#include "bn_optional.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_palette_ptr.h"
namespace manosaba
{
class story_scene;
class text_canvas;
enum class save_ui_result
{
    none,
    resume,
    chapters,
    loaded,
    settings
};
class save_system
{
  public:
    void init();
    void new_story();
    void open_load()
    {
        _load_only = true;
        _view = 1;
        list(true);
        _paint = 0;
    }
    void settings_closed()
    {
        _paint = 0;
        _painted_pause = -1;
        _advance.reset();
    }
    bool settings_ready() const
    {
        return _global_checked && _operation != global_read;
    }
    bool has_hiro_save() const;
    bool has_saves() const;
    int dialogue_speed() const
    {
        return (_global.settings & 3) < 3 ? _global.settings & 3 : 0;
    }
    int music_level() const
    {
        return (_global.volume + 128) / 256;
    }
    int sound_level() const
    {
        return 4 - (_global.reserved <= 4 ? _global.reserved : 0);
    }
    bool original_line_breaks() const
    {
        return !(_global.settings & 8);
    }
    bool branch_hints() const
    {
        return !(_global.settings & 4);
    }
    void setting(int index, int value);
    void editing_settings(bool value)
    {
        _editing_settings = value;
    }
    void open_pause(bool failure = false);
    void close();
    bool active() const
    {
        return _view >= 0;
    }
    bool warning_pending() const
    {
        return _store.writes_paused() && !_warning_seen && !busy();
    }
    void return_after_warning()
    {
        _warning_resume = true;
    }
    bool busy() const
    {
        return _store.busy() || _operation != none;
    }
    bool retry_available() const
    {
        return _seed_size != 0;
    }
    bool retry_choice() const;
    void observe(story_scene& scene, bool ready);
    void tick(story_scene& scene, bool gameplay = true);
    bool gallery_unlocked(int index) const
    {
        return _global.gallery[index / 8] & (1u << (index % 8));
    }
    void unlock_gallery_asset(int asset);
    unsigned gallery_revision() const
    {
        return _gallery_revision;
    }
    bool music_unlocked(int bit) const
    {
        return _global.music[bit / 8] & (1u << (bit % 8));
    }
    void unlock_music(uint32_t key);
    unsigned music_revision() const
    {
        return _music_revision;
    }
    void pump();
    save_ui_result update(story_scene& scene, bool ready);
    void draw(text_canvas& canvas, story_scene& scene);
    bool painting() const
    {
        return dirty() && (_view >= 2 || !busy());
    }
    bool dirty() const
    {
        return _paint < 6 || _preview_row >= 0;
    }

  private:
    save_store _store;
    uint8_t* _buffer = nullptr;
    uint8_t _seed[save_seed_limit];
    unsigned _seed_size = 0, _seed_pc = 0;
    int _seed_script = -1, _auto_script = -1, _auto_attempt = -1;
    enum operation
    {
        none,
        manual_write,
        load_record,
        auto_write,
        retry_write,
        global_read,
        global_write,
        compact_read,
        compact_write
    };
    operation _operation = none;
    operation _pending_operation = none;
    int _pending_slot = -1, _compact_slot = -1;
    struct global_data
    {
        uint16_t volume = 1024;
        uint8_t settings = 0, reserved = 0;
        uint8_t scripts[64] = {}, endings[2] = {}, book[21] = {}, legacy_padding = 0, gallery[gallery_flag_bytes] = {},
                music[music_flag_bytes] = {};
    } _global;
    static_assert(sizeof(global_data) == 120);
    unsigned _gallery_revision = 0, _music_revision = 0;
    uint32_t _music_key = 0;
    int _view = -1, _pause = 0, _selected[2] = {}, _first = 0, _paint = 0, _confirm = 0, _confirm_choice = 0,
        _notice = 0, _last_progress = -1;
    bool _load_only = false, _editing_settings = false;
    bool _failure = false, _seed_dirty = false, _global_dirty = false, _remembered = false;
    advance_button _advance;
    save_metadata _rows[2];
    bn::optional<bn::sprite_ptr> _thumbs[2];
    bn::optional<bn::sprite_tiles_ptr> _thumb_tiles[2];
    bn::optional<bn::sprite_palette_ptr> _thumb_palettes[2];
    bool _global_checked = false, _load_ready = false;
    bool _warning_open = false, _warning_seen = false, _warning_resume = false, _clear_panel = false;
    int _preview_row = -1, _preview_line = 0, _row_line = 0;
    int _line_offset = 0, _line_x = 78;
    int _painted_pause = -1;
    bool _confirm_static = false;
    uint8_t _confirm_stage = 0;
    bool _header_restored = false;
    const char* _message = "";
    bool buffer();
    void free_buffer();
    int selected_slot() const
    {
        return _view == 1 ? (_selected[1] ? _selected[1] - 1 : save_auto_slot) : _selected[0];
    }
    void list(bool header = false);
    bool write(story_scene& scene, int slot, operation kind);
    bool compact(int slot, operation kind);
    bool convert_loaded_record(unsigned& length, unsigned& seed);
    void mark(const char* message);
    void key(text_canvas& canvas, int x, int y, int index, const char* label);
    void heading(text_canvas& canvas, story_scene& scene);
    bool row(text_canvas& canvas, int index);
    bool row_text(text_canvas& canvas, int y, const char* text, int color);
    void thumbnail(text_canvas& canvas, int row, const save_metadata& metadata);
};
} // namespace manosaba
#endif
