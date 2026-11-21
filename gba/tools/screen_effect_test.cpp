#include "screen_effect.h"
#include "court_effect.h"
#include <cassert>
#include <cstdio>
using namespace manosaba;
int main()
{
    screen_effect effect;
    effect.start(0,0,0,false);
    effect.start(21,256,12,false);effect.tick(6);
    assert(effect.value().color==11 && effect.value().amount==128);
    effect.tick(6);assert(effect.value().color==21 && effect.value().amount==256);
    effect.start(0,0,12,false);effect.tick(50);assert(effect.value().amount==0);
    effect.start(uint16_t(17|23<<5|17<<10),171,24,false);effect.tick(7);
    uint8_t bytes[32];save_archive write(bytes,sizeof(bytes),false);effect.archive(write);
    screen_effect restored;save_archive read(bytes,write.size(),true);restored.archive(read);
    assert(read.good() && write.size()<=8 && restored.value().color==(17|23<<5|17<<10) && restored.value().amount==171);
    auto mixed=effect_over({32767,128},{21,128});
    assert(mixed.amount==192 && mixed.color==(24|10<<5|10<<10));
    auto unchanged=effect_over({1234,73},{0,0});assert(unchanged.color==1234 && unchanged.amount==73);
    effect.start(0,256,0,true);assert(effect.value().amount==256);
    court_shake shake;shake.start({10800,1,-1,6,0});
    bool horizontal=false,vertical=false;
    for(int i=0;i<60;++i){shake.tick(1);horizontal|=shake.x()!=0;vertical|=shake.y()!=0;}
    assert(horizontal && vertical && shake.left()==10740);
    uint8_t saved[32];save_archive sw(saved,sizeof(saved),false);shake.archive(sw);
    court_shake loaded;save_archive sr(saved,sw.size(),true);loaded.archive(sr);assert(sr.good() && loaded.left()==10740);
    loaded.tick(20000);assert(!loaded.left() && !loaded.x() && !loaded.y());
    shake.start({30,3,3,4,255});shake.tick(29);assert(!shake.x() && !shake.y());
    std::puts("PASS: timed RGBA fades, overrun, composition and destination-only save restore");
}
