// Host-only budget check using the actual VM and generated stage at every ADV choice.
#include "story_vm.h"
#include "stage_player.h"
#include "graphics_data.h"
#include "save_archive.h"
#include "save_store.h"
#include <cassert>
#include <cstdio>
#include <set>

namespace manosaba {
extern const stage_key stage_keys[]={{}};
extern const stage_command stage_commands[]={{}};
// Descriptors refer to this symbol; this budget check never reads image pixels.
extern const uint8_t graphics_bytes[10000000]={};
}

int main()
{
    using namespace manosaba;
    unsigned count=0,worst=0;
    uint8_t bytes[8192];
    for(int script=0;script<story_selectable_count;++script)
        for(int strategy=0;strategy<2;++strategy)
        {
            story_vm vm;vm.start(script);
            stage_player stage;
            int bank=-1;
            std::set<unsigned> seen;
            for(int step=0;step<10000 && vm.script()==script && vm.state()!=story_state::end;++step)
            {
                if(!seen.insert(vm.position()).second)break;
                int next=vm.graphical_bank();
                if(next!=bank)
                {
                    bank=next;
                    stage.reset(bank<0 ? stage_keys : graphics_banks[bank].keys,
                                bank<0 ? stage_commands : graphics_banks[bank].commands);
                }
                if(vm.state()==story_state::stage && bank>=0)
                {
                    stage.apply(graphics_banks[bank].events[vm.stage_event()]);
                    for(int frame=0;frame<60 && stage.blocked();++frame)stage.tick(600,true);
                    stage.tick(600,true);
                }
                if(vm.state()==story_state::menu && !vm.map_menu())
                {
                    save_archive data(bytes,sizeof(bytes),false);
                    unsigned kind=2;data.field(kind);vm.archive(data);
                    int current_bank=bank,frame=vm.visual_frame();bool double_buffer=true;
                    data.field(current_bank);data.field(frame);data.field(double_buffer);stage.archive(data);
                    // Reserve additional bytes for the actual music/SFX checkpoint.
                    unsigned size=unsigned(data.size())+96;
                    assert(data.good() && size<=save_seed_limit);
                    ++count;
                    if(size>worst)
                    {
                        worst=size;
                        std::printf("max=%u script=%s pc=%u history=%d\n",size,
                                    story_scripts[script].path,vm.position(),vm.history_count());
                    }
                    vm.choose(strategy ? vm.option_count()-1 : 0);
                }
                else if(vm.state()==story_state::inventory)vm.choose_item(0);
                else vm.advance();
            }
        }
    assert(count);
    std::printf("PASS: %u story choice routes fit retry seed; largest=%u/%d B including 96 B audio allowance\n",
                count,worst,save_seed_limit);
}
