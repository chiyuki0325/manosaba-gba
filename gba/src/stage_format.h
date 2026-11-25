#ifndef STAGE_FORMAT_H
#define STAGE_FORMAT_H
#include <cstdint>
namespace manosaba
{
constexpr int stage_actor_count = 3;
constexpr int stage_entity_count = 16; // 3 characters, 8 logical scenery slots, camera, overlay, 3 masks.
constexpr int stage_camera = 11, stage_overlay = 12, stage_mask_first = 13;
constexpr int stage_one = 256;
enum class stage_property : uint8_t
{
    x,
    y,
    scale,
    angle,
    opacity,
    tint,
    blur,
    reveal,
    fill,
    red,
    green,
    blue,
    count
};
enum class stage_ease : uint8_t
{
    linear,
    smooth,
    in_quad,
    out_quad,
    in_out_quad,
    in_cubic,
    out_cubic,
    sine,
    out_back
};
enum class stage_kind : uint8_t
{
    image,
    tween,
    wait,
    clear,
    order,
    mask,
    emote,
    maskable,
    printer,
    set,
    cg_backend
};
struct stage_key
{
    int32_t value;
    uint16_t frames;
    stage_ease easing;
    uint8_t relative;
};
struct stage_command
{
    int32_t value;
    uint16_t first, count;
    stage_kind kind;
    uint8_t target, property, flags;
};
struct stage_event
{
    uint16_t first, count, source_line;
};
struct stage_texture
{
    const uint8_t* pixels;
    const uint8_t* blurred;
    uint16_t width, height;
    int16_t pivot_x, pivot_y;
    uint16_t nominal_scale;
    bool opaque;
};
extern const stage_event stage_events[];
extern const stage_command stage_commands[];
extern const stage_key stage_keys[];
extern const stage_texture stage_textures[];
extern const uint16_t stage_palette[240];
extern const uint8_t stage_rgb_lookup[32768];
extern const uint8_t stage_dark[240];
extern const uint8_t stage_masks[][240 * 160];
extern const int stage_event_count, stage_texture_count, stage_mask_count;
} // namespace manosaba
#endif
