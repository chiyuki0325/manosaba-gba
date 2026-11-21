"""Measure audition latency without changing the ROM or injecting calls."""
from pathlib import Path
import hashlib,json,re,subprocess,sys

root=Path(__file__).resolve().parents[1]
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
layer=next(row.split()[0] for row in nm.splitlines() if row.endswith(' mmLayerMain'))
catalog=json.loads((root/'assets/music/catalog.json').read_text())
assert next(i for i,c in enumerate(catalog) if c['id']=='Bgm019')==16
plan=out/'plan.txt';plan.write_text(''.join(f'{c["id"]} {int(bool(c.get("asset")))}\n' for c in catalog))
test=out/'music_latency_game_test'
subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/music_latency_game_test.c'),'-lmgba','-o',str(test)],check=True)
run=subprocess.run([str(test),str(rom),layer,str(plan)],capture_output=True,text=True)
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
rows=[dict(id=m[0],start_frame=int(m[1]),audible_frame=int(m[2])) for m in re.findall(r'(\w+) start_frame=(\d+) audible_frame=(\d+)',run.stdout)]
(out/'result.json').write_text(json.dumps(dict(passed=run.returncode==0,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),audible_threshold=128,rows=rows),indent=2)+'\n')
raise SystemExit(run.returncode)
