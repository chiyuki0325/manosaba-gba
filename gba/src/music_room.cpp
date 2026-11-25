#include "music_room.h"
#include "music_room_data.h"
#include "music_room_runtime.h"
#include "music_catalog.h"
#include "music_player.h"
#include "save_system.h"
#include "save_ui_data.h"
#include "text_canvas.h"
#include "sfx_ui.h"
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_format.h"
#include "bn_bg_palettes.h"
#include "bn_color.h"
extern "C"
{
    BN_DATA_EWRAM_BSS music_room_profile music_room_runtime = {};
}
namespace manosaba
{
namespace
{
constexpr int page_size = 5;
}
void music_room::open()
{
    music_player::reset();
    _selected = 0;
    _playing = -1;
    _revision = ~0u;
    suspend();
}
void music_room::suspend()
{
    if (music_room_runtime.active)
        bn::bg_palettes::set_transparent_color(bn::color(0, 0, 0));
    _phase = _fill = 0;
    _dirty = 255;
    _shown = false;
    music_room_runtime = {};
}
bool music_room::update(bool ready, const save_system& progress)
{
    if (progress.music_revision() != _revision)
    {
        _revision = progress.music_revision();
        _collected = 0;
        for (int i = 0; i < music_room_count; ++i)
            _collected += progress.music_unlocked(music_room_tracks[i].bit);
        _dirty = 255;
    }
    if (_playing >= 0 && !music_player::playing())
    {
        _playing = -1;
        _dirty |= 192;
    }
    unsigned missed = music_room_runtime.missed + unsigned(bn::core::last_missed_frames());
    music_room_runtime = {1,
                          unsigned(ready && _shown && !_dirty),
                          unsigned(_selected),
                          unsigned(_playing + 1),
                          unsigned(_selected / page_size),
                          unsigned(_collected),
                          unsigned(_phase),
                          _dirty,
                          missed};
    if (!ready || !_shown || _dirty)
        return _phase < 3 || _dirty || !_shown;
    int previous = _selected;
    if (bn::keypad::up_pressed())
        _selected = (_selected + music_room_count - 1) % music_room_count;
    else if (bn::keypad::down_pressed())
        _selected = (_selected + 1) % music_room_count;
    else if (bn::keypad::left_pressed() || bn::keypad::l_pressed() || bn::keypad::right_pressed() ||
             bn::keypad::r_pressed())
    {
        int pages = (music_room_count + page_size - 1) / page_size;
        int direction = bn::keypad::left_pressed() || bn::keypad::l_pressed() ? -1 : 1;
        int page = (_selected / page_size + pages + direction) % pages;
        _selected = bn::min(page * page_size + _selected % page_size, music_room_count - 1);
    }
    if (previous != _selected)
    {
        play_ui_sound(ui_sound::move);
        if (previous / page_size != _selected / page_size)
            _dirty |= 31 | 64;
        else
            _dirty |= (1u << (previous % page_size)) | (1u << (_selected % page_size));
        _dirty |= 128;
    }
    if (bn::keypad::a_pressed() && progress.music_unlocked(music_room_tracks[_selected].bit))
    {
        if (_playing == _selected)
        {
            music_player::reset();
            _playing = -1;
        }
        else
        {
            const auto& entry = music_catalog[music_room_tracks[_selected].catalog];
            music_command command;
            command.path = entry.alias;
            command.loop = entry.loop;
            command.parameters = music_has_path | music_has_loop | music_audition;
            music_player::execute(command);
            _playing = _selected;
        }
        play_ui_sound(ui_sound::confirm);
        _dirty |= 31 | 64 | 128;
    }
    return _dirty != 0;
}
void music_room::key(text_canvas& canvas, int x, int index, const char* label, bool disabled)
{
    const auto& icon = save_ui_keys[index];
    uint8_t pixels[18 * 14];
    for (int i = 0; i < icon.width * icon.height; ++i)
    {
        int value = icon.pixels[i];
        pixels[i] = uint8_t(!value       ? 0
                            : value == 1 ? (disabled ? 7 : 10)
                            : value == 2 ? (disabled ? 7 : 9)
                            : value == 6 ? 11
                                         : 8);
    }
    canvas.stamp(x, 142, icon.width, icon.height, pixels);
    if (*label)
        canvas.text(x + icon.width + 1, 142, label, disabled ? text_canvas::dark : text_canvas::muted, 238, 158);
}
void music_room::controls(text_canvas& canvas, bool disabled)
{
    canvas.pattern_restore(music_room_background, 124, 140, 52, 16);
    key(canvas, 124, 0, _playing == _selected ? "停止" : "播放", disabled);
}
void music_room::row(text_canvas& canvas, int row, const save_system& progress)
{
    int y = 27 + row * 18, index = _selected / page_size * page_size + row;
    canvas.pattern_restore(music_room_background, 8, y - 2, 224, 18);
    if (index >= music_room_count)
        return;
    bool selected = index == _selected, unlocked = progress.music_unlocked(music_room_tracks[index].bit);
    if (selected)
    {
        canvas.rectangle(8, y - 2, 224, 17, text_canvas::warning);
        canvas.rectangle(9, y - 1, 222, 15, text_canvas::selection);
        canvas.rectangle(9, y - 1, 2, 15, text_canvas::accent);
    }
    if (unlocked && index == _playing)
        for (int x = 0; x < 5; ++x)
            canvas.rectangle(13 + x, y + 4 + x / 2, 1, 7 - x, text_canvas::accent);
    auto number = bn::format<8>("{}{}", index < 9 ? "0" : "", index + 1);
    canvas.text(24, y + 1, number.c_str(), unlocked ? text_canvas::muted : text_canvas::dark, 42, y + 14);
    canvas.text(43, y + 1, unlocked ? music_room_tracks[index].title : "未收集",
                unlocked ? text_canvas::white : text_canvas::dark, 228, y + 14);
}
void music_room::status(text_canvas& canvas)
{
    canvas.pattern_restore(music_room_background, 8, 120, 224, 17);
    if (_playing >= 0)
    {
        for (int x = 0; x < 5; ++x)
            canvas.rectangle(10 + x, 126 + x / 2, 1, 7 - x, text_canvas::accent);
        canvas.text(22, 123, "播放中", text_canvas::accent, 64, 137);
        canvas.text(65, 123, music_room_tracks[_playing].title, text_canvas::white, 191, 137);
    }
    else
        canvas.text(10, 123, "随剧情收集", text_canvas::muted, 191, 137);
    auto page = bn::format<12>("{}/{}", _selected / page_size + 1, (music_room_count + page_size - 1) / page_size);
    canvas.text(232 - page.size() * 6, 123, page.c_str(), text_canvas::muted, 238, 137);
}
void music_room::draw(text_canvas& canvas, const save_system& progress)
{
    if (!_phase)
    {
        canvas.pattern_screen(music_room_background);
        canvas.visible(false);
        _phase = 1;
        return;
    }
    if (_phase == 1)
    {
        canvas.pattern_restore(music_room_background, 0, _fill, 240, 32);
        _fill += 32;
        if (_fill == 160)
            _phase = 2;
        return;
    }
    if (_phase == 2)
    {
        canvas.text(9, 6, "音乐馆");
        canvas.text(142, 6, "已收集", text_canvas::muted);
        canvas.rectangle(9, 21, 223, 1, text_canvas::warning);
        canvas.rectangle(9, 119, 223, 1, text_canvas::warning);
        canvas.text(6, 142, "\ue003", text_canvas::muted);
        canvas.text(22, 142, "选曲", text_canvas::muted);
        key(canvas, 53, 2, "");
        key(canvas, 73, 3, "");
        canvas.text(93, 142, "翻页", text_canvas::muted);
        key(canvas, 188, 1, "返回");
        _phase = 3;
        return;
    }
    if (_dirty & 32)
    {
        canvas.pattern_restore(music_room_background, 184, 6, 48, 14);
        auto count = bn::format<12>("{}/{}", _collected, music_room_count);
        canvas.text(232 - count.size() * 6, 6, count.c_str(), text_canvas::white, 238, 20);
        _dirty &= ~32u;
        return;
    }
    if (_dirty & 64)
    {
        status(canvas);
        _dirty &= ~64u;
        return;
    }
    if (_dirty & 128)
    {
        controls(canvas, !progress.music_unlocked(music_room_tracks[_selected].bit));
        _dirty &= ~128u;
        return;
    }
    for (int i = 0; i < page_size; ++i)
        if (_dirty & (1u << i))
        {
            row(canvas, i, progress);
            _dirty &= ~(1u << i);
            return;
        }
    if (!_shown && canvas.ready())
    {
        canvas.visible(true);
        _shown = true;
    }
}
} // namespace manosaba
