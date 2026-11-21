#!/usr/bin/env python3
"""Import every chapter into a shared, incremental graphics workspace."""
from __future__ import annotations
import argparse
import collections
import csv
import gc
import io
import json
import re
import sys
import time
from pathlib import Path
from types import SimpleNamespace
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'gba/tools'),str(ROOT/'character_composition_demo')]
from graphics_project import ContentCache, canonical, config, digest, source_paths, write_changed
import compose

SCHEMA=2
LAYERED=set('Ema Sherry Hiro Leia Hanna Coco Meruru Noah Miria Alisa AnAn Margo Nanoka'.split())


def actor_spec(value):
    # A command may consist of just "Alisa,ArmR3,..." without an appearance dot.
    who,separator,appearance=value.partition('.')
    if ',' in who:
        who,extra=who.split(',',1)
        appearance=extra+('.'+appearance if separator else '')
    return who,appearance


def plan(path, cache):
    doc=json.loads(path.read_text()); key=digest(dict(schema=SCHEMA,source=cache.file_key(path)))
    old=cache.get_json(key)
    if old is not None: return old
    actors={}; requests=[]; backgrounds=[]; masks=set(); dynamics=[]
    for event in doc['events']:
        op,p=event['op'],event.get('params',{})
        if op=='ModifyCharacterExtended':
            value=p.get('IdAndAppearance',p.get('Id',''))
            if not isinstance(value,str): dynamics.append(dict(line=event['line'],op=op,value=value));continue
            who,appearance=actor_spec(value)
            instance=who+('-'+p['SubId'] if p.get('SubId') else '')
            history=actors.get(instance,())+((appearance,) if appearance else ())
            actors[instance]=history
            requests.append(dict(line=event['line'],who=who,instance=instance,history=list(history)))
        elif op=='ModifyBackgroundExtended':
            value=p.get('AppearanceAndTransition','')
            if isinstance(value,str):
                appearance=value.split('.')[0]
                if appearance and appearance not in ('Transparent','SolidColor'):
                    backgrounds.append(dict(line=event['line'],namespace=p.get('Id','MainBackground'),appearance=appearance))
            else: dynamics.append(dict(line=event['line'],op=op,value=value))
            if p.get('DissolveTexturePath'): masks.add(p['DissolveTexturePath'])
    result=dict(schema=SCHEMA,path=doc['path'],source_sha256=cache.file_key(path),
                playable=bool(re.search(r'_(Adv|Bad)\d+$',path.stem) or re.fullmatch(r'CommonBad\d+',path.stem)),events=doc['events'],
                characters=requests,backgrounds=backgrounds,masks=sorted(masks),dynamic_references=dynamics)
    cache.put_json(key,result)
    return result


def png_object(cache,image,metadata):
    data=io.BytesIO(); image.save(data,format='PNG')
    blob=cache.put_blob(data.getvalue(),'.png')
    return dict(metadata,png=blob,size=list(image.size))


def character_rules(who, cache, renderer):
    bundle=compose.BUNDLE_DIR/(who.lower()+'.bundle')
    dependencies=dict(who=who,bundle=cache.file_key(bundle),renderer=renderer)
    key=digest(dict(step='character-rules',**dependencies))
    rules=cache.get_json(key); scene=None
    if rules is None:
        cache.stats['unity_scenes']+=1
        scene=compose.load_scene(who.lower())
        layers=compose.collect_layers(scene[1],scene[4],scene[5],scene[6],scene[7])
        rules=dict(default=scene[2].defaultAppearance or '',compositions={x.Key:x.Composition for x in scene[2].compositionMap},
                   layers=[dict(name=x.name,path=list(x.path),tr_id=x.tr_id,enabled=x.enabled) for x in layers],dependencies=dependencies)
        cache.put_json(key,rules)
    return key,rules,scene


def import_layered(who, requests, cache, renderer):
    rule_id,rules,scene=character_rules(who,cache,renderer)
    imported={}
    defaults=[SimpleNamespace(**dict(x,path=tuple(x['path']))) for x in rules['layers']]
    compose.apply_composition(defaults,rules['default'],rules['compositions'])
    history_states={():tuple(layer.enabled for layer in defaults)}
    for position,request in enumerate(requests):
        history=tuple(request['history'])
        if position and position%250==0: print(f'  {who}: {position}/{len(requests)} references resolved',flush=True)
        alias=digest(dict(step='pose-alias',rules=rule_id,history=request['history']))
        cached=cache.get_json(alias)
        if cached is not None:
            if 'enabled' in cached: history_states[history]=tuple(cached['enabled'])
            imported[request['ref']]=cached;continue
        layers=[SimpleNamespace(**dict(x,path=tuple(x['path']))) for x in rules['layers']]
        prefix=len(history)
        while history[:prefix] not in history_states: prefix-=1
        for layer,value in zip(layers,history_states[history[:prefix]]): layer.enabled=value
        for appearance in history[prefix:]: compose.apply_composition(layers,appearance,rules['compositions'])
        history_states[history]=tuple(layer.enabled for layer in layers)
        enabled={layer.tr_id:layer.enabled for layer in layers}
        key=digest(dict(step='pose',rules=rule_id,enabled=sorted(enabled.items()),ppu=10))
        result=cache.get_json(key)
        if result is None:
            if scene is None:
                cache.stats['unity_scenes']+=1;scene=compose.load_scene(who.lower())
            cache.stats['rendered_poses']+=1
            image,_,bbox=compose.render(who.lower(),'',ppu=10,scene=scene,enabled=enabled,save=False)
            result=png_object(cache,image,dict(id=key,kind='character',who=who,bbox=list(bbox),dependencies=[rule_id]))
            cache.put_json(key,result)
        if 'enabled' not in result:
            result['enabled']=list(history_states[history]); cache.put_json(key,result)
        cache.put_json(alias,result); imported[request['ref']]=result
    del scene
    gc.collect()
    return imported


def import_sprite(who, requests, rows, cache):
    matches=[r for r in rows if r['type']=='Sprite' and r['source'].lower().endswith('/characters/'+who.lower()+'.bundle')]
    if not matches: raise ValueError(f'No sprite adapter/source for {who}')
    by_name={r['name']:r for r in matches}
    def number(row): return [int(p) if p.isdigit() else p for p in re.split(r'(\d+)',row['name'])]
    imported={}
    for request in requests:
        appearance=request['history'][-1].split(',')[0] if request['history'] else ''
        chosen=[by_name[appearance]] if appearance in by_name else sorted([r for r in matches if r['name'].startswith(appearance+'-') or r['name'].startswith(appearance+'_')],key=number)
        if not chosen:
            if appearance in ('','Default','Normal'): chosen=[sorted(matches,key=number)[0]]
            else: raise ValueError(f'{who}: missing appearance {appearance!r}')
        frames=[]
        for row in chosen:
            path=ROOT/'original_sprites'/row['output']
            key=digest(dict(step='sprite-native-v1',source=cache.file_key(path),maximum=[200,256]))
            result=cache.get_json(key)
            if result is None:
                image=Image.open(path).convert('RGBA')
                image.thumbnail((200,256),Image.Resampling.LANCZOS)
                result=png_object(cache,image,dict(id=key,kind='sprite',who=who,bbox=list(image.getbbox()),dependencies=[cache.file_key(path)]))
                cache.put_json(key,result)
            frames.append(result)
        if len(frames)>1:
            sequence=dict(kind='animation',who=who,frames=[f['id'] for f in frames],fps=24)
            sequence['id']=digest(sequence);cache.put_json(sequence['id'],sequence)
            imported[request['ref']]=sequence
        else: imported[request['ref']]=frames[0]
    return imported


def import_background(request, rows, cache):
    group={'MainBackground':'mainbackground','Stills':'stills','Tricks':'tricks'}[request['namespace']]
    suffix=f'/backgrounds/{group}/{request["appearance"].lower()}.bundle'
    matches=[r for r in rows if r['type']=='Sprite' and r['source'].lower().endswith(suffix)]
    if len(matches)!=1: raise ValueError(f'Background {suffix}: expected one sprite, found {len(matches)}')
    path=ROOT/'original_sprites'/matches[0]['output']
    key=digest(dict(step='background-native-v1',source=cache.file_key(path),height=160))
    result=cache.get_json(key)
    if result is None:
        image=Image.open(path).convert('RGBA')
        image=image.resize((max(240,round(image.width*160/image.height)),160),Image.Resampling.LANCZOS)
        result=png_object(cache,image,dict(id=key,kind='background',namespace=request['namespace'],appearance=request['appearance'],dependencies=[cache.file_key(path)]))
        cache.put_json(key,result)
    return result


def import_masks(documents, rows, cache, assets):
    names={name for doc in documents for name in doc['masks']}
    result={}
    for name in sorted(names):
        matches=[r for r in rows if r['type']=='Texture2D' and r['name']==name and 'general-transitions_' in r['source']]
        if len(matches)!=1: raise ValueError(f'Missing transition mask {name}')
        path=ROOT/'original_sprites'/matches[0]['output']
        key=digest(dict(step='mask-source-v1',source=cache.file_key(path),size=[240,160]))
        asset=cache.get_json(key)
        if asset is None:
            image=Image.open(path).convert('L').resize((240,160),Image.Resampling.BILINEAR)
            asset=png_object(cache,image,dict(id=key,kind='mask',name=name,dependencies=[cache.file_key(path)]))
            cache.put_json(key,asset)
        result[name]=key;assets[key]=asset
    return result


def run(selection=None, cache_root=None, plan_only=False):
    started=time.monotonic()
    cache=ContentCache(cache_root); documents=[plan(p,cache) for p in source_paths(selection)]
    paths=[d['path'] for d in documents]
    stats=dict(chapters=len(documents),playable_chapters=sum(d['playable'] for d in documents),
               commands=sum(len(d['events']) for d in documents),dynamic_references=sum(len(d['dynamic_references']) for d in documents))
    print('plan '+json.dumps(stats),flush=True)
    if plan_only:
        stats.update(cache_hits=cache.hits,cache_misses=cache.misses)
        write_changed(cache.root/'plan.json',canonical(dict(stats=stats,chapters=paths)))
        return stats
    rows=list(csv.DictReader((ROOT/'original_sprites/manifest.csv').open()))
    renderer=cache.file_key(Path(compose.__file__))
    people=collections.defaultdict(list)
    for doc in documents:
        for request in doc['characters']:
            people[request['who']].append(dict(request,ref=(doc['path'],request['line'])))
    resolved={}; assets={}; errors=[];optional_errors=[]
    for who,requests in sorted(people.items()):
        print(f'characters {who}: {len(requests)} references',flush=True)
        try:
            results=import_layered(who,requests,cache,renderer) if who in LAYERED else import_sprite(who,requests,rows,cache)
            resolved.update(results)
            for result in results.values():
                assets[result['id']]=result
                for frame in result.get('frames',[]): assets[frame]=cache.get_json(frame)
        except (ValueError,KeyError,SystemExit) as error:
            destination=optional_errors if all(r['ref'][0].startswith('Debug/') for r in requests) else errors
            destination.append(dict(who=who,error=str(error),scripts=sorted({r['ref'][0] for r in requests})))
    backgrounds={}
    for doc in documents:
        refs={}
        for request in doc['backgrounds']:
            key=(request['namespace'],request['appearance'])
            if key not in backgrounds:
                try:
                    backgrounds[key]=import_background(request,rows,cache)
                    if len(backgrounds)%25==0: print(f'backgrounds: {len(backgrounds)} unique sources',flush=True)
                except (ValueError,KeyError) as error:
                    (optional_errors if doc['path'].startswith('Debug/') else errors).append(dict(script=doc['path'],line=request['line'],error=str(error)));continue
            result=backgrounds[key]; assets[result['id']]=result;refs[str(request['line'])]=result['id']
        characters={str(r['line']):resolved[(doc['path'],r['line'])]['id'] for r in doc['characters'] if (doc['path'],r['line']) in resolved}
        output=dict(doc,character_assets=characters,background_assets=refs)
        write_changed(cache.root/'chapters'/(doc['path']+'.json'),canonical(output))
    mask_assets=import_masks(documents,rows,cache,assets)
    from graphics_program import compile_program
    programs={}
    for doc in documents:
        path=cache.root/'chapters'/(doc['path']+'.json')
        chapter=json.loads(path.read_text())
        trial_pictures='_Trial' in doc['path'] and bool(chapter['backgrounds'])
        if not doc['playable'] and not trial_pictures: continue
        try:
            program=compile_program(chapter,assets,cache,mask_assets)
            programs[doc['path']]=program['id']
            chapter['program']=program['id']
            write_changed(path,canonical(chapter))
        except (ValueError,KeyError,AssertionError) as error:
            errors.append(dict(script=doc['path'],step='program',error=str(error)))
    write_changed(cache.root/'programs.json',canonical(programs))
    stats.update(elapsed_seconds=round(time.monotonic()-started,3),unity_scenes_opened=cache.stats['unity_scenes'],rendered_poses=cache.stats['rendered_poses'],programs=len(programs),assets=len(assets),character_assets=sum(a['kind'] in ('character','sprite') for a in assets.values()),
                 background_assets=len(backgrounds),cache_hits=cache.hits,cache_misses=cache.misses,errors=errors,optional_errors=optional_errors,animations=sum(a['kind']=='animation' for a in assets.values()))
    write_changed(cache.root/'import.json',canonical(dict(stats=stats,chapters=paths,assets=assets)))
    return stats


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script',action='append',help='Import selected paths; default imports all chapters')
    parser.add_argument('--cache',type=Path)
    parser.add_argument('--plan-only',action='store_true')
    args=parser.parse_args()
    stats=run(args.script,args.cache,args.plan_only)
    print(json.dumps(stats,ensure_ascii=False,indent=2))
    if stats.get('errors'): raise SystemExit(1)

if __name__=='__main__': main()
