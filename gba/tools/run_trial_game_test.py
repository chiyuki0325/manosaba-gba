"""Bind a production-player integration test to its matching ELF and ROM."""
from pathlib import Path
import argparse,subprocess,os,json,hashlib,re,sys
from host_test_cache import build

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('directory',type=Path);p.add_argument('--quick',action='store_true');args=p.parse_args()
    root=Path(__file__).resolve().parents[1];args.directory.mkdir(parents=True,exist_ok=True)
    nm=os.environ.get('ARM_NM','/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm')
    output=subprocess.check_output([nm,str(args.rom.with_suffix('.elf'))],text=True)
    address=next(row.split()[0] for row in output.splitlines() if row.endswith(' trial_runtime'))
    book_address=next(row.split()[0] for row in output.splitlines() if row.endswith(' witch_book_runtime'))
    sfx_address=next(row.split()[0] for row in output.splitlines() if row.endswith(' mm_sfx_bitmask'))
    sfx_counter=next(row.split()[0] for row in output.splitlines() if row.endswith(' mm_sfx_counter'))
    test=build('trial_game_test',[root/'tools/trial_game_test.c'])
    env=dict(os.environ)
    if args.quick:env['MANOSABA_TRIAL_QUICK']='1'
    else:env.pop('MANOSABA_TRIAL_QUICK',None)
    run=subprocess.run([str(test),str(args.rom),str(args.directory),address,str(root/'assets/trial/camera/Court/expected.bin'),str(json.loads((root/'assets/trial/camera.json').read_text())['motions'][0]['end']),str(root/'assets/trial/reduced/podium-forbidden.bin'),book_address,sfx_address,sfx_counter],capture_output=True,text=True,env=env)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
    (args.directory/'result.log').write_text(run.stdout+run.stderr)
    report=dict(rom=str(args.rom),sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),passed=run.returncode==0,quick=args.quick,
        ticks_per_frame=4389,phases=[dict(zip(('phase','frames','max_ticks','missed'),map(int,m))) for m in re.findall(r'phase (\d+): frames=(\d+) max_ticks=(\d+) missed=(\d+)',run.stdout)])
    summary=re.search(r'frames=(\d+) missed=(\d+) peak_ticks=(\d+)',run.stdout)
    if summary:report.update(dict(zip(('frames','missed','peak_ticks'),map(int,summary.groups()))))
    (args.directory/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    if run.returncode:raise SystemExit(run.returncode)

if __name__=='__main__':main()
