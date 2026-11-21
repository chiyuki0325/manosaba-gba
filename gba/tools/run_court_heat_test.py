"""Source fire on/off, dynamic HDMA, unchanged actors and real overlay returns."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from concurrent.futures import ThreadPoolExecutor
from PIL import Image
from host_test_cache import build,planned_routes,GBA
from story_layout import clean_text

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    rom=args.rom.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    addr=lambda name:next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
    tool=build('trial_walk_test',[GBA/'tools/trial_walk_test.c'])
    normalize=lambda text:''.join(clean_text(text).split())
    cases=[]
    for number,line,enabled in ((20,55,1),(21,671,0)):
        path=f'Act02_Chapter05/Act02_Chapter05_Trial{number}'
        doc=json.loads((GBA.parent/'story/zh-Hans'/f'{path}.json').read_text())
        phrase=next(e['params']['Text'] for e in doc['events'] if e['line']>line and e['op']=='PrintTextModified')
        routes,_=planned_routes('segment',[path]);route=next(r for r in routes if r['path']==path)
        target=next(s for s in route['steps'] if s['state']==1 and (enabled or normalize(s['text'])==normalize(phrase)))
        directory=out/f'Trial{number}';directory.mkdir(exist_ok=True);plan=directory/'plan.txt'
        terminal=dict(route['terminal'],action=0,arg=0,kind=0)
        plan.write_text(f'{route["offset"]} {len(route["steps"])}\n'+''.join(' '.join(str(s[k]) for k in ('script','pc','state','action','arg','kind'))+'\n' for s in route['steps']+[terminal]))
        env=dict(os.environ,MANOSABA_TRIAL_HEAT_TEST=str(enabled),MANOSABA_TRIAL_CAPTURE_PC=str(target['pc']),MANOSABA_TRIAL_CAPTURE_STATE='1')
        cases.append((directory,plan,env))
    def run(case):
        directory,plan,env=case
        native=subprocess.run([str(tool),str(rom),str(directory),addr('trial_runtime'),addr('witch_book_runtime'),str(plan),'80000'],env=env,capture_output=True,text=True,timeout=180)
        (directory/'native.log').write_text(native.stdout+native.stderr)
        rows=[json.loads(r) for r in native.stdout.splitlines() if r.startswith('{')]
        result=dict(name=directory.name,passed=native.returncode==0,observations=rows)
        for picture in directory.glob('*.ppm'):Image.open(picture).save(picture.with_suffix('.png'))
        frames=[Image.open(p).resize((720,480),Image.Resampling.NEAREST) for p in sorted(directory.glob('heat-??.png'))]
        if frames:frames[0].save(directory/'heat.gif',save_all=True,append_images=frames[1:],duration=67,loop=0)
        print(directory.name,'PASS' if result['passed'] else 'FAIL',flush=True)
        return result
    with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,cases))
    report=dict(passed=all(r['passed'] for r in results),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
