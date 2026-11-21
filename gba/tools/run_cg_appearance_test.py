"""Actual clear/blur occurrences of CG300_1, including book, history and Flash restore."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys
from PIL import Image
from host_test_cache import GBA,build

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    snapshot=output/'snapshot';snapshot.mkdir(exist_ok=True)
    rom=snapshot/'game.gba'
    for extension in ('gba','elf'):shutil.copy2(args.rom.with_suffix('.'+extension),rom.with_suffix('.'+extension))
    unit=subprocess.run([sys.executable,str(GBA/'tools/cg_appearance_policy_test.py')],capture_output=True,text=True)
    (output/'policy.log').write_text(unit.stdout+unit.stderr)
    source=GBA.parent/'story/zh-Hans'
    paths=[str(p.relative_to(source).with_suffix('')) for p in sorted(source.glob('Act*/*.json')) if re.search(r'_(Adv|Bad)\d+\.json$',p.name)]
    names=['Act01_Chapter04/Act01_Chapter04_Adv09','Act01_Chapter04/Act01_Chapter04_Adv07']
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    profile=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' _ZN8manosaba20graphics_performanceE'))
    test=build('cg_appearance_game_test',[GBA/'tools/cg_appearance_game_test.c'])
    run=subprocess.run([str(test),str(rom),str(output),profile,*[str(paths.index(p)) for p in names]],capture_output=True,text=True,timeout=120)
    log=run.stdout+run.stderr;(output/'native.log').write_text(log);print(unit.stdout+unit.stderr+log,end='')
    passed=unit.returncode==0 and run.returncode==0
    report=dict(passed=passed,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),clear_script=names[0],blur_script=names[1],policy_cases=6)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    for path in output.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
