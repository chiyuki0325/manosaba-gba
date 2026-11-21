"""Exercise actual PCM, live menus, native cues and stack on the integrated ROM."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,wave
from PIL import Image

root=Path(__file__).resolve().parents[1]
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
def address(name):return next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
data=(root/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
paths=[json.loads(row[1:].split(',',1)[0]) for row in data.splitlines() if row.startswith('{')]
first=int(re.search(r'story_trial_first = (\d+)',(root/'src/story_data.cpp').read_text())[1])
offset=paths.index('Act02_Chapter05/Act02_Chapter05_Trial28')-first
test=out/'hiro_game_test'
subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/hiro_game_test.c'),'-lmgba','-o',str(test)],check=True)
catalog=json.loads((root/'assets/music/catalog.json').read_text())
demo_offset=next(i for i,c in enumerate(catalog) if c['id']=='Bgm037')-next(i for i,c in enumerate(catalog) if c['id']=='Bgm019')
run=subprocess.run([str(test),str(rom),str(out),address('mmLayerMain'),address('__iwram_end'),str(offset),str(demo_offset)],capture_output=True,text=True)
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
report=dict(passed=run.returncode==0,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),real_audio=True,music_channels=16,sfx_channels=2)
match=re.search(r'peak=(\d+) samples=(\d+) nonzero=(\d+) stack_used=(\d+) stack_gap=(\d+)',run.stdout)
if match:report.update(dict(zip(('pcm_peak','samples','nonzero','stack_used','stack_gap'),map(int,match.groups()))))
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
for path in out.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
raw=out/'hiro-stereo.raw'
if raw.exists():
    with wave.open(str(out/'hiro-stereo.wav'),'wb') as audio:
        audio.setnchannels(2);audio.setsampwidth(2);audio.setframerate(32768);audio.writeframes(raw.read_bytes())
raise SystemExit(run.returncode)
