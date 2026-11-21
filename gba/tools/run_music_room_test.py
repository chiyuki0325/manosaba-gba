"""Music collection mapping, Flash COW and actual title/story/music-room paths."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,subprocess,sys,hashlib
from PIL import Image
from host_test_cache import GBA,build

def main():
    rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src'))
    specs=[('music_collection_test',['tools/music_collection_test.cpp','src/music_room_data.cpp']),('music_player_test',['tools/music_player_test.cpp','src/music_player.cpp','src/music_cues.cpp'])]
    def host(spec):
        name,paths=spec;test=build(name,[GBA/p for p in paths],compiler='g++',flags=flags,libraries=())
        result=subprocess.run([str(test)],capture_output=True,text=True);(out/(name+'.log')).write_text(result.stdout+result.stderr);print(name,result.returncode,result.stdout+result.stderr,flush=True);return result.returncode==0
    with ThreadPoolExecutor(max_workers=3) as pool:passed=all(pool.map(host,specs))
    assert passed,'host checks failed'
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    def address(name):return next(r.split()[0] for r in nm.splitlines() if r.endswith(' '+name))
    rows=json.loads((GBA/'assets/music-room/catalog.json').read_text())['tracks'];index=next(i for i,r in enumerate(rows) if r['id']=='Bgm026')
    test=build('music_room_game_test',[GBA/'tools/music_room_game_test.c'])
    hiro=next(i for i,r in enumerate(rows) if r['id']=='Bgm037')
    result=subprocess.run([str(test),str(rom),str(out),*[address(n) for n in ('music_room_runtime','home_runtime','save_runtime','mmLayerMain','_ZN8manosaba20graphics_performanceE')],str(index),str(rows[index]['bit']),str(hiro),str(len(rows))],capture_output=True,text=True,timeout=180)
    log=result.stdout+result.stderr;(out/'native.log').write_text(log);print(log,end='')
    for p in out.glob('*.ppm'):Image.open(p).resize((960,640),Image.Resampling.NEAREST).save(p.with_suffix('.png'))
    (out/'report.json').write_text(json.dumps(dict(passed=result.returncode==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),tracks=len(rows),native=log),ensure_ascii=False,indent=2)+'\n')
    raise SystemExit(result.returncode)
if __name__=='__main__':main()
