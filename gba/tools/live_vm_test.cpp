#include "story_vm.h"
#include "save_archive.h"
#include <cassert>
#include <cstdio>
int main()
{
    using namespace manosaba;
    story_vm vm;vm.start(1);assert(!vm.live());
    for(int i=0;i<1000 && !vm.live();++i)vm.advance();
    assert(vm.live());uint8_t bytes[6144];save_archive writer(bytes,sizeof(bytes),false);vm.archive(writer);assert(writer.good());
    story_vm restored;save_archive reader(bytes,writer.size(),true);restored.archive(reader);assert(reader.good() && restored.live());
    for(int i=0;i<100 && vm.live();++i)vm.advance();
    assert(!vm.live() && vm.script()==1);
    std::puts("PASS: real ADV02 Stream entry, save/load and Default exit");
}
