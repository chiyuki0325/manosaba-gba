"""Targeted real-script toast conversion, lifetime and native pixel checks."""
from pathlib import Path
import argparse, hashlib, json, subprocess
from PIL import Image
from host_test_cache import GBA, build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    rom, out = args.rom.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    route = build('toast_route_test', [GBA/'tools/toast_route_test.cpp', GBA/'src/story_vm.cpp',
                  GBA/'src/lzss.cpp', GBA/'src/story_data.cpp'], compiler='g++',
                  flags=('-std=c++17', '-O2', '-Wall', '-Wextra', '-I'+str(GBA/'src')), libraries=())
    subprocess.run([route, out/'plan.txt'], check=True)
    rows = subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',
                                    rom.with_suffix('.elf')], text=True).splitlines()
    symbols = [next(row.split()[0] for row in rows if name in row) for name in
               ('_ZN8manosaba11toast_tilesE', '12canvas_tilesE', ' trial_runtime', ' witch_book_runtime')]
    native = build('toast_game_test', [GBA/'tools/toast_game_test.c'])
    run = subprocess.run([native, rom, out, *symbols, out/'plan.txt'], capture_output=True, text=True)
    print(run.stdout+run.stderr, end='')
    (out/'native.log').write_text(run.stdout+run.stderr)
    (out/'report.json').write_text(json.dumps(dict(passed=run.returncode==0,
        rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest()), indent=2)+'\n')
    for path in out.glob('*.ppm'):
        Image.open(path).save(path.with_suffix('.png'))
    raise SystemExit(run.returncode)


if __name__ == '__main__':
    main()
