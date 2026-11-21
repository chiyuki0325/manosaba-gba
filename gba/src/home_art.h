#pragma once
#include "ui_picture.h"
namespace manosaba {
struct home_row_art {const uint8_t* pixels;uint16_t width,height;uint8_t x,y;};
extern const ui_picture home_titles[2],home_menus[2];
extern const home_row_art home_rows[2][5][2];
}
