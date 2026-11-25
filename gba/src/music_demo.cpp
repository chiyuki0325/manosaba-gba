#include "music_demo.h"
#include "music_catalog.h"
#include "music_player.h"
#include "text_canvas.h"
#include "bn_keypad.h"
#include "bn_string.h"

namespace manosaba
{
namespace
{
constexpr int page_size = 4;
}

void music_demo::reset()
{
    _selected = music_default_index;
    _playing = -1;
}

bool music_demo::update()
{
    bool redraw = false;
    if (_playing >= 0 && !music_player::playing())
    {
        _playing = -1;
        redraw = true;
    }
    int previous = _selected;
    if (bn::keypad::up_pressed())
        _selected = (_selected + music_count - 1) % music_count;
    else if (bn::keypad::down_pressed())
        _selected = (_selected + 1) % music_count;
    else if (bn::keypad::left_pressed())
        _selected = _selected >= page_size ? _selected - page_size : music_count - 1;
    else if (bn::keypad::right_pressed())
        _selected = _selected + page_size < music_count ? _selected + page_size : 0;
    if (bn::keypad::a_pressed() && music_catalog[_selected].module_id >= 0)
    {
        const auto& entry = music_catalog[_selected];
        music_player::reset();
        music_command command;
        command.path = entry.alias;
        command.loop = entry.loop;
        command.parameters = music_has_path | music_has_loop | music_audition;
        music_player::execute(command);
        _playing = _selected;
        redraw = true;
    }
    if (bn::keypad::select_pressed())
    {
        music_player::reset();
        _playing = -1;
        redraw = true;
    }
    return redraw || previous != _selected;
}

void music_demo::draw(text_canvas& canvas) const
{
    canvas.clear();
    canvas.text(12, 6, "音乐 DEMO", text_canvas::accent);
    if (_playing >= 0)
        canvas.text(108, 6, "播放中", text_canvas::accent);
    auto page = bn::to_string<16>(_selected / page_size + 1);
    page += "/";
    page += bn::to_string<16>((music_count + page_size - 1) / page_size);
    canvas.text(186, 6, page.c_str(), text_canvas::muted);
    canvas.rectangle(12, 23, 216, 1, text_canvas::selection);
    int first = _selected / page_size * page_size;
    for (int row = 0; row < page_size && first + row < music_count; ++row)
    {
        int index = first + row;
        int y = 28 + row * 26;
        const auto& entry = music_catalog[index];
        if (index == _selected)
            canvas.rectangle(10, y - 1, 220, 25, text_canvas::selection);
        auto color = entry.module_id >= 0 ? text_canvas::white : text_canvas::muted;
        const char* rest = canvas.text(16, y, entry.label, color, 210, y + 12);
        if (*rest)
            canvas.text(210, y, "...", color);
        bn::string<80> status = entry.module_id < 0 ? "未制作" : index == _playing ? "播放中：" : "已制作：";
        if (entry.module_id >= 0)
            status += entry.description;
        canvas.text(24, y + 12, status.c_str(), entry.module_id >= 0 ? text_canvas::accent : text_canvas::muted, 228,
                    y + 24);
    }
    canvas.text(12, 144, "上下选择 左右翻页 A播放 Select停", text_canvas::muted);
    canvas.present();
}
} // namespace manosaba
