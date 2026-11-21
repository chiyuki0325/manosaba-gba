"""Check the ending screen against the original Unifont, using real game input."""
import gzip,json,re,subprocess,sys,hashlib
from pathlib import Path
from PIL import Image

def main():
    root=Path(__file__).resolve().parents[1];rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' trial_runtime'))
    book_address=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' witch_book_runtime'))
    scripts=[json.loads(p.read_text()) for p in sorted((root.parent/'story/zh-Hans').glob('Act*/*.json')) if re.search(r'_(Adv|Bad)\d+\.json$',p.name)]
    index=next(i for i,p in enumerate(scripts) if p['path'].endswith('Act01_Chapter03_Bad01'))
    data=(root/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(row[1:].split(',',1)[0]) for row in data.splitlines() if row.startswith('{')]
    common=paths.index('Common/CommonBad01')
    glyphs={}
    with gzip.open(root/'assets/unifont-17.0.03.hex.gz','rt') as source:
        for line in source:
            code,data=line.strip().split(':')
            if chr(int(code,16)) in 'BAD END':glyphs[chr(int(code,16))]=bytes.fromhex(data)
    mask=bytearray(240*160)
    for ch,char in enumerate('BAD END'):
        for y,row in enumerate(glyphs[char]):
            for x in range(8):mask[(128+y)*240+159+ch*8+x]=int(bool(row&(128>>x)))
    for y in range(9):mask[(132+y)*240+219+min(y,8-y)]=1
    reference=out/'expected.bin';reference.write_bytes(mask)
    test=out/'bad_end_game_test';subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/bad_end_game_test.c'),'-lmgba','-o',str(test)],check=True)
    run=subprocess.run([str(test),str(rom),str(out),address,str(index),str(reference),str(common),book_address],capture_output=True,text=True)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr);(out/'result.log').write_text(run.stdout+run.stderr)
    (out/'result.json').write_text(json.dumps(dict(passed=run.returncode==0,sha256=hashlib.sha256(rom.read_bytes()).hexdigest()),indent=2)+'\n')
    for image in out.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
    raise SystemExit(run.returncode)

if __name__=='__main__':main()
