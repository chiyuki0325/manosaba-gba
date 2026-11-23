#include "story_vm.h"
#include "lzss.h"
#include "save_archive.h"
#include "save_checkpoint.h"
#include <cstring>

namespace manosaba
{
namespace {
bool prefix(const char* text,const char* start)
{
    for(int i=0;start[i];++i)if(text[i]!=start[i])return false;
    return true;
}
}
uint8_t story_vm::byte()
{
    int block = story_scripts[_script].first_block + int(_pc / story_block_size);
    if(block != _cached_block)
    {
        const auto& entry = story_blocks[block];
        const uint8_t* source = story_bytes + entry.offset;
        if(! entry.codec)
        {
            std::memcpy(_cache, source, entry.size);
        }
        else
        {
            int size = int(story_scripts[_script].size - (_pc / story_block_size) * story_block_size);
            if(size > story_block_size)
            {
                size = story_block_size;
            }
            lzss_decode(source, _cache, size);
        }
        _cached_block = block;
    }
    uint8_t value = _cache[_pc % story_block_size];
    ++_pc;
    return value;
}

uint16_t story_vm::word()
{
    unsigned low = byte();
    return uint16_t(low | (unsigned(byte()) << 8));
}

uint32_t story_vm::offset()
{
    uint32_t low = word();
    return low | (uint32_t(word()) << 16);
}

void story_vm::string(char* out)
{
    uint8_t value;
    do
    {
        value = byte();
        *out++ = char(value);
    }
    while(value);
}

void story_vm::remember(uint16_t pc)
{
    for(int i=0;i<_history_count;++i)if(_history[i].pc==pc)return;
    int lines=1;for(const char* p=_text;*p;++p)lines+=*p=='\n' || *p=='\r';
    _history[_history_count++]={pc,_speaker,uint8_t(lines)};
}
void story_vm::history_text(int index,char* destination)
{
    auto saved=_pc;_pc=_history[index].pc;string(destination);_pc=saved;
    // Keep the shared decompression cache at its new block; byte() reloads as needed.
    char* out=destination;
    for(char* p=destination;*p;)
    {
        if(uint8_t(p[0])==0xee && uint8_t(p[1])==0x80 && uint8_t(p[2])>=0x80 && uint8_t(p[2])<=0x82)p+=3;
        else {*out++=*p=='\r' ? '\n' : *p;++p;}
    }
    *out=0;
}

void story_vm::jump(int script, uint32_t pc)
{
    if(script != _script)
    {
        // Sustained sounds belong to their script; let one-shot tails finish.
        if(_sfx_sink)
        {sfx_command command;command.stop=command.loops_only=true;_sfx_sink(_sfx_context,command);}
        _history_count=0;
        if(!trial() && story_scripts[script].mode)
        {
            _hp=10;_bad_id=0;_total_left=0;_return_valid=false;_printer_style=0;_reenact=false;
            _screen={};_screen_wait=0;_shake={};_court_fire=false;
        }
        _cached_block = -1;
        _direct = false;
        _visual_frame = -1;
    }
    _script = script;
    _pc = pc;
}

void story_vm::start(int script)
{
    _cached_block = -1;
    _script = script;
    _pc = 0;
    _source_pc=0;
    _state = story_state::running;
    _history_count=0;
    _flags = 0;
    _area = 0;
    _speaker = 0;
    _direct = true;
    _option_count = 0;
    _visual_frame = -1;
    _text[0] = _backdrop[0] = _character[0] = 0;
    _music_alias=0;
    _toast=_toast_left=0;
    _trial_flags=0;_hp=10;_bad_id=0;_trial_actor=_trial_view=_picture=0;
    _duration=_line_left=_total_left=0;_link_count=_menu_mode=_candidate_count=_chosen_item=0;
    _return_valid=_restored=_witch_hiro=false;
    _printer_style=0;_reenact=_bad_end=false;_reenact_closed=true;
    _reenact_scene=-1;_reenact_wait=_reenact_action=_credits_act=0;
    _map_segment=-1;_map_wait=false;
    _actor_shake={};_actor_shake_id=255;
    _screen={};_screen_wait=0;
    const auto& effects=story_effect_entries[story_effect_ids[script]];
    _screen.start(effects.color,effects.amount,0,false);_shake.start(effects.shake);_court_fire=effects.fire;
    _trial_camera=-1;_camera_frame=_camera_duration=_camera_wait=0;
    std::memset(_trial_poses,255,sizeof(_trial_poses));_trial_layout=0;
    std::memset(_book_versions,0,sizeof(_book_versions));
    _book_chapter=0;
    advance();
}

void story_vm::archive(save_archive& a)
{
    a.field(_script);a.field(_pc);a.field(_state);a.field(_speaker);
    a.text(_text,sizeof(_text));a.text(_backdrop,sizeof(_backdrop));a.text(_character,sizeof(_character));
    a.field(_music_alias);a.field(_visual_frame);a.field(_stage_event);a.field(_flags);a.field(_area);a.field(_direct);a.field(_map);
    a.field(_option_count);
    if(_option_count<0 || _option_count>story_max_options){a.reject();return;}
    for(int i=0;i<_option_count;++i)
    {auto& o=_options[i];a.text(o.text,sizeof(o.text));if(!_map){a.field(o.pc);a.field(o.script);}a.field(o.value);}
    // Script/PC already anchor the current dialogue. Consume old lists when
    // loading, but new saves do not persist the session's history.
    uint16_t history_count=0;a.field(history_count);
    if(history_count>story_history_capacity){a.reject();return;}
    if(a.reading()){a.bytes(_history,history_count*sizeof(_history[0]));_history_count=0;}
    // Two 32-bit words keep the 64-flag field portable without 64-bit varint code.
    uint32_t low=uint32_t(_trial_flags),high=uint32_t(_trial_flags>>32);a.field(low);a.field(high);
    if(a.reading())_trial_flags=uint64_t(low)|(uint64_t(high)<<32);
    a.field(_link_count);
    if(_link_count<0 || _link_count>3){a.reject();return;}
    for(int i=0;i<_link_count;++i){auto& l=_links[i];a.field(l.pc);a.field(l.script);a.field(l.first);a.field(l.last);}
    a.field(_menu_mode);a.field(_hp);a.field(_bad_id);a.field(_trial_actor);a.field(_trial_view);a.field(_picture);
    a.field(_trial_camera);for(auto& p:_trial_poses)a.field(p);a.field(_trial_layout);
    // Presentation motion settles on load; dialogue timers below stay exact.
    a.field(_camera_duration);
    if(a.reading()){_camera_frame=_camera_duration;_camera_wait=0;}
    a.field(_duration);a.field(_line_left);a.field(_total_left);
    a.field(_candidate_count);
    if(_candidate_count<0 || _candidate_count>book_entry_count){a.reject();return;}
    for(int i=0;i<_candidate_count;++i)a.field(_candidates[i]);
    a.field(_chosen_item);a.field(_inventory_kind);a.field(_inventory_back);a.field(_cutin_kind);a.field(_cutin_variant);
    a.bytes(_book_versions,sizeof(_book_versions));a.field(_book_chapter);
    a.field(_return_valid);a.field(_restored);a.field(_witch_hiro);a.field(_printer_style);a.field(_reenact);a.field(_bad_end);
    a.field(_reenact_scene);a.field(_reenact_wait);a.field(_reenact_action);
    if(_state==story_state::credits)a.field(_credits_act);
    a.field(_map_segment);a.field(_map_wait);
    _screen.archive(a);if(a.reading())_screen_wait=0;
    _shake.archive(a);a.field(_court_fire);
    _actor_shake.archive(a);a.field(_actor_shake_id);
    auto box=[&](dialogue_layout& b){a.field(b.left);a.field(b.top);a.field(b.right);a.field(b.bottom);};box(_debate_layout);
    if(_return_valid)
    {
        auto& r=_return;a.text(r.text,sizeof(r.text));a.field(r.link_count);
        if(r.link_count<0 || r.link_count>3){a.reject();return;}
        for(int i=0;i<r.link_count;++i){auto& l=r.links[i];a.field(l.pc);a.field(l.script);a.field(l.first);a.field(l.last);}
        a.field(r.pc);a.field(r.script);a.field(r.speaker);a.field(r.duration);a.field(r.left);
        a.field(r.actor);a.field(r.view);a.field(r.picture);a.field(r.camera);a.field(r.camera_duration);
        if(a.reading())r.camera_frame=r.camera_duration;
        for(auto& p:r.poses)a.field(p);
        a.field(r.trial_layout);a.field(r.witch_hiro);box(r.layout);
    }
    if(a.reading())
    {
        _cached_block=-1;_toast=_toast_left=0;
        if(_state==story_state::text || _state==story_state::debate)
        {
            unsigned tail=_state==story_state::debate ? 1+_link_count*10 : 0;
            unsigned length=0;while(length<sizeof(_text) && _text[length])++length;
            remember(uint16_t(_pc-length-1-tail));
        }
    }
}


uint8_t story_vm::checkpoint(save_archive& a,uint8_t flags)
{
    unsigned source=_source_pc;
    if(!a.reading())
    {
        if(_state==story_state::running){flags|=save_cursor;source=_pc;}
        if(_state==story_state::title && _cutin_kind==2)flags|=save_failure;
    }
    a.fixed(flags);a.fixed(_script);a.fixed(source);
    a.fixed(_flags);a.fixed(_area);a.fixed(_direct);a.fixed(_trial_flags);
    a.fixed(_hp);a.fixed(_bad_id);a.fixed(_chosen_item);a.fixed(_total_left);
    a.bytes(_book_versions,sizeof(_book_versions));a.fixed(_book_chapter);
    auto court=[&](int& actor,int& view,int& picture,int& camera,auto& duration,
                   uint16_t* poses,uint8_t& layout,bool& witch)
    {
        a.fixed(actor);a.fixed(view);a.fixed(picture);a.fixed(camera);a.fixed(duration);
        for(int i=0;i<17;++i)a.fixed(poses[i]);
        a.fixed(layout);a.fixed(witch);
    };
    court(_trial_actor,_trial_view,_picture,_trial_camera,_camera_duration,_trial_poses,_trial_layout,_witch_hiro);
    uint8_t reenact=_reenact ? uint8_t(1|(uint8_t(_reenact_closed && _reenact_scene>=0)<<1)) : 0;
    a.fixed(_visual_frame);a.fixed(_printer_style);a.fixed(reenact);a.fixed(_reenact_scene);a.fixed(_map_segment);
    if(a.reading()){_reenact=reenact&1;_reenact_closed=reenact&2;}
    _screen.checkpoint(a);a.fixed(_court_fire);
    int current_script=_script;
    auto& r=_return;a.fixed(_return_valid);
    if(_return_valid)
    {
        a.fixed(r.script);a.fixed(r.source_pc);
        court(r.actor,r.view,r.picture,r.camera,r.camera_duration,r.poses,r.trial_layout,r.witch_hiro);
    }
    else {uint8_t empty[64]={};a.bytes(empty,sizeof(empty));if(a.reading())r={};}
    if(!a.reading() || !a.good())return flags;
    _cached_block=-1;_toast=_toast_left=0;_history_count=0;_music_alias=0;
    _text[0]=_backdrop[0]=_character[0]=0;_speaker=0;
    _option_count=_candidate_count=_link_count=_menu_mode=0;_map=false;
    _duration=_line_left=0;_camera_frame=_camera_duration;_camera_wait=_screen_wait=0;
    _shake={};_actor_shake={};_actor_shake_id=255;_restored=false;
    _cutin_kind=_cutin_variant=_reenact_action=_reenact_wait=_credits_act=0;
    _map_wait=false;_bad_end=false;_stage_event=-1;
    _source_pc=source;_pc=source;_state=story_state::running;
    if(_return_valid)
    {
        if(r.script<0 || r.script>=story_script_count || r.source_pc>=story_scripts[r.script].size){a.reject();return flags;}
        _script=r.script;_pc=r.source_pc;
        if(!restore_payload(true)){a.reject();return flags;}
        r.camera_frame=r.camera_duration;
    }
    _script=current_script;_pc=source;_cached_block=-1;
    if(_script<0 || _script>=story_script_count || source>=story_scripts[_script].size){a.reject();return flags;}
    if(!(flags&save_cursor) && !restore_payload()){a.reject();return flags;}
    if(flags&save_failure){_state=story_state::title;_cutin_kind=2;_text[0]=0;}
    return flags;
}

bool story_vm::restore_payload(bool returning)
{
    auto op=story_op(byte());unsigned length=word(),end=_pc+length;
    if(end>story_scripts[_script].size)return false;
    if(returning)
    {
        if(op!=story_op::debate)return false;
        auto& r=_return;r.speaker=byte();r.duration=word();r.left=r.duration&0x7fff;
        r.layout={byte(),byte(),byte(),byte()};string(r.text);r.link_count=byte();
        for(int i=0;i<r.link_count;++i){auto& l=r.links[i];l.first=word();l.last=word();l.script=word();l.pc=offset();}
        r.pc=end;return _pc==end;
    }
    switch(op)
    {
    case story_op::say:
    { _speaker=byte();auto text_pc=uint16_t(_pc);string(_text);remember(text_pc);_state=story_state::text;break; }
    case story_op::debate:
    {
        _speaker=byte();_duration=word();_line_left=statement_duration();_debate_layout={byte(),byte(),byte(),byte()};
        auto text_pc=uint16_t(_pc);string(_text);remember(text_pc);_link_count=byte();
        for(int i=0;i<_link_count;++i){auto& l=_links[i];l.first=word();l.last=word();l.script=word();l.pc=offset();}
        _state=story_state::debate;break;
    }
    case story_op::menu:
    case story_op::trial_menu:
    {
        _option_count=0;_menu_mode=byte();_map=op==story_op::menu && _menu_mode==1;int count=byte();
        for(int i=0;i<count;++i)
        {
            auto& choice=_options[_option_count];bool visible=true;
            if(_map){unsigned bit=byte();choice.value=byte();visible=!(_flags&(1<<bit));}
            else {choice.value=byte();choice.script=word();choice.pc=offset();}
            string(choice.text);_option_count+=visible;
        }
        _state=_option_count ? story_state::menu : story_state::running;break;
    }
    case story_op::inventory:
    {
        _candidate_count=0;_inventory_kind=byte();_inventory_back=byte();int count=byte();
        if(_inventory_kind<2)
        {for(int b=0;b<book_entry_count;++b)if(book_entries[b].kind==_inventory_kind && _book_versions[b])_candidates[_candidate_count++]=book_entries[b].story_id;}
        else for(int i=0;i<count;++i)_candidates[_candidate_count++]=word();
        _state=story_state::inventory;break;
    }
    case story_op::pause:string(_text);_state=story_state::pause;break;
    case story_op::end:
    case story_op::bad_end:string(_text);_bad_end=op==story_op::bad_end;_state=story_state::end;break;
    case story_op::title:_text[0]=0;_cutin_kind=byte();_state=story_state::title;break;
    case story_op::cutin:_cutin_kind=byte();_cutin_variant=byte();_state=story_state::cutin;break;
    case story_op::credits:_text[0]=0;_visual_frame=-1;_credits_act=byte();_state=story_state::credits;break;
    case story_op::execution:_text[0]=0;_state=story_state::execution;break;
    case story_op::stage:_stage_event=word();_state=story_state::stage;break;
    case story_op::reenact:
        _reenact_action=byte();
        _state=story_state::reenact;
        break;
    case story_op::music:_state=story_state::audio;break;
    case story_op::trial:_state=story_state::camera;break;
    case story_op::screen:_state=story_state::screen;break;
    case story_op::map:_state=story_state::map;break;
    default:break; // Already-applied audio/camera/screen/map commands settle at their target.
    }
    _pc=end;return true;
}

unsigned story_vm::anchor_before(int script,unsigned end,bool* curtain)
{
    int saved_script=_script;unsigned saved_pc=_pc,result=end;
    _script=script;_pc=0;_cached_block=-1;
    while(_pc<end)
    {
        unsigned anchor=_pc;auto op=story_op(byte());unsigned length=word(),next=_pc+length;
        if(curtain && op==story_op::reenact)
        {int action=byte();if(action<2)*curtain=action==1;}
        _pc=next;
        if(_pc==end){result=anchor;break;}
    }
    _script=saved_script;_pc=saved_pc;_cached_block=-1;return result;
}
void story_vm::legacy_anchors()
{
    _reenact_closed=_reenact_action==1;
    _source_pc=anchor_before(_script,_pc,&_reenact_closed);
    if(_return_valid)_return.source_pc=anchor_before(_return.script,_return.pc);
}

void story_vm::object(int index)
{
    std::memcpy(_return.text,_text,sizeof(_text));
    std::memcpy(_return.links,_links,sizeof(_links));
    _return.pc=_pc;_return.source_pc=_source_pc;_return.script=_script;_return.speaker=_speaker;_return.link_count=_link_count;
    _return.duration=_duration;_return.left=_line_left;_return.actor=_trial_actor;_return.view=_trial_view;_return.picture=_picture;
    _return.camera=_trial_camera;_return.camera_frame=_camera_frame;_return.camera_duration=_camera_duration;
    std::memcpy(_return.poses,_trial_poses,sizeof(_trial_poses));_return.trial_layout=_trial_layout;_return.witch_hiro=_witch_hiro;
    _return.layout=_debate_layout;
    _return_valid=true;auto target=_links[index];jump(target.script,target.pc);advance();
}

void story_vm::fail()
{
    _return_valid=false;_picture=_link_count=_menu_mode=_candidate_count=0;
    _duration=_line_left=_total_left=0;
    _text[0]=0;_printer_style=0;_cutin_kind=2;_state=story_state::title;
    _screen.start(0,256,54,false);
    if(_music_sink)
    {music_command command;command.stop=true;command.duration_frames=21;_music_sink(_music_context,command);}
}

bool story_vm::tick_debate(int frames)
{
    unsigned elapsed=_line_left ? (unsigned(frames)<_line_left ? unsigned(frames) : _line_left) : unsigned(frames);
    if(_line_left)_line_left-=elapsed;
    if(elapsed>=_total_left){fail();return true;}
    _total_left-=elapsed;
    if(!_line_left && !debate_stopped()){advance();return true;}
    return false;
}

void story_vm::skip_debate()
{
    if(debate_stopped())return;
    // Charge the skipped tail too: later statements receive no extra budget.
    tick_debate(int(_line_left));
}

void story_vm::choose_item(int index)
{
    _chosen_item=_candidates[index];advance();
}

void story_vm::cancel_inventory()
{
    _chosen_item=0;advance();
}

void story_vm::choose(int index)
{
    const auto& choice = _options[index];
    if(_map)
    {
        _area = choice.value;
    }
    else
    {
        jump(choice.script, choice.pc);
    }
    advance();
}

void story_vm::advance()
{
    if(_state==story_state::title && _cutin_kind==2)
    {_screen={};_screen_wait=0;jump(story_common_bad_scripts[_bad_id-1],0);}
    _state = story_state::running;
    _restored=false;
    // A bounded amount of work per frame, including scripts with no dialogue.
    for(int budget = 0; budget < 128; ++budget)
    {
        _source_pc=_pc;
        auto op = story_op(byte());
        unsigned length = word();
        uint32_t next = _pc + length;
        switch(op)
        {
        case story_op::say:
            _pc=_source_pc;restore_payload();return;
        case story_op::jump:
        {
            int script = word();
            uint32_t pc = offset();
            jump(script, pc);
            continue;
        }
        case story_op::branch:
        {
            int mode = byte();
            unsigned value = word();
            uint32_t pc = offset();
            bool condition = mode == 0 ? bool(value) : mode == 1 ? _flags == value :
                             mode == 2 ? _area == value : _direct;
            if(! condition)
            {
                _pc = pc;
                continue;
            }
            break;
        }
        case story_op::menu:
            _pc=_source_pc;restore_payload();if(_state!=story_state::running)return;break;
        case story_op::flags:
            _flags = word();
            break;
        case story_op::flag:
            _flags |= uint16_t(1 << byte());
            break;
        case story_op::backdrop:
            string(_backdrop);
            break;
        case story_op::character:
            string(_character);
            if(prefix(_character,"CreatureHiro"))_witch_hiro=true;
            else if(prefix(_character,"Hiro."))_witch_hiro=false;
            break;
        case story_op::sfx:
        {
            int cue=word();sfx_command command;command.cue=cue==65535 ? -1 : cue;
            command.volume=word();command.fade_frames=word();command.loop=byte();command.stop=byte();
            if(_sfx_sink)_sfx_sink(_sfx_context,command);
            break;
        }
        case story_op::music:
        {
            auto path=word(),intro=word();
            music_command command;
            command.path=path ? story_music_aliases[path] : nullptr;
            command.intro=intro ? story_music_aliases[intro] : nullptr;
            command.volume=word();command.duration_frames=word();command.fade_in_frames=word();
            command.parameters=word();command.loop=byte();command.wait=byte();command.stop=byte();
            if(command.stop){if(!path || path==_music_alias)_music_alias=0;}
            else if(path)_music_alias=path;
            if(_music_sink && _music_sink(_music_context,command))
            {_state=story_state::audio;return;}
            break;
        }
        case story_op::pause:
        case story_op::bad_end:
        case story_op::end:
            _pc=_source_pc;restore_payload();return;
        case story_op::stage:
            _stage_event = word();
            if(_visual_frame < 0) _visual_frame = 0;
            _state = story_state::stage;
            return;
        case story_op::visual:
            _visual_frame = word();
            break;
        case story_op::trial:
        {
            int kind=byte();
            if(kind==0){int default_bad=byte();if(!_bad_id)_bad_id=default_bad;_trial_camera=-1;_camera_frame=_camera_duration=_camera_wait=0;}
            else if(kind==1){_trial_actor=byte();_trial_view=byte();if(_trial_actor==1 && _witch_hiro)_trial_actor=16;}
            else if(kind==2){unsigned gap=word();_total_left=_total_left>gap ? _total_left-gap : 0;}
            else if(kind==3)_printer_style=byte();
            else if(kind==4){_reenact=bool(byte());if(!_reenact)_reenact_scene=-1;}
            else if(kind==5)
            {
                int camera=byte();_trial_camera=camera==255 ? -1 : camera;_camera_frame=_camera_wait=0;_camera_duration=word();
                if(byte()){_camera_wait=_camera_duration;_state=story_state::camera;_text[0]=0;return;}
            }
            else if(kind==6){_camera_wait=word();_state=story_state::camera;_text[0]=0;return;}
            else if(kind==7)_trial_layout=byte();
            else if(kind==8){int actor=byte();_trial_poses[actor]=word();}
            else if(kind==9)_text[0]=0;
            else if(kind==10)
            {
                trial_shake_spec spec;spec.frames=word();spec.x=int8_t(byte());spec.y=int8_t(byte());spec.period=byte();spec.damping=byte();
                _shake.start(spec);
                if(byte() && spec.frames){_screen_wait=spec.frames;_state=story_state::screen;return;}
            }
            else if(kind==11)_court_fire=bool(byte());
            else if(kind==12)
            {
                _actor_shake_id=byte();trial_shake_spec spec;spec.frames=word();spec.x=int8_t(byte());spec.y=int8_t(byte());spec.period=byte();spec.damping=byte();_actor_shake.start(spec);
            }
            break;
        }
        case story_op::debate:
        case story_op::trial_menu:
            _pc=_source_pc;restore_payload();return;
        case story_op::test:
        {
            bool stack[16];int used=0;unsigned token;
            while((token=byte())!=255)
            {
                if(token==0)stack[used++]=bool(byte());
                else if(token==1)stack[used++]=bool(_trial_flags&(uint64_t(1)<<byte()));
                else if(token==2)stack[used++]=_chosen_item==word();
                else if(token==3)stack[used++]=_direct;
                else if(token==128){stack[used-2]=stack[used-2] || stack[used-1];--used;}
                else if(token==129){stack[used-2]=stack[used-2] && stack[used-1];--used;}
                else if(token==130)stack[used-1]=!stack[used-1];
            }
            uint32_t target=offset();if(!stack[0]){_pc=target;continue;}break;
        }
        case story_op::variable:
        {
            uint64_t mask=uint64_t(1)<<byte();if(byte())_trial_flags|=mask;else _trial_flags&=~mask;break;
        }
        case story_op::resume:
            if(_return_valid)
            {
                jump(_return.script,_return.pc);_source_pc=_return.source_pc;std::memcpy(_text,_return.text,sizeof(_text));std::memcpy(_links,_return.links,sizeof(_links));
                _speaker=uint8_t(_return.speaker);_link_count=_return.link_count;_duration=_return.duration;_line_left=_return.left;
                _trial_actor=_return.actor;_trial_view=_return.view;_picture=_return.picture;
                _trial_camera=_return.camera;_camera_frame=_return.camera_frame;_camera_duration=_return.camera_duration;
                std::memcpy(_trial_poses,_return.poses,sizeof(_trial_poses));_trial_layout=_return.trial_layout;_witch_hiro=_return.witch_hiro;
                _debate_layout=_return.layout;
                _return_valid=false;_restored=true;_state=story_state::debate;return;
            }
            break;
        case story_op::cancel_resume:_return_valid=false;break;
        case story_op::inventory:
            _pc=_source_pc;restore_payload();return;
        case story_op::timer:_total_left=offset();break;
        case story_op::damage:break; // Retail Damage is a no-op.
        case story_op::cutin:
        case story_op::title:
            _pc=_source_pc;restore_payload();return;
        case story_op::toast:_toast=byte();_toast_left=150;break;
        case story_op::reenact:
            _reenact_action=byte();
            if(_reenact_action==0){_reenact_scene=int16_t(word());_reenact_closed=false;}
            else if(_reenact_action==1)_reenact_closed=true;
            else if(_reenact_action==2)_reenact_wait=word();
            _text[0]=0;_state=story_state::reenact;return;
        case story_op::picture:_picture=word();break;
        case story_op::screen:
        {
            int action=byte();
            if(action==0)
            {
                uint16_t color=word(),amount=word(),frames=word();int flags=byte();
                _screen.start(color,amount,frames,flags&1);
                if((flags&128) && frames){_screen_wait=frames;_state=story_state::screen;return;}
            }
            else {_screen_wait=word();_state=story_state::screen;return;}
            break;
        }
        case story_op::credits:
        case story_op::execution:
            _pc=_source_pc;restore_payload();return;
        case story_op::map:
            _map_segment=int16_t(word());_map_wait=bool(byte());_text[0]=0;_state=story_state::map;return;
        case story_op::bad_id:_bad_id=byte();break;
        case story_op::book_update:
        {int entry=byte();_book_versions[entry]=byte();break;}
        case story_op::book_clear:
        {
            int mask=byte();for(int i=0;i<book_entry_count;++i)if(mask&(1<<book_entries[i].kind))_book_versions[i]=0;
            break;
        }
        case story_op::book_reset:
        {
            int index=byte();bool direct_only=byte();
            if(!direct_only || _direct || _book_chapter!=book_initial_chapters[index])
                std::memcpy(_book_versions,book_initial_states[index],sizeof(_book_versions));
            _book_chapter=book_initial_chapters[index];
            break;
        }
        default:
            break;
        }
        _pc = next; // Unknown TLV opcodes are also skipped here.
    }
}
}
