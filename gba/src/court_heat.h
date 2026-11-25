#ifndef MANOSABA_COURT_HEAT_H
#define MANOSABA_COURT_HEAT_H
#include <cstdint>
namespace manosaba
{
constexpr uint16_t court_heat_color = 31 | (13 << 5) | (1 << 10);
constexpr int court_heat_amount = 72;
inline void court_heat_rows(uint16_t* rows, int scroll, unsigned phase)
{
    static constexpr int8_t wave[] = {0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
                                      2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  0,  0,
                                      0,  0,  0,  -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2,
                                      -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, -1, 0,  0};
    // HDMA seeds line0 from the final slot; HBlank0 then consumes slot0 for line1.
    for (unsigned i = 0; i < 159; ++i)
        rows[i] = uint16_t((scroll + wave[(i + 1 + phase) & 63]) & 511);
    rows[159] = uint16_t((scroll + wave[phase & 63]) & 511);
}
} // namespace manosaba
#endif
