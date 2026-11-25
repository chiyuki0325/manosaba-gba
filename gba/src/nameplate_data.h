#ifndef NAMEPLATE_DATA_H
#define NAMEPLATE_DATA_H
#include <cstdint>
#include "label_art.h"
namespace manosaba
{
struct nameplate
{
    const char* speaker;
    const label_art* tiles;
    uint16_t color;
};
extern const nameplate nameplates[];
extern const int nameplate_count;
extern const uint32_t nameplate_diamond[384];
extern const uint16_t nameplate_diamond_colors[4];
} // namespace manosaba
#endif
