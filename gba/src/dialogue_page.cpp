#include "dialogue_page.h"
#include "text_breaks.h"
#include "save_archive.h"

namespace manosaba
{
void dialogue_page::archive(save_archive& a, const char* text)
{
    a.field(_start);
    a.field(_end);
    a.field(_visible);
    a.field(_glyph_begin);
    a.field(_fading);
    if (a.reading())
        _text = text;
}
namespace
{
int next_character(const char* text, int at)
{
    auto first = static_cast<unsigned char>(text[at]);
    return at + (first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4);
}
} // namespace

void dialogue_page::begin(const char* text, int offset, const dialogue_layout& box, bool original_breaks)
{
    _text = text;
    _start = _visible = _glyph_begin = offset;
    _fading = false;
    int x = box.left, y = box.top, at = offset;
    while (text[at])
    {
        if (text[at] == '\r' && !original_breaks)
        {
            if (x > box.left && soft_break_space(text + at))
                x += dialogue_ascii_width;
            ++at;
            continue;
        }
        if (text[at] == '\n' || text[at] == '\r')
        {
            x = box.left;
            y += dialogue_line_height;
            ++at;
            continue;
        }
        bool control =
            static_cast<unsigned char>(text[at]) == 0xee && static_cast<unsigned char>(text[at + 1]) == 0x80 &&
            static_cast<unsigned char>(text[at + 2]) >= 0x80 && static_cast<unsigned char>(text[at + 2]) <= 0x84;
        int width = control                                      ? 13
                    : static_cast<unsigned char>(text[at]) < 128 ? dialogue_ascii_width
                                                                 : dialogue_wide_width;
        if (x + width > box.right)
        {
            x = box.left;
            y += dialogue_line_height;
        }
        if (y + 12 > box.bottom)
            break;
        x += width;
        at = next_character(text, at);
    }
    _end = at;
}

reveal_step dialogue_page::tick()
{
    if (_fading)
    {
        _fading = false;
        return reveal_step::brighten;
    }
    if (_visible == _end)
        return reveal_step::none;
    _glyph_begin = _visible;
    while (_visible < _end && (_text[_visible] == '\n' || _text[_visible] == '\r'))
        ++_visible;
    if (_visible < _end)
    {
        _visible = next_character(_text, _visible);
        _fading = true;
    }
    return reveal_step::append;
}

void advance_button::reset()
{
    _released = 0;
    _cooldown = 8;
    _armed = false;
}

bool advance_button::update(bool held, bool ready, int frames)
{
    bool pressed = held && !_held;
    _held = held;
    _cooldown = _cooldown > frames ? _cooldown - frames : 0;
    if (!ready)
    {
        _released = 0;
        _armed = false;
        return false;
    }
    if (!held)
    {
        if (_released < 2)
            ++_released;
        if (_released == 2)
            _armed = true;
        return false;
    }
    _released = 0;
    if (!pressed)
        return false;
    bool accepted = _armed && !_cooldown;
    _armed = false;
    if (accepted)
        _cooldown = 8;
    return accepted;
}
} // namespace manosaba
