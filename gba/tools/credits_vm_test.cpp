#include "story_vm.h"
#include "save_archive.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
static int calls;
static bool music(void*, const music_command&)
{
    ++calls;
    return false;
}
static story_vm vm, restored;
static uint8_t data[6144];
int main()
{
    for (int act = 1; act <= 2; ++act)
    {
        const char* suffix = act == 1 ? "Act01_Chapter05_Adv10" : "Act02_Chapter05_Adv08";
        int index = -1;
        for (int i = 0; i < story_script_count; ++i)
            if (std::strstr(story_scripts[i].path, suffix))
                index = i;
        assert(index >= 0);
        vm.bind_music(music, nullptr);
        vm.start(index);
        for (int n = 0; n < 3000 && vm.state() != story_state::credits; ++n)
        {
            assert(vm.state() != story_state::end);
            vm.advance();
        }
        assert(vm.state() == story_state::credits && vm.credits_act() == act && !*vm.text() && vm.visual_frame() == -1);
        if (act == 2)
        {
            int miria = -1;
            for (int i = 0; i < book_entry_count; ++i)
                if (book_entries[i].kind == 1 && !std::strcmp(book_entries[i].id, "Miria"))
                    miria = i;
            assert(miria >= 0 && vm.book_version(miria) == 11);
        }
        save_archive writer(data, sizeof(data), false);
        vm.archive(writer);
        assert(writer.good());
        save_archive reader(data, writer.size(), true);
        restored.archive(reader);
        assert(reader.good() && reader.size() == writer.size());
        assert(restored.state() == story_state::credits && restored.credits_act() == act);
        int before = calls;
        vm.advance();
        restored.advance();
        assert(calls > before); // EndCredits executes its original StopBgm; entry itself has no music opcode.
        assert(vm.script() == restored.script() && vm.position() == restored.position() &&
               vm.state() == restored.state());
        for (int n = 0; n < 100 && vm.script() == index; ++n)
            vm.advance();
        assert(vm.script() == index + 1);
    }
    std::puts("PASS: both credits states, Miria v11 retained, act-only save/load, EndCredits audio and original "
              "continuations");
}
