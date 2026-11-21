"""Shared hardware pixel encoding for the offline image and UI generators."""
from PIL import Image

UI_COLORS = [(0, 0, 0), (248, 248, 240), (152, 144, 144), (248, 128, 168),
             (32, 24, 32), (216, 192, 136), (0, 0, 0), (8, 8, 8),
             (24, 24, 24), (48, 48, 48), (72, 72, 72), (104, 104, 104),
             (32, 16, 24), (64, 32, 40), (112, 56, 72), (176, 88, 112)]


def rgb555(rgb):
    return (rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10)


def quantized(image, colors):
    palette = Image.new('P', (1, 1))
    palette.putpalette(sum((list(c) for c in colors), []) + list(colors[-1]) * (256-len(colors)))
    result = image.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE)
    # Padding repeats the last color; map it back to a valid hardware index.
    lut = [colors.index(tuple(palette.getpalette()[i*3:i*3+3])) for i in range(256)]
    return result.point(lut)


def train(pixels, count):
    if not pixels:
        return [(0, 0, 0)] * count
    image = Image.new('RGB', (len(pixels), 1))
    image.putdata(pixels)
    pal = image.quantize(colors=count, method=Image.Quantize.MEDIANCUT).getpalette()
    pal += [0] * (count*3-len(pal))
    return [tuple(pal[i*3:i*3+3]) for i in range(count)]


def tiles(image, bpp):
    pixels = image.tobytes()
    out = bytearray()
    for ty in range(0, image.height, 8):
        for tx in range(0, image.width, 8):
            for y in range(8):
                row = pixels[(ty+y)*image.width+tx:(ty+y)*image.width+tx+8]
                out.extend(row if bpp == 8 else bytes(row[x] | row[x+1] << 4 for x in range(0, 8, 2)))
    return bytes(out)
