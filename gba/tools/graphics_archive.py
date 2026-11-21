"""Deduplicated native tile archive and chapter programs, with explicit ROM budgets."""
from __future__ import annotations
import collections
import json
from pathlib import Path
from graphics_project import ContentCache, canonical, check_budget, config, digest, write_changed


def build_archive(cache, encoded_assets, programs, output=None, project=None, extra_resources=()):
    """Only referenced assets enter ROM; development PNGs/intermediate states stay on disk."""
    project=project or config()
    selected=project.get('rom_scripts','all')
    paths=sorted(programs) if selected=='all' else list(selected)
    missing=set(paths)-set(programs)
    if missing: raise ValueError(f'Chapters have not been imported: {sorted(missing)}')
    resources=set(extra_resources); bank_sizes={}; program_bytes=0
    for path in paths:
        program=cache.get_json(programs[path])
        resources.update(program['resources']);resources.update(program.get('mask_resources',{}).values())
        size=len(program['commands'])*12+len(program['keys'])*8+len(program['events'])*6
        bank_sizes[path]=size; program_bytes+=size
    pending=list(resources)
    while pending:
        key=pending.pop()
        if key not in encoded_assets: raise ValueError(f'Missing encoded shared asset: {key}')
        for child in (encoded_assets[key].get('frames',[]) if encoded_assets[key]['kind']=='animation' else encoded_assets[key].get('obj_frames',[])):
            if child not in resources: resources.add(child);pending.append(child)
    blobs={}; assets={}
    for key in sorted(resources):
        if key not in encoded_assets: raise ValueError(f'Missing encoded shared asset: {key}')
        asset=encoded_assets[key];assets[key]=asset
        for blob in asset.get('blobs',[]):
            data=cache.path(blob,'.bin').read_bytes();blobs[blob]=data
    stored=sum((len(data)+3)&~3 for data in blobs.values())
    directories=sum(asset['rom_directory_bytes'] for asset in assets.values())+4*len(blobs)
    sizes=check_budget(stored+program_bytes+directories,project)
    report=dict(sizes,chapters=len(paths),assets=len(assets),shared_blobs=len(blobs),
                payload_bytes=stored,program_bytes=program_bytes,directory_estimate_bytes=directories,
                chapter_program_bytes=bank_sizes)
    output=Path(output or cache.root/'linked')
    index=[];data=bytearray()
    for key,blob in sorted(blobs.items()):
        index.append(dict(id=key,offset=len(data),size=len(blob)))
        data.extend(blob);data.extend(bytes(-len(data)%4))
    write_changed(output/'graphics.bin',data)
    write_changed(output/'graphics.json',canonical(dict(report=report,assets=assets,blobs=index,programs={p:programs[p] for p in paths})))
    return report
