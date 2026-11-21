"""Run actual story/trial history input on the ROM and matching ELF."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
from PIL import Image

def main():
    root=Path(__file__).resolve().parents[1];rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    addresses=[next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+name)) for name in ('dialogue_history_runtime','trial_runtime','_ZN8manosaba20graphics_performanceE')]
    test=out/'dialogue_history_game_test';subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/dialogue_history_game_test.c'),'-lmgba','-o',str(test)],check=True)
    run=subprocess.run([str(test),str(rom),str(out),*addresses],capture_output=True,text=True)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
    report=dict(passed=run.returncode==0,sha256=hashlib.sha256(rom.read_bytes()).hexdigest())
    match=re.search(r'History frames=(\d+) peak_ticks=(\d+)',run.stdout)
    if match:report.update(frames=int(match[1]),peak_ticks=int(match[2]))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for path in out.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
    raise SystemExit(run.returncode)

if __name__=='__main__':main()
