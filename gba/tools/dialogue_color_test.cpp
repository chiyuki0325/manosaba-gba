// Validate the generated sidecar against the production decoder and save format.
#include "story_format.h"
#include "story_layout.h"
#include "witch_book_data.h"
#include "music_command.h"
#define private public
#include "story_vm.h"
#undef private
#include "save_archive.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
int main()
{
    static_assert(sizeof(story_history_entry)==4);
    unsigned span_count=0;
    for(int n=0;n<story_text_style_count;++n)
    {
        const auto& s=story_text_styles[n];story_vm vm;vm.start(s.script);
        vm._pc=s.pc-4;
        bool say=vm.byte()==uint8_t(story_op::say);
        vm.jump(s.script,s.pc-(say ? 4 : 10));vm.advance();
        assert(vm.position()==s.end && vm.text_style()==&s);
        assert(vm.state()==(say ? story_state::text : story_state::debate));
        int index=vm.history_count()-1;
        assert(vm.history(index).pc==s.pc && vm.history_style(index)==&s);
        char history[story_text_capacity];vm.history_text(index,history);
        for(int i=0;i<s.count;++i)
        {
            const auto& r=story_color_spans[s.first+i];assert(r.first<r.last && r.last<=std::strlen(vm.text()));
            assert(r.history_first<r.history_last && r.history_last<=std::strlen(history));
            assert((uint8_t(vm.text()[r.first])&192)!=128 && (uint8_t(vm.text()[r.last])&192)!=128);
            assert(r.last-r.first==r.history_last-r.history_first);
            for(unsigned j=0;j<r.last-r.first;++j)assert((vm.text()[r.first+j]=='\r' ? '\n' : vm.text()[r.first+j])==history[r.history_first+j]);
            ++span_count;
        }
        uint8_t bytes[6144];save_archive writer(bytes,sizeof(bytes),false);vm.archive(writer);assert(writer.good());
        story_vm restored;save_archive reader(bytes,writer.size(),true);restored.archive(reader);assert(reader.good());
        assert(restored.text_style()==&s && restored.history_style(index)==&s && !std::strcmp(vm.text(),restored.text()));
        if(vm.link_count() && vm.state()==story_state::debate)
        {
            vm.object(0);vm._return_valid=true;vm.jump(vm._return.script,vm._return.pc);
            assert(vm.text_style()==&s); // Returning uses the existing logical PC, without a saved style field.
        }
        if(n){const auto& prev=story_text_styles[n-1];assert(prev.script<s.script || (prev.script==s.script && prev.end<s.end && prev.pc<s.pc));}
    }
    const auto& first=story_text_styles[0];
    assert(!find_text_style(first.script,first.end-1));
    std::printf("PASS: %d original colored utterances / %u spans: UTF8 boundaries, ROM lookup, current history, unchanged 4B history entries and save/load\n",story_text_style_count,span_count);
}
