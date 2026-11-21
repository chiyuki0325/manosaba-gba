"""Classify existing budget candidates using source dialogue and peak VM states.

Host analysis only. It does not alter the renderer, art or story commands.
"""
from build_paths import generated_path
import collections
import ast
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

GBA=Path(__file__).resolve().parents[1]
WORK=GBA/'build-butano/graphics-budget-scan'
OUT=generated_path('reports/graphics-budget/context-analysis')
ENTITY=struct.Struct('<12i4hB3x')
LABELS={'bird_flight':'典狱长大幅飞掠','bird_other':'典狱长近景／淡入等','guard':'看守构图',
        'cg':'CG／背景借用OBJ','masked':'蒙版／分栏人物特写','three':'三人同屏',
        'pair':'普通双人构图','solo':'单人构图'}


def div(a,b):return int(a/b)
def sine(a):return round(math.sin((a%(360*256)*2048//(360*256))*2*math.pi/2048)*4096)
def cosine(a):return sine(a+90*256)
def doubled(scale,angle,p):
    inv=65536//max(1,scale);c=cosine(-angle);s=sine(-angle)
    pa=c*inv>>12;pb=-s*inv>>12;pc=s*inv>>12;pd=c*inv>>12;det=pa*pd-pb*pc
    if not det:return False
    w=h=32
    if p['width']>p['height']:h=8 if p['width']==4*p['height'] else 16
    if p['height']>p['width']:w=8 if p['height']==4*p['width'] else 16
    xs=(div(-256*h*pb-256*w*pd+256*pb,det),div(-256*h*pb+256*w*pd+256*pb-256*pd,det))
    ys=(div(256*(h*pa+w*pc-pa),det),div(256*(h*pa-w*pc-pa+pc),det))
    return any(v< -w or v>=w for v in xs) or any(v< -h or v>=h for v in ys)


def actor_cost(e,cam,a,mask,regular_identity=False):
    scale=div(e[2]*cam[2],256);angle=e[3]+cam[3]
    c=div(cosine(angle)*scale,4096);s=div(sine(angle)*scale,4096)
    dx=e[0]-120*256-cam[0];dy=e[1]-80*256-cam[1]
    cx=120*256+div((dx*cosine(cam[3])-dy*sine(cam[3]))*cam[2],4096*256)
    cy=80*256+div((dx*sine(cam[3])+dy*cosine(cam[3]))*cam[2],4096*256)
    w,h=a.get('size',[a.get('width',240),a.get('height',160)]);px,py=a.get('pivot',[w//2,h//2])
    cost=[0]*160;count=byte_count=twice_count=0
    identity=scale==256 and angle%(360*256)==0
    for i,p in enumerate(a['parts']):
        if not mask>>i&1:continue
        byte_count+=p['width']*p['height']//2
        x=p['x']+p['width']//2-px;y=p['y']+p['height']//2-py
        twice=doubled(scale,angle,p);twice_count+=twice
        ww=p['width']*(2 if twice else 1);hh=p['height']*(2 if twice else 1)
        x=(cx+x*c-y*s)//256-ww//2
        y=(cy+(p['x']+p['width']//2-px)*s+(p['y']+p['height']//2-py)*c)//256-hh//2
        if x>=240 or x+ww<=0 or y>=160 or y+hh<=0:continue
        count+=1
        cycles=ww-2+min(x,0)//2 if identity and regular_identity else 8+2*ww+min(x,0)
        for row in range(max(0,y),min(160,y+hh)):cost[row]+=cycles
    return dict(cost=cost,objects=count,bytes=byte_count,doubled_parts=twice_count,
                scale=scale/256,scale_q8=scale,angle=angle/256,identity=identity,
                screen_center=[round(cx/256,2),round(cy/256,2)])


def main():
    global WORK,OUT
    variant=next((a.split('=',1)[1] for a in sys.argv[1:] if a.startswith('--variant=')),'baseline')
    snap=variant in ('scale-snap','scale-snap115')
    if snap:
        WORK=GBA/'build-butano/actor-prototype'/variant
        OUT=generated_path('reports/actor-prototype')/variant/'context-analysis'
    OUT.mkdir(parents=True,exist_ok=True)
    linked=json.loads((GBA/'assets/graphics-cache/linked/graphics.json').read_text())
    imported=json.loads((GBA/'assets/graphics-cache/import.json').read_text())['assets']
    keys=sorted(linked['assets']);paths=sorted(linked['programs'])
    report=json.loads(((generated_path('reports/actor-prototype')/variant/'report.json') if snap else generated_path('reports/graphics-budget/report.json')).read_text())
    exe=WORK/'graphics_budget_contexts'
    flags=['-DMANOSABA_ACTOR_SCALE_SNAP'] if snap else []
    if variant=='scale-snap115':flags+=['-DMANOSABA_ACTOR_SCALE_SNAP_115','-DMANOSABA_ACTOR_SILHOUETTE_SHARE']
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra',*flags,'-I'+str(GBA/'src'),
                    str(GBA/'tools/graphics_budget_scan.cpp'),str(GBA/'src/stage_player.cpp'),'-o',str(exe)],check=True)
    dump=WORK/'context-snapshots.bin'
    subprocess.run([str(exe),str(WORK/'input.bin'),str(WORK/'context-samples.csv'),'1',str(dump)],check=True)
    peak={};raw=dump.read_bytes();assert len(raw)%1048==0
    for at in range(0,len(raw),1048):
        h=struct.unpack_from('<16i',raw,at)
        if h[4]:continue
        key=(h[0],h[1])
        if key in peak and peak[key][0][7]>=h[7]:continue
        peak[key]=(h,[ENTITY.unpack_from(raw,at+64+i*60) for i in range(16)],struct.unpack_from('<3Q',raw,at+1024))
    docs={path:json.loads((GBA.parent/'story/zh-Hans'/f'{path}.json').read_text()) for path in {s['path'] for s in report['spans']}}
    classified=[];scale_counts=collections.Counter();asset_variants=collections.defaultdict(set);max_model_error=0
    for span in report['spans']:
        candidates=[v for (bank,event),v in peak.items() if paths[bank]==span['path'] and span['event_start']<=event<=span['event_end']]
        h,states,masks=max(candidates,key=lambda v:v[0][7]);cam=states[11];actors=[]
        for s,image in enumerate(h[10:13]):
            if image<0:continue
            key=keys[image];a=linked['assets'][key];e=states[h[13+s]];role=imported.get(key,{}).get('who','CG')
            result=actor_cost(e,cam,a,masks[s],regular_identity=snap);normal=actor_cost(e,cam,a,masks[s],regular_identity=True)
            active_masks=[]
            for i in range(13,16):
                m=states[i]
                if e[16] and m[12]>=0 and m[4]>0 and div(m[14]+1,20)==div(e[14],20):
                    shape=linked['assets'][keys[m[12]]]
                    active_masks.append(dict(entity=i,opacity=round(m[4]/256,3),size=shape.get('size',[shape.get('width',240),shape.get('height',160)])))
            actors.append(dict(slot=s,entity=h[13+s],image=image,asset_key=key,role=role,opacity=round(e[4]/256,3),
                               stage_position=[e[0]/256,e[1]/256],stage_angle=e[3]/256,
                               masks=active_masks,parts=masks[s].bit_count(),normal_cost=normal['cost'],**result))
            scale_counts[(result['scale_q8'],round(result['angle'],3))]+=1
            if role not in ('Warden','Jailer','CG'):asset_variants[(result['scale_q8'],round(result['angle'],3))].add(image)
        overhead=2*max(0,sum(a['objects'] for a in actors)-1)
        cost=[overhead+sum(a['cost'][y] for a in actors) for y in range(160)]
        max_model_error=max(max_model_error,abs(max(cost)-h[7]));assert abs(max(cost)-h[7])<=4
        regular=[overhead+sum(a['normal_cost'][y] for a in actors) for y in range(160)]
        birds=[a for a in actors if a['role']=='Warden'];flight=any(abs(a['stage_angle'])>=45 for a in birds)
        if flight:category='bird_flight'
        elif birds:category='bird_other'
        elif any(a['role']=='Jailer' for a in actors):category='guard'
        elif any(a['role']=='CG' for a in actors):category='cg'
        elif any(a['masks'] for a in actors):category='masked'
        elif len(actors)>=3:category='three'
        elif len(actors)==2:category='pair'
        else:category='solo'
        others=[a for a in actors if a['role']!='Warden'];other_overhead=2*max(0,sum(a['objects'] for a in others)-1)
        no_bird=max(other_overhead+sum(a['cost'][y] for a in others) for y in range(160))
        source=docs[span['path']]['events'];by_line={e['line']:e for e in source}
        flagged=[e for e in report['events'] if e['path']==span['path'] and span['event_start']<=e['event']<=span['event_end']]
        dialogue=[by_line[e['line']] for e in flagged if by_line[e['line']]['op']=='PrintTextModified']
        upcoming=next((e for e in source if e['line']>=h[2] and e['op']=='PrintTextModified'),None)
        context=[e for e in source if h[2]-14<=e['line']<=h[2]+5]
        for a in actors:
            a['cycles_at_peak_row']=a['cost'][h[9]];a['above_dialogue_peak']=max(a['cost'][:104])
            del a['cost'];del a['normal_cost']
        classified.append(dict(**span,category=category,peak_line=h[2],peak_offset_frames=h[5],
                               peak_cycles=h[7],peak_y=h[9],peak_above_dialogue=max(cost[:104]),actors=actors,
                               continues_into_dialogue=bool(dialogue),flagged_dialogue_count=len(dialogue),
                               quote=upcoming['params']['Text'] if upcoming else '',
                               first_flagged_dialogue=dialogue[0] if dialogue else None,
                               source_context=context,identity_normal_obj_peak_estimate=max(regular),
                               remove_warden_peak_estimate=no_bird))
    categories=collections.Counter(s['category'] for s in classified)
    summary=dict(spans=len(classified),categories={LABELS[k]:v for k,v in categories.items()},
                 continues_into_dialogue=sum(s['continues_into_dialogue'] for s in classified),
                 transition_events_only=sum(not s['continues_into_dialogue'] for s in classified),
                 tilted_peak_spans=sum(any(abs(a['angle'])>.5 for a in s['actors']) for s in classified),
                 identity_peak_spans=sum(any(a['identity'] for a in s['actors']) for s in classified),
                 model_cost_reconstruction_error=max_model_error)
    output=dict(summary=summary,basis=dict(input_sha256=hashlib.sha256((WORK/'input.bin').read_bytes()).hexdigest(),
                                         rom_sha256=hashlib.sha256((GBA/'manosaba.gba').read_bytes()).hexdigest()),
                scope='One representative peak per contiguous span; source context and offline geometry, supplemented by selected real ROM comparisons.',
                limitations=['Categories describe the representative peak, not every frame within a span.',
                             'Counterfactual costs apply only to these snapshots and are not full-route pass guarantees.',
                             'A scanline overflow can discard only transparent/covered pixels; native pixel comparison determines visible damage.'],
                common_scales=[dict(scale_q8=k[0],angle=k[1],instances=n) for k,n in scale_counts.most_common()],
                common_variants=[dict(scale_q8=k[0],angle=k[1],unique_assets=len(v)) for k,v in sorted(asset_variants.items(),key=lambda i:-len(i[1]))],spans=classified)
    (OUT/'contexts.json').write_text(json.dumps(output,ensure_ascii=False,indent=2)+'\n')
    tree=ast.parse((GBA/'tools/story.py').read_text())
    names=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and
               any(isinstance(t,ast.Name) and t.id=='SPEAKERS' for t in n.targets))
    md=['# 按实际场景分类的候选列表','',
        '每段以扫描线需求最高的代表快照分类；同一段的其他帧可能包含不同人物与构图。原剧本行号对应源事件的line字段。',
        '台词列表示相邻剧情上下文，预算超限不等于可见像素一定丢失；原生对照见本目录README。','',
        '| 章节／原行号 | 类别 | 峰值人物／图层 | 延续到台词 | 峰值周期 | 相邻剧情台词 |',
        '|---|---|---|---|---:|---|']
    with (OUT/'contexts.csv').open('w') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['path','source_lines','category','roles','continues_into_dialogue','peak_cycles','peak_y','vram_bytes','quote'])
        for s in classified:w.writerow([s['path'],f'{s["line_start"]}-{s["line_end"]}',LABELS[s['category']],'+'.join(a['role'] for a in s['actors']),s['continues_into_dialogue'],s['peak_cycles'],s['peak_y'],s['vram_bytes'],re.sub(r'<br>\s*','／',s['quote'])])
    for s in classified:
        roles='＋'.join(names.get(a['role'],a['role']) for a in s['actors'])
        quote=re.sub(r'<[^>]*>','',s['quote']).replace('\n','／').replace('|','／')[:75]
        md.append(f'| {s["path"]}：{s["line_start"]}–{s["line_end"]} | {LABELS[s["category"]]} | {roles} | {"是" if s["continues_into_dialogue"] else "仅过渡事件"} | {s["peak_cycles"]} | {quote} |')
    (OUT/'spans.md').write_text('\n'.join(md)+'\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
