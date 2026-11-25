#ifndef WITCH_BOOK_RUNTIME_H
#define WITCH_BOOK_RUNTIME_H
#include <stdint.h>
typedef struct witch_book_profile
{
    uint32_t open, mode, tab, count, selected, first, loading, ready, paused;
    uint32_t script, state, line_left, total_left, camera_frame, paint, frames, missed;
    int32_t map_floor, map_area, map_left, map_top;
    uint32_t source;
} witch_book_profile;
#ifdef __cplusplus
extern "C"
{
    extern witch_book_profile witch_book_runtime;
}
#endif
#endif
