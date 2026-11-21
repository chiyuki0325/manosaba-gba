#!/usr/bin/env python3
"""Audit localization text and generate a compact Unicode-indexed 12px font."""
from collections import Counter
from pathlib import Path
from build_paths import generated_path
import json
import re
import gzip

ROOT = Path(__file__).resolve().parents[2]
GBA = ROOT / 'gba'
FONT = GBA / 'assets/fusion-pixel-12px-monospaced-zh_hans.bdf'

def translations():
    for path in sorted((ROOT / 'original_scripts/zh-Hans').rglob('*.nani')):
        for line_no, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
            line = line.strip()
            if not line or line.startswith((';', '#')):
                continue
            # Naninovel rich-text commands are markup, not displayed glyphs.
            yield path, line_no, re.sub(r'<[^>]*>', '', line)

def ui_characters():
    # UI strings use ordinary UTF-8 C++ literals. Generated headers are excluded.
    chars = set()
    for path in sorted((GBA / 'src').iterdir()):
        if path.suffix not in {'.cpp', '.h'} or path.name == 'story_data.cpp':
            continue
        for literal in re.findall(r'"(?:[^"\\]|\\.)*"', path.read_text(encoding='utf-8')):
            chars.update(json.loads(literal))
    return chars

def read_font():
    glyphs = {}
    for block in FONT.read_text(encoding='utf-8').split('STARTCHAR ')[1:]:
        lines = block.splitlines()
        cp = int(next(x[9:] for x in lines if x.startswith('ENCODING ')))
        w, h, x, y = map(int, next(x[4:] for x in lines if x.startswith('BBX ')).split())
        start = lines.index('BITMAP') + 1
        rows = [0] * 12
        for row in range(h):
            bits = int(lines[start + row], 16)
            bits >>= ((w + 7) // 8 * 8 - w)
            dest = 10 - y - h + row
            if 0 <= dest < 12:
                for col in range(w):
                    if 0 <= x + col < 12 and bits & (1 << (w - 1 - col)):
                        rows[dest] |= 1 << (11 - x - col)
        glyphs[cp] = rows
    # Supplied 12px Up bitmap; rotate/combine it for objection and reader controls.
    up=(0x0f0,0x108,0x168,0x76e,0x861,0x801,0x801,0x801,0x70e,0x108,0x108,0x0f0)
    left=[sum(((up[x]>>(y))&1)<<(11-x) for x in range(12)) for y in range(12)]
    right=[sum(((up[11-x]>>(11-y))&1)<<(11-x) for x in range(12)) for y in range(12)]
    down=[sum(((up[11-y]>>x)&1)<<(11-x) for x in range(12)) for y in range(12)]
    vertical=[a|b for a,b in zip(up,down)]
    horizontal=[a|b for a,b in zip(left,right)]
    for i,rows in enumerate((up,left,right,vertical,horizontal)):
        glyphs[0xe000+i]=list(rows)
    return glyphs

def main():
    counts = Counter()
    locations = {}
    files = set()
    for path, line, text in translations():
        files.add(path)
        for ch in text:
            counts[ch] += 1
            if ch not in locations:
                locations[ch] = (path.relative_to(ROOT), line)
    missing_gb = []
    for ch in sorted(counts):
        try: ch.encode('gb2312')
        except UnicodeEncodeError: missing_gb.append(ch)
    glyphs = read_font()
    chars = set(counts) | ui_characters()
    extra = GBA / 'src/story_glyphs.txt'
    if extra.exists(): chars.update(extra.read_text())
    ending=GBA/'src/ending_glyphs.txt'
    if ending.exists():chars.update(ending.read_text())
    chars -= {'\n', '\r', '\t'}
    missing_font = sorted(ch for ch in chars if ord(ch) not in glyphs)
    report = [f'文件数: {len(files)}', f'译文字符种类: {len(counts)}', f'GB2312 无法编码: {len(missing_gb)} 种 / {sum(counts[c] for c in missing_gb)} 次', '']
    for ch in missing_gb:
        p, line = locations[ch]
        report.append(f'U+{ord(ch):04X} {ch} {counts[ch]} 次 {p}:{line}')
    report += ['', f'字体缺字（含 UI）: {len(missing_font)}', ' '.join(f'U+{ord(ch):04X} {ch}' for ch in missing_font)]
    (generated_path('reports/font-coverage.txt')).write_text('\n'.join(report) + '\n', encoding='utf-8')
    print('\n'.join(report))
    if missing_font: raise SystemExit('Font is missing glyphs')
    chars = sorted(chars, key=ord)
    with (GBA / 'src/font_data.c').open('w', encoding='utf-8') as f:
        f.write('#include <stdint.h>\nconst uint32_t font_codes[] = {\n')
        for ch in chars: f.write(f'0x{ord(ch):x},')
        f.write('\n};\nconst uint16_t font_rows[][12] = {\n')
        for ch in chars: f.write('{' + ','.join(f'0x{x:03x}' for x in glyphs[ord(ch)]) + '},\n')
        f.write(f'}};\nconst unsigned font_count = {len(chars)};\n')
        bad_end={}
        with gzip.open(GBA/'assets/unifont-17.0.03.hex.gz','rt') as source:
            for line in source:
                code,bitmap=line.strip().split(':')
                if int(code,16) in map(ord,'BAD END'):
                    bad_end[chr(int(code,16))]=bytes.fromhex(bitmap)
        f.write('const uint8_t bad_end_rows[7][16] = {\n')
        for ch in 'BAD END':
            assert len(bad_end[ch])==16
            f.write('{'+','.join(str(row) for row in bad_end[ch])+'},\n')
        f.write('};\n')
    print(f'裁剪后: {len(chars)} 字形, {len(chars)*28} 字节（索引和点阵）')

if __name__ == '__main__': main()
