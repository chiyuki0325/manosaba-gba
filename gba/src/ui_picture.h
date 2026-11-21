#ifndef UI_PICTURE_H
#define UI_PICTURE_H
#include <cstdint>
namespace manosaba {
struct ui_picture_block {const uint8_t* data;uint16_t codec;};
struct ui_picture
{
    const uint16_t* palette;
    const uint32_t* raw;
    const ui_picture_block* blocks;
};
struct ui_pattern
{
    const uint16_t* palette;
    const uint32_t* tiles;
    const uint16_t* cells;
};
}
#endif
