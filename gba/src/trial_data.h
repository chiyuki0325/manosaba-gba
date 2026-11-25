#ifndef TRIAL_DATA_H
#define TRIAL_DATA_H
#include "actor_crop.h"
namespace manosaba
{
struct trial_bg_asset
{
    const uint32_t* tiles;
    const uint8_t* banks;
    uint16_t columns;
};
struct trial_camera_layout
{
    const uint8_t* actors;
    uint8_t seats, background;
};
struct trial_camera_motion
{
    int16_t from, to;
    uint16_t frames;
    uint8_t layout, ease, start_view, end_view, mode;
    int8_t rise;
    uint8_t actor;
};
struct trial_view_placement
{
    int16_t x, y, cutoff;
    uint16_t scale;
};
extern const uint8_t trial_bytes[];
extern const trial_bg_asset trial_backgrounds[];
extern const trial_view_placement trial_view_positions[2][9];
extern const uint16_t* const trial_bg_palette;
extern const uint16_t trial_actor_images[17], trial_demo_images[3];
extern const trial_actor_placement trial_actor_positions[17][4], trial_demo_positions[4][3], trial_empty_position;
extern const trial_camera_layout trial_camera_layouts[];
extern const trial_camera_motion trial_camera_motions[];
extern const trial_actor_placement trial_pan_positions[2][17];
} // namespace manosaba
#endif
