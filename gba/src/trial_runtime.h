#ifndef TRIAL_RUNTIME_H
#define TRIAL_RUNTIME_H
#include <stdint.h>
typedef struct trial_runtime_profile
{
    uint32_t state, script, actor, view, line_left, duration, total_left, hp, links, choice, items, chosen, preview;
    uint32_t scene_ready, canvas_ready, movie_frame, frame, missed, core_ticks, max_ticks, movie_kind;
    uint32_t ui_work, scene_init, interaction_ready;
    uint32_t clear_work, text_work, hud_work;
    uint32_t camera, camera_frame, camera_duration, camera_scroll;
    uint32_t choice_first, choice_rows;
    uint32_t layout, pose, background, window_x;
    uint32_t title_kind, title_frame, title_stage;
    uint32_t pc, vm_state, vm_script, vm_hp;
} trial_runtime_profile;
#ifdef __cplusplus
extern "C"
{
    extern trial_runtime_profile trial_runtime;
}
#endif
#endif
