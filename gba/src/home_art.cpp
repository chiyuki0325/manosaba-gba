#include "home_art.h"
namespace manosaba {
extern const uint8_t home_bytes[];
static const ui_picture_block blocks_0[]={{home_bytes+512,1},{home_bytes+2620,1},{home_bytes+5792,1},{home_bytes+9244,1},{home_bytes+12980,1},{home_bytes+16768,1},{home_bytes+20132,1},{home_bytes+23308,1},{home_bytes+26180,1},{home_bytes+29568,1}};
static const ui_picture_block blocks_1[]={{home_bytes+30856,1},{home_bytes+31800,1},{home_bytes+33528,1},{home_bytes+36280,1},{home_bytes+39972,1},{home_bytes+43512,1},{home_bytes+46692,1},{home_bytes+49764,1},{home_bytes+53556,1},{home_bytes+57180,1}};
static const ui_picture_block blocks_2[]={{home_bytes+58328,1},{home_bytes+60592,1},{home_bytes+62968,1},{home_bytes+65256,1},{home_bytes+67260,1},{home_bytes+69760,1},{home_bytes+71776,1},{home_bytes+74264,1},{home_bytes+75816,1},{home_bytes+78428,1}};
static const ui_picture_block blocks_3[]={{home_bytes+79116,1},{home_bytes+80840,1},{home_bytes+82964,1},{home_bytes+85336,1},{home_bytes+87796,1},{home_bytes+90156,1},{home_bytes+92484,1},{home_bytes+95124,1},{home_bytes+96128,1},{home_bytes+97440,1}};
const ui_picture home_titles[2]={{reinterpret_cast<const uint16_t*>(home_bytes),nullptr,blocks_0},{reinterpret_cast<const uint16_t*>(home_bytes),nullptr,blocks_1}};
const ui_picture home_menus[2]={{reinterpret_cast<const uint16_t*>(home_bytes),nullptr,blocks_2},{reinterpret_cast<const uint16_t*>(home_bytes),nullptr,blocks_3}};
const home_row_art home_rows[2][5][2]={{{{home_bytes+98128,168,28,36,4},{home_bytes+102832,168,28,36,4}},{{home_bytes+107536,168,28,36,35},{home_bytes+112240,168,28,36,35}},{{home_bytes+116944,168,28,36,66},{home_bytes+121648,168,28,36,66}},{{home_bytes+126352,168,28,36,97},{home_bytes+131056,168,28,36,97}},{{home_bytes+135760,168,28,36,128},{home_bytes+140464,168,28,36,128}}},{{{home_bytes+145168,208,24,16,24},{home_bytes+150160,208,24,16,24}},{{home_bytes+155152,208,24,16,49},{home_bytes+160144,208,24,16,49}},{{home_bytes+165136,208,24,16,74},{home_bytes+170128,208,24,16,74}},{{home_bytes+175120,208,24,16,99},{home_bytes+180112,208,24,16,99}},{{home_bytes+185104,208,24,16,124},{home_bytes+190096,208,24,16,124}}}};
}
