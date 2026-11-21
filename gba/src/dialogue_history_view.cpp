#include "dialogue_history_view.h"
#include "save_archive.h"
#include "dialogue_history_runtime.h"
#include "text_canvas.h"
#include "trial_ui_data.h"
#include "nameplate_data.h"
#include "bn_keypad.h"
#include "bn_core.h"
#include "bn_string.h"
#include "bn_string_view.h"
extern "C" {BN_DATA_EWRAM_BSS dialogue_history_profile dialogue_history_runtime={};}
namespace manosaba
{
void dialogue_history_view::archive(save_archive& a)
{
    bool opened=_open;int scroll=_scroll;a.field(opened);a.field(scroll);
    if(a.reading()){close();if(opened){open();_scroll=scroll;repaint();}}
}
void dialogue_history_view::open()
{
    _open=true;_rows=0;
    for(int i=0;i<_vm.history_count();++i){const auto& entry=_vm.history(i);_rows+=entry.lines+1+int(entry.speaker!=0);_ends[i]=uint16_t(_rows);}
    _rows=_rows ? _rows-1 : 1;_scroll=bn::max(0,_rows-8);repaint();
}
void dialogue_history_view::close()
{
    if(_open && _canvas)_canvas->reader_close();
    _open=false;_reader.reset();
}
void dialogue_history_view::repaint()
{
    _paint=0;_cached=-1;_footer=false;_reader.reset();
}
bool dialogue_history_view::update()
{
    if(bn::keypad::b_pressed() || bn::keypad::l_pressed())return true;
    if(_reader.update()){_scroll=_reader.first();_paint=603;_footer=true;}
    return false;
}
void dialogue_history_view::line(void* context,text_canvas& canvas,int row)
{static_cast<dialogue_history_view*>(context)->line(canvas,row);}
void dialogue_history_view::line(text_canvas& canvas,int row)
{
    if(row>=_rows){canvas.reader_line(row,"",text_canvas::white);return;}
    if(!_vm.history_count()){canvas.reader_line(row,"暂无对话记录",text_canvas::muted);return;}
    int low=0,high=_vm.history_count();
    while(low<high){int mid=(low+high)/2;if(_ends[mid]<=row)low=mid+1;else high=mid;}
    int local=row-(low ? _ends[low-1] : 0);const auto& entry=_vm.history(low);
    if(entry.speaker && !local)
    {
        const char* name=story_speakers[entry.speaker];uint16_t color=19|(18<<5)|(18<<10);
        for(int i=0;i<nameplate_count;++i)if(bn::string_view(name)==nameplates[i].speaker){color=nameplates[i].color;break;}
        // Nine rows are live; ten name slots leave three fixed source-text colors.
        static constexpr int inks[]={2,3,4,5,7,8,9,10,11,15};int ink=inks[row%10];
        canvas.reader_color(ink,color);canvas.reader_line(row,name,text_canvas::ink(ink));return;
    }
    local-=int(entry.speaker!=0);
    if(local>=entry.lines){canvas.reader_line(row,"",text_canvas::white);return;}
    if(_cached!=low){_vm.history_text(low,_text);_cached=low;}
    const char* value=_text;
    for(int i=0;i<local;++i){while(*value && *value!='\n')++value;if(*value)++value;}
    const auto* style=_vm.history_style(low);
    if(style)canvas.reader_color(12+style->color,story_text_colors[style->color]);
    canvas.reader_line(row,value,text_canvas::white,false,style,value-_text);
}
void dialogue_history_view::footer(text_canvas& canvas)
{
    canvas.rectangle(132,144,108,16,text_canvas::shadow);
    auto value=bn::to_string<20>(_scroll+1);value+="-";value+=bn::to_string<8>(bn::min(_scroll+8,_rows));
    value+="/";value+=bn::to_string<8>(_rows);
    canvas.text(238-value.size()*6,145,value.c_str(),text_canvas::white,239,158);
}
void dialogue_history_view::draw(text_canvas& canvas)
{
    _canvas=&canvas;
    do
    {
        if(_paint<600)
        {
            int count=bn::min(32,600-_paint);
            canvas.reader_picture(trial_choice_pictures[0],trial_history_remap,_paint,count);_paint+=count;
        }
        else if(_paint==600)
        {
            canvas.text(96,4,"对话记录",text_canvas::white);
            canvas.rectangle(4,144,232,16,text_canvas::shadow);
            canvas.text(8,145,"\ue003滚动 \ue004翻页",text_canvas::white);footer(canvas);_paint=601;
        }
        else if(_paint==601)
        {_reader.begin(24,8,_rows,_scroll);_paint=603;}
        else if(_paint==603)
        {
            _reader.draw(canvas,this,&dialogue_history_view::line);
            if(_footer){footer(canvas);_footer=false;}
            if(_reader.ready())_paint=604;
            if(_reader.yielded())return;
        }
    }while(dirty() && bn::core::current_cpu_ticks()+1600<2100);
}
void dialogue_history_view::observe(bool ready) const
{
    dialogue_history_runtime={unsigned(_open),unsigned(_vm.history_count()),unsigned(_scroll),unsigned(_rows),
        unsigned(_open && !dirty() && ready),unsigned(_vm.script()),_vm.statement_left(),_vm.trial_left()};
}
}
