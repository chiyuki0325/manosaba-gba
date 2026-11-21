"""Production ROM regression for bounded mirror, pose, CourtFinal and compound-motion fallback."""
from pathlib import Path
import argparse,subprocess,json,hashlib,re,sys
from graphics_project import GBA


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('rom',type=Path);parser.add_argument('directory',type=Path);args=parser.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True)
    data=json.loads((GBA/'assets/trial/camera.json').read_text())
    cases=data['reduced_compound'];spec=args.directory/'cases.txt'
    from trial_views import reduced_view
    spec.write_text(''.join(f'{row["motion"]} {row["frames"]} {row["layout"]} {9+data["layouts"][row["layout"]]["stage"]*9+reduced_view(row["end"],data["layouts"][row["layout"]]["count"])-4}\n' for row in cases))
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(args.rom.with_suffix('.elf'))],text=True)
    address=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' trial_runtime'))
    executable=args.directory.resolve()/'trial_presentation_test'
    subprocess.run(['cc','-O2','-Wall','-Wextra',str(GBA/'tools/trial_presentation_test.c'),'-lmgba','-o',str(executable)],check=True)
    table=(GBA/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(row[1:].split(',',1)[0]) for row in table.splitlines() if row.startswith('{')]
    fixture=paths.index('Demo/TrialPresentation')
    run=subprocess.run([str(executable),str(args.rom),str(args.directory),address,str(GBA/'assets/trial/reduced/Court/expected.bin'),str(GBA/'assets/trial/reduced/Court_Final/expected.bin'),str(GBA/'assets/trial/reduced/podium-forbidden.bin'),str(spec),str(fixture)],capture_output=True,text=True)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
    (args.directory/'result.log').write_text(run.stdout+run.stderr)
    report=dict(sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),passed=run.returncode==0,cases=cases)
    match=re.search(r'frames=(\d+) peak_ticks=(\d+) missed=(\d+)',run.stdout)
    if match:report.update(dict(zip(('frames','peak_ticks','missed'),map(int,match.groups()))))
    (args.directory/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    if run.returncode:raise SystemExit(run.returncode)


if __name__=='__main__':main()
