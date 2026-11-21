#!/usr/bin/env python3
"""Audit XM entry timing and empty export prefixes; never edit musical data."""
from build_paths import generated_path
import hashlib
import json
from pathlib import Path
import struct

GBA = Path(__file__).resolve().parents[1]
EMPTY_EXPORT_IDS = {'Bgm009', 'Bgm010', 'Bgm026',
                    'Bgm032', 'Bgm038', 'Bgm039', 'Bgm045'}


def decode(path):
    data = path.read_bytes()
    count, _, channels, n_patterns = struct.unpack_from('<4H', data, 64)
    speed, bpm = struct.unpack_from('<2H', data, 76)
    orders = list(data[80:80 + count])
    pos = 60 + struct.unpack_from('<I', data, 60)[0]
    patterns = []
    for _ in range(n_patterns):
        header, _, rows, length = struct.unpack_from('<IBHH', data, pos)
        pos += header
        end = pos + length
        pattern = []
        for _ in range(rows):
            cells = []
            for _ in range(channels):
                cell = [0] * 5
                if length:
                    mask = data[pos]
                    pos += 1
                    if mask & 128:
                        for i in range(5):
                            if mask & (1 << i):
                                cell[i] = data[pos]
                                pos += 1
                    else:
                        cell = [mask, *data[pos:pos + 4]]
                        pos += 4
                cells.append(cell)
            pattern.append(cells)
        assert pos == end
        patterns.append(pattern)
    return orders, patterns, speed, bpm


def measure(xm, start, boundary=None):
    orders, patterns, speed, bpm = xm
    order, row, elapsed, attacks, first = start, 0, 0.0, 0, None
    visited = set()
    while order < len(orders):
        if boundary is not None and order == boundary and row == 0:
            break
        state = order, row
        assert state not in visited, 'Unexpected pre-boundary cycle'
        visited.add(state)
        cells = patterns[orders[order]][row]
        onset_count = sum(1 <= c[0] <= 96 for c in cells)
        if onset_count and first is None:
            first = elapsed
        attacks += onset_count
        jump, next_row, delay = None, None, 0
        for _, _, _, effect, value in cells:
            if effect == 15 and value:
                if value < 32:
                    speed = value
                else:
                    bpm = value
            elif effect == 11:
                jump = value
            elif effect == 13:
                next_row = (value >> 4) * 10 + (value & 15)
            elif effect == 14 and value >> 4 == 14:
                delay = value & 15
        elapsed += speed * 2.5 / bpm * (delay + 1)
        if jump is not None or next_row is not None:
            order = order + 1 if jump is None else jump
            row = 0 if next_row is None else next_row
        else:
            row += 1
            if row == len(patterns[orders[order]]):
                order += 1
                row = 0
        if boundary is None and first is not None:
            break
    return dict(seconds=round(elapsed, 6), note_on_count=attacks,
                first_note_on_seconds=None if first is None else round(first, 6))


def main():
    manifest = json.loads((GBA / 'assets/music/runtime.json').read_text())
    result = []
    for entry in manifest['entries']:
        path = GBA / 'audio' / (entry['asset'] + '.xm')
        xm = decode(path)
        start, direct = entry['intro_order'], entry['direct_loop_order']
        prefix = measure(xm, start, direct)
        item = dict(id=entry['id'], asset=entry['asset'], intro_order=start,
                    direct_loop_order=direct,
                    first_note_on_seconds=measure(xm, start)['first_note_on_seconds'],
                    direct_first_note_on_seconds=measure(xm, direct)['first_note_on_seconds'],
                    skipped_prefix_seconds=prefix['seconds'],
                    skipped_prefix_note_ons=prefix['note_on_count'],
                    story_default_order=direct if entry['id'] in EMPTY_EXPORT_IDS else start,
                    demo_default_order=start)
        if entry['id'] in EMPTY_EXPORT_IDS:
            assert prefix['note_on_count'] == 0 and prefix['seconds'] > 0
        result.append(item)
    report = dict(scope='XM scheduling only; PCM audibility and player-command delay not measured',
                  evidence='Existing exporter/loop reports identify empty prefixes; byte-level audit verifies zero note-ons before the boundary',
                  entries=result)
    path = generated_path('reports/music-entry-latency.json')
    path.write_text(json.dumps(report, indent=2) + '\n')
    for e in result:
        print(f"{e['id']:7} first {e['first_note_on_seconds']:9.6f}s  "
              f"skip {e['skipped_prefix_seconds']:9.6f}s/{e['skipped_prefix_note_ons']:3} onsets  "
              f"direct first {e['direct_first_note_on_seconds']:9.6f}s  "
              f"story order {e['story_default_order']}")


if __name__ == '__main__':
    main()
