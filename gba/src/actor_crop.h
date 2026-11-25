#ifndef ACTOR_CROP_H
#define ACTOR_CROP_H
#include <cstdint>
namespace manosaba
{
// Source-canvas cutoff: pixels at or below bottom[x] are hidden by the podium.
struct actor_crop
{
    const int16_t* bottom;
    int width;
    int limit = 32767;
    int at(int x) const
    {
        return bottom ? bottom[x] : limit;
    }
    unsigned mask(int x, int y) const
    {
        unsigned result = 0;
        for (int i = 0; i < 8 && x + i < width; ++i)
            if (y < at(x + i))
                result |= 15u << (i * 4);
        return result;
    }
};
struct trial_actor_piece
{
    uint8_t part, x, y, width, height;
};
struct trial_actor_placement
{
    const int16_t* bottom;
    const trial_actor_piece* pieces;
    int16_t x, y;
    uint16_t scale, count;
    int16_t limit = 32767;
    uint16_t alignment = 0;
    int cutoff(int column) const
    {
        return bottom ? bottom[column] : limit;
    }
};
} // namespace manosaba
#endif
