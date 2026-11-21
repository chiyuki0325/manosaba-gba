"""Capture a real unnamed trial line and check its 12px narrator glyphs are absent."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from PIL import Image
from host_test_cache import build

GBA=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path);p.add_argument('fixtures',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--baseline',type=Path,help='optional old ROM expected to display the unwanted label')
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    fixture=json.loads(args.fixtures.read_text())
    route=next(r for r in fixture['routes'] if r['scope']=='segment' and r['path'].endswith('Act02_Chapter01_Trial06'))
    target=next(s for s in route['steps'] if '艾玛也带来了毛巾' in s['text'])
    plan=out/'plan.txt';terminal=dict(route['terminal'],action=0,arg=0,kind=0)
    plan.write_text(f'{route["offset"]} {len(route["steps"])}\n'+''.join(' '.join(str(s[k]) for k in ('script','pc','state','action','arg','kind'))+'\n' for s in route['steps']+[terminal]))
    executable=build('trial_walk_test',[GBA/'tools/trial_walk_test.c'])
    font=(GBA/'src/font_data.c').read_text()
    numbers=lambda value:[int(s,0) for s in re.findall(r'0x[0-9a-fA-F]+|\d+',value)]
    codes=numbers(font.split('font_codes[] = {')[1].split('};')[0])
    rows=[numbers(row) for row in re.findall(r'\{([^}]+)\}',font.split('font_rows[][12] = {')[1].split('};')[0])]
    mask=[(26+i*12+x,102+y) for i,c in enumerate('旁白') for y,bits in enumerate(rows[codes.index(ord(c))]) for x in range(12) if bits&(1<<(11-x))]
    results=[]
    for name,source in [('current',args.rom)]+([('baseline',args.baseline)] if args.baseline else []):
        rom=source.resolve();directory=out/name;directory.mkdir(exist_ok=True)
        symbols=subprocess.check_output([os.environ.get('ARM_NM','/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'),str(rom.with_suffix('.elf'))],text=True)
        address=lambda label:next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+label))
        native=subprocess.run([str(executable),str(rom),str(directory),address('trial_runtime'),address('witch_book_runtime'),str(plan),'200000'],env=dict(os.environ,MANOSABA_TRIAL_CAPTURE_PC=str(target['pc'])),capture_output=True,text=True)
        (directory/'run.log').write_text(native.stdout+native.stderr)
        assert native.returncode==0 and '"captured"' in native.stdout,native.stdout+native.stderr
        image=Image.open(directory/'capture.ppm').convert('RGB');image.save(directory/'capture.png')
        count=sum(min(image.getpixel(point))>=240 for point in mask)
        result=dict(name=name,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),matched=count,total=len(mask),path=route['path'],pc=target['pc'])
        results.append(result)
        assert count<len(mask)//4 if name=='current' else count>len(mask)*.95,result
    (out/'result.json').write_text(json.dumps(dict(passed=True,results=results),ensure_ascii=False,indent=2)+'\n')
    print('PASS: unnamed trial dialogue hides narrator label; '+', '.join(f'{r["name"]}={r["matched"]}/{r["total"]} glyph pixels' for r in results))

if __name__=='__main__':main()
