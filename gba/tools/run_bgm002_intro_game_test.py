"""Run the targeted native Intro scenario and save its actual GBA PCM."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

GBA = Path(__file__).resolve().parents[1]
rom = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
nm = subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',
                              str(rom.with_suffix('.elf'))], text=True)


def address(name):
    return next(int(row.split()[0], 16) for row in nm.splitlines() if row.endswith(' ' + name))


data = rom.read_bytes()
bank = address('bn_audio_soundbank_bin')
start = bank - 0x08000000
samples, songs = struct.unpack_from('<HH', data, start)
modules = re.findall(r'MUSIC_MODULE\((\w+)\)', (GBA / 'src/music_modules.inc').read_text())
assert len(modules) == songs


def module(asset):
    return bank + struct.unpack_from('<I', data, start + 12 + 4 * (samples + modules.index(asset)))[0] + 8


source = (GBA / 'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
paths = [json.loads(row[1:].split(',', 1)[0]) for row in source.splitlines() if row.startswith('{')]
binary = out / 'bgm002_intro_game_test'
subprocess.run(['cc', '-O2', '-Wall', '-Wextra', str(GBA / 'tools/bgm002_intro_game_test.c'),
                '-lmgba', '-o', str(binary)], check=True)
command = [str(binary), str(rom), str(out), f'{address("mmLayerMain"):x}',
           f'{address("_ZN8manosaba12music_player12_GLOBAL__N_1L4dataE"):x}',
           str(paths.index('Act02_Chapter05/Act02_Chapter05_Adv08')),
           f'{module("bgm002_001_intro"):x}', f'{module("bgm002_001"):x}']
run = subprocess.run(command, capture_output=True, text=True)
print(run.stdout, end='')
print(run.stderr, end='', file=sys.stderr)
(out / 'result.log').write_text(run.stdout + run.stderr)
report = dict(passed=run.returncode == 0, rom=str(rom), sha256=hashlib.sha256(data).hexdigest())
match = re.search(r'PASS wraps=(\d+) shortest=(\d+) longest=(\d+) fade=(\d+) peak=(\d+) pcm_frames=(\d+)', run.stdout)
if match:
    report.update(dict(zip(('wraps', 'shortest_frames', 'longest_frames', 'fade_frames', 'pcm_peak', 'pcm_frames'),
                           map(int, match.groups()))))
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-f', 's16le', '-ar', '32768', '-ac', '2',
                    '-i', str(out / 'native-intro.s16le'), '-c:a', 'libmp3lame', '-b:a', '160k',
                    str(out / 'native-intro.mp3')], check=True)
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
raise SystemExit(run.returncode)
