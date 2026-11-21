"""Compile shared chapter graphics into fixed-point native BG/OBJ timelines."""
import collections
import ast
import re
import operator
from pathlib import Path

from stage_schema import CAMERA, OVERLAY, MASK_FIRST, MASK_COUNT, SCENERY_FIRST, SCENERY_COUNT

FPS = 16777216 / 280896
# CameraConfiguration: 1440 reference pixels / 100 pixels per world unit.
# Fit the world viewport to GBA height; camera offsets are not scene percentages.
CAMERA_PIXELS_PER_UNIT = 160 * 100 / 1440
PROPS = ['x','y','scale','angle','opacity','tint','blur','reveal','fill','red','green','blue']
EASES = {'Linear':0,'SmoothStep':1,'EaseInQuad':2,'EaseOutQuad':3,'EaseInOutQuad':4,
         'EaseInCubic':5,'EaseOutCubic':6,'EaseInOutSine':7,'EaseOutBack':8}
KINDS = ['image','tween','wait','clear','order','mask','emote','maskable','printer']


def seconds(value): return max(0,round(float(value)*FPS))
def fixed(value): return round(float(value)*256)

def compile_stage(events,char_textures,backgrounds,rectangles,white,black,mapping,mask_names=('AngleGradient','HorizontalRects','VerticalBlindConcurrently','Diamonds60','VerticalBlindGradually','HorizontalLinearGradient','Diamonds15','VerticalRects'),script_path='',instant_actor_hide=False):
    commands, keys, stages, lines = [], [], [], {}
    states, assigned = {}, {}
    degradation = []
    request = 0
    muted=False
    variables={'g_backgroundDefaultScale':1.,'g_characterDefaultScale':1.,'bgmUnitDuration':60/88,'g_fadeDurationAtScenarioTransition':.35}
    coverage = collections.Counter()
    def cmd(kind, target=0, value=0, first=0, count=0, prop=0, flags=0):
        if not muted: commands.append((value,first,count,KINDS.index(kind),target,prop,flags))
    def tween(target, prop, values, durations, easing='Linear', flags=0, relative=False):
        if not isinstance(values,list): values=[values]
        if not isinstance(durations,list): durations=[durations]
        easings = str(easing).split('|')
        assert all(e in EASES for e in easings), easing
        first = len(keys)
        for i,value in enumerate(values):
            keys.append((fixed(value),seconds(durations[min(i,len(durations)-1)]),EASES[easings[min(i,len(easings)-1)]],int(relative)))
        cmd('tween',target,first=first,count=len(values),prop=PROPS.index(prop),flags=flags)
    def target(key, character):
        nonlocal muted
        if key in assigned: return assigned[key]
        slots = range(3) if character else range(SCENERY_FIRST,SCENERY_FIRST+SCENERY_COUNT)
        used = {v for k,v in assigned.items() if states[k]['visible']}
        slot = next((s for s in slots if s not in used),None)
        if slot is None:
            assert character, ('Scenery plane capacity',event)
            speaker=next((e.get('params',{}).get('AuthorId','') for e in events[ei+1:] if e['op']=='PrintTextModified'),'')
            cast={k for k,v in assigned.items() if v<3 and states[k]['visible']}
            explicit='Miria' if script_path.endswith('_Adv12') and 131<=event['line']<=167 else 'Meruru' if script_path.endswith('_Adv20') and 17<=event['line']<=24 else None
            def priority(actor):
                who=actor.split('-')[0]
                return (who==speaker,who in ('Ema','Hiro'),actor in cast)
            victim=explicit if explicit in cast|{key} else min(cast|{key},key=lambda actor:(priority(actor),actor))
            if victim==key:
                muted=True
                degradation.append(dict(line=event['line'],removed=key,reason='four-to-three'))
                return 0
            slot=assigned[victim]
            degradation.append(dict(line=event['line'],removed=victim,added=key,reason='four-to-three'))
        for old in list(assigned):
            if assigned[old]==slot: del assigned[old]
        assigned[key]=slot
        cmd('clear',first=slot,count=1)
        return slot
    def expression(value):
        ops={ast.Add:operator.add,ast.Sub:operator.sub,ast.Mult:operator.mul,ast.Div:operator.truediv,ast.USub:operator.neg,ast.UAdd:operator.pos}
        def evaluate(node):
            if isinstance(node,ast.Constant): return float(node.value)
            if isinstance(node,ast.Name): return variables[node.id]
            if isinstance(node,ast.BinOp): return ops[type(node.op)](evaluate(node.left),evaluate(node.right))
            if isinstance(node,ast.UnaryOp): return ops[type(node.op)](evaluate(node.operand))
            raise ValueError(f'Unsupported numeric expression: {value}')
        text=value.strip()
        while '{' in text:
            text=re.sub(r'\{([^{}]*)\}',lambda match:format(evaluate(ast.parse(match[1],mode='eval').body),'.9g'),text)
        return evaluate(ast.parse(text,mode='eval').body)

    def numeric(value, default=1):
        if isinstance(value,list): return float(value[0])
        if isinstance(value,dict): return expression(value['expression'])
        return float(value)
    def parts(value): return [numeric(value)] if isinstance(value,dict) else [float(v) for v in str(value).split('|')]
    def timing(p,default=.35):
        return numeric(p.get('Duration',default)),p.get('EasingTypeName','Linear'),128 if p.get('Wait') else 0
    for ei,event in enumerate(events):
        op,p = event['op'],event.get('params',{})
        begin = len(commands)
        key_begin=len(keys)
        muted=False
        if op in ('ModifyCharacterExtended','ModifyBackgroundExtended'):
            character = op=='ModifyCharacterExtended'
            if character:
                who,_,appearance=p['IdAndAppearance'].partition('.')
                if ',' in who:
                    who,extra=who.split(',',1);appearance=extra+('.'+appearance if appearance else '')
                key=who+('-'+p['SubId'] if p.get('SubId') else '')
                texture=char_textures[mapping[request]]; request+=1
            else:
                who=p.get('Id','MainBackground'); key=who+('-'+p['SubId'] if p.get('SubId') else '')
                appearance=p.get('AppearanceAndTransition','').split('.')[0]
                texture=white if appearance=='SolidColor' else black if appearance=='Transparent' else backgrounds.get((who,appearance))
            duration,ease,flags=timing(p)
            old=states.get(key,dict(visible=False,image=None,x=120,y=0 if character else 80,scale=1,tint=1))
            state=dict(old); states[key]=state
            overlay=p.get('SubId')=='Overlay'
            initialize=key not in assigned
            slot=OVERLAY if overlay else target(key,character)
            if overlay:
                texture=white
                showing=appearance!='Transparent'
                cmd('order',slot,100)
                cmd('image',slot,texture)
                # reveal controls a black overlay; the transition mask controls its edge.
                if old['image'] is None: tween(slot,'opacity',0 if showing else 1,0)
                tween(slot,'opacity',1 if showing else 0,duration,ease,flags)
                tween(slot,'x',120,0); tween(slot,'y',80,0)
                state.update(visible=showing,image=texture)
            else:
                if texture is None or appearance=='Transparent': texture=old['image']
                inherited=texture is None
                cmd('image',slot,black if inherited else texture,flags=int(inherited))
                order=(10 if character else 0)+{'middle':20,'front':40,'forefront':60}.get(p.get('SubId','').lower(),0)
                if not character:
                    # Same-layer scenery follows presentation order, not reused slot IDs.
                    siblings=sorted((k for k,s in states.items() if k!=key and k in assigned and
                                     SCENERY_FIRST<=assigned[k]<SCENERY_FIRST+SCENERY_COUNT and
                                     s.get('order',0)//20==order//20),key=lambda k:states[k]['order'])
                    for rank,sibling in enumerate(siblings):
                        value=order+rank
                        if states[sibling]['order']!=value:cmd('order',assigned[sibling],value)
                        states[sibling]['order']=value
                    order+=len(siblings)
                state['order']=order
                cmd('order',slot,order)
                # Pose changes cut directly; only background changes dissolve.
                if not character and texture!=old['image'] and old['visible']:
                    tween(slot,'reveal',0,0); tween(slot,'reveal',1,duration,ease,flags)
                else: tween(slot,'reveal',1,0)
                visible=bool(p.get('Visible',1)) and (character or appearance!='Transparent')
                tween(slot,'opacity',int(visible),duration if old['visible']!=visible else 0,ease,flags)
                position=p.get('ScenePosition',[])
                for i,prop in enumerate(('x','y')):
                    value=state[prop]
                    if len(position)>i:
                        value=position[i]*2.4 if prop=='x' else 30-position[i] if character else (100-position[i])*1.6
                    if len(position)>i or initialize: tween(slot,prop,value,duration if old['visible'] else 0,ease,flags)
                    state[prop]=value
                scale=numeric(p.get('Scale',state['scale']))
                if 'Scale' in p or initialize: tween(slot,'scale',scale,duration if old['visible'] else 0,ease,flags)
                state['scale']=scale
                if p.get('Rotation') is not None:
                    rotation=p['Rotation']; tween(slot,'angle',numeric(rotation[-1:] if isinstance(rotation,list) else rotation),duration,ease,flags)
                state.update(visible=visible,image=texture)
            if 'TintColor' in p:
                value=p['TintColor']
                value={'white':'#ffffff','black':'#000000','red':'#ff0000','transparent':'#00000000'}.get(value,value)
                channels=bytes.fromhex(value.lstrip('#'))
                if len(set(channels[:3]))==1:
                    tint=channels[0]/255
                    for prop in ('red','green','blue'): tween(slot,prop,1,duration,ease,flags)
                else:
                    tint=1
                    for prop,v in zip(('red','green','blue'),channels): tween(slot,prop,v/255,duration,ease,flags)
                tween(slot,'tint',tint,duration,ease,flags); state['tint']=tint
                if len(channels)==4: tween(slot,'opacity',channels[3]/255,duration,ease,flags)
            mask=p.get('DissolveTexturePath')
            if mask:
                name=Path(mask).stem
                assert name in mask_names,(name,event)
                cmd('mask',slot,mask_names.index(name))
        elif op in ('HideAllCharacters','HideAllActors','HideActors','ShowActors'):
            ids = list(states) if op in ('HideAllCharacters','HideAllActors') else p['ActorIds']
            duration,ease,flags=timing(p)
            for key in ids:
                if key not in assigned:
                    if key in states: states[key]['visible']=op=='ShowActors'
                    continue
                slot=assigned[key]
                if op=='HideAllCharacters' and slot>=3: continue
                visible=op=='ShowActors'
                cut=instant_actor_hide and slot<3 and not visible
                tween(slot,'opacity',int(visible),0 if cut else duration,ease,flags)
                if cut and flags:cmd('wait',value=seconds(duration))
                states[key]['visible']=visible
        elif op=='AnimateActorExtended':
            loop=127 if p.get('Loop') else 0
            flags=loop|(128 if p.get('Wait') else 0)
            duration=parts(p.get('Duration','.35'))
            for key in p['ActorIds']:
                if key not in assigned: continue
                slot=assigned[key]; character=slot<3
                for source,prop in [('ScenePositionX','x'),('ScenePositionY','y'),('Scale','scale'),('Rotation','angle'),('Opacity','opacity')]:
                    if source not in p: continue
                    vals=parts(p[source]); relative=bool(p.get('Relative'))
                    if prop=='x': vals=[v*2.4 for v in vals]
                    if prop=='y': vals=[-v*(1 if character else 1.6) if relative else 30-v if character else (100-v)*1.6 for v in vals]
                    easing=p.get('EasingTypeNameForPosX' if prop=='x' else 'EasingTypeNameForPosY' if prop=='y' else 'EasingTypeName',p.get('EasingTypeName','Linear'))
                    tween(slot,prop,vals,duration,easing,flags,relative)
                    states[key][prop]=states[key].get(prop,0)+vals[-1] if relative else vals[-1]
        elif op=='ModifyCamera':
            duration,ease,flags=timing(p)
            if 'Offset' in p:
                for prop,value in zip(('x','y'),p['Offset']):
                    tween(CAMERA,prop,value*CAMERA_PIXELS_PER_UNIT*(1 if prop=='x' else -1),duration,ease,flags)
            if 'Zoom' in p:
                zoom=1/max(.0625,1-p['Zoom'])
                # Apply the portrait closeup downgrade to camera-driven shots
                # too. Keep the authored world-space focus and motion timing.
                if zoom>3 and any(slot<3 and states[key]['visible'] for key,slot in assigned.items()):
                    degradation.append(dict(line=event['line'],reason='camera-closeup',scale=zoom,shown_scale=2))
                    zoom=2
                tween(CAMERA,'scale',zoom,duration,ease,flags)
            rotation=p.get('Roll',p.get('Rotation'))
            if rotation is not None: tween(CAMERA,'angle',numeric(rotation[-1:] if isinstance(rotation,list) else rotation),duration,ease,flags)
        elif op=='SpawnBlur':
            for key in p.get('ActorId','').split(','):
                if key in assigned: tween(assigned[key],'blur',float(p.get('Intensity',1)),numeric(p.get('FadeDuration',.35)))
        elif op=='SpawnExtended' and p.get('Path')=='MaskingRectangle':
            key='MaskingRectangle'+('-'+p['SubId'] if p.get('SubId') else '')
            if key not in assigned:
                active={v for k,v in assigned.items() if states[k]['visible']}
                slot=next(s for s in range(MASK_FIRST,MASK_FIRST+MASK_COUNT) if s not in active)
                for old in list(assigned):
                    if assigned[old]==slot: del assigned[old]
                assigned[key]=slot
                cmd('clear',first=slot,count=1)
            slot=assigned[key]; params=p['Params']
            duration=float(params[3]) if len(params)>3 else .35
            color=params[4] if len(params)>4 else '#000000'
            alpha=int(color[-2:],16)/255 if len(color)==9 else 1
            group=p.get('SubId','').lower()
            order=20 if group=='middle' else 60 if group in ('forefront',) else 40 if group=='front' else 0
            cmd('image',slot,rectangles[event['line']]); cmd('order',slot,order-1); cmd('maskable',slot,0)
            tween(slot,'x',p.get('ScenePosition',[50,50])[0]*2.4,0)
            tween(slot,'y',(100-p.get('ScenePosition',[50,50])[1])*1.6,0)
            tween(slot,'angle',p.get('Rotation',[0,0,0])[-1],0)
            # The clip fades with opacity; a transparent rectangle still clips.
            tween(slot,'opacity',1,duration); tween(slot,'fill',alpha,0)
            states[key]=dict(visible=True,image=rectangles[event['line']],x=p.get('ScenePosition',[50,50])[0]*2.4,
                             y=(100-p.get('ScenePosition',[50,50])[1])*1.6,scale=1,tint=alpha)
        elif op=='DestroySpawnedExtended' and p.get('Path')=='MaskingRectangle':
            key='MaskingRectangle'+('-'+p['SubId'] if p.get('SubId') else '')
            if key in assigned:
                tween(assigned[key],'opacity',0,float(p.get('Params',['.35'])[0]))
                states[key]['visible']=False
        elif op=='DestroySpawnedExtended' and p.get('Path')=='Blur':
            for slot in set(assigned.values()): tween(slot,'blur',0,numeric(p.get('Duration',.35)))
        elif op=='SpawnExtended' and p.get('Path') in ('ShakeBackground','ShakeCamera'):
            params=p.get('Params',[])
            count=int(params[1]) if len(params)>1 and params[1] else 3
            targets=[CAMERA] if p['Path']=='ShakeCamera' else [slot for key,slot in assigned.items() if key.split('-')[0]=='MainBackground']
            for slot in targets:cmd('emote',slot,2,count=seconds(.15*count))
        elif op in ('EmoteJumpDouble','EmoteJump','EmoteShake','EmoteNod','SpawnShake'):
            for key in p.get('ActorIds',[p.get('ActorId','Camera' if op=='SpawnShake' else '')]):
                if key in assigned or key=='Camera':
                    slot=CAMERA if key=='Camera' else assigned[key]
                    kind={'EmoteJump':0,'EmoteJumpDouble':1,'EmoteShake':2,'SpawnShake':2,'EmoteNod':3}[op]
                    speed=float(p.get('Speed',1))
                    cmd('emote',slot,kind,count=seconds((.6 if kind==1 else .35)/speed),flags=127 if p.get('Loop') else 0)
        elif op=='ChangeActorMaskability':
            for key in p.get('ActorIds',[]):
                if key in assigned: cmd('maskable',assigned[key],int(p.get('Enable',1)))
        elif op in ('Wait','WaitForNextFrame'):
            value=p.get('WaitMode','')
            mode=value['expression'] if isinstance(value,dict) else str(value)
            if mode.startswith('i') and '{' in mode: mode='i'+str(expression(mode[1:]))
            elif mode.startswith('{'): mode=str(expression(mode))
            if op=='WaitForNextFrame': cmd('wait',value=1)
            elif mode.startswith('i'): cmd('wait',value=seconds(mode[1:]) if len(mode)>1 else 2147483647,flags=1)
            elif mode: cmd('wait',value=seconds(mode))
            else: cmd('wait',flags=2)
        elif op=='SetCustomVariable' and p.get('Expression','').startswith('bgmUnitDuration ='):
            variables['bgmUnitDuration']=expression(p['Expression'].split('=',1)[1])
        elif op in ('ResetText','HidePrinter'): cmd('printer',value=0)
        elif op=='PrintTextModified':
            speaker=p.get('AuthorId','')
            omitted=[k for k,s in states.items() if s['visible'] and k.split('-')[0]==speaker and k not in assigned]
            if omitted:
                key=omitted[0];slot=target(key,True);s=states[key]
                cmd('image',slot,s['image']);cmd('order',slot,10+{'middle':20,'front':40,'forefront':60}.get(key.partition('-')[2].lower(),0))
                for prop in ('x','y','scale','tint'):tween(slot,prop,s[prop],0)
                tween(slot,'opacity',1,0);tween(slot,'reveal',1,0)
            cast=[k for k,slot in assigned.items() if slot<3 and states[k]['visible']]
            source_count=sum(s['visible'] for k,s in states.items() if k.split('-')[0] in {r.split('-')[0] for r in cast}|{speaker} and s.get('image') is not None)
            four=source_count>3 or any(s['visible'] and k not in assigned and s.get('image') is not None for k,s in states.items() if k.split('-')[0] not in ('MainBackground','Stills','MaskingRectangle'))
            horizontal=all('-' not in key for key in cast)
            for index,key in enumerate(sorted(cast,key=lambda k:states[k]['x'])):
                desired=(48,120,192)[index] if four and horizontal else states[key]['x']
                if states[key].get('adapt_x',states[key]['x'])!=desired:
                    tween(assigned[key],'x',desired,0);states[key]['adapt_x']=desired
            cmd('printer',value=1)
        if muted: del keys[key_begin:]
        if len(commands)>begin:
            lines[event['line']]=len(stages)
            stages.append((begin,len(commands)-begin,event['line']))
            coverage[op]+=1
    assert request==len(mapping)
    return dict(commands=commands,keys=keys,events=stages,event_lines=lines,dropped_actors=degradation,coverage=dict(coverage))
