#ifndef GRAPHICS_DATA_H
#define GRAPHICS_DATA_H
#include "stage_format.h"
namespace manosaba
{
enum class graphic_kind : uint8_t { obj, bg, solid, rectangle, mask, animation };
struct graphic_part
{
    const uint32_t* tiles;
    int16_t x,y;
    uint8_t width,height,bank;
    uint8_t silhouette_group=0; // Equal binary alpha + shape; uses tail padding.
};
struct graphic_variant
{
    const uint32_t* tiles;
    // Mask with null map: tile 0 transparent, tiles 1..30 repeat by column.
    const uint8_t* map;
    uint16_t count,scale;
};
struct graphic_asset
{
    const graphic_part* parts;
    const graphic_variant* variants;
    const uint16_t* palette;
    const uint16_t* frames;
    uint16_t width,height,count,frame_count,base_count;
    int16_t pivot_x,pivot_y;
    graphic_kind kind;
    uint8_t fps;
    uint16_t color;
    uint16_t nominal_scale=256; // Uses the existing two-byte tail padding.
};
struct graphics_bank
{
    const char* path;
    const stage_command* commands;
    const stage_key* keys;
    const stage_event* events;
    const int16_t* masks;
    uint16_t event_count;
};
struct graphic_cache_entry
{
    uint16_t base,image,scale;
    int16_t angle;
    uint8_t phase_x,phase_y;
};
// Optional clear-CG view; its tiles are the sharp affine variant's shared dictionary.
struct graphic_regular
{
    const uint16_t* map;
    uint16_t image,count;
    uint8_t columns,origin;
};
extern const graphic_regular graphic_regular_images[];
extern const int graphic_regular_count;
extern const graphic_cache_entry graphic_cache_entries[];
extern const int graphic_cache_entry_count;
extern const uint8_t graphics_bytes[];
extern const graphic_asset graphic_assets[];
extern const graphics_bank graphics_banks[];
extern const int graphics_bank_count,graphic_asset_count;
}
#endif
