#ifndef TEXT_CANVAS_H
#define TEXT_CANVAS_H

#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_optional.h"
#include "story_layout.h"
#include "ui_picture.h"
#include "label_art.h"

namespace manosaba
{

// Native UI: 4bpp dialogue/maps, 8bpp full-screen choices and book pictures.
struct story_text_style;
class text_canvas
{
public:
    enum ink { background, white, muted, accent, selection, warning, shadow, dark };
    struct cursor { int x, y; };

    explicit text_canvas(bool compact=false);
    void clear(bool compact=false);
    void release();
    void visible(bool value){if(_bg)_bg->set_visible(value);}
    void trial_screen();
    void title_begin(bool timeup=false);
    void title_restore(const struct trial_title_sequence& sequence,int frame);
    void title_changes(const struct trial_title_frame& frame,int first,int count);
    void title_show() {_bg->set_visible(true);}
    void bad_end();
    void frame_corners(const uint32_t* tiles);
    bool picture(const ui_picture& image);
    bool save_page(const uint32_t* tiles,const uint16_t* palette);
    void prepare();
    void restore(int x,int y,int width,int height);
    void blit(int x,int y,int width,int height,const uint32_t* tiles);
    void rectangle(int x, int y, int width, int height, ink color);
    void book_page(const uint8_t* tiles,const uint16_t* palette,int first,int count,bool reader=false);
    void reader_open(int top,int height,bool full=false,bool black=false);
    void reader_picture(const ui_picture& image,const uint8_t* remap,int first,int count);
    void reader_close();
    void reader_line(int row,const char* text,ink color,bool centered=false,const story_text_style* style=nullptr,int offset=0);
    void reader_color(int index,uint16_t color);
    void reader_position(int pixel);
    void reader_show();
    void execution_screen();
    void execution_button(int pose,int level);
    void execution_fill(int pose,int from,int to);
    void execution_complete();
    void book_colors(const uint16_t* palette);
    void palette_color(int index,uint16_t color);
    void book_restore(const uint8_t* paper,int x,int y,int width,int height);
    void map_page(const uint16_t* palette,int first,int count,bool story=false);
    uint8_t* scratch();
    void map_hide(){if(_map_bg)_map_bg->set_visible(false);}
    void map_tint(uint16_t color,int amount);
    void map_position(int left,int top);
    void map_upload(const uint8_t* pixels,int first,int count);
    void map_cells(const uint16_t* cells);
    int map_cell_offset() const;
    bool map_pending() const {return _map_bytes || _map_cells_pending;}
    void map_capture(int row,int count);
    void map_hold_ui(bool held) {_map_hold_ui=held;}
    void map_reveal(bool atomic=true);
    void map_close();
    void stamp(int x,int y,int width,int height,const uint8_t* pixels,int stride=0);
    void initial(int x,int y,const uint16_t* rows,ink color);
    const char* text(int x, int y, const char* value, ink color = white, int right = 228, int bottom = 160,bool cast_shadow=true);
    const char* text(cursor& position, const dialogue_layout& box, const char* value, const char* end, ink color,bool cast_shadow=true,bool original_breaks=true);
    void styled_text(cursor& position,const dialogue_layout& box,const char* value,const char* end,ink color,
                     const story_text_style* style,int offset=0,bool history=false,bool original_breaks=true);
    void present();
    void decoration(const uint32_t* tiles);
    void toast(int id);
    void name_label(const label_art* art, uint16_t color);
    void category_label(int x,int y,const label_art& art,const uint8_t* inks,int color);
    void clock(int seconds,const label_art& frame,const uint8_t* inks);
    void flat_screen(const uint16_t* palette);
    void pattern_screen(const ui_pattern& pattern);
    void pattern_restore(const ui_pattern& pattern,int x,int y,int width,int height);
    void choice_screen();
    void choice_button(int y,const char* label,bool highlighted,bool bad,int height=40);
    int commit(int bytes);
    const uint16_t* bitmap_colors() const { return _colors; }
    bool ready() const { return _dirty_count == 0 && _picture_bytes==38400 && (!_save_tiles || _save_bytes==19200) && !map_pending() && !_map_hold_ui && !_map_atomic_ui && !_reader_dirty; }

private:
    bn::optional<bn::regular_bg_tiles_ptr> _tiles;
    bn::optional<bn::regular_bg_ptr> _bg;
    bn::optional<bn::regular_bg_tiles_ptr> _map_tiles;
    bn::optional<bn::regular_bg_ptr> _map_bg;
    bn::tile* _map_tile_vram=nullptr;
    uint16_t* _map_cell_vram=nullptr;
    const uint8_t* _map_data=nullptr;
    const uint16_t* _map_cells_data=nullptr;
    int _map_first=0,_map_bytes=0,_map_left=0,_map_top=0;
    bool _map_cells_pending=false,_map_hold_ui=false,_map_atomic_ui=false;
    int _palette_colors=256;
    bool _reader_active=false,_reader_drawing=false;
    bool _reader_full=false;
    bool _execution_ui=false;
    unsigned _reader_dirty=0;
    int _reader_top=0;
    bool _dirty[600] = {};
    bool _occupied[600] = {};
    int _dirty_count = 0;
    bool _graphical = false;
    bool _choice_ui = false;
    bool _trial_canvas=false;
    const ui_picture* _picture=nullptr;
    const uint32_t* _save_tiles=nullptr;
    int _save_bytes=0;
    const uint32_t* _decoration=nullptr;
    uint8_t _toast=0;
    int _picture_bytes=38400;
    alignas(4) uint16_t _colors[16];
    void allocate();

    void glyph(int x,int y,const uint16_t* rows,int width,ink color,bool cast_shadow);
    void dirty(int index);
    void graphical(bool enabled);
    void label_word(int x,int y,uint32_t value,const uint8_t* inks);
    void label_base(int x,int y,int width,const uint8_t* inks);
    void label_text(int x,int y,const label_art& art,const uint8_t* inks);
};

}

#endif
