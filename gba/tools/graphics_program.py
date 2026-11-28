"""Compile chapter-local timeline programs referencing shared immutable assets."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'python_workspace'))
from graphics_project import digest
from export_gba_stage import compile_stage


def presentation_events(events, path=''):
    """Adapt authored shots to GBA planes without adding VM event anchors."""
    out=[]
    court_cutins=path=='Act01_Chapter03/Act01_Chapter03_Adv27'
    for i,event in enumerate(events):
        p=event.get('params',{})
        if court_cutins:
            if event['op'] in ('SpawnExtended','DestroySpawnedExtended') and p.get('Path')=='MaskingRectangle':
                event=dict(event,params=dict(p,SubId='Front'))
            elif event['op']=='HideActors' and i+1<len(events):
                following=events[i+1]
                if following['op']=='DestroySpawnedExtended' and following['params'].get('Path')=='MaskingRectangle':
                    # Without its window the GBA would draw this cut-in across the main shot.
                    event=dict(event,params=dict(p,ActorIds=[*p['ActorIds'],'MainBackground-Front']))
        rotation=p.get('Rotation',[0,0,0])
        flight=(event['op']=='ModifyCharacterExtended' and
                p.get('IdAndAppearance','').split('.',1)[0]=='Warden' and
                len(rotation)>=3 and abs(float(rotation[2]))>=45)
        if flight:event=dict(event,params=dict(p,Visible=0))
        out.append(event)
    return out


def trial_picture_chapter(chapter):
    """Only 2D picture actors; court poses stay in the court renderer."""
    events=[]
    picture_ops={'ModifyBackgroundExtended','HideActors','ShowActors','HideAllActors','HideAllCharacters',
                 'ModifyCamera','AnimateActorExtended','SpawnBlur','ChangeActorMaskability',
                 'PrintTextModified','ResetText','HidePrinter'}
    for e in chapter['events']:
        op=e['op'];p=e.get('params',{})
        if op=='ModifyCamera':events.append(dict(e,params=dict(p,Duration=0)))
        elif op=='PrintDebateText':events.append(dict(e,op='HidePrinter'))
        elif op=='ModifyCharacterExtended':
            if p.get('SubId'):events.append(e)
        elif op=='ModifyBackgroundExtended':
            if p.get('SubId')!='Overlay':events.append(e)
        elif op in picture_ops:events.append(e)
        elif op=='DestroySpawnedExtended' and p.get('Path')=='Blur':events.append(e)
    lines={e['line'] for e in events if e['op']=='ModifyCharacterExtended'}
    return dict(chapter,events=events,characters=[r for r in chapter['characters'] if r['line'] in lines])


def compile_program(chapter, assets, cache, mask_assets=None):
    if '_Trial' in chapter['path']:chapter=trial_picture_chapter(chapter)
    refs=[]; indexes={}
    def ref(key):
        if key not in indexes: indexes[key]=len(refs);refs.append(key)
        return indexes[key]
    def solid(color,width=240,height=160,kind='solid'):
        value=dict(kind=kind,color=color,size=[width,height]);key=digest(value)
        assets[key]=dict(value,id=key);cache.put_json(key,assets[key])
        return ref(key)
    white=solid('#ffffff');black=solid('#000000')
    mapping={i:ref(chapter['character_assets'][str(r['line'])]) for i,r in enumerate(chapter['characters'])}
    backgrounds={(r['namespace'],r['appearance']):ref(chapter['background_assets'][str(r['line'])]) for r in chapter['backgrounds']}
    rectangles={}
    for event in chapter['events']:
        p=event.get('params',{})
        if event['op']=='SpawnExtended' and p.get('Path')=='MaskingRectangle':
            params=p['Params']
            rectangles[event['line']]=solid(params[4] if len(params)>4 else '#000000',round(float(params[0])*2.4),round(float(params[1])*1.6),'rectangle')
    key=digest(dict(step='stage-program-v1',source=chapter['source_sha256'],resources=refs,adapter=cache.file_key(Path(__file__)),
                    compiler=cache.file_key(Path(__file__).resolve().parents[2]/'python_workspace/export_gba_stage.py'),masks=mask_assets or {}))
    result=cache.get_json(key)
    if result is None:
        result=compile_stage(presentation_events(chapter['events'],chapter['path']),{v:v for v in mapping.values()},backgrounds,rectangles,white,black,mapping,script_path=chapter['path'],instant_actor_hide='_Trial' in chapter['path'])
        mask_names=('AngleGradient','HorizontalRects','VerticalBlindConcurrently','Diamonds60','VerticalBlindGradually','HorizontalLinearGradient','Diamonds15','VerticalRects')
        result.update(id=key,path=chapter['path'],resources=refs,mask_resources={str(mask_names.index(name)):mask_assets[name] for name in chapter['masks']} if mask_assets else {})
        cache.put_json(key,result)
    return result
