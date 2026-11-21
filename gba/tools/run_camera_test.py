"""Walk the reported camera shots and reject clipping, scanline loss and Y wrap."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,json,os,subprocess
from host_test_cache import GBA,build


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    catalog={r['path']:r for r in json.loads((GBA/'assets/graphics/catalog.json').read_text())['chapters']}
    source=(GBA/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    scripts=[json.loads(r[1:].split(',',1)[0]) for r in source.splitlines() if r.startswith('{')]
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(args.rom.with_suffix('.elf'))],text=True)
    address=next(r.split()[0] for r in symbols.splitlines() if r.endswith(' _ZN8manosaba20graphics_performanceE'))
    test=build('graphics_budget_scene_probe',[GBA/'tools/graphics_budget_scene_probe.c'])
    def run(case):
        path,line=case;c=catalog[path];directory=out/path.split('/')[-1];directory.mkdir(exist_ok=True)
        plan=directory/'plan.txt'
        plan.write_text(f"camera {c['bank']} {c['event_lines'][str(line)]} 160 {scripts.index(path)}\n")
        result=subprocess.run([str(test),str(args.rom),str(directory),address,str(plan)],
                              env=dict(os.environ,MANOSABA_ASSERT_COMPLETE_OBJECTS='1'),capture_output=True,text=True,timeout=120)
        log=result.stdout+result.stderr;(directory/'run.log').write_text(log)
        print(path+'\n'+log,flush=True)
        return result.returncode==0
    cases=[('Act01_Chapter01/Act01_Chapter01_Adv04',66),('Act01_Chapter01/Act01_Chapter01_Adv10',21),
           ('Act02_Chapter01/Act02_Chapter01_Bad01',225)]
    with ThreadPoolExecutor(max_workers=3) as pool:passed=all(list(pool.map(run,cases)))
    raise SystemExit(0 if passed else 1)


if __name__=='__main__':main()
