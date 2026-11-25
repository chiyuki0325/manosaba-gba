#ifndef WITCH_BOOK_MAP_H
#define WITCH_BOOK_MAP_H
#include "story_vm.h"
#include "text_canvas.h"
#include "book_map_data.h"
namespace manosaba
{
class witch_book_map
{
  public:
    explicit witch_book_map(story_vm& vm) : _vm(vm)
    {
    }
    void open(bool forced, bool normal);
    void archive(save_archive& data, bool active);
    void close();
    void reset_view()
    {
        close();
        _floor = _floor_count = _count = _mode = 0;
        _restore_floor = -1;
        for (auto& i : _positions)
            i = 0;
    }
    bool yielded() const
    {
        return _yield;
    }
    int update(bool action, bool ready); // -1 close, 1 submit
    void draw(text_canvas& canvas, uint8_t* block, const uint8_t* shared, uint8_t* cache);
    void repaint()
    {
        _paint = 0;
        _full = true;
        _loaded_image = -1;
        _mini_row = _clear_row = 0;
        _highlight = -1;
    }
    bool dirty() const
    {
        return _paint != 612;
    }
    int paint() const
    {
        return _paint;
    }
    void tab_painted()
    {
        ++_paint;
    }
    int count() const
    {
        return _count;
    }
    int selected() const
    {
        return _positions[image()];
    }
    int source() const
    {
        return _sources[_areas[selected()]];
    }
    int mode() const
    {
        return _mode;
    }
    int floor() const
    {
        return _floor_count ? _floors[_floor] : -1;
    }
    int area() const
    {
        return _count ? _areas[selected()] : -1;
    }
    int left() const
    {
        return _left;
    }
    int top() const
    {
        return _top;
    }

  private:
    story_vm& _vm;
    int8_t _sources[33];
    alignas(4) uint16_t _palette[16];
    uint8_t _floors[4], _areas[22], _positions[4] = {};
    int _floor = 0, _floor_count = 0, _count = 0, _mode = 0, _paint = 0, _block = 0, _mini_row = 0, _clear_row = 0;
    int _restore_floor = -1;
    int _left = 0, _top = 0, _view_left = 0, _view_top = 0, _label_x = 0, _label_y = 0, _label_width = 0, _mini_x = 182,
        _mini_y = 27;
    bool _forced = false, _normal = false, _full = true, _yield = false, _floor_change = false, _tag_visible = false;
    int _loaded_image = -1, _highlight = -1, _highlight_bank = 0, _scroll_frame = 0, _scroll_duration = 0,
        _from_left = 0, _from_top = 0;
    int _drawn_floor = -1, _drawn_count = 0, _drawn_mode = 0, _drawn_mini_x = 0, _drawn_mini_y = 0;
    int _drawn_label_x = 0, _drawn_label_y = 0, _drawn_label_width = 0, _drawn_px = 0, _drawn_py = 0;
    text_canvas* _canvas = nullptr;
    void refresh();
    void remember();
    void clear_tag(text_canvas& canvas);
    void tag_position();
    bool visible_tag() const;
    void mini_cursor(text_canvas& canvas, bool restore);
    void clear_rect(text_canvas& canvas, int x, int y, int width, int height);
    int image() const
    {
        return _floor_count ? map_floors[_floors[_floor]].image : 0;
    }
    const map_area* point() const
    {
        return _count ? &map_areas[_areas[selected()]] : nullptr;
    }
    const char* name() const
    {
        return point() ? point()->name : "一楼设计图";
    }
    void places();
    void layout(bool center = true);
    void key(text_canvas& canvas, const uint8_t* shared, int x, int y, int index, const char* label = nullptr);
    void decorate(text_canvas& canvas, const uint8_t* shared, int part);
};
} // namespace manosaba
#endif
