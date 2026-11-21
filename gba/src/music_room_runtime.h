#pragma once
#include <stdint.h>
typedef struct music_room_profile {uint32_t active,ready,selected,playing,page,unlocked,phase,dirty,missed;} music_room_profile;
#ifdef __cplusplus
extern "C" {
#endif
extern music_room_profile music_room_runtime;
#ifdef __cplusplus
}
#endif
