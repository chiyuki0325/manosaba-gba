#include "video_upload.h"
#include "reenact_scene.h"
#include "bn_regular_bg_map_item.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_bg_palette_item.h"
#include "bn_sprite_palette_item.h"
#include "bn_sprite_shape_size.h"
#include "bn_bpp_mode.h"
#include "bn_array.h"
#include "bn_color.h"
#include "bn_memory.h"
#include "bn_tile.h"
#include "bn_rect_window.h"
#include "bn_window.h"
#include "bn_blending.h"
#include "bn_sprites.h"
namespace manosaba
{
void reenact_scene::protect() const
{
    if (_bg)
        video_upload::protect(*_bg);
    for (const auto& bg : _curtains)
        if (bg)
            video_upload::protect(*bg);
}

namespace
{
constexpr auto cells()
{
    bn::array<bn::regular_bg_map_cell, 1024> map{};
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x)
            map[y * 32 + x] = uint16_t(y * 30 + x + 1);
    return map;
}
constexpr auto background_cells = cells();
constexpr auto cloth_cells(int half)
{
    bn::array<bn::regular_bg_map_cell, 1024> map{};
    for (int y = 0; y < 20; ++y)
        for (int x = half * 15; x < half * 15 + 15; ++x)
            map[y * 32 + x] = background_cells[y * 32 + x];
    return map;
}
constexpr auto left_cells = cloth_cells(0), right_cells = cloth_cells(1);
constexpr auto shade_cells()
{
    bn::array<bn::regular_bg_map_cell, 1024> map{};
    for (int y = 13; y < 20; ++y)
        for (int x = 0; x < 30; ++x)
            map[y * 32 + x] = uint16_t(y == 13 ? 601 : y == 14 ? 602 : 603);
    return map;
}
constexpr auto panel_cells = shade_cells();
constexpr auto shade_tiles()
{
    bn::array<bn::tile, 3> tiles{};
    constexpr int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int t = 0; t < 3; ++t)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (t * 8 + y >= 16 || bayer[y % 4][x % 4] < t * 8 + y + 1)
                    tiles[t].data[y] |= 15u << (x * 4);
    return tiles;
}
constexpr auto panel_tiles = shade_tiles();
const bn::tile& tile(int index)
{
    return *reinterpret_cast<const bn::tile*>(reenact_tiles + index * 32);
}
} // namespace
void reenact_scene::allocate()
{
    // Fixed workspaces keep the largest scene possible without fragmenting BG
    // blocks when an almost empty backdrop is followed by a textured one.
    _background_tiles = bn::regular_bg_tiles_ptr::allocate(601, bn::bpp_mode::BPP_4);
    _curtain_tiles = bn::regular_bg_tiles_ptr::allocate(604, bn::bpp_mode::BPP_4);
    _background_palette = bn::bg_palette_ptr::create(bn::bg_palette_item(
        bn::span<const bn::color>(reinterpret_cast<const bn::color*>(reenact_palette), 16), bn::bpp_mode::BPP_4));
    _palette = bn::sprite_palette_ptr::create(bn::sprite_palette_item(
        bn::span<const bn::color>(reinterpret_cast<const bn::color*>(reenact_palette), 16), bn::bpp_mode::BPP_4));
    auto curtain_palette = bn::bg_palette_ptr::create(
        bn::bg_palette_item(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(reenact_curtain_palette), 16),
                            bn::bpp_mode::BPP_4));
    _bg = bn::regular_bg_ptr::create(
        8, 48,
        bn::regular_bg_map_ptr::create(bn::regular_bg_map_item(background_cells[0], bn::size(32, 32)),
                                       *_background_tiles, *_background_palette));
    _curtains[0] = bn::regular_bg_ptr::create(
        8, 48,
        bn::regular_bg_map_ptr::create(bn::regular_bg_map_item(left_cells[0], bn::size(32, 32)), *_curtain_tiles,
                                       curtain_palette));
    _curtains[1] = bn::regular_bg_ptr::create(
        8, 48,
        bn::regular_bg_map_ptr::create(bn::regular_bg_map_item(right_cells[0], bn::size(32, 32)), *_curtain_tiles,
                                       curtain_palette));
    _bg->set_priority(2);
    _bg->set_blending_bottom_enabled(true);
    _bg->set_visible(false);
    for (auto& curtain : _curtains)
    {
        curtain->set_priority(0);
        curtain->set_z_order(1);
        curtain->set_visible(false);
    }
    bn::sprites::set_blending_bottom_enabled(true);
    _panel = false;
    auto left = bn::rect_window::internal(), right = bn::rect_window::external();
    for (int i = 0; i < 2; ++i)
    {
        _curtains[i]->set_visible_in_window(i == 0, left);
        _curtains[i]->set_visible_in_window(i == 1, right);
        bn::window::outside().set_show_bg(*_curtains[i], false);
        bn::window::sprites().set_show_bg(*_curtains[i], false);
    }
    for (auto* tiles : {&*_background_tiles, &*_curtain_tiles})
        for (auto& word : tiles->vram().value()[0].data)
            word = 0;
    bn::memory::copy(panel_tiles[0], 3, _curtain_tiles->vram().value()[601]);
    _background_uploaded = _curtain_uploaded = 0;
    _position = -1;
}
void reenact_scene::reset()
{
    visible(false);
    _sprites.clear();
    _tiles[0].clear();
    _tiles[1].clear();
    _bg.reset();
    for (auto& curtain : _curtains)
        curtain.reset();
    _background_tiles.reset();
    _curtain_tiles.reset();
    _background_palette.reset();
    _palette.reset();
    _scene = -1;
    _pose = -1;
    _clock = _front = _target = _transition = _opening = _fade = 0;
    _releasing = _fade_out = _retiring = false;
    _background = nullptr;
    _objects_ready = false;
}
bool reenact_scene::release()
{
    visible(false);
    _releasing = true;
    int bank = _pose < 0 ? _front ^ 1 : _front;
    for (int budget = 8; budget && !_sprites.empty(); --budget)
    {
        _sprites.pop_back();
        if (!_tiles[bank].empty())
            _tiles[bank].pop_back();
    }
    if (!_sprites.empty())
        return false;
    for (int b = 0; b < 2; ++b)
        for (int budget = 8; budget && !_tiles[b].empty(); --budget)
            _tiles[b].pop_back();
    if (!_tiles[0].empty() || !_tiles[1].empty())
        return false;
    _bg.reset();
    for (auto& curtain : _curtains)
        curtain.reset();
    _background_tiles.reset();
    _curtain_tiles.reset();
    _background_palette.reset();
    _palette.reset();
    _pose = -1;
    _background = nullptr;
    _retiring = false;
    return true;
}
void reenact_scene::request(int scene, bool settled, bool closed)
{
    bool changed = scene != _scene;
    bool rebuilding = !_bg;
    if (changed && !_sprites.empty())
    {
        for (int budget = 8; budget && !_sprites.empty(); --budget)
        {
            _sprites.pop_back();
            _tiles[_front].pop_back();
        }
        return;
    }
    if (changed)
    {
        _sprites.clear();
        _tiles[0].clear();
        _tiles[1].clear();
        _scene = scene;
        _pose = -1;
        _target = 0;
        _front = 0;
        _clock = settled ? reenact_assets[scene].duration : 0;
        _opening = settled && !closed ? 120 : 0;
        _transition = 0;
        _fade_out = _retiring = false;
        _fade = 0;
        _background = nullptr;
    }
    if (!_bg)
    {
        allocate();
        _pose = -1;
    }
    _releasing = false;
    int target = target_pose();
    if (changed || rebuilding || target != _target)
        request_pose(target);
}
void reenact_scene::request_pose(int pose)
{
    _target = pose;
    _prepared = _upload_part = _upload_tile = 0;
    _objects_ready = false;
    if (!_retiring)
        _tiles[_front ^ 1].clear();
    const auto& asset = reenact_assets[_scene];
    const auto& next = asset.poses[pose];
    if (next.background != _background)
    {
        _fade_out = _pose >= 0 && _opening > 0;
        _background = next.background;
        _background_uploaded = 0;
    }
    if (_pose >= 0 && next.parts == asset.poses[_pose].parts)
        _objects_ready = true;
}
void reenact_scene::prepare()
{
    if (_retiring)
    {
        auto& old = _tiles[_front ^ 1];
        for (int budget = 8; budget && !old.empty(); --budget)
            old.pop_back();
        _retiring = !old.empty();
        if (_retiring)
            return;
    }
    if (!_bg || _releasing || _objects_ready)
        return;
    const auto& pose = reenact_assets[_scene].poses[_target];
    for (int budget = 4; budget && _prepared < pose.count; --budget)
    {
        const auto& part = pose.parts[_prepared++];
        _tiles[_front ^ 1].push_back(
            bn::sprite_tiles_ptr::allocate(part.width * part.height / 64, bn::bpp_mode::BPP_4));
        if (_pose < 0)
        {
            auto sprite = bn::sprite_ptr::create(0, 0, bn::sprite_shape_size(part.width, part.height),
                                                 _tiles[_front ^ 1].back(), *_palette);
            sprite.set_bg_priority(1);
            sprite.set_z_order(-part.group);
            sprite.set_visible(false);
            _sprites.push_back(bn::move(sprite));
        }
    }
    if (_upload_part == pose.count)
        _objects_ready = true;
}
void reenact_scene::publish()
{
    const auto& asset = reenact_assets[_scene];
    const auto& pose = asset.poses[_target];
    if (_pose < 0 || pose.parts != asset.poses[_pose].parts)
    {
        _retiring = !_tiles[_front].empty();
        _front ^= 1;
        while (_sprites.size() > pose.count)
            _sprites.pop_back();
        for (int i = 0; i < pose.count; ++i)
        {
            const auto& part = pose.parts[i];
            if (i < _sprites.size())
            {
                auto& sprite = _sprites[i];
                sprite.set_tiles(bn::sprite_shape_size(part.width, part.height), _tiles[_front][i]);
                sprite.set_z_order(-part.group);
            }
            else
            {
                auto sprite = bn::sprite_ptr::create(0, 0, bn::sprite_shape_size(part.width, part.height),
                                                     _tiles[_front][i], *_palette);
                sprite.set_bg_priority(1);
                sprite.set_z_order(-part.group);
                sprite.set_visible(false);
                _sprites.push_back(bn::move(sprite));
            }
        }
    }
    _pose = _target;
    _position = -1;
    position();
}
void reenact_scene::position()
{
    if (_pose < 0)
        return;
    const auto& asset = reenact_assets[_scene];
    int move = bn::min(phase(), int(asset.move_count) - 1);
    if (move == _position)
        return;
    _position = move;
    const auto& pose = asset.poses[_pose];
    for (int i = 0; i < _sprites.size(); ++i)
    {
        const auto& p = pose.parts[i];
        int dx = 0, dy = 0;
        if (p.group == asset.moving)
        {
            dx = asset.moves[move].x;
            dy = asset.moves[move].y;
        }
        _sprites[i].set_position(p.x + p.width / 2 + dx - 120, p.y + p.height / 2 + dy - 80);
    }
}
void reenact_scene::open()
{
    _clock = 0;
    if (_target)
        request_pose(0);
    _position = -1;
    position();
    _transition = 1;
}
void reenact_scene::close()
{
    _transition = -1;
}
int reenact_scene::phase() const
{
    const auto& asset = reenact_assets[_scene];
    return asset.loop && asset.duration ? _clock % asset.duration : bn::min(_clock, int(asset.duration));
}
int reenact_scene::target_pose() const
{
    const auto& asset = reenact_assets[_scene];
    return asset.duration ? (phase() * (asset.pose_count - 1) + (asset.loop ? asset.duration / 2 : 0)) / asset.duration
                          : 0;
}
void reenact_scene::panel(bool enabled)
{
    if (enabled == _panel)
        return;
    _panel = enabled;
    auto map = _curtains[0]->map();
    map.set_cells_ref(bn::regular_bg_map_item(enabled ? panel_cells[0] : left_cells[0], bn::size(32, 32)));
    auto palette = _curtains[0]->palette();
    palette.set_color(15, enabled ? bn::color(1, 1, 1) : bn::color(reenact_curtain_palette[15]));
    _curtains[0]->set_blending_top_enabled(enabled);
    bn::blending::set_transparency_alpha(enabled ? bn::fixed(.625) : bn::fixed(1));
    bn::window::outside().set_show_bg(*_curtains[0], enabled);
}
void reenact_scene::curtains()
{
    if (!_curtains[0])
        return;
    panel(_opening == 120);
    bool cloth = _visible && _curtain_uploaded == 600 && !_panel;
    auto left = bn::rect_window::internal(), right = bn::rect_window::external();
    left.set_boundaries(-80, -120, 80, -_opening);
    right.set_boundaries(-80, _opening, 80, 120);
    left.set_visible(cloth);
    right.set_visible(cloth);
    _curtains[0]->set_top_left_position(_panel ? 0 : -_opening, 0);
    _curtains[1]->set_top_left_position(_opening, 0);
    _curtains[0]->set_visible(_visible && _curtain_uploaded == 600);
    _curtains[1]->set_visible(cloth);
}
void reenact_scene::visible(bool enabled)
{
    _visible = enabled;
    if (_bg)
        _bg->set_visible(enabled && _pose >= 0);
    curtains();
    for (auto& sprite : _sprites)
        sprite.set_visible(enabled && _pose >= 0);
}
void reenact_scene::tint(palette_effect effect)
{
    if (!_bg)
        return;
    auto background = effect_over({0, uint16_t(_fade * 16)}, effect);
    _background_palette->set_fade(bn::color(background.color), bn::fixed::from_data(background.amount * 16));
    _palette->set_fade(bn::color(effect.color), bn::fixed::from_data(effect.amount * 16));
    auto cloth = _curtains[0]->palette();
    cloth.set_fade(bn::color(effect.color), bn::fixed::from_data(effect.amount * 16));
}
void reenact_scene::update(bool display_ready, int frames)
{
    if (!_bg || _releasing)
        return;
    prepare();
    if (_fade_out)
    {
        _fade = bn::min(16, _fade + frames * 2);
        _background_palette->set_fade(bn::color(0, 0, 0), bn::fixed::from_data(_fade * 256));
    }
    if (_objects_ready && _background_uploaded == 600 && _curtain_uploaded == 600 && _pose != _target)
        publish();
    if (ready() && display_ready)
    {
        if (!_fade_out && _fade)
        {
            _fade = bn::max(0, _fade - frames * 2);
            _background_palette->set_fade(bn::color(0, 0, 0), bn::fixed::from_data(_fade * 256));
        }
        if (_transition)
        {
            _opening = bn::clamp(_opening + _transition * frames * 3, 0, 120);
            if (_opening == 0 || _opening == 120)
                _transition = 0;
        }
        else if (_opening == 120)
        {
            const auto& asset = reenact_assets[_scene];
            _clock = asset.loop ? _clock + frames : bn::min(int(asset.duration), _clock + frames);
            int target = target_pose();
            if (target != _target)
                request_pose(target);
            position();
        }
    }
    visible(_visible);
}
int reenact_scene::commit(int bytes)
{
    if (!_bg || _releasing)
        return bytes;
    int budget = bn::min(bytes, 2048), available = budget;
    auto cloth = _curtain_tiles->vram().value();
    while (_curtain_uploaded < 600 && available >= 32)
    {
        bn::memory::copy(tile(reenact_curtain[_curtain_uploaded]), 1, cloth[1 + _curtain_uploaded]);
        ++_curtain_uploaded;
        available -= 32;
    }
    if (!_fade_out || _fade == 16)
    {
        auto bg = _background_tiles->vram().value();
        while (_background_uploaded < 600 && available >= 32)
        {
            bn::memory::copy(tile(_background[_background_uploaded]), 1, bg[1 + _background_uploaded]);
            ++_background_uploaded;
            available -= 32;
        }
        if (_background_uploaded == 600)
            _fade_out = false;
    }
    const auto& pose = reenact_assets[_scene].poses[_target];
    while (!_retiring && !_objects_ready && _upload_part < _prepared && available >= 32)
    {
        const auto& part = pose.parts[_upload_part];
        auto vram = _tiles[_front ^ 1][_upload_part].vram().value();
        int total = part.width * part.height / 64;
        while (_upload_tile < total && available >= 32)
        {
            bn::memory::copy(tile(part.tiles[_upload_tile]), 1, vram[_upload_tile]);
            ++_upload_tile;
            available -= 32;
        }
        if (_upload_tile == total)
        {
            ++_upload_part;
            _upload_tile = 0;
        }
        if (_upload_part == pose.count)
            _objects_ready = true;
    }
    return bytes - (budget - available);
}
int reenact_scene::tile_bytes() const
{
    int total = 0;
    for (const auto& tiles : _tiles[_front])
        total += tiles.tiles_count() * 32;
    return total;
}
} // namespace manosaba
