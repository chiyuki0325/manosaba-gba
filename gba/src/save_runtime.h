#ifndef MANOSABA_SAVE_RUNTIME_H
#define MANOSABA_SAVE_RUNTIME_H
#include <stdint.h>
typedef struct save_runtime_profile
{
    uint32_t view, paint, selected, first, confirm, confirm_choice, operation, busy, progress, available, seed_size;
    uint32_t script, pc, restoring;
    uint32_t draw_work, thumb_work, snapshot_work;
    uint32_t row_step;
} save_runtime_profile;
#ifdef __cplusplus
extern "C"
{
    extern save_runtime_profile save_runtime;
}
#endif
#endif
