"""Patrol source red/green/white/black screen effects with real audio and pause."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from PIL import Image
from host_test_cache import GBA,build,planned_routes


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    rom=args.rom.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    addr=lambda name:next(line.split()[0] for line in symbols.splitlines() if line.endswith(' '+name))
    from story import Compiler,load_documents
    documents,_=load_documents();compiler=Compiler(documents)
    for document in documents:compiler.compile(document)
    compiler.link();context=compiler.trials.screen_contexts
    tool=build('trial_walk_test',[GBA/'tools/trial_walk_test.c'])
    unit=build('screen_effect_test',[GBA/'tools/screen_effect_test.cpp'],compiler='c++',flags=('-std=c++17','-O2','-Wall','-Wextra','-I'+str(GBA/'src')),libraries=())
    subprocess.run([str(unit)],check=True)
    cases=[]
    for suffix in ('12','19','21'):
        path='Act02_Chapter05/Act02_Chapter05_Trial'+suffix
        routes,_=planned_routes('segment',[path]);route=next(r for r in routes if r['path']==path)
        directory=out/path.split('/')[-1];directory.mkdir(exist_ok=True);plan=directory/'plan.txt'
        terminal=dict(route['terminal'],action=0,arg=0,kind=0)
        plan.write_text(f'{route["offset"]} {len(route["steps"])}\n'+''.join(' '.join(str(s[k]) for k in ('script','pc','state','action','arg','kind'))+'\n' for s in route['steps']+[terminal]))
        watched=[r for r in context if r['path']==path and (r['payload'][0]==1 or r['payload'][-1]&128)]
        color=32767;amount=0;modes={}
        for event in (r for r in context if r['path']==path):
            raw=event['payload']
            if raw[0]==0:
                if not raw[-1]&1:color=raw[1]|raw[2]<<8
                amount=raw[3]|raw[4]<<8
            r,g,b=color&31,(color>>5)&31,(color>>10)&31
            modes[event['pc']]=0 if not amount else 3 if min(r,g,b)>28 else 4 if not color else 1 if r>g and r>b else 2 if g>r and g>b else 5
        env=dict(os.environ,MANOSABA_TRIAL_SCREEN_PCS=','.join(str(r['pc']) for r in watched))
        env['MANOSABA_TRIAL_SCREEN_MODES']=','.join(str(modes[r['pc']]) for r in watched)
        if suffix=='12':env['MANOSABA_TRIAL_SCREEN_PAUSE_PC']=str(next(r['pc'] for r in watched if r['line']==815))
        cases.append((path,directory,plan,watched,env))
    def run(case):
        path,directory,plan,watched,env=case
        native=subprocess.run([str(tool),str(rom),str(directory),addr('trial_runtime'),addr('witch_book_runtime'),str(plan),'80000'],env=env,capture_output=True,text=True,timeout=180)
        (directory/'native.log').write_text(native.stdout+native.stderr)
        rows=[json.loads(line) for line in native.stdout.splitlines() if line.startswith('{')]
        result=next((r for r in rows if 'status' in r),dict(status='failed'))
        samples=[dict(r,**next({'line':c['line'],'payload':c['payload']} for c in watched if c['pc']==r['screen_pc'])) for r in rows if 'screen_pc' in r]
        result.update(path=path,samples=samples,returncode=native.returncode)
        result['passed']=native.returncode==0 and result['status']=='passed' and result['missed']==0 and result['peak_ticks']<4389
        for image in directory.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
        if path.endswith('Trial12') and result['passed']:
            red=next(r for r in samples if r['line']==722);green=next(r for r in samples if r['line']==819)
            result['red_visible']=red['bright_rgb'][0]>red['bright_rgb'][1]+800
            result['green_visible']=green['bright_rgb'][1]>green['bright_rgb'][0]+120
            result['passed'] &= result['red_visible'] and result['green_visible']
        (directory/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        print(path,'PASS' if result['passed'] else 'FAIL','peak',result.get('peak_ticks'),'missed',result.get('missed'),flush=True)
        return result
    with ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,cases))
    report=dict(passed=all(r['passed'] for r in results),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if not report['passed']:raise SystemExit(1)


if __name__=='__main__':main()
