#pragma once
namespace manosaba
{
class text_canvas;
class save_system;
class music_room
{
  public:
    void open();
    void suspend();
    bool update(bool ready, const save_system& progress);
    void draw(text_canvas& canvas, const save_system& progress);

  private:
    int _selected = 0, _playing = -1, _phase = 0, _fill = 0, _collected = 0;
    unsigned _dirty = 255, _revision = ~0u;
    bool _shown = false;
    void row(text_canvas& canvas, int row, const save_system& progress);
    void status(text_canvas& canvas);
    void key(text_canvas& canvas, int x, int index, const char* label, bool disabled = false);
    void controls(text_canvas& canvas, bool disabled);
};
} // namespace manosaba
