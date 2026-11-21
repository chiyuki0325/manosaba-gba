#ifndef LABEL_ART_H
#define LABEL_ART_H
#include <cstdint>
namespace manosaba {
struct label_art
{
    const uint32_t* pixels;
    const uint8_t* positions;
    uint8_t count;
};
}
#endif
