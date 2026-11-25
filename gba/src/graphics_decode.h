#pragma once
#include "graphics_data.h"
namespace manosaba
{
// Shared scratch: story scenery and the gallery never own the display together.
extern uint8_t graphic_map_cells[4096];
const graphic_regular* regular_graphic(int image);
uint8_t affine_cell(const graphic_asset& asset, const graphic_variant& variant, int x, int y);
void expand_affine_map(const graphic_asset& asset, const graphic_variant& variant, uint8_t* output);
void regular_window(const graphic_regular& image, int x, int y, uint16_t* output);
} // namespace manosaba
