"""Study non-overlapping OBJ packing using only existing indexed tile bytes.

Writes alternate host audit input; production art/ROM stay untouched.
"""
import collections
import json
from pathlib import Path
import struct
import subprocess
import sys

GBA=Path(__file__).resolve().parents[1]
OUT=GBA/'build-butano/actor-repack'
SHAPES=[(8,8),(8,4),(4,8),(4,4),(4,2),(2,4),(2,2),(4,1),(1,4),(2,1),(1,2),(1,1)]


def rectangles(required,threshold):
    remaining=set(required);claimed=set();result=[]
    while remaining:
        x,y=min(remaining,key=lambda p:(p[1],p[0]));choices=[]
        for w,h in SHAPES:
            cells={(xx,yy) for yy in range(y,y+h) for xx in range(x,x+w)}
            covered=cells&remaining
            if not cells&claimed and len(covered)*100>=w*h*threshold:
                choices.append((len(covered),-w*h,w,h,cells))
        _,_,w,h,cells=max(choices,key=lambda p:p[:2])
        result.append((x,y,w,h));remaining-=cells;claimed|=cells
    return result


def cost(parts,scale):
    diff=collections.Counter()
    for p in parts:
        w,h=p['width'],p['height'];twice=scale>256
        y=(p['y']*2+h)*scale//512-h*(2 if twice else 1)//2
        height=h*(2 if twice else 1)
        c=8+w*2*(2 if twice else 1) if scale!=256 else w-2
        diff[y]+=c;diff[y+height]-=c
    total=peak=0
    for y,d in sorted(diff.items()):total+=d;peak=max(peak,total)
    return peak+2*max(0,len(parts)-1)


def main():
    global OUT
    compact='--compact' in sys.argv
    if compact:OUT=GBA/'build-butano/actor-repack-compact'
    OUT.mkdir(parents=True,exist_ok=True)
    linked=json.loads((GBA/'assets/graphics-cache/linked/graphics.json').read_text())
    keys=sorted(linked['assets']);data=(GBA/'assets/graphics/graphics.bin').read_bytes()
    offsets={b['id']:b['offset'] for b in linked['blobs']}
    empty={key:not any(data[off:off+32]) for key,off in offsets.items()}
    zero=next(key for key in offsets if empty[key] and data[offsets[key]:offsets[key]+32]==bytes(32))
    variants={};summary=collections.Counter()
    for index,key in enumerate(keys):
        a=linked['assets'][key]
        if a['kind']!='obj':continue
        banks=[{} for _ in range(4)]
        for p in a['parts']:
            for t,ref in enumerate(p['tiles']):
                if empty[ref]:continue
                pos=p['x']//8+t%(p['width']//8),p['y']//8+t//(p['width']//8)
                old=banks[p['bank']].get(pos)
                assert old is None or old==ref
                summary['duplicate_nonzero_tiles']+=old is not None
                banks[p['bank']][pos]=ref
        original=a['parts'];old_bytes=sum(p['width']*p['height']//2 for p in original)
        old_costs=[cost(original,s) for s in (256,282,294,384)]
        candidates=[(old_bytes+(0 if compact else len(original)*32),original,0)]
        for threshold in (100,75,50):
            parts=[]
            for bank,cells in enumerate(banks):
                for x,y,w,h in rectangles(cells,threshold):
                    refs=[cells.get((xx,yy),zero) for yy in range(y,y+h) for xx in range(x,x+w)]
                    parts.append(dict(x=x*8,y=y*8,width=w*8,height=h*8,bank=bank,tiles=refs))
            size=sum(p['width']*p['height']//2 for p in parts)
            # A source-wide criterion, with no chapter/character conditions.
            if len(parts)<=64 and size<=old_bytes and (compact or all(cost(parts,s)<=c for s,c in zip((256,282,294,384),old_costs))):
                candidates.append((size+(0 if compact else len(parts)*32),parts,threshold))
        _,parts,threshold=min(candidates,key=lambda p:p[0])
        if not threshold:continue
        variants[key]=parts
        summary['changed_assets']+=1;summary['old_parts']+=len(original);summary['new_parts']+=len(parts)
        summary['source_vram_saved_bytes']+=old_bytes-sum(p['width']*p['height']//2 for p in parts)
    (OUT/'parts.json').write_text(json.dumps(dict(summary=summary,assets=variants),indent=2)+'\n')
    source=(GBA/'build-butano/actor-prototype/scale-snap/input.bin').read_bytes();at=0
    def read():
        nonlocal at
        v=struct.unpack_from('<i',source,at)[0];at+=4;return v
    count=read();out=bytearray(struct.pack('<i',count))
    for i in range(count):
        fixed=[read() for _ in range(6)];frames=[read() for _ in range(read())];n=read()
        original=[[read() for _ in range(4)] for _ in range(n)]
        parts=variants.get(keys[i]);rows=original if parts is None else [[p['x'],p['y'],p['width'],p['height']] for p in parts]
        values=fixed+[len(frames)]+frames+[len(rows)]+[v for row in rows for v in row]
        out.extend(struct.pack('<'+'i'*len(values),*values))
    out.extend(source[at:]);(OUT/'input.bin').write_bytes(out)
    exe=GBA/'build-butano/actor-prototype/scale-snap/graphics_budget_scan'
    run=subprocess.run([str(exe),str(OUT/'input.bin'),str(OUT/'samples.csv'),'1'],text=True,capture_output=True,check=True)
    (OUT/'scan.log').write_text(run.stderr)
    import csv
    rows=list(csv.DictReader((OUT/'samples.csv').open()));events={(r['bank'],r['event']) for r in rows}
    print(json.dumps(dict(summary=summary,flagged_events=len(events),flagged_banks=len({r['bank'] for r in rows}),vram_events=len({(r['bank'],r['event']) for r in rows if int(r['vram_bytes'])>32768})),indent=2))


if __name__=='__main__':main()
