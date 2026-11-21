"""Run all eight original execution-button entries concurrently on the actual ROM."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib,json,re,subprocess,sys
from PIL import Image
from host_test_cache import build,GBA

def main():
    rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda name:next(row.split()[0] for row in nm.splitlines() if row.endswith(' '+name))
    test=build('execution_game_test',[GBA/'tools/execution_game_test.c'])
    scripts=sorted(p for p in (GBA.parent/'story/zh-Hans').glob('Act*/*.json') if re.search(r'_(Adv|Bad)\d+\.json$',p.name))
    cases=[(i,p) for i,p in enumerate(scripts) if any(e['op']=='ShowUI' and 'ExecutionButton' in e.get('params',{}).get('UINames',[]) for e in json.loads(p.read_text())['events'])]
    if '--quick' in sys.argv:cases=[next(c for c in cases if c[1].stem==name) for name in ('Act01_Chapter04_Adv13','Act02_Chapter04_Adv14')]
    from execution_assets import button,indexed,COLORS
    from witch_book_icons import key_icon
    for pressed in (False,True):
        bounds=button(0,pressed).getchannel('A').point(lambda a:255 if a>=128 else 0).getbbox()
        assert 88+bounds[0]==240-(88+bounds[2]),'execution button centered in both poses'
    key=key_icon('A');key=key.crop(key.getbbox());expected=indexed(key)
    key_x,key_y=240-6-key.width,160-6-key.height
    def run(case):
        index,path=case;directory=out/path.stem;directory.mkdir(exist_ok=True)
        result=subprocess.run([str(test),str(rom),str(directory),address('execution_runtime'),address('save_runtime'),str(index)],capture_output=True,text=True,timeout=120)
        log=result.stdout+result.stderr;(directory/'result.log').write_text(log);print(path.stem+': '+log,flush=True)
        for image in directory.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
        aligned=True
        if result.returncode==0:
            image=Image.open(directory/'idle.png').convert('RGB')
            for y in range(key.height):
                for x in range(key.width):
                    ink=expected.getpixel((x,y))
                    if ink:
                        color=tuple((v>>3)*8+(v>>5) for v in COLORS[ink])
                        aligned &= image.getpixel((key_x+x,key_y+y))==color
            if not aligned:print(path.stem+': FAIL A glyph placement/palette')
        return dict(path=path.stem,passed=result.returncode==0 and aligned,key_margin_right=6,key_margin_bottom=6,log=log)
    with ThreadPoolExecutor(max_workers=8) as pool:results=list(pool.map(run,cases))
    vm_test=build('execution_vm_test',[GBA/'tools/execution_vm_test.cpp',GBA/'src/story_vm.cpp',GBA/'src/lzss.cpp',GBA/'src/story_data.cpp'],compiler='g++',flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src')),libraries=())
    host=subprocess.run([str(vm_test)],capture_output=True,text=True);print(host.stdout,end='');print(host.stderr,end='',file=sys.stderr)
    report=dict(vm_passed=host.returncode==0,passed=all(r['passed'] for r in results) and host.returncode==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),results=results)
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
