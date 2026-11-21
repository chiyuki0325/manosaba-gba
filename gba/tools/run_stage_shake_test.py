"""Walk the three source ADV shake sites with the production music/SFX flow."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
from PIL import Image
from host_test_cache import GBA,build

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);rom=out/'patrol.gba'
    shutil.copy2(args.rom,rom);shutil.copy2(args.rom.with_suffix('.elf'),rom.with_suffix('.elf'))
    source=(GBA/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(r[1:].split(',',1)[0]) for r in source.splitlines() if r.startswith('{')]
    catalog={r['path']:r for r in json.loads((GBA/'assets/graphics/catalog.json').read_text())['chapters']}
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=next(r.split()[0] for r in nm.splitlines() if r.endswith(' _ZN8manosaba20graphics_performanceE'))
    test=build('stage_shake_game_test',[GBA/'tools/stage_shake_game_test.c'])
    def run(row):
        path,line=row;d=out/path.split('/')[-1];d.mkdir(exist_ok=True);c=catalog[path]
        result=subprocess.run([str(test),str(rom),str(d),address,str(paths.index(path)),str(c['bank']),str(c['event_lines'][str(line)])],capture_output=True,text=True,timeout=120)
        log=result.stdout+result.stderr;(d/'run.log').write_text(log)
        for image in d.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
        print(path,log,flush=True);return dict(path=path,line=line,passed=result.returncode==0,log=log)
    cases=[('Act01_Chapter01/Act01_Chapter01_Adv18',401),('Act01_Chapter01/Act01_Chapter01_Adv21',62),('Act01_Chapter02/Act01_Chapter02_Adv01',532)]
    with ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,cases))
    report=dict(rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),passed=all(r['passed'] for r in results),cases=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
