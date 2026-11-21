"""Real-input regressions for A reveal scheduling and protagonist timer colors."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from PIL import Image
from host_test_cache import GBA,build,planned_routes


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    rom=args.rom.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda name:next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
    tool=build('trial_walk_test',[GBA/'tools/trial_walk_test.c'])
    cases=[('reveal','Act02_Chapter06/Act02_Chapter06_Trial08',1),
           ('ema-bar','Act01_Chapter01/Act01_Chapter01_Trial01',6),
           ('hiro-bar','Act02_Chapter01/Act02_Chapter01_Trial01',6)]
    plans=[]
    for name,path,state in cases:
        routes,_=planned_routes('segment',[path]);route=next(r for r in routes if r['path']==path)
        target=next(s for s in route['steps'] if ('因为她不是' in s['text'] if name=='reveal' else s['state']==state))
        directory=out/name;directory.mkdir(exist_ok=True);plan=directory/'plan.txt'
        terminal=dict(route['terminal'],action=0,arg=0,kind=0)
        plan.write_text(f'{route["offset"]} {len(route["steps"])}\n'+''.join(' '.join(str(s[k]) for k in ('script','pc','state','action','arg','kind'))+'\n' for s in route['steps']+[terminal]))
        env=dict(os.environ,MANOSABA_TRIAL_CAPTURE_PC=str(target['pc']),MANOSABA_TRIAL_CAPTURE_STATE=str(state))
        if name=='reveal':env['MANOSABA_TRIAL_REVEAL_TEST']='1'
        else:env['MANOSABA_TRIAL_TIMER_COLOR']=f'{(27|7<<5|11<<10) if name=="hiro-bar" else (31|16<<5|21<<10):x}'
        plans.append((name,path,directory,plan,env))
    def run(case):
        name,path,directory,plan,env=case
        native=subprocess.run([str(tool),str(rom),str(directory),address('trial_runtime'),address('witch_book_runtime'),str(plan),'50000'],env=env,capture_output=True,text=True,timeout=120)
        (directory/'native.log').write_text(native.stdout+native.stderr)
        rows=[json.loads(line) for line in native.stdout.splitlines() if line.startswith('{')]
        for picture in directory.glob('*.ppm'):Image.open(picture).save(picture.with_suffix('.png'))
        result=dict(name=name,path=path,passed=native.returncode==0,observations=rows)
        print(name,'PASS' if result['passed'] else 'FAIL',flush=True)
        return result
    with ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,plans))
    report=dict(passed=all(r['passed'] for r in results),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if not report['passed']:raise SystemExit(1)


if __name__=='__main__':main()
