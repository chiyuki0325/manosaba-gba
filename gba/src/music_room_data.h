#pragma once
#include <cstdint>
#include "ui_picture.h"
namespace manosaba {
inline constexpr int music_flag_bytes=8;
struct music_room_track {const char* title;uint8_t catalog,bit;};
extern const music_room_track music_room_tracks[];
extern const int music_room_count;
extern const ui_pattern music_room_background;
int music_collection_bit(uint32_t key);
}
