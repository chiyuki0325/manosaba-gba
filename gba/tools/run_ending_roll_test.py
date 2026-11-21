"""Native credits scroll continuity, Chinese cast, music and existing-reader regression."""
from pathlib import Path
import hashlib,json,subprocess,sys
from PIL import Image
from host_test_cache import build,GBA

def main():
    rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda name:next(line.split()[0] for line in nm.splitlines() if line.endswith(' '+name))
    test=build('ending_roll_game_test',[GBA/'tools/ending_roll_game_test.c'])
    import re
    from concurrent.futures import ThreadPoolExecutor
    scripts=sorted(p for p in (GBA.parent/'story/zh-Hans').glob('Act*/*.json') if re.search(r'_(Adv|Bad)\d+\.json$',p.name))
    scenarios=[('debug',[])]
    for act,stem in [(1,'Act01_Chapter05_Adv10'),(2,'Act02_Chapter05_Adv08')]:
        index=next(i for i,p in enumerate(scripts) if p.stem==stem)
        scenarios.append((f'formal-act{act}',[str(index),str(act),address('save_runtime')]))
    def execute(scenario):
        name,extra=scenario;directory=out/name;directory.mkdir(exist_ok=True)
        run=subprocess.run([str(test),str(rom),str(directory),address('credits_runtime'),address('mmLayerMain'),*extra],capture_output=True,text=True)
        print(name+': '+run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(directory/'result.log').write_text(run.stdout+run.stderr)
        return dict(name=name,passed=run.returncode==0,log=run.stdout+run.stderr)
    with ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(execute,scenarios))
    vm_test=build('credits_vm_test',[GBA/'tools/credits_vm_test.cpp',GBA/'src/story_vm.cpp',GBA/'src/lzss.cpp',GBA/'src/story_data.cpp'],compiler='g++',flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src')),libraries=())
    host=subprocess.run([str(vm_test)],capture_output=True,text=True)
    print(host.stdout,end='');print(host.stderr,end='',file=sys.stderr)
    results.append(dict(name='vm',passed=host.returncode==0,log=host.stdout+host.stderr))
    catalog=json.loads((GBA/'assets/ending-roll/catalog.json').read_text())
    report=dict(passed=all(r['passed'] for r in results),scenarios=results,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),cast='existing full Chinese names; original actors',catalog=catalog)
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for image in out.rglob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
    raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
