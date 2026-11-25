// Host-only budget audit. Reuses the production stage VM; never linked into ROM.
#include "stage_player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

namespace manosaba
{
const stage_key stage_keys[] = {{}};
const stage_command stage_commands[] = {{}};
} // namespace manosaba
using namespace manosaba;
struct part
{
    int x, y, w, h, group = 0;
#ifdef MANOSABA_AFFINE_FOOTPRINT
    std::array<uint64_t, 64> ink{};
#endif
};
struct asset
{
    int kind, w, h, px, py, fps;
    std::vector<part> parts;
    std::vector<int> frames;
};
struct point
{
    int x, y;
};
struct placement
{
    point center;
    int c, s;
};
static std::vector<asset> assets;
static bool cull_invisible = true;
static int read_int(FILE* f)
{
    int v;
    if (fread(&v, 4, 1, f) != 1)
        std::abort();
    return v;
}
static std::vector<int> read_list(FILE* f)
{
    std::vector<int> out(read_int(f));
    for (int& v : out)
        v = read_int(f);
    return out;
}
static int sine(int angle)
{
    angle %= 360 * 256;
    if (angle < 0)
        angle += 360 * 256;
    int index = angle * 2048 / (360 * 256);
    return int(std::round(std::sin(index * 2 * 3.141592653589793 / 2048) * 4096));
}
static int cosine(int angle)
{
    return sine(angle + 90 * 256);
}
static placement transform(const stage_entity& e, const stage_entity& cam)
{
    int dx = e.values[0] - 120 * 256 - cam.values[0], dy = e.values[1] - 80 * 256 - cam.values[1];
    int c = cosine(cam.values[3]), s = sine(cam.values[3]), z = cam.values[2];
    int scale = e.values[2] * z / 256, angle = e.values[3] + cam.values[3];
    return {{120 * 256 + int((int64_t(dx) * c - int64_t(dy) * s) * z / (4096 * 256)),
             80 * 256 + int((int64_t(dx) * s + int64_t(dy) * c) * z / (4096 * 256))},
            cosine(angle) * scale / 4096,
            sine(angle) * scale / 4096};
}
static point center(const placement& t, const asset& a, const part& p)
{
    int dx = p.x + p.w / 2 - a.px, dy = p.y + p.h / 2 - a.py;
    return {t.center.x + dx * t.c - dy * t.s, t.center.y + dx * t.s + dy * t.c};
}
static bool visible(const placement& t, const asset& a, const part& p, int margin)
{
    auto v = center(t, a, p);
    int hx = (abs(t.c) * p.w + abs(t.s) * p.h) / 2, hy = (abs(t.s) * p.w + abs(t.c) * p.h) / 2;
    return v.x + hx >= -margin * 256 && v.x - hx <= (240 + margin) * 256 && v.y + hy >= -margin * 256 &&
           v.y - hy <= (160 + margin) * 256;
}
static bool covers(const placement& t, const asset& a)
{
    int64_t len = int64_t(t.c) * t.c + int64_t(t.s) * t.s;
    if (!len)
        return false;
    for (int corner = 0; corner < 4; ++corner)
    {
        int x = (corner & 1 ? 240 * 256 : 0) - t.center.x, y = (corner & 2 ? 160 * 256 : 0) - t.center.y;
        if (std::abs(int64_t(x) * t.c + int64_t(y) * t.s) > a.w * len / 2 ||
            std::abs(-int64_t(x) * t.s + int64_t(y) * t.c) > a.h * len / 2)
            return false;
    }
    return true;
}
static bool doubled(int scale, int angle, const part& p)
{
    // Butano AUTO: inverse affine registers, and the same shape aspect tests.
    int inv = 65536 / std::max(1, scale), c = cosine(-angle), s = sine(-angle);
    int pa = c * inv >> 12, pb = -s * inv >> 12, pc = s * inv >> 12, pd = c * inv >> 12, div = pa * pd - pb * pc;
    if (!div)
        return false;
    int w = 32, h = 32;
    if (p.w > p.h)
        h = p.w == 4 * p.h ? 8 : 16;
    if (p.h > p.w)
        w = p.h == 4 * p.w ? 8 : 16;
    int ix1 = (-256 * h * pb - 256 * w * pd + 256 * pb) / div;
    int ix2 = (-256 * h * pb + 256 * w * pd + 256 * pb - 256 * pd) / div;
    int iy1 = 256 * (h * pa + w * pc - pa) / div, iy2 = 256 * (h * pa - w * pc - pa + pc) / div;
    return ix1 < -w || ix1 >= w || ix2 < -w || ix2 >= w || iy1 < -h || iy1 >= h || iy2 < -h || iy2 >= h;
}
struct slot
{
    int image = -1;
    std::vector<int> loaded;
};
struct metrics
{
    int bytes = 0, cycles = 0, objects = 0, row = 0;
    int images[3] = {-1, -1, -1}, entities[3] = {0, 1, 2};
};
#ifdef MANOSABA_AFFINE_FOOTPRINT
static int footprint_clipped(const placement& t, const asset& a, const part& p, int scale, int angle, bool twice)
{
    int inv = 65536 / std::max(1, scale), c = cosine(-angle), s = sine(-angle);
    int pa = int16_t(c * inv >> 12), pb = int16_t(-s * inv >> 12), pc = int16_t(s * inv >> 12), pd = pa;
    int64_t determinant = int64_t(pa) * pd - int64_t(pb) * pc;
    if (!determinant)
        return 0;
    int hw = p.w * (twice ? 2 : 1) / 2, hh = p.h * (twice ? 2 : 1) / 2;
    int hx = int((int64_t(abs(pd)) * p.w + int64_t(abs(pb)) * p.h) * 128 / abs(determinant)) + 1;
    int hy = int((int64_t(abs(pc)) * p.w + int64_t(abs(pa)) * p.h) * 128 / abs(determinant)) + 1;
    if (hx <= hw && hy <= hh)
        return 0;
    auto point = center(t, a, p);
    int cx = int(std::floor(double(point.x) / 256)), cy = int(std::floor(double(point.y) / 256));
    for (int y = std::max(-hy, -cy); y <= std::min(hy, 159 - cy); ++y)
        for (int x = std::max(-hx, -cx); x <= std::min(hx, 239 - cx); ++x)
        {
            if (x >= -hw && x < hw && y >= -hh && y < hh)
                continue;
            int u = ((pa * x + pb * y) >> 8) + p.w / 2, v = ((pc * x + pd * y) >> 8) + p.h / 2;
            if (u >= 0 && u < p.w && v >= 0 && v < p.h && ((p.ink[v] >> u) & 1))
                return 1;
        }
    return 0;
}
static int footprint_parts;
static int footprint_actor_parts[3];
#endif
static int image_for(const stage_player& vm, int entity)
{
    int i = vm.entity(entity).image;
    if (i >= 0 && assets[i].kind == 5)
    {
        auto& a = assets[i];
        i = a.frames[(vm.clock() * a.fps / 60) % a.frames.size()];
    }
    return i;
}
static int clip_for(const stage_player& vm, int id)
{
    auto& e = vm.entity(id);
    if (!e.maskable || id >= stage_overlay)
        return -1;
    int clip = -1;
    for (int m = stage_mask_first; m < stage_entity_count; ++m)
    {
        auto& r = vm.entity(m);
        if (r.image >= 0 && r.values[4] > 0 && (r.order + 1) / 20 == e.order / 20)
            clip = m;
    }
    return clip;
}
static bool mask_intersects(const stage_entity& e, const stage_entity& mask, const asset& shape, const asset& a,
                            const part& p)
{
    int scale = std::max(1, mask.values[2]), angle = e.values[3] - mask.values[3];
    int c = cosine(angle), s = sine(angle), ic = cosine(mask.values[3]), is = sine(mask.values[3]);
    int dx = e.values[0] - mask.values[0], dy = e.values[1] - mask.values[1];
    int u = shape.w * 128 + int((int64_t(dx) * ic + int64_t(dy) * is) / 16 / scale);
    int v = shape.h * 128 + int((-int64_t(dx) * is + int64_t(dy) * ic) / 16 / scale);
    int du = c * e.values[2] / 16 / scale, dv = s * e.values[2] / 16 / scale;
    int x = p.x - a.px, y = p.y - a.py, u0 = u + x * du - y * dv, v0 = v + x * dv + y * du;
    int ux = (p.w - 1) * du, uy = -(p.h - 1) * dv, vx = (p.w - 1) * dv, vy = (p.h - 1) * du;
    return u0 + std::max(0, ux) + std::max(0, uy) >= 0 && u0 + std::min(0, ux) + std::min(0, uy) < shape.w * 256 &&
           v0 + std::max(0, vx) + std::max(0, vy) >= 0 && v0 + std::min(0, vx) + std::min(0, vy) < shape.h * 256;
}
#ifdef MANOSABA_ACTOR_PART_CROP
static part viewport_crop(const placement& t, const asset& a, const part& original)
{
    int64_t length = int64_t(t.c) * t.c + int64_t(t.s) * t.s;
    if (!length)
        return original;
    double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
    for (int corner = 0; corner < 4; ++corner)
    {
        int x = (corner & 1 ? 242 : -2) * 256 - t.center.x, y = (corner & 2 ? 162 : -2) * 256 - t.center.y;
        double u = double(int64_t(x) * t.c + int64_t(y) * t.s) / length + a.px;
        double v = double(-int64_t(x) * t.s + int64_t(y) * t.c) / length + a.py;
        left = std::min(left, u);
        right = std::max(right, u);
        top = std::min(top, v);
        bottom = std::max(bottom, v);
    }
    left = std::max(double(original.x), left - 8);
    right = std::min(double(original.x + original.w), right + 8);
    top = std::max(double(original.y), top - 8);
    bottom = std::min(double(original.y + original.h), bottom + 8);
    static constexpr int shapes[][2] = {{8, 8},   {16, 8},  {8, 16},  {16, 16}, {32, 8},  {8, 32},
                                        {32, 16}, {16, 32}, {32, 32}, {64, 32}, {32, 64}, {64, 64}};
    part result = original;
    for (auto& shape : shapes)
    {
        int w = shape[0], h = shape[1];
        if (w > original.w || h > original.h || w * h >= result.w * result.h)
            continue;
        int x = original.x + std::clamp(int(std::floor((left - original.x) / 8)) * 8, 0, original.w - w);
        int y = original.y + std::clamp(int(std::floor((top - original.y) / 8)) * 8, 0, original.h - h);
        if (x <= left && y <= top && x + w >= right && y + h >= bottom)
            result = {x, y, w, h};
    }
    return result;
}
#endif
static metrics measure(const stage_player& vm, slot (&slots)[3])
{
#ifdef MANOSABA_AFFINE_FOOTPRINT
    footprint_parts = 0;
    std::fill(footprint_actor_parts, footprint_actor_parts + 3, 0);
#endif
    const auto& cam = vm.entity(stage_camera);
    std::vector<int> bgs;
    for (int id = 3; id < stage_camera; ++id)
    {
        int i = image_for(vm, id);
        if (i >= 0 && (assets[i].kind == 1 || assets[i].kind == 2) && vm.entity(id).values[4] > 0)
            bgs.push_back(id);
    }
    std::stable_sort(bgs.begin(), bgs.end(), [&](int a, int b) { return vm.entity(a).order < vm.entity(b).order; });
    int wanted[2] = {bgs.size() > 1 ? bgs[bgs.size() - 2] : -1, bgs.empty() ? -1 : bgs.back()};
    if (image_for(vm, stage_overlay) >= 0 && vm.entity(stage_overlay).values[4] > 0)
        wanted[0] = stage_overlay;
    bool covered[2] = {};
    for (int p = 0; p < 2; ++p)
    {
        int id = wanted[p];
        covered[p] = id >= 0 && vm.entity(id).values[4] >= 256 && clip_for(vm, id) < 0 &&
                     covers(transform(vm.entity(id), cam), assets[image_for(vm, id)]);
    }
    int entities[3] = {0, 1, 2}, images[3];
    for (int s = 0; s < 3; ++s)
    {
        int i = image_for(vm, s);
        bool hide = vm.entity(s).values[4] <= 0;
        for (int p = 0; p < 2; ++p)
            hide |= covered[p] && vm.entity(s).order < vm.entity(wanted[p]).order;
        images[s] = i >= 0 && assets[i].kind == 0 && !hide ? i : -1;
    }
    if (wanted[0] != stage_overlay && !covered[0] && !covered[1])
        for (int j = int(bgs.size()) - 3; j >= 0; --j)
        {
            int id = bgs[j];
            auto& a = assets[image_for(vm, id)];
            if (a.frames.empty())
                continue;
            int s = 0;
            while (s < 3 && images[s] >= 0)
                ++s;
            if (s == 3)
                break;
            entities[s] = id;
            images[s] = a.frames[std::clamp(vm.entity(id).values[6], 0, 256) * int(a.frames.size() - 1) / 256];
        }
    metrics out;
    std::array<int, 161> diff{};
    for (int s = 0; s < 3; ++s)
    {
        int image = images[s];
        out.images[s] = image;
        out.entities[s] = entities[s];
        auto& slot = slots[s];
        if (image != slot.image)
        {
            slot.image = image;
            slot.loaded.clear();
        }
        if (image < 0)
            continue;
        auto& e = vm.entity(entities[s]);
        auto& a = assets[image];
        auto t = transform(e, cam);
        int clip = clip_for(vm, entities[s]);
        bool full_clip = cull_invisible && clip >= 0 && vm.entity(clip).values[4] >= 256;
        auto needed = [&](int p, int margin)
        {
            return visible(t, a, a.parts[p], margin) &&
                   (!full_clip || mask_intersects(e, vm.entity(clip), assets[vm.entity(clip).image], a, a.parts[p]));
        };
        int margin = cull_invisible ? 0 : 16;
        std::erase_if(slot.loaded, [&](int p) { return !needed(p, margin); });
        bool add = slot.loaded.empty();
        for (int p = 0; p < int(a.parts.size()) && !add; ++p)
            if (needed(p, 0) && std::find(slot.loaded.begin(), slot.loaded.end(), p) == slot.loaded.end())
                add = true;
        if (add)
            for (int p = 0; p < int(a.parts.size()); ++p)
                if (needed(p, margin) && std::find(slot.loaded.begin(), slot.loaded.end(), p) == slot.loaded.end())
                    slot.loaded.push_back(p);
        int scale = e.values[2] * cam.values[2] / 256, angle = e.values[3] + cam.values[3];
        bool shared_groups[65] = {};
        for (int p : slot.loaded)
        {
            auto part = a.parts[p];
#ifdef MANOSABA_ACTOR_PART_CROP
            part = viewport_crop(t, a, part);
#endif
            bool shared = false;
#ifdef MANOSABA_ACTOR_SILHOUETTE_SHARE
            if (e.values[5] == 0 && clip < 0 && part.group)
            {
                shared = shared_groups[part.group];
                shared_groups[part.group] = true;
            }
#else
            (void)shared_groups;
#endif
            if (!shared)
                out.bytes += part.w * part.h / 2;
            bool affine = true;
#if defined(MANOSABA_ACTOR_REGULAR) || defined(MANOSABA_ACTOR_SCALE_SNAP)
            affine = scale != 256 || angle % (360 * 256) != 0;
#endif
            auto v = center(t, a, part);
            bool twice = affine && doubled(scale, angle, part);
#ifdef MANOSABA_AFFINE_FOOTPRINT
            if (affine)
            {
                int clipped = footprint_clipped(t, a, part, scale, angle, twice);
                footprint_parts += clipped;
                footprint_actor_parts[s] += clipped;
            }
#endif
            int w = part.w * (twice ? 2 : 1), h = part.h * (twice ? 2 : 1);
            int x = int(std::floor(double(v.x) / 256)) - w / 2, y = int(std::floor(double(v.y) / 256)) - h / 2;
            if (x >= 240 || x + w <= 0 || y >= 160 || y + h <= 0)
                continue;
            ++out.objects;
            int cost = affine ? 8 + w * 2 + std::min(x, 0) : w - 2 + (std::min(x, 0) >> 1);
            diff[std::max(0, y)] += cost;
            diff[std::min(160, y + h)] -= cost;
        }
    }
    int cost = 2 * std::max(0, out.objects - 1);
    for (int y = 0; y < 160; ++y)
    {
        cost += diff[y];
        if (cost > out.cycles)
        {
            out.cycles = cost;
            out.row = y;
        }
    }
    return out;
}
int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5)
        return 2;
    cull_invisible = argv[3][0] == '1';
    FILE* input = fopen(argv[1], "rb");
    FILE* output = fopen(argv[2], "w");
    FILE* snapshots = argc == 5 ? fopen(argv[4], "wb") : nullptr;
    static_assert(sizeof(stage_entity) == 60);
    int count = read_int(input);
    assets.resize(count);
    for (auto& a : assets)
    {
        a.kind = read_int(input);
        a.w = read_int(input);
        a.h = read_int(input);
        a.px = read_int(input);
        a.py = read_int(input);
        a.fps = read_int(input);
        a.frames = read_list(input);
        a.parts.resize(read_int(input));
        for (auto& p : a.parts)
        {
            p.x = read_int(input);
            p.y = read_int(input);
            p.w = read_int(input);
            p.h = read_int(input);
#ifdef MANOSABA_ACTOR_SILHOUETTE_SHARE
            p.group = read_int(input);
#endif
#ifdef MANOSABA_AFFINE_FOOTPRINT
            if (fread(p.ink.data(), 8, p.h, input) != unsigned(p.h))
                std::abort();
#endif
        }
    }
    int chapters = read_int(input);
    uint64_t sampled = 0;
    int blocked_caps = 0;
#ifdef MANOSABA_AFFINE_FOOTPRINT
    fputs("bank,event,line,dwell_frames,clipped_parts,peak_offset_frames,actor0,actor1,actor2,camera_scale,parts0,"
          "scale0,effective0,angle0,parts1,scale1,effective1,angle1,parts2,scale2,effective2,angle2\n",
          output);
#else
    fputs("bank,event,line,dwell_frames,vram_bytes,scanline_cycles,visible_objs,worst_y,peak_offset_frames,actor0,"
          "actor1,actor2\n",
          output);
#endif
    for (int bank = 0; bank < chapters; ++bank)
    {
        std::vector<stage_command> commands(read_int(input));
        for (auto& c : commands)
        {
            c.value = read_int(input);
            c.first = read_int(input);
            c.count = read_int(input);
            c.kind = stage_kind(read_int(input));
            c.target = read_int(input);
            c.property = read_int(input);
            c.flags = read_int(input);
        }
        std::vector<stage_key> keys(read_int(input));
        for (auto& k : keys)
        {
            k.value = read_int(input);
            k.frames = read_int(input);
            k.easing = stage_ease(read_int(input));
            k.relative = read_int(input);
        }
        std::vector<stage_event> events(read_int(input));
        for (auto& e : events)
        {
            e.first = read_int(input);
            e.count = read_int(input);
            e.source_line = read_int(input);
        }
        auto text_lines = read_list(input);
        for (int dwell : {36, 180})
        {
            stage_player vm;
            vm.reset(keys.data(), commands.data());
            slot slots[3];
            for (int event = 0; event < int(events.size()); ++event)
            {
                vm.apply(events[event]);
                metrics peak;
                int worst_offset = 0, frames = 0;
#ifdef MANOSABA_AFFINE_FOOTPRINT
                int clipped_peak = 0, clip_offset = 0, clip_images[3] = {-1, -1, -1};
                int clip_camera = 256, clip_parts[3] = {}, clip_scales[3] = {}, clip_effective[3] = {},
                    clip_angles[3] = {};
#endif
                std::array<stage_entity, stage_entity_count> states[2];
                metrics at_peak[2];
                int offsets[2] = {};
                uint64_t part_masks[2][3] = {};
                auto sample = [&]
                {
                    ++sampled;
                    auto m = measure(vm, slots);
#ifdef MANOSABA_AFFINE_FOOTPRINT
                    if (footprint_parts > clipped_peak)
                    {
                        clipped_peak = footprint_parts;
                        clip_offset = frames;
                        std::copy(m.images, m.images + 3, clip_images);
                        clip_camera = vm.entity(stage_camera).values[2];
                        for (int s = 0; s < 3; ++s)
                        {
                            const auto& e = vm.entity(m.entities[s]);
                            clip_parts[s] = footprint_actor_parts[s];
                            clip_scales[s] = e.values[2];
                            clip_effective[s] = e.values[2] * clip_camera / 256;
                            clip_angles[s] = e.values[3] + vm.entity(stage_camera).values[3];
                        }
                    }
#endif
                    if (snapshots)
                        for (int k = 0; k < 2; ++k)
                            if ((k ? m.bytes : m.cycles) > (k ? at_peak[k].bytes : at_peak[k].cycles))
                            {
                                at_peak[k] = m;
                                offsets[k] = frames;
                                std::copy_n(&vm.entity(0), stage_entity_count, states[k].begin());
                                for (int s = 0; s < 3; ++s)
                                {
                                    part_masks[k][s] = 0;
                                    for (int p : slots[s].loaded)
                                        part_masks[k][s] |= uint64_t(1) << p;
                                }
                            }
                    if (m.cycles > peak.cycles)
                    {
                        peak.row = m.row;
                        worst_offset = frames;
                        for (int i = 0; i < 3; ++i)
                            peak.images[i] = m.images[i];
                    }
                    peak.bytes = std::max(peak.bytes, m.bytes);
                    peak.cycles = std::max(peak.cycles, m.cycles);
                    peak.objects = std::max(peak.objects, m.objects);
                };
                sample();
                int guard = 0;
                while (vm.blocked() && guard++ < 3600)
                {
                    vm.tick();
                    ++frames;
                    sample();
                }
                if (vm.blocked())
                    ++blocked_caps;
                if (std::find(text_lines.begin(), text_lines.end(), events[event].source_line) != text_lines.end())
                {
                    bool animated = false;
                    for (int id = 0; id < stage_entity_count; ++id)
                    {
                        int i = vm.entity(id).image;
                        animated |= i >= 0 && assets[i].kind == 5;
                    }
                    int f = 0;
                    for (; f < dwell && (vm.animating() || animated); ++f)
                    {
                        vm.tick();
                        ++frames;
                        sample();
                    }
                    if (f < dwell)
                        vm.tick(dwell - f);
                }
                if (snapshots && !vm.animating())
                {
                    auto m = measure(vm, slots);
                    bool clipped = false;
                    for (int s = 0; s < 3 && !clipped; ++s)
                        if (m.images[s] >= 0 && m.entities[s] < 3)
                        {
                            const auto& e = vm.entity(m.entities[s]);
                            const auto& cam = vm.entity(stage_camera);
                            auto t = transform(e, cam);
                            int scale = e.values[2] * cam.values[2] / 256, angle = e.values[3] + cam.values[3];
                            for (int index : slots[s].loaded)
                            {
                                const auto& p = assets[m.images[s]].parts[index];
                                int factor = doubled(scale, angle, p) ? 2 : 1;
                                if (abs(t.c) * p.w + abs(t.s) * p.h > factor * p.w * 256 + 256 ||
                                    abs(t.s) * p.w + abs(t.c) * p.h > factor * p.h * 256 + 256)
                                {
                                    clipped = true;
                                    break;
                                }
                            }
                        }
                    if (m.cycles > 1000 || clipped)
                    {
                        int header[] = {bank,
                                        event,
                                        events[event].source_line,
                                        dwell,
                                        2,
                                        frames,
                                        m.bytes,
                                        m.cycles,
                                        m.objects,
                                        m.row,
                                        m.images[0],
                                        m.images[1],
                                        m.images[2],
                                        m.entities[0],
                                        m.entities[1],
                                        m.entities[2]};
                        std::array<stage_entity, stage_entity_count> settled;
                        std::copy_n(&vm.entity(0), stage_entity_count, settled.begin());
                        uint64_t masks[3] = {};
                        for (int s = 0; s < 3; ++s)
                            for (int p : slots[s].loaded)
                                masks[s] |= uint64_t(1) << p;
                        fwrite(header, sizeof(header), 1, snapshots);
                        fwrite(settled.data(), sizeof(stage_entity), stage_entity_count, snapshots);
                        fwrite(masks, sizeof(masks), 1, snapshots);
                    }
                }
#ifdef MANOSABA_AFFINE_FOOTPRINT
                if (clipped_peak)
                {
                    fprintf(output, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", bank, event, events[event].source_line, dwell,
                            clipped_peak, clip_offset, clip_images[0], clip_images[1], clip_images[2], clip_camera);
                    for (int s = 0; s < 3; ++s)
                        fprintf(output, ",%d,%d,%d,%d", clip_parts[s], clip_scales[s], clip_effective[s],
                                clip_angles[s]);
                    fputc('\n', output);
                }
#else
                if (peak.bytes > 32768 || peak.cycles > 1210 || peak.objects > 128)
                {
                    fprintf(output, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", bank, event, events[event].source_line,
                            dwell, peak.bytes, peak.cycles, peak.objects, peak.row, worst_offset, peak.images[0],
                            peak.images[1], peak.images[2]);
                    if (snapshots)
                        for (int k = 0; k < 2; ++k)
                        {
                            const auto& m = at_peak[k];
                            int header[] = {bank,
                                            event,
                                            events[event].source_line,
                                            dwell,
                                            k,
                                            offsets[k],
                                            m.bytes,
                                            m.cycles,
                                            m.objects,
                                            m.row,
                                            m.images[0],
                                            m.images[1],
                                            m.images[2],
                                            m.entities[0],
                                            m.entities[1],
                                            m.entities[2]};
                            fwrite(header, sizeof(header), 1, snapshots);
                            fwrite(states[k].data(), sizeof(stage_entity), stage_entity_count, snapshots);
                            fwrite(part_masks[k], sizeof(part_masks[k]), 1, snapshots);
                        }
                }
#endif
            }
        }
        if (bank % 30 == 0)
            fprintf(stderr, "audited %d/%d banks\n", bank + 1, chapters);
    }
    fprintf(stderr, "sampled=%llu blocked_caps=%d banks=%d\n", (unsigned long long)sampled, blocked_caps, chapters);
    fclose(input);
    fclose(output);
    if (snapshots)
        fclose(snapshots);
}
