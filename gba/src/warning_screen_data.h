#pragma once
#include <stdint.h>
namespace manosaba {
struct warning_line {const char* text;uint8_t x,y;};
extern const warning_line warning_lines[8];
extern const uint16_t warning_header[7][16];
}
