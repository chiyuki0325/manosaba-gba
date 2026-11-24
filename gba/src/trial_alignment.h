#ifndef TRIAL_ALIGNMENT_H
#define TRIAL_ALIGNMENT_H
#include <cstdint>
namespace manosaba
{
struct trial_alignment_entry
{
    uint16_t image,scale;
    int16_t limit;
    uint16_t offset;
};
extern const uint8_t trial_alignment_offsets[];
extern const trial_alignment_entry trial_alignment_entries[];
extern const int trial_alignment_count;
}
#endif
