#include "trial_map_scene.h"
#include "trial_special_runtime.h"
#include "bn_core.h"
#include "bn_sprite_tiles_ptr.h"
#include "book_map_data.h"
#include "story_ui_data.h"
#include "lzss.h"
#include "bn_sprite_tiles_item.h"
#include "bn_sprite_palette_item.h"
#include "bn_sprite_shape_size.h"
#include "bn_tile.h"
#include "bn_algorithm.h"
#include <cstring>
extern "C"
{
    BN_DATA_EWRAM_BSS trial_special_profile trial_special_runtime = {};
}
namespace manosaba
{
const trial_map_frame& trial_map_scene::frame() const
{
    const auto& s = trial_map_segments[_segment];
    return trial_map_frames[s.first + bn::min(_clock / 6, int(s.count) - 1)];
}
void trial_map_scene::suspend()
{
    trial_special_runtime.map_ready = 0;
    _sprites.clear();
    _palettes.clear();
    _opened = false;
    _canvas = nullptr;
    _loaded = _loading = -1;
    _block = 0;
    _cells = false;
}
void trial_map_scene::reset()
{
    trial_special_runtime.map = trial_special_runtime.map_ready = 0;
    suspend();
    _segment = -1;
    _clock = _close_left = 0;
    _closing = false;
}
void trial_map_scene::request(int segment, bool settled)
{
    if (segment == _segment)
        return;
    bool same = _segment >= 0 && trial_map_segments[_segment].pins == trial_map_segments[segment].pins &&
                trial_map_segments[_segment].icons[0] == trial_map_segments[segment].icons[0];
    if (!same)
    {
        _sprites.clear();
        _palettes.clear();
    }
    _segment = segment;
    _clock = settled ? trial_map_segments[segment].ticks : 0;
    _closing = false;
}
void trial_map_scene::close()
{
    if (!_closing)
    {
        _closing = true;
        _close_left = 30;
    }
}
void trial_map_scene::update(bool display_ready, int frames)
{
    if (!active() || !ready() || !display_ready)
        return;
    ++trial_special_runtime.frames;
    trial_special_runtime.missed += unsigned(bn::core::last_missed_frames());
    if (_closing)
        _close_left = bn::max(0, _close_left - frames);
    else
        _clock = bn::min(int(trial_map_segments[_segment].ticks), _clock + frames);
    position();
}
void trial_map_scene::position()
{
    const auto& s = trial_map_segments[_segment];
    const auto& a = frame();
    const auto& b = trial_map_frames[s.first + bn::min(_clock / 6 + 1, int(s.count) - 1)];
    int left = interpolate(a.left, b.left), top = interpolate(a.top, b.top), alpha = interpolate(a.alpha, b.alpha);
    if (_closing)
        alpha = alpha * _close_left / 30;
    auto& p = trial_special_runtime;
    p.map = unsigned(_segment + 1);
    p.map_clock = unsigned(_clock);
    p.map_duration = s.ticks;
    p.map_ready = unsigned(ready());
    p.map_floor = a.floor;
    p.map_left = unsigned(left);
    p.map_top = unsigned(top);
    p.map_pins = s.pins;
    p.map_alpha = unsigned(alpha);
    _canvas->map_position(left + 4, top + 25);
    _canvas->map_tint(0, 256 - alpha);
    for (int i = 0; i < _sprites.size(); ++i)
    {
        int x = interpolate(a.pins[i].x, b.pins[i].x) - left, y = interpolate(a.pins[i].y, b.pins[i].y) - top;
        if (!i)
        {
            p.pin_x = unsigned(x);
            p.pin_y = unsigned(y);
        }
        int opacity = interpolate(a.pins[i].alpha, b.pins[i].alpha) * alpha / 256;
        _sprites[i].set_position(x - 120, y - 80 - 8);
        _sprites[i].set_visible(_loaded == a.floor && opacity > 0 && x >= -8 && x < 248 && y >= 0 && y < 176);
        _palettes[i].set_fade(bn::color(0, 0, 0), bn::fixed::from_data((256 - opacity) * 16));
    }
}
void trial_map_scene::draw(text_canvas& canvas)
{
    _canvas = &canvas;
    if (!_opened)
    {
        canvas.map_page(book_map_palette, 0, 0, true);
        canvas.decoration(story_ui_tiles);
        _opened = true;
        return;
    }
    int target = frame().floor;
    if (target != _loaded)
    {
        if (_loading != target)
        {
            canvas.map_hide();
            _loading = target;
            _block = 0;
            _cells = false;
            for (auto& sprite : _sprites)
                sprite.set_visible(false);
        }
        if (canvas.map_pending())
            return;
        const auto& picture = map_pictures[target];
        auto* scratch = canvas.scratch();
        int first = _block * 128;
        if (first < picture.tile_count)
        {
            int count = bn::min(128, int(picture.tile_count) - first);
            const auto& block = picture.blocks[_block++];
            if (block.codec)
                lzss_decode(block.data, scratch, count * 32);
            else
                std::memcpy(scratch, block.data, unsigned(count * 32));
            canvas.map_upload(scratch, first, count);
            return;
        }
        if (!_cells)
        {
            if (picture.cells.codec)
                lzss_decode(picture.cells.data, scratch, 4096);
            else
                std::memcpy(scratch, picture.cells.data, 4096);
            auto* cells = reinterpret_cast<uint16_t*>(scratch);
            int bank = canvas.map_cell_offset();
            for (int i = 0; i < 2048; ++i)
                cells[i] = uint16_t(cells[i] | bank);
            canvas.map_cells(cells);
            _cells = true;
            return;
        }
        _loaded = target;
        canvas.map_reveal(false);
    }
    const auto& s = trial_map_segments[_segment];
    for (int budget = 2; budget && _sprites.size() < s.pins; --budget)
    {
        int i = _sprites.size();
        auto palette = bn::sprite_palette_ptr::create_new(bn::sprite_palette_item(
            bn::span<const bn::color>(reinterpret_cast<const bn::color*>(trial_map_palette), 16), bn::bpp_mode::BPP_4));
        auto tiles = bn::sprite_tiles_ptr::create(bn::sprite_tiles_item(
            bn::span<const bn::tile>(reinterpret_cast<const bn::tile*>(trial_map_pins + s.icons[i] * 128), 4),
            bn::bpp_mode::BPP_4));
        auto sprite = bn::sprite_ptr::create(0, 0, bn::sprite_shape_size(16, 16), tiles, palette);
        sprite.set_bg_priority(1);
        sprite.set_visible(false);
        _sprites.push_back(bn::move(sprite));
        _palettes.push_back(bn::move(palette));
    }
    position();
}
} // namespace manosaba
