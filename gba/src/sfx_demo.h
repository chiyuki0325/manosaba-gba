#ifndef MANOSABA_SFX_DEMO_H
#define MANOSABA_SFX_DEMO_H
namespace manosaba
{
class text_canvas;
class sfx_demo
{
  public:
    void reset();
    bool update();
    void draw(text_canvas& canvas);

  private:
    int _selected = 0, _voices = -1, _paint = 0, _status_phase = 0;
    bool _loop = false, _music = false, _status = true;
};
} // namespace manosaba
#endif
