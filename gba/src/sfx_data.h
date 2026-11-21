#ifndef MANOSABA_SFX_DATA_H
#define MANOSABA_SFX_DATA_H
#include <stdint.h>
namespace manosaba {
struct sfx_note {uint16_t frame,sample,rate,volume,hold;};
struct sfx_cutin_event {uint16_t frame,cue,volume;};
struct sfx_cue {
    const char* alias;const char* label;
    int16_t sample;uint16_t first,count,duration;
    uint8_t ambient,envelope,native_loop;
};
extern const sfx_note sfx_notes[];
extern const sfx_cue sfx_cues[];
extern const int sfx_cue_count;
extern const sfx_cutin_event sfx_cutin_events[];
extern const int sfx_cutin_event_count;
}
#endif
