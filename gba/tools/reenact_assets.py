"""Import original reenactment poses into shared 4bpp BG/OBJ tile dictionaries.

The stage frame is omitted. Static sprite groups are composed on the GBA;
only the few authored deforming poses are baked offline. Empty 8x8 cells never
belong to an OBJ rectangle. Source RGBA intermediates are content cached.
"""
from build_paths import generated_path
from pathlib import Path
from functools import lru_cache
from concurrent.futures import ThreadPoolExecutor
import hashlib, inspect, io, json, math, struct, zlib
import numpy as np
from PIL import Image, ImageDraw
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler
from graphics_project import GBA, ROOT, ContentCache, digest, write_changed
from trial_cutins import clip_curves, sample_track
from reenact_mockup import matrix
from image_encoding import rgb555, tiles

BUNDLE=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle'
OUT=GBA/'assets/reenact'
SHAPES=((64,64),(64,32),(32,64),(32,32),(32,16),(16,32),(16,16),(32,8),(8,32),(16,8),(8,16),(8,8))
POSE_COUNTS={'Reenact_Ch01_E01_S01':4,'Reenact_Ch01_E03_S06':5,
             'Reenact_Ch01_E05_S07':6,'Reenact_Ch01_E05_S08':6}
MOVING={'Reenact_Ch01_E01_S01':'Noa','Reenact_Ch02_E04_S04':'Ema'}
FIGURES=set('Ema Hiro Leia Noa Shelly Hanna Hannna Koko Anan Mago Milia Mlia Alisa Meruru Nanoka Yuki Ema_Leia'.split())
SS=2


class Source:
    def __init__(self,env,pointer,config):
        self.env=env;self.objects={o.path_id:o for o in env.objects};self.nodes=[];self.component_nodes={};self.transform_nodes={}
        def visit(ptr,parent=-1,path=''):
            go=ptr.read();components=[c.component for c in go.m_Component]
            tr=next(c.read() for c in components if c.type.name=='Transform')
            index=len(self.nodes);path=(path+'/' if path else '')+go.m_Name
            self.nodes.append(dict(name=go.m_Name,path=path,parent=parent,active=go.m_IsActive,
                pos=dict(x=tr.m_LocalPosition.x,y=tr.m_LocalPosition.y),
                angle=math.degrees(math.atan2(2*(tr.m_LocalRotation.w*tr.m_LocalRotation.z+tr.m_LocalRotation.x*tr.m_LocalRotation.y),1-2*(tr.m_LocalRotation.y**2+tr.m_LocalRotation.z**2))),
                scale=dict(x=tr.m_LocalScale.x,y=tr.m_LocalScale.y),components=components))
            for c in components:
                self.component_nodes[c.path_id]=index
                if c.type.name=='Transform':self.transform_nodes[c.path_id]=index
            for child in tr.m_Children:visit(child.read().m_GameObject,index,path)
        visit(pointer)
        self.name=self.nodes[0]['name'];self.groups=[];self.sprites=[]
        self.story=next(i for i,n in enumerate(self.nodes) if n['path']==self.name+'/Story')
        root=next(c.read_typetree() for c in self.nodes[0]['components'] if c.type.name=='MonoBehaviour' and '_storiesDirector' in c.read_typetree())
        self.data=dict(objects={},clips={});self.tracks=[];self.duration=0.
        for ref in root['_storiesDirector']:
            director=self.objects[ref['m_PathID']].read_typetree()
            timeline=self.objects[director['m_PlayableAsset']['m_PathID']].read_typetree()
            queue=list(timeline['m_Tracks']);tracks={}
            while queue:
                ref=queue.pop();track=self.objects[ref['m_PathID']].read_typetree();tracks[ref['m_PathID']]=track
                queue.extend(track.get('m_Children',[]))
                for segment in track.get('m_Clips',[]):
                    self.duration=max(self.duration,segment['m_Start']+segment['m_Duration'])
                    aid=segment['m_Asset']['m_PathID'];a=self.objects[aid].read_typetree();self.data['objects'][str(aid)]=a
                    cid=a.get('m_Clip',{}).get('m_PathID',0)
                    if cid:self.data['clips'][str(cid)]=self.objects[cid].read_typetree()
                cid=track.get('m_InfiniteClip',{}).get('m_PathID',0)
                if cid:
                    clip=self.objects[cid].read_typetree();self.data['clips'][str(cid)]=clip
                    self.duration=max(self.duration,clip['m_MuscleClip']['m_StopTime'])
            for bind in director['m_SceneBindings']:
                track=tracks.get(bind['key']['m_PathID']);target=self.component_nodes.get(bind['value']['m_PathID'])
                if track is None or target is None:continue
                prefix=self.nodes[target]['path'];paths={0:target}
                for i,n in enumerate(self.nodes):
                    if n['path'].startswith(prefix+'/'):paths[zlib.crc32(n['path'][len(prefix)+1:].encode())]=i
                self.tracks.append((track,paths))
        for i,n in enumerate(self.nodes):
            if n['parent']==self.story:self.groups.append(i)
            if '/Story/' not in n['path']:continue
            renderer=next((c.read() for c in n['components'] if c.type.name=='SpriteRenderer'),None)
            if renderer and renderer.m_Enabled and renderer.m_Sprite.m_PathID:
                skin=next((c.read_typetree() for c in n['components'] if c.type.name=='MonoBehaviour' and 'm_BoneTransforms' in c.read_typetree()),None)
                group=i
                while self.nodes[group]['parent']!=self.story:group=self.nodes[group]['parent']
                self.sprites.append((renderer.m_SortingOrder,i,group,renderer,skin))
        self.sprites.sort(key=lambda r:r[0])
        crop=config['crop'];factor=240/(crop[2]-crop[0]);ppu=config['source_canvas'][0]/config['world_width']
        self.screen=np.array([[ppu*factor*SS,0,(config['source_canvas'][0]/2-crop[0])*factor*SS],
                              [0,-ppu*factor*SS,(config['source_canvas'][1]/2-crop[1])*factor*SS],[0,0,1.]])

    def state(self,time):
        states=[dict(pos=n['pos'].copy(),angle=n['angle'],scale=n['scale'].copy(),alpha=1.) for n in self.nodes]
        for track,paths in self.tracks:
            sampled,_=sample_track(self.data,track,time)
            if not sampled:continue
            sample,bindings=sampled
            baseline,_=sample_track(self.data,track,0)
            initial=baseline[0] if baseline else sample
            for binding,index,count in bindings:
                target=paths.get(binding['path'])
                if target is None:continue
                s=states[target];attr=binding['attribute']
                if binding['typeID']==4:
                    if attr==1:
                        s['pos']=dict(x=sample(index,time),y=sample(index+1,time))
                        if binding['path']==0:
                            for k,key in enumerate(('x','y')):s['pos'][key]+=self.nodes[target]['pos'][key]-initial(index+k,0)
                    elif attr==3:
                        s['scale']=dict(x=sample(index,time),y=sample(index+1,time))
                        if binding['path']==0:
                            for k,key in enumerate(('x','y')):s['scale'][key]+=self.nodes[target]['scale'][key]-initial(index+k,0)
                    elif attr==4:
                        s['angle']=sample(index+2,time)
                        if binding['path']==0:s['angle']+=self.nodes[target]['angle']-initial(index+2,0)
                    elif attr==2:
                        x,y,z,w=(sample(index+k,time) for k in range(4));s['angle']=math.degrees(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z)))
                elif binding['typeID']==212 and attr==304273561:s['alpha']=sample(index,time)
        worlds=[]
        for i,s in enumerate(states):
            angle=math.radians(s['angle']);c,sn=math.cos(angle),math.sin(angle)
            m=np.array([[c*s['scale']['x'],-sn*s['scale']['y'],s['pos']['x']],
                        [sn*s['scale']['x'],c*s['scale']['y'],s['pos']['y']],[0,0,1.]])
            parent=self.nodes[i]['parent'];worlds.append(worlds[parent]@m if parent>=0 else m)
        return states,worlds

    @lru_cache(maxsize=None)
    def sprite(self,path_id):
        sprite=self.objects[path_id].read();return sprite,sprite.image.convert('RGBA')

    @lru_cache(maxsize=None)
    def atlas(self,path_id):
        return self.objects[path_id].read().image.convert('RGBA')

    def paint(self,target,renderer,skin,state,worlds,index):
        sprite,image=self.sprite(renderer.m_Sprite.path_id);rect,offset=sprite.m_Rect,sprite.m_RD.textureRectOffset
        ppu=sprite.m_PixelsToUnits
        local=np.array([[1/ppu,0,(offset.x-sprite.m_Pivot.x*rect.width)/ppu],
                        [0,-1/ppu,(offset.y+image.height-sprite.m_Pivot.y*rect.height)/ppu],[0,0,1.]])
        if renderer.m_FlipX:local[0]*=-1
        if renderer.m_FlipY:local[1]*=-1
        tint=renderer.m_Color;alpha=max(0.,min(1.,state['alpha']*tint.a))
        if alpha<.001:return
        if alpha<.999 or min(tint.r,tint.g,tint.b)<.999:
            rgba=np.asarray(image).astype(np.float32).copy();rgba*=np.array([tint.r,tint.g,tint.b,alpha]);image=Image.fromarray(np.clip(rgba,0,255).astype('uint8'))
        if not skin:
            transform=self.screen@worlds[index]@local
            if abs(np.linalg.det(transform))<1e-7:return
            inverse=np.linalg.inv(transform)
            target.alpha_composite(image.transform(target.size,Image.Transform.AFFINE,tuple(inverse[:2].reshape(-1)),Image.Resampling.BICUBIC));return
        # Only the first Leia puppet uses SpriteSkin. Deform it offline, with the
        # exact source bind poses and referenced bones, then rasterize its mesh.
        mesh=MeshHandler(sprite.m_RD,version=sprite.object_reader.version);mesh.process()
        vertices=np.c_[np.asarray(mesh.m_Vertices)[:,:2],np.ones(len(mesh.m_Vertices))]
        skinned=np.zeros_like(vertices)
        for bone,bind in enumerate(sprite.m_RD.m_Bindpose):
            b=np.array([[getattr(bind,f'e{r}{c}') for c in (0,1,3)] for r in (0,1,3)])
            world=worlds[self.transform_nodes[skin['m_BoneTransforms'][bone]['m_PathID']]]
            weights=np.asarray(mesh.m_BoneWeights);indices=np.asarray(mesh.m_BoneIndices)
            w=np.where(indices==bone,weights,0).sum(axis=1)
            skinned+=(vertices@(world@b).T)*w[:,None]
        dest=(skinned@self.screen.T)[:,:2]
        # Sprite.image trims a tight mesh's empty border; mesh UVs refer to the
        # original atlas, so sample that atlas rather than the trimmed bitmap.
        image=self.atlas(sprite.m_RD.texture.path_id)
        uv=np.asarray(mesh.m_UV0)
        texture=np.c_[uv[:,0]*image.width,(1-uv[:,1])*image.height]
        for triangle in np.asarray(mesh.m_IndexBuffer).reshape(-1,3):
            points=dest[triangle];left,top=np.floor(points.min(axis=0)).astype(int);right,bottom=np.ceil(points.max(axis=0)).astype(int)
            left=max(0,left);top=max(0,top);right=min(target.width,right);bottom=min(target.height,bottom)
            if right<=left or bottom<=top:continue
            xy=np.c_[points-[left,top],np.ones(3)]
            if abs(np.linalg.det(xy))<1e-7:continue
            inverse=np.linalg.solve(xy,texture[triangle]).T
            patch=image.transform((right-left,bottom-top),Image.Transform.AFFINE,tuple(inverse.reshape(-1)),Image.Resampling.BICUBIC)
            mask=Image.new('L',patch.size);ImageDraw.Draw(mask).polygon([tuple(p) for p in points-[left,top]],fill=255)
            patch.putalpha(Image.fromarray((np.asarray(patch.getchannel('A')).astype(np.uint16)*np.asarray(mask)//255).astype('uint8')))
            target.alpha_composite(patch,(left,top))

    def render(self,time):
        states,worlds=self.state(time);layers={g:Image.new('RGBA',(240*SS,160*SS)) for g in self.groups}
        moving=MOVING.get(self.name)
        if moving:
            group=next(g for g in self.groups if self.nodes[g]['name']==moving)
            _,base=self.state(0);adjust=base[group]@np.linalg.inv(worlds[group]);prefix=self.nodes[group]['path']
            for i,n in enumerate(self.nodes):
                if i==group or n['path'].startswith(prefix+'/'):worlds[i]=adjust@worlds[i]
        for _,index,group,renderer,skin in self.sprites:
            active=True;n=index
            while n>=0:active&=self.nodes[n]['active'];n=self.nodes[n]['parent']
            if active:self.paint(layers[group],renderer,skin,states[index],worlds,index)
        return {self.nodes[g]['name']:im.resize((240,160),Image.Resampling.LANCZOS) for g,im in layers.items()}


def indices(image,colors,transparent=True):
    rgba=np.asarray(image);unique,inverse=np.unique((rgba[:,:,:3]>>3).reshape(-1,3),axis=0,return_inverse=True)
    nearest=((unique[:,None,:].astype(np.int32)-(np.asarray(colors[1:])>>3)[None,:,:])**2).sum(axis=2).argmin(axis=1)+1
    pixels=nearest[inverse].astype('uint8').reshape(160,240)
    if transparent:pixels[rgba[:,:,3]<96]=0
    return pixels


def sparse_parts(pixels):
    occupied=(pixels.reshape(20,8,30,8)!=0).any(axis=(1,3));parts=[]
    for y in range(20):
        for x in range(30):
            if not occupied[y,x]:continue
            for w,h in SHAPES:
                tw,th=w//8,h//8
                if x+tw<=30 and y+th<=20 and occupied[y:y+th,x:x+tw].all():
                    occupied[y:y+th,x:x+tw]=False
                    parts.append((x*8,y*8,w,h,pixels[y*8:y*8+h,x*8:x*8+w]));break
    return parts


def main():
    OUT.mkdir(parents=True,exist_ok=True);cache=ContentCache();config=json.loads((GBA/'assets/reenact-layout.json').read_text())
    source_key=digest(dict(step='reenact-poses-v2',bundle=cache.file_key(BUNDLE),layout=config,renderer=digest(inspect.getsource(Source))))
    cached=cache.get_json(source_key)
    if cached is None:
        env=UnityPy.load(str(BUNDLE));scenes=[]
        for path,ptr in sorted(env.container.items()):
            if '/Reenactment/' not in path:continue
            source=Source(env,ptr,config);count=POSE_COUNTS.get(source.name,3 if source.name=='Reenact_Ch01_E02_S05' else 1)
            frames=[]
            for frame in range(count):
                time=source.duration*frame/max(1,count-1)
                if source.name=='Reenact_Ch01_E02_S05':time=(0.,3.,6.)[frame]
                layers=source.render(time);saved={}
                for name,image in layers.items():
                    data=io.BytesIO();image.save(data,format='PNG');saved[name]=cache.put_blob(data.getvalue(),'.png')
                frames.append(saved)
            moves=[];moving=MOVING.get(source.name,'')
            if moving:
                target=next(g for g in source.groups if source.nodes[g]['name']==moving)
                _,world0=source.state(0)
                for frame in range(round(source.duration*60)+1):
                    _,world=source.state(frame/60);delta=(source.screen@world[target])[:2,2]-(source.screen@world0[target])[:2,2]
                    moves.append([int(round(delta[0]/SS)),int(round(delta[1]/SS))])
            order=sorted(source.groups,key=lambda g:min((r[0] for r in source.sprites if r[2]==g),default=-1000))
            scenes.append(dict(name=source.name,order=[source.nodes[g]['name'] for g in order],frames=frames,duration=round(source.duration*60),moving=moving,moves=moves))
            print('rendered',source.name,'poses',count,flush=True)
            source.sprite.cache_clear()
            source.atlas.cache_clear()
        # The closed image is split into left/right hardware BGs at runtime.
        root=next(p for n,p in env.container.items() if n.endswith('Reenact_Ch01_E01_S01.prefab'))
        cloth=[]
        def curtain(ptr):
            go=ptr.read();cs=[c.component for c in go.m_Component]
            if go.m_Name in ('Curtain_Close_L','Curtain_Close_R'):
                sprite=next(c.read().m_Sprite.read() for c in cs if c.type.name=='SpriteRenderer');cloth.append((go.m_Name,sprite.image.convert('RGBA')))
            tr=next(c.read() for c in cs if c.type.name=='Transform')
            for child in tr.m_Children:curtain(child.read().m_GameObject)
        curtain(root);image=Image.new('RGBA',(240,160))
        for name,im in cloth:
            image.alpha_composite(im.resize((120,160),Image.Resampling.LANCZOS),(0 if name.endswith('_L') else 120,0))
        # Closed cloth must cover the whole view, including transparent mesh margins.
        base=Image.new('RGBA',(240,160),(32,16,24,255));base.alpha_composite(image)
        data=io.BytesIO();base.save(data,format='PNG');curtain_png=cache.put_blob(data.getvalue(),'.png')
        cached=dict(scenes=scenes,curtain=curtain_png);cache.put_json(source_key,cached)
    scenes=cached['scenes']
    # Playback mode belongs to the director, not AnimationClip.m_LoopTime.
    # Keep this metadata separate from the expensive rasterized-pose cache.
    playback_key=digest(dict(step='reenact-playback-v1',bundle=cache.file_key(BUNDLE)))
    playback=cache.get_json(playback_key)
    if playback is None:
        env=UnityPy.load(str(BUNDLE));objects={o.path_id:o for o in env.objects};playback={}
        for path,ptr in env.container.items():
            if '/Reenactment/' not in path:continue
            go=ptr.read()
            root=next(c.component.read_typetree() for c in go.m_Component if c.component.type.name=='MonoBehaviour' and '_storiesDirector' in c.component.read_typetree())
            playback[go.m_Name]=any(objects[r['m_PathID']].read_typetree()['m_WrapMode']==1 for r in root['_storiesDirector'])
        cache.put_json(playback_key,playback)
    for scene in scenes:scene['loop']=playback[scene['name']]
    if any('order' not in scene for scene in scenes):
        env=UnityPy.load(str(BUNDLE))
        pointers={Path(path).stem:ptr for path,ptr in env.container.items() if '/Reenactment/' in path}
        for scene in scenes:
            source=Source(env,pointers[scene['name']],config)
            order=sorted(source.groups,key=lambda g:min((r[0] for r in source.sprites if r[2]==g),default=-1000))
            scene['order']=[source.nodes[g]['name'] for g in order]
        cache.put_json(source_key,cached)
    def load(key):return Image.open(cache.path(key,'.png')).convert('RGBA')
    samples=[]
    for scene in scenes:
        for key in scene['frames'][0].values():
            rgba=np.asarray(load(key));samples.extend(rgba[::4,::4,:3][rgba[::4,::4,3]>=96].tolist())
    trained=Image.fromarray(np.array(samples,dtype='uint8').reshape(1,-1,3)).quantize(colors=15,method=Image.Quantize.MEDIANCUT)
    raw=trained.getpalette()[:45];colors=[(0,0,0)]+[tuple(min(248,(v+4)//8*8) for v in raw[i:i+3]) for i in range(0,45,3)]
    curtain_im=load(cached['curtain']);trained=curtain_im.convert('RGB').quantize(colors=15,method=Image.Quantize.MEDIANCUT)
    raw=trained.getpalette()[:45];curtain_colors=[(0,0,0)]+[tuple(min(248,(v+4)//8*8) for v in raw[i:i+3]) for i in range(0,45,3)]
    pool=[bytes(32)];lookup={pool[0]:0};blob=bytearray();intern={};part_sets={};lines=['// Generated from original reenactment references.','#include "reenact_data.h"','namespace manosaba {']
    def add(raw):
        if raw in intern:return intern[raw]
        blob.extend(bytes((-len(blob))%4));offset=len(blob);blob.extend(raw);return offset
    def tile_ids(pixels):
        data=tiles(Image.fromarray(pixels,'P'),4);result=[]
        for offset in range(0,len(data),32):
            tile=data[offset:offset+32]
            if tile not in lookup:lookup[tile]=len(pool);pool.append(tile)
            result.append(lookup[tile])
        return result
    def u16(values):
        raw=struct.pack('<'+'H'*len(values),*values)
        if raw not in intern:intern[raw]=add(raw)
        return intern[raw]
    def pointer(offset,kind='uint16_t'):return f'reinterpret_cast<const {kind}*>(reenact_bytes+{offset})'
    stats=[]
    for sid,scene in enumerate(scenes):
        names=[n for n in scene['order'] if n in FIGURES or n.startswith('1-1-')]
        moving=names.index(scene['moving']) if scene['moving'] else 255
        bg_rows=[];pose_rows=[];frame_stats=[]
        for fi,frame in enumerate(scene['frames']):
            background=Image.new('RGBA',(240,160),(8,8,8,255))
            for n in scene['order']:
                if n not in names:background.alpha_composite(load(frame[n]))
            bg=indices(background,colors,False);bg_offset=u16(tile_ids(bg));bg_rows.append(bg_offset)
            parts=[];nonempty=0
            expected=Image.fromarray(bg,'P');expected.putpalette(sum((list(c) for c in colors),[])+[0]*720);expected=expected.convert('RGB')
            for group,name in enumerate(names):
                layer=indices(load(frame[name]),colors)
                for x,y,w,h,pixels in sparse_parts(layer):
                    ids=tile_ids(pixels);assert all(ids),'Empty tile included in OBJ rectangle'
                    offset=u16(ids);parts.append('{'+f'{pointer(offset)},{x},{y},{w},{h},{group}'+'}');nonempty+=len(ids)
                pal=Image.fromarray(layer,'P');pal.putpalette(sum((list(c) for c in colors),[])+[0]*720)
                expected.paste(pal.convert('RGB'),(0,0),Image.fromarray((layer!=0).astype('uint8')*255))
            assert len(parts)<=128,(scene['name'],len(parts))
            signature=tuple(parts)
            if signature not in part_sets:
                part_sets[signature]=f'reenact_parts_{sid}_{fi}'
                lines.append(f'const reenact_part {part_sets[signature]}[]={{'+','.join(parts or ['{}'])+'};')
            pose_rows.append('{'+f'{part_sets[signature]},{len(parts)},{pointer(bg_offset)}'+'}')
            frame_stats.append(dict(parts=len(parts),tiles=nonempty,bytes=nonempty*32))
            expected.save(generated_path(f'design/reenact/{scene["name"]}-{fi}.png'))
        lines.append(f'const reenact_pose reenact_poses_{sid}[]={{'+','.join(pose_rows)+'};')
        moves=scene['moves'] or [[0,0]];move_offset=add(b''.join(struct.pack('<bb',*v) for v in moves))
        scene['move_offset']=move_offset;scene['moving_index']=moving
        stats.append(dict(name=scene['name'],poses=frame_stats,duration=scene['duration'],moving=scene['moving'],moves=len(moves),loop=scene['loop']))
    curtain=indices(curtain_im,curtain_colors,False);curtain_offset=u16(tile_ids(curtain))
    assert len(pool)<65536
    dictionary=add(b''.join(pool));lines.append(f'const uint8_t* const reenact_tiles=reenact_bytes+{dictionary};')
    lines.append('const uint16_t reenact_palette[16]={'+','.join(str(rgb555(c)) for c in colors)+'};')
    lines.append('const uint16_t reenact_curtain_palette[16]={'+','.join(str(rgb555(c)) for c in curtain_colors)+'};')
    lines.append(f'const uint16_t* const reenact_curtain={pointer(curtain_offset)};')
    lines.append('const reenact_asset reenact_assets[]={'+','.join('{'+f'reenact_poses_{i},{pointer(s["move_offset"],"reenact_move")},{s["duration"]},{len(s["frames"])},{max(1,len(s["moves"]))},{s["moving_index"]},{int(s["loop"])}'+'}' for i,s in enumerate(scenes))+'};\n}')
    write_changed(OUT/'reenact.bin',bytes(blob));write_changed(GBA/'src/reenact_data.cpp','\n'.join(lines)+'\n')
    write_changed(GBA/'src/reenact_bytes.s','// '+digest(bytes(blob))+'\n.section .rodata.reenact_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba13reenact_bytesE\n_ZN8manosaba13reenact_bytesE:\n.incbin "../assets/reenact/reenact.bin"\n')
    report=dict(scene_count=len(scenes),tile_count=len(pool),tile_bytes=len(pool)*32,rom_bytes=len(blob),bpp=4,sparse_empty_tiles=True,scenes=stats)
    write_changed(OUT/'catalog.json',json.dumps(dict(names=[s['name'] for s in scenes]),indent=2)+'\n')
    write_changed(generated_path('reports/reenact.json'),json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print('Reenact:',len(scenes),'scenes,',len(blob),'ROM bytes,',len(pool),'shared tiles',flush=True)


if __name__=='__main__':main()
