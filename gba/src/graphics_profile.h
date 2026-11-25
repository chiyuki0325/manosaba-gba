#ifndef GRAPHICS_PROFILE_H
#define GRAPHICS_PROFILE_H
#include <stdint.h>
#ifdef __cplusplus
namespace manosaba
{
#endif
struct graphics_profile
{
    uint32_t cpu_ticks, max_cpu_ticks, missed_frames, frames;
    int32_t bank, event;
    uint32_t core_ticks, max_core_ticks, max_vblank_ticks;
    uint32_t ui_ticks, update_ticks;
    uint32_t loading, clock;
    uint32_t upload_pending;
};
#ifdef __cplusplus
extern graphics_profile graphics_performance;
}
#endif
#endif
