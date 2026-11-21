"""Exercise production title animations under real HirO and export a preview."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
from PIL import Image

root=Path(__file__).resolve().parents[1];rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
def address(name):return next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+name))
test=out/'trial_title_game_test'
subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/trial_title_game_test.c'),'-lmgba','-o',str(test)],check=True)
stress='0' if '--native' in sys.argv[3:] else '1'
run=subprocess.run([str(test),str(rom),str(out),address('trial_runtime'),address('witch_book_runtime'),address('mmLayerMain'),stress],capture_output=True,text=True)
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
report=dict(passed=run.returncode==0,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),stress_music=stress=='1')
if match:=re.search(r'Titles: frames=(\d+) peak_ticks=(\d+) missed=(\d+)',run.stdout):report.update(dict(zip(('frames','peak_ticks','missed'),map(int,match.groups()))))
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
for p in out.glob('*.ppm'):Image.open(p).save(p.with_suffix('.png'))
for kind,delay in (('begin',167),('end',133)):
    frames=[Image.open(p).convert('RGB') for p in sorted(out.glob(kind+'-0*.ppm'))+sorted(out.glob(kind+'-1*.ppm'))+sorted(out.glob(kind+'-2*.ppm'))]
    if frames:frames[0].save(out/(kind+'.gif'),save_all=True,append_images=frames[1:],duration=delay,loop=0)
raise SystemExit(run.returncode)
