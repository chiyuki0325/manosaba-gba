#!/usr/bin/env python3
"""Run mmutil, then remove only exact digital silence from non-looped PCM tails."""
import json
from pathlib import Path
import struct
import subprocess
import sys

GBA=Path(__file__).resolve().parents[1]


def blocks(data):
    samples,songs=struct.unpack_from('<HH',data)
    assert data[4:12]==b'*maxmod*'
    offsets=struct.unpack_from(f'<{samples+songs}I',data,12)
    return samples,[data[o:o+8+struct.unpack_from('<I',data,o)[0]] for o in offsets]


def compact(data):
    samples,chunks=blocks(data);header=12+4*len(chunks);result=bytearray(data[:header]);offsets=[];trimmed=[]
    for i,chunk in enumerate(chunks):
        if i<samples:
            length,loop=struct.unpack_from('<II',chunk,8)
            assert len(chunk)==length+24 and chunk[16]==0
            if loop==0xffffffff:
                pcm=chunk[20:20+length]
                zeros=len(pcm)-len(pcm.rstrip(b'\x80'))
                # Keep guard samples for the mixer's interpolation/word reads.
                trim=max(0,(zeros-16)//4*4)
                if trim:
                    chunk=bytearray(chunk[:20]+pcm[:-trim]+chunk[20+length:])
                    struct.pack_into('<I',chunk,0,len(chunk)-8)
                    struct.pack_into('<I',chunk,8,length-trim)
                    trimmed.append(dict(sample=i,removed_bytes=trim))
        result.extend(b'\0'*(-len(result)%4));offsets.append(len(result));result.extend(chunk)
    struct.pack_into(f'<{len(offsets)}I',result,12,*offsets)
    return bytes(result),dict(before_bytes=len(data),after_bytes=len(result),saved_bytes=len(data)-len(result),trimmed=trimmed)


def verify(before,after):
    samples,old=blocks(before);count,new=blocks(after)
    assert samples==count and len(old)==len(new)
    assert before[:12]==after[:12]
    for i,(a,b) in enumerate(zip(old,new)):
        if i>=samples:assert a==b;continue
        old_len,loop=struct.unpack_from('<II',a,8);new_len,new_loop=struct.unpack_from('<II',b,8)
        assert loop==new_loop and a[4:8]==b[4:8] and a[12:20]==b[12:20]
        assert new_len<=old_len and a[20:20+new_len]==b[20:20+new_len]
        assert a[20+new_len:20+old_len]==b'\x80'*(old_len-new_len)
        assert a[20+old_len:]==b[20+new_len:]
        if loop!=0xffffffff:assert a==b


def main():
    if not (GBA/'build-tools/mmutil').is_file():
        raise SystemExit('Run make -C gba music-tools once to build the pinned mmutil converter.')
    subprocess.run([str(GBA/'build-tools/mmutil'),*sys.argv[1:]],check=True)
    output=next((Path(a[2:]) for a in sys.argv[1:] if a.startswith('-o')),None)
    if output:
        before=output.read_bytes();after,report=compact(before);verify(before,after)
        output.write_bytes(after)
        output.with_suffix('.compact.json').write_text(json.dumps(report,indent=2)+'\n')
        print(f'Exact-silence PCM compaction: {report["saved_bytes"]:,} bytes saved; all module blocks and nonzero PCM unchanged')


if __name__=='__main__':main()
