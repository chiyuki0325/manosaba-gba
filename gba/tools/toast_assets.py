#!/usr/bin/env python3
"""Bake plain script notifications below the book icon, using the existing font."""
from build_paths import generated_path
from pathlib import Path
import struct
from PIL import Image,ImageDraw
from font import read_font
from image_encoding import UI_COLORS, tiles

GBA = Path(__file__).resolve().parents[1]
LABELS = ('魔女图鉴 更新…', '讨论前进…', 'LIVE')
LEFT, TOP, WIDTH, HEIGHT = 0, 24, 96, 24


def main():
    glyphs = read_font()
    base = Image.new('P', (WIDTH, HEIGHT), 0)
    base.putpalette(sum((list(c) for c in UI_COLORS), [])+[0]*720)
    output = generated_path('design/toasts')
    output.mkdir(parents=True, exist_ok=True)
    labels = []
    for label in LABELS:
        image = base.copy()
        left = 18 if label=='LIVE' else 6
        if label=='LIVE':
            draw=ImageDraw.Draw(image)
            draw.ellipse((7,8,15,16),outline=6)
            draw.ellipse((6,7,14,15),outline=5)
        for char in label:
            for shift, ink in ((1, 6), (0, 1)):
                for y, row in enumerate(glyphs[ord(char)]):
                    for x in range(12):
                        if row & (1 << (11-x)):
                            # Final user adjustment: first ink at screen (6,30).
                            image.putpixel((left+x+shift, 5+y+shift), ink)
            left += 6 if ord(char)<128 else 12
        labels.append(tiles(image, 4))
    with (GBA/'src/toast_data.cpp').open('w') as f:
        f.write('#include "toast_data.h"\nnamespace manosaba {\nconst uint32_t toast_tiles[3][288]={\n')
        for raw in labels:
            f.write('{'+','.join(map(str, struct.unpack('<288I', raw)))+'},\n')
        f.write('};\n}\n')
    background = Image.new('RGB',(240,160),(24,24,24))
    overview = Image.new('RGB', (240*len(labels), 160), (0, 0, 0))
    for i, raw in enumerate(labels):
        # Reuse the exact baked pixels for the design, at native screen coordinates.
        frame = background.copy()
        data = bytearray(WIDTH*HEIGHT)
        for tile in range(36):
            for y in range(8):
                for x in range(8):
                    v = raw[tile*32+y*4+x//2]
                    data[(tile//12*8+y)*WIDTH+tile%12*8+x] = (v >> (x%2*4)) & 15
        image = Image.frombytes('P', (WIDTH, HEIGHT), bytes(data))
        image.putpalette(base.getpalette())
        if i==2:
            palette=image.getpalette();palette[15:18]=[248,32,48];image.putpalette(palette)
        mask = Image.frombytes('L', image.size, bytes(255 if v else 0 for v in image.tobytes()))
        frame.paste(image.convert('RGB'), (LEFT, TOP), mask)
        frame.save(output/f'{i+1}-240x160.png')
        overview.paste(frame, (i*240, 0))
    overview.resize((overview.width*3, 480), Image.Resampling.NEAREST).save(output/'overview-3x.png')


if __name__ == '__main__':
    main()
