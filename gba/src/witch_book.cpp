#include "witch_book.h"
#include "save_archive.h"
#include "witch_book_runtime.h"
#include "trial_ui_data.h"
#include "choice_window.h"
#include "book_theme.h"
#include "lzss.h"
#include "bn_keypad.h"
#include "bn_core.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include <cstring>
extern "C" {BN_DATA_EWRAM_BSS witch_book_profile witch_book_runtime={};}
namespace manosaba {
namespace {
BN_DATA_EWRAM_BSS uint8_t book_shared[book_shared_size];
alignas(4) BN_DATA_EWRAM_BSS uint8_t book_pixels[216*120];
alignas(4) BN_DATA_EWRAM_BSS uint8_t book_block[4096];
constexpr int preview_bytes=76*76,photo_offset=preview_bytes*3;
int shared_offset=0;
int rows(const char* text){int count=1;for(;*text;++text)count+=*text=='\n';return count;}
const char* row_at(const char* text,int row)
{
    while(row-- && *text){while(*text && *text!='\n')++text;if(*text)++text;}
    return text;
}
void line(text_canvas& canvas,int x,int y,const char* value,int width,text_canvas::ink color)
{
    const char* end=value;while(*end && *end!='\n')++end;
    text_canvas::cursor cursor{x,y};canvas.text(cursor,{x,y,x+width,y+13},value,end,color);
}
}
void witch_book::open(bool forced)
{
    _open=true;_forced=forced;_map=_vm.map_menu();_mode=_scroll=0;
    if(forced)_tab=_map ? 2 : _vm.inventory_kind();
    _advance.reset();list();
}
void witch_book::archive(save_archive& a)
{
    int mode=_mode,scroll=_scroll;
    a.field(_open);a.field(_forced);a.field(_map);a.field(_tab);a.field(mode);a.field(scroll);
    for(auto& i:_selected)a.field(i);
    for(auto& i:_first)a.field(i);
    if(a.reading() && _open)
    {
        list();_mode=mode;_scroll=scroll;_reader_started=false;_reader.reset();
        request_image();repaint();_advance.reset();
    }
    _maps.archive(a,_open && _tab==2);
}
void witch_book::observe(bool paused,bool ready) const
{
    unsigned frames=witch_book_runtime.frames+1,missed=witch_book_runtime.missed+unsigned(bn::core::last_missed_frames());
    witch_book_runtime={unsigned(_open),unsigned(_mode),unsigned(_tab),unsigned(count()),unsigned(selected()),
        unsigned(_tab==3 ? _scroll : _first[_tab]),unsigned(_image_offset<38400 || shared_offset<book_shared_size),unsigned(_open && !dirty() && ready),
        unsigned(paused),unsigned(_vm.script()),unsigned(_vm.state()),_vm.statement_left(),_vm.trial_left(),_vm.camera_frame(),unsigned(_tab==2 ? _maps.paint() : _paint),frames,missed,
        _maps.floor(),_maps.area(),_maps.left(),_maps.top(),unsigned(count() ? source() : 0)};
}
void witch_book::list()
{
    _count=0;
    if(_tab==2)
    {invalidate_images();_image=nullptr;_image_offset=38400;_width=_height=0;_maps.open(_forced,_map);_mode=0;_advance.reset();return;}
    _maps.close();
    if(_forced && (_map || _vm.inventory_kind()==2))
    {
        int count=_map ? _vm.option_count() : _vm.inventory_count();
        for(int i=0;i<count;++i)
        {
            _entries[_count]=int16_t(-i-1);_sources[_count++]=uint8_t(i);
        }
    }
    else for(int i=0;i<book_entry_count;++i)
        if(book_entries[i].kind==_tab && _vm.book_version(i))
        {_entries[_count]=int16_t(i);_sources[_count]=uint8_t(_forced ? _count : i);++_count;}
    _selected[_tab]=_count ? bn::min(_selected[_tab],_count-1) : 0;
    if(_tab==3)
    {
        _rule_total=0;
        for(int i=0;i<_count;++i)
        {
            int index=_entries[i];const auto& item=book_entries[index];
            _rule_lengths[i]=rows(book_texts[item.first+_vm.book_version(index)-1].detail);
            _rule_total+=_rule_lengths[i]+2;
        }
        if(_rule_total)--_rule_total;
    }
    window();_scroll=0;request_image();repaint();
}
void witch_book::window()
{
    if(!_count){_first[_tab]=0;return;}
    int heights[book_entry_count/2],count=(_count+1)/2;
    for(int i=0;i<count;++i)heights[i]=16;
    int first=bn::min(_first[_tab]/2,bn::max(0,count-2));
    auto visible=visible_choices(heights,count,_selected[_tab]/2,first,0,32);
    _first[_tab]=visible.first*2;
}
const book_text* witch_book::content() const
{
    int index=entry();if(index<0)return nullptr;
    const auto& item=book_entries[index];int version=_vm.book_version(index);
    return &book_texts[item.first+version-1];
}
int witch_book::rule_rows() const {return _rule_total;}
const char* witch_book::rule_line(int row,bool& title) const
{
    title=false;
    for(int i=0;i<_count;++i)
    {
        int index=_entries[i];const auto& item=book_entries[index];
        const auto& text=book_texts[item.first+_vm.book_version(index)-1];int count=_rule_lengths[i];
        if(!row){title=true;return text.name;}
        if(row<=count)return row_at(text.detail,row-1);
        if(row==count+1)return "";
        row-=count+2;
    }
    return "";
}
const char* witch_book::name() const
{
    if(!_count)return "暂无条目";
    if(const auto* text=content())return text->name;
    return _map ? _vm.option(source()).text : story_items[_vm.item_id(source())].label;
}
void witch_book::request_image()
{
    _cache_pending=-1;_image=nullptr;_image_offset=38400;_width=_height=0;
    std::memset(_colors,0,sizeof(_colors));std::memcpy(_colors,book_page_palette,64);
    book_theme(_colors,hiro_book_theme(_vm));
    int index=entry();if(index<0 || _mode==1 || _tab==3)return;
    int x=0,y=0,w=0,h=0;
    const auto& item=book_entries[index];
    if(item.kind==0)
    {
        for(int i=0;i<trial_evidence_count;++i)
            if(bn::string_view(item.id)==trial_evidence_pictures[i].id)
            {
                const auto& asset=trial_evidence_pictures[i];_image=&asset.image;
                x=asset.x;y=asset.y;w=asset.width;h=asset.height;break;
            }
    }
    else for(int i=0;i<book_image_count;++i)
        if(book_images[i].entry==index)
        {
            const auto& asset=book_images[i];_image=&asset.image;
            x=asset.x;y=asset.y;w=asset.width;h=asset.height;break;
        }
    if(!_image)return;
    std::memcpy(_colors+32,_image->palette+32,224*2);
    int max_w=_mode==2 ? 216 : item.kind==2 ? 216 : 76;
    int max_h=_mode==2 ? 120 : item.kind==2 ? 96 : 76;
    if(w*max_h>h*max_w){_width=bn::min(w,max_w);_height=bn::max(1,h*_width/w);}
    else {_height=bn::min(h,max_h);_width=bn::max(1,w*_height/h);}
    _native_image=_width==w && _height==h;
    for(int i=0;i<_width;++i)_source_x[i]=uint16_t(x+i*w/_width);
    for(int i=0;i<_height;++i)_source_y[i]=uint16_t(y+i*h/_height);
    _preview_x=_mode==2 || item.kind==2 ? (240-_width)/2 : 13+(76-_width)/2;
    _preview_y=_mode==2 ? 18+(120-_height)/2 : item.kind==2 ? 27+(96-_height)/2 : 32+(76-_height)/2;
    if(_mode==2)
    {
        _pixel_offset=0;
        if(_large==_image)return;
        invalidate_images();
    }
    else
    {
        _large=nullptr;
        for(int i=0;i<3;++i)if(_previews[i]==_image){_pixel_offset=i*preview_bytes;return;}
        _cache_pending=_cache_next;_cache_next=(_cache_next+1)%3;
        _previews[_cache_pending]=nullptr;_pixel_offset=_cache_pending*preview_bytes;
    }
    _image_blocks=0;
    for(int row=0;row<_height;++row)
    {
        int sy=_source_y[row],base=sy/8*1920+(sy%8)*8;
        int first=(base+_source_x[0]/8*64+_source_x[0]%8)/4096;
        int last=(base+_source_x[_width-1]/8*64+_source_x[_width-1]%8)/4096;
        for(int block=first;block<=last;++block)_image_blocks|=1u<<block;
    }
    _image_offset=0;_decode_row=-1;
    while(!(_image_blocks&(1u<<(_image_offset/4096))))_image_offset+=4096;
    std::memset(book_pixels+_pixel_offset,0,_width*_height);
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void witch_book::decode_image()
{
    int amount=bn::min(4096,38400-_image_offset);
    if(_decode_row<0)
    {
        if(_image->raw)std::memcpy(book_block,reinterpret_cast<const uint8_t*>(_image->raw)+_image_offset,amount);
        else
        {
            const auto& block=_image->blocks[_image_offset/4096];
            if(block.codec)lzss_decode(block.data,book_block,amount);else std::memcpy(book_block,block.data,amount);
        }
        _decode_row=0;return;
    }
    int rows=0;
    for(;_decode_row<_height;++_decode_row)
    {
        int y=_decode_row;
        int sy=_source_y[y],row=sy/8*1920;
        if(row+1920<=_image_offset || row>=_image_offset+amount)continue;
        if(rows++==8)return;
        auto* out=book_pixels+_pixel_offset+y*_width;
        if(_native_image)
        {
            for(int x=0;x<_width;)
            {
                int sx=_source_x[0]+x,count=bn::min(8-sx%8,_width-x);
                int offset=row+sx/8*64+(sy%8)*8+sx%8-_image_offset;
                if(unsigned(offset)<unsigned(amount))
                    for(int i=0;i<count;++i){int ink=book_block[offset+i];out[x+i]=ink>=32 ? uint8_t(ink) : 0;}
                x+=count;
            }
            continue;
        }
        for(int x=0;x<_width;++x)
        {
            int sx=_source_x[x],offset=row+sx/8*64+(sy%8)*8+sx%8-_image_offset;
            if(unsigned(offset)<unsigned(amount))out[x]=book_block[offset]>=32 ? book_block[offset] : 0;
        }
    }
    _decode_row=-1;
    _image_offset+=amount;
    while(_image_offset<38400 && !(_image_blocks&(1u<<(_image_offset/4096))))_image_offset+=bn::min(4096,38400-_image_offset);
    if(_image_offset==38400)
    {
        if(_cache_pending>=0){_previews[_cache_pending]=_image;_cache_pending=-1;}
        else _large=_image;
    }
}
void witch_book::invalidate_images()
{
    for(auto& preview:_previews)preview=nullptr;
    _photo_tab=-1;_large=nullptr;_cache_pending=-1;
}
void witch_book::patch(int paint){_partial_list=paint==620;_heading_done=false;_paint=paint;_line=_stamp_row=0;_advance.reset();}
void witch_book::repaint()
{
    _text_total=_tab==3 ? rule_rows() : 0;
    if(_tab<2 && _mode==1){const auto* text=content();_text_total=text ? rows(text->detail) : 1;}
    _reader_started=false;_reader.reset();_partial_list=false;_heading_done=false;
    _paint=_line=_stamp_row=0;if(_tab==2)_maps.repaint();_advance.reset();
}
book_result witch_book::update(bool ready)
{
    bool action=_advance.update(bn::keypad::a_held(),ready && !dirty());
    if(bn::keypad::select_pressed() && !_forced)return book_result::close;
    if(_tab==2)
    {
        if(!_mode && !_forced && (bn::keypad::l_pressed() || bn::keypad::r_pressed()))
        {_tab=(_tab+(bn::keypad::r_pressed() ? 1 : 3))%4;list();return book_result::none;}
        int result=_maps.update(action,ready);_mode=_maps.mode();
        return result<0 ? book_result::close : result ? book_result::submit : book_result::none;
    }
    if(_tab==3)
    {
        if(bn::keypad::b_pressed())return book_result::close;
        if(bn::keypad::l_pressed() || bn::keypad::r_pressed())
        {_tab=(_tab+(bn::keypad::r_pressed() ? 1 : 3))%4;list();return book_result::none;}
        scroll_input();
        return book_result::none;
    }
    if(bn::keypad::b_pressed())
    {
        if(_mode){--_mode;request_image();repaint();return book_result::none;}
        if(!_forced || (!_map && _vm.inventory_back()))return book_result::close;
    }
    if(!_mode)
    {
        if(!_forced && (bn::keypad::l_pressed() || bn::keypad::r_pressed()))
        {_tab=(_tab+(bn::keypad::r_pressed() ? 1 : 3))%4;list();return book_result::none;}
        int previous=_selected[_tab];
        if(_count)
        {
            int next=previous;
            if(bn::keypad::left_pressed())--next;
            else if(bn::keypad::right_pressed())++next;
            else if(bn::keypad::up_pressed())next-=2;
            else if(bn::keypad::down_pressed())next+=2;
            next=(next+_count)%_count;
            if(next!=previous)
            {
                _selected[_tab]=next;
                window();request_image();patch(_tab==3 || _photo_tab==_tab ? 620 : 0);
            }
            if(action && previous==next){_mode=1;_scroll=0;request_image();repaint();}
        }
    }
    else if(_mode==1)
    {
        if(action)
        {
            if(_forced)return book_result::submit;
            if(_tab<3){_mode=2;request_image();repaint();}
        }
        else if(bn::keypad::r_pressed() && _tab<3){_mode=2;request_image();repaint();}
        else scroll_input();
    }
    return book_result::none;
}
void witch_book::scroll_input()
{
    if(!_reader_started)return;
    if(_reader.update()){_scroll=_reader.first();_paint=610;_reader_footer=true;}
}
void witch_book::reader_line(void* context,text_canvas& canvas,int row)
{static_cast<witch_book*>(context)->reader_line(canvas,row);}
void witch_book::reader_line(text_canvas& canvas,int row)
{
    bool title=false;const auto* text=content();
    const char* value=_tab==3 ? rule_line(row,title) : row_at(text ? text->detail : "",row);
    canvas.reader_line(row,value,title ? text_canvas::accent : text_canvas::white);
}
void witch_book::footer_count(text_canvas& canvas)
{
    canvas.rectangle(184,144,52,16,text_canvas::dark);
    if(_tab==3)
    {
        int total=rule_rows();auto position=bn::to_string<20>(total ? _scroll+1 : 0);
        position+="-";position+=bn::to_string<8>(bn::min(_scroll+8,total));
        position+="/";position+=bn::to_string<8>(total);
        canvas.text(238-position.size()*6,145,position.c_str(),text_canvas::muted,239,158);
    }
    else
    {
        int total=_reader_started ? _text_total : content() ? rows(content()->detail) : 1;
        auto page=bn::to_string<12>(_scroll/7+1);page+="/";page+=bn::to_string<8>((total+6)/7);
        canvas.text(194,145,page.c_str(),text_canvas::muted,239,158);
    }
}
void witch_book::stamp(text_canvas& canvas,int x,int y,int index)
{
    const auto& art=book_stamps[index];canvas.stamp(x,y,art.width,art.height,book_shared+art.offset);
}
void witch_book::key(text_canvas& canvas,int x,int y,int index,const char* label)
{
    stamp(canvas,x,y,index+3);
    if(label)canvas.text(x+book_stamps[index+3].width+1,y,label,text_canvas::white);
}
void witch_book::initial(text_canvas& canvas,int x,int y,const char*& value,text_canvas::ink color)
{
    unsigned code=uint8_t(*value++);
    if(code>=128)
    {
        int count=code<224 ? 1 : code<240 ? 2 : 3;code&=count==1 ? 31 : count==2 ? 15 : 7;
        while(count--)code=(code<<6)|(uint8_t(*value++)&63);
    }
    for(int i=0;i<book_initial_count;++i)if(book_initials[i].code==code)
    {canvas.initial(x,y,book_initials[i].rows,color);return;}
}
void witch_book::tabs(text_canvas& canvas,int first)
{
    static constexpr const char* labels[]={"证物","人物","地图","规定"};
    for(int i=first;i<first+2;++i)
    {
        int x=23+i*49,top=i==_tab ? 1 : 5;
        canvas.rectangle(x+3,top,41,1,text_canvas::warning);
        for(int y=top+1;y<23;++y)
        {
            int inset=bn::max(0,top+3-y);
            canvas.rectangle(x+inset,y,47-inset*2,1,text_canvas::warning);
            if(y<22)canvas.rectangle(x+inset+1,y,45-inset*2,1,text_canvas::ink(i==_tab ? 9 : 11));
        }
        const char* label=labels[i];
        if(i==_tab){initial(canvas,x+8,4,label,text_canvas::accent);canvas.text(x+25,7,label);}
        else canvas.text(x+11,8,label,_forced ? text_canvas::warning : text_canvas::muted);
    }
    if(!_mode && !_forced && first==2){key(canvas,1,8,2);key(canvas,221,8,3);}
}
void witch_book::heading(text_canvas& canvas)
{
    if(!_count || _tab==3)return;
    if(_mode)
    {canvas.text(12,_mode==2 ? 1 : 27,name(),text_canvas::white,228,_mode==2 ? 14 : 40);return;}
    if(_tab==2){if(entry()<0)canvas.text(12,28,name(),text_canvas::white,228,41);return;}
    if(entry()>=0 && book_entries[entry()].kind==1)
    {
        canvas.rectangle(100,26,136,26,text_canvas::ink(9));stamp(canvas,100,26,2);
        // Character names keep their own representative ink; the tab stays pink.
        const char* value=name();initial(canvas,106,29,value,text_canvas::ink(8));canvas.text(123,33,value,text_canvas::white,235,46);
    }
    else
    {
        const char* value=name();initial(canvas,106,29,value,text_canvas::white);canvas.text(123,33,value,text_canvas::white,235,46);
    }
}
void witch_book::footer(text_canvas& canvas,int part)
{
    if(_tab==3)
    {
        if(part)return;
        canvas.rectangle(4,144,232,16,text_canvas::dark);
        canvas.text(8,145,"\ue003滚动 \ue004翻页",text_canvas::muted);
        footer_count(canvas);return;
    }
    if(!part)canvas.rectangle(4,_mode ? 144 : 126,232,_mode ? 16 : 34,text_canvas::dark);
    if(_mode)
    {
        if(part)return;
        if(_mode==2){key(canvas,10,145,1,"返回");return;}
        else
        {
            if(_forced)key(canvas,10,145,0,_tab==2 ? "选定" : "出示");
            else if(_tab<3)key(canvas,10,145,0,"看图");
            key(canvas,73,145,1,"返回");
            if(_forced && _tab<3 && entry()>=0)key(canvas,132,145,3,"看图");
        }
        footer_count(canvas);return;
    }
    if(!part)return;
    int i=_first[_tab]+part-1;
    if(i<_count)
    {
        int col=(i-_first[_tab])%2,row=(i-_first[_tab])/2,x=7+col*114,y=127+row*16;
        if(i==_selected[_tab])canvas.rectangle(x,y,112,16,text_canvas::selection);
        int saved=_selected[_tab];_selected[_tab]=i;const char* value=name();
        // Reserve a full cell for the ellipsis, never clip half a glyph.
        int pixels=0;const char* end=value;
        while(*end)
        {unsigned c=uint8_t(*end);int width=c<128 ? 6 : 12;if(pixels+width>84)break;pixels+=width;end+=c<128 ? 1 : c<224 ? 2 : c<240 ? 3 : 4;}
        if(*end)
        {
            text_canvas::cursor cursor{x+10,y+1};canvas.text(cursor,{x+10,y+1,x+107,y+14},value,end,text_canvas::white);
            canvas.text(cursor.x,y+1,"…",text_canvas::white,x+112,y+14);
        }
        else canvas.text(x+10,y+1,value,text_canvas::white,x+112,y+14);
        _selected[_tab]=saved;
        if(i==saved)canvas.text(x,y+1,">",text_canvas::accent,x+8,y+14);
    }
    if(part!=4)return;
    if(_first[_tab])canvas.text(230,126,"^",text_canvas::muted,239,139);
    if(_first[_tab]+4<_count)canvas.text(230,145,"v",text_canvas::muted,239,158);
    auto count=bn::to_string<12>(_count ? _selected[_tab]+1 : 0);count+="/";count+=bn::to_string<8>(_count);
    canvas.text(238-count.size()*6,113,count.c_str(),text_canvas::muted,239,126);
}
int witch_book::work_cost() const
{
    if(shared_offset<book_shared_size)return 1400;
    if(_tab==2)
    {
        int paint=_maps.paint();
        if(paint==600 || paint==601)return 2200;
        if(paint==603 || paint==604 || paint==607 || paint==608 || paint==609)return 1400;
        if(paint==630)return 1200;
        if(paint==615 || paint==616 || paint==618 || paint==620)return 2200;
        return 1000;
    }
    if(_paint==600 || _paint==601)return _mode==2 ? 0 : 2200;
    if(_paint==603)
    {
        if(_image_offset<38400)return _decode_row<0 ? 700 : 800;
        return _image && _stamp_row<_height ? 1000 : 0;
    }
    if(_paint==620)return _image_offset<38400 ? (_decode_row<0 ? 700 : 800) : 200;
    if(_paint==621)return 1500;
    if(_paint==622 || _paint==623)return 1400;
    if(_paint==610 && _reader.moving())return 2200;
    if(_paint==610 && (_partial_list || _mode==1 || _tab==3))return 1400;
    if(_paint==604)return _heading_done ? 0 : 1800;
    if(_paint==605 || _paint==625)return 1400;
    if(_paint==610 || (_paint>605 && _paint<610))return 900;
    return 700;
}
void witch_book::draw(text_canvas& canvas)
{
    _canvas=&canvas;_yield=false;
    // Leave headroom for the real mixer's busiest sequencer frames.
    do {draw_step(canvas);} while(dirty() && !_yield && !(_tab==2 && _maps.yielded()) && bn::core::current_cpu_ticks()+work_cost()<2600);
}
void witch_book::draw_step(text_canvas& canvas)
{
    if(shared_offset<book_shared_size)
    {
        int amount=bn::min(4096,book_shared_size-shared_offset);
        const auto& block=book_shared_blocks[shared_offset/4096];
        if(block.codec)lzss_decode(block.data,book_shared+shared_offset,amount);else std::memcpy(book_shared+shared_offset,block.data,amount);
        shared_offset+=amount;return;
    }
    if(_tab==2)
    {
        if(_maps.paint()==600 || _maps.paint()==601)
        {tabs(canvas,_maps.paint()==600 ? 0 : 2);_maps.tab_painted();}
        else _maps.draw(canvas,book_block,book_shared,book_pixels);
        return;
    }
    if(_paint==620)
    {
        // Decode into a free cache slot while the previous page remains visible.
        if(_image_offset<38400){decode_image();return;}
        if(entry()>=0 && book_entries[entry()].kind==1)_colors[8]=book_entries[entry()].color;
        canvas.book_colors(_colors);_paint=621;return;
    }
    if(_paint==621)
    {
        int count=bn::min(8,76-_stamp_row),top=32+_stamp_row;
        canvas.stamp(13,top,76,count,book_pixels+photo_offset+_stamp_row*76);
        int first=bn::max(top,_preview_y),last=bn::min(top+count,_preview_y+_height);
        if(_image && last>first)canvas.stamp(_preview_x,first,_width,last-first,
            book_pixels+_pixel_offset+(first-_preview_y)*_width);
        _stamp_row+=count;if(_stamp_row==76){_stamp_row=0;_paint=622;}return;
    }
    if(_paint==622 || _paint==623)
    {
        int height=_paint==622 ? 26 : 15,count=bn::min(8,height-_stamp_row);
        canvas.book_restore(book_shared,100,(_paint==622 ? 26 : 111)+_stamp_row,140,count);
        _stamp_row+=count;
        if(_stamp_row==height){_stamp_row=0;_paint=_paint==622 ? 623 : 604;}
        return;
    }
    if(_paint==618)
    {
        const auto& art=book_stamps[_tab];
        int end=bn::min(_stamp_row+8,76);
        for(int y=_stamp_row;y<end;++y)for(int x=0;x<76;++x)
        {
            int px=13+x,py=32+y,offset=(py/8*30+px/8)*32+(py%8)*4+(px%8)/2;
            int ink=book_shared[art.offset+(y+7)*art.width+x+8];
            book_pixels[photo_offset+y*76+x]=uint8_t(ink ? ink : 16+((book_shared[offset]>>((px&1)*4))&15));
        }
        _stamp_row=end;if(end==76){_photo_tab=_tab;_paint=603;_stamp_row=0;}return;
    }
    if(_paint<600)
    {
        if(!_paint && entry()>=0 && book_entries[entry()].kind==1)_colors[8]=book_entries[entry()].color;
        int count=bn::min(32,600-_paint);canvas.book_page(book_shared,_colors,_paint,count,reader());_paint+=count;return;
    }
    if(_paint==600)
    {
        if(_mode!=2)tabs(canvas,0);
        _paint=601;return;
    }
    if(_paint==601)
    {
        if(_mode!=2)tabs(canvas,2);
        _paint=602;return;
    }
    if(_paint==602)
    {
        if(!_mode && _tab<2)
        {
            const auto& art=book_stamps[_tab];int count=bn::min(8,art.height-_stamp_row);
            canvas.stamp(5,25+_stamp_row,art.width,count,book_shared+art.offset+_stamp_row*art.width);
            _stamp_row+=count;if(_stamp_row<art.height)return;_stamp_row=0;
        }
        _paint=!_mode && _tab<2 && _photo_tab!=_tab ? 618 : 603;return;
    }
    if(_paint==603)
    {
        if(_image_offset<38400){decode_image();return;}
        if(_image && _stamp_row<_height)
        {
            int count=bn::min(bn::max(1,768/_width),_height-_stamp_row);
            canvas.stamp(_preview_x,_preview_y+_stamp_row,_width,count,book_pixels+_pixel_offset+_stamp_row*_width);
            _stamp_row+=count;return;
        }
        _paint=604;return;
    }
    if(_paint==604){if(!_heading_done)heading(canvas);_paint=605;return;}
    if(_paint<610 && _paint>=605)
    {footer(canvas,_paint-605);_paint=_mode || _tab==3 ? 610 : _paint+1;return;}
    if(_paint==610)
    {
        if(reader())
        {
            if(!_reader_started)
            {_reader.begin(_tab==3 ? 28 : 45,_tab==3 ? 8 : 7,_text_total,_scroll);_reader_started=true;}
            _reader.draw(canvas,this,&witch_book::reader_line);
            if(_reader_footer){footer_count(canvas);_reader_footer=false;}
            if(_reader.ready())_paint=611;
            _yield=_reader.yielded();return;
        }
        const auto* text=content();const char* value=text ? (_mode==1 ? text->detail : text->compact) : "";
        int limit=_mode==1 ? 7 : !_mode && _tab!=2 ? 4 : 0;
        if(_line<limit)
        {
            if(_partial_list)canvas.book_restore(book_shared,100,55+_line*14,140,14);
            line(canvas,_mode ? 12 : 104,(_mode ? 45 : 55)+_line*14,row_at(value,_line),_mode ? 216 : 120,text_canvas::white);++_line;
        }
        if(_line==limit)
        {
            if(!_mode && limit && *row_at(value,4))canvas.text(104,113,"更多…",text_canvas::muted,192,126);
            if(!_count)canvas.text(104,55,"暂无条目",text_canvas::muted,228,68);
            _paint=611;
        }
    }
}
}
