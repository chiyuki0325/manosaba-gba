#include "witch_book_map.h"
#include "save_archive.h"
#include "witch_book_art.h"
#include "book_theme.h"
#include "lzss.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include <cstring>
#include <cstdlib>
namespace manosaba
{
namespace
{
int overlap(int x, int y, int w, int h, int left, int top, int right, int bottom)
{
    return bn::max(0, bn::min(x + w, right) - bn::max(x, left)) * bn::max(0, bn::min(y + h, bottom) - bn::max(y, top));
}
int letters(const char* value)
{
    int count = 0;
    for (; *value; ++value)
        count += (uint8_t(*value) & 192) != 128;
    return count;
}

} // namespace
void witch_book_map::open(bool forced, bool normal)
{
    int previous = image();
    _full = true;
    _loaded_image = -1;
    _forced = forced;
    _normal = normal;
    _mode = 0;
    _floor_count = 0;
    std::memcpy(_palette, book_map_palette, sizeof(_palette));
    book_theme(_palette, hiro_book_theme(_vm), true);
    std::memset(_sources, -1, sizeof(_sources));
    for (int a = 0; a < 33; ++a)
    {
        if (!forced)
        {
            _sources[a] = 0;
            continue;
        }
        int count = normal ? _vm.option_count() : _vm.inventory_count();
        for (int i = 0; i < count; ++i)
            if (normal ? map_areas[a].normal_id == _vm.option(i).value
                       : bn::string_view(map_areas[a].id) == story_items[_vm.item_id(i)].id)
            {
                _sources[a] = int8_t(i);
                break;
            }
    }
    for (int f = 0; f < 5; ++f)
    {
        const auto& floor = map_floors[f];
        if (_vm.book_version(floor.entry) != floor.version)
            continue;
        int count = 0;
        for (int a = floor.first; a < floor.first + floor.count; ++a)
            count += _sources[a] >= 0;
        if (!forced || count)
            _floors[_floor_count++] = uint8_t(f);
    }
    _floor = 0;
    for (int i = 0; i < _floor_count; ++i)
        if (_restore_floor >= 0 ? _floors[i] == _restore_floor : map_floors[_floors[i]].image == previous)
            _floor = i;
    _restore_floor = -1;
    places();
}
void witch_book_map::archive(save_archive& a, bool active)
{
    int saved = floor(), left = _view_left, top = _view_top, mode = _mode;
    a.field(saved);
    a.field(left);
    a.field(top);
    a.field(mode);
    a.bytes(_positions, sizeof(_positions));
    if (a.reading())
    {
        _restore_floor = saved;
        if (active)
        {
            open(_forced, _normal);
            _mode = mode;
            _left = left;
            _top = top;
            layout(false);
            repaint();
        }
    }
}
void witch_book_map::places()
{
    _count = 0;
    if (_floor_count)
    {
        const auto& floor = map_floors[_floors[_floor]];
        for (int a = floor.first; a < floor.first + floor.count; ++a)
            if (_sources[a] >= 0)
                _areas[_count++] = uint8_t(a);
    }
    _positions[image()] = uint8_t(_count ? bn::min(int(_positions[image()]), _count - 1) : 0);
    layout();
    refresh();
}
void witch_book_map::layout(bool center)
{
    const auto* area = point();
    int px = area ? area->x : 192, py = area ? area->y : 112;
    if (center)
    {
        _left = bn::clamp(px - 116, 0, 152);
        _top = bn::clamp(py - 59, 0, 106);
    }
    px += 4 - _left;
    py += 25 - _top;
    _label_width = letters(name()) * 12 + 8 + (_mode == 0 ? 16 : 0);
    _label_x = area ? bn::clamp(px - _label_width / 2, 7, 233 - _label_width) : 76;
    _label_y = area ? py + 9 : 28;
    if (_label_y + 17 > 140)
        _label_y = py - 26;
    _label_y = bn::clamp(_label_y, 28, 123);
    constexpr int corners[4][2] = {{182, 27}, {7, 46}, {182, 109}, {7, 109}};
    int best = 0x7fffffff;
    for (const auto& corner : corners)
    {
        int x = corner[0], y = corner[1];
        int score = overlap(x, y, 52, 32, _label_x, _label_y, _label_x + _label_width, _label_y + 17) * 1000;
        if (area)
            score += overlap(x, y, 52, 32, area->left + 4 - _left, area->top + 25 - _top, area->right + 4 - _left,
                             area->bottom + 25 - _top) *
                     100;
        score -= (x + 26 - px) * (x + 26 - px) + (y + 16 - py) * (y + 16 - py);
        if (score < best)
        {
            best = score;
            _mini_x = x;
            _mini_y = y;
        }
    }
}
int witch_book_map::update(bool action, bool ready)
{
    if (bn::keypad::b_pressed())
    {
        if (_mode)
        {
            _mode = 0;
            layout();
            refresh();
            return 0;
        }
        if (!_forced || (!_normal && _vm.inventory_back()))
            return -1;
    }
    if (!ready || dirty())
        return 0;
    if (action && _floor_count)
    {
        if (_mode == 1 && _forced)
            return 1;
        _mode = _mode == 0 && _count ? 1 : 2;
        layout(false);
        refresh();
        return 0;
    }
    if (_mode == 1)
        return 0;
    if (_mode == 2)
    {
        int left = _left, top = _top;
        if (bn::keypad::left_pressed())
            _left = bn::max(0, _left - 16);
        if (bn::keypad::right_pressed())
            _left = bn::min(152, _left + 16);
        if (bn::keypad::up_pressed())
            _top = bn::max(0, _top - 16);
        if (bn::keypad::down_pressed())
            _top = bn::min(106, _top + 16);
        if (left != _left || top != _top)
        {
            layout(false);
            refresh();
        }
        return 0;
    }
    int floor_step = bn::keypad::down_pressed() ? 1 : bn::keypad::up_pressed() ? -1 : 0;
    if (floor_step && _floor_count > 1)
    {
        _floor = (_floor + floor_step + _floor_count) % _floor_count;
        places();
        return 0;
    }
    int step = bn::keypad::right_pressed() ? 1 : bn::keypad::left_pressed() ? -1 : 0;
    if (step && _count > 1)
    {
        _positions[image()] = uint8_t((selected() + step + _count) % _count);
        layout();
        refresh();
    }
    return 0;
}
void witch_book_map::key(text_canvas& canvas, const uint8_t* shared, int x, int y, int index, const char* label)
{
    const auto& art = book_stamps[index + 3];
    canvas.stamp(x, y, art.width, art.height, shared + art.offset);
    if (label)
        canvas.text(x + art.width + 1, y, label, text_canvas::white);
}
void witch_book_map::decorate(text_canvas& canvas, const uint8_t* shared, int part)
{
    const auto* area = point();
    int px = area ? area->x + 4 - _view_left : 120, py = area ? area->y + 25 - _view_top : 84;
    if (!part)
    {
        if (_full)
        {
            canvas.rectangle(3, 24, 234, 1, text_canvas::warning);
            canvas.rectangle(3, 143, 234, 1, text_canvas::warning);
            canvas.rectangle(3, 25, 1, 118, text_canvas::warning);
            canvas.rectangle(236, 25, 1, 118, text_canvas::warning);
        }
        if (area && visible_tag())
        {
            for (int y = -4; y <= 4; ++y)
                canvas.rectangle(px - (4 - std::abs(y)), py + y, 9 - 2 * std::abs(y), 1, text_canvas::accent);
            int edge = _label_y > py ? py + 5 : _label_y + 17;
            canvas.rectangle(px, edge, 1, std::abs(_label_y > py ? _label_y - edge : py - 4 - edge) + 1,
                             text_canvas::selection);
        }
        if (visible_tag())
        {
            canvas.rectangle(_label_x, _label_y, _label_width, 17, text_canvas::warning);
            canvas.rectangle(_label_x + 1, _label_y + 1, _label_width - 2, 15, text_canvas::dark);
            canvas.text(_label_x + 4, _label_y + 2, name(), text_canvas::white, _label_x + _label_width - 2,
                        _label_y + 16);
            if (!_mode)
                key(canvas, shared, _label_x + _label_width - 16, _label_y + 1, 0);
        }
    }
    else if (part == 1)
    {
        if (_full || _drawn_floor != floor())
        {
            canvas.rectangle(7, 28, 56, 16, text_canvas::dark);
            static constexpr const char* labels[] = {"1F", "2F", "B1", "1F"};
            canvas.text(11, 29, labels[image()]);
            auto number = bn::to_string<8>(_floor + 1);
            number += "/";
            number += bn::to_string<8>(_floor_count);
            canvas.text(32, 29, number.c_str(), text_canvas::muted);
        }
        canvas.rectangle(_mini_x, _mini_y, 52, 32, text_canvas::warning);
        canvas.rectangle(_mini_x + 1, _mini_y + 1, 50, 30, text_canvas::dark);
    }
    else if (part == 2)
        mini_cursor(canvas, false);
    else if (part == 3)
    {
        canvas.rectangle(4, 144, 232, 16, text_canvas::dark);
        if (_mode == 1)
        {
            key(canvas, shared, 10, 145, 0, _forced ? "选定" : "看图");
        }
        else if (_mode == 2)
            canvas.text(8, 146, "方向键移图", text_canvas::muted);
        else
        {
            if (_floor_count > 1)
                canvas.text(8, 146, "\ue003楼层", text_canvas::muted);
            if (_count > 1)
                canvas.text(76, 146, "\ue004地点", text_canvas::muted);
        }
        if (_mode || !_forced || (!_normal && _vm.inventory_back()))
            key(canvas, shared, 184, 145, 1, "返回");
    }
    else if (part == 4)
    {
        canvas.rectangle(27, 58, 186, 1, text_canvas::warning);
        canvas.rectangle(27, 110, 186, 1, text_canvas::warning);
        canvas.rectangle(27, 59, 1, 51, text_canvas::warning);
        canvas.rectangle(212, 59, 1, 51, text_canvas::warning);
        canvas.rectangle(28, 59, 184, 51, text_canvas::ink(9));
    }
    else if (part == 5)
    {
        canvas.text(78, 64, _forced ? "选定这个地点？" : "查看这个地点？", text_canvas::muted);
        canvas.text(120 - letters(name()) * 6, 87, name());
    }
}
void witch_book_map::close()
{
    if (_canvas)
        _canvas->map_close();
    _loaded_image = -1;
}
void witch_book_map::refresh()
{
    if (_full)
    {
        repaint();
        return;
    }
    _floor_change = _loaded_image != image();
    _paint = _floor_change ? 630 : 618;
    _clear_row = _mini_row = 0;
}
void witch_book_map::remember()
{
    _drawn_mode = _mode;
    _drawn_floor = floor();
    _drawn_count = _count;
    _drawn_mini_x = _mini_x;
    _drawn_mini_y = _mini_y;
    _full = _floor_change = false;
}
void witch_book_map::clear_rect(text_canvas& canvas, int x, int y, int width, int height)
{
    int left = bn::max(4, x), right = bn::min(236, x + width), top = bn::max(25, y), bottom = bn::min(143, y + height);
    if (right <= left || bottom <= top)
        return;
    // The floor badge is an independent fixed overlay.
    for (int row = top; row < bottom; ++row)
    {
        if (row >= 28 && row < 44 && left < 63)
        {
            if (left < 7)
                canvas.rectangle(left, row, bn::min(right, 7) - left, 1, text_canvas::background);
            if (right > 63)
                canvas.rectangle(bn::max(left, 63), row, right - bn::max(left, 63), 1, text_canvas::background);
        }
        else
            canvas.rectangle(left, row, right - left, 1, text_canvas::background);
    }
}
void witch_book_map::clear_tag(text_canvas& canvas)
{
    if (!_tag_visible)
        return;
    clear_rect(canvas, _drawn_label_x, _drawn_label_y, _drawn_label_width, 17);
    clear_rect(canvas, _drawn_px - 4, _drawn_py - 4, 9, 9);
    int top = bn::min(_drawn_py, _drawn_label_y), bottom = bn::max(_drawn_py, _drawn_label_y + 17);
    clear_rect(canvas, _drawn_px, top, 1, bottom - top + 1);
    _tag_visible = false;
}
bool witch_book_map::visible_tag() const
{
    if (_mode == 2)
        return false;
    const auto* area = point();
    if (!area)
        return true;
    int x = area->x + 4 - _view_left, y = area->y + 25 - _view_top;
    return x >= 8 && x < 232 && y >= 29 && y < 139 &&
           !overlap(x - 4, y - 4, 9, 9, _mini_x, _mini_y, _mini_x + 52, _mini_y + 32) &&
           !overlap(x - 4, y - 4, 9, 9, 7, 28, 63, 44);
}
void witch_book_map::tag_position()
{
    const auto* area = point();
    int px = area ? area->x + 4 - _view_left : 120, py = area ? area->y + 25 - _view_top : 84;
    _label_width = letters(name()) * 12 + 8 + (!_mode ? 16 : 0);
    _label_x = area ? bn::clamp(px - _label_width / 2, 7, 233 - _label_width) : 76;
    _label_y = area ? bn::clamp(py + 9 + 17 > 140 ? py - 26 : py + 9, 28, 123) : 28;
    if (_label_x < 63 && _label_y < 44)
        _label_y = 45;
    if (overlap(_label_x, _label_y, _label_width, 17, _mini_x, _mini_y, _mini_x + 52, _mini_y + 32))
        _label_y = _mini_y >= 70 ? bn::max(28, _mini_y - 18) : _mini_y + 33;
}
void witch_book_map::mini_cursor(text_canvas& canvas, bool restore)
{
    int dx = _view_left / 8, dy = _view_top / 8;
    int x = _mini_x + 2 + dx, y = _mini_y + 2 + dy, w = 29, h = 15;
    if (restore)
    {
        const auto* mini = map_pictures[image()].mini;
        canvas.stamp(x, y, w, 1, mini + dy * 48 + dx);
        canvas.stamp(x, y + h - 1, w, 1, mini + (dy + h - 1) * 48 + dx);
        canvas.stamp(x, y, 1, h, mini + dy * 48 + dx, 48);
        canvas.stamp(x + w - 1, y, 1, h, mini + dy * 48 + dx + w - 1, 48);
    }
    else
    {
        canvas.rectangle(x, y, w, 1, text_canvas::white);
        canvas.rectangle(x, y + h - 1, w, 1, text_canvas::white);
        canvas.rectangle(x, y, 1, h, text_canvas::white);
        canvas.rectangle(x + w - 1, y, 1, h, text_canvas::white);
        if (const auto* area = point())
            canvas.rectangle(_mini_x + 2 + area->x / 8 - 1, _mini_y + 2 + area->y / 8 - 1, 3, 3, text_canvas::accent);
    }
}
void witch_book_map::draw(text_canvas& canvas, uint8_t* block, const uint8_t* shared, uint8_t* cache)
{
    _canvas = &canvas;
    _yield = false;
    auto* cells = reinterpret_cast<uint16_t*>(cache);
    auto* saved = cells + 2048;
    if (_paint < 600)
    {
        int count = bn::min(32, 600 - _paint);
        canvas.map_page(_palette, _paint, count);
        _paint += count;
        return;
    }
    if (_paint < 602)
        return;
    if (!_floor_count)
    {
        canvas.text(84, 76, "暂无地图", text_canvas::muted);
        decorate(canvas, shared, 3);
        _paint = 612;
        remember();
        return;
    }
    if (_paint == 630)
    {
        if (_clear_row < 118)
        {
            int count = bn::min(8, 118 - _clear_row);
            canvas.map_capture(_clear_row, count);
            _clear_row += count;
            return;
        }
        if (!canvas.ready())
        {
            _yield = true;
            return;
        }
        canvas.map_hold_ui(true);
        _clear_row = 0;
        _paint = 602;
        return;
    }
    if (_paint == 602)
    {
        int count = bn::min(8, 118 - _clear_row);
        canvas.rectangle(4, 25 + _clear_row, 232, count, text_canvas::background);
        _clear_row += count;
        if (_clear_row == 118)
        {
            _paint = 615;
            _block = 0;
            _tag_visible = false;
        }
        return;
    }
    if (_paint == 615)
    {
        const auto& data = map_pictures[image()].cells;
        if (data.codec)
            lzss_decode(data.data, cache, 4096);
        else
            std::memcpy(cache, data.data, 4096);
        unsigned offset = unsigned(canvas.map_cell_offset());
        for (int i = 0; i < 2048; ++i)
            cells[i] = uint16_t(cells[i] | offset);
        _highlight = -1;
        _highlight_bank = 0;
        _paint = 616;
        return;
    }
    if (_paint == 616)
    {
        if (canvas.map_pending())
        {
            _yield = true;
            return;
        }
        const auto& picture = map_pictures[image()];
        int first = _block * 128;
        if (first < picture.tile_count)
        {
            int count = bn::min(128, int(picture.tile_count) - first);
            const auto& part = picture.blocks[_block];
            if (part.codec)
                lzss_decode(part.data, block, count * 32);
            else
                std::memcpy(block, part.data, count * 32);
            canvas.map_upload(block, first, count);
            ++_block;
            _yield = true;
            return;
        }
        canvas.map_cells(cells);
        _paint = 617;
        _yield = true;
        return;
    }
    if (_paint == 617)
    {
        if (canvas.map_pending())
        {
            _yield = true;
            return;
        }
        _loaded_image = image();
        _paint = 618;
    }
    if (_paint == 618)
    {
        if (canvas.map_pending())
        {
            _yield = true;
            return;
        }
        int next = _mode == 2 ? -1 : area();
        if (next != _highlight)
        {
            if (_highlight >= 0)
            {
                const auto& high = *map_areas[_highlight].highlight;
                for (int i = 0; i < high.count; ++i)
                    cells[high.cells[i]] = saved[i];
            }
            if (next >= 0)
            {
                const auto& high = *map_areas[next].highlight;
                if (high.count)
                {
                    _highlight_bank ^= 1;
                    int first = map_pictures[image()].tile_count + _highlight_bank * 128;
                    if (high.tiles.codec)
                        lzss_decode(high.tiles.data, block, high.count * 32);
                    else
                        std::memcpy(block, high.tiles.data, high.count * 32);
                    for (int i = 0; i < high.count; ++i)
                    {
                        int at = high.cells[i];
                        saved[i] = cells[at];
                        cells[at] = uint16_t((cells[at] & 0xfc00) | (first + i));
                    }
                    canvas.map_upload(block, first, high.count);
                }
            }
            _highlight = next;
            canvas.map_cells(cells);
            _paint = 619;
            _yield = true;
            return;
        }
        _paint = 619;
    }
    if (_paint == 619)
    {
        if (canvas.map_pending())
        {
            _yield = true;
            return;
        }
        if (!_full && !_floor_change)
        {
            clear_tag(canvas);
            _clear_row = 0;
            _paint = 631;
            return;
        }
        _paint = 633;
    }
    if (_paint == 631 || _paint == 632)
    {
        int height = _paint == 631 ? 32 : _drawn_mode == 1 ? 53 : 0;
        int count = bn::min(8, height - _clear_row);
        if (count)
            clear_rect(canvas, _paint == 631 ? _drawn_mini_x : 27, (_paint == 631 ? _drawn_mini_y : 58) + _clear_row,
                       _paint == 631 ? 52 : 186, count);
        _clear_row += count;
        if (_clear_row == height)
        {
            _clear_row = 0;
            ++_paint;
        }
        return;
    }
    if (_paint == 633)
    {
        if (_full || _floor_change)
        {
            _view_left = _left;
            _view_top = _top;
        }
        _from_left = _view_left;
        _from_top = _view_top;
        _scroll_frame = 0;
        int distance = bn::max(std::abs(_left - _view_left), std::abs(_top - _view_top));
        _scroll_duration = distance ? bn::clamp(distance / 6, 4, 12) : 0;
        canvas.map_position(_view_left, _view_top);
        tag_position();
        _paint = 604;
        return;
    }
    if (_paint == 604)
    {
        decorate(canvas, shared, 1);
        _paint = 605;
        return;
    }
    if (_paint == 605)
    {
        int count = bn::min(8, 28 - _mini_row);
        canvas.stamp(_mini_x + 2, _mini_y + 2 + _mini_row, 48, count, map_pictures[image()].mini + _mini_row * 48);
        _mini_row += count;
        if (_mini_row == 28)
            _paint = 606;
        return;
    }
    if (_paint == 606)
    {
        decorate(canvas, shared, 2);
        _paint = 603;
        return;
    }
    if (_paint == 603)
    {
        decorate(canvas, shared, 0);
        const auto* area = point();
        int px = area ? area->x + 4 - _view_left : 120, py = area ? area->y + 25 - _view_top : 84;
        _tag_visible = visible_tag();
        _drawn_label_x = _label_x;
        _drawn_label_y = _label_y;
        _drawn_label_width = _label_width;
        _drawn_px = px;
        _drawn_py = py;
        _paint = 607;
        return;
    }
    if (_paint == 607)
    {
        if (_full || _drawn_mode != _mode || (_drawn_count > 1) != (_count > 1))
            decorate(canvas, shared, 3);
        if (_drawn_mode != _mode && !_full)
        {
            canvas.rectangle(1, 8, 18, 14, text_canvas::ink(8));
            canvas.rectangle(221, 8, 18, 14, text_canvas::ink(8));
            if (!_mode && !_forced)
            {
                key(canvas, shared, 1, 8, 2);
                key(canvas, shared, 221, 8, 3);
            }
        }
        _paint = _mode == 1 ? 608 : 610;
        return;
    }
    if (_paint == 608)
    {
        decorate(canvas, shared, 4);
        _paint = 609;
        return;
    }
    if (_paint == 609)
    {
        decorate(canvas, shared, 5);
        _paint = 610;
        return;
    }
    if (_paint == 610)
    {
        if (_full || _floor_change)
            canvas.map_reveal();
        _paint = _scroll_duration ? 620 : 612;
        if (_paint == 612)
            remember();
        _yield = true;
        return;
    }
    if (_paint == 620)
    {
        clear_tag(canvas);
        mini_cursor(canvas, true);
        ++_scroll_frame;
        int t = _scroll_frame * 256 / _scroll_duration, ease = t * t * (768 - 2 * t) / 65536;
        _view_left = _from_left + (_left - _from_left) * ease / 256;
        _view_top = _from_top + (_top - _from_top) * ease / 256;
        canvas.map_position(_view_left, _view_top);
        tag_position();
        mini_cursor(canvas, false);
        decorate(canvas, shared, 0);
        const auto* area = point();
        int px = area ? area->x + 4 - _view_left : 120, py = area ? area->y + 25 - _view_top : 84;
        _tag_visible = visible_tag();
        _drawn_label_x = _label_x;
        _drawn_label_y = _label_y;
        _drawn_label_width = _label_width;
        _drawn_px = px;
        _drawn_py = py;
        if (_scroll_frame == _scroll_duration)
        {
            _paint = 612;
            remember();
        }
        _yield = true;
    }
}
} // namespace manosaba
