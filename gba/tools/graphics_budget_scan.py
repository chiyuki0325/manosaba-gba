"""Audit all native story banks without rebuilding or modifying the ROM."""
from build_paths import generated_path
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
from obj_silhouettes import silhouette_groups

GBA=Path(__file__).resolve().parents[1]
OUT=GBA/'build-butano/graphics-budget-scan'
REPORT=generated_path('reports/graphics-budget')
KINDS={'obj':0,'bg':1,'solid':2,'rectangle':3,'mask':4,'animation':5}


def main():
    variant=next((a.split('=',1)[1] for a in sys.argv[1:] if a.startswith('--variant=')),'baseline')
    flags={'baseline':[],'regular-obj':['-DMANOSABA_ACTOR_REGULAR'],'scale-snap':['-DMANOSABA_ACTOR_SCALE_SNAP'],
           'part-crop':['-DMANOSABA_ACTOR_SCALE_SNAP','-DMANOSABA_ACTOR_PART_CROP'],
           'silhouette-share':['-DMANOSABA_ACTOR_SCALE_SNAP','-DMANOSABA_ACTOR_SILHOUETTE_SHARE'],
           'scale-snap115':['-DMANOSABA_ACTOR_SCALE_SNAP','-DMANOSABA_ACTOR_SCALE_SNAP_115','-DMANOSABA_ACTOR_SILHOUETTE_SHARE'],
           'closeup-limit':['-DMANOSABA_ACTOR_SCALE_SNAP','-DMANOSABA_ACTOR_SCALE_SNAP_115','-DMANOSABA_ACTOR_SILHOUETTE_SHARE','-DMANOSABA_CLOSEUP_LIMIT']}[variant]
    footprint='--footprint' in sys.argv
    suffix=variant+('-footprint' if footprint else '')
    if footprint:flags=flags+['-DMANOSABA_AFFINE_FOOTPRINT']
    work_dir=OUT if variant=='baseline' and not footprint else GBA/'build-butano/actor-prototype'/suffix
    report_root=REPORT if variant=='baseline' and not footprint else generated_path('reports/actor-prototype')/suffix
    output=next((a.split('=',1)[1] for a in sys.argv[1:] if a.startswith('--output=')),None)
    if output:work_dir=Path(output)/'work';report_root=Path(output)/'report'
    culled='--before-culling' not in sys.argv[1:]
    report_dir=report_root if culled else report_root/'before'
    start=time.monotonic();work_dir.mkdir(parents=True,exist_ok=True);report_dir.mkdir(parents=True,exist_ok=True)
    cache=GBA/'assets/graphics-cache'
    linked=json.loads((cache/'linked/graphics.json').read_text())
    ids={key:i for i,key in enumerate(sorted(linked['assets']))}
    paths=sorted(linked['programs']);catalog=json.loads((GBA/'assets/graphics/catalog.json').read_text())['chapters']
    assert [c['path'] for c in catalog]==paths
    packed=io.BytesIO()
    def ints(*values):packed.write(struct.pack('<'+'i'*len(values),*values))
    def sequence(values):ints(len(values),*values)
    ints(len(ids))
    payload=(cache/'linked/graphics.bin').read_bytes();offsets={b['id']:b['offset'] for b in linked['blobs']};alpha_tiles={}
    for key in ids:
        a=linked['assets'][key];w,h=a.get('size',[a.get('width',240),a.get('height',160)])
        px,py=a.get('pivot',[w//2,h//2])
        ints(KINDS[a['kind']],w,h,px,py,a.get('fps',0))
        sequence([ids[k] for k in a.get('obj_frames',a.get('frames',[]) if a['kind']=='animation' else [])])
        parts=a.get('parts',[]);ints(len(parts))
        groups=silhouette_groups(parts,payload,offsets,alpha_tiles) if variant in ('silhouette-share','scale-snap115','closeup-limit') else []
        for i,p in enumerate(parts):
            ints(p['x'],p['y'],p['width'],p['height'])
            if groups:ints(groups[i])
            if footprint:
                rows=[0]*p['height'];width=p['width']//8
                for tile,ref in enumerate(p['tiles']):
                    raw_tile=payload[offsets[ref]:offsets[ref]+32];tx=(tile%width)*8;ty=(tile//width)*8
                    for y in range(8):
                        for x in range(8):
                            if (raw_tile[y*4+x//2]>>((x&1)*4))&15:rows[ty+y]|=1<<(tx+x)
                packed.write(struct.pack('<'+'Q'*len(rows),*rows))
    ints(len(paths))
    for path in paths:
        key=linked['programs'][path]
        p=json.loads((cache/'objects'/key[:2]/(key+'.json')).read_text())
        ints(len(p['commands']))
        for row in p['commands']:
            row=list(row)
            if row[3]==0:row[0]=ids[p['resources'][row[0]]]
            ints(*row)
        ints(len(p['keys']))
        for row in p['keys']:ints(*row)
        ints(len(p['events']))
        for row in p['events']:ints(*row)
        sequence(catalog[paths.index(path)]['source_lines'])
    source=work_dir/'input.bin';source.write_bytes(packed.getvalue())
    exe=work_dir/'graphics_budget_scan';raw=work_dir/'samples.csv'
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra',*flags,'-I'+str(GBA/'src'),
                    str(GBA/'tools/graphics_budget_scan.cpp'),str(GBA/'src/stage_player.cpp'),'-o',str(exe)],check=True)
    run=subprocess.run([str(exe),str(source),str(raw),str(int(culled))],capture_output=True,text=True,check=True)
    print(run.stderr,end='');(work_dir/'scan.log').write_text(run.stderr)
    if footprint:
        from graphics_footprint_report import write_report
        write_report(raw,paths,report_dir,variant,hashlib.sha256(packed.getvalue()).hexdigest())
        return
    rows={}
    for row in csv.DictReader(raw.open()):
        row={k:int(v) for k,v in row.items()};key=(row['bank'],row['event'])
        if key not in rows:
            rows[key]=dict(row,reading_profiles=[row['dwell_frames']])
        else:
            dest=rows[key];dest['reading_profiles'].append(row['dwell_frames'])
            if row['scanline_cycles']>dest['scanline_cycles']:
                for k in ('worst_y','peak_offset_frames','actor0','actor1','actor2','dwell_frames'):dest[k]=row[k]
            for k in ('vram_bytes','scanline_cycles','visible_objs'):dest[k]=max(dest[k],row[k])
    for row in rows.values():
        row['path']=paths[row['bank']]
        row['vram_over']=row['vram_bytes']>32768
        row['scanline_over']=row['scanline_cycles']>1210
        row['oam_over']=row['visible_objs']>128
    rows=list(rows.values());chapters=[];spans=[]
    for path in paths:
        found=[r for r in rows if r['path']==path]
        if not found:continue
        chapters.append(dict(path=path,events=len(found),vram_over=any(r['vram_over'] for r in found),
                             scanline_over=any(r['scanline_over'] for r in found),
                             peak_vram_bytes=max(r['vram_bytes'] for r in found),
                             peak_scanline_cycles=max(r['scanline_cycles'] for r in found)))
        for r in found:
            if spans and spans[-1]['path']==path and r['event']==spans[-1]['event_end']+1:
                s=spans[-1];s['event_end']=r['event'];s['line_end']=r['line']
                for k in ('vram_bytes','scanline_cycles'):s[k]=max(s[k],r[k])
                s['vram_over']|=r['vram_over'];s['scanline_over']|=r['scanline_over']
            else:spans.append(dict(path=path,event_start=r['event'],event_end=r['event'],line_start=r['line'],line_end=r['line'],
                                   vram_bytes=r['vram_bytes'],scanline_cycles=r['scanline_cycles'],vram_over=r['vram_over'],scanline_over=r['scanline_over']))
    summary=dict(banks_scanned=len(paths),flagged_banks=len(chapters),flagged_events=len(rows),contiguous_spans=len(spans),
                 vram_banks=sum(c['vram_over'] for c in chapters),scanline_banks=sum(c['scanline_over'] for c in chapters),
                 both_banks=sum(c['vram_over'] and c['scanline_over'] for c in chapters),
                 elapsed_seconds=round(time.monotonic()-start,2))
    report=dict(summary=summary,scope='Native image stages: ADV, Bad, CommonBad and Trial picture overlays; the courtroom uses its own renderer.',
                variant=variant,
                invisible_part_culling=culled,
                method='Production stage_player replay; both 36- and 180-frame reading profiles; every animation frame sampled; adjacent flagged events merged into spans.',
                limits=dict(obj_vram_bytes=32768,obj_scanline_cycles=1210,oam_objects=128),
                limitations=['Offline budget candidates, not a per-scene emulator verdict.',
                             'Affine geometry uses a host approximation of Butano sine LUT/register rounding.',
                             'Source events are replayed linearly; branch reachability and cross-script entry history are not proven.',
                             'Upload latency, deferred allocation and allocator fragmentation are not simulated.',
                             'Transparent pixels and mask-zeroed pixels still cost OBJ cycles; completely covered scenes are culled.',
                             'Per-event peaks can occur on different frames; raw per-profile samples are kept under build-butano/graphics-budget-scan/samples.csv.'],
                input_sha256=hashlib.sha256(packed.getvalue()).hexdigest(),chapters=chapters,spans=spans,events=rows)
    (report_dir/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (report_dir/'events.csv').open('w') as f:
        fields=['path','line','event','vram_over','scanline_over','vram_bytes','scanline_cycles','visible_objs','worst_y','reading_profiles']
        writer=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore',lineterminator='\n');writer.writeheader();writer.writerows(rows)
    md=['# 故事图像预算快速扫描','',
        f'扫描 {len(paths)} 个原生故事图像章节，{len(chapters)} 个章节存在超限候选；连续超限片段 {len(spans)} 段，对应 {len(rows)} 个舞台事件。',
        f'显存超限章节 {summary["vram_banks"]} 个，扫描线超限章节 {summary["scanline_banks"]} 个，两者都有 {summary["both_banks"]} 个。','',
        '**这是离线筛查候选，不等于每个片段都已经在模拟器复现。**覆盖ADV／Bad／CommonBad，审判使用另一套渲染器，不计入本表。',
        '复用游戏stage_player，每个动画帧采样；分别采用36／180帧阅读时长。相邻超限舞台事件合并为一段。',
        ('容量采用游戏新增规则：不预取完全离屏块，不申请被全不透明矩形蒙版完全排除的块。' if culled else '容量采用原16px预取／回收规则。')+
        '扫描线按仿射精灵包围框计费。角度取整、脚本分支、上传延迟和分配器碎片未精确模拟。',
        '原剧本行号见下表；细目见events.csv，完整方法与逐事件数据见report.json。','',
        '| 章节 | 原剧本行号 | 显存峰值B／32768 | 扫描线峰值／1210 | 类型 |','|---|---:|---:|---:|---|']
    for s in spans:
        types='/'.join(t for t,key in [('显存','vram_over'),('扫描线','scanline_over')] if s[key])
        md.append(f'| {s["path"]} | {s["line_start"]}–{s["line_end"]} | {s["vram_bytes"]} | {s["scanline_cycles"]} | {types} |')
    (report_dir/'report.md').write_text('\n'.join(md)+'\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
