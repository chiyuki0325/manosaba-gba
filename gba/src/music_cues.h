#ifndef MANOSABA_MUSIC_CUES_H
#define MANOSABA_MUSIC_CUES_H
#include <stdint.h>

namespace manosaba
{
struct music_cue
{
    uint32_t key;
    const char* alias;
    const char* title;
    uint16_t volume;
    uint8_t module;
    uint8_t intro_order;
    uint8_t loop_order;
    uint8_t story_order;
};
extern const music_cue music_cues[];
extern const int music_cue_count;
extern const int music_hiro_index;
const music_cue* find_music_cue(unsigned key);
int music_intro_order(unsigned key,unsigned intro);
}
#endif
