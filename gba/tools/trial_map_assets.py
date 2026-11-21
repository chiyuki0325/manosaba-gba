"""Original SpawnableMap timelines, sampled offline into shared 4bpp tiles."""
from pathlib import Path
from functools import lru_cache
import copy,inspect,io,json,math,struct,zlib
import numpy as np
from PIL import Image
import UnityPy
from graphics_project import ROOT,GBA,ContentCache,digest,write_changed
from trial_cutins import clip_curves
from image_encoding import tiles,rgb555
BUNDLE=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle'
NAMES=('Map_A01C01_01','Map_A01C01_03','Map_A01C02_01')

class Source:
    def __init__(self,env,pointer):
        self.env=env;self.nodes=[];self.ids={};self.tracks=[];self.durations=[]
        def visit(ptr,parent=-1,path=''):
            go=ptr.read();components=[c.component for c in go.m_Component];tr=next(c.read_typetree() for c in components if c.type.name=='RectTransform');index=len(self.nodes)
            path=path+'/'+go.m_Name;self.nodes.append(dict(path=path,parent=parent,active=bool(go.m_IsActive),tr=tr,components=components))
            for c in components:self.ids[c.path_id]=index
            transform=next(c.read() for c in components if c.type.name=='RectTransform')
            for ch in transform.m_Children:visit(ch.read().m_GameObject,index,path)
        visit(pointer);self.objects={o.path_id:o for o in env.objects};self.initial=[]
        for i,n in enumerate(self.nodes):
            t=n['tr'];s=t['m_LocalScale'];q=t['m_LocalRotation'];a=next((c.read_typetree().get('m_Alpha') for c in n['components'] if c.type.name=='CanvasGroup'),None)
            self.initial.append(dict(x=t['m_AnchoredPosition']['x'],y=t['m_AnchoredPosition']['y'],sx=s['x'] if i else 1,sy=s['y'] if i else 1,angle=2*math.atan2(q['z'],q['w']),alpha=a if a is not None else 1))
        controller=next(c.read_typetree() for c in self.nodes[0]['components'] if c.type.name=='MonoBehaviour' and '_playableDirectors' in c.read_typetree())
        self.hide=controller['_hideDuration']
        for ref in controller['_playableDirectors']:
            director=self.objects[ref['m_PathID']].read_typetree();timeline=self.objects[director['m_PlayableAsset']['m_PathID']].read_typetree();queue=list(timeline['m_Tracks']);tracks=[];duration=0
            binding={b['key']['m_PathID']:self.ids.get(b['value']['m_PathID']) for b in director['m_SceneBindings']}
            while queue:
                ref=queue.pop();track=self.objects[ref['m_PathID']].read_typetree();queue.extend(track.get('m_Children',[]));cid=track.get('m_InfiniteClip',{}).get('m_PathID',0)
                if not cid:continue
                clip=self.objects[cid].read_typetree();duration=max(duration,clip['m_MuscleClip']['m_StopTime']);target=binding.get(ref['m_PathID'])
                if target is not None:tracks.append((target,clip_curves(clip)))
            self.tracks.append(tracks);self.durations.append(duration)
        self.base=next(i for i,n in enumerate(self.nodes) if n['path'].endswith('/RectMask/Base'))
    def sample(self,index,time,initial):
        states=copy.deepcopy(initial)
        for target,(sample,bindings) in self.tracks[index]:
            for b,first,count in bindings:
                s=states[target];a=b['attribute']
                if b['typeID']==4 and a==3:s['sx'],s['sy']=sample(first,time),sample(first+1,time)
                elif b['typeID']==224 and a==1460864421:s['x']=sample(first,time)
                elif b['typeID']==224 and a==538195251:s['y']=sample(first,time)
                elif b['typeID']==225 and a==1574349066:s['alpha']=sample(first,time)
        return states
    @lru_cache(None)
    def sprite(self,sid):return self.objects[sid].read().image.convert('RGBA')

def main():
    cache=ContentCache();env=UnityPy.load(str(BUNDLE));pointers={Path(n).stem:p for n,p in env.container.items() if Path(n).stem in NAMES};sources={n:Source(env,p) for n,p in pointers.items()}
    icons={};images=[];frames=[];sequences=[];contexts=[];destroys=[]
    def appearance(src,node):
        for c in node['components']:
            if c.type.name=='MonoBehaviour':
                d=c.read_typetree()
                for field in ('m_Sprite','m_Texture'):
                    if d.get(field,{}).get('m_PathID'):return d[field]['m_PathID']
    def ancestors(src,states,index):
        x=y=0.;alpha=1
        while index!=src.base:
            s=states[index];x+=s['x'];y+=s['y'];alpha*=s['alpha'];index=src.nodes[index]['parent']
        return x,y,alpha
    for path in sorted((ROOT/'story/zh-Hans').glob('*/*Trial*.json')):
        doc=json.loads(path.read_text());previous={}
        for e in doc['events']:
            p=e.get('params',{});name=p.get('Path')
            if name not in NAMES:continue
            src=sources[name]
            if e['op']=='DestroySpawnedExtended':destroys.append(dict(path=doc['path'],line=e['line'],frames=round(src.hide*60),wait=bool(p.get('Wait'))));previous.pop(name,None);continue
            if e['op']!='SpawnExtended':continue
            clip=int(p['Params'][0]);start=previous.get(name,src.initial);duration=round(src.durations[clip]*60);nodes=[i for i,n in enumerate(src.nodes) if '/Pins' in n['path'] and appearance(src,n)]
            icon_ids=[]
            for i in nodes:
                sid=appearance(src,src.nodes[i]);key=(name,sid)
                if key not in icons:
                    icons[key]=len(images);im=src.sprite(sid).copy();im.thumbnail((12,15),Image.Resampling.LANCZOS);canvas=Image.new('RGBA',(16,16));canvas.alpha_composite(im,((16-im.width)//2,16-im.height));images.append(canvas)
                icon_ids.append(icons[key])
            first=len(frames)
            for tick in range(0,duration+1,6):
                state=src.sample(clip,tick/60,start);b=state[src.base]
                floor=2 if name!='Map_A01C01_03' else 1;mapname='Map';factor=.2;origin=192
                if name=='Map_A01C02_01':
                    f1=next(i for i,n in enumerate(src.nodes) if n['path'].endswith('/Map_1F'));b1=next(i for i,n in enumerate(src.nodes) if n['path'].endswith('/Map_B1F'));floor=0 if state[f1]['alpha']>state[b1]['alpha'] else 2;mapname='Map_1F' if floor==0 else 'Map_B1F'
                if floor in (0,1):factor=.16
                if floor==0:origin=220
                mapnode=next(i for i,n in enumerate(src.nodes) if n['path'].endswith('/'+mapname));m=state[mapnode]
                left=max(0,min(144,round(origin+(-b['x']/max(.01,b['sx'])-m['x'])*factor-120)))
                top=max(0,min(64,round(112-(-b['y']/max(.01,b['sy'])-m['y'])*factor-80)))
                points=[]
                for i in nodes:
                    x,y,a=ancestors(src,state,i);points.append((round(origin+(x-m['x'])*factor),round(112-(y-m['y'])*factor),round(max(0,min(1,a))*256)))
                frames.append((left,top,floor,round(max(0,min(1,b['alpha']))*256),points))
            previous[name]=src.sample(clip,src.durations[clip],start);segment=len(sequences);sequences.append((first,len(frames)-first,duration,icon_ids));contexts.append(dict(path=doc['path'],line=e['line'],segment=segment,wait=bool(p.get('Wait')),name=name,clip=clip))
    pixels=np.concatenate([np.asarray(im).reshape(-1,4) for im in images]);samples=pixels[pixels[:,3]>=128,:3];pal=Image.fromarray(samples.reshape(1,-1,3)).quantize(colors=15,method=Image.Quantize.MEDIANCUT).getpalette()[:45];colors=[(0,0,0)]+[tuple(pal[i:i+3])for i in range(0,45,3)];blob=bytearray()
    for image in images:
        a=np.asarray(image);dist=((a[:,:,:3].astype(np.int32)[:,:,None,:]-np.array(colors[1:])[None,None,:,:])**2).sum(3);p=Image.fromarray(np.where(a[:,:,3]>=128,dist.argmin(2)+1,0).astype('uint8'));blob.extend(tiles(p,4))
    out=GBA/'assets/trial-maps';out.mkdir(exist_ok=True);write_changed(out/'pins.bin',bytes(blob));lines=['// Generated by trial_map_assets.py; map backgrounds reuse book_map_data.','#include "trial_map_data.h"','namespace manosaba {','const uint16_t trial_map_palette[16]={'+','.join(str(rgb555(c))for c in colors)+'};']
    lines+=['const trial_map_frame trial_map_frames[]={']
    for left,top,floor,alpha,points in frames:lines+=['{'+f'{left},{top},{alpha},{floor},'+'{'+','.join('{%d,%d,%d}'%p for p in points)+'}},']
    lines+=['};','const trial_map_segment trial_map_segments[]={']
    for first,count,ticks,ids in sequences:lines+=['{'+f'{first},{count},{ticks},{len(ids)},'+'{'+','.join(map(str,ids))+'}},']
    lines+=['};','}'];write_changed(GBA/'src/trial_map_data.cpp','\n'.join(lines)+'\n');write_changed(GBA/'src/trial_map_bytes.s','.section .rodata\n.balign 4\n.global trial_map_pins\ntrial_map_pins:\n.incbin "../assets/trial-maps/pins.bin"\n')
    report=dict(names=list(NAMES),contexts=contexts,destroy=destroys,segments=len(sequences),frames=len(frames),pins=len(images),rom_bytes=len(blob)+len(frames)*52+len(sequences)*14+32,background='shared map_pictures/book_map_bytes',interpolation_fps=60,keys_fps=10)
    write_changed(out/'catalog.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(report)
if __name__=='__main__':main()
