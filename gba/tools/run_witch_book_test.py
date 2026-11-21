"""Run native book integration and export actual GBA screenshots."""
from pathlib import Path
import json
import hashlib
import re
import subprocess
import sys
from PIL import Image
from witch_book_map_art import floor_image,floor_data
from witch_book_maps import indices


def main():
    root=Path(__file__).resolve().parents[1]
    rom=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    addresses=[next(row.split()[0] for row in symbols.splitlines() if row.endswith(suffix))
               for suffix in (' witch_book_runtime',' _ZN8manosaba20graphics_performanceE',' trial_runtime')]
    reference=out/'map-reference.bin'
    reference.write_bytes(b''.join(indices(floor_image(floor_data(name,1))[0]).tobytes() for name in ('F1','F2','B1','Blueprint')))
    test=out/'witch_book_game_test'
    subprocess.run(['cc','-O2','-Wall','-Wextra',str(root/'tools/witch_book_game_test.c'),'-lmgba','-o',str(test)],check=True)
    run=subprocess.run([str(test),str(rom),str(out),*addresses,str(reference)],capture_output=True,text=True)
    print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
    (out/'result.log').write_text(run.stdout+run.stderr)
    match=re.search(r'frames=(\d+) peak_ticks=(\d+) missed=(\d+)',run.stdout)
    report=dict(passed=run.returncode==0,rom=str(rom),sha256=hashlib.sha256(rom.read_bytes()).hexdigest())
    scroll=re.search(r'Hardware map scroll: frames=(\d+) positions=(\d+) peak_ticks=(\d+)',run.stdout)
    if scroll:report['hardware_scroll']=dict(zip(('frames','positions','peak_ticks'),map(int,scroll.groups())))
    reader=re.search(r'Hardware text scroll: frames=(\d+) positions=(\d+) tile_rows=(\d+) peak_ticks=(\d+)',run.stdout)
    if reader:report['hardware_text_scroll']=dict(zip(('frames','positions','tile_rows','peak_ticks'),map(int,reader.groups())))
    vblank=re.search(r'VBlank peak_ticks=(\d+)',run.stdout)
    if vblank:report['peak_vblank_ticks']=int(vblank[1])
    if match:report.update(dict(zip(('frames','peak_ticks','missed'),map(int,match.groups()))))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for path in out.glob('*.ppm'):
        Image.open(path).save(path.with_suffix('.png'))
    scroll_frames=[Image.open(path).convert('RGB') for path in sorted(out.glob('map-scroll-*.ppm'))]
    if scroll_frames:scroll_frames[0].save(out/'map-scroll.gif',save_all=True,append_images=scroll_frames[1:],duration=[(20,20,10)[i%3] for i in range(len(scroll_frames))],loop=0)
    text_frames=[Image.open(out/'book-rules-top.ppm').convert('RGB')] if (out/'book-rules-top.ppm').exists() else []
    text_frames += [Image.open(path).convert('RGB') for path in sorted(out.glob('text-scroll-*.ppm'))]
    if len(text_frames)>1:
        text_frames[0].save(out/'text-scroll.gif',save_all=True,append_images=text_frames[1:]+text_frames[-2:0:-1],duration=[350]+[20]*(len(text_frames)-2)+[350]+[20]*(len(text_frames)-2),loop=0)
    raise SystemExit(run.returncode)


if __name__=='__main__':main()
