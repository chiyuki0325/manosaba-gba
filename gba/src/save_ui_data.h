#ifndef MANOSABA_SAVE_UI_DATA_H
#define MANOSABA_SAVE_UI_DATA_H
#include "ui_picture.h"
namespace manosaba
{
struct save_key_art
{
    const uint8_t* pixels;
    uint8_t width, height;
};
struct save_thumb_art
{
    const uint8_t* pixels;
    const uint16_t* palette;
};
struct save_preview_colors
{
    uint16_t image;
    const uint8_t* map;
    const uint16_t* palette;
};
extern const uint8_t save_ui_bytes[];
extern const uint16_t save_ui_palette[256], save_ui_initials[4][16];
extern const uint32_t* const save_ui_backgrounds[2];
extern const save_key_art save_ui_keys[4];
extern const save_thumb_art save_court_thumbs[2];
extern const save_preview_colors save_background_colors[];
extern const int save_background_count;
extern const uint32_t save_content_signature;
} // namespace manosaba
#endif
