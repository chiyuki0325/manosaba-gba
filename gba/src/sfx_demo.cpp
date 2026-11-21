#include "sfx_demo.h"
#include "sfx_player.h"
#include "sfx_data.h"
#include "music_player.h"
#include "text_canvas.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_core.h"
namespace manosaba {
void sfx_demo::reset(){sfx_player::reset();music_player::reset();_selected=0;_voices=-1;_paint=0;_status_phase=0;_status=true;_loop=_music=false;}
bool sfx_demo::update()
{
    int old=_selected;
    if(bn::keypad::up_pressed())_selected=(_selected+sfx_cue_count-1)%sfx_cue_count;
    if(bn::keypad::down_pressed())_selected=(_selected+1)%sfx_cue_count;
    if(bn::keypad::left_pressed())_selected=(_selected+sfx_cue_count-8)%sfx_cue_count;
    if(bn::keypad::right_pressed())_selected=(_selected+8)%sfx_cue_count;
    bool changed=old!=_selected;
    if(bn::keypad::a_pressed())
    {sfx_command c;c.cue=_selected;c.loop=_loop || sfx_cues[_selected].ambient;sfx_player::execute(c);changed=true;}
    if(bn::keypad::b_pressed() || bn::keypad::select_pressed()){sfx_player::reset();changed=true;}
    if(bn::keypad::l_pressed()){_loop=!_loop;changed=true;}
    if(bn::keypad::r_pressed())
    {_music=!_music;if(_music)music_player::play_test_track();else music_player::reset();changed=true;}
    int voices=sfx_player::active_voices();changed|=_voices!=voices;_voices=voices;
    if(old!=_selected)_paint=0;
    if(changed){_status=true;_status_phase=0;}
    return changed || _paint<11 || _status;
}
void sfx_demo::draw(text_canvas& canvas)
{
    if(bn::core::current_cpu_ticks()>1800)return;
    if(!_paint)
    {canvas.clear();_status=true;_status_phase=0;++_paint;canvas.present();return;}
    if(_paint==1)
    {canvas.rectangle(8,33,224,1,text_canvas::selection);canvas.text(8,134,"A播放 B停 L循环 R叠加HirO",text_canvas::muted);++_paint;canvas.present();return;}
    if(_paint==2)
    {canvas.text(8,146,"上下选择 左右跳8项 Start菜单",text_canvas::muted);++_paint;canvas.present();return;}
    if(_status)
    {
        if(!_status_phase)
        {
            canvas.rectangle(0,0,240,32,text_canvas::background);
            canvas.text(8,4,"音效 DEMO",text_canvas::accent);
            auto status=bn::to_string<24>(_selected+1);status+="/";status+=bn::to_string<8>(sfx_cue_count);
            canvas.text(174,4,status.c_str(),text_canvas::muted,238,16);_status_phase=1;
        }
        else
        {
            canvas.text(8,19,_music ? "HirO 开" : "HirO 关",text_canvas::muted);
            canvas.text(78,19,_loop ? "循环 开" : "循环 关",text_canvas::muted);
            auto voices=bn::to_string<8>(_voices<0 ? 0 : _voices);voices+="/2";
            canvas.text(180,19,voices.c_str(),text_canvas::accent);_status=false;
        }
        canvas.present();return;
    }
    int first=_selected/4*4;
    if(_paint<11)
    {
        int i=first+(_paint-3)/2;
        if(i<sfx_cue_count)
        {
            const auto& c=sfx_cues[i];int y=39+(i-first)*23;
            bool absent=c.sample<0 && !c.count;
            if((_paint-3)%2==0)
            {
                if(i==_selected)canvas.rectangle(6,y-1,228,23,text_canvas::selection);
                canvas.text(10,y,c.alias,absent ? text_canvas::muted : text_canvas::white,220,y+12);
            }
            else canvas.text(16,y+11,absent ? "按方案不播放" : c.label,text_canvas::muted,230,y+23);
        }
        ++_paint;
    }
    canvas.present();
}
}
