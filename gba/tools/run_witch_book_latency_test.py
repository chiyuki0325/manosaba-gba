"""Measure book response time; optionally compare every final frame with a baseline ROM."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
NM = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'


def measure(test, rom, out):
    out.mkdir(parents=True, exist_ok=True)
    symbols = subprocess.check_output([NM, str(rom.with_suffix('.elf'))], text=True).splitlines()
    addresses = [next(row.split()[0] for row in symbols if row.endswith(suffix)) for suffix in
                 (' witch_book_runtime', ' _ZN8manosaba20graphics_performanceE')]
    run = subprocess.run([str(test), str(rom), *addresses, str(out)], capture_output=True, text=True)
    (out/'result.log').write_text(run.stdout+run.stderr)
    print(run.stdout, end='')
    if run.returncode:
        raise RuntimeError(run.stderr)
    operations = {}
    for row in run.stdout.splitlines():
        if row.startswith('OP\t'):
            fields = dict(part.split('=') for part in row.split('\t')[2:])
            operations[row.split('\t')[1]] = dict(frames=int(fields['frames']), ms=float(fields['ms']))
    for path in out.glob('*.ppm'):
        Image.open(path).save(path.with_suffix('.png'))
    return dict(rom=str(rom), sha256=hashlib.sha256(rom.read_bytes()).hexdigest(), operations=operations)


def main():
    rom, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    test = out/'witch_book_latency_test'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', str(ROOT/'tools/witch_book_latency_test.c'),
                    '-lmgba', '-o', str(test)], check=True)
    report = measure(test, rom, out)
    if len(sys.argv) > 3:
        baseline = measure(test, Path(sys.argv[3]).resolve(), out/'baseline')
        report['baseline'] = baseline
        report['pixel_equal'] = {}
        report['intentional_ui_changes'] = []
        for name in report['operations']:
            if name == 'map_to_rules' or name.startswith('rule_'):
                report['intentional_ui_changes'].append(name)
                continue
            # The old modal return left 31 pixels of its border over the minimap.
            # Returning must match the pre-modal map, including the minimap frame.
            reference_name = 'next_map_place' if name == 'map_detail_back' else name
            a, b = Image.open(out/(name+'.png')), Image.open(out/'baseline'/(reference_name+'.png'))
            equal = ImageChops.difference(a, b).getbbox() is None
            report['pixel_equal'][name] = equal
            if not equal:
                raise AssertionError('changed book appearance: '+name)
    # Keep interactive updates distinctly faster than the former full-page redraws.
    limits = {'cold_open_clue': 38, 'warm_open_same_clue': 26, 'next_profile': 30,
              'profile_full_image': 28, 'cached_previous_profile': 22, 'detail_to_cached_profile': 22,
              'map_detail': 14, 'map_detail_back': 14, 'next_floor': 26, 'previous_floor': 27, 'same_view_fire_to_water': 8,
              'rule_scroll_one_line': 10, 'rule_scroll_second_line': 10,
              'rule_page_forward': 11, 'rule_page_back': 11}
    report['passed'] = all(report['operations'][name]['frames'] <= limit for name, limit in limits.items())
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    if not report['passed']:
        raise AssertionError('book response time exceeds the regression budget')
    print('PASS: book response time'+('; unchanged final frames match baseline' if 'baseline' in report else ''))


if __name__ == '__main__':
    main()
