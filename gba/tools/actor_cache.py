"""Budgeted offline portrait transforms; runtime never resamples their pixels."""
import collections
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
from graphics_project import GBA, canonical, digest, write_changed
from actor_repack_study import rectangles
from obj_parts import split_tall_parts

BUDGET=128*1024
ENTITY=struct.Struct('<12i4hB3x')


def div(a,b):return (abs(a)//abs(b))*(-1 if (a<0)!=(b<0) else 1)


def coverage_key(x,y,scale,angle):
    key=1
    for value in (scale,angle,x>>11,y>>13):key=((key^(value&0xffffffff))*16777619)&0xffffffff
    return key or 1


def doubled(pa,pb,pc,pd,w,h):
    determinant=pa*pd-pb*pc
    if not determinant:return False
    ww=hh=32
    if w>h:hh=8 if w==h*4 else 16
    if h>w:ww=8 if h==w*4 else 16
    xx=(div(-256*hh*pb-256*ww*pd+256*pb,determinant),div(-256*hh*pb+256*ww*pd+256*pb-256*pd,determinant))
    yy=(div(256*(hh*pa+ww*pc-pa),determinant),div(256*(hh*pa-ww*pc-pa+pc),determinant))
    return any(v < -ww or v>=ww for v in xx) or any(v < -hh or v>=hh for v in yy)


def geometry(requests,work):
    helper=work/'transform_helper'
    subprocess.run(['g++','-std=c++23','-O2','-DBN_CFG_ASSERT_ENABLED=false','-I'+str(GBA/'vendor/butano/butano/include'),str(GBA/'tools/graphics_transform_helper.cpp'),'-o',str(helper)],check=True)
    pairs=sorted(requests)
    result=subprocess.run([str(helper)],input=''.join(f'{s} {a}\n'for s,a in pairs),text=True,capture_output=True,check=True)
    return dict(zip(pairs,[tuple(map(int,row.split()))for row in result.stdout.splitlines()]))


def candidate_states(assets,work):
    # Reuse the production VM and its warm indexed import; no Unity extraction.
    subprocess.run([str(GBA.parent/'python_workspace/.venv/bin/python'),str(GBA/'tools/graphics_budget_scan.py'),'--variant=scale-snap115'],stdout=(work/'scan.log').open('w'),check=True)
    audit=GBA/'build-butano/actor-prototype/scale-snap115'
    subprocess.run([str(audit/'graphics_budget_scan'),str(audit/'input.bin'),str(work/'samples.csv'),'1',str(work/'states.bin')],stdout=subprocess.DEVNULL,stderr=(work/'states.log').open('w'),check=True)
    rows=[];pairs=set();raw=(work/'states.bin').read_bytes();keys=sorted(assets)
    for at in range(0,len(raw),1048):
        h=struct.unpack_from('<16i',raw,at)
        if h[4]!=2:continue
        states=[ENTITY.unpack_from(raw,at+64+i*60)for i in range(16)]
        cam=states[11];pairs.add((cam[2],cam[3]))
        for slot,base in enumerate(h[10:13]):
            if base<0 or h[13+slot]>=3:continue
            e=states[h[13+slot]]
            if e[12]!=base or e[5]==0:continue # Animated images and shared black silhouettes stay native.
            mask=next(((i,m) for i,m in reversed(list(enumerate(states[13:],13)))
                       if e[16] and m[12]>=0 and m[4]>0 and div(m[14]+1,20)==div(e[14],20)),None)
            scale=div(e[2]*cam[2],256);angle=(e[3]+cam[3]+180*256)%(360*256)-180*256
            if (scale==256 and angle==0)or not(128<=scale<=512)or not(-32768<=angle<=32767):continue
            pairs.add((scale,angle));rows.append((base,scale,angle,e,cam,mask))
    matrices=geometry(pairs,work);counts=collections.Counter();positions={};viewports={}
    for base,scale,angle,e,cam,mask in rows:
        cc,ss,*_=matrices[cam[2],cam[3]]
        dx=e[0]-120*256-cam[0];dy=e[1]-80*256-cam[1]
        cx=120*256+div((dx*cc-dy*ss)*cam[2],4096*256);cy=80*256+div((dx*ss+dy*cc)*cam[2],4096*256)
        signature=coverage_key(cx,cy,scale,angle) if mask else 0
        viewport=None
        if mask:
            # Keep all pixels that can enter the viewport within this position
            # cell, including while a live mask is still fading in.
            x=(cx>>11)*8;y=(cy>>13)*32
            viewport=(-x-8,-y-32,240-x,160-y)
        key=(base,scale,angle,cx&255,cy&255,signature)
        if viewport:viewports[key]=viewport
        counts[key]+=1;positions.setdefault(key,[]).append((cx//256,cy//256))
    return counts,positions,matrices,viewports


def bake(asset,scale,angle,phase_x,phase_y,matrix,payload,offsets,viewport=None):
    co,si,pa,pb,pc,pd=matrix;c=div(co*scale,4096);s=div(si*scale,4096)
    parts=[]
    for p in asset['parts']:
        dx=p['x']+p['width']//2-asset['pivot'][0];dy=p['y']+p['height']//2-asset['pivot'][1]
        cx=(phase_x+dx*c-dy*s)//256;cy=(phase_y+dx*s+dy*c)//256
        twice=doubled(pa,pb,pc,pd,p['width'],p['height']);bw=p['width']*(2 if twice else 1);bh=p['height']*(2 if twice else 1)
        parts.append((p,cx,cy,bw,bh))
    sw=max(asset['width'],max(p['x']+p['width'] for p in asset['parts']))
    sh=max(asset['height'],max(p['y']+p['height'] for p in asset['parts']))
    source=np.zeros((sh,sw),dtype=np.uint8);source_banks=np.full((sh,sw),-1,dtype=np.int8)
    # Composite the original indexed pixels before transforming them. Sampling
    # each part around its rounded center would bake hardware seams into ROM.
    for p in reversed(asset['parts']):
        raw=np.frombuffer(b''.join(payload[offsets[t]:offsets[t]+32]for t in p['tiles']),dtype=np.uint8)
        pixels=np.empty((len(p['tiles']),8,8),dtype=np.uint8)
        packed=raw.reshape(-1,8,4);pixels[:,:,::2]=packed&15;pixels[:,:,1::2]=packed>>4
        pixels=pixels.reshape(p['height']//8,p['width']//8,8,8).transpose(0,2,1,3).reshape(p['height'],p['width'])
        dest=source[p['y']:p['y']+p['height'],p['x']:p['x']+p['width']]
        owners=source_banks[p['y']:p['y']+p['height'],p['x']:p['x']+p['width']]
        take=(pixels!=0)&(owners<0);dest[take]=pixels[take];owners[take]=p['bank']
    det=pa*pd-pb*pc;corners=[]
    for x,y in ((0,0),(sw,0),(0,sh),(sw,sh)):
        u=x-asset['pivot'][0];v=y-asset['pivot'][1]
        corners.append((phase_x+div(65536*(pd*u-pb*v),det),phase_y+div(65536*(pa*v-pc*u),det)))
    left=(min(x for x,y in corners)-256)//2048*8;top=(min(y for x,y in corners)-256)//2048*8
    right=(max(x for x,y in corners)+2303)//2048*8;bottom=(max(y for x,y in corners)+2303)//2048*8
    width,height=right-left,bottom-top;yy,xx=np.mgrid[top:bottom,left:right]
    u=((pa*(xx*256-phase_x)+pb*(yy*256-phase_y))>>16)+asset['pivot'][0]
    v=((pc*(xx*256-phase_x)+pd*(yy*256-phase_y))>>16)+asset['pivot'][1]
    valid=(u>=0)&(u<sw)&(v>=0)&(v<sh);u=u.clip(0,sw-1);v=v.clip(0,sh-1)
    indices=np.where(valid,source[v,u],0);banks=np.where(valid,source_banks[v,u],-1)
    if viewport:
        x0,y0,x1,y1=viewport;outside=(xx<x0)|(xx>=x1)|(yy<y0)|(yy>=y1)
        indices[outside]=0;banks[outside]=-1
    packed_parts=[];blobs={}
    for bank in range(4):
        plane=np.where(banks==bank,indices,0);occupied=plane.reshape(height//8,8,width//8,8).any(axis=(1,3))
        cells={(int(x),int(y))for y,x in np.argwhere(occupied)}
        for tx,ty,tw,th in rectangles(cells,60 if viewport else 75):
            refs=[]
            for y in range(th):
                for x in range(tw):
                    tile=np.zeros((8,8),dtype=np.uint8);sample=plane[(ty+y)*8:(ty+y+1)*8,(tx+x)*8:(tx+x+1)*8];tile[:sample.shape[0],:sample.shape[1]]=sample
                    data=(tile[:,::2]|(tile[:,1::2]<<4)).tobytes();key=hashlib.sha256(data).hexdigest();refs.append(key)
                    if key not in offsets:blobs[key]=data
            packed_parts.append(dict(x=tx*8,y=ty*8,width=tw*8,height=th*8,bank=bank,tiles=refs))
    packed_parts=split_tall_parts(packed_parts)
    if len(packed_parts)>(256 if viewport else 64):return None
    return dict(width=width,height=height,pivot=[-left,-top],parts=packed_parts,nominal_scale=scale,baked_angle=angle,palette=asset['palette']),blobs,parts


def cost(parts,pivot,position):
    rows=[0]*160;size=count=0
    for p in parts:
        x=position[0]+p['x']-pivot[0];y=position[1]+p['y']-pivot[1];w=p['width'];h=p['height']
        if x+w<=0 or x>=240 or y+h<=0 or y>=160:continue
        size+=w*h//2;count+=1
        for yy in range(max(0,y),min(160,y+h)):rows[yy]+=w
    return max(rows,default=0)+count*2,size,count


def cached_entry_angles(program,ids,cached):
    """Use a safe cached pose for clipped single-turn entrances, keeping XY/alpha."""
    safe={(v['base'],v['scale'],v['angle'])for v in cached if v['clipped']}
    result=set()
    for begin,count,_ in program['events']:
        fresh=set();images={};scales={};angles={}
        for index in range(begin,begin+count):
            value,first,n,kind,target,prop,flags=program['commands'][index]
            if kind==3: # clear resets a newly spawned actor, not a later turn.
                fresh.update(range(first,first+n))
            elif kind==0 and target<3 and target in fresh:
                images[target]=ids[program['resources'][value]]
            elif kind==1 and target in images and n==1 and not(flags&127):
                key=program['keys'][first]
                if key[3]:continue
                if prop==2 and not key[1]:scales[target]=key[0]
                if prop==3 and key[1]:angles[target]=(index,key[0])
        for target,(index,angle) in angles.items():
            angle=(angle+180*256)%(360*256)-180*256
            if (images[target],scales.get(target,256),angle) in safe:result.add(index)
    return result


def build(assets,ids,offsets,payload,programs,budget=BUDGET):
    work=GBA/'build-butano/actor-cache';work.mkdir(parents=True,exist_ok=True)
    cache=GBA/'assets/graphics-cache/actor-cache';cache.mkdir(parents=True,exist_ok=True)
    signature=digest(dict(budget=budget,assets=digest(assets),programs=programs,payload=hashlib.sha256(payload).hexdigest(),stage=hashlib.sha256((GBA/'src/stage_player.cpp').read_bytes()).hexdigest(),tools=[hashlib.sha256((GBA/'tools'/n).read_bytes()).hexdigest()for n in('actor_cache.py','obj_parts.py','graphics_transform_helper.cpp','graphics_budget_scan.cpp','graphics_budget_scan.py')]))
    manifest=cache/'manifest.json';binary=cache/'payload.bin'
    if manifest.exists():
        old=json.loads(manifest.read_text())
        if old['signature']==signature:return old['variants'],payload+binary.read_bytes(),old
    counts,positions,matrices,viewports=candidate_states(assets,work);keys=sorted(assets);choices=[]
    for spec,uses in counts.items():
        base,scale,angle,px,py,coverage=spec;a=assets[keys[base]]
        result=bake(a,scale,angle,px,py,matrices[scale,angle],payload,offsets,viewport=viewports.get(spec))
        if result is None:continue
        baked,blobs,old=result;savings=[]
        for position in positions[spec]:
            before=[0]*160
            for p,cx,cy,bw,bh in old:
                x=position[0]+cx-bw//2;y=position[1]+cy-bh//2
                if x+bw<=0 or x>=240 or y+bh<=0 or y>=160:continue
                for yy in range(max(0,y),min(160,y+bh)):before[yy]+=10+bw*2
            new,_,_=cost(baked['parts'],baked['pivot'],position);savings.append(max(before)-new)
        saved=max(savings)
        # ARM graphic_asset (36B) plus graphic_cache_entry (16B).
        directory=52+sum(12+4*len(p['tiles'])for p in baked['parts']);extra=sum(map(len,blobs.values()))+directory
        co,si,*_=matrices[scale,angle];c=abs(div(co*scale,4096));s=abs(div(si*scale,4096))
        clipped=any(c*p['width']+s*p['height']>bw*256+256 or s*p['width']+c*p['height']>bh*256+256 for p,cx,cy,bw,bh in old)
        if spec in viewports and not clipped:continue
        if saved<200 and not clipped:continue
        choices.append(dict(base=base,scale=scale,angle=angle,phase_x=px,phase_y=py,coverage_key=coverage,clipped=clipped,uses=uses,saved_cycles=saved,asset=baked,blobs=blobs,directory=directory,score=max(saved,200)*uses/max(extra,1)))
    variants=[];tail=bytearray();new_offsets=dict(offsets);directory=4
    for choice in sorted(choices,key=lambda r:(not r['clipped'],-r['score'])):
        additions={k:v for k,v in choice['blobs'].items()if k not in new_offsets}
        added=sum(map(len,additions.values()))
        if len(tail)+added+directory+choice['directory']+2>budget:continue
        for key,data in additions.items():new_offsets[key]=len(payload)+len(tail);tail.extend(data)
        item={k:v for k,v in choice.items()if k not in('blobs','score','directory')};item['image']=len(ids)+len(variants)
        for p in item['asset']['parts']:p['tiles']=[new_offsets[t]for t in p['tiles']]
        item['asset']['palette']=offsets[item['asset']['palette']]
        variants.append(item);directory+=choice['directory']
    directory+=(-directory)%4
    result=dict(signature=signature,budget=budget,payload_bytes=len(tail),directory_bytes=directory,variants=variants,candidates=len(choices))
    write_changed(manifest,canonical(result));write_changed(binary,tail)
    print(f'Actor cache: {len(variants)} variants, {len(tail)+directory}/{budget} bytes',flush=True)
    return variants,payload+tail,result
