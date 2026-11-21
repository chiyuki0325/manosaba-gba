"""Complete Adv04 with instant text and several rapid A cadences; retain a ROM snapshot."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess
from PIL import Image
from host_test_cache import GBA,build

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True);snapshot=output/'snapshot';snapshot.mkdir(exist_ok=True);rom=snapshot/'game.gba'
    for ext in ('gba','elf'):shutil.copy2(args.rom.with_suffix('.'+ext),rom.with_suffix('.'+ext))
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    profile=next(r.split()[0] for r in symbols.splitlines() if r.endswith(' _ZN8manosaba20graphics_performanceE'))
    test=build('adv_instant_test',[GBA/'tools/adv_instant_test.c'])
    def run(period):
        directory=output/str(period);directory.mkdir(exist_ok=True)
        p=subprocess.run([str(test),str(rom),str(directory),profile,str(period)],capture_output=True,text=True,timeout=120)
        log=p.stdout+p.stderr;(directory/'native.log').write_text(log);print(period,p.returncode,log[-600:],flush=True)
        for path in directory.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
        frames=re.search(r'; (\d+) frames',log)
        return dict(a_period_frames=period,passed=p.returncode==0,frames=int(frames[1]) if frames else None)
    with ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,(4,8,12)))
    report=dict(rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),passed=all(r['passed'] for r in results),cases=results)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
