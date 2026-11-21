"""Exercise source camera movement through each formal chapter's production UI."""
from pathlib import Path
import argparse,subprocess,json,hashlib,os,sys
from trial_camera import catalog,SOURCE

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('directory',type=Path);p.add_argument('--short',action='store_true');args=p.parse_args()
    root=Path(__file__).resolve().parents[1];args.directory.mkdir(parents=True,exist_ok=True)
    c=catalog();paths=[p.with_suffix('').relative_to(SOURCE).as_posix() for p in sorted(SOURCE.glob('Act*/*Trial[0-9]*.json'))]
    selected=[]
    for layout in range(11):
        choices=[(i,m) for i,m in enumerate(c['motions']) if m['layout']==layout]
        selected.append(min(choices,key=lambda row:(row[1]['kind']!=('yaw' if args.short else 'orbit'),row[1]['line'])))
    specs=args.directory/'cases.txt';specs.write_text(''.join(f'{paths.index(m["path"])} {i} {m["frames"]} {m["end"]} {c["layouts"][m["layout"]]["stage"]}\n' for i,m in selected))
    nm=os.environ.get('ARM_NM','/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm')
    symbols=subprocess.check_output([nm,str(args.rom.with_suffix('.elf'))],text=True)
    address=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' trial_runtime'))
    exe=args.directory.resolve()/'trial_camera_game_test'
    subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/trial_camera_game_test.c'),'-lmgba','-o',str(exe)],check=True)
    run=subprocess.run([str(exe),str(args.rom),str(args.directory),address,str(specs),str(root/'assets/trial/camera/Court/expected.bin'),str(root/'assets/trial/camera/Court_Final/expected.bin'),str(root/'assets/trial/reduced/Court/expected.bin'),str(root/'assets/trial/reduced/Court_Final/expected.bin')],capture_output=True,text=True)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
    (args.directory/'result.log').write_text(run.stdout+run.stderr)
    report=dict(rom=str(args.rom),sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),passed=run.returncode==0,source_counts=c['counts'],playback_counts=json.loads((root/'assets/trial/camera.json').read_text())['counts'],cases=[m for i,m in selected],log=run.stdout)
    (args.directory/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if run.returncode:raise SystemExit(run.returncode)
if __name__=='__main__':main()
