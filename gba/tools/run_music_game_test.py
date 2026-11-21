"""Exercise the full DEMO catalog and an unmodified production BGM cue."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys
from PIL import Image

root=Path(__file__).resolve().parents[1]
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
def address(name):return next(int(row.split()[0],16) for row in nm.splitlines() if row.endswith(' '+name))
bank_address=address('bn_audio_soundbank_bin');data=rom.read_bytes();start=bank_address-0x08000000
samples,songs=struct.unpack_from('<HH',data,start)
modules=re.findall(r'MUSIC_MODULE\((\w+)\)',(root/'src/music_modules.inc').read_text())
assert len(modules)==songs
def song(asset):return bank_address+struct.unpack_from('<I',data,start+12+4*(samples+modules.index(asset)))[0]+8
catalog=json.loads((root/'assets/music/catalog.json').read_text())
plan=out/'catalog.txt'
plan.write_text(''.join(f'{c["id"]} {song(c["asset"]) if c.get("asset") else 0:x}\n' for c in catalog))
test=out/'music_game_test'
source=(root/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
paths=[json.loads(row[1:].split(',',1)[0]) for row in source.splitlines() if row.startswith('{')]
subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/music_game_test.c'),'-lmgba','-o',str(test)],check=True)
command=[str(test),str(rom),str(out),f'{address("mmLayerMain"):x}',f'{address("__iwram_end"):x}',str(plan),
         str(next(i for i,c in enumerate(catalog) if c['id']=='Bgm019')),f'{song("bgm026_001"):x}',
         str(paths.index('Act02_Chapter05/Act02_Chapter05_Adv08')),f'{song("bgm002_001_intro"):x}']
manifest=json.loads((root/'assets/music/runtime.json').read_text())
if manifest.get('intro_links'):
    first_trial=int(re.search(r'story_trial_first\s*=\s*(\d+)',(root/'src/story_data.cpp').read_text())[1])
    body=next(c for c in manifest['entries'] if c['id']=='Bgm048')
    command += [str(paths.index('Act02_Chapter06/Act02_Chapter06_Trial02')-first_trial),f'{song(body["asset"]):x}',str(body['intro_order']),f'{address("_ZN8manosaba20graphics_performanceE"):x}']
run=subprocess.run(command,capture_output=True,text=True)
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
report=dict(passed=run.returncode==0,sha256=hashlib.sha256(data).hexdigest(),modules=songs,samples=samples,
            catalog=len(catalog),produced=sum(bool(c.get('asset')) for c in catalog))
match=re.search(r'peak=(\d+) samples=(\d+) nonzero=(\d+) stack_used=(\d+) stack_gap=(\d+)',run.stdout)
if match:report.update(dict(zip(('pcm_peak','pcm_samples','nonzero','stack_used','stack_gap'),map(int,match.groups()))))
paired=re.search(r'paired_intro body_order=(\d+) trial_peak=(\d+)',run.stdout)
if paired:report['paired_intro']=dict(zip(('body_order','trial_peak_ticks'),map(int,paired.groups())))
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
for path in out.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
raise SystemExit(run.returncode)
