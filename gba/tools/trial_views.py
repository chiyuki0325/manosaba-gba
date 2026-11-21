"""Resolve complete original trial camera states before selecting offline view samples."""
from build_paths import generated_path
from collections import Counter
from functools import lru_cache
from pathlib import Path
import json, math
from trial_camera import ACTORS, SOURCE, catalog as horizontal_catalog, number


def rotation(value, variables, before):
    if isinstance(value, dict):
        value = value['expression']
    if isinstance(value, str):
        value = [number(part, variables) if part.strip() else None for part in value.split(',')]
    if isinstance(value, (int, float)):
        value = [value]
    result = list(before)
    for index, part in enumerate(value):
        if part is not None:
            result[index] = float(part)
    return result


def modify(state, params, variables):
    result = dict(state)
    for prop in ('Height', 'Distance'):
        if prop in params:
            result[prop.lower()] = float(number(params[prop], variables))
    if 'Rotation' in params:
        result['rotation'] = rotation(params['Rotation'], variables, state['rotation'])
    return result


def focus(params, variables, count, actor):
    actor = params.get('LookCharacterId', actor)
    actor = next((name for name in ACTORS if name.lower()==actor.lower()),actor)
    seat = variables.get(actor.lower()+'Idx', -1)
    zoom = params.get('ZoomLevel', 1)
    height, distance = {1:(5.2,10), 2:(5.3,11), 3:(5.5,12), 4:(5.6,13)}[zoom]
    composition = params.get('Composition', 'Center')
    offset = {'Center':0, 'Left':.1, 'Right':-.1}[composition]
    roll = {'Left':4, 'Right':-4}.get(params.get('RollDirection'), 0)
    return dict(height=height, distance=distance, rotation=[0,(seat+offset)*360/count,roll]), actor


def canonical(state, count, stage):
    pitch, yaw, roll = state['rotation']
    seat = math.floor(yaw*count/360+.5)
    # Geometry repeats once per seat; preserve the source's off-centre composition.
    return (stage, round(state['distance'],5), round(state['height'],5),
            round(pitch,5), round(yaw-seat*360/count,5), round(roll,5)), seat


def reduced_view(state, count):
    distance=state['distance']
    level=0 if distance<11.5 else 1 if distance<12.5 else 2
    yaw=canonical(state,count,0)[0][4]
    composition=0 if abs(yaw)<.05*360/count else 1 if yaw>0 else 2
    return 4+level*3+composition


def preset_view(params):
    zoom=params.get('ZoomLevel',1)
    level=0 if zoom<3 else zoom-2
    return 4+level*3+{'Center':0,'Left':1,'Right':2}[params.get('Composition','Center')]


@lru_cache(maxsize=1)
def catalog():
    layouts = horizontal_catalog()['layouts']
    chapter_ids = {row['chapter']:index for index,row in enumerate(layouts)}
    initials = {}
    for path in sorted(SOURCE.glob('Act*/*TrialInit.json')):
        variables = {}
        for event in json.loads(path.read_text())['events']:
            if event['op'] == 'SetCustomVariable':
                expression = event['params']['Expression']
                if isinstance(expression,str):
                    key,value = expression.split('=',1)
                    variables[key.strip()] = number(value,variables)
        initials[path.parent.name] = variables
    views, view_ids, references, motions, lookup = [], {}, [], [], {}
    counts = Counter()
    def view(state, layout):
        shape,seat = canonical(state,layouts[layout]['count'],layouts[layout]['stage'])
        if shape not in view_ids:
            view_ids[shape] = len(views)
            views.append(dict(stage=shape[0],distance=shape[1],height=shape[2],pitch=shape[3],yaw=shape[4],roll=shape[5]))
        return dict(view=view_ids[shape],seat=seat,layout=layout)
    for path in sorted(SOURCE.glob('Act*/*Trial[0-9]*.json')):
        doc = json.loads(path.read_text())
        variables = dict(initials[path.parent.name]);layout = chapter_ids[path.parent.name]
        count = layouts[layout]['count'];actor = 'Ema'
        cameras = [dict(height=5.2,distance=10.,rotation=[0.,0.,0.]) for _ in range(2)]
        for event in doc['events']:
            op,params = event['op'],event.get('params',{})
            if op == 'SetCustomVariable':
                expression = params['Expression']
                if isinstance(expression,dict):expression=expression['expression']
                if '=' in expression:
                    key,value=expression.split('=',1)
                    try:variables[key.strip()]=number(value,variables)
                    except (KeyError,ValueError,SyntaxError):pass
            elif op == 'PrintTextModified':
                actor = params.get('AuthorId',actor)
            elif op == 'GosubToModifyPerspectiveStageCamera':
                state,actor = focus(params,variables,count,actor)
                cameras[params.get('Index',0)] = state
                reference = dict(path=doc['path'],line=event['line'],index=params.get('Index',0),actor=ACTORS.index(actor) if actor in ACTORS else 0,**view(state,layout))
                lookup[(doc['path'],event['line'])] = reference
                references.append(reference);counts['presets']+=1
            elif op == 'ModifyPerspectiveStageCamera':
                index = params.get('Index',0);before = cameras[index]
                after = modify(before,params,variables);cameras[index]=after
                reference = dict(path=doc['path'],line=event['line'],index=index,**view(after,layout))
                if params.get('Duration',0)>0:
                    start_yaw,end_yaw = before['rotation'][1],after['rotation'][1]
                    after = dict(after,rotation=list(after['rotation']))
                    after['rotation'][1] = start_yaw+(end_yaw-start_yaw+180)%360-180
                    motion = dict(reference,start=before,end=after,frames=round(params['Duration']*60),ease=params.get('EaseName','Linear'),wait=bool(params.get('Wait',0)))
                    motions.append(motion);reference['motion']=len(motions)-1;counts['motions']+=1
                else:counts['cuts']+=1
                lookup[(doc['path'],event['line'])] = reference;references.append(reference)
    return dict(layouts=layouts,views=views,references=references,motions=motions,lookup=lookup,counts=dict(counts))


if __name__ == '__main__':
    from graphics_project import GBA,write_changed
    result=catalog()
    write_changed(generated_path('reports/trial-views.json'),json.dumps({key:value for key,value in result.items() if key!='lookup'},ensure_ascii=False,indent=2)+'\n')
    print(result['counts'],'unique canonical views:',len(result['views']))
