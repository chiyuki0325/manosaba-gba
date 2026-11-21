#pragma once
#include <cstdint>
namespace manosaba {
struct gallery_image {uint16_t asset,columns;const uint16_t* palette;const uint8_t* remap;};
struct gallery_group {uint8_t first,count;uint16_t number;};
inline constexpr int gallery_image_count=157,gallery_group_count=59,gallery_flag_bytes=(gallery_image_count+7)/8;
extern const uint16_t gallery_columns[][72];
extern const gallery_image gallery_images[];
extern const gallery_group gallery_groups[];
extern const uint8_t gallery_asset_order[];
}
