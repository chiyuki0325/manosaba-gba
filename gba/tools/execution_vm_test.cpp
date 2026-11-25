#include "story_vm.h"
#include "save_archive.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
static story_vm vm, restored;
static uint8_t snapshot[6144];
int main()
{
    const char* scripts[] = {"Act01_Chapter01_Adv35", "Act01_Chapter02_Adv27", "Act01_Chapter03_Adv27",
                             "Act01_Chapter04_Adv13", "Act02_Chapter01_Adv26", "Act02_Chapter02_Adv19",
                             "Act02_Chapter03_Adv14", "Act02_Chapter04_Adv14"};
    for (const char* suffix : scripts)
    {
        int index = -1;
        for (int i = 0; i < story_script_count; ++i)
            if (std::strstr(story_scripts[i].path, suffix))
                index = i;
        assert(index >= 0);
        vm.start(index);
        for (int guard = 0; guard < 4000 && vm.state() != story_state::execution; ++guard)
        {
            assert(vm.state() != story_state::end);
            vm.advance();
        }
        assert(vm.state() == story_state::execution && !*vm.text());
        save_archive writer(snapshot, sizeof(snapshot), false);
        vm.archive(writer);
        assert(writer.good());
        save_archive reader(snapshot, writer.size(), true);
        restored.archive(reader);
        assert(reader.good() && reader.size() == writer.size());
        assert(restored.state() == story_state::execution);
        vm.advance();
        restored.advance();
        assert(vm.position() == restored.position() && vm.state() == restored.state());
        assert(!std::strcmp(vm.music(), "Bgm_31_1"));
    }
    std::puts("PASS: all eight execution opcodes, no placeholder Stop, logical save roundtrip and original "
              "confirmation music");
}
