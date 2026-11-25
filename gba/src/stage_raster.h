#ifndef STAGE_RASTER_H
#define STAGE_RASTER_H
#include "stage_player.h"
namespace manosaba
{
// CPU compositor, independent of GBA registers. The UI is composited afterwards.
void stage_render(const stage_player& player, uint8_t* pixels, int first_y = 0, int rows = 160);
uint8_t stage_blend(uint8_t bottom, uint8_t top, int alpha);
} // namespace manosaba
#endif
