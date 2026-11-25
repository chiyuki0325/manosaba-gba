#ifndef MANOSABA_CREDITS_RUNTIME_H
#define MANOSABA_CREDITS_RUNTIME_H
#include <stdint.h>
typedef struct credits_runtime_profile
{
    uint32_t active, act, phase, pixel, row, total, flags, frame, missed, core_ticks, max_ticks, draw_ticks, max_draw,
        ready, done, source;
} credits_runtime_profile;
#ifdef __cplusplus
extern "C"
{
    extern credits_runtime_profile credits_runtime;
}
#endif
#endif
