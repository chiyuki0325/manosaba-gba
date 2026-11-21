"""Make a separate save-UI pressure ROM: bind the ADV04 cue to existing full HirO.

Only the six module/volume/order bytes change in both ROM and matching ELF.
The production ROM, scripts, mixer and music assets remain the reference inputs.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
from host_test_cache import GBA

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    original=args.rom.resolve();rom=bytearray(original.read_bytes());elf=bytearray(original.with_suffix('.elf').read_bytes())
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(original.with_suffix('.elf'))],text=True)
    address=int(next(row.split()[0] for row in symbols.splitlines() if row.endswith(' _ZN8manosaba10music_cuesE')),16)
    # The current ARM ABI has a 20-byte cue. Read keys until the two known exact cues are found.
    rows={}
    for i in range(100):
        at=address+i*20;key=struct.unpack_from('<I',rom,at-0x08000000)[0]
        if key in (4122,8229):rows[key]=at
        if len(rows)==2:break
    target=rows[4122]+12;source=rows[8229]+12;data=rom[source-0x08000000:source-0x08000000+6]
    assert struct.unpack_from('<H',data)[0]==960 and data[3:6]==bytes([0,8,0]),'HirO baseline changed; recheck cue metadata'
    rom[target-0x08000000:target-0x08000000+6]=data
    phoff=struct.unpack_from('<I',elf,28)[0];entsize,count=struct.unpack_from('<HH',elf,42)
    for i in range(count):
        kind,offset,vaddr,_,size,*_=struct.unpack_from('<8I',elf,phoff+i*entsize)
        if kind==1 and vaddr<=target and target+6<=vaddr+size:
            start=offset+target-vaddr;elf[start:start+6]=data;break
    else:raise ValueError('cue segment missing from ELF')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    (out/'runtime.gba').write_bytes(rom);(out/'runtime.elf').write_bytes(elf)
    (out/'override.json').write_text(json.dumps(dict(base_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
        override='Bgm026 module/volume/orders -> existing full HirO; key/alias/script unchanged',address=hex(target)),indent=2)+'\n')
    print(out/'runtime.gba')
if __name__=='__main__':main()
