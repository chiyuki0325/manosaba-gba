#include "video_upload.h"
#include "save_system.h"
#include "reenact_data.h"
#include "story_scene.h"
#include "save_ui_data.h"
#include "save_rtc.h"
#include "save_archive.h"
#include "save_checkpoint.h"
#include "save_legacy.h"
#include "save_runtime.h"
#include "sfx_ui.h"
#include "sfx_player.h"
#include "graphics_data.h"
#include "graphics_decode.h"
#include "bn_keypad.h"
#include "bn_core.h"
#include "bn_memory.h"
#include "bn_sprite_palette_item.h"
#include "bn_sprite_shape_size.h"
#include "bn_tile.h"
#include "bn_format.h"
#include "bn_string_view.h"
#include <cstring>
extern "C" {BN_DATA_EWRAM_BSS save_runtime_profile save_runtime={};}
namespace manosaba {
namespace {
BN_DATA_EWRAM_BSS uint32_t thumbnail_pixels[2][256];
void copy(char* out,int size,const bn::string_view& text)
{int length=bn::min(size-1,text.size());std::memcpy(out,text.data(),unsigned(length));out[length]=0;}
bn::string<12> digits(unsigned value)
{bn::string<12> text;if(value<10)text+="0";text+=bn::to_string<12>(value);return text;}
void chapter(char* out,int size,const save_metadata& metadata,bool short_form=false)
{
    if(metadata.script>=story_script_count){copy(out,size,"记录损坏");return;}
    const char* path=story_scripts[metadata.script].path;
    if(path[0]!='A'){copy(out,size,"审判失败");return;}
    int number=(path[13]-'0')*10+path[14]-'0';
    if(short_form){copy(out,size,bn::format<48>("第{}章",number));return;}
    const char* segment=path;for(const char* at=path;*at;++at)if(*at=='_')segment=at+1;
    const char* label=*segment=='T' ? "审问" : *segment=='B' ? "结局" : "日常";
    segment+=*segment=='T' ? 5 : 3;int index=(segment[0]-'0')*10+segment[1]-'0';
    copy(out,size,bn::format<48>("第{}章 {}{}",number,label,digits(unsigned(index))));
}
bn::string<32> saved_at(const save_metadata& meta)
{
    const auto& date=meta.timestamp;
    if(!date.valid)return bn::format<32>("游玩 {}:{}",digits(meta.play_frames/216000),digits(meta.play_frames/3600%60));
    return bn::format<32>("{}-{}-{} {}:{}",2000+date.year,digits(date.month),digits(date.day),digits(date.hour),digits(date.minute));
}
text_canvas::ink ink(int color){return text_canvas::ink(color);}
struct work_timer {uint32_t& result;unsigned begin=unsigned(bn::core::current_cpu_ticks());~work_timer(){result=unsigned(bn::core::current_cpu_ticks())-begin;}};
}
void save_system::init(){save_runtime.view=~0u;_store.mount(save_content_signature);}
bool save_system::buffer()
{
    if(!_buffer)_buffer=static_cast<uint8_t*>(bn::memory::ewram_alloc(save_buffer_size));
    return _buffer!=nullptr;
}
void save_system::free_buffer()
{if(_buffer){bn::memory::ewram_free(_buffer);_buffer=nullptr;}}
bool save_system::has_hiro_save() const
{
    save_metadata info;
    for(int slot=0;slot<=save_auto_slot;++slot)if(_store.metadata(slot,info) && info.hero)return true;
    return false;
}
bool save_system::has_saves() const
{
    save_metadata info;
    for(int slot=0;slot<=save_auto_slot;++slot)if(_store.metadata(slot,info))return true;
    return false;
}
bool save_system::retry_choice() const
{return _seed_size && _seed_script>=0 && !story_scripts[_seed_script].mode;}
void save_system::setting(int index,int value)
{
    if(index==0)_global.settings=uint8_t((_global.settings&~3)|value);
    else if(index==1){_global.volume=uint16_t(value*256);music_player::set_master_volume(_global.volume);}
    else if(index==2){_global.reserved=uint8_t(4-value);sfx_player::set_master_volume(value*256);}
    else if(index==3)_global.settings=uint8_t((_global.settings&~4)|(value ? 0 : 4));
    else if(index==4)_global.settings=uint8_t((_global.settings&~8)|(value ? 0 : 8));
    _global_dirty=true;
}
void save_system::new_story(){_seed_size=0;_seed_script=_auto_script=-1;_seed_pc=0;_seed_dirty=false;close();}
void save_system::close()
{
    for(int i=0;i<2;++i){_thumbs[i].reset();_thumb_tiles[i].reset();_thumb_palettes[i].reset();}
    _view=-1;_load_only=false;_confirm=0;_preview_row=-1;_preview_line=0;if(!_store.busy() && _operation==none)free_buffer();
}
void save_system::open_pause(bool failure)
{_load_only=false;_failure=failure;_view=2;_pause=failure && !retry_available() ? 1 : 0;_paint=0;_confirm=0;_preview_row=-1;_preview_line=0;_advance.reset();}
void save_system::list(bool header)
{
    int total=save_manual_slots+int(_view==1);_first=_selected[_view]<total-1 ? _selected[_view] : total-2;
    _paint=header ? 1 : 2;_confirm=0;_preview_row=-1;_preview_line=_row_line=_line_offset=0;_header_restored=false;_advance.reset();
}
void save_system::mark(const char* message)
{_message=message;_notice=90;_paint=5;}
bool save_system::write(story_scene& scene,int slot,operation kind)
{
    if(_store.needs_compaction(slot))
    {if(compact(slot,kind))return true;mark("保存失败");return false;}
    if(!buffer()){mark("内存不足");return false;}
    unsigned begin=unsigned(bn::core::current_cpu_ticks());
    int limit=bn::min(save_state_limit,save_page_size-save_payload_offset-int(_seed_size));
    int size=scene.snapshot(_buffer+save_payload_offset,limit);
    save_runtime.snapshot_work=unsigned(bn::core::current_cpu_ticks())-begin;
    if(size<0){mark("无法保存当前状态");return false;}
    if(_seed_size)std::memcpy(_buffer+save_payload_offset+size,_seed,_seed_size);
    auto metadata=scene.save_info();metadata.timestamp=save_rtc_now();
    if(!_store.begin_write(slot,_buffer,unsigned(size),_seed_size,metadata)){mark("存储不可用");return false;}
    _operation=kind;_last_progress=-1;return true;
}
bool save_system::compact(int slot,operation kind)
{
    int victim=_store.legacy_slot();
    if(victim<0 || !buffer() || !_store.begin_read(victim,_buffer))return false;
    _pending_slot=slot;_pending_operation=kind;_compact_slot=victim;_operation=compact_read;return true;
}
bool save_system::convert_loaded_record(unsigned& length,unsigned& seed)
{
#if !MANOSABA_SAVE_SRAM
    unsigned bytes=save_checkpoint_bytes*(seed ? 2u : 1u);
    auto* converted=static_cast<uint8_t*>(bn::memory::ewram_alloc(int(bytes)));
    if(!converted)return false;
    bool good=convert_legacy_save(_buffer+save_payload_offset,int(length),converted);
    if(good && seed)good=convert_legacy_save(_buffer+save_payload_offset+length,int(seed),converted+save_checkpoint_bytes);
    if(good){std::memcpy(_buffer+save_payload_offset,converted,bytes);length=save_checkpoint_bytes;seed=seed ? save_checkpoint_bytes : 0;}
    bn::memory::ewram_free(converted);return good;
#else
    (void)length;(void)seed;return false;
#endif
}
void save_system::unlock_gallery_asset(int asset)
{
    int first=0,last=gallery_image_count;
    while(first<last){int mid=(first+last)/2;if(gallery_images[gallery_asset_order[mid]].asset<asset)first=mid+1;else last=mid;}
    if(first==gallery_image_count)return;
    int index=gallery_asset_order[first];
    if(gallery_images[index].asset!=asset || gallery_unlocked(index))return;
    _global.gallery[index/8]|=uint8_t(1u<<(index%8));_global_dirty=true;++_gallery_revision;
}
void save_system::unlock_music(uint32_t key)
{
    if(!key || key==_music_key)return;
    _music_key=key;
    int bit=music_collection_bit(key);
    if(bit<0 || music_unlocked(bit))return;
    _global.music[bit/8]|=uint8_t(1u<<(bit%8));_global_dirty=true;++_music_revision;
}
void save_system::observe(story_scene& scene,bool ready)
{
    save_runtime={unsigned(_view),unsigned(ready ? _paint : 0),unsigned(_view<2 && _view>=0 ? _selected[_view] : _pause),unsigned(_first),unsigned(_confirm),unsigned(_confirm_choice),unsigned(_operation),unsigned(_store.busy() || _operation!=none),_store.progress(),unsigned(_store.available()),_seed_size,unsigned(scene.script()),0,0,save_runtime.draw_work,save_runtime.thumb_work,save_runtime.snapshot_work,unsigned(_row_line)};
}
void save_system::tick(story_scene& scene,bool gameplay)
{
    observe(scene,true);
    if(_notice && !--_notice){_message="";if(active() && _paint==6)_paint=5;}
    if(!_global_checked)
    {
        _global_checked=true;
        if(_store.status(save_global_slot)==save_slot_status::valid && buffer() && _store.begin_read(save_global_slot,_buffer))_operation=global_read;
    }
    int script;unsigned pc;
    if(gameplay && scene.retry_identity(script,pc) && (script!=_seed_script || pc!=_seed_pc))
    {
        int count=scene.snapshot(_seed,sizeof(_seed),true);
        if(count>0){_seed_size=unsigned(count);_seed_script=script;_seed_pc=pc;_seed_dirty=true;}
    }
    int bad=gameplay ? scene.bad_end_id() : 0;
    if(bad>0 && bad<=13)
    {unsigned i=unsigned(bad-1),mask=1u<<(i%8);if(!(_global.endings[i/8]&mask)){_global.endings[i/8]|=uint8_t(mask);_global_dirty=true;}}
    int entry=gameplay ? scene.reading_book_entry() : -1;
    if(entry>=0){unsigned i=unsigned(entry),mask=1u<<(i%8);if(!(_global.book[i/8]&mask)){_global.book[i/8]|=uint8_t(mask);_global_dirty=true;}}
    if(_operation!=none && !_store.busy())
    {
        if(_operation==load_record){_load_ready=true;return;}
        bool failed=_store.failed();
        if(_operation==compact_read || _operation==compact_write)
        {
            if(_operation==compact_read && !failed)
            {
                unsigned length=_store.length(),seed=_store.seed_length();save_metadata metadata{};
                failed=(_compact_slot!=save_retry_slot && !_store.metadata(_compact_slot,metadata)) || !convert_loaded_record(length,seed);
                if(!failed && _store.begin_write(_compact_slot,_buffer,length,seed,metadata))
                {_operation=compact_write;return;}
                failed=true;
            }
            operation pending=_pending_operation;int slot=_pending_slot;
            _operation=_pending_operation=none;_pending_slot=_compact_slot=-1;
            if(failed){mark("保存失败");return;}
            if(pending==retry_write)
            {
                std::memcpy(_buffer+save_payload_offset,_seed,_seed_size);
                if(_store.begin_write(slot,_buffer,_seed_size,0,scene.save_info()))
                {_operation=retry_write;_seed_dirty=false;}
                else mark("保存失败");
            }
            else write(scene,slot,pending);
            return;
        }
        if(_operation==global_read && !failed && (_store.length()==92 || _store.length()==112 || _store.length()==sizeof(_global)))
        {
            // The first 92 bytes retain the pre-gallery global format.
            auto* saved=reinterpret_cast<global_data*>(_buffer+save_payload_offset);
            for(unsigned i=4;i<_store.length();++i)reinterpret_cast<uint8_t*>(&_global)[i]|=reinterpret_cast<uint8_t*>(saved)[i];
            _global.volume=saved->volume;_global.settings=saved->settings;_global.reserved=saved->reserved;
            sfx_player::set_master_volume(sound_level()*256);
            music_player::set_master_volume(_global.volume);++_gallery_revision;++_music_revision;
        }
        if(_operation==manual_write){if(!failed)play_ui_sound(ui_sound::save);_confirm=0;list(true);mark(failed ? "保存失败" : "已保存");_paint=1;}
        _operation=none;
        if(_view<0 || _view==2)free_buffer();
    }
    if(!_store.available() || _operation!=none)return;
    if(gameplay && scene.script()!=_auto_script && scene.auto_save_ready())
    {
        unsigned id=unsigned(scene.script());
        if(id<512){unsigned bit=1u<<(id%8);_global.scripts[id/8]|=uint8_t(bit);_global_dirty=true;}
        if(write(scene,save_auto_slot,auto_write))_auto_script=scene.script();
    }
    else if(gameplay && _seed_dirty && buffer())
    {
        if(_store.needs_compaction(save_retry_slot))
        {if(!compact(save_retry_slot,retry_write))mark("保存失败");return;}
        std::memcpy(_buffer+save_payload_offset,_seed,_seed_size);save_metadata metadata=scene.save_info();
        if(_store.begin_write(save_retry_slot,_buffer,_seed_size,0,metadata)){_operation=retry_write;_seed_dirty=false;}
    }
    else if(_global_dirty && !_editing_settings && (_view<0 || _view==2) && buffer())
    {
        std::memcpy(_buffer+save_payload_offset,&_global,sizeof(_global));save_metadata metadata{};
        if(_store.begin_write(save_global_slot,_buffer,sizeof(_global),0,metadata)){_operation=global_write;_global_dirty=false;}
    }
}
void save_system::pump()
{
    if(!_store.busy())return;
    _store.frame();unsigned begin=unsigned(bn::core::current_cpu_ticks());
    unsigned budget=active() && _operation==manual_write ? 512 : 128;
    for(int step=0;step<2048 && _store.busy() && unsigned(bn::core::current_cpu_ticks())<3500 && unsigned(bn::core::current_cpu_ticks())-begin<budget;++step)
    {_store.step();if(_store.waiting_erase())break;}
    if(active() && (_operation==manual_write || _operation==load_record) && int(_store.progress())!=_last_progress)
    {_last_progress=int(_store.progress());_paint=5;}
}
save_ui_result save_system::update(story_scene& scene,bool ready)
{
    if(_load_ready)
    {
        _load_ready=false;_operation=none;
        unsigned length=_store.length(),seed=_store.seed_length();
        bool good=!_store.failed();
#if !MANOSABA_SAVE_SRAM
        if(good && _store.version()==3)good=convert_loaded_record(length,seed);
#endif
        if(!good || (seed && seed!=save_checkpoint_bytes) || !scene.restore_snapshot(_buffer+save_payload_offset,int(length)))
        {mark("记录损坏");_confirm=0;list(true);return save_ui_result::none;}
        save_metadata metadata;
        _store.metadata(selected_slot(),metadata);
        scene.restore_play_time(metadata.play_frames);
        _seed_size=seed;
        if(seed)std::memcpy(_seed,_buffer+save_payload_offset+length,seed);
        if(_seed_size)
        {
            save_archive reader(_seed,int(_seed_size),true);uint8_t flags=0;reader.fixed(flags);reader.fixed(_seed_script);reader.fixed(_seed_pc);_seed_dirty=true;
        }
        else {_seed_script=-1;_seed_pc=0;}
        play_ui_sound(ui_sound::load);_auto_script=scene.script();close();return save_ui_result::loaded;
    }
    bool locked=(_store.busy() || _operation!=none) && (_view<2 || _operation==manual_write || _operation==load_record);
    bool action=_advance.update(bn::keypad::a_held(),ready && _paint==6 && _preview_row<0 && !locked,1+bn::core::last_missed_frames());
    if(locked)return save_ui_result::none;
    if(bn::keypad::l_pressed() || bn::keypad::r_pressed())play_ui_sound(ui_sound::tab);
    else if(bn::keypad::up_pressed() || bn::keypad::down_pressed() || bn::keypad::left_pressed() || bn::keypad::right_pressed())play_ui_sound(ui_sound::move);
    else if(action)play_ui_sound(ui_sound::confirm);
    else if(bn::keypad::b_pressed() || bn::keypad::start_pressed())play_ui_sound(ui_sound::cancel);
    if(_confirm)
    {
        if(bn::keypad::left_pressed() || bn::keypad::right_pressed()){_confirm_choice^=1;_paint=2;}
        if(bn::keypad::b_pressed() || (action && !_confirm_choice)){_confirm=0;list(true);}
        else if(action)
        {
            int slot=selected_slot();
            if(_confirm==1){if(write(scene,slot,manual_write))_paint=5;}
            else if(buffer() && _store.begin_read(slot,_buffer)){_operation=load_record;_paint=5;}
        }
        return save_ui_result::none;
    }
    if(_view==2)
    {
        int count=_failure ? 3 : 4;
        if(bn::keypad::up_pressed()){_pause=(_pause+count-1)%count;_paint=2;}
        if(bn::keypad::down_pressed()){_pause=(_pause+1)%count;_paint=2;}
        if(bn::keypad::b_pressed() || bn::keypad::start_pressed()){close();return save_ui_result::resume;}
        if(action)
        {
            if(!_failure && !_pause){close();return save_ui_result::resume;}
            if(_failure && !_pause)
            {
                if(!_seed_size){mark("没有重试记录");return save_ui_result::none;}
                if(scene.restore_snapshot(_seed,int(_seed_size))){close();return save_ui_result::loaded;}
            }
            else if(_pause==(_failure ? 2 : 3)){return save_ui_result::settings;}
            else
            {
                _view=_failure || _pause==2 ? 1 : 0;
                if(!_view && !_remembered)
                {for(int i=0;i<save_manual_slots;++i)if(_store.status(i)==save_slot_status::empty){_selected[0]=i;break;}_remembered=true;}
                list(true);_paint=0;
            }
        }
        return save_ui_result::none;
    }
    if(_load_only && (bn::keypad::b_pressed() || bn::keypad::start_pressed())){close();return save_ui_result::resume;}
    if(bn::keypad::b_pressed())
    {for(int i=0;i<2;++i){_thumbs[i].reset();_thumb_tiles[i].reset();_thumb_palettes[i].reset();}_view=2;_paint=0;_preview_row=-1;_preview_line=0;_advance.reset();return save_ui_result::none;}
    if(!_load_only && !_failure && (bn::keypad::l_pressed() || bn::keypad::r_pressed())){_view^=1;list(true);}
    int total=save_manual_slots+int(_view==1);
    if(bn::keypad::up_pressed()){_selected[_view]=(_selected[_view]+total-1)%total;list();}
    if(bn::keypad::down_pressed()){_selected[_view]=(_selected[_view]+1)%total;list();}
    if(action)
    {
        int slot=selected_slot();auto status=_store.status(slot);
        if(!_store.available()){mark("存储不可用");return save_ui_result::none;}
        if(!_view)
        {
            if(!scene.save_allowed()){mark("此场景不可保存");return save_ui_result::none;}
            if(status==save_slot_status::empty){if(write(scene,slot,manual_write))_paint=5;}
            else {_confirm=1;_confirm_choice=0;_paint=0;_advance.reset();}
        }
        else if(status==save_slot_status::valid){_confirm=2;_confirm_choice=0;_paint=0;_advance.reset();}
        else mark(status==save_slot_status::empty ? "空白记录" : status==save_slot_status::incompatible ? "版本不兼容" : "记录损坏");
    }
    return save_ui_result::none;
}
void save_system::key(text_canvas& canvas,int x,int y,int index,const char* label)
{
    const auto& icon=save_ui_keys[index];canvas.stamp(x,y,icon.width,icon.height,icon.pixels);
    if(label)canvas.text(x+icon.width+1,y+1,label,text_canvas::white,238,y+14);
}
void save_system::heading(text_canvas& canvas,story_scene& scene)
{
    canvas.palette_color(3,scene.hiro() ? uint16_t(27|7<<5|11<<10) : uint16_t(31|16<<5|21<<10));
    if(_view==2)
    {
        canvas.initial(104,6,save_ui_initials[0],text_canvas::accent);canvas.initial(120,6,save_ui_initials[1],text_canvas::accent);
        char title[48];chapter(title,sizeof(title),scene.save_info(),true);canvas.text(105,30,title,text_canvas::muted,230,43);return;
    }
    bool single=_load_only || _failure;
    for(int tab=single ? 1 : 0;tab<2;++tab)
    {
        int x=single ? 80 : 32+92*tab,y=tab==_view ? 4 : 8;
        auto edge=tab==_view ? text_canvas::accent : ink(11);
        auto fill=tab==_view ? ink(8) : text_canvas::background;
        for(int row=0;row<4;++row)
        {
            int inset=4-row;
            canvas.rectangle(x+inset,y+row,81-inset*2,1,fill);
            canvas.rectangle(x+inset,y+row,1,1,edge);canvas.rectangle(x+80-inset,y+row,1,1,edge);
        }
        canvas.rectangle(x,y+4,81,26-y,fill);
        canvas.rectangle(x+4,y,73,1,edge);canvas.rectangle(x,y+4,1,26-y,edge);canvas.rectangle(x+80,y+4,1,26-y,edge);canvas.rectangle(x,29,81,1,edge);
        if(tab==_view){canvas.initial(x+23,8,save_ui_initials[tab+2],text_canvas::accent);canvas.text(x+39,12,"档",text_canvas::white,220,25);}
        else canvas.text(x+28,12,tab ? "读档" : "存档",text_canvas::muted,220,25);
    }
    if(!single){key(canvas,7,18,2,nullptr);key(canvas,215,18,3,nullptr);}
}
bool save_system::row_text(text_canvas& canvas,int y,const char* text,int color)
{
    if(!_line_offset)_line_x=78;
    int end=_line_offset;
    for(int count=0;count<8 && text[end];++count)
    {unsigned byte=uint8_t(text[end]);end+=byte<128 ? 1 : byte<224 ? 2 : byte<240 ? 3 : 4;}
    text_canvas::cursor cursor{_line_x,y};
    const char* reached=canvas.text(cursor,{78,y,225,y+13},text+_line_offset,text+end,text_canvas::ink(color));
    bool done=reached!=text+end || !text[end];_line_offset=done ? 0 : end;_line_x=cursor.x;return done;
}
bool save_system::row(text_canvas& canvas,int row)
{
    int index=_first+row,y=34+row*54,slot=_view==1 ? (index ? index-1 : save_auto_slot) : index;
    if(!_row_line)
    {
        canvas.restore(8,y/8*8,224,56);
        _thumbs[row].reset();_thumb_tiles[row].reset();_thumb_palettes[row].reset();_row_line=1;return false;
    }
    bool selected=index==_selected[_view];
    auto& meta=_rows[row];char label[40];
    if(_row_line==1)
    {
        if(selected)
        {
            canvas.rectangle(10,y+2,218,1,text_canvas::accent);canvas.rectangle(10,y+47,218,1,text_canvas::accent);
            canvas.rectangle(10,y+2,2,46,text_canvas::accent);canvas.rectangle(227,y+2,1,46,text_canvas::accent);
        }
        if(slot==save_auto_slot)copy(label,sizeof(label),"自动");else copy(label,sizeof(label),digits(unsigned(slot+1)));
        canvas.text(17,y+5,label,selected ? text_canvas::accent : text_canvas::muted,76,y+18);
        auto status=_store.status(slot);
        if(status!=save_slot_status::valid || !_store.metadata(slot,_rows[row]))
        {
            const char* name=status==save_slot_status::empty ? "空白记录" : status==save_slot_status::incompatible ? "版本不兼容" : "记录损坏";
            canvas.text(81,y+15,name,text_canvas::muted,225,y+29);_row_line=0;return true;
        }
        meta.summary[sizeof(meta.summary)-1]=0;
        _row_line=2;return false;
    }
    if(_row_line==2)
    {chapter(label,sizeof(label),meta);if(row_text(canvas,y+5,label,text_canvas::white))_row_line=3;return false;}
    if(_row_line==3)
    {if(row_text(canvas,y+19,meta.summary,text_canvas::white))_row_line=4;return false;}
    auto stamp=saved_at(meta);
    if(meta.timestamp.valid && meta.trial)copy(label,sizeof(label),bn::format<48>("{} 余{}:{}",stamp,digits(unsigned(meta.trial_left/3600)),digits(unsigned(meta.trial_left/60%60))));
    else copy(label,sizeof(label),stamp);
    if(!row_text(canvas,y+32,label,text_canvas::muted))return false;
    _preview_row=row;_preview_line=0;_row_line=0;return true;
}
void save_system::thumbnail(text_canvas&,int row,const save_metadata& meta)
{
    work_timer timer{save_runtime.thumb_work};
    if(meta.preview==65535){_preview_row=-1;return;}
    auto* pixels=reinterpret_cast<uint8_t*>(thumbnail_pixels[row]);
    const save_preview_colors* colors=nullptr;bool court=(meta.preview&0xc000)==0x4000,reenact=(meta.preview&0xc000)==0x8000;
    const reenact_pose* pose=nullptr;
    if(reenact){const auto& asset=reenact_assets[meta.preview&0x3fff];pose=&asset.poses[asset.pose_count-1];}
    if(!court && !reenact)
    {
        for(int i=0;i<save_background_count;++i)if(save_background_colors[i].image==meta.preview){colors=&save_background_colors[i];break;}
        if(!colors){_preview_row=-1;return;}
    }
    if(!_preview_line)std::memset(thumbnail_pixels[row],0,sizeof(thumbnail_pixels[row]));
    if(_preview_line<24)
    {
        for(int y=_preview_line;y<_preview_line+4;++y)
        {
            uint8_t samples[48];int py=y*160/24;
            for(int x=0;x<48;++x)
            {
                unsigned value;
                if(court)value=save_court_thumbs[meta.preview&1].pixels[y*48+x];
                else if(reenact)
                {
                    int px=x*5;unsigned tile=pose->background[py/8*30+px/8];
                    value=(reenact_tiles[tile*32+(py%8)*4+px%8/2]>>((px&1)*4))&15;
                }
                else
                {
                    const auto& variant=graphic_assets[meta.preview].variants[0];int px=x*5+136,sy=py+176;
                    unsigned tile=affine_cell(graphic_assets[meta.preview],variant,px/8,sy/8);
                    value=colors->map[graphics_bytes[(variant.tiles[tile]&0x7fffffffu)+(sy%8)*8+px%8]];
                }
                samples[x]=uint8_t(value);
            }
            if(reenact)for(int p=0;p<pose->count;++p)
            {
                const auto& part=pose->parts[p];if(py<part.y || py>=part.y+part.height)continue;
                int sy=py-part.y;
                for(int x=(part.x+4)/5;x<48 && x*5<part.x+part.width;++x)
                {
                    int sx=x*5-part.x;unsigned tile=part.tiles[sy/8*(part.width/8)+sx/8];
                    unsigned value=(reenact_tiles[tile*32+(sy%8)*4+sx%8/2]>>((sx&1)*4))&15;
                    if(value)samples[x]=uint8_t(value);
                }
            }
            for(int x=0;x<48;++x)
            {unsigned offset=(y/8*8+x/8)*32+(y%8)*4+x%8/2;pixels[offset]|=uint8_t(samples[x]<<((x&1)*4));}
        }
        _preview_line+=4;return;
    }
    auto shape=bn::sprite_shape_size(64,32);
    const auto* palette=reenact ? reenact_palette : court ? save_court_thumbs[meta.preview&1].palette : colors->palette;
    _thumb_palettes[row]=bn::sprite_palette_ptr::create_new(bn::sprite_palette_item(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(palette),16),bn::bpp_mode::BPP_4));
    _thumb_tiles[row]=bn::sprite_tiles_ptr::allocate(32,bn::bpp_mode::BPP_4);
    video_upload::copy(thumbnail_pixels[row],_thumb_tiles[row]->vram()->data(),1024);
    _thumbs[row]=bn::sprite_ptr::create(-71,34+row*54+20+16-80,shape,*_thumb_tiles[row],*_thumb_palettes[row]);
    _thumbs[row]->set_bg_priority(0);_preview_row=-1;_preview_line=0;
}
void save_system::draw(text_canvas& canvas,story_scene& scene)
{
    work_timer timer{save_runtime.draw_work};
    if(_view<0)return;
    save_runtime.paint=0;
    // The mixer has already run. Defer this small UI step on its heavier frames.
    if(!video_upload::finishing && bn::core::current_cpu_ticks()>1800)return;
    if(_store.busy() && _view<2)
    {
        if(_paint==5){canvas.rectangle(0,144,240,16,text_canvas::background);char text[32];copy(text,sizeof(text),bn::format<48>("{} {}%",_operation==load_record ? "读取中" : "保存中",_store.progress()));canvas.text(78,145,text,text_canvas::muted,238,159);_paint=6;}
        return;
    }
    if(_preview_row>=0){thumbnail(canvas,_preview_row,_rows[_preview_row]);return;}
    if(_paint==0)
    {for(int i=0;i<2;++i){_thumbs[i].reset();_thumb_tiles[i].reset();_thumb_palettes[i].reset();}_painted_pause=-1;_confirm_static=false;_confirm_stage=0;_header_restored=false;_paint=1;}
    int background=(_view==2 || _confirm) ? 0 : 1;
    if(!canvas.save_page(save_ui_backgrounds[background],save_ui_palette))return;
    if(_paint==1)
    {
        if(!_header_restored){canvas.restore(0,0,240,32);_header_restored=true;return;}
        heading(canvas,scene);_paint=2;return;
    }
    if(_confirm)
    {
        if(_paint==2 && !_confirm_static)
        {
            char text[48];auto& old=_rows[0];
            if(!_confirm_stage)
            {
                int slot=selected_slot();_store.metadata(slot,old);
                copy(text,sizeof(text),slot==save_auto_slot ? bn::string<48>("读取自动记录？") : bn::format<48>(_confirm==1 ? "覆盖记录 {}？" : "读取记录 {}？",digits(unsigned(slot+1))));
                canvas.text(66,53,text,text_canvas::white,224,67);
            }
            else if(_confirm_stage==1)
            {
                auto stamp=saved_at(old);
                canvas.text(63,34,stamp.c_str(),text_canvas::muted,224,48);
            }
            else if(_confirm_stage==2)
            {chapter(text,sizeof(text),old);canvas.text(36,73,text,text_canvas::muted,224,87);}
            else if(_confirm_stage==3)
            {chapter(text,sizeof(text),scene.save_info());canvas.text(36,89,text,text_canvas::white,224,103);}
            if(++_confirm_stage==4){_confirm_static=true;_paint=3;}
            return;
        }
        if(_paint==2 || _paint==3)
        {
            canvas.rectangle(41,107,67,20,_confirm_choice ? ink(8) : text_canvas::selection);canvas.rectangle(129,107,67,20,_confirm_choice ? text_canvas::selection : ink(8));
            canvas.text(62,111,"取消",_confirm_choice ? text_canvas::muted : text_canvas::white,110,125);
            canvas.text(150,111,_confirm==1 ? "覆盖" : "读取",_confirm_choice ? text_canvas::white : text_canvas::muted,200,125);_paint=5;
        }
    }
    else if(_view==2)
    {
        if(_paint==2)
        {
            if(_painted_pause>=0)canvas.restore(24,(52+_painted_pause*19)/8*8,192,24);
            const char* normal[]={"继续游戏","存档","读档","设置"};const char* failed[]={retry_choice() ? "重新选择" : "重新审问","读档","设置"};
            int count=_failure ? 3 : 4;
            for(int i=0;i<count;++i)
            {
                int y=53+i*19;const char* label=_failure ? failed[i] : normal[i];bool selected=i==_pause;
                if(selected){canvas.rectangle(25,y-1,190,16,text_canvas::selection);canvas.rectangle(25,y-1,2,16,text_canvas::accent);}
                auto color=(_failure && !i && !retry_available()) ? text_canvas::muted : selected ? text_canvas::accent : text_canvas::white;
                canvas.text(bn::string_view(label).size()==6 ? 108 : 96,y,label,color,218,y+13);
            }
            _painted_pause=_pause;_paint=5;
        }
    }
    else if(_paint==2 || _paint==3){if(row(canvas,_paint-2))++_paint;return;}
    if(_paint==4)
    {
        int total=save_manual_slots+int(_view==1),selected=_selected[_view];canvas.restore(232,32,8,112);
        canvas.rectangle(234,35,1,99,ink(10));int height=198/total;canvas.rectangle(234,35+(99-height)*selected/(total-1),2,height,text_canvas::muted);_paint=5;
    }
    if(_paint==5)
    {
        canvas.restore(0,144,240,16);
        if(_notice)canvas.text(72,145,_message,text_canvas::muted,238,159);
        else if(_view==2 || _confirm){key(canvas,66,144,0,"确认");key(canvas,133,144,1,"返回");}
        else
        {
            canvas.text(8,145,"\ue003选择",text_canvas::muted,63,159);key(canvas,63,144,0,_view ? "读取" : "存入");key(canvas,131,144,1,"返回");
            char count[16];copy(count,sizeof(count),bn::format<16>("{}/{}",_selected[_view]+1,save_manual_slots+int(_view==1)));canvas.text(199,145,count,text_canvas::muted,239,159);
        }
        _paint=6;
    }
}
}
