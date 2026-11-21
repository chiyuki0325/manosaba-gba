"""Offline feasibility estimate: crop/repack existing tiles at saved peak poses.

No image import, ROM assets or game code are changed. Retains each original
part's palette and tile pixels; only estimates different legal OBJ rectangles.
"""
import collections
import json
import struct

import numpy as np
from PIL import Image
from graphics_budget_contexts import GBA,WORK,OUT,ENTITY,actor_cost,cosine,sine,div

SHAPES=((8,8),(8,4),(4,8),(4,4),(4,2),(2,4),(2,2),(4,1),(1,4),(2,1),(1,2),(1,1))


def pack(tiles):
    remaining=set(tiles);out=[]
    while remaining:
        x,y=min(remaining,key=lambda p:(p[1],p[0]));choices=[]
        for w,h in SHAPES:
            if all((xx,yy) in remaining for yy in range(y,y+h) for xx in range(x,x+w)):
                choices.append((w*h,w,h))
        _,w,h=max(choices)
        remaining.difference_update((xx,yy) for yy in range(y,y+h) for xx in range(x,x+w))
        out.append((x,y,w,h))
    return out


def main():
    linked=json.loads((GBA/'assets/graphics-cache/linked/graphics.json').read_text())
    imported=json.loads((GBA/'assets/graphics-cache/import.json').read_text())['assets']
    contexts=json.loads((OUT/'contexts.json').read_text());keys=sorted(linked['assets']);paths=sorted(linked['programs'])
    raw=(WORK/'context-snapshots.bin').read_bytes();peaks={};alpha_cache={}
    for at in range(0,len(raw),1048):
        h=struct.unpack_from('<16i',raw,at)
        if h[4]:continue
        key=h[0],h[1]
        if key in peaks and peaks[key][0][7]>=h[7]:continue
        peaks[key]=(h,[ENTITY.unpack_from(raw,at+64+i*60) for i in range(16)],struct.unpack_from('<3Q',raw,at+1024))
    results=[];projected_results=[];merged_results=[]
    for span in contexts['spans']:
        h,states,masks=max((v for (bank,event),v in peaks.items() if paths[bank]==span['path'] and span['event_start']<=event<=span['event_end']),key=lambda v:v[0][7])
        cam=states[11];costs=[];actors=[];projected_costs=[];projected_actors=[];merged_costs=[];merged_actors=[]
        for slot,image in enumerate(h[10:13]):
            if image<0:continue
            key=keys[image];a=linked['assets'][key];e=states[h[13+slot]];original=actor_cost(e,cam,a,masks[slot])
            source=imported.get(key);new_parts=[]
            if not source or not source.get('png'):
                changed=original;new_parts=[p for i,p in enumerate(a['parts']) if masks[slot]>>i&1]
                projected_costs.append(original['cost']);projected_actors.append(dict(parts=len(new_parts),bytes=original['bytes'],objects=original['objects']))
                merged_costs.append(original['cost']);merged_actors.append(dict(parts=len(new_parts),bytes=original['bytes'],objects=original['objects']))
            else:
                if key not in alpha_cache:
                    png=source['png'];alpha_cache[key]=np.asarray(Image.open(GBA/'assets/graphics-cache/objects'/png[:2]/(png+'.png')).convert('RGBA'))[:,:,3]>=128
                alpha=alpha_cache[key]
                height,width=alpha.shape;px,py=a.get('pivot',[width//2,height//2])
                scale=div(e[2]*cam[2],256);angle=e[3]+cam[3];c=div(cosine(angle)*scale,4096);s=div(sine(angle)*scale,4096)
                dx=e[0]-120*256-cam[0];dy=e[1]-80*256-cam[1]
                cx=120*256+div((dx*cosine(cam[3])-dy*sine(cam[3]))*cam[2],4096*256)
                cy=80*256+div((dx*sine(cam[3])+dy*cosine(cam[3]))*cam[2],4096*256)
                ys,xs=np.mgrid[0:height,0:width];local_x=xs-px;local_y=ys-py
                screen_x=cx+local_x*c-local_y*s;screen_y=cy+local_x*s+local_y*c
                visible=alpha & (screen_x>=-2*256) & (screen_x<=242*256) & (screen_y>=-2*256) & (screen_y<=162*256)
                clip=None
                if e[16]:
                    for m in states[13:]:
                        if m[12]>=0 and m[4]>0 and div(m[14]+1,20)==div(e[14],20):clip=m
                if clip and clip[4]>=256:
                    shape=linked['assets'][keys[clip[12]]];w,hh=shape.get('size',[shape.get('width',240),shape.get('height',160)])
                    mscale=max(1,clip[2]);dx=e[0]-clip[0];dy=e[1]-clip[1]
                    u=w*128+div(div(dx*cosine(clip[3])+dy*sine(clip[3]),16),mscale)
                    v=hh*128+div(div(-dx*sine(clip[3])+dy*cosine(clip[3]),16),mscale)
                    du=div(div(cosine(e[3]-clip[3])*e[2],16),mscale);dv=div(div(sine(e[3]-clip[3])*e[2],16),mscale)
                    uu=u+local_x*du-local_y*dv;vv=v+local_x*dv+local_y*du
                    visible&=(uu>=0)&(uu<w*256)&(vv>=0)&(vv<hh*256)
                for i,p in enumerate(a['parts']):
                    if not masks[slot]>>i&1:continue
                    required=[]
                    for y in range(p['height']//8):
                        for x in range(p['width']//8):
                            sx=p['x']+x*8;sy=p['y']+y*8
                            if visible[sy:sy+8,sx:sx+8].any():required.append((x,y))
                    for x,y,w,hh in pack(required):new_parts.append(dict(x=p['x']+x*8,y=p['y']+y*8,width=w*8,height=hh*8,bank=p['bank']))
                changed=actor_cost(e,cam,dict(a,parts=new_parts),(1<<len(new_parts))-1)
                # A feasibility bound: bake the final transform into pixels,
                # preserving each old part's palette, then use regular OBJs.
                oy,ox=np.mgrid[:160,:240];length=c*c+s*s
                if length:
                    sx=np.floor(((ox*256-cx)*c+(oy*256-cy)*s)/length+px).astype(np.int32)
                    sy=np.floor((-(ox*256-cx)*s+(oy*256-cy)*c)/length+py).astype(np.int32)
                    valid=(sx>=0)&(sx<width)&(sy>=0)&(sy<height)
                    sx=sx.clip(0,width-1);sy=sy.clip(0,height-1);ink=visible[sy,sx]&valid
                else:sx=ox;sy=oy;ink=np.zeros((160,240),dtype=bool)
                projected_parts=[];projected_cost=[0]*160;projected_bytes=0;palette_owner=np.full((160,240),-1,dtype=np.int8)
                for i,p in enumerate(a['parts']):
                    if not masks[slot]>>i&1:continue
                    pixels=ink&(sx>=p['x'])&(sx<p['x']+p['width'])&(sy>=p['y'])&(sy<p['y']+p['height'])
                    palette_owner[pixels]=p['bank']
                    tiles=pixels.reshape(20,8,30,8).any(axis=(1,3))
                    required=[(int(x),int(y)) for y,x in np.argwhere(tiles)]
                    for x,y,w,hh in pack(required):
                        projected_parts.append((x,y,w,hh));projected_bytes+=w*hh*32
                        for row in range(y*8,(y+hh)*8):projected_cost[row]+=w*8-2
                projected_costs.append(projected_cost);projected_actors.append(dict(parts=len(projected_parts),bytes=projected_bytes,objects=len(projected_parts)))
                merged_cost=[0]*160;merged_count=merged_bytes=0
                for bank in range(4):
                    tiles=(palette_owner==bank).reshape(20,8,30,8).any(axis=(1,3))
                    required=[(int(x),int(y)) for y,x in np.argwhere(tiles)]
                    for x,y,w,hh in pack(required):
                        merged_count+=1;merged_bytes+=w*hh*32
                        for row in range(y*8,(y+hh)*8):merged_cost[row]+=w*8-2
                merged_costs.append(merged_cost);merged_actors.append(dict(parts=merged_count,bytes=merged_bytes,objects=merged_count))
            costs.append(changed['cost']);actors.append(dict(image=image,role=source.get('who','CG') if source else 'CG',
                before_parts=masks[slot].bit_count(),after_parts=len(new_parts),bytes_before=original['bytes'],bytes_after=changed['bytes'],objects=changed['objects']))
        overhead=2*max(0,sum(a['objects'] for a in actors)-1)
        cycles=max(overhead+sum(c[y] for c in costs) for y in range(160))
        total_bytes=sum(a['bytes_after'] for a in actors);valid=max((a['after_parts'] for a in actors),default=0)<=64 and sum(a['objects'] for a in actors)<=128
        results.append(dict(path=span['path'],source_lines=[span['line_start'],span['line_end']],category=span['category'],before_cycles=h[7],
            after_cycles=cycles,after_vram_bytes=total_bytes,within_object_limits=valid,
            snapshot_within_budget=valid and cycles<=1210 and total_bytes<=32768,actors=actors))
        count=sum(a['objects'] for a in projected_actors);overhead=2*max(0,count-1)
        cycles=max(overhead+sum(c[y] for c in projected_costs) for y in range(160))
        size=sum(a['bytes'] for a in projected_actors);valid=count<=128 and max((a['parts'] for a in projected_actors),default=0)<=64
        projected_results.append(dict(path=span['path'],source_lines=[span['line_start'],span['line_end']],before_cycles=h[7],
            cycles=cycles,vram_bytes=size,objects=count,within_object_limits=valid,snapshot_within_budget=valid and cycles<=1210 and size<=32768))
        count=sum(a['objects'] for a in merged_actors);overhead=2*max(0,count-1)
        cycles=max(overhead+sum(c[y] for c in merged_costs) for y in range(160));size=sum(a['bytes'] for a in merged_actors)
        valid=count<=128 and max((a['parts'] for a in merged_actors),default=0)<=64
        merged_results.append(dict(path=span['path'],source_lines=[span['line_start'],span['line_end']],before_cycles=h[7],
            cycles=cycles,vram_bytes=size,objects=count,within_object_limits=valid,snapshot_within_budget=valid and cycles<=1210 and size<=32768))
    summary=dict(snapshots=len(results),repacked_snapshot_within_budget=sum(r['snapshot_within_budget'] for r in results),
                 average_peak_cycle_ratio=round(sum(r['after_cycles']/r['before_cycles'] for r in results)/len(results),3),
                 snapshots_exceeding_object_count=sum(not r['within_object_limits'] for r in results))
    out=dict(summary=summary,method='Repack each original palette part independently into legal GBA OBJ sizes, using 8px tile occupancy after screen/mask clipping; preserve original pixel/palette data.',
             limitations=['Peak-snapshot feasibility only, not a full-animation pass.',
                          'A 2px screen-edge guard is used; exact hardware pixel equivalence is not established.',
                          'Runtime allocation churn, upload cost and scanline order effects require a separate implementation and test.',
                          'Original overlapping palette parts are not merged; some duplicate coverage remains.'],results=results)
    (OUT/'retile-estimate.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
    projected_summary=dict(snapshots=len(projected_results),within_budget=sum(r['snapshot_within_budget'] for r in projected_results),
                           max_cycles=max(r['cycles'] for r in projected_results),max_vram_bytes=max(r['vram_bytes'] for r in projected_results),
                           max_objects=max(r['objects'] for r in projected_results))
    (OUT/'projected-snapshot-bound.json').write_text(json.dumps(dict(summary=projected_summary,
        scope='Feasibility bound for pre-baked regular OBJ snapshots. No runtime code or ROM art is generated.',
        limitations=['Not a proposed per-scene full-frame ROM movie.',
                     'Per-part palette regions are kept, but projection/rounding is approximate; pixel equivalence is not proven.',
                     'Static peak snapshots only; ROM deduplication, reusable scale variants, moving masks/cameras and transitions are not costed.'],
        results=projected_results),ensure_ascii=False,indent=2)+'\n')
    merged_summary=dict(snapshots=len(merged_results),within_budget=sum(r['snapshot_within_budget'] for r in merged_results),
                        max_cycles=max(r['cycles'] for r in merged_results),max_vram_bytes=max(r['vram_bytes'] for r in merged_results),
                        max_objects=max(r['objects'] for r in merged_results))
    (OUT/'projected-palette-bound.json').write_text(json.dumps(dict(summary=merged_summary,
        scope='Pre-baked regular OBJ snapshot feasibility, merging regions that use the same existing palette bank.',
        limitations=['Screen-space snapshots, not a complete reusable transform-variant implementation.',
                     'Pixel/priority equivalence, ROM growth, runtime resource switching and dynamic transitions are not verified.',
                     'No color quantization or palette reduction is applied by this estimate.'],
        results=merged_results),ensure_ascii=False,indent=2)+'\n')
    coverage=[]
    for key,alpha in alpha_cache.items():
        a=linked['assets'][key];cover=np.zeros(alpha.shape,dtype=np.uint16)
        for p in a['parts']:cover[p['y']:p['y']+p['height'],p['x']:p['x']+p['width']]+=1
        area=sum(p['width']*p['height'] for p in a['parts']);copies=int(cover[alpha].sum());unique=int(((cover>0)&alpha).sum())
        coverage.append(dict(asset=key,role=imported[key].get('who','CG'),parts=len(a['parts']),rectangle_pixels=area,
            opaque_pixel_copies=copies,unique_opaque_pixels=unique,duplicate_opaque_pixels=copies-unique,
            unused_or_duplicate_rectangle_fraction=round(1-unique/area,4)))
    (OUT/'asset-coverage.json').write_text(json.dumps(dict(
        scope='Source rectangle coverage only; these fractions are not ROM/compression savings estimates.',assets=coverage),ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    print('projected regular OBJ snapshots:',json.dumps(projected_summary,ensure_ascii=False))
    print('merged existing palette regions:',json.dumps(merged_summary,ensure_ascii=False))
    for r in results:
        if r['path'].endswith(('Adv22','Adv36')) and r['before_cycles']>2200:print(r)


if __name__=='__main__':main()
