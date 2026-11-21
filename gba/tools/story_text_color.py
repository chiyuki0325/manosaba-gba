"""Original inline colors, mapped to final reflowed UTF-8 byte positions."""
import html
import re
from story_layout import INLINE

COLORS = ('#ff0000', '#9c8eff', '#6b85d0')

def color_spans(source, text):
    if not re.search(r'<color\s*=', source, re.I):
        return None
    source_chars = []
    stack = [None]
    for token in re.split(r'(<[^>]*>)', source):
        if token.startswith('<'):
            if m := re.fullmatch(r'<color\s*=\s*[\"\']?(#[0-9a-f]{6})[\"\']?\s*>', token, re.I):
                stack.append(COLORS.index(m[1].lower()))
            elif re.fullmatch(r'</color\s*>', token, re.I):
                stack.pop()
            continue
        for char in html.unescape(INLINE.sub('', token)):
            if not char.isspace() and char != '\u2060':
                source_chars.append((char, stack[-1]))
    visible = []
    offset = history_offset = 0
    for char in text:
        size = len(char.encode())
        marker = '\ue000' <= char <= '\ue002'
        if not char.isspace() and not marker:
            visible.append((char, offset, offset+size, history_offset, history_offset+size))
        offset += size
        if not marker:
            history_offset += size
    assert ''.join(c for c, _ in source_chars) == ''.join(c[0] for c in visible), (source, text)
    used = {color for _, color in source_chars if color is not None}
    assert len(used) == 1, (source, used)
    color = used.pop()
    spans = []
    active = None
    for (_, ink), (_, first, last, hfirst, hlast) in zip(source_chars, visible):
        if ink is None:
            active = None
        elif active is None:
            active = [first, last, hfirst, hlast]
            spans.append(active)
        else:
            active[1], active[3] = last, hlast
    return color, spans
