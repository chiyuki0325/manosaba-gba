"""Pixel-exact startup notice and real cold-boot/title navigation."""
from warning_screen_mockup import render
from pathlib import Path
import hashlib,json,subprocess,sys
from PIL import Image
from host_test_cache import GBA,build
rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
test=build('warning_screen_game_test',[GBA/'tools/warning_screen_game_test.c'])
run=subprocess.run([str(test),str(rom),str(out)],capture_output=True,text=True,timeout=60)
log=run.stdout+run.stderr;(out/'native.log').write_text(log);print(log,end='')
if run.returncode:sys.exit(run.returncode)
expected=render()[0].convert('RGB').point(lambda v:((v>>3)<<3)|(v>>5))
actual=Image.open(out/'warning.ppm').convert('RGB');a=expected.tobytes();b=actual.tobytes();difference=sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),3))
for p in out.glob('*.ppm'):Image.open(p).resize((960,640),Image.Resampling.NEAREST).save(p.with_suffix('.png'))
report=dict(passed=difference==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),different_pixels=difference,native=log)
(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
assert difference==0,f'{difference} pixels differ from the approved layout'
