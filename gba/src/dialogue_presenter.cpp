#include "dialogue_presenter.h"
#include "story_vm.h"
#include "nameplate_data.h"
#include "bn_algorithm.h"
#include "bn_string_view.h"
#include "save_archive.h"

namespace manosaba
{
void dialogue_presenter::archive(save_archive& a, const char* text)
{
    a.field(_prepared);
    a.field(_original_breaks);
    _page.archive(a, text);
    if (a.reading())
    {
        _short_second_page = false;
        _guard.restart();
        repaint();
    }
}
void dialogue_presenter::begin(const char* text, const dialogue_layout& box, int offset)
{
    _page.begin(text, offset, box, _original_breaks);
    _short_second_page = false;
    if (_speed == 2)
        _page.finish();
    _delay = 0;
    _prepared = true;
    _guard.restart();
    repaint();
}

dialogue_advance dialogue_presenter::advance(const char* text, const dialogue_layout& box, bool timed)
{
    if (timed && _playback == dialogue_playback_mode::fast)
        return dialogue_advance::script;
    if (!complete())
    {
        finish();
        return dialogue_advance::finish;
    }
    if (!can_advance())
        return dialogue_advance::wait;
    if (text[_page.end()])
    {
        next_page(text, box);
        return dialogue_advance::page;
    }
    return dialogue_advance::script;
}

void dialogue_presenter::range(text_canvas& canvas, text_canvas::cursor& cursor, const char* text,
                               const dialogue_layout& box, int first, int last, text_canvas::ink ink,
                               const story_link* links, int count, const story_text_style* style, bool original_breaks)
{
    if (!count)
    {
        canvas.styled_text(cursor, box, text + first, text + last, ink, style, first, false, original_breaks);
        return;
    }
    while (first < last)
    {
        int end = last;
        auto color = ink;
        for (int i = 0; i < count; ++i)
        {
            const auto& link = links[i];
            if (first >= link.first && first < link.last)
            {
                color = text_canvas::accent;
                end = bn::min(end, int(link.last));
            }
            else if (first < link.first)
                end = bn::min(end, int(link.first));
        }
        canvas.styled_text(cursor, box, text + first, text + end, color, style, first, false, original_breaks);
        first = end;
    }
}

void dialogue_presenter::draw(text_canvas& canvas, const char* text, const dialogue_layout& box, bool full, int budget,
                              const story_link* links, int count, const story_text_style* style)
{
    if (style && canvas.bitmap_colors()[text_canvas::selection] != story_text_colors[style->color])
        canvas.palette_color(text_canvas::selection, story_text_colors[style->color]);
    if (full)
    {
        if (!_redrawing)
        {
            _cursor = {box.left, box.top};
            _drawn = _page.start();
        }
        int end = _page.visible();
        if (budget)
        {
            end = _drawn;
            for (; budget && end < _page.visible(); --budget)
            {
                unsigned c = uint8_t(text[end]);
                end += c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
            }
        }
        int white_end = bn::min(end, _page.fading() ? _page.glyph_begin() : _page.visible());
        range(canvas, _cursor, text, box, _drawn, white_end, text_canvas::white, links, count, style, _original_breaks);
        _last_cursor = _cursor;
        _last_begin = white_end;
        _last_end = end;
        range(canvas, _cursor, text, box, white_end, end, text_canvas::muted, links, count, style, _original_breaks);
        _drawn = end;
        _redrawing = end < _page.visible();
    }
    else if (_step == reveal_step::append)
    {
        _last_cursor = _cursor;
        _last_begin = _drawn;
        _last_end = _page.visible();
        range(canvas, _cursor, text, box, _last_begin, _last_end, text_canvas::muted, links, count, style,
              _original_breaks);
        _drawn = _last_end;
    }
    else if (_step == reveal_step::brighten)
    {
        auto cursor = _last_cursor;
        range(canvas, cursor, text, box, _last_begin, _last_end, text_canvas::white, links, count, style,
              _original_breaks);
    }
    _step = reveal_step::none;
}

void dialogue_presenter::prompt(text_canvas& canvas, const char* text) const
{
    if (complete() && !_redrawing)
        canvas.text(226, 146, text[_page.end()] ? "+" : ">", text_canvas::white, 239, 160);
}

void dialogue_presenter::speaker(text_canvas& canvas, const char* name, bool show_narrator)
{
    for (int i = 0; i < nameplate_count; ++i)
        if (bn::string_view(name) == nameplates[i].speaker)
        {
            canvas.name_label(nameplates[i].tiles, nameplates[i].color);
            return;
        }
    if (show_narrator || bn::string_view(name) != "旁白")
        canvas.text(26, 102, name, text_canvas::white, 225, 116);
}
} // namespace manosaba
