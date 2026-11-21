#include "credits_player.h"
#include "credits_runtime.h"
#include "text_canvas.h"
#include "bn_core.h"
#include "bn_algorithm.h"
#include "bn_array.h"
#include "bn_tile.h"
#include "bn_sprite_tiles_item.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_palette_item.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_sprite_shape_size.h"
#include "sfx_player.h"

namespace {
// Four shared 32x32 transparent speckle patterns; no runtime pixel generation.
constexpr auto make_noise()
{
    bn::array<bn::tile,64> tiles{};uint32_t random=0x391541u;
    for(auto& tile:tiles)for(auto& word:tile.data)
    {
        uint32_t pixels=0;
        for(int x=0;x<8;++x)
        {
            random^=random<<13;random^=random>>17;random^=random<<5;
            if((random&31)==0)pixels|=(1+(random>>8)%3)<<(x*4);
        }
        word=pixels;
    }
    return tiles;
}
constexpr auto noise_tiles=make_noise();
constexpr bn::color noise_colors[16]={bn::color(0,0,0),bn::color(9,9,9),bn::color(17,17,17),bn::color(25,25,25)};
const bn::sprite_tiles_item noise_item(noise_tiles,bn::bpp_mode::BPP_4,4);
void noise_sound(bool stop=false)
{
    manosaba::sfx_command sound;sound.cue=manosaba::sfx_player::find("Ambi_101_1");
    sound.volume=384;sound.stop=stop;sound.ambient=true;
    manosaba::sfx_player::execute(sound);
}
}
extern "C" {BN_DATA_EWRAM_BSS credits_runtime_profile credits_runtime={};}
namespace manosaba {
void credits_player::suspend()
{
    if(!_noise.empty())noise_sound(true);
    _noise.clear();_noise_tick=-1;_opened=_shown=false;
}
void credits_player::begin(int act,bool story)
{
    suspend();
    _act=act;_entry=&credits_entries[act-1];_pixel=_fill=_wait=_reverse_frame=0;
    _phase=forward;_first=0;_last=-1;_opened=_shown=false;
    credits_runtime={};credits_runtime.active=1;credits_runtime.act=unsigned(act);credits_runtime.total=_entry->count;credits_runtime.source=unsigned(story);
}
void credits_player::draw_noise()
{
    if(_phase!=hold && _phase!=reverse)
    {if(!_noise.empty()){_noise.clear();_noise_tick=-1;}return;}
    // Prepare while the last credit holds, at two objects per frame.
    for(int budget=2;budget && _noise.size()<40;--budget)
    {
        int i=_noise.size();
        auto tiles=bn::sprite_tiles_ptr::create(noise_item,i%4);
        auto palette=bn::sprite_palette_ptr::create(bn::sprite_palette_item(noise_colors,bn::bpp_mode::BPP_4));
        auto sprite=bn::sprite_ptr::create(i%8*32-104,i/8*32-64,bn::sprite_shape_size(32,32),tiles,palette);
        sprite.set_bg_priority(0);sprite.set_visible(false);_noise.push_back(bn::move(sprite));
        _noise_tick=-1;
    }
    int tick=_phase==reverse ? _reverse_frame/3 : -2;
    if(_noise_tick==tick)return;
    _noise_tick=tick;
    bool visible=_phase==reverse && _shown && _reverse_frame%84<24;
    for(int i=0;i<_noise.size();++i)
    {
        auto& sprite=_noise[i];
        sprite.set_visible(visible);
        if(visible)
        {
            unsigned pattern=unsigned(tick*13+i*7);
            sprite.set_tiles(noise_item,int((pattern^(pattern>>2))&3));
            sprite.set_horizontal_flip(pattern&4);sprite.set_vertical_flip(pattern&8);
        }
    }
}
bool credits_player::update(bool ready)
{
    auto& p=credits_runtime;
    ++p.frame;p.core_ticks=unsigned(bn::core::last_cpu_ticks());p.max_ticks=bn::max(p.max_ticks,p.core_ticks);p.missed+=unsigned(bn::core::last_missed_frames());
    p.phase=unsigned(_phase);p.pixel=unsigned(_pixel>>8);p.row=unsigned((_pixel>>8)/14);p.ready=unsigned(ready && _shown);p.done=unsigned(done());
    if(!_shown)return true;
    if(!ready || done())return false;
    int frames=1+bn::core::last_missed_frames();
    if(_phase==forward)
    {
        int row=(_pixel>>8)/14;unsigned flags=_entry->rows[row]>>24;p.flags=flags;
        int limit=(_entry->count*14-160)*256;
        _pixel=bn::min(limit,_pixel+((flags&credits_fast) ? 512 : 128)*frames);
        if(_pixel==limit){_phase=_act==1 ? hold : ending;_wait=_act==1 ? 84 : 60;}
    }
    else if(_phase==reverse)
    {
        int previous=_reverse_frame;
        _reverse_frame=bn::min(420,_reverse_frame+frames);
        _pixel=_reverse_from*(420-_reverse_frame)/420;
        if(_reverse_frame==420){noise_sound(true);_phase=ending;_wait=54;}
        else if(previous/84!=_reverse_frame/84)noise_sound();
    }
    else
    {
        _wait=bn::max(0,_wait-frames);
        if(!_wait)
        {
            if(_phase==hold){_phase=reverse;_reverse_frame=0;_reverse_from=_pixel;noise_sound();}
            else {_phase=finished;p.done=1;p.active=0;}
        }
    }
    return true;
}
void credits_player::paint(text_canvas& canvas,int row)
{
    uint32_t value=row<_entry->count ? _entry->rows[row] : 0;
    canvas.reader_line(row,row<_entry->count ? credits_text+(value&0xffffffu) : "",value>>24&credits_heading ? text_canvas::accent : text_canvas::white,value>>24&credits_center);
}
void credits_player::draw(text_canvas& canvas)
{
    unsigned start=unsigned(bn::core::current_cpu_ticks());
    if(!_opened)
    {
        canvas.reader_open(0,160,true,_act==1);_opened=true;_shown=false;_fill=0;
        _first=(_pixel>>8)/14;_last=_first-1;return;
    }
    if(!_shown)
    {
        if(_fill<18){paint(canvas,_first+_fill++);_last=_first+_fill-1;}
        else if(canvas.ready()){canvas.reader_position(_pixel>>8);canvas.reader_show();_shown=true;}
    }
    else
    {
        int first=(_pixel>>8)/14;
        if(first<_first){paint(canvas,first);_first=first;_last=first+17;}
        else if(first+17>_last){paint(canvas,_last+1);++_last;_first=_last-17;}
        canvas.reader_position(_pixel>>8);
    }
    draw_noise();
    credits_runtime.draw_ticks=unsigned(bn::core::current_cpu_ticks())-start;
    credits_runtime.max_draw=bn::max(credits_runtime.max_draw,credits_runtime.draw_ticks);
}
}
