"""Design previews only; no ROM assets or runtime source are generated."""
from build_paths import generated_path
import csv
import gzip
import json
from pathlib import Path

from PIL import Image, ImageDraw

from font import read_font
from nameplates import nameplate_base, nameplate_palette
from story_layout import reflow, pixel_width
from witch_book_icons import key_icon

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'manosaba-web/public/UI_WitchBook_1'
OUT = generated_path('design/witch-book')
BODY_INK = (248, 248, 240)
INACTIVE_INK = (208, 200, 192)
FONT = read_font()
BIG = {}
with gzip.open(ROOT / 'gba/assets/unifont-17.0.03.hex.gz', 'rt') as source:
    for line in source:
        code, bits = line.strip().split(':')
        if int(code, 16) in map(ord, '樱被证人地规'):
            BIG[int(code, 16)] = [int(bits[i:i+4], 16) for i in range(0, 64, 4)]
MANIFEST = list(csv.DictReader((ROOT / 'original_sprites/manifest.csv').open()))
MASTER = json.loads((ROOT / 'gba/assets/source-metadata/trial.json').read_text())['master']


def localized(values):
    return next(v['_text'] for v in values if v['_locale'] == 2)


def source_image(name):
    row = next(v for v in MANIFEST if v['type'] == 'Texture2D' and v['name'] == name)
    return Image.open(ROOT / 'original_sprites' / row['output']).convert('RGBA')


def paste(im, art, xy, size):
    art = art.resize(size, Image.Resampling.LANCZOS)
    im.alpha_composite(art, xy)


def text(im, x, y, value, color=(48, 40, 40), large=False, cast_shadow=True):
    draw = ImageDraw.Draw(im)
    for char in value:
        width = 16 if large else 6 if ord(char) < 128 else 12
        rows = BIG[ord(char)] if large else FONT[ord(char)]
        points = [(x+dx, y+dy) for dy, row in enumerate(rows)
                  for dx in range(width) if row & (1 << ((15 if large else 11)-dx))]
        if cast_shadow:
            for dx, dy in points:
                draw.point((dx+1, dy+1), fill=(0, 0, 0))
        for point in points:
            draw.point(point, fill=color)
        x += width


KEY_ICONS = {key: key_icon(key) for key in 'ABLR'}


def key_action(im, x, y, key, label):
    icon = KEY_ICONS[key]
    im.alpha_composite(icon, (x, y+1))
    text(im, x+icon.width+1, y+1, label, BODY_INK)


def base(active, tab_controls=True):
    im = Image.new('RGBA', (240, 160), (160, 152, 144, 255))
    # One shared paper, not a separately baked page for each item.
    paste(im, Image.open(ART / 'Background_1.png').convert('RGBA'), (0, 0), (240, 160))
    draw = ImageDraw.Draw(im)
    for i, label in enumerate(('证物', '人物', '地图', '规定')):
        x = 23+i*49
        top = 1 if i == active else 5
        draw.polygon([(x, top+3), (x+3, top), (x+43, top), (x+46, top+3),
                      (x+46, 22), (x, 22)], fill=(56, 48, 48) if i == active else (80, 72, 72),
                     outline=(136, 120, 112))
        if i == active:
            text(im, x+8, 4, label[0], (248, 144, 168), large=True)
            text(im, x+25, 7, label[1], BODY_INK)
        else:
            text(im, x+11, 8, label, INACTIVE_INK)
    if tab_controls:
        im.alpha_composite(KEY_ICONS['L'], (1, 8))
        im.alpha_composite(KEY_ICONS['R'], (221, 8))
    return im


def title(im, value, profile):
    if not profile:
        text(im, 106, 29, value[0], BODY_INK, large=True)
        text(im, 123, 33, value[1:], BODY_INK)
        return
    ImageDraw.Draw(im).rectangle((100, 26, 235, 51), fill=(56, 48, 48))
    indexed = nameplate_base()
    palette = nameplate_palette()
    diamond = Image.new('RGBA', indexed.size)
    diamond.putdata([(*palette[v-12], 255) if v else (0, 0, 0, 0) for v in indexed.tobytes()])
    im.alpha_composite(diamond, (100, 26))
    text(im, 106, 29, value[0], (248, 144, 168), large=True)
    # Baselines align; body starts one pixel closer to the initial.
    text(im, 123, 33, value[1:], (248, 248, 240))


def overview(profile=False):
    im = base(int(profile))
    ident = 'Ema' if profile else '1-1'
    item = next(v['_item'] for v in MASTER['ProfileData' if profile else 'ClueData']['_items']
                if v['_id'] == ident and v['_version'] == (2 if profile else 1))
    name = '樱羽艾玛' if profile else localized(item['_name'])
    frame = Image.open(ART / ('PersonFrame.png' if profile else 'EvidenceFrame.png')).convert('RGBA')
    paste(im, frame, (5, 25), (92, 93))
    image = source_image('Profile_Ema' if profile else 'Clue_001_001')
    # Nearest sampling approximates the proposed one-time runtime reduction.
    if not profile:
        image.thumbnail((216, 120), Image.Resampling.LANCZOS)
    image.thumbnail((76, 76), Image.Resampling.NEAREST)
    im.alpha_composite(image, (13+(76-image.width)//2, 32+(76-image.height)//2))
    title(im, name, profile)
    lines = reflow(localized(item['_description']), 120).split('\n')
    for row, line in enumerate(lines[:4]):
        assert pixel_width(line) <= 120
        text(im, 104, 55+row*14, line, BODY_INK)
    if len(lines) > 4:
        text(im, 104, 113, '更多…', INACTIVE_INK)
    text(im, 210, 113, '1/4', INACTIVE_INK)
    draw = ImageDraw.Draw(im)
    draw.rectangle((4, 126, 235, 159), fill=(48, 40, 40))
    names = ['樱羽艾玛', '二阶堂希罗', '泽渡可可', '远野汉娜'] if profile else ['被拆解的零件', '安安的素描本', '沾血的发带', '扫帚']
    draw.rectangle((7, 127, 118, 142), fill=(104, 56, 64))
    for i, label in enumerate(names):
        text(im, 17+(i%2)*114, 128+(i//2)*16, label, (248, 248, 240))
    text(im, 7, 128, '>', (248, 144, 168))
    draw.polygon([(231, 151), (235, 151), (233, 154)], fill=(248, 232, 216))
    return im


def description():
    im = base(0, tab_controls=False)
    item = next(v['_item'] for v in MASTER['ClueData']['_items'] if v['_id'] == '1-5')
    text(im, 12, 27, localized(item['_name']), BODY_INK)
    lines = reflow(localized(item['_description']), 216).split('\n')
    for row, line in enumerate(lines[:7]):
        assert pixel_width(line) <= 216
        text(im, 12, 45+row*14, line, BODY_INK)
    ImageDraw.Draw(im).rectangle((4, 144, 235, 159), fill=(48, 40, 40))
    key_action(im, 10, 144, 'A', '出示')
    key_action(im, 73, 144, 'B', '返回')
    key_action(im, 132, 144, 'R', '看图')
    text(im, 194, 145, '1/'+str(max(1, (len(lines)+6)//7)), INACTIVE_INK)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    keys = Image.new('RGBA', (70, 14))
    left = 0
    for key, icon in KEY_ICONS.items():
        bounds = icon.getbbox()
        assert bounds[0] >= 1 and bounds[1] >= 1
        assert bounds[2] <= icon.width-1 and bounds[3] <= icon.height-1
        icon.save(OUT / f'key-{key}-native.png')
        keys.alpha_composite(icon, (left, 0))
        left += icon.width+2
    keys.save(OUT / 'keys-native.png')
    keys.resize((420, 84), Image.Resampling.NEAREST).save(OUT / 'keys-6x.png')
    previews = [('clue', overview()), ('profile', overview(True)), ('description', description())]
    sheet = Image.new('RGB', (240*3, 160))
    for at, (name, im) in enumerate(previews):
        # GBA RGB555 output; these remain design previews, not emulator captures.
        rgb = im.convert('RGB').point(lambda v: v & 248)
        rgb.save(OUT / f'{name}-240x160.png')
        sheet.paste(rgb, (240*at, 0))
    sheet.resize((1440, 320), Image.Resampling.NEAREST).save(OUT / 'overview-2x.png')
    print('Wrote design previews:', OUT)


if __name__ == '__main__':
    main()
