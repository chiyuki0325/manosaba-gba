"""Real choice checkpoints: seed size, native bad ending, restore and load."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,subprocess
from PIL import Image
from host_test_cache import GBA,build

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    def host():
        sources=('tools/story_retry_size_test.cpp','src/story_vm.cpp','src/lzss.cpp','src/story_data.cpp','src/stage_player.cpp','src/graphics_data.cpp')
        exe=build('story_retry_size_test',[GBA/s for s in sources],compiler='g++',flags=('-std=c++17','-O2','-Wall','-Wextra','-I'+str(GBA/'src')),libraries=())
        return subprocess.run([str(exe)],capture_output=True,text=True)
    def native():
        exe=build('story_retry_game_test',[GBA/'tools/story_retry_game_test.c'])
        return subprocess.run([str(exe),str(a.rom.resolve()),str(out)],capture_output=True,text=True,timeout=150)
    passed=True
    with ThreadPoolExecutor(max_workers=2) as pool:
        for name,r in zip(('host','native'),pool.map(lambda f:f(),(host,native))):
            (out/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');passed&=r.returncode==0
    report=dict(passed=passed,rom_sha256=hashlib.sha256(a.rom.read_bytes()).hexdigest())
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    for image in out.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
    if not passed:raise SystemExit(1)
if __name__=='__main__':main()
