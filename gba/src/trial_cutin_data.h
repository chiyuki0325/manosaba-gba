#ifndef TRIAL_CUTIN_DATA_H
#define TRIAL_CUTIN_DATA_H
#include "graphics_data.h"
#include "actor_crop.h"
namespace manosaba
{
struct trial_cutin_frame
{
    int16_t x;
    uint8_t alpha, flash;
};
struct trial_cutin_asset
{
    const graphic_asset* glass;
    const graphic_asset* poses;
    const graphic_asset* shards;
    const trial_actor_placement* positions;
    const trial_actor_placement* glass_position;
    const trial_cutin_frame* frames;
    uint16_t count;
};
extern const uint8_t trial_cutin_bytes[];
extern const trial_cutin_asset trial_cutins[3];
} // namespace manosaba
#endif
