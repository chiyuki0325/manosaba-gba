"""Production SFX host semantics and native PCM/UI tests."""
from pathlib import Path
import argparse,json,subprocess,hashlib,re,struct
from host_test_cache import GBA,build
from PIL import Image

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    test=build('sfx_player_test',[GBA/name for name in ('tools/sfx_player_test.cpp','src/sfx_player.cpp','src/sfx_data.cpp','src/story_vm.cpp','src/lzss.cpp','src/story_data.cpp')],compiler='g++',flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src')),libraries=())
    run=subprocess.run([str(test)],text=True,capture_output=True);print(run.stdout,end='');print(run.stderr,end='');assert run.returncode==0
    rom=a.rom.resolve();symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    def addr(name):return next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+name))
    address={row.split()[-1]:int(row.split()[0],16) for row in symbols.splitlines() if len(row.split())>=3}
    bank=address['bn_audio_soundbank_bin'];data=rom.read_bytes();at=bank-0x08000000
    samples,songs=struct.unpack_from('<HH',data,at)
    modules=re.findall(r'MUSIC_MODULE\((\w+)\)',(GBA/'src/music_modules.inc').read_text())
    def song(asset):return format(bank+struct.unpack_from('<I',data,at+12+4*(samples+modules.index(asset)))[0]+8,'x')
    source=(GBA/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(row[1:].split(',',1)[0]) for row in source.splitlines() if row.startswith('{')]
    test=build('sfx_game_test' ,[GBA/'tools/sfx_game_test.c'])
    cues=json.loads((GBA/'assets/sfx/manifest.json').read_text())['cues'];ids={c['alias']:i for i,c in enumerate(cues)}
    run=subprocess.run([str(test),str(rom),str(out),addr('mm_sfx_bitmask'),addr('mmLayerMain'),addr('_ZN8manosaba20graphics_performanceE'),*[str(ids[c]) for c in ('Ambi_4_1','Ambi_8_1','Sfx_71','Sfx_57')],str(paths.index('Act01_Chapter02/Act01_Chapter02_Adv14')),song('ring_chakumero'),song('bgm005')],text=True,capture_output=True)
    print(run.stdout,end='');print(run.stderr,end='');(out/'native.log').write_text(run.stdout+run.stderr)
    m=re.search(r'peak_ticks=(\d+) voice_peak=(\d+) nonzero=(\d+)',run.stdout)
    report=dict(passed=run.returncode==0,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),peak_ticks=int(m[1]) if m else None,voice_peak=int(m[2]) if m else None)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    for image in out.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
    if run.returncode:raise SystemExit(run.returncode)
if __name__=='__main__':main()
