#ifndef WITCH_BOOK_H
#define WITCH_BOOK_H
#include "story_vm.h"
#include "text_canvas.h"
#include "dialogue_page.h"
#include "witch_book_art.h"
#include "witch_book_map.h"
#include "text_reader.h"
namespace manosaba {
enum class book_result {none,close,submit};
class witch_book
{
public:
    explicit witch_book(story_vm& vm):_vm(vm),_maps(vm) {}
    void open(bool forced);
    void archive(save_archive& data);
    void close(){_maps.close();if(_canvas)_canvas->reader_close();_open=false;}
    bool active() const {return _open;}
    bool forced() const {return _forced;}
    int source() const {return _tab==2 ? _maps.source() : _sources[_selected[_tab]];}
    book_result update(bool ready);
    void draw(text_canvas& canvas);
    void repaint();
    void observe(bool paused,bool ready) const;
    bool dirty() const {return _tab==2 ? _maps.dirty() : _paint!=611;}
    int tab() const {return _tab;}
    int count() const {return _tab==2 ? _maps.count() : _count;}
    int selected() const {return _tab==2 ? _maps.selected() : _selected[_tab];}
    int mode() const {return _mode;}
    int reading_entry() const {return _open && _mode==1 && _tab!=2 ? entry() : -1;}
private:
    text_canvas* _canvas=nullptr;
    story_vm& _vm;
    witch_book_map _maps;
    int16_t _entries[book_entry_count];
    uint8_t _sources[book_entry_count];
    alignas(4) uint16_t _colors[256];
    uint16_t _source_x[216],_source_y[120];
    const ui_picture* _image=nullptr;
    const ui_picture* _previews[3]={};
    const ui_picture* _large=nullptr;
    int _pixel_offset=0,_cache_next=0,_cache_pending=-1,_photo_tab=-1;
    unsigned _image_blocks=0;
    int _rule_lengths[5]={},_rule_total=0;
    text_reader _reader;
    int _text_total=0;
    bool _reader_started=false,_yield=false,_reader_footer=false;
    int _tab=0,_count=0,_mode=0,_selected[4]={},_first[4]={};
    int _paint=0,_line=0,_scroll=0,_image_offset=38400,_stamp_row=0,_decode_row=-1;
    int _width=0,_height=0,_preview_x=0,_preview_y=0;
    bool _open=false,_forced=false,_map=false,_native_image=false,_heading_done=false,_partial_list=false;
    advance_button _advance;
    void list();
    void window();
    void request_image();
    void decode_image();
    void draw_step(text_canvas& canvas);
    int work_cost() const;
    int rule_rows() const;
    const char* rule_line(int row,bool& title) const;
    void patch(int paint);
    void scroll_input();
    static void reader_line(void* context,text_canvas& canvas,int row);
    void reader_line(text_canvas& canvas,int row);
    void footer_count(text_canvas& canvas);
    bool reader() const {return _tab==3 || (_mode==1 && _text_total>7);}
    void invalidate_images();
    int entry() const {return _count ? _entries[_selected[_tab]] : -1;}
    const char* name() const;
    const book_text* content() const;
    void tabs(text_canvas& canvas,int first);
    void heading(text_canvas& canvas);
    void footer(text_canvas& canvas,int part);
    void stamp(text_canvas& canvas,int x,int y,int index);
    void key(text_canvas& canvas,int x,int y,int index,const char* label=nullptr);
    void initial(text_canvas& canvas,int x,int y,const char*& text,text_canvas::ink color);
};
}
#endif
