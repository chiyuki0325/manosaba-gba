"""Resolve the profiling symbol from this ROM's ELF; never use a stale RAM address."""
import argparse
from pathlib import Path
import subprocess
import os
import shutil
import hashlib


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('test',type=Path)
    parser.add_argument('rom',type=Path)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    nm=os.environ.get('ARM_NM') or shutil.which('arm-none-eabi-nm')
    if not nm:
        if os.environ.get('DEVKITARM'):nm=str(Path(os.environ['DEVKITARM'])/'bin/arm-none-eabi-nm')
        else:nm=str(Path(os.environ.get('WONDERFUL_TOOLCHAIN','/opt/wonderful'))/'toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm')
    symbols=subprocess.check_output([nm,str(args.rom.with_suffix('.elf'))],text=True)
    address=next(row.split()[0] for row in symbols.splitlines() if row.endswith(' _ZN8manosaba20graphics_performanceE'))
    args.directory.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,MANOSABA_ROM_SHA256=hashlib.sha256(args.rom.read_bytes()).hexdigest())
    subprocess.run([str(args.test.resolve()),str(args.rom),str(args.directory),address],check=True,env=env)

if __name__=='__main__':main()
