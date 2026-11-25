#ifndef MANOSABA_TRIAL_TITLE_DATA_H
#define MANOSABA_TRIAL_TITLE_DATA_H
#include <stdint.h>
namespace manosaba
{
struct trial_title_frame
{
    const uint16_t* changes;
    uint16_t count;
};
struct trial_title_sequence
{
    const trial_title_frame* frames;
    const uint16_t* const* snapshots;
    uint16_t count;
};
extern const uint8_t trial_title_bytes[];
extern const uint16_t trial_title_palette[16];
extern const uint16_t trial_timeup_palette[16];
extern const trial_title_sequence trial_titles[3];
} // namespace manosaba
#endif
