// Host test: run the ROM's exact decoder/interpreter against source-level traces.
#include "story_vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace manosaba;
static story_vm vm;
static uint32_t hash_value;
static void byte(unsigned value)
{
    hash_value = (hash_value ^ value) * 16777619u;
}
static void text(const char* value)
{
    do { byte(static_cast<unsigned char>(*value)); } while(*value++);
}
static void record()
{
    if(vm.history_count())
    {
        char history[story_text_capacity];
        vm.history_text(0,history); // Exercise reading an old cache block before playback resumes.
        if(!*history)std::abort();
    }
    auto state = vm.state();
    byte(state == story_state::text ? 1 : state == story_state::menu ? 2 : state == story_state::pause ? 3 : 4);
    byte(vm.script() & 255); byte(unsigned(vm.script()) >> 8);
    byte(vm.area_flags() & 255); byte(vm.area_flags() >> 8);
    text(state == story_state::menu ? "" : vm.text());
    if(state == story_state::text) text(vm.speaker());
    if(state == story_state::menu)
        for(int i = 0; i < vm.option_count(); ++i) { byte(vm.option_bad(i)); text(vm.option(i).text); }
}
static bool record_music(void* context,const music_command& command)
{
    if(vm.trial())return false; // Source traces stop at the normal-to-trial boundary.
    auto word=[](unsigned value){byte(value&255);byte(value>>8);};
    byte(6);word(unsigned(vm.script()));word(vm.area_flags());
    text(command.path ? command.path : "");text(command.intro ? command.intro : "");
    word(command.volume);word(command.duration_frames);word(command.fade_in_frames);word(command.parameters);
    byte(command.loop);byte(command.wait);byte(command.stop);
    ++*static_cast<int*>(context);
    return false;
}
int main(int argc, char** argv)
{
    if(argc != 2 && argc != 3) return 2;
    FILE* fixtures = std::fopen(argv[1], "r");
    if(! fixtures) return 2;
    int script, strategy, expected_count, total = 0;
    unsigned expected_hash;
    while(std::fscanf(fixtures, "%d %d %d %u", &script, &strategy, &expected_count, &expected_hash) == 4)
    {
        hash_value = 2166136261u;
        int count = 0;
        vm.bind_music(argc==3 ? record_music : nullptr,&count);
        vm.start(script);
        for(int step = 0; step < 150000; ++step)
        {
            if(vm.trial()){byte(5);text(story_scripts[vm.script()].path);++count;break;}
            if(vm.state() == story_state::running || vm.state() == story_state::stage) { vm.advance(); continue; }
            record();
            ++count;
            if(vm.state() == story_state::end)
            {
                if(vm.bad_end()!=bool(std::strstr(vm.text(),"坏结局 ")==vm.text() || std::strstr(story_scripts[vm.script()].path,"Common/CommonBad")))return 1;
                break;
            }
            if(vm.state() == story_state::menu) vm.choose(strategy ? vm.option_count()-1 : 0);
            else vm.advance();
        }
        if(count != expected_count || hash_value != expected_hash)
        {
            std::fprintf(stderr, "FAIL script %d (%s), strategy %d: %d/%d events, hash %u/%u\n",
                         script, story_scripts[script].path, strategy, count, expected_count, hash_value, expected_hash);
            return 1;
        }
        ++total;
    }
    std::fclose(fixtures);
    int normal=0;for(int i=0;i<story_script_count;++i)normal+=!story_scripts[i].mode;
    if(total != normal*2) return 1;
    std::printf("PASS: %d source/VM traces, all %d normal scripts through trial boundaries, first/last choices and map orders\n", total, normal);
}
