#ifndef MANOSABA_TRIAL_SPECIAL_RUNTIME_H
#define MANOSABA_TRIAL_SPECIAL_RUNTIME_H
#include <stdint.h>
typedef struct trial_special_profile
{
    uint32_t map, map_clock, map_duration, map_ready, map_floor, map_left, map_top, map_pins, map_alpha, pin_x, pin_y,
        mask, shake_actor, picture, frames, missed;
} trial_special_profile;
#ifdef __cplusplus
extern "C"
{
    extern trial_special_profile trial_special_runtime;
}
#endif
#endif
