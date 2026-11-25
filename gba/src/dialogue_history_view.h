#ifndef DIALOGUE_HISTORY_VIEW_H
#define DIALOGUE_HISTORY_VIEW_H
#include "story_vm.h"
#include "text_reader.h"
namespace manosaba
{
class text_canvas;
class dialogue_history_view
{
  public:
    explicit dialogue_history_view(story_vm& vm) : _vm(vm)
    {
    }
    void open();
    void archive(save_archive& data);
    void close();
    void repaint();
    bool active() const
    {
        return _open;
    }
    bool painting() const
    {
        return dirty() && !_reader.moving();
    }
    bool dirty() const
    {
        return _paint != 604;
    }
    bool update();
    void draw(text_canvas& canvas);
    void observe(bool ready) const;

  private:
    story_vm& _vm;
    text_canvas* _canvas = nullptr;
    text_reader _reader;
    uint16_t _ends[story_history_capacity];
    char _text[story_text_capacity];
    int _rows = 0, _paint = 0, _cached = -1, _scroll = 0;
    bool _open = false, _footer = false;
    static void line(void* context, text_canvas& canvas, int row);
    void line(text_canvas& canvas, int row);
    void footer(text_canvas& canvas);
};
} // namespace manosaba
#endif
