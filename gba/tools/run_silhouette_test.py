"""Resolve symbols from the matching ELF and test actual GBA tile sharing."""
import json
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
swap=len(sys.argv)>3 and sys.argv[3]=='swap'
nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
def address(name):return next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
assets=json.loads((root/'assets/graphics-cache/linked/graphics.json').read_text())['assets']
rectangle=next(i for i,key in enumerate(sorted(assets)) if assets[key]['kind']=='rectangle' and assets[key]['size']==[480,320])
warden=[]
if not swap:
    from graphics_project import ContentCache
    cache=ContentCache()
    chapter=json.loads((cache.root/'chapters/Act01_Chapter01/Act01_Chapter01_Adv02.json').read_text())
    actor=next(row for row in chapter['characters'] if row['who']=='Warden')
    source=chapter['character_assets'][str(actor['line'])]
    warden=[str(sorted(assets).index(source))]
test=out/('actor_swap_test' if swap else 'silhouette_test')
source='actor_swap_test.c' if swap else 'silhouette_sharing_test.c'
subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools'/source),'-lmgba','-o',str(test)],check=True)
run=subprocess.run([str(test),str(rom),str(out),address('_ZN8manosaba20graphics_performanceE'),address('_ZN8manosaba14graphics_banksE')]+([] if swap else [str(rectangle),*warden]),text=True,capture_output=True)
(out/('actor-swap.log' if swap else 'silhouette.log')).write_text(run.stdout+run.stderr)
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
raise SystemExit(run.returncode)
