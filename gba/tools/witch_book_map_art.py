"""Shared original map geometry and fixed-palette offline artwork."""
import json
import numpy as np
from PIL import Image, ImageDraw
from graphics_project import ROOT, GBA

PALETTE = [(0,0,0),(248,248,240),(208,200,192),(248,144,168),
           (104,56,64),(136,120,112),(0,0,0),(48,40,40),
           (184,176,160),(56,48,48),(176,168,152),(80,72,72),
           (144,136,128),(160,152,144),(192,120,136),(112,104,96)]
PLANE = (384,224)
VIEW = (4,25,232,118)
SCALE = .2
INK = PALETTE[3]
SOURCE = json.loads((GBA/'assets/source-metadata/book-maps.json').read_text())


def quantize(image, indices=None):
    pool = PALETTE if indices is None else [PALETTE[i] for i in indices]
    pixels = np.asarray(image.convert('RGB')).astype(np.int32)
    unique, inverse = np.unique(pixels.reshape(-1, 3), axis=0, return_inverse=True)
    selected = ((unique[:, None, :]-np.array(pool)[None, :, :])**2).sum(axis=2).argmin(axis=1)
    return Image.fromarray(np.array(pool, dtype=np.uint8)[selected[inverse]].reshape(pixels.shape))


def paper(size):
    image = Image.new('RGB', size, PALETTE[8])
    draw = ImageDraw.Draw(image)
    # One repeated 16x16 tile. No full-page paper texture.
    for y in range(0, size[1], 16):
        for x in range(0, size[0], 16):
            for dx, dy in [(2, 3), (10, 11), (13, 5)]:
                if x+dx < size[0] and y+dy < size[1]:
                    draw.point((x+dx, y+dy), fill=PALETTE[10])
    return image


def floor_data(ident, version):
    return next(f for f in SOURCE['floors'] if f['pair'] == {'Id': ident, 'Version': version})


def geometry(floor):
    size = floor['tree']['rect']['m_SizeDelta']
    width, height = size['x'], size['y']
    origin = (220 if floor['pair']['Id'] == 'F1' else 192, 112)

    def point(x, y):
        return origin[0]+x*SCALE, origin[1]-y*SCALE

    result = {}
    for area in floor['tree']['children']:
        components = area['components']
        if not any('_areaKind' in c for c in components):
            continue
        rect = area['rect']
        center = rect['m_AnchoredPosition']
        extent = rect['m_SizeDelta']
        polygons = []
        for child in area['children']:
            r = child['rect']
            for c in child['components']:
                if '_vertexes' not in c:
                    continue
                p, amin, amax = r['m_Pivot'], r['m_AnchorMin'], r['m_AnchorMax']
                w = extent['x']*(amax['x']-amin['x'])+r['m_SizeDelta']['x']
                h = extent['y']*(amax['y']-amin['y'])+r['m_SizeDelta']['y']
                cx = center['x']+extent['x']*(amin['x']+(amax['x']-amin['x'])*p['x']-.5)+r['m_AnchoredPosition']['x']
                cy = center['y']+extent['y']*(amin['y']+(amax['y']-amin['y'])*p['y']-.5)+r['m_AnchoredPosition']['y']
                polygons.append([point(cx+(v['x']*.5+.5-p['x'])*w,
                                       cy+(v['y']*.5+.5-p['y'])*h) for v in c['_vertexes']])
        result[area['name']] = dict(center=point(center['x'], center['y']), polygons=polygons)
    return (width, height), origin, result


def floor_image(floor):
    size, origin, areas = geometry(floor)
    ident = floor['pair']['Id']
    image = paper(PLANE).convert('RGBA')
    art = Image.open(ROOT/f'manosaba-web/public/UI_WitchBook_2/Layout_{ident}_1.png').convert('RGBA')
    art = art.resize(tuple(round(v*SCALE) for v in size), Image.Resampling.LANCZOS)
    image.alpha_composite(art, (round(origin[0]-size[0]*SCALE/2), round(origin[1]-size[1]*SCALE/2)))
    image = quantize(image, [8, 10, 11, 12, 13, 15])
    if ident == 'F1':
        # Outside directions remain in the world bounds, not clipped to the bitmap.
        draw = ImageDraw.Draw(image)
        for name in ('Wall', 'Lake', 'FlowerFields'):
            cx, cy = map(round, areas[name]['center'])
            draw.polygon([(cx-12, cy), (cx, cy-9), (cx, cy-4), (cx+12, cy-4),
                          (cx+12, cy+4), (cx, cy+4), (cx, cy+9)],
                         fill=PALETTE[12], outline=PALETTE[11])
    return image, areas


def select_region(image, area):
    mask = Image.new('L', PLANE)
    for polygon in area['polygons']:
        ImageDraw.Draw(mask).polygon(polygon, fill=255)
    pixels = np.asarray(image).copy()
    inside = np.asarray(mask) > 0
    # Preserve dark architectural strokes; highlight room interiors only.
    pixels[inside & (pixels[:, :, 0] >= 112)] = PALETTE[14]
    image = Image.fromarray(pixels)
    draw = ImageDraw.Draw(image)
    for polygon in area['polygons']:
        draw.line(polygon+[polygon[0]], fill=PALETTE[4], width=1)
    cx, cy = map(round, area['center'])
    draw.polygon([(cx, cy-4), (cx+4, cy), (cx, cy+4), (cx-4, cy)],
                 fill=INK, outline=PALETTE[6])
    return image
