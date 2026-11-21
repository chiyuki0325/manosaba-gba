#define MANOSABA_TRIAL_ROUTE_LIBRARY
#include "trial_route_plan.cpp"
#include "save_archive.h"
#include "stage_player.h"
#include <cassert>
#include <algorithm>
namespace manosaba {
extern const stage_key stage_keys[]={{}};
extern const stage_command stage_commands[]={{}};
}
static uint8_t encoded[6144],roundtrip[6144];
static unsigned maximum=0,samples=0;
static void apply(story_vm& vm,const step& s)
{
    if(s.action==object_action)vm.object(s.arg);else if(s.action==choice_action)vm.choose(s.arg);
    else if(s.action==item_action)vm.choose_item(s.arg);else if(s.action==skip_action)vm.skip_debate();else vm.advance();
}
static void check(story_vm& vm,const step& s)
{
    save_archive writer(encoded,sizeof(encoded),false);vm.archive(writer);assert(writer.good());
    maximum=std::max(maximum,unsigned(writer.size()));++samples;
    story_vm restored{};save_archive reader(encoded,writer.size(),true);restored.archive(reader);assert(reader.good() && reader.size()==writer.size());
    save_archive again(roundtrip,sizeof(roundtrip),false);restored.archive(again);
    assert(again.size()==writer.size() && !std::memcmp(encoded,roundtrip,writer.size()));
    // Exercise ROM decode/cache reload and cancellation after restoration.
    apply(vm,s);apply(restored,s);
    save_archive next(encoded,sizeof(encoded),false),other(roundtrip,sizeof(roundtrip),false);vm.archive(next);restored.archive(other);
    if(!(next.good() && other.good() && next.size()==other.size() && !std::memcmp(encoded,roundtrip,next.size())))
    {std::fprintf(stderr,"snapshot mismatch input=%d:%u state=%d, after=%d:%u/%d:%u states=%d/%d duration=%u/%u return=%d/%d\n",s.script,s.pc,s.state,vm.script(),vm.position(),restored.script(),restored.position(),int(vm.state()),int(restored.state()),vm._camera_duration,restored._camera_duration,vm._return_valid,restored._return_valid);std::abort();}
    if(vm.history_count())
    {
        char a[story_text_capacity],b[story_text_capacity];vm.history_text(vm.history_count()-1,a);restored.history_text(restored.history_count()-1,b);assert(!std::strcmp(a,b));
    }
}
int main()
{
    unsigned adv_samples=0;
    for(int script=0;script<story_selectable_count;++script)
    {
        story_vm vm{};vm.start(script);std::unordered_set<std::string> visited;
        for(int guard=0;guard<12000 && vm.script()==script && vm.state()!=story_state::end && visited.insert(key(vm)).second;++guard)
        {
            step next{};next.script=vm.script();next.pc=vm.position();next.state=int(vm.state());next.action=advance_action;
            if(vm.state()==story_state::menu)
            {next.action=choice_action;for(int i=0;i<vm.option_count();++i)if(!vm.option_bad(i)){next.arg=i;break;}}
            else if(vm.state()==story_state::inventory)next.action=item_action;
            check(vm,next);++adv_samples;
        }
    }
    chapter_scope=true;
    for(entry=story_trial_first;entry<story_trial_first+story_trial_count;++entry)
    {
        std::string path=story_scripts[entry].path;if(path.substr(path.size()-8)!="_Trial00")continue;
        chapter_prefix=path.substr(0,path.rfind("_Trial"))+"_Trial";route.clear();seen.clear();nodes=0;
        story_vm vm{};vm.start(entry);assert(search(vm));vm.start(entry);
        for(const auto& s:route)
        {
            for(int guard=0;guard<5000 && (vm.script()!=s.script || vm.position()!=s.pc || int(vm.state())!=s.state);++guard)
            {if(vm.state()==story_state::camera)vm.tick_camera(vm.camera_wait());vm.advance();}
            assert(vm.script()==s.script && vm.position()==s.pc && int(vm.state())==s.state);
            check(vm,s);
        }
    }
    stage_key keys[]={{200,90,stage_ease::smooth,0},{-100,50,stage_ease::linear,1}};
    stage_player a(keys,stage_commands),b(keys,stage_commands);a.reset(keys,stage_commands);b.reset(keys,stage_commands);
    stage_command command{0,0,2,stage_kind::tween,0,0,129};a.apply(command);a.tick(31);
    static_assert(sizeof(stage_entity)==60,"CG backend selection uses existing entity padding");
    save_archive baseline(encoded,sizeof(encoded),false);a.archive(baseline);int baseline_size=baseline.size();
    a.apply(stage_command{1,0,0,stage_kind::cg_backend,3,0,0});
    a.apply(stage_command{0,0,0,stage_kind::maskable,3,0,0});
    save_archive write(encoded,sizeof(encoded),false);a.archive(write);assert(write.good());
    assert(write.size()==baseline_size);
    save_archive read(encoded,write.size(),true);b.archive(read);assert(read.good());
    assert(b.entity(3).regular_cg && !b.entity(3).maskable);
    for(int frame=0;frame<500 && a.animating();++frame)a.tick();
    assert(!b.animating() && !b.blocked());
    for(int property=0;property<int(stage_property::count);++property)assert(a.entity(0).values[property]==b.entity(0).values[property]);
    std::printf("PASS: %u ADV + %u trial checkpoints round-trip and advance identically; largest VM=%uB; stage animation restored at its final pose\n",adv_samples,samples-adv_samples,maximum);
}
