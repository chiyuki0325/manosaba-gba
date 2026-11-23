"""Walk the reported camera shots and reject clipping, scanline loss and Y wrap."""
from concurrent.futures import ThreadPoolExecutor
from collections import defaultdict
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
    def run(group):
        path,cases=group;c=catalog[path];directory=out/path.split('/')[-1];directory.mkdir(exist_ok=True)
        plan=directory/'plan.txt'
        rows=[]
        for line,cg,*timing in sorted(cases):
            offset=timing[0] if timing else 160
            rows.append(f"camera{line} {c['bank']} {c['event_lines'][str(line)]} {offset} {scripts.index(path)} {cg}\n")
        plan.write_text(''.join(rows))
        result=subprocess.run([str(test),str(args.rom),str(directory),address,str(plan)],
                              env=dict(os.environ,MANOSABA_ASSERT_COMPLETE_OBJECTS='1'),capture_output=True,text=True,timeout=120)
        log=result.stdout+result.stderr;(directory/'run.log').write_text(log)
        print(path+'\n'+log,flush=True)
        return result.returncode==0
    cases=[('Act01_Chapter01/Act01_Chapter01_Adv04',66,0),('Act01_Chapter01/Act01_Chapter01_Adv10',21,0),
           ('Act02_Chapter01/Act02_Chapter01_Bad01',225,0),('Act02_Chapter05/Act02_Chapter05_Bad01',575,0),
           ('Act01_Chapter01/Act01_Chapter01_Adv35',360,0,10),
           ('Act01_Chapter01/Act01_Chapter01_Adv35',364,2),('Act01_Chapter01/Act01_Chapter01_Adv35',697,3),
           ('Act01_Chapter02/Act01_Chapter02_Adv27',506,1),
           ('Act02_Chapter01/Act02_Chapter01_Adv24',152,0),('Act02_Chapter01/Act02_Chapter01_Adv24',164,0),
           ('Act02_Chapter01/Act02_Chapter01_Adv24',168,0),('Act02_Chapter03/Act02_Chapter03_Adv10',420,0)]
    # One continuous walk per script; keep one witness for each changed pose.
    for path,lines in [('Act01_Chapter01/Act01_Chapter01_Adv35',[84,91,127,166,236,341,353]),
                       ('Act01_Chapter01/Act01_Chapter01_Adv36',[136,369]),
                       ('Act01_Chapter02/Act01_Chapter02_Adv17',[154,165]),
                       ('Act01_Chapter02/Act01_Chapter02_Adv27',[639]),
                       ('Act01_Chapter04/Act01_Chapter04_Adv02',[261]),
                       ('Act01_Chapter05/Act01_Chapter05_Adv07',[574]),
                       ('Act01_Chapter05/Act01_Chapter05_Adv10',[386])]:
        cases.extend((path,line,0) for line in lines)
    groups=defaultdict(list)
    for path,*case in cases:groups[path].append(case)
    with ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 1)) as pool:passed=all(list(pool.map(run,groups.items())))
    raise SystemExit(0 if passed else 1)


if __name__=='__main__':main()
