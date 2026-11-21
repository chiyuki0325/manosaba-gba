"""Bake the original localized debate titles into shared tiles and sparse changes."""
from build_paths import generated_path
import csv
import io
import json
import math
import struct
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
import UnityPy
from graphics_project import ROOT, GBA, ContentCache, digest, write_changed
from trial_cutins import sample_track

BUNDLE = ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle'
SCALE = 160/1440


def extract(cache):
    key = digest(dict(step='debate-title-source-v2-timeup', source=cache.file_key(BUNDLE)))
    data = cache.get_json(key)
    if data:
        return data
    env = UnityPy.load(str(BUNDLE))
    data = dict(prefabs={}, objects={}, clips={})
    def visit(ptr):
        go = ptr.read()
        node = dict(name=go.m_Name, active=go.m_IsActive, components=[], children=[])
        for component in go.m_Component:
            obj = component.component
            node['components'].append(dict(id=obj.path_id, type=obj.type.name, data=obj.read_typetree()))
            if obj.type.name in ('Transform', 'RectTransform'):
                node['children'] = [visit(child.read().m_GameObject) for child in obj.read().m_Children]
        return node
    for name, ptr in env.container.items():
        if Path(name).stem in ('BeginDebateText', 'EndDebateText', 'TimeIsUpText'):
            data['prefabs'][Path(name).stem] = visit(ptr)
    for obj in env.objects:
        if obj.type.name == 'AnimationClip':
            data['clips'][str(obj.path_id)] = obj.read_typetree()
        elif obj.type.name == 'MonoBehaviour':
            tree = obj.read_typetree()
            if any(k in tree for k in ('m_Clips', 'm_Tracks', 'm_Clip')):
                data['objects'][str(obj.path_id)] = tree
    cache.put_json(key, data)
    return data


def walk(node):
    yield node
    for child in node['children']:
        yield from walk(child)


def setup(data, kind):
    prefab = data['prefabs'][('BeginDebateText','EndDebateText','TimeIsUpText')[kind]]
    root = prefab['children'][0]
    director = next(c['data'] for n in walk(prefab) for c in n['components'] if c['type']=='PlayableDirector')
    timeline = data['objects'][str(director['m_PlayableAsset']['m_PathID'])]
    tracks = [data['objects'][str(ref['m_PathID'])] for ref in timeline['m_Tracks']]
    duration = max(c['m_Start']+c['m_Duration'] for t in tracks for c in t.get('m_Clips', []))
    bindings = []
    for track in tracks:
        target = next(b['value']['m_PathID'] for b in director['m_SceneBindings'] if b['key']['m_PathID'] in
                      [ident for ident, obj in ((int(i), o) for i,o in data['objects'].items()) if obj is track])
        node = next(n for n in walk(prefab) if any(c['id']==target for c in n['components']))
        bindings.append((node, track))
    sprites = {int(r['path_id']): ROOT/'original_sprites'/r['output'] for r in csv.DictReader((ROOT/'original_sprites/manifest.csv').open()) if r['type']=='Sprite'}
    states = {}
    def visit(node, path=''):
        tr = next(c['data'] for c in node['components'] if c['type']=='RectTransform')
        image = next((c['data'] for c in node['components'] if c['type']=='MonoBehaviour' and 'm_Sprite' in c['data']), None)
        group = next((c['data']['m_Alpha'] for c in node['components'] if c['type']=='CanvasGroup'), 1)
        color = image['m_Color'].copy() if image else dict(r=1,g=1,b=1,a=1)
        state = dict(node=node, pos=tr['m_AnchoredPosition'].copy(), size=tr['m_SizeDelta'],
                     scale=tr['m_LocalScale'].copy(), rotation=0., alpha=group, color=color, active=node['active'])
        if image:
            source = sprites[image['m_Sprite']['m_PathID']]
            # Localization changes only the four title sprites, retaining native layout.
            if kind<2 and path.startswith('TextContainer/') and node['name'].split('_')[-1] in ('01','02','03','04','W'):
                number = int(node['name'].split('_')[1])
                style = 'White' if node['name'].endswith('_W') else 'Normal'
                source = ROOT/f'manosaba-web/public/UI_Trial/Debate/Debate_Text_{("Begin","End")[kind]}_{number}_{style}@ZhHans.png'
            state['image'] = Image.open(source).convert('RGBA')
        states[id(node)] = state
        for child in node['children']:
            visit(child, (path+'/' if path else '')+child['name'])
    visit(root)
    path_maps = {}
    for node, _ in bindings:
        paths = {0: states[id(node)]}
        def visit_paths(n, path=''):
            for child in n['children']:
                p = (path+'/' if path else '')+child['name']
                paths[zlib.crc32(p.encode())] = states[id(child)]
                visit_paths(child,p)
        visit_paths(node)
        path_maps[id(node)] = paths
    return root, states, bindings, path_maps, duration


ATTRS = {zlib.crc32(name.encode()): name for name in (
    'm_AnchoredPosition.x', 'm_AnchoredPosition.y', 'm_Alpha', 'm_Color.r', 'm_Color.g', 'm_Color.b', 'm_Color.a', 'm_IsActive')}


def render(data, setup_data, time):
    root, states, tracks, path_maps, _ = setup_data
    for node, track in tracks:
        sampled, _ = sample_track(data, track, time)
        if not sampled:
            continue
        sample, bindings = sampled
        for binding, index, count in bindings:
            state = path_maps[id(node)][binding['path']]
            attribute = binding['attribute']
            if binding['typeID']==4:
                values = [sample(index+i,time) for i in range(count)]
                if attribute==3:
                    state['scale'] = dict(zip(('x','y','z'),values))
                elif attribute==4:
                    state['rotation'] = values[2]
                elif attribute==2:
                    x,y,z,w=values
                    state['rotation']=math.degrees(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z)))
            elif attribute in ATTRS:
                attr=ATTRS[attribute];value=sample(index,time)
                if attr.startswith('m_AnchoredPosition.'):
                    state['pos'][attr[-1]]=value
                elif attr.startswith('m_Color.'):
                    state['color'][attr[-1]]=value
                elif attr=='m_Alpha':state['alpha']=value
                elif attr=='m_IsActive':state['active']=value>.5
    canvas=Image.new('RGBA',(240,160))
    def paint(node, parent, alpha):
        state=states[id(node)]
        if not state['active'] or node['name']=='Trail_BG':return
        angle=math.radians(state['rotation']);sx=state['scale']['x'];sy=state['scale']['y']
        matrix=parent@np.array([[math.cos(angle)*sx,-math.sin(angle)*sy,state['pos']['x']],
                               [math.sin(angle)*sx,math.cos(angle)*sy,state['pos']['y']],[0,0,1]])
        alpha*=state['alpha']
        if 'image' in state and alpha*state['color']['a']>0.001 and abs(np.linalg.det(matrix))>1e-7:
            source=state['image'];size=state['size']
            pixel_to_local=np.array([[size['x']/source.width,0,-size['x']/2],
                                     [0,-size['y']/source.height,size['y']/2],[0,0,1]])
            screen=np.array([[SCALE,0,120],[0,-SCALE,80],[0,0,1]])@matrix@pixel_to_local
            inv=np.linalg.inv(screen)
            rgba=np.asarray(source).copy().astype(np.float32)
            rgba*=np.array([state['color'][c] for c in ('r','g','b')]+[state['color']['a']*alpha])
            tinted=Image.fromarray(np.rint(rgba).clip(0,255).astype(np.uint8))
            warped=tinted.transform((240,160),Image.Transform.AFFINE,tuple(inv[:2].flatten()),Image.Resampling.BILINEAR)
            canvas.alpha_composite(warped)
        for child in node['children']:paint(child,matrix,alpha)
    paint(root,np.eye(3),1)
    return canvas


def main():
    cache=ContentCache();data=extract(cache);sequences=[];durations=[]
    for kind in range(3):
        stage=setup(data,kind);duration=stage[-1];durations.append(duration)
        key=digest(dict(step='debate-title-render-v5-no-paper',source=cache.file_key(BUNDLE),kind=kind,
                        art=[cache.file_key(p) for p in sorted((ROOT/'manosaba-web/public/UI_Trial/Debate').glob('*ZhHans.png'))]))
        encoded=cache.get_json(key)
        if encoded is None:
            frames=[]
            for frame in range(math.ceil(duration*60)):
                image=render(data,stage,frame/60)
                frames.append(cache.put_blob(image.tobytes(),'.rgba'))
            encoded=dict(frames=frames);cache.put_json(key,encoded)
        sequences.append([Image.frombytes('RGBA',(240,160),cache.path(k,'.rgba').read_bytes()) for k in encoded['frames'][:round(duration*60)]])
    # Keep the original poses; compress the decorative lead and the long hold.
    # The lettering is visible within the first second instead of after 2.2s.
    source_durations=list(durations)
    source_frames=np.interp(np.arange(120),[0,18,48,90,120],[0,100,150,200,260]).astype(int)
    sequences[0]=[sequences[0][frame] for frame in source_frames]
    durations[0]=len(sequences[0])/60
    # Give the foreground equal training weight so the paper cannot consume the palette.
    training=[]
    for sequence in sequences[:2]:
        for image in sequence[::15]:
            rgba=np.asarray(image);training.append(rgba[:,:,:3][rgba[:,:,:3].max(axis=2)>100])
    for sequence in sequences[:2]:
        rgba=np.asarray(sequence[len(sequence)//2]);training.append(rgba[:,:,:3][rgba[:,:,3]>0][::8])
    rgb=np.concatenate(training);palette_image=Image.fromarray(rgb.reshape(-1,1,3)).quantize(colors=15)
    palette=np.asarray(palette_image.getpalette()[:45],dtype=np.uint8).reshape(15,3)
    timeup_rgb=[]
    for image in sequences[2][::10]:
        rgba=np.asarray(image);timeup_rgb.append(rgba[:,:,:3][rgba[:,:,3]>0])
    timeup_image=Image.fromarray(np.concatenate(timeup_rgb).reshape(-1,1,3)).quantize(colors=15)
    timeup_palette=np.asarray(timeup_image.getpalette()[:45],dtype=np.uint8).reshape(15,3)
    tile_ids={bytes(32):0};tiles=[bytes(32)];streams=[];stats=[]
    for kind,sequence in enumerate(sequences):
        previous=np.zeros(600,dtype=np.uint16);frames=[];maximum=0
        for image in sequence:
            rgba=np.asarray(image)
            colors=timeup_palette if kind==2 else palette
            distance=((rgba[:,:,:3].astype(np.int32)[:,:,None,:]-colors.astype(np.int32))**2).sum(axis=3)
            pixels=distance.argmin(axis=2).astype(np.uint8)+1;pixels[rgba[:,:,3]<128]=0
            raw=(pixels[:,::2]|pixels[:,1::2]<<4).reshape(20,8,30,4).transpose(0,2,1,3).reshape(600,32)
            refs=[]
            for tile in raw:
                blob=tile.tobytes()
                if blob not in tile_ids:tile_ids[blob]=len(tiles);tiles.append(blob)
                refs.append(tile_ids[blob])
            refs=np.asarray(refs,dtype=np.uint16);changed=np.flatnonzero(refs!=previous)
            frames.append([(int(i),int(refs[i])) for i in changed]);previous=refs
            if len(frames)>1:maximum=max(maximum,len(changed))
        stats.append(dict(kind=('begin','end','timeup')[kind],frames=len(frames),duration=durations[kind],
                          source_duration=source_durations[kind],max_changed_tiles=maximum))
        streams.append(frames)
    assert len(tiles)<65536
    assert max(row['max_changed_tiles'] for row in stats)<=128, 'Title exceeds the 4KiB VBlank budget'
    blob=bytearray(b''.join(tiles));lines=['// Generated from original localized title art and Timeline curves.','#include "trial_title_data.h"','namespace manosaba {']
    def add(raw):
        blob.extend(bytes((-len(blob))%4));offset=len(blob);blob.extend(raw);return offset
    palette_data=[0]+[int(r)//8|(int(g)//8<<5)|(int(b)//8<<10) for r,g,b in palette]
    lines.append('const uint16_t trial_title_palette[16]={'+','.join(map(str,palette_data))+'};')
    timeup_colors=[0]+[int(r)//8|(int(g)//8<<5)|(int(b)//8<<10) for r,g,b in timeup_palette]
    lines.append('const uint16_t trial_timeup_palette[16]={'+','.join(map(str,timeup_colors))+'};')
    for kind,frames in enumerate(streams):
        rows=[]
        refs=np.zeros(600,dtype='<u2');snapshots=[]
        for index,frame in enumerate(frames):
            offset=add(b''.join(struct.pack('<HH',*change) for change in frame))
            rows.append('{'+f'reinterpret_cast<const uint16_t*>(trial_title_bytes+{offset}),{len(frame)}'+'}')
            for tile,ref in frame:refs[tile]=ref
            if index%30==0:snapshots.append(add(refs.tobytes()))
        lines.append(f'const trial_title_frame title_frames_{kind}[]={{'+','.join(rows)+'};')
        lines.append(f'const uint16_t* const title_snapshots_{kind}[]={{'+','.join(f'reinterpret_cast<const uint16_t*>(trial_title_bytes+{o})' for o in snapshots)+'};')
    lines.append('const trial_title_sequence trial_titles[3]={'+','.join('{'+f'title_frames_{k},title_snapshots_{k},{len(frames)}'+'}' for k,frames in enumerate(streams))+'};\n}')
    target=GBA/'assets/trial';target.mkdir(exist_ok=True)
    write_changed(target/'titles.bin',bytes(blob));write_changed(GBA/'src/trial_title_data.cpp','\n'.join(lines)+'\n')
    write_changed(GBA/'src/trial_title_bytes.s','// '+digest(bytes(blob))+'\n.section .rodata.trial_title_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba17trial_title_bytesE\n_ZN8manosaba17trial_title_bytesE:\n.incbin "../assets/trial/titles.bin"\n')
    report=dict(tiles=len(tiles),tile_bytes=len(tiles)*32,rom_bytes=len(blob),sequences=stats,paper_layer=False,
                source='BeginDebateText / EndDebateText localized ZhHans; original TimeIsUpText')
    write_changed(generated_path('reports/trial-titles.json'),json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
    for kind,sequence in enumerate(sequences):
        sequence[len(sequence)//2].save(generated_path(f'design/trial/title-{kind}-source.png'))


if __name__=='__main__':main()
