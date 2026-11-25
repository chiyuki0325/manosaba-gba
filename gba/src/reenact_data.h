#ifndef MANOSABA_REENACT_DATA_H
#define MANOSABA_REENACT_DATA_H
#include <cstdint>
namespace manosaba
{
struct reenact_part
{
    const uint16_t* tiles;
    uint8_t x, y, width, height, group;
};
struct reenact_pose
{
    const reenact_part* parts;
    uint16_t count;
    const uint16_t* background;
};
struct reenact_move
{
    int8_t x, y;
};
struct reenact_asset
{
    const reenact_pose* poses;
    const reenact_move* moves;
    uint16_t duration, pose_count, move_count;
    uint8_t moving, loop;
};
extern const uint8_t reenact_bytes[];
extern const uint8_t* const reenact_tiles;
extern const reenact_asset reenact_assets[];
extern const uint16_t reenact_palette[16], reenact_curtain_palette[16];
extern const uint16_t* const reenact_curtain;
} // namespace manosaba
#endif
