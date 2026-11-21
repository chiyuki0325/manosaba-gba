#ifndef BOOK_THEME_H
#define BOOK_THEME_H
#include <cstdint>
#include "story_vm.h"
namespace manosaba {
constexpr uint16_t book_color(int r,int g,int b){return uint16_t((r>>3)|((g>>3)<<5)|((b>>3)<<10));}
inline bool hiro_book_theme(const story_vm& vm){return story_scripts[vm.script()].path[4]=='2';}
inline void book_theme(uint16_t* palette,bool hiro,bool map=false)
{
    palette[3]=hiro ? book_color(235,78,85) : book_color(248,144,168);
    palette[4]=hiro ? book_color(112,40,48) : book_color(104,56,64);
    if(map)palette[14]=hiro ? book_color(176,80,88) : book_color(192,120,136);
}
}
#endif
