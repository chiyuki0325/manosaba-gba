#!/usr/bin/env python3
"""Export Unity Texture2D and Sprite objects, retaining their source and path IDs."""
import csv
import gc
import json
import re
import shutil
import sys
from pathlib import Path
from output_paths import report_path
import UnityPy

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / 'original_game'
OUT = ROOT / 'original_sprites'
DATA = GAME / 'manosaba_Data'

def safe(s):
    s = re.sub(r'[\x00-\x1f<>:"/\\|?*]', '_', str(s)).strip(' .')
    return s[:120] or 'unnamed'

def main():
    OUT.mkdir(exist_ok=True)
    bundles = sorted(GAME.rglob('*.bundle'))
    assets = sorted(p for p in DATA.iterdir() if p.is_file() and (p.name.endswith('.assets') or p.name in ('globalgamemanagers', 'level0')))
    assets += sorted(p for p in (DATA / 'Resources').iterdir() if p.is_file())
    sources = assets + bundles
    errors, skipped, used = [], [], set()
    counts = {'Texture2D': 0, 'Sprite': 0, 'files_scanned': 0, 'standalone_images': 0}
    with (OUT / 'manifest.csv').open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['source', 'serialized_file', 'type', 'path_id', 'name', 'width', 'height', 'output'])
        for index, p in enumerate(sources, 1):
            rel = p.relative_to(GAME)
            print(f'[{index}/{len(sources)}] {rel}', flush=True)
            try:
                env = UnityPy.load(str(p))
                for obj in env.objects:
                    kind = obj.type.name
                    if kind not in ('Texture2D', 'Sprite'):
                        continue
                    try:
                        data = obj.read()
                        name = data.m_Name
                        if (kind == 'Texture2D' and (data.m_Width == 0 or data.m_Height == 0)) or (kind == 'Sprite' and (data.m_Rect.width == 0 or data.m_Rect.height == 0)):
                            skipped.append({'source': rel.as_posix(), 'type': kind, 'path_id': obj.path_id, 'name': name, 'reason': 'zero-sized image'})
                            continue
                        img = data.image
                        if img is None:
                            raise ValueError('image decoder returned None')
                        source_dir = OUT / 'unity' / rel.parent / rel.name / kind
                        source_dir.mkdir(parents=True, exist_ok=True)
                        dest = source_dir / f'{safe(name)}__{obj.path_id}.png'
                        # Serialized files may contain identical path IDs: retain both.
                        if dest in used:
                            dest = source_dir / f'{safe(name)}__{obj.path_id}__{safe(obj.assets_file.name)}.png'
                        if dest in used:
                            raise ValueError(f'duplicate output path: {dest}')
                        used.add(dest)
                        img.save(dest, format='PNG')
                        writer.writerow([rel.as_posix(), obj.assets_file.name, kind, obj.path_id, name, img.width, img.height, dest.relative_to(OUT).as_posix()])
                        counts[kind] += 1
                    except Exception as exc:
                        errors.append({'source': rel.as_posix(), 'type': kind, 'path_id': obj.path_id, 'error': repr(exc)})
                counts['files_scanned'] += 1
                del env
                gc.collect()
            except Exception as exc:
                errors.append({'source': rel.as_posix(), 'error': repr(exc)})
            f.flush()
        for p in sorted(GAME.rglob('*')):
            if p.is_file() and p.suffix.lower() in {'.png', '.jpg', '.jpeg', '.bmp', '.tga', '.gif', '.webp', '.tif', '.tiff', '.dds'}:
                dest = OUT / 'standalone' / p.relative_to(GAME)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dest)
                counts['standalone_images'] += 1
    report_path('images.json').write_text(json.dumps({'counts': counts, 'skipped': skipped, 'errors': errors}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'counts': counts, 'errors': len(errors)}, ensure_ascii=False), flush=True)
    return bool(errors)

if __name__ == '__main__':
    sys.exit(main())
