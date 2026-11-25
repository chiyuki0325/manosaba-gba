#include "story_vm.h"
#include "save_archive.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace manosaba;
int script(const char* suffix)
{
    for (int i = 0; i < story_script_count; ++i)
        if (std::strstr(story_scripts[i].path, suffix))
            return i;
    assert(false);
    return 0;
}
void until_point(story_vm& vm)
{
    for (int i = 0; i < 300; ++i)
    {
        if (vm.state() == story_state::debate && vm.link_count())
            return;
        if (vm.state() == story_state::debate)
            vm.skip_debate();
        else
            vm.advance();
    }
    assert(false);
}
void wrong_objection(story_vm& vm)
{
    until_point(vm);
    vm.object(0);
    assert(vm.state() == story_state::menu);
    vm.choose(0);
    for (int i = 0; i < 300 && vm.trial() && vm.state() != story_state::debate; ++i)
        vm.advance();
}
int main()
{
    for (int i = 0; i < story_script_count; ++i)
        assert((story_scripts[i].mode == 1) == (i >= story_trial_first && i < story_trial_first + story_trial_count));
    const char* entries[][2] = {
        {"Act01_Chapter01_Adv34", "Act01_Chapter01_Trial00"}, {"Act01_Chapter02_Adv26", "Act01_Chapter02_Trial00"},
        {"Act01_Chapter03_Adv26", "Act01_Chapter03_Trial00"}, {"Act01_Chapter04_Adv12", "Act01_Chapter04_Trial00"},
        {"Act01_Chapter05_Adv05", "Act01_Chapter05_Trial00"}, {"Act02_Chapter01_Adv25", "Act02_Chapter01_Trial00"},
        {"Act02_Chapter02_Adv18", "Act02_Chapter02_Trial00"}, {"Act02_Chapter03_Adv13", "Act02_Chapter03_Trial00"},
        {"Act02_Chapter04_Adv13", "Act02_Chapter04_Trial00"}, {"Act02_Chapter05_Adv04", "Act02_Chapter05_Trial00"},
        {"Act02_Chapter05_Adv05", "Act02_Chapter06_Trial00"}};
    int layout = 0;
    for (const auto& entry : entries)
    {
        story_vm bridge;
        bridge.start(script(entry[0]));
        for (int step = 0; step < 500 && !bridge.trial(); ++step)
        {
            assert(bridge.state() != story_state::end);
            if (bridge.state() == story_state::menu)
                bridge.choose(0);
            else
                bridge.advance();
        }
        assert(bridge.trial() && bridge.script() == script(entry[1]) && bridge.hp() == 10);
        assert(bridge.trial_layout() == layout++);
    }
    story_vm vm;
    vm.start(script("Act01_Chapter01_Trial00"));
    assert(vm.state() == story_state::camera && vm.camera_wait() == 36);
    vm.tick_camera(36);
    assert(!vm.camera_wait());
    vm.advance();
    assert(vm.state() == story_state::text && vm.trial_actor() == 7);
    assert(vm.trial_camera() == 0 && vm.camera_duration() == 1500 && vm.camera_frame() == 36);
    vm.tick_camera(60);
    assert(vm.camera_frame() == 96);
    vm.advance();
    assert(vm.state() == story_state::text && vm.trial_actor() == 2);
    assert(vm.trial_camera() == 0 && vm.camera_frame() == 96); // Changing speakers keeps the ongoing sweep.
    vm.tick_camera(2000);
    assert(vm.camera_frame() == 1500);
    for (int i = 0; i < 7; ++i)
        vm.advance();
    assert(vm.trial_camera() == 1 && vm.camera_duration() == 36 && vm.camera_frame() == 0);
    story_vm intro;
    intro.start(script("Act02_Chapter03_Trial13"));
    assert(intro.state() == story_state::camera && intro.camera_duration() == 60 && intro.camera_wait() == 90);
    intro.tick_camera(60);
    assert(intro.camera_frame() == 60 && intro.camera_wait() == 30);
    intro.tick_camera(30);
    assert(intro.camera_frame() == 60 && !intro.camera_wait());
    intro.advance();
    assert(intro.state() == story_state::camera && intro.camera_duration() == 60 && intro.camera_frame() == 0);
    int first = script("Act01_Chapter01_Trial01");
    vm.start(first);
    assert(vm.state() == story_state::title && vm.cutin_kind() == 0);
    vm.advance();
    assert(vm.trial() && vm.state() == story_state::debate && vm.statement_duration() > 60);
    assert(vm.debate_layout().left >= 100); // Original first statement is anchored at 70%.
    unsigned total = vm.trial_left(), duration = vm.statement_duration();
    assert(!vm.tick_debate(60));
    assert(vm.statement_left() == duration - 60 && vm.trial_left() == total - 60);
    vm.skip_debate();
    assert(vm.trial_left() == total - duration && vm.state() == story_state::debate);
    assert(vm.debate_layout().left < 72); // The next speaker's statement moves to 30%.
    until_point(vm);
    unsigned left = vm.statement_left();
    total = vm.trial_left();
    char text[384];
    std::strcpy(text, vm.text());
    auto box = vm.debate_layout();
    uint16_t poses[17];
    std::memcpy(poses, vm.trial_poses(), sizeof(poses));
    int before_layout = vm.trial_layout();
    vm.object(0);
    assert(vm.state() == story_state::menu && vm.objection_menu());
    assert(vm.trial_left() == total);
    int cancel = -1;
    for (int i = 0; i < vm.option_count(); ++i)
        if (vm.option(i).value & 128)
            cancel = i;
    assert(cancel >= 0);
    vm.choose(cancel);
    assert(vm.state() == story_state::debate && vm.restored_debate());
    int history_count = vm.history_count();
    char history[story_text_capacity];
    vm.history_text(history_count - 1, history);
    assert(vm.history_count() == history_count);
    assert(!std::strstr(history, "\ue000") && !std::strstr(history, "\ue001") && !std::strstr(history, "\ue002"));
    assert(vm.statement_left() == left && vm.trial_left() == total && !std::strcmp(vm.text(), text));
    assert(!std::memcmp(&box, &vm.debate_layout(), sizeof(box)));
    assert(before_layout == vm.trial_layout() && !std::memcmp(poses, vm.trial_poses(), sizeof(poses)));
    story_vm correct;
    correct.start(first);
    until_point(correct);
    correct.skip_debate();
    assert(correct.state() == story_state::debate);
    correct.object(0);
    correct.choose(0);
    assert(correct.state() == story_state::cutin && correct.cutin_kind() == 0 && correct.cutin_variant() == 1);
    correct.advance();
    assert(correct.state() == story_state::text);
    for (int n = 0; n < 12; ++n)
        wrong_objection(vm);
    assert(vm.hp() == 10 && vm.script() == first && vm.state() == story_state::debate && !vm.bad_end());
    vm.start(first);
    until_point(vm);
    int failed_script = vm.script();
    for (int i = 0; i < 3000 && !(vm.state() == story_state::title && vm.cutin_kind() == 2); ++i)
    {
        if (vm.state() == story_state::debate)
            vm.skip_debate();
        else
            vm.advance();
    }
    assert(vm.state() == story_state::title && vm.cutin_kind() == 2 && vm.script() == failed_script &&
           !vm.trial_left());
    vm.advance();
    assert(vm.script() == story_common_bad_scripts[0] && vm.trial_left() == 0 && vm.hp() == 10 && !vm.bad_end());
    for (int id = 0; id < 13; ++id)
    {
        vm.start(story_common_bad_scripts[id]);
        unsigned spoken = 0;
        for (int i = 0; i < 600 && !vm.bad_end(); ++i)
        {
            spoken += vm.state() == story_state::text;
            vm.advance();
        }
        assert(spoken > 0 && vm.bad_end());
    }
    vm.start(script("Act02_Chapter02_Trial00"));
    assert(vm.bad_id() == 7);
    vm.start(script("Act02_Chapter03_Trial00"));
    assert(vm.bad_id() == 7);
    vm.start(script("Act02_Chapter03_Trial04"));
    assert(vm.bad_id() == 8);
    vm.start(first);
    assert(!vm.bad_end());
    story_vm held;
    held.start(script("Act02_Chapter06_Trial09"));
    for (int i = 0; i < 1500 && !(held.state() == story_state::debate && held.debate_stopped()); ++i)
    {
        if (held.state() == story_state::debate)
            held.skip_debate();
        else
            held.advance();
    }
    assert(held.state() == story_state::debate && held.debate_stopped() && held.link_count() == 1);
    auto held_pc = held.position();
    unsigned tail = held.statement_left(), clock = held.trial_left();
    assert(!held.tick_debate(int(tail)) && !held.statement_left());
    assert(!held.tick_debate(120) && held.position() == held_pc && held.trial_left() == clock - tail - 120);
    held.skip_debate();
    assert(held.position() == held_pc && held.trial_left() == clock - tail - 120);
    uint8_t snapshot[4096];
    save_archive writer(snapshot, sizeof(snapshot), false);
    held.archive(writer);
    assert(writer.good());
    story_vm loaded;
    save_archive reader(snapshot, writer.size(), true);
    loaded.archive(reader);
    assert(reader.good());
    assert(loaded.state() == story_state::debate && loaded.debate_stopped() && !loaded.statement_left() &&
           loaded.trial_left() == held.trial_left());
    held.object(0);
    assert(held.state() == story_state::menu);
    held.choose(0);
    assert(held.state() != story_state::pause);
    std::puts("PASS: timed trials, cancellation, wrong objections retain HP and return to debate; expiry enters "
              "original dialogue, all 13 common endings, Hiro phase inheritance and final BAD END");
}
