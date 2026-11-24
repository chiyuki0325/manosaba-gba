"""Bake position corrections for fixed court portraits from prepared GBA assets."""
from collections import Counter, deque
from functools import lru_cache
import json
from pathlib import Path
import re
import struct
import sys

import numpy as np

GBA = Path(__file__).resolve().parents[1]
SHAPES = ((8,8),(8,4),(4,8),(4,4),(4,2),(2,4),(2,2),(4,1),(1,4),(2,1),(1,2),(1,1))
NEUTRAL = 18


def array(source, name):
    return re.search(r'\b'+name+r'\[[^=]*\]\s*=\s*\{(.*?)\};', source, re.S)[1]


def numbers(text):
    return tuple(map(int,re.findall(r'-?\d+',text)))


def div(a,b):
    return a//b if a>=0 else -((-a)//b)


def pieces(parts, pivot_x, pivot_y, bottom):
    # Same order as trial_scene::placement, including transparent pieces.
    result=[]
    for x,y,w,h,alpha in parts:
        width=w//8;height=min(h//8,div(bottom-y+7,8))
        ty=0
        while ty<height:
            tx=0
            while tx<width:
                tw,th=next((a,b) for a,b in SHAPES if tx+a<=width and ty+b<=height)
                u,v=tx*8,ty*8;w1,h1=tw*8,th*8
                ink=alpha[v:v+h1,u:u+w1] & (np.arange(y+v,y+v+h1)[:,None]<bottom)
                result.append((x+u-pivot_x,y+v-pivot_y,w1,h1,ink))
                tx+=tw
            ty+=th
    assert len(result)<=64
    return result


def describe(parts, scale):
    q=65536//scale
    def auto_double(w,h):
        # Butano tests the affine enclosure with shape-specific bounds,
        # which are different from the smaller OBJ's texture dimensions.
        if w==h:hw,hh=32,32
        elif w>h:hw,hh=32,8 if (w,h)==(32,8) else 16
        else:hw,hh=8 if (w,h)==(8,32) else 16,32
        return div(-256*hw,q)<-hw or div(256*(hh-1),q)>=hh

    def axis(origin,size,doubled):
        center=div((origin+size//2)*scale,256)
        radius=size if doubled else size//2
        return center,max(-radius,-(size*128//q)),min(radius,(size*128+q-1)//q)
    # OBJ texture coordinates are fixed-point and relative to each sprite center.
    return [(axis(x,w,doubled),axis(y,h,doubled),w,h,ink,doubled)
            for x,y,w,h,ink in parts
            for doubled in (auto_double(w,h),)]


def edges(parts):
    result=([],[])
    for dim, group in enumerate(result):
        for i,first in enumerate(parts):
            for j,second in enumerate(parts):
                if first[dim]+first[dim+2]!=second[dim]:
                    continue
                u,v=first[1-dim],second[1-dim]
                lo,hi=max(u,v),min(u+first[3-dim],v+second[3-dim])
                if lo>=hi:
                    continue
                a,b=first[4],second[4]
                if dim==0:a,b=a.T,b.T
                if np.any(a[-1,lo-u:hi-u]&b[0,lo-v:hi-v]):group.append((i,j))
    return result


def differences(bounds, links, dim):
    return [(i,j,bounds[i][dim][0]+bounds[i][dim][2]-bounds[j][dim][0]-bounds[j][dim][1]) for i,j in links]


def solve(count, relationships, pinned, maximum, reverse=False):
    """Keep touching edges adjacent or give them one pixel of overlap."""
    links=[[] for _ in range(count)]
    for i,j,d in relationships:
        links[i].append((j,d-1,d));links[j].append((i,-d,1-d))
    def assign(domains,queue):
        while queue:
            i=queue.popleft();lo,hi=domains[i]
            for j,dlo,dhi in links[i]:
                first,last=domains[j];changed=max(first,lo+dlo),min(last,hi+dhi)
                if changed[0]>changed[1]:return None
                if changed!=domains[j]:domains[j]=changed;queue.append(j)
        order=range(count-1,-1,-1) if reverse else range(count)
        for i in order:
            lo,hi=domains[i]
            if lo==hi:continue
            for value in sorted(range(lo,hi+1),key=lambda n:(abs(n),n)):
                next_domains=domains.copy();next_domains[i]=(value,value)
                found=assign(next_domains,deque([i]))
                if found is not None:return found
            return None
        return [lo for lo,_ in domains]
    return assign([(0,0) if i in pinned else (-maximum,maximum) for i in range(count)],deque(range(count)))


def sample(bounds,q,offsets,area):
    x0,y0,x1,y1=area
    mask=np.zeros((y1-y0,x1-x0),dtype=bool)
    for ((cx,lo,hi),(cy,top,bottom),w,h,ink,_),(dx,dy) in zip(bounds,offsets):
        columns=(np.arange(lo,hi)*q)//256+w//2
        rows=(np.arange(top,bottom)*q)//256+h//2
        x,y=cx+lo+dx-x0,cy+top+dy-y0
        mask[y:y+len(rows),x:x+len(columns)]|=ink[np.ix_(rows,columns)]
    return mask


def peak(bounds, offsets, y):
    rows=[0]*160
    for (_, (center,_,_),w,h,_,double),(_,dy) in zip(bounds,offsets):
        radius=h if double else h//2
        first,last=y+center+dy-radius,y+center+dy+radius
        for row in range(max(0,first),min(160,last)):
            rows[row]+=10+2*w*(2 if double else 1)
    return max(rows)


def seam_pixels(bounds, q, relationships, mask, area):
    x0,y0,_,_=area;leaks=np.zeros_like(mask)
    for dim, links, existing, result, row0,col0 in ((0,relationships[0],mask.T,leaks.T,x0,y0),
                                                    (1,relationships[1],mask,leaks,y0,x0)):
        for i,j,d in links:
            if d>=0:continue
            a,b=bounds[i],bounds[j];ao,bo=a[1-dim],b[1-dim];av,bv=a[dim],b[dim]
            ink_a,ink_b=(a[4].T,b[4].T) if dim==0 else (a[4],b[4])
            cols=np.arange(max(ao[0]+ao[1],bo[0]+bo[1]),min(ao[0]+ao[2],bo[0]+bo[2]))
            ca=(cols-ao[0])*q//256+ink_a.shape[1]//2
            cb=(cols-bo[0])*q//256+ink_b.shape[1]//2
            ra=(av[2]-1)*q//256+ink_a.shape[0]//2
            rb=bv[1]*q//256+ink_b.shape[0]//2
            cols=cols[ink_a[-1,ca]&ink_b[0,cb]&ink_a[ra,ca]&ink_b[rb,cb]]-col0
            top,bottom=av[0]+av[2]-row0,bv[0]+bv[1]-row0
            result[top:bottom,cols]|=~existing[top:bottom,cols]
    return leaks


def safe_alignment(parts,scale,bottom,screen_y):
    if scale in (256,512) or not parts:return 'clear',None
    bounds=describe(parts,scale);count=len(parts)
    relationships=[differences(bounds,links,dim) for dim,links in enumerate(edges(parts))]
    if all(d>=0 for group in relationships for _,_,d in group):return 'clear',None
    q=65536//scale
    area=(min(x+l for (x,l,_),*rest in bounds)-3,min(y+t for _,(y,t,_),*rest in bounds)-3,
          max(x+r for (x,_,r),*rest in bounds)+3,max(y+b for _,(y,_,b),*rest in bounds)+3)
    zero=[(0,0)]*count
    before=sample(bounds,q,zero,area)
    leaks=seam_pixels(bounds,q,relationships,before,area)
    if not leaks.any():return 'clear',None
    base_peak=max(900,peak(bounds,zero,screen_y))
    pinned={i for i,(_,y,_,h,ink) in enumerate(parts) if y+h>=bottom and ink.any()}
    for maximum in (1,2):
        xs=[solve(count,relationships[0],set(),maximum,r) for r in (False,True)]
        ys=[solve(count,relationships[1],pinned,maximum,r) for r in (False,True)]
        best=None
        for u in xs:
            for v in ys:
                if u is None or v is None:continue
                offsets=list(zip(u,v));after=sample(bounds,q,offsets,area)
                if not after.any() or np.flatnonzero(before.any(axis=1))[-1]!=np.flatnonzero(after.any(axis=1))[-1]:continue
                if peak(bounds,offsets,screen_y)>base_peak:continue
                # A one-pixel outline change is acceptable; introducing a new line is not.
                internal=(after[1:-1,:-2]&after[1:-1,2:])|(after[:-2,1:-1]&after[2:,1:-1])
                lost=before[1:-1,1:-1]&~after[1:-1,1:-1]&internal
                if (np.any(lost[:,:-2]&lost[:,1:-1]&lost[:,2:]) or
                    np.any(lost[:-2,:]&lost[1:-1,:]&lost[2:,:])):continue
                filled=int((after&leaks).sum());loss=int(lost.sum())
                if filled<=loss:continue
                quality=(filled-loss,-loss,-sum(abs(dx)+abs(dy) for dx,dy in offsets))
                if best is None or quality>best[0]:best=quality,bytes(NEUTRAL+dx+8*dy for dx,dy in offsets)
        if best is not None:return 'aligned',best[1]
    return 'retained',None


def generate():
    graphics=(GBA/'src/graphics_data.cpp').read_text();trial=(GBA/'src/trial_data.cpp').read_text()
    tiles=(GBA/'assets/graphics/graphics.bin').read_bytes();court=(GBA/'assets/trial/court.bin').read_bytes()
    descriptions=re.findall(r'\{([^{}]+)\}',array(graphics,'graphic_assets'))
    part_tables=dict(re.findall(r'const graphic_part (\w+)\[\]=\{(.*?)\};',graphics,re.S))
    tile_tables=dict(re.findall(r'const uint32_t (\w+_tiles)\[\]=\{(.*?)\};',graphics,re.S))

    @lru_cache(None)
    def asset(image):
        fields=descriptions[image].split(',')
        w,h,_,_,_,px,py=map(int,fields[4:11]);parts=[]
        for entry in re.findall(r'\{([^{}]+)\}',part_tables[fields[0]]):
            table,x,y,width,height,*_=entry.split(',');x,y,width,height=map(int,(x,y,width,height))
            ink=np.zeros((height,width),dtype=bool)
            for i,at in enumerate(numbers(tile_tables[table])):
                tile=np.frombuffer(tiles,dtype=np.uint8,count=32,offset=at)
                tx,ty=i%(width//8)*8,i//(width//8)*8
                ink[ty:ty+8,tx:tx+8]=np.stack((tile&15,tile>>4),axis=1).reshape(8,8)!=0
            parts.append((x,y,width,height,ink))
        return w,h,px,py,parts

    base=numbers(array(trial,'trial_actor_images'))
    available=[{image} for image in base]
    poses=json.loads((GBA/'assets/trial/poses.json').read_text())
    for pose in poses['references']:available[pose['actor']].add(pose['image'])
    close=numbers(array(trial,'trial_view_positions'))
    cases={}
    for at,actor,view,y,scale in re.findall(r'\(trial_bytes\+(\d+)\),cast_(\d+)_(\d+),-?\d+,(-?\d+),(\d+),',trial):
        at,actor,view,y,scale=map(int,(at,actor,view,y,scale))
        base_w,_,_,base_py,_=asset(base[actor])
        cutoff=struct.unpack_from('<h',court,at+(base_w//2)*2)[0]
        for image in available[actor]:
            _,h,_,py,_=asset(image);limit=min(h,cutoff+py-base_py)
            cases.setdefault((image,scale,limit),set()).add(y)
    for image in set().union(*available):
        _,h,_,py,_=asset(image)
        for _,y,cutoff,view_scale in zip(*[iter(close)]*4):
            scale=min(512,view_scale*300//h);limit=min(h,(cutoff-y)*256//scale+py)
            cases.setdefault((image,scale,limit),set()).add(y)

    offsets=bytearray([NEUTRAL]);plans={};entries=[];stats=Counter()
    for (image,scale,limit),screen_rows in sorted(cases.items()):
        _,_,px,py,source=asset(image)
        layout=pieces(source,px,py,limit)
        # Identical crops in different views share a solution only if it is safe at every vertical offset.
        results=[safe_alignment(layout,scale,limit-py,y) for y in sorted(screen_rows)]
        status,plan=results[0]
        if any(result!=(status,plan) for result in results):status,plan='retained',None
        stats[status]+=1
        if plan is None:continue
        if plan not in plans:plans[plan]=len(offsets);offsets.extend(plan)
        entries.append((image,scale,limit,plans[plan]))
    assert len(offsets)<65536
    lines=['// Generated by trial_alignment.py from prepared court and OBJ assets.', '#include "trial_alignment.h"',
           'namespace manosaba {', 'const uint8_t trial_alignment_offsets[]={']
    lines+=['    '+','.join(map(str,offsets[i:i+32]))+',' for i in range(0,len(offsets),32)]
    lines+=['};','extern const trial_alignment_entry trial_alignment_entries[]={']
    lines+=['    '+','.join('{'+','.join(map(str,e))+'}' for e in entries[i:i+6])+',' for i in range(0,len(entries),6)]
    lines+=['};','extern const int trial_alignment_count=sizeof(trial_alignment_entries)/sizeof(trial_alignment_entries[0]);','}','']
    target=GBA/'src/trial_alignment_data.cpp';text='\n'.join(lines)
    if '--check' in sys.argv:
        assert target.read_text()==text,'Prepared court alignment table is out of date'
    elif not target.exists() or target.read_text()!=text:target.write_text(text)
    print(f'Court OBJ alignment: {len(set().union(*available))} base/alternate images; {dict(stats)}; {len(entries)*8+len(offsets)} bytes of data')


if __name__=='__main__':
    generate()
