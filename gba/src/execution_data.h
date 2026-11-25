#ifndef MANOSABA_EXECUTION_DATA_H
#define MANOSABA_EXECUTION_DATA_H
#include <cstdint>
namespace manosaba
{
constexpr int execution_steps = 40;
struct execution_change
{
    uint16_t word;
    uint32_t before, after;
};
struct execution_patch
{
    uint16_t first, count;
};
extern const uint16_t execution_colors[16];
extern const uint32_t execution_base[];
extern const uint32_t execution_buttons[2][392];
extern const execution_patch execution_patches[2][execution_steps];
extern const execution_change execution_changes[];
extern const uint32_t execution_check[392];
} // namespace manosaba
#endif
