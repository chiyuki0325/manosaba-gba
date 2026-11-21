"""Native gallery tests on normal ADV and Trial paths, with real music and Flash."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,subprocess,sys,shutil,re
from PIL import Image
from host_test_cache import build,GBA

def main():
    rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    source_rom=rom;frozen=out/'snapshot';frozen.mkdir(exist_ok=True)
    shutil.copy2(rom,frozen/'game.gba');shutil.copy2(rom.with_suffix('.elf'),frozen/'game.elf');rom=frozen/'game.gba'
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    addr=lambda n:next(r.split()[0] for r in nm.splitlines() if r.endswith(' '+n))
    music=(GBA/'src/music_catalog.cpp').read_text();names=re.findall(r'\{"([^"]+)',music)
    hiro_steps=next(i for i,n in enumerate(names) if n.startswith('Bgm037'))-int(re.search(r'music_default_index = (\d+)',music)[1])
    executable=build('gallery_game_test',[GBA/'tools/gallery_game_test.c'])
    def run(mode):
        path=out/('trial' if mode else 'story');path.mkdir(exist_ok=True)
        p=subprocess.run([str(executable),str(rom),str(path),addr('gallery_runtime'),addr('save_runtime'),addr('_ZN8manosaba20graphics_performanceE'),addr('mmLayerMain'),str(mode),str(hiro_steps)],capture_output=True,text=True,timeout=180)
        log=p.stdout+p.stderr;(path/'native.log').write_text(log);print(log,end='');return dict(mode=mode,passed=p.returncode==0,log=log)
    with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,(0,1)))
    for p in out.rglob('*.ppm'):Image.open(p).resize((720,480),Image.Resampling.NEAREST).save(p.with_suffix('.png'))
    report=dict(source_rom=str(source_rom),passed=all(r['passed'] for r in results),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),cases=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
