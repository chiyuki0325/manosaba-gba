"""Shared graphics project configuration, source selection and content cache."""
from __future__ import annotations
import hashlib
from collections import Counter
import json
import os
from pathlib import Path

GBA = Path(__file__).resolve().parents[1]
ROOT = GBA.parent
SOURCE = ROOT/'story/zh-Hans'
CONFIG_PATH = GBA/'assets/graphical-story.json'


def canonical(value):
    return json.dumps(value,sort_keys=True,ensure_ascii=False,separators=(',',':')).encode()


def digest(value):
    return hashlib.sha256(value if isinstance(value,bytes) else canonical(value)).hexdigest()


def write_changed(path, data):
    path=Path(path); data=data.encode() if isinstance(data,str) else data
    if path.exists() and path.read_bytes()==data: return False
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_name(path.name+f'.tmp.{os.getpid()}')
    temporary.write_bytes(data); os.replace(temporary,path)
    return True


def config():
    value=json.loads(CONFIG_PATH.read_text())
    value.setdefault('demo',value.get('script','Act01_Chapter01/Act01_Chapter01_Adv04'))
    value.setdefault('import','all')
    value.setdefault('rom_scripts',[value['demo']])
    value.setdefault('budgets',dict(rom_bytes=32<<20,graphics_bytes=16<<20,audio_reserve_bytes=8<<20,other_reserve_bytes=2<<20))
    value.setdefault('effects',dict(blur_levels=2,mask_steps=32))
    return value


def source_paths(selection=None):
    selection=config()['import'] if selection is None else selection
    paths=sorted(SOURCE.rglob('*.json'))
    if selection=='all': return paths
    selected=set(selection)
    found=[p for p in paths if str(p.relative_to(SOURCE).with_suffix('')) in selected]
    missing=selected-{str(p.relative_to(SOURCE).with_suffix('')) for p in found}
    if missing: raise ValueError(f'Unknown graphical scripts: {sorted(missing)}')
    return found


class ContentCache:
    """Immutable objects plus small mutable dependency indexes; never ROM input by accident."""
    def __init__(self, root=None):
        self.root=Path(root or GBA/'assets/graphics-cache')
        self.hits=0; self.misses=0; self.stats=Counter()
        self._files={}

    def path(self, key, suffix):
        return self.root/'objects'/key[:2]/(key+suffix)

    def get_json(self, key):
        path=self.path(key,'.json')
        if path.exists():
            self.hits+=1
            return json.loads(path.read_text())
        self.misses+=1
        return None

    def put_json(self, key, value):
        write_changed(self.path(key,'.json'),json.dumps(value,ensure_ascii=False,sort_keys=True)+'\n')

    def put_blob(self, data, suffix='.bin'):
        key=digest(data); write_changed(self.path(key,suffix),data)
        return key

    def file_key(self, path):
        path=Path(path)
        if path not in self._files:
            self._files[path]=digest(path.read_bytes())
        return self._files[path]


def check_budget(graphics_bytes, config_value=None, other_bytes=0):
    budget=(config_value or config())['budgets']
    total=graphics_bytes+other_bytes+budget['audio_reserve_bytes']+budget['other_reserve_bytes']
    if graphics_bytes>budget['graphics_bytes'] or total>budget['rom_bytes']:
        raise ValueError(f'Graphics ROM budget exceeded: graphics={graphics_bytes:,}, limit={budget["graphics_bytes"]:,}; '
                         f'with audio/other reservation={total:,}/{budget["rom_bytes"]:,}. No data was silently omitted.')
    return dict(graphics_bytes=graphics_bytes,reserved_audio_bytes=budget['audio_reserve_bytes'],
                reserved_other_bytes=budget['other_reserve_bytes'],projected_total_bytes=total,rom_limit_bytes=budget['rom_bytes'])
