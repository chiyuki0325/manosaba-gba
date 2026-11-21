"""Five-pose prototype: resample indexed art offline and retain its palette banks.

Original asset IDs stay intact for trial/cut-in consumers. Story image_scene
selects appended variants only in MANOSABA_ACTOR_BAKED builds.
"""
from build_paths import generated_path
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np

from graphics_project import GBA,write_changed
from graphics_retile_estimate import SHAPES

OUT=GBA/'build-actor-baked'


def pack(tiles,width,height):
    remaining=set(tiles);claimed=set();out=[]
    while remaining:
        x,y=min(remaining,key=lambda p:(p[1],p[0]));choices=[]
        for w,h in SHAPES:
            if x+w>width or y+h>height:continue;
            cells={(xx,yy) for yy in range(y,y+h) for xx in range(x,x+w)}
            covered=cells&remaining
            if not cells&claimed and len(covered)*4>=w*h*3:choices.append((len(covered),-w*h,w,h,cells))
        _,_,w,h,cells=max(choices,key=lambda c:c[:2]);out.append((x,y,w,h));remaining-=cells;claimed|=cells
    return out


def main():
    OUT.mkdir(exist_ok=True)
    linked=json.loads((GBA/'assets/graphics-cache/linked/graphics.json').read_text())
    keys=sorted(linked['assets']);assets=linked['assets']
    source=(GBA/'assets/graphics/graphics.bin').read_bytes();payload=bytearray(source)
    offsets={b['id']:b['offset'] for b in linked['blobs']}
    contexts=json.loads((generated_path('reports/graphics-budget/context-analysis/contexts.json')).read_text())
    # These are asset selections for the prototype, not chapter-specific runtime rules.
    scenes=[('Act01_Chapter01/Act01_Chapter01_Adv22',86),('Act01_Chapter05/Act01_Chapter05_Adv06',35)]
    chosen={}
    for path,line in scenes:
        s=next(s for s in contexts['spans'] if s['path']==path and s['peak_line']==line)
        for a in s['actors']:chosen[a['image']]=a['scale_q8']
    definitions=[];rows=[];variants=[]
    for base,scale in sorted(chosen.items()):
        a=assets[keys[base]];w=a['width'];h=a['height'];px,py=a['pivot']
        index=np.zeros((h,w),np.uint8);bank=np.zeros((h,w),np.uint8)
        for p in a['parts']:
            pixels=np.zeros((p['height'],p['width']),np.uint8)
            for t,ref in enumerate(p['tiles']):
                off=offsets[ref];raw=np.frombuffer(source[off:off+32],np.uint8).reshape(8,4)
                tile=np.empty((8,8),np.uint8);tile[:,::2]=raw&15;tile[:,1::2]=raw>>4
                tx=t%(p['width']//8)*8;ty=t//(p['width']//8)*8;pixels[ty:ty+8,tx:tx+8]=tile
            ph=min(p['height'],h-p['y']);pw=min(p['width'],w-p['x']);ink=pixels[:ph,:pw]!=0
            region=index[p['y']:p['y']+ph,p['x']:p['x']+pw];region[ink]=pixels[:ph,:pw][ink]
            region=bank[p['y']:p['y']+ph,p['x']:p['x']+pw];region[ink]=p['bank']
        nw=round(w*scale/256);nh=round(h*scale/256);npx=round(px*scale/256);npy=round(py*scale/256)
        # Anchor-relative nearest sampling keeps the source character origin.
        sx=np.floor((np.arange(nw)-npx)*256/scale+px).astype(int).clip(0,w-1)
        sy=np.floor((np.arange(nh)-npy)*256/scale+py).astype(int).clip(0,h-1)
        new_index=index[sy[:,None],sx[None,:]];new_bank=bank[sy[:,None],sx[None,:]]
        padded_w=(nw+7)//8*8;padded_h=(nh+7)//8*8;parts=[];tile_bytes=0
        variant_key=f'prototype_baked_{base}_{scale}'
        for b in range(4):
            plane=np.zeros((padded_h,padded_w),np.uint8)
            plane[:nh,:nw]=np.where(new_bank==b,new_index,0)
            occupied=plane.reshape(padded_h//8,8,padded_w//8,8).any(axis=(1,3))
            required=[(int(x),int(y)) for y,x in np.argwhere(occupied)]
            for x,y,ww,hh in pack(required,padded_w//8,padded_h//8):
                refs=[]
                for ty in range(hh):
                    for tx in range(ww):
                        tile=plane[(y+ty)*8:(y+ty+1)*8,(x+tx)*8:(x+tx+1)*8]
                        raw=(tile[:,::2]|tile[:,1::2]<<4).tobytes();key=hashlib.sha256(raw).hexdigest()
                        if key not in offsets:offsets[key]=len(payload);payload.extend(raw)
                        refs.append(key)
                part=dict(x=x*8,y=y*8,width=ww*8,height=hh*8,bank=b,tiles=refs)
                parts.append(part);tile_bytes+=ww*hh*32
        assert len(parts)<=64,(base,len(parts))
        image=len(keys)+len(variants);prefix=f'baked_{image}'
        for i,p in enumerate(parts):
            definitions.append(f'const uint32_t {prefix}_p{i}_tiles[]={{'+','.join(str(offsets[t]) for t in p['tiles'])+'};')
        definitions.append(f'const graphic_part {prefix}_parts[]={{'+','.join(
            '{'+f'{prefix}_p{i}_tiles,{p["x"]},{p["y"]},{p["width"]},{p["height"]},{p["bank"]}'+'}' for i,p in enumerate(parts))+'};')
        palette=f'reinterpret_cast<const uint16_t*>(graphics_bytes+{offsets[a["palette"]]})'
        rows.append('{'+f'{prefix}_parts,nullptr,{palette},nullptr,{nw},{nh},{len(parts)},0,0,{npx},{npy},graphic_kind::obj,0,0,{scale}'+'},')
        new_asset=dict(a,id=variant_key,width=nw,height=nh,pivot=[npx,npy],parts=parts,tile_bytes=tile_bytes,nominal_scale=scale)
        variants.append(dict(base=base,image=image,scale=scale,key=variant_key,asset=new_asset))
    base_cpp=(GBA/'src/graphics_data.cpp').read_text()
    if base_cpp.startswith('#ifdef MANOSABA_ACTOR_BAKED\n'):
        base_cpp=base_cpp.split('#else\n',1)[1].rsplit('\n#endif',1)[0]
    marker='const graphic_asset graphic_assets[]='
    at=base_cpp.index(marker);end=base_cpp.index('};',at)
    result=base_cpp[:at]+'\n'.join(definitions)+'\n'+base_cpp[at:end]+'\n'+'\n'.join(rows)+'\n'+base_cpp[end:]
    result=re.sub(r'graphic_asset_count=\d+',f'graphic_asset_count={len(keys)+len(variants)}',result)
    write_changed(OUT/'graphics_data.inc',result)
    write_changed(OUT/'graphics.bin',bytes(payload))
    mappings=','.join('{'+f'{v["base"]},{v["image"]},{v["scale"]}'+'}' for v in variants)
    header='''#pragma once
namespace manosaba::actor_bake {
struct entry { int base,image,scale; };
inline constexpr entry entries[]={'''+mappings+'''};
inline int image(int base){for(const auto& e:entries)if(e.base==base)return e.image;return base;}
inline int nominal(int image){for(const auto& e:entries)if(e.image==image)return e.scale;return 256;}
}
'''
    write_changed(OUT/'actor_variant_map.h',header)
    manifest=dict(base_asset_count=len(keys),base_payload_sha256=hashlib.sha256(source).hexdigest(),
                  appended_payload_bytes=len(payload)-len(source),variants=variants)
    write_changed(OUT/'manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(dict(variants=len(variants),appended_payload_bytes=manifest['appended_payload_bytes'],
        poses=[dict(base=v['base'],image=v['image'],scale=v['scale'],parts=len(v['asset']['parts'])) for v in variants]),indent=2))


if __name__=='__main__':main()
