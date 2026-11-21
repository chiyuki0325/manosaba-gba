"""Extract original cut-in prefab/timeline metadata, retaining content-addressed intermediates."""
from build_paths import generated_path
from pathlib import Path
import json
import UnityPy
from graphics_project import ROOT,GBA,ContentCache,digest,write_changed

BUNDLE=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle'


def extract(cache=None):
    cache=cache or ContentCache();key=digest(dict(step='trial-cutin-source-v2',source=cache.file_key(BUNDLE)))
    data=cache.get_json(key)
    if data:return data
    env=UnityPy.load(str(BUNDLE));data=dict(prefabs={},objects={},clips={})
    def visit(ptr):
        go=ptr.read();result=dict(name=go.m_Name,active=go.m_IsActive,components=[],children=[])
        for c in go.m_Component:
            obj=c.component;tree=obj.read_typetree();result['components'].append(dict(id=obj.path_id,type=obj.type.name,data=tree))
            if obj.type.name=='Transform':
                for child in obj.read().m_Children:result['children'].append(visit(child.read().m_GameObject))
        return result
    for name,ptr in env.container.items():
        if 'ObjectionCutIn' in name:data['prefabs'][Path(name).stem]=visit(ptr)
    for obj in env.objects:
        if obj.type.name=='AnimationClip':data['clips'][str(obj.path_id)]=obj.read_typetree()
        elif obj.type.name=='MonoBehaviour':
            t=obj.read_typetree()
            if any(k in t for k in ('m_Clips','m_Tracks','m_RootTracks','m_Clip','m_PostPlaybackState','m_ControlledParticleSystems')):
                data['objects'][str(obj.path_id)]=t
    cache.put_json(key,data)
    return data


# Unity's streamed clip stores (time, key count, curve index + cubic coefficients).
def clip_curves(clip):
    import struct,math
    raw=clip['m_MuscleClip']['m_Clip']['data'];stream=raw['m_StreamedClip'];words=stream['data'];curves={};at=0
    as_float=lambda value:struct.unpack('<f',struct.pack('<I',value))[0]
    while at<len(words):
        time=as_float(words[at]);count=words[at+1];at+=2
        for _ in range(count):
            index=words[at];coeff=[as_float(v) for v in words[at+1:at+5]];at+=5
            curves.setdefault(index,[]).append((time,coeff))
    constants=raw.get('m_ConstantClip',{}).get('data',[]);dense=raw['m_DenseClip']
    def sample(index,time):
        if index in curves:
            keys=curves[index];chosen=keys[0]
            for key in keys:
                if key[0]>time:break
                chosen=key
            dt=time-chosen[0] if math.isfinite(chosen[0]) else 0;a,b,c,d=chosen[1]
            return ((a*dt+b)*dt+c)*dt+d
        dense_index=index-stream['curveCount']
        if 0<=dense_index<dense['m_CurveCount']:
            frame=min(max(0,round((time-dense['m_BeginTime'])*dense['m_SampleRate'])),dense['m_FrameCount']-1)
            return dense['m_SampleArray'][frame*dense['m_CurveCount']+dense_index]
        return constants[dense_index-dense['m_CurveCount']]
    bindings=[];index=0
    for binding in clip['m_ClipBindingConstant']['genericBindings']:
        count=3 if binding['typeID']==4 and binding['attribute'] in (1,3,4) else 4 if binding['typeID']==4 and binding['attribute']==2 else 1
        bindings.append((binding,index,count));index+=count
    return sample,bindings


def timeline_tracks(data,root):
    director=next(c['data'] for c in root['children'][0]['components'] if c['type']=='PlayableDirector')
    asset=data['objects'][str(director['m_PlayableAsset']['m_PathID'])];tracks=[]
    def visit(ref):
        track=data['objects'][str(ref['m_PathID'])];tracks.append((ref['m_PathID'],track))
        for child in track.get('m_Children',[]):visit(child)
    for ref in asset['m_Tracks']:visit(ref)
    return director,tracks


def animation_track(data,root,node_name):
    director,tracks=timeline_tracks(data,root);node=next(n for n in root['children'] if n['name']==node_name)
    animator=next(c['id'] for c in node['components'] if c['type']=='Animator')
    tracks=dict(tracks)
    key=next(b['key']['m_PathID'] for b in director['m_SceneBindings'] if b['value']['m_PathID']==animator and b['key']['m_PathID'] in tracks)
    return tracks[key]


def sample_track(data,track,time):
    clip=data['clips'].get(str(track.get('m_InfiniteClip',{}).get('m_PathID')))
    if clip:return clip_curves(clip),track.get('m_InfiniteClipOffsetPosition',dict(x=0,y=0,z=0))
    for segment in track.get('m_Clips',[]):
        if segment['m_Start']<=time<segment['m_Start']+segment['m_Duration']:
            playable=data['objects'].get(str(segment['m_Asset']['m_PathID']),{})
            clip=data['clips'].get(str(playable.get('m_Clip',{}).get('m_PathID')))
            if clip:
                sampler,bindings=clip_curves(clip);local=(time-segment['m_Start'])*segment.get('m_TimeScale',1)+segment.get('m_ClipIn',0)
                return (lambda index,_:sampler(index,local),bindings),dict(x=0,y=0,z=0)
    return None,dict(x=0,y=0,z=0)


def build():
    import csv,io,math,struct,zlib
    import numpy as np
    from PIL import Image,ImageFilter
    from court_render import cropped_pieces
    from encode_gba_graphics import train_family,encode_actor
    cache=ContentCache();data=extract(cache)
    sprites={int(r['path_id']):ROOT/'original_sprites'/r['output'] for r in csv.DictReader((ROOT/'original_sprites/manifest.csv').open()) if r['type']=='Sprite'}
    def find(node,name):
        if node['name']==name:return node
        for child in node['children']:
            result=find(child,name)
            if result:return result
    def sprite(node):return next(c['data']['m_Sprite']['m_PathID'] for c in node['components'] if c['type']=='SpriteRenderer')
    source_rows=[];reports=[]
    for kind in ('Ema','Hiro','CreatureHiro'):
        root=data['prefabs']['ObjectionCutIn_'+kind];hero='Ema' if kind=='Ema' else 'Hiro'
        library=next(c['data']['_objectLibrary'] for c in root['components'] if c['type']=='MonoBehaviour' and '_objectLibrary' in c['data'])
        node_ids={next(c['data']['m_GameObject']['m_PathID'] for c in n['components']):n for n in find(root,hero)['children']}
        poses=[]
        for group in library:
            node=node_ids[group['Objects'][0]['m_PathID']];im=Image.open(sprites[sprite(node)]).convert('RGBA');im.thumbnail((128,176),Image.Resampling.LANCZOS)
            canvas=Image.new('RGBA',((im.width+7)//8*8,(im.height+7)//8*8));canvas.alpha_composite(im)
            buf=io.BytesIO();canvas.save(buf,format='PNG');png=cache.put_blob(buf.getvalue(),'.png')
            poses.append(dict(id=digest([kind,node['name'],png]),png=png,kind='sprite',bbox=list(canvas.getbbox())))
        family=train_family(poses,cache);family['anchor_y']=88
        encoded=[encode_actor(asset,family,cache) for asset in poses]
        panel=find(root,'RefuteCutIn_StainedGlass_001');glass=Image.open(sprites[sprite(panel)]).convert('RGBA');glass.thumbnail((288,132),Image.Resampling.LANCZOS)
        glass_canvas=Image.new('RGBA',(256,160));glass_canvas.alpha_composite(glass,((256-glass.width)//2,(160-glass.height)//2))
        buf=io.BytesIO();glass_canvas.save(buf,format='PNG');glass_png=cache.put_blob(buf.getvalue(),'.png')
        glass_source=dict(id=digest([kind,'overlay-glass',glass_png]),png=glass_png,kind='sprite',bbox=list(glass_canvas.getbbox()))
        fragments=[]
        glass_root=find(root,'Glass2')
        for n in range(1,4):
            node=find(glass_root,f'GlassFragment_{n:03}')
            im=Image.open(sprites[sprite(node)]).convert('RGBA');im.thumbnail((32,32),Image.Resampling.LANCZOS)
            canvas=Image.new('RGBA',(32,32));canvas.alpha_composite(im,((32-im.width)//2,(32-im.height)//2))
            rgba=np.asarray(canvas).copy();mask=rgba[:,:,3]>=128
            inner=np.asarray(canvas.getchannel('A').filter(ImageFilter.MinFilter(3)))>=128
            edge=mask & ~inner
            rgba[edge,:3]=np.rint(rgba[edge,:3]*.35+np.array([224,240,248])*.65).astype(np.uint8)
            canvas=Image.fromarray(rgba);buf=io.BytesIO();canvas.save(buf,format='PNG');png=cache.put_blob(buf.getvalue(),'.png')
            fragments.append(dict(id=digest([kind,'shard',n,png]),png=png,kind='sprite',bbox=list(canvas.getbbox())))
        shard_family=train_family([glass_source,*fragments],cache);shard_family['anchor_y']=16
        glass_asset=encode_actor(glass_source,shard_family,cache);glass_asset['pivot']=[128,80]
        # Keep the original burst as tiny particles in one 32x32 OBJ per texture.
        shards=[]
        for fragment in fragments:
            image=Image.open(cache.path(fragment['png'],'.png')).convert('RGBA');rgba=np.asarray(image);mask=rgba[:,:,3]>=128
            choices=[]
            for bank,colors in enumerate(shard_family['colors']):
                distance=((rgba[:,:,:3].astype(np.int32)[:,:,None,:]-np.asarray(colors,dtype=np.int32)[None,None,:,:])**2).sum(axis=3)
                choices.append((int(distance.min(axis=2)[mask].sum()),bank,distance.argmin(axis=2).astype(np.uint8)+1))
            _,bank,pixels=min(choices,key=lambda v:v[0]);pixels[~mask]=0;refs=[]
            for ty in range(4):
                for tx in range(4):
                    tile=pixels[ty*8:ty*8+8,tx*8:tx*8+8];refs.append(cache.put_blob((tile[:,0::2]|(tile[:,1::2]<<4)).tobytes()))
            shards.append(dict(width=32,height=32,pivot=[16,16],palette=shard_family['blob'],parts=[dict(x=0,y=0,width=32,height=32,bank=bank,tiles=refs)]))
        hero_track=animation_track(data,root,hero);director,tracks=timeline_tracks(data,root)
        duration=max(c['m_Start']+c['m_Duration'] for _,t in tracks for c in t.get('m_Clips',[]));count=math.ceil(duration*60)+1
        white=animation_track(data,root,'White');frames=[]
        for frame in range(count):
            time=frame/60;track,offset=sample_track(data,hero_track,time);sample,bindings=track
            position_index=next(i for b,i,_ in bindings if b['path']==0 and b['typeID']==4 and b['attribute']==1)
            opacity_index=next(i for b,i,_ in bindings if b['typeID']==212 and b['attribute']==zlib.crc32(b'm_Color.a'))
            x=(sample(position_index,time)+offset['x'])*160/14.4
            alpha=max(0,min(16,round(sample(opacity_index,time)*16)))
            wt,_=sample_track(data,white,time);flash=0
            if wt:
                ws,wb=wt
                slots=[i for b,i,_ in wb if b['typeID']==212 and b['attribute']==zlib.crc32(b'm_Color.a')]
                if slots:flash=max(0,min(16,round(ws(slots[0],time)*16)))
            assert abs(x)<512,(kind,frame,x)
            frames.append((round(x),alpha,flash))
        source_rows.append((encoded,shards,glass_asset,frames));reports.append(dict(kind=kind,frames=count,source_duration=duration,hero_tile_bytes=max(a['tile_bytes'] for a in encoded),glass_tile_bytes=glass_asset['tile_bytes'],shard_tile_bytes=1536,overlay_obj_count=max(len(a['parts']) for a in encoded)+len(glass_asset['parts'])+3,variant_nodes=[node_ids[g['Objects'][0]['m_PathID']]['name'] for g in library]))
    blob=bytearray();lines=['// Generated from original cut-in art and Timeline streams.','#include "trial_cutin_data.h"','namespace manosaba {'];shared={}
    def add(raw):blob.extend(bytes((-len(blob))%4));offset=len(blob);blob.extend(raw);return offset
    def ptr(offset,kind='uint8_t'):return f'trial_cutin_bytes+{offset}' if kind=='uint8_t' else f'reinterpret_cast<const {kind}*>(trial_cutin_bytes+{offset})'
    def blob_ref(key):
        if key not in shared:shared[key]=add(cache.path(key,'.bin').read_bytes())
        return shared[key]
    def asset_definition(asset,name):
        parts=[]
        for i,p in enumerate(asset['parts']):
            array=f'{name}_p{i}';lines.append(f'const uint32_t {array}[]={{'+','.join(str(blob_ref(t)) for t in p['tiles'])+'};')
            parts.append('{'+f'{array},{p["x"]},{p["y"]},{p["width"]},{p["height"]},{p["bank"]}'+'}')
        lines.append(f'const graphic_part {name}_parts[]={{'+','.join(parts)+'};')
        return '{'+f'{name}_parts,nullptr,{ptr(blob_ref(asset["palette"]),"uint16_t")},nullptr,{asset["width"]},{asset["height"]},{len(parts)},0,0,{asset["pivot"][0]},{asset["pivot"][1]},graphic_kind::obj,0,0'+'}'
    rows=[]
    for k,(poses,shards,glass_asset,frames) in enumerate(source_rows):
        definitions=[];positions=[]
        for i,a in enumerate(poses):
            definitions.append(asset_definition(a,f'hero_{k}_{i}'));bottom=np.full(a['width'],a['height'],dtype='<i2');p=dict(x=128,y=103,scale=256,bottom=bottom);pieces=cropped_pieces(a,p,3,cache)
            name=f'hero_{k}_{i}_pieces';lines.append(f'const trial_actor_piece {name}[]={{'+','.join('{'+','.join(str(s[n]) for n in ('part','x','y','width','height'))+'}' for s in pieces)+'};')
            positions.append('{'+f'{ptr(add(bottom.tobytes()),"int16_t")},{name},128,103,256,{len(pieces)}'+'}')
        lines.append(f'const graphic_asset heroes_{k}[]={{'+','.join(definitions)+'};')
        definitions=[asset_definition(a,f'shard_{k}_{i}') for i,a in enumerate(shards)]
        lines.append(f'const graphic_asset shards_{k}[]={{'+','.join(definitions)+'};')
        lines.append(f'const trial_actor_placement positions_{k}[]={{'+','.join(positions)+'};')
        glass_definition=asset_definition(glass_asset,f'glass_{k}')
        lines.append(f'const graphic_asset glass_{k}={glass_definition};')
        bottom=np.full(glass_asset['width'],glass_asset['height'],dtype='<i2');p=dict(x=128,y=80,scale=256,bottom=bottom)
        pieces=cropped_pieces(glass_asset,p,3,cache);name=f'glass_{k}_pieces'
        lines.append(f'const trial_actor_piece {name}[]={{'+','.join('{'+','.join(str(piece[n]) for n in ('part','x','y','width','height'))+'}' for piece in pieces)+'};')
        lines.append(f'const trial_actor_placement glass_position_{k}={{'+f'{ptr(add(bottom.tobytes()),"int16_t")},{name},128,80,256,{len(pieces)}'+'};')
        frame_ptr=ptr(add(b''.join(struct.pack('<hBB',*f) for f in frames)),'trial_cutin_frame')
        rows.append('{'+f'&glass_{k},heroes_{k},shards_{k},positions_{k},&glass_position_{k},{frame_ptr},{len(frames)}'+'}')
    lines.append('const trial_cutin_asset trial_cutins[]={'+','.join(rows)+'};\n}')
    target=GBA/'assets/trial';write_changed(target/'cutins.bin',bytes(blob));write_changed(generated_path('reports/cutins.json'),json.dumps(reports,indent=2)+'\n')
    write_changed(GBA/'src/trial_cutin_data.cpp','\n'.join(lines)+'\n')
    write_changed(GBA/'src/trial_cutin_bytes.s','// '+digest(bytes(blob))+'\n.section .rodata.trial_cutin_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba17trial_cutin_bytesE\n_ZN8manosaba17trial_cutin_bytesE:\n.incbin "../assets/trial/cutins.bin"\n')
    print('cut-in native data:',len(blob),'bytes;',reports,flush=True)

if __name__=='__main__':build()
