"""Separate output-box clipping candidates from VRAM/scanline budget reports."""
import csv
import json


def write_report(raw,paths,directory,variant,sha):
    events={}
    for row in csv.DictReader(raw.open()):
        r={k:int(v) for k,v in row.items()};key=r['bank'],r['event']
        if key not in events or r['clipped_parts']>events[key]['clipped_parts']:events[key]=r
    rows=list(events.values());spans=[]
    for r in rows:
        r['path']=paths[r['bank']]
        if spans and spans[-1]['path']==r['path'] and spans[-1]['event_end']+1==r['event']:
            s=spans[-1];s['event_end']=r['event'];s['line_end']=r['line'];s['peak_parts']=max(s['peak_parts'],r['clipped_parts'])
        else:spans.append(dict(path=r['path'],event_start=r['event'],event_end=r['event'],line_start=r['line'],line_end=r['line'],peak_parts=r['clipped_parts']))
    summary=dict(banks_scanned=len(paths),flagged_banks=len({r['bank'] for r in rows}),flagged_events=len(rows),contiguous_spans=len(spans))
    report=dict(summary=summary,variant=variant,input_sha256=sha,
        method='Production VM replay; sample opaque indexed source texels outside native OBJ output boxes on screen; 36/180-frame dwell profiles.',
        limitations=['Host affine LUT/register rounding approximation; validate actual OAM in mGBA.',
                     'Source alpha is used; partial masks, other sprites and backgrounds may cover candidates.',
                     'Counts represent clipped parts, not unique visible pixels; route reachability is not proven.',
                     'Degenerate affine matrices are excluded; this is not a complete small-scale/wraparound audit.'],spans=spans,events=rows)
    (directory/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (directory/'events.csv').open('w') as f:
        fields=['path']+[k for k in rows[0] if k!='path'] if rows else ['path','line','event']
        w=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore',lineterminator='\n');w.writeheader();w.writerows(rows)
    md=['# OBJ显示框裁切候选','',f'扫描{len(paths)}章，{summary["flagged_banks"]}章／{len(rows)}事件／{len(spans)}连续片段存在候选。',
        '使用源图非透明像素，不把全透明角落算作裁切。仍未模拟其他图层遮挡、完整蒙版和原生寄存器全部取整；候选不等于最终可见损坏。与显存／扫描线报告分开计数。','',
        '| 章节 | 原行号 | 峰值裁切部件数 |','|---|---:|---:|']
    md.extend(f'| {s["path"]} | {s["line_start"]}–{s["line_end"]} | {s["peak_parts"]} |' for s in spans)
    (directory/'report.md').write_text('\n'.join(md)+'\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
