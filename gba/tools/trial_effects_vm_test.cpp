#include "story_format.h"
#include "story_layout.h"
#include "witch_book_data.h"
#include "music_command.h"
#define private public
#include "story_vm.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
int script(const char* suffix)
{
    for(int i=0;i<story_script_count;++i)if(std::strstr(story_scripts[i].path,suffix))return i;
    assert(false);return 0;
}
int main()
{
    int next=script("Act02_Chapter05_Trial23");story_vm vm{};vm.start(next);
    assert(vm._shake.left()>0 && !vm.court_fire());
    vm.start(script("Act02_Chapter05_Trial20"));assert(vm.court_fire());
    vm.start(script("Act02_Chapter05_Trial22"));assert(!vm.court_fire());
    // A live jump must retain both remaining time and phase, not reapply the seed.
    vm._shake.start({10800,1,1,6,0});vm.tick_shake(71);vm._court_fire=true;
    vm._screen.start(31,128,0,false);unsigned remaining=vm._shake.left();int x=vm.shake_x(),y=vm.shake_y();
    vm.jump(next,0);assert(vm._shake.left()==remaining && vm.shake_x()==x && vm.shake_y()==y);
    assert(vm.court_fire() && vm.screen_color().color==31 && vm.screen_color().amount==128);
    vm.advance();assert(vm._shake.left()==remaining && vm.court_fire());
    uint8_t bytes[6144];save_archive write(bytes,sizeof(bytes),false);vm.archive(write);assert(write.good());
    story_vm loaded{};save_archive read(bytes,write.size(),true);loaded.archive(read);
    assert(read.good() && loaded.court_fire() && loaded._shake.left()==remaining && loaded.screen_color().amount==128);
    vm.tick_shake(20000);assert(!vm._shake.left() && !vm.shake_x() && !vm.shake_y());
    vm.start(script("Act01_Chapter01_Trial00"));assert(!vm.court_fire() && !vm._shake.left() && !vm.screen_color().amount);
    std::puts("PASS: actual Trial20/22/23 entry seeds; live jump retains effect phase/time; archive and fresh entry");
}
