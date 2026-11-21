"""Select bounded shared trial poses and map minor expression variants offline."""
from collections import Counter, defaultdict
import json,sys
from pathlib import Path
import numpy as np
from PIL import Image
from graphics_project import GBA,write_changed,digest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'python_workspace'))
from encode_gba_graphics import train_family,encode_actor

EXTRA_BYTES=512*1024
EXTRA_POSES=192


def select(cache, imported, encoded, programs):
    references=[];frequency=Counter()
    for path in sorted((cache.root/'chapters').glob('Act*/*Trial[0-9]*.json')):
        chapter=json.loads(path.read_text())
        for request in chapter['characters']:
            if request['instance']!=request['who']:continue
            key=chapter['character_assets'][str(request['line'])]
            references.append((chapter['path'],request['line'],request['who'],key));frequency[key]+=1
    fingerprint=digest(dict(step='trial-shared-poses-v3-body-match',frequency=sorted(frequency.items()),budget=EXTRA_BYTES,limit=EXTRA_POSES))
    checkpoint=cache.root/'trial-poses.json'
    if checkpoint.exists():
        old=json.loads(checkpoint.read_text())
        if old.get('fingerprint')==fingerprint and set(old['selected'])<=encoded.keys():return old
    base=set()
    for program in programs.values():base.update(cache.get_json(program)['resources'])
    pending=list(base)
    while pending:
        key=pending.pop()
        for child in encoded[key].get('frames',[])+encoded[key].get('obj_frames',[]):
            if child not in base:base.add(child);pending.append(child)
    selected={key for key in frequency if key in base}
    base_blobs={blob for key in base for blob in encoded[key].get('blobs',[])}
    groups=defaultdict(list)
    for asset in imported.values():
        if asset['kind'] in ('character','sprite'):groups[asset['who']].append(asset)
    wanted=defaultdict(list)
    for key in frequency:wanted[imported[key]['who']].append(key)
    families={};added=[];extra_blobs=set();extra_bytes=0
    bodies={}
    def body(key):
        if key not in bodies:
            image=Image.open(cache.path(imported[key]['png'],'.png')).convert('RGBA').resize((20,40),Image.Resampling.BOX)
            pixels=np.asarray(image,dtype=np.float32)/255
            pixels[:,:,:3]*=pixels[:,:,3:4]
            bodies[key]=pixels[12:28].reshape(-1)
        return bodies[key]
    candidates=[]
    for who,keys in sorted(wanted.items()):
        keys=sorted(keys,key=lambda key:(-frequency[key],key))
        # Every form gets a representative before adding extra expressions.
        if not any(key in selected for key in keys):candidates.append((True,keys[0]))
    candidates.extend((False,key) for key in sorted(frequency,key=lambda key:(-frequency[key],key)) if key not in selected)
    for required,key in candidates:
        if key in selected:continue
        if not required and len(added)>=EXTRA_POSES:break
        asset=imported[key];who=asset['who']
        same=[chosen for chosen in selected if imported[chosen]['who']==who]
        if not required and same and min(float(np.square(body(chosen)-body(key)).mean()) for chosen in same)<.0015:continue
        if who not in families:families[who]=train_family(groups[who],cache)
        result=encoded.get(key) or encode_actor(asset,families[who],cache)
        new_blobs=set(result['blobs'])-base_blobs-extra_blobs
        cost=sum(((cache.path(blob,'.bin').stat().st_size+3)&~3)+4 for blob in new_blobs)+result['rom_directory_bytes']
        if extra_bytes+cost>EXTRA_BYTES:
            if required:raise ValueError('Trial pose budget cannot fit form '+who)
            continue
        encoded[key]=result;selected.add(key);added.append(key);extra_blobs.update(new_blobs);extra_bytes+=cost
        print('trial shared pose',who,len(added),'extra bytes',extra_bytes,flush=True)
    vectors={}
    def vector(key):
        if key not in vectors:
            image=Image.open(cache.path(imported[key]['png'],'.png')).convert('RGBA').resize((20,40),Image.Resampling.BOX)
            pixels=np.asarray(image,dtype=np.float32)/255
            pixels[:,:,:3]*=pixels[:,:,3:4]
            # Visible upper-body shape matters more than offscreen skirt/legs.
            weights=np.ones((40,1,1),dtype=np.float32);weights[:12]=.35;weights[28:]=.15
            vectors[key]=(pixels*weights).reshape(-1)
        return vectors[key]
    by_who={who:sorted(key for key in selected if imported[key]['who']==who) for who in wanted}
    matrices={who:np.array([vector(key) for key in keys]) for who,keys in by_who.items()}
    mapping={}
    for who,keys in wanted.items():
        for key in keys:
            if key in selected:mapping[key]=key
            else:mapping[key]=by_who[who][int(np.square(matrices[who]-vector(key)).sum(axis=1).argmin())]
    report=dict(version=1,fingerprint=fingerprint,policy='shared exact poses; nearest visible upper-body/expression combination for remaining variants',
        source_combinations=len(frequency),selected=sorted(selected),added=added,extra_bytes=extra_bytes,extra_budget=EXTRA_BYTES,
        exact_combinations=sum(mapping[key]==key for key in frequency),exact_events=sum(frequency[key] for key in frequency if mapping[key]==key),
        events=len(references),mapping=mapping,
        references=[dict(path=path,line=line,who=who,source=key,selected=mapping[key],exact=mapping[key]==key) for path,line,who,key in references])
    write_changed(cache.root/'trial-poses.json',json.dumps(report,ensure_ascii=False,sort_keys=True)+'\n')
    return report
