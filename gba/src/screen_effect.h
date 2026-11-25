#ifndef MANOSABA_SCREEN_EFFECT_H
#define MANOSABA_SCREEN_EFFECT_H
#include "save_archive.h"
namespace manosaba
{
struct palette_effect
{
    uint16_t color = 0, amount = 0;
}; // RGB555 and Q8 opacity.
inline palette_effect effect_over(palette_effect bottom, palette_effect top)
{
    if (!top.amount)
        return bottom;
    int under = bottom.amount * (256 - top.amount) / 256, total = top.amount + under;
    unsigned color = 0;
    for (int shift = 0; shift < 15; shift += 5)
        color |= unsigned((((top.color >> shift) & 31) * top.amount + ((bottom.color >> shift) & 31) * under) / total)
                 << shift;
    return {uint16_t(color), uint16_t(total)};
}
class screen_effect
{
  public:
    void start(uint16_t color, int amount, int frames, bool inherit_color)
    {
        for (int i = 0; i < 4; ++i)
        {
            _from[i] = _current[i];
            _target[i] = uint16_t(i == 3 ? amount : inherit_color ? _current[i] : (color >> (i * 5)) & 31);
            if (!frames)
                _current[i] = _target[i];
        }
        _duration = _left = uint16_t(frames);
    }
    void tick(int frames)
    {
        if (!_left)
            return;
        _left = uint16_t(frames < _left ? _left - frames : 0);
        for (int i = 0; i < 4; ++i)
            _current[i] = uint16_t(_target[i] + (int(_from[i]) - _target[i]) * _left / _duration);
    }
    palette_effect value() const
    {
        return {uint16_t(_current[0] | _current[1] << 5 | _current[2] << 10), _current[3]};
    }
    void archive(save_archive& a)
    {
        // Save the destination color, not a transient animation cursor.
        for (auto& value : _target)
            a.field(value);
        if (a.reading())
        {
            for (int i = 0; i < 4; ++i)
                _current[i] = _from[i] = _target[i];
            _duration = _left = 0;
        }
    }
    void checkpoint(save_archive& a)
    {
        for (auto& value : _target)
            a.fixed(value);
        if (a.reading())
        {
            for (int i = 0; i < 4; ++i)
                _current[i] = _from[i] = _target[i];
            _duration = _left = 0;
        }
    }

  private:
    uint16_t _current[4] = {31, 31, 31, 0}, _from[4] = {31, 31, 31, 0}, _target[4] = {31, 31, 31, 0};
    uint16_t _duration = 0, _left = 0;
};
} // namespace manosaba
#endif
