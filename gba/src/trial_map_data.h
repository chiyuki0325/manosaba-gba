#ifndef MANOSABA_TRIAL_MAP_DATA_H
#define MANOSABA_TRIAL_MAP_DATA_H
#include <cstdint>
extern "C" {extern const uint8_t trial_map_pins[];}
namespace manosaba {
struct trial_map_pin {int16_t x,y;uint16_t alpha;};
struct trial_map_frame {int16_t left,top;uint16_t alpha;uint8_t floor;trial_map_pin pins[7];};
struct trial_map_segment {uint16_t first,count,ticks;uint8_t pins,icons[7];};
extern const uint16_t trial_map_palette[16];
extern const trial_map_frame trial_map_frames[];
extern const trial_map_segment trial_map_segments[];
}
#endif
