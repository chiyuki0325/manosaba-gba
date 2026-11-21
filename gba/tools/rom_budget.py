"""Check the built ROM as well as the native archive before using music reserves."""
import json
from pathlib import Path
import sys
import os,shutil,subprocess
from graphics_project import GBA,config


def check_build(path):
    rom=Path(path).stat().st_size
    catalog=json.loads((GBA/'assets/graphics/catalog.json').read_text())
    budget=config()['budgets']
    graphics=catalog['rom']['graphics_bytes']
    graphics+=sum(p.stat().st_size for p in (GBA/'assets/trial').glob('*.bin')) if (GBA/'assets/trial').exists() else 0
    if graphics>budget['graphics_bytes']:raise ValueError('Native graphics exceed their ROM budget')
    # Already linked music consumes part of the audio allowance, not a second
    # allowance on top of the ROM that contains it.
    nm=os.environ.get('ARM_NM') or shutil.which('arm-none-eabi-nm') or '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'
    symbols={row.split()[-1]:int(row.split()[0],16) for row in subprocess.check_output([nm,str(Path(path).with_suffix('.elf'))],text=True).splitlines() if len(row.split())>=3}
    audio=symbols.get('bn_audio_soundbank_bin_end',0)-symbols.get('bn_audio_soundbank_bin',0)
    remaining_audio=max(0,budget['audio_reserve_bytes']-audio)
    reserved=rom+remaining_audio+budget['other_reserve_bytes']
    if reserved>budget['rom_bytes']:raise ValueError(f'Built ROM and audio/other reserves exceed cartridge size: {reserved:,}/{budget["rom_bytes"]:,}')
    report=dict(rom_bytes=rom,physical_free_bytes=budget['rom_bytes']-rom,graphics_estimate_bytes=graphics,
                packed_audio_bytes=audio,audio_budget_bytes=budget['audio_reserve_bytes'],reserved_audio_bytes=remaining_audio,
                reserved_other_bytes=budget['other_reserve_bytes'],remaining_after_reserves=budget['rom_bytes']-reserved)
    print(json.dumps(report,indent=2))
    return report

if __name__=='__main__':check_build(sys.argv[1])
