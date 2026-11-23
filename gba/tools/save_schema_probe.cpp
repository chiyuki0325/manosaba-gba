// Host-only v5 design probe. Not linked into the ROM or the current save writer.
// Build/run command and scope are documented in docs/save-capacity.md.
#include "story_format.h"
#include "story_layout.h"
#include "witch_book_data.h"
#include "music_command.h"
#include "stage_format.h"
#include "save_archive.h"
#include "save_checkpoint.h"
#define private public
#include "story_vm.h"
#undef private
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace manosaba;
namespace {
// Explicit-width fields; no struct padding, host endian, varints or pointers.
struct wire {
    uint8_t* data;
    bool reading;
    unsigned size=0;
    template<class T> void field(T& value) {
        using U=std::make_unsigned_t<T>;
        U bits=0;
        if(!reading)std::memcpy(&bits,&value,sizeof(T));
        for(unsigned byte=0;byte<sizeof(T);++byte) {
            if(reading)bits|=U(data[size++])<<(byte*8);
            else data[size++]=uint8_t(bits>>(byte*8));
        }
        if(reading)std::memcpy(&value,&bits,sizeof(T));
    }
    template<class T,size_t N> void fields(T (&values)[N]) {for(auto& value:values)field(value);}
};

struct court_record {
    int32_t actor=0,view=0,picture=0,camera=-1;
    uint32_t camera_duration=0;
    uint16_t poses[17]={};
    uint8_t layout=0,witch_hiro=0;
    void archive(wire& a) {
        a.field(actor);a.field(view);a.field(picture);a.field(camera);
        a.field(camera_duration);a.fields(poses);a.field(layout);a.field(witch_hiro);
    }
};
struct entity_record {
    int32_t values[int(stage_property::count)]={};
    int16_t image=-1,order=0,mask=-1;
    uint8_t presentation=1;
    void archive(wire& a) {a.fields(values);a.field(image);a.field(order);a.field(mask);a.field(presentation);}
};
struct sound_record {
    int32_t cue=-1,volume=0;
    uint8_t loop=0;
    void archive(wire& a) {a.field(cue);a.field(volume);a.field(loop);}
};
struct checkpoint {
    // Execution bookkeeping, not a saved UI: bit 0 = next-op cursor (work
    // budget/legacy), bit 1 = yielded stage command has not been applied yet.
    uint8_t resume_flags=0;
    int32_t script=0;
    uint32_t source_pc=0;
    uint16_t flags=0;
    uint8_t area=0,direct=0;
    uint64_t trial_flags=0;
    int32_t hp=10,bad_id=0,chosen_item=0;
    uint32_t total_left=0;
    uint8_t book_versions[book_entry_count]={},book_chapter=0;
    court_record court;
    int32_t visual_frame=-1;
    uint8_t printer_style=0,reenact=0;
    int16_t reenact_scene=-1,map_segment=-1;
    uint16_t screen_target[4]={};
    uint8_t court_fire=0,return_valid=0;
    int32_t return_script=0;
    uint32_t return_source_pc=0;
    court_record return_court;
    entity_record entities[stage_entity_count];
    uint8_t printer=1;
    uint32_t music_key=0;
    int32_t music_volume=0;
    uint8_t music_loop=0;
    sound_record sounds[2];
    void archive(wire& a) {
        a.field(resume_flags);a.field(script);a.field(source_pc);
        a.field(flags);a.field(area);a.field(direct);a.field(trial_flags);
        a.field(hp);a.field(bad_id);a.field(chosen_item);a.field(total_left);
        a.fields(book_versions);a.field(book_chapter);court.archive(a);
        a.field(visual_frame);a.field(printer_style);a.field(reenact);a.field(reenact_scene);a.field(map_segment);
        a.fields(screen_target);a.field(court_fire);
        a.field(return_valid);a.field(return_script);a.field(return_source_pc);return_court.archive(a);
        for(auto& entity:entities)entity.archive(a);
        a.field(printer);a.field(music_key);a.field(music_volume);a.field(music_loop);
        for(auto& sound:sounds)sound.archive(a);
    }
};

void capacity() {
    static_assert(sizeof(int)==4 && stage_entity_count==16 && int(stage_property::count)==12);
    // Fill every byte, including otherwise-impossible simultaneous maxima. The
    // wire length must be independent of semantic values and branch reachability.
    checkpoint source,restored;
    std::array<uint8_t,4096> first{},second{};
    wire output{first.data(),false};source.archive(output);
    const unsigned bytes=output.size;
    for(unsigned pattern:{0u,0x55u,0xaau,0xffu}) {
        std::memset(first.data(),int(pattern),bytes);
        wire input{first.data(),true};restored.archive(input);
        wire again{second.data(),false};restored.archive(again);
        assert(input.size==bytes && again.size==bytes && !std::memcmp(first.data(),second.data(),bytes));
    }
    assert(bytes==save_checkpoint_bytes && bytes<=1280);
    // Retry has no entry in the save/load list: no thumbnail, summary or RTC
    // metadata is necessary. Also verify the conservative existing 256B prefix.
    const unsigned page=256+bytes*2,retry=64+bytes;
    assert(page<=3072 && retry<=1536);
    assert(256+bytes<=1536);
    assert(9*3072+1536+3072+2*256==32768);
    // Normal slots retain the historical 192-byte metadata and 64-byte header.
    std::printf("FIXED schema=%uB VM=%uB stage=%uB audio=27B; manual+seed=%u/3072B retry=%u/1536B\n",
        bytes,bytes-(stage_entity_count*55+1)-27,stage_entity_count*55+1,page,retry);
}

// Pure display-payload decoder prototype. These are reads from ROM only; do not
// call start(), advance(), jump(), or an audio sink during restoration.
bool payload(story_vm& vm,unsigned anchor) {
    vm._pc=anchor;
    auto op=story_op(vm.byte());unsigned length=vm.word(),end=vm._pc+length;
    vm._option_count=vm._candidate_count=vm._link_count=0;
    switch(op) {
    case story_op::say:
        vm._speaker=vm.byte();vm.string(vm._text);vm._state=story_state::text;break;
    case story_op::debate:
        vm._speaker=vm.byte();vm._duration=vm.word();vm._line_left=vm.statement_duration();
        vm._debate_layout={vm.byte(),vm.byte(),vm.byte(),vm.byte()};vm.string(vm._text);
        vm._link_count=vm.byte();
        for(int i=0;i<vm._link_count;++i) {
            auto& link=vm._links[i];link.first=vm.word();link.last=vm.word();link.script=vm.word();link.pc=vm.offset();
        }
        vm._state=story_state::debate;break;
    case story_op::menu:
    case story_op::trial_menu: {
        vm._menu_mode=vm.byte();vm._map=op==story_op::menu && vm._menu_mode==1;
        int count=vm.byte();
        for(int i=0;i<count;++i) {
            auto& option=vm._options[vm._option_count];bool visible=true;
            if(vm._map) {unsigned bit=vm.byte();option.value=vm.byte();visible=!(vm._flags&(1<<bit));}
            else {option.value=vm.byte();option.script=vm.word();option.pc=vm.offset();}
            vm.string(option.text);vm._option_count+=visible;
        }
        vm._state=vm._option_count ? story_state::menu : story_state::running;break;
    }
    case story_op::inventory: {
        vm._inventory_kind=vm.byte();vm._inventory_back=vm.byte();int count=vm.byte();
        if(vm._inventory_kind<2) {
            for(int b=0;b<book_entry_count;++b)
                if(book_entries[b].kind==vm._inventory_kind && vm._book_versions[b])
                    vm._candidates[vm._candidate_count++]=book_entries[b].story_id;
        }
        else for(int i=0;i<count;++i)vm._candidates[vm._candidate_count++]=vm.word();
        vm._state=story_state::inventory;break;
    }
    case story_op::pause:vm.string(vm._text);vm._state=story_state::pause;break;
    case story_op::end:
    case story_op::bad_end:
        vm.string(vm._text);vm._bad_end=op==story_op::bad_end;vm._state=story_state::end;break;
    case story_op::title:
        vm._cutin_kind=vm.byte();vm._text[0]=0;vm._state=story_state::title;break;
    default:assert(false);
    }
    assert(vm._pc==end);
    return vm._state!=story_state::running;
}

bool display(story_op op) {
    return op==story_op::say || op==story_op::debate || op==story_op::menu || op==story_op::trial_menu ||
        op==story_op::inventory || op==story_op::pause || op==story_op::end || op==story_op::bad_end || op==story_op::title;
}
int audio_calls=0;
void compare(const story_vm& a,const story_vm& b) {
    assert(a._state==b._state && a._pc==b._pc && a._script==b._script);
    if(a._state==story_state::text || a._state==story_state::debate)
        assert(a._speaker==b._speaker && !std::strcmp(a._text,b._text));
    if(a._state==story_state::pause || a._state==story_state::end)
        assert(!std::strcmp(a._text,b._text) && a._bad_end==b._bad_end);
    if(a._state==story_state::debate) {
        assert(a._duration==b._duration && a._line_left==b._line_left && a._link_count==b._link_count);
        assert(!std::memcmp(&a._debate_layout,&b._debate_layout,sizeof(a._debate_layout)));
        for(int i=0;i<a._link_count;++i) {
            const auto& x=a._links[i];const auto& y=b._links[i];
            assert(x.pc==y.pc && x.script==y.script && x.first==y.first && x.last==y.last);
        }
    }
    if(a._state==story_state::menu) {
        assert(a._menu_mode==b._menu_mode && a._map==b._map && a._option_count==b._option_count);
        for(int i=0;i<a._option_count;++i) {
            const auto& x=a._options[i];const auto& y=b._options[i];
            assert(!std::strcmp(x.text,y.text) && x.value==y.value);
            if(!a._map)assert(x.pc==y.pc && x.script==y.script);
        }
    }
    if(a._state==story_state::inventory) {
        assert(a._inventory_kind==b._inventory_kind && a._inventory_back==b._inventory_back && a._candidate_count==b._candidate_count);
        assert(!std::memcmp(a._candidates,b._candidates,a._candidate_count*sizeof(a._candidates[0])));
    }
    if(a._state==story_state::title)assert(a._cutin_kind==b._cutin_kind);
    assert(a._flags==b._flags && a._area==b._area && a._direct==b._direct && a._trial_flags==b._trial_flags);
    assert(a._hp==b._hp && a._total_left==b._total_left && a._chosen_item==b._chosen_item);
    assert(!std::memcmp(a._book_versions,b._book_versions,sizeof(a._book_versions)));
}

void anchors() {
    unsigned instructions=0,displays=0,cases=0,maps=0,zero_menus=0;
    for(int script=0;script<story_script_count;++script) {
        story_vm reader{};reader._script=script;
        for(unsigned anchor=0;anchor<story_scripts[script].size;) {
            reader._pc=anchor;auto op=story_op(reader.byte());unsigned length=reader.word(),end=reader._pc+length;
            assert(end<=story_scripts[script].size);++instructions;
            if(display(op)) {
                ++displays;
                for(unsigned variation=0;variation<3;++variation) {
                    story_vm source{};source._script=script;source._pc=anchor;source._state=story_state::running;
                    source._flags=variation==2 ? 65535 : 0;source._hp=7;
                    source._chosen_item=23;source._trial_flags=UINT64_MAX;source._total_left=123456;
                    source._direct=false;source._area=3;
                    for(int b=0;b<book_entry_count;++b)source._book_versions[b]=uint8_t(variation ? book_entries[b].versions : 0);
                    source.bind_music([](void*,const music_command&){++audio_calls;return false;},nullptr);
                    source.bind_sfx([](void*,const sfx_command&){++audio_calls;},nullptr);
                    auto restored=source;
                    bool visible=payload(restored,anchor);
                    if(op==story_op::menu && restored._map && !variation)++maps;
                    // All-hidden map menu is intentionally not a stopping point.
                    if(!visible) {++zero_menus;continue;}
                    source.advance();compare(source,restored);++cases;
                    uint8_t logical[save_vm_checkpoint_bytes];
                    save_archive writer(logical,sizeof(logical),false);source.checkpoint(writer);
                    assert(writer.good() && writer.size()==save_vm_checkpoint_bytes);
                    story_vm actual{};save_archive reader(logical,sizeof(logical),true);actual.checkpoint(reader);
                    assert(reader.good() && reader.size()==save_vm_checkpoint_bytes);
                    compare(source,actual);
                    assert(audio_calls==0);
                    // Legacy cursor inversions need lengths of all source map
                    // choices, which the old save omits when they are hidden.
                    unsigned recovered=0;
                    if(op==story_op::say)recovered=end-5-unsigned(std::strlen(source._text));
                    else if(op==story_op::debate)recovered=end-12-unsigned(std::strlen(source._text))-10*source._link_count;
                    else if(op==story_op::menu || op==story_op::trial_menu) {
                        if(source._map)continue;
                        unsigned text=0;for(int i=0;i<source._option_count;++i)text+=unsigned(std::strlen(source._options[i].text));
                        recovered=end-5-8*source._option_count-text;
                    }
                    else if(op==story_op::inventory)recovered=end-6-(source._inventory_kind==2 ? 2*source._candidate_count : 0);
                    else if(op==story_op::title)recovered=end-4;
                    else recovered=end-4-unsigned(std::strlen(source._text));
                    assert(recovered==anchor);
                }
            }
            anchor=end;
        }
    }
    std::printf("PAYLOAD scripts=%d instructions=%u display_ops=%u checked=%u hidden_map_cases=%u legacy_map_anchors=%u audio_calls=%d\n",
        story_script_count,instructions,displays,cases,zero_menus,maps,audio_calls);
}
}
int main() {capacity();anchors();std::puts("PASS: fixed-wire bound and display-only reconstruction (production v5 VM and fixed-wire schema)");}
