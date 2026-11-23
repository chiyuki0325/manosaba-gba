"""Flash/SRAM power cuts, real-script snapshots and native save/load with live music."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,re,subprocess,os
from PIL import Image
from host_test_cache import GBA,build

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--ui-only',action='store_true');parser.add_argument('--sram',action='store_true');args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);rom=args.rom.resolve()
    flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src'),'-DMANOSABA_SAVE_SRAM='+str(int(args.sram)))
    def host(spec):
        name,sources=spec;test=build(name,[GBA/p for p in sources],compiler='g++',flags=flags,libraries=())
        run=subprocess.run([str(test)],capture_output=True,text=True);(out/(name+'.log')).write_text(run.stdout+run.stderr)
        print(run.stdout,end='');print(run.stderr,end='');return run.returncode==0
    specs=[('save_store_test',['tools/sram_store_test.cpp' if args.sram else 'tools/save_store_test.cpp','src/save_store.cpp','src/save_store_sram.cpp']),
           ('save_snapshot_test',['tools/save_snapshot_test.cpp','src/story_vm.cpp','src/lzss.cpp','src/story_data.cpp','src/stage_player.cpp'])]
    with ThreadPoolExecutor(max_workers=2) as pool:passed=all(pool.map(host,specs)) if not args.ui_only else True
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda name:next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
    test=build('save_game_test',[GBA/'tools/save_game_test.c'],flags=('-O2','-Wall','-Wextra','-DMANOSABA_TEST_SRAM='+str(int(args.sram))))
    with (out/'native.log').open('w') as handle:
        run=subprocess.run([str(test),str(rom),str(out),address('save_runtime'),address('trial_runtime'),address('_ZN8manosaba20graphics_performanceE')],stdout=handle,stderr=subprocess.STDOUT,timeout=180,env=dict(os.environ,MANOSABA_SAVE_UI_ONLY="1") if args.ui_only else dict(os.environ))
    native=(out/'native.log').read_text();print(native,end='')
    peak=re.search(r'menu_peak=(\d+)',native)
    report=dict(ui_only=args.ui_only,passed=passed and run.returncode==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),native_passed=run.returncode==0,menu_peak_ticks=int(peak[1]) if peak else None)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    for image in out.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
