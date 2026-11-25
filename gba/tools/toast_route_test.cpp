// Targeted source-flow check; emit one real-input route ending at a progress toast.
#define MANOSABA_TRIAL_ROUTE_LIBRARY
#include "trial_route_plan.cpp"
#include "save_archive.h"
#include <cassert>
static int find_script(const char* path)
{
    for (int i = 0; i < story_script_count; ++i)
        if (!std::strcmp(story_scripts[i].path, path))
            return i;
    std::abort();
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    story_vm vm{};
    vm.start(find_script("Act01_Chapter01/Act01_Chapter01_Adv07"));
    for (int i = 0; i < 2000 && !vm.toast(); ++i)
        vm.advance();
    assert(vm.toast() == 1);
    vm.tick_toast(149);
    assert(vm.toast() == 1);
    vm.tick_toast(1);
    assert(!vm.toast());
    entry = find_script("Act02_Chapter01/Act02_Chapter01_Trial07");
    vm.start(entry);
    assert(search(vm));
    vm.start(entry);
    size_t count = 0;
    for (const auto& s : route)
    {
        vm.tick_toast(255);
        if (s.action == object_action)
            vm.object(s.arg);
        else if (s.action == choice_action)
            vm.choose(s.arg);
        else if (s.action == item_action)
            vm.choose_item(s.arg);
        else if (s.action == skip_action)
            vm.skip_debate();
        else
            vm.advance();
        ++count;
        if (vm.toast() == 2)
            break;
    }
    assert(vm.toast() == 2);
    uint8_t bytes[6144];
    save_archive writer(bytes, sizeof(bytes), false);
    vm.archive(writer);
    assert(writer.good());
    story_vm restored{};
    save_archive reader(bytes, writer.size(), true);
    restored.archive(reader);
    assert(reader.good() && !restored.toast());
    FILE* out = std::fopen(argv[1], "w");
    assert(out);
    std::fprintf(out, "%d %zu\n", entry - story_trial_first, count);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& s = route[i];
        std::fprintf(out, "%d %u %d %d %d %d\n", s.script, s.pc, s.state, s.action, s.arg, s.kind);
    }
    std::fprintf(out, "%d %u %d 0 0 0\n", vm.script(), vm.position(), int(vm.state()));
    std::fclose(out);
    std::printf("PASS: real ADV/Trial toast opcodes, 150-frame lifetime, transient state excluded from saves; native "
                "route=%zu steps\n",
                count);
}
