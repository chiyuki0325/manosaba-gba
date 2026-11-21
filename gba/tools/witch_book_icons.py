"""Native-size book controls shared by ROM import and design previews."""
from PIL import Image, ImageDraw

BODY_INK = (248, 248, 240)
INACTIVE_INK = (208, 200, 192)
KEY_LETTERS = {
    'A': ('01110', '10001', '10001', '11111', '10001', '10001', '10001'),
    'B': ('11110', '10001', '10001', '11110', '10001', '10001', '11110'),
    'L': ('10000', '10000', '10000', '10000', '10000', '10000', '11111'),
    'R': ('11110', '10001', '10001', '11110', '10100', '10010', '10001'),
}

def key_icon(key):
    """Native pixel art, including one clear pixel beyond the cast shadow."""
    width = 14 if key in 'AB' else 18
    face = Image.new('RGBA', (width, 14))
    draw = ImageDraw.Draw(face)
    if key in 'AB':
        draw.ellipse((1, 1, 11, 11), fill=(*INACTIVE_INK, 255))
        draw.ellipse((2, 2, 10, 10), fill=(56, 48, 48, 255))
        left = 4
    else:
        corners = [(1, 4), (4, 1), (15, 1), (15, 11), (1, 11)] if key == 'L' else [
            (1, 1), (12, 1), (15, 4), (15, 11), (1, 11)]
        draw.polygon(corners, fill=(56, 48, 48, 255), outline=(*INACTIVE_INK, 255))
        left = 6
    for y, row in enumerate(KEY_LETTERS[key]):
        for x, ink in enumerate(row):
            if ink == '1':
                draw.point((left+x, 3+y), fill=(*BODY_INK, 255))
    result = Image.new('RGBA', face.size)
    shadow = Image.new('RGBA', face.size, (0, 0, 0, 255))
    shadow.putalpha(face.getchannel('A'))
    result.alpha_composite(shadow, (1, 1))
    result.alpha_composite(face)
    return result
