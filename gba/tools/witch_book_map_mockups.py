"""Native 240x160 map-page designs, using original geometry and one 16-color bank.

Design only: no game code, imported ROM assets or runtime buffers are changed.
"""
from build_paths import generated_path
import json
import numpy as np
from PIL import Image, ImageDraw
from graphics_project import ROOT, GBA, write_changed
from witch_book_mockups import text, key_action, KEY_ICONS, BODY_INK, INACTIVE_INK
from story import compress

OUT = generated_path('design/witch-book/map')
PALETTE = [
    (0, 0, 0), BODY_INK, INACTIVE_INK, (248, 144, 168),
    (104, 56, 64), (136, 120, 112), (48, 40, 40), (80, 72, 72),
    (56, 48, 48), (184, 176, 160), (176, 168, 152), (64, 56, 56),
    (144, 136, 128), (160, 152, 144), (192, 120, 136), (112, 104, 96),
]
PLANE = (384, 224)
VIEW = (4, 25, 232, 118)
SCALE = .2
INK = PALETTE[3]
SOURCE = json.loads((GBA/'assets/source-metadata/book-maps.json').read_text())
NAMES = {'Lounge': '休息室', 'ChamberUndine': '水之室', 'Lake': '湖边',
         'UnknownArea': '？？？', 'NoahStudio': '诺亚的工作室',
         'BasementHallway': '地下走廊', 'Infirmary': '医务室'}


def quantize(image, indices=None):
    pool = PALETTE if indices is None else [PALETTE[i] for i in indices]
    pixels = np.asarray(image.convert('RGB')).astype(np.int32)
    unique, inverse = np.unique(pixels.reshape(-1, 3), axis=0, return_inverse=True)
    selected = ((unique[:, None, :]-np.array(pool)[None, :, :])**2).sum(axis=2).argmin(axis=1)
    return Image.fromarray(np.array(pool, dtype=np.uint8)[selected[inverse]].reshape(pixels.shape))


def paper(size):
    image = Image.new('RGB', size, PALETTE[9])
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


def tabs(image, forced):
    draw = ImageDraw.Draw(image)
    for index, label in enumerate(('证物', '人物', '地图', '规定')):
        x, top = 23+index*49, 1 if index == 2 else 5
        draw.polygon([(x, top+3), (x+3, top), (x+43, top), (x+46, top+3),
                      (x+46, 22), (x, 22)],
                     fill=PALETTE[8 if index == 2 else 7], outline=PALETTE[5])
        if index == 2:
            text(image, x+8, 4, label[0], INK, large=True)
            text(image, x+25, 7, label[1], BODY_INK)
        else:
            text(image, x+11, 8, label, INACTIVE_INK)
    if not forced:
        image.alpha_composite(KEY_ICONS['L'], (1, 8))
        image.alpha_composite(KEY_ICONS['R'], (221, 8))


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
    image = quantize(image, [9, 10, 11, 12, 13, 15])
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


def overlap(first, second):
    return max(0, min(first[2], second[2])-max(first[0], second[0])) * \
        max(0, min(first[3], second[3])-max(first[1], second[1]))


def place_label(image, selected, point, confirm):
    vx, vy, vw, vh = VIEW
    name = NAMES[selected] if selected else '一楼设计图'
    width, height = len(name)*12+8+(16 if not confirm else 0), 17
    px, py = point
    left = round(max(vx+3, min(vx+vw-width-3, px-width/2))) if selected else 76
    top = round(py+9) if selected else 28
    if top+height > vy+vh-3:
        top = round(py-height-9)
    top = max(vy+3, min(vy+vh-height-3, top))
    box = (left, top, left+width, top+height)
    draw = ImageDraw.Draw(image)
    if selected:
        edge = top-1 if top > py else top+height
        draw.line([(round(px), round(py+4 if top > py else py-4)),
                   (round(px), edge)], fill=PALETTE[4], width=1)
    draw.rectangle((left, top, left+width-1, top+height-1),
                   fill=PALETTE[6], outline=PALETTE[5])
    text(image, left+4, top+2, name, BODY_INK)
    if not confirm:
        image.alpha_composite(KEY_ICONS['A'], (left+width-16, top+1))
    assert vx <= box[0] < box[2] <= vx+vw
    assert vy <= box[1] < box[3] <= vy+vh
    return box


def map_page(ident, version, selected, confirm=False, forced=False):
    floor = floor_data(ident, version)
    original, areas = floor_image(floor)
    area = areas[selected] if selected else dict(center=(192, 112))
    plane = select_region(original, area) if selected else original
    cx, cy = area['center']
    vx, vy, vw, vh = VIEW
    left = round(max(0, min(PLANE[0]-vw, cx-vw/2)))
    top = round(max(0, min(PLANE[1]-vh, cy-vh/2)))
    image = paper((240, 160)).convert('RGBA')
    # Same approved four tabs; forced selection disables category switching.
    tabs(image, forced)
    image.paste(plane.crop((left, top, left+vw, top+vh)), (vx, vy))
    draw = ImageDraw.Draw(image)
    draw.rectangle((vx-1, vy-1, vx+vw, vy+vh), outline=PALETTE[5])
    draw.rectangle((7, 28, 62, 43), fill=PALETTE[6])
    text(image, 11, 29, {'F1': '1F', 'F2': '2F', 'B1': 'B1', 'Blueprint': '1F'}[ident], BODY_INK)
    text(image, 32, 29, {'F1': '1/3', 'F2': '2/3', 'B1': '3/3', 'Blueprint': '4/4'}[ident], INACTIVE_INK)

    focus = (vx+cx-left, vy+cy-top)
    label_box = place_label(image, selected, focus, confirm)
    # Opposite-corner placement avoids both the selected room and its name.
    polygons = [p for polygon in area.get('polygons', []) for p in polygon]
    if polygons:
        focus_box = (vx+min(p[0] for p in polygons)-left, vy+min(p[1] for p in polygons)-top,
                     vx+max(p[0] for p in polygons)-left, vy+max(p[1] for p in polygons)-top)
    else:
        focus_box = (focus[0]-6, focus[1]-6, focus[0]+6, focus[1]+6)
    def cost(pos):
        box = (pos[0], pos[1], pos[0]+52, pos[1]+32)
        distance = (pos[0]+26-focus[0])**2+(pos[1]+16-focus[1])**2
        return overlap(box, label_box)*1000+overlap(box, focus_box)*100-distance
    mini_x, mini_y = min([(182, 27), (7, 46), (182, 109), (7, 109)], key=cost)
    assert not overlap((mini_x, mini_y, mini_x+52, mini_y+32), label_box)
    draw = ImageDraw.Draw(image)
    draw.rectangle((mini_x, mini_y, mini_x+51, mini_y+31), fill=PALETTE[6], outline=PALETTE[5])
    mini = original.resize((48, 28), Image.Resampling.NEAREST)
    image.paste(mini, (mini_x+2, mini_y+2))
    draw = ImageDraw.Draw(image)
    box = (mini_x+2+left*48//384, mini_y+2+top*28//224,
           mini_x+2+(left+vw)*48//384, mini_y+2+(top+vh)*28//224)
    draw.rectangle(box, outline=BODY_INK)
    mx, my = mini_x+2+round(cx*48/384), mini_y+2+round(cy*28/224)
    if selected:
        draw.rectangle((mx-1, my-1, mx+1, my+1), fill=INK)
    for show, points in [
        (left > 0, [(5, 84), (9, 80), (9, 88)]),
        (left+vw < PLANE[0], [(234, 84), (230, 80), (230, 88)]),
        (top > 0, [(118, 26), (114, 30), (122, 30)]),
        (top+vh < PLANE[1], [(118, 141), (114, 137), (122, 137)]),
    ]:
        if show:
            draw.polygon(points, fill=PALETTE[6])

    draw.rectangle((4, 144, 235, 159), fill=PALETTE[6])
    if not confirm:
        text(image, 8, 146, '↑↓楼层', INACTIVE_INK)
        if selected:
            text(image, 76, 146, '←→地点', INACTIVE_INK)
    else:
        draw.rectangle((27, 58, 212, 110), fill=PALETTE[8], outline=PALETTE[5])
        prompt = '选定这个地点？'
        text(image, 120-len(prompt)*6, 64, prompt, INACTIVE_INK)
        name = NAMES[selected]
        text(image, 120-len(name)*6, 87, name, BODY_INK)
        key_action(image, 10, 144, 'A', '选定')
    key_action(image, 184, 144, 'B', '返回')
    image = quantize(image)
    assert len(image.getcolors(256)) <= 16
    return image


def indexed_bytes(image, tiled=False):
    rgb = np.asarray(image)
    pixels = np.zeros(rgb.shape[:2], np.uint8)
    for index, color in enumerate(PALETTE):
        pixels[np.all(rgb == color, axis=2)] = index
    if tiled:
        pixels = pixels.reshape(28, 8, 48, 8).transpose(0, 2, 1, 3).reshape(-1, 8)
    return (pixels[:, 0::2] | pixels[:, 1::2] << 4).tobytes()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    specs = [
        ('01-1f-lounge', 'F1', 1, 'Lounge', False, False),
        ('02-1f-small-room', 'F1', 1, 'ChamberUndine', False, False),
        ('03-1f-outside', 'F1', 1, 'Lake', False, False),
        ('04-2f-unknown', 'F2', 1, 'UnknownArea', False, False),
        ('05-2f-studio', 'F2', 2, 'NoahStudio', False, False),
        ('06-trial-confirm', 'B1', 1, 'BasementHallway', True, True),
    ]
    sheet = Image.new('RGB', (720, 320))
    for index, (name, *args) in enumerate(specs):
        image = map_page(*args)
        image.save(OUT/(name+'-240x160.png'))
        image.resize((720, 480), Image.Resampling.NEAREST).save(OUT/(name+'-3x.png'))
        sheet.paste(image, (index%3*240, index//3*160))
    sheet.resize((1440, 640), Image.Resampling.NEAREST).save(OUT/'overview-2x.png')
    sheet.resize((1440, 640), Image.Resampling.NEAREST).save(OUT/'overview-v2-2x.png')
    blueprint = map_page('Blueprint', 1, None)
    blueprint.save(OUT/'07-blueprint-240x160.png')
    blueprint.resize((720, 480), Image.Resampling.NEAREST).save(OUT/'07-blueprint-3x.png')
    estimates = []
    for ident, version in [('F1', 1), ('F2', 1), ('B1', 1), ('Blueprint', 1)]:
        image, _ = floor_image(floor_data(ident, version))
        raw = indexed_bytes(image)
        tiled = indexed_bytes(image, True)
        estimates.append(dict(id=ident, size=list(PLANE), raw_4bpp_bytes=len(raw),
                              compressed_linear_bytes=len(compress(raw)),
                              compressed_tile_blocks_bytes=sum(len(compress(tiled[first:first+4096]))
                                                               for first in range(0, len(tiled), 4096))))
    report = dict(design_only=True, screen=[240, 160], viewport=list(VIEW), palette=PALETTE,
                  colors=16, plane=list(PLANE), scale=SCALE, images=estimates,
                  controls=dict(floor='up/down', area='left/right'), location_label='on-map',
                  note='Offline LZSS samples only; not an imported ROM or runtime timing measurement.')
    write_changed(OUT/'design-report.json', json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
