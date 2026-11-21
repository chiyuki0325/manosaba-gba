"""Check formal build navigation and absence of debug/demo entry points."""
from pathlib import Path
import hashlib,json,subprocess,sys
from PIL import Image
from host_test_cache import GBA,build
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm','-C',str(rom.with_suffix('.elf'))],text=True)
for name in ('debug_hotkey','manosaba::debug_menu::draw(', 'manosaba::debug_menu::update(', 'manosaba::music_demo::', 'manosaba::sfx_demo::', 'manosaba::draw_scene('):
    assert name not in symbols,name+' unexpectedly linked in release'
test=build('release_menu_game_test',[GBA/'tools/release_menu_game_test.c'])
result=subprocess.run([str(test),str(rom),str(out)],capture_output=True,text=True,timeout=90)
log=result.stdout+result.stderr;print(log,end='');(out/'native.log').write_text(log)
for p in out.glob('*.ppm'):Image.open(p).resize((960,640),Image.Resampling.NEAREST).save(p.with_suffix('.png'))
(out/'report.json').write_text(json.dumps(dict(passed=result.returncode==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),native=log),ensure_ascii=False,indent=2)+'\n')
sys.exit(result.returncode)
