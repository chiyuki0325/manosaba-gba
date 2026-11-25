#include "video_upload.h"
#include "gallery.h"
#include "gallery_data.h"
#include "save_ui_data.h"
#include "gallery_runtime.h"
#include "graphics_data.h"
#include "graphics_decode.h"
#include "bn_regular_bg_map_ptr.h"
#include "save_system.h"
#include "text_canvas.h"
#include "sfx_ui.h"
#include "bn_core.h"
#include "bn_tile.h"
#include "bn_keypad.h"
#include "bn_bg_palette_item.h"
#include "bn_affine_bg_map_item.h"
#include "bn_affine_bg_map_ptr.h"
#include "bn_bg_palettes.h"
#include "bn_memory.h"
#include "bn_format.h"
extern "C"
{
    BN_DATA_EWRAM_BSS gallery_runtime_profile gallery_runtime = {};
}
namespace manosaba
{
void gallery::protect() const
{
    if (_bg)
        video_upload::protect(*_bg);
    if (_regular)
        video_upload::protect(*_regular);
}

namespace
{
// Warm charcoal, ivory type and a one-pixel gray shadow, based on the original gallery.
alignas(4) constexpr uint16_t paper[256] = {0x1084, 0x5f1a, 0x4632, 0x677d, 0x2109, 0x4e73, 0x1ce8, 0x14a6, 0x77bd, 0};
constexpr int thumb_width = 72, thumb_height = 36;
} // namespace
void gallery::open(bool demo)
{
    suspend();
    _demo = demo;
    _done = false;
    _mode = 0;
    _paint = 0;
}
bool gallery::seen(int image, const save_system& progress) const
{
    return _demo || progress.gallery_unlocked(image);
}
void gallery::suspend()
{
    _bg.reset();
    _tiles.reset();
    _regular.reset();
    _regular_tiles.reset();
    _palette.reset();
    _paint = 0;
    _window = -1024;
    _map_pending = false;
    if (gallery_runtime.active)
        bn::bg_palettes::set_transparent_color(bn::color(0, 0, 0));
    gallery_runtime.active = 0;
}
int gallery::unlocked(int group, const save_system& progress) const
{
    const auto& g = gallery_groups[group];
    for (int i = g.first; i < g.first + g.count; ++i)
        if (seen(i, progress))
            return i;
    return -1;
}
void gallery::position()
{
    const auto& asset = graphic_assets[gallery_images[_image].asset];
    if (_regular)
    {
        const auto& image = *regular_graphic(gallery_images[_image].asset);
        int left = (asset.width - 240) / 2 + image.origin + _pan;
        if (left / 8 != _window)
        {
            _window = left / 8;
            regular_window(image, _window, 0, reinterpret_cast<uint16_t*>(graphic_map_cells));
            _map_pending = true;
        }
        _regular->set_position(8 - left % 8, 48);
        return;
    }
    _bg->set_scale(_native ? bn::fixed(1) : bn::fixed(240) / asset.width);
    _bg->set_position(-_pan, 0);
}
bool gallery::update(bool ready, const save_system& progress)
{
    gallery_runtime.ready = unsigned(
        ready && _paint >= 3 && (_mode ? (_bg && _bg->visible()) || (_regular && _regular->visible()) : _cell == 6));
    gallery_runtime.pan = unsigned(_pan);
    gallery_runtime.native = _native;
    gallery_runtime.phase = unsigned(_paint);
    gallery_runtime.cell = unsigned(_cell);
    gallery_runtime.row = unsigned(_row);
    bool redraw = _paint < 3 || (_mode == 0 && _cell < 6);
    if (progress.gallery_revision() != _revision)
    {
        _revision = progress.gallery_revision();
        if (!_mode)
            _paint = 0;
        redraw = true;
    }
    if (!ready || _paint < 3 || (!_mode && _cell < 6) || (_mode && !_bg && !_regular))
        return redraw;
    if (bn::keypad::b_pressed())
    {
        play_ui_sound(ui_sound::cancel);
        if (_mode)
        {
            suspend();
            _mode = 0;
            _paint = 0;
        }
        else
            _done = true;
        return true;
    }
    if (_mode)
    {
        int asset = gallery_images[_image].asset;
        if (_upload < (_regular ? regular_graphic(asset)->count : graphic_assets[asset].variants[0].count) * 64)
            return false;
        if (bn::keypad::l_pressed() || bn::keypad::r_pressed())
        {
            const auto& g = gallery_groups[_selected];
            int next = _image;
            for (int i = 0; i < g.count; ++i)
            {
                next = g.first + (next - g.first + (bn::keypad::r_pressed() ? 1 : g.count - 1)) % g.count;
                if (seen(next, progress))
                    break;
            }
            if (next != _image)
            {
                _image = next;
                suspend();
                _paint = 0;
                play_ui_sound(ui_sound::tab);
                return true;
            }
        }
        if (bn::keypad::a_pressed())
        {
            _native = !_native;
            _pan = 0;
            suspend();
            play_ui_sound(ui_sound::confirm);
            return true;
        }
        if (_native)
        {
            int pan = _pan, limit = (graphic_assets[gallery_images[_image].asset].width - 240) / 2;
            if (bn::keypad::left_held())
                pan -= 2;
            if (bn::keypad::right_held())
                pan += 2;
            pan = bn::clamp(pan, -limit, limit);
            if (pan != _pan)
            {
                _pan = pan;
                position();
            }
        }
        return false;
    }
    int next = _selected, pages = (gallery_group_count + 5) / 6, local = _selected % 6, row = local / 3,
        col = local % 3;
    auto adjacent = [&](int delta, int cell)
    { return bn::min(((page() + pages + delta) % pages) * 6 + cell, gallery_group_count - 1); };
    if (bn::keypad::left_pressed())
        next = col ? next - 1 : adjacent(-1, row * 3 + 2);
    if (bn::keypad::right_pressed())
        next = col < 2 && next + 1 < gallery_group_count ? next + 1 : adjacent(1, row * 3);
    if (bn::keypad::up_pressed())
        next = row ? next - 3 : adjacent(-1, 3 + col);
    if (bn::keypad::down_pressed())
        next = !row && next + 3 < gallery_group_count ? next + 3 : adjacent(1, col);
    if (bn::keypad::l_pressed())
        next = adjacent(-1, local);
    if (bn::keypad::r_pressed())
        next = adjacent(1, local);
    if (next != _selected)
    {
        int old_page = page();
        _previous = _selected % 6;
        _selected = next;
        if (page() != old_page)
            _paint = 0;
        play_ui_sound(ui_sound::move);
        return true;
    }
    if (bn::keypad::a_pressed())
    {
        int image = unlocked(_selected, progress);
        if (image >= 0)
        {
            _image = image;
            _mode = 1;
            _native = false;
            _pan = 0;
            _paint = 0;
            play_ui_sound(ui_sound::confirm);
            return true;
        }
    }
    return redraw;
}
void gallery::frame(text_canvas& canvas, int cell, const save_system& progress)
{
    int group = page() * 6 + cell;
    if (group >= gallery_group_count)
        return;
    int x = 8 + cell % 3 * 76, y = 32 + cell / 3 * 56;
    auto ink = cell == _selected % 6 ? text_canvas::accent : text_canvas::selection;
    canvas.rectangle(x - 2, y - 2, 76, 1, ink);
    canvas.rectangle(x - 2, y + 36, 76, 1, ink);
    canvas.rectangle(x - 2, y - 1, 1, 37, ink);
    canvas.rectangle(x + 73, y - 1, 1, 37, ink);
    canvas.rectangle(x - 2, y + 39, 76, 13, text_canvas::background);
    if (unlocked(group, progress) < 0)
        canvas.text(x, y + 39, "未解锁", text_canvas::muted, x + 72, 160, true);
    else
    {
        const auto& g = gallery_groups[group];
        int seen = 0;
        for (int i = g.first; i < g.first + g.count; ++i)
            seen += this->seen(i, progress);
        auto label = bn::format<12>("{}/{}", seen, g.count);
        canvas.text(x, y + 39, label.c_str(), text_canvas::white, x + 72, 160, true);
    }
}
void gallery::controls(text_canvas& canvas, bool grid)
{
    auto key = [&](int x, int index)
    {
        const auto& icon = save_ui_keys[index];
        uint8_t pixels[18 * 14];
        for (int i = 0; i < icon.width * icon.height; ++i)
        {
            int value = icon.pixels[i];
            pixels[i] = uint8_t(!grid ? value : !value ? 0 : value == 1 ? 8 : value == 2 ? 2 : value == 6 ? 9 : 4);
        }
        canvas.stamp(x, 144, icon.width, icon.height, pixels);
    };
    key(8, 2);
    key(28, 3);
    canvas.text(48, 145, grid ? "翻页" : "差分", text_canvas::muted, 238, 160, true);
    key(86, 0);
    canvas.text(102, 145, grid ? "查看" : "原尺寸", text_canvas::muted, 238, 160, true);
    key(166, 1);
    canvas.text(182, 145, "返回", text_canvas::muted, 238, 160, true);
}
void gallery::draw(text_canvas& canvas, const save_system& progress)
{
    gallery_runtime.phase = unsigned(_paint);
    gallery_runtime.cell = unsigned(_cell);
    gallery_runtime.row = unsigned(_row);
    gallery_runtime.active = 1;
    gallery_runtime.mode = unsigned(_mode);
    gallery_runtime.selected = unsigned(_selected);
    gallery_runtime.page = unsigned(page());
    gallery_runtime.image = unsigned(_image);
    if (!_paint)
    {
        _previous = -1;
        if (!_mode)
        {
            canvas.flat_screen(paper);
            bn::bg_palettes::set_transparent_color(bn::color(paper[0]));
            _cell = 0;
            _row = 0;
            _revision = progress.gallery_revision();
        }
        else
        {
            canvas.clear();
            canvas.palette_color(6, 0x1ce8);
            bn::bg_palettes::set_transparent_color(bn::color(0, 0, 0));
            video_upload::reclaim();
            const auto& asset = graphic_assets[gallery_images[_image].asset];
            _palette = bn::bg_palette_ptr::create(
                bn::bg_palette_item(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(asset.palette), 112),
                                    bn::bpp_mode::BPP_8));
            if (_native && regular_graphic(gallery_images[_image].asset))
            {
                _regular_tiles = bn::regular_bg_tiles_ptr::allocate(
                    regular_graphic(gallery_images[_image].asset)->count * 2, bn::bpp_mode::BPP_8, false);
                _regular = bn::regular_bg_ptr::create(
                    8, 48, bn::regular_bg_map_ptr::allocate(bn::size(32, 32), *_regular_tiles, *_palette));
                _regular->set_priority(3);
                _regular->set_visible(false);
            }
            else
            {
                _tiles = bn::affine_bg_tiles_ptr::allocate(512, false);
                expand_affine_map(asset, asset.variants[0], graphic_map_cells);
                auto map = video_upload::map(bn::affine_bg_map_item(graphic_map_cells[0], bn::size(64, 64)), *_tiles,
                                             *_palette);
                _bg = bn::affine_bg_ptr::create(0, 0, map);
                _bg->set_priority(3);
                _bg->set_visible(false);
            }
            _upload = 0;
            position();
        }
        _paint = 1;
        return;
    }
    if (_paint == 1)
    {
        if (!_mode)
        {
            canvas.text(8, 7, _demo ? "画廊 demo" : "画廊", text_canvas::accent, 230, 160, true);
            int seen = 0;
            for (int i = 0; i < gallery_image_count; ++i)
                seen += this->seen(i, progress);
            auto number =
                bn::format<24>("{}/{}  {}/{}", seen, gallery_image_count, page() + 1, (gallery_group_count + 5) / 6);
            canvas.text(114, 8, number.c_str(), text_canvas::muted, 238, 160, true);
            canvas.rectangle(8, 24, 224, 1, text_canvas::selection);
        }
        _paint = 2;
        return;
    }
    if (_paint == 2)
    {
        if (!_mode || !_native)
            controls(canvas, !_mode);
        _paint = 3;
        return;
    }
    if (!_mode)
    {
        if (_previous >= 0)
        {
            frame(canvas, _previous, progress);
            frame(canvas, _selected % 6, progress);
            _previous = -1;
        }
        if (_cell < 6)
        {
            int group = page() * 6 + _cell;
            if (group >= gallery_group_count)
            {
                _cell = 6;
            }
            else
            {
                int index = unlocked(group, progress);
                int x = 8 + _cell % 3 * 76, y = 32 + _cell / 3 * 56;
                if (!_row)
                {
                    frame(canvas, _cell, progress);
                    if (index < 0)
                    {
                        canvas.rectangle(x, y, 72, 36, text_canvas::selection);
                        ++_cell;
                    }
                    else
                        for (int i = 0; i < 32; ++i)
                            canvas.palette_color(16 + _cell * 32 + i, gallery_images[index].palette[i]);
                }
                if (index >= 0)
                {
                    const auto& image = gallery_images[index];
                    const auto& asset = graphic_assets[image.asset];
                    const auto& variant = asset.variants[0];
                    int height = asset.height * thumb_width / asset.width, top = (thumb_height - height) / 2;
                    uint8_t line[thumb_width];
                    const auto* columns = gallery_columns[image.columns];
                    for (int n = 0; n < 12 && _row < thumb_height; ++n, ++_row)
                    {
                        if (!video_upload::finishing && unsigned(bn::core::current_cpu_ticks()) >= 3200)
                            break;
                        if (_row >= top && _row < top + height)
                        {
                            int sy = (512 - asset.height) / 2 + (_row - top) * asset.height / height;
                            int left = variant.scale ? 0 : (512 - asset.width) / 16;
                            int stride = variant.scale ? 64 : (asset.width + ((512 - asset.width) / 2) % 8 + 7) / 8;
                            int top = variant.scale ? 0 : (512 - asset.height) / 16;
                            const auto* cells = variant.map + (sy / 8 - top) * stride;
                            int y_offset = sy % 8 * 8, bank = 16 + _cell * 32;
                            for (int col = 0; col < thumb_width; ++col)
                            {
                                unsigned packed_x = columns[col], tile = cells[int(packed_x & 63) - left];
                                unsigned offset =
                                    (variant.tiles[tile] & 0x7fffffffu) + unsigned(y_offset) + (packed_x >> 6);
                                line[col] = uint8_t(bank + image.remap[graphics_bytes[offset]]);
                            }
                        }
                        else
                            for (auto& pixel : line)
                                pixel = text_canvas::selection;
                        canvas.stamp(x, y + _row, thumb_width, 1, line);
                    }
                    if (_row == thumb_height)
                    {
                        ++_cell;
                        _row = 0;
                    }
                }
            }
        }
    }
    else
    {
        // In native-size view the image owns all 160 lines.
        canvas.rectangle(0, 144, 240, 16, text_canvas::background);
        if (!_native)
            controls(canvas, false);
    }
    canvas.present();
    int seen = 0;
    for (int i = 0; i < gallery_image_count; ++i)
        seen += this->seen(i, progress);
    gallery_runtime.unlocked = unsigned(seen);
    gallery_runtime.upload = unsigned(_upload);
}
int gallery::commit(int bytes)
{
    if (!_bg && !_regular)
        return bytes;
    int asset = gallery_images[_image].asset;
    const auto& variant = graphic_assets[asset].variants[0];
    int total = (_regular ? regular_graphic(asset)->count : variant.count) * 64;
    if (_map_pending && bytes >= 2048)
    {
        auto owner = _regular->map();
        auto map = owner.vram().value();
        bn::memory::copy(*reinterpret_cast<const uint16_t*>(graphic_map_cells), 1024, map[0]);
        bytes -= 2048;
        _map_pending = false;
    }
    auto vram = _regular ? _regular_tiles->vram().value() : _tiles->vram().value();
    while (bytes >= 64 && _upload < total)
    {
        unsigned offset = variant.tiles[_upload / 64] & 0x7fffffffu;
        bn::memory::copy(*reinterpret_cast<const uint32_t*>(graphics_bytes + offset), 16, vram[_upload / 32].data[0]);
        _upload += 64;
        bytes -= 64;
    }
    if (_upload == total && !_map_pending)
    {
        if (_bg)
            _bg->set_visible(true);
        else
            _regular->set_visible(true);
    }
    gallery_runtime.upload = unsigned(_upload);
    return bytes;
}
} // namespace manosaba
