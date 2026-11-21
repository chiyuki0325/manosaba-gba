"""Bound derived image generation to effects and lower-layer scales actually used."""
from collections import defaultdict
from stage_schema import SCENERY_FIRST,SCENERY_COUNT,ENTITY_COUNT
from cg_stage_analysis import initial,apply_event,O,S,B


def resource_usage(programs,assets):
    blur=set();base=defaultdict(set)
    for program in programs:
        state=initial()
        for event in program['events']:
            # Dialogue can advance before a nonblocking fade ends. Include the
            # whole key envelope, and expire tracks only after guaranteed waits.
            envelope,state,*_=apply_event(state,program,event)
            visible=[]
            for index in range(SCENERY_FIRST,SCENERY_FIRST+SCENERY_COUNT):
                item=envelope[index]
                if item[0] and item[1][O][1]>0 and any(assets[key]['kind'] in ('background','solid') for key in item[0]):
                    visible.append(item)
                    if item[1][B][1]>0:blur.update(key for key in item[0] if assets[key]['kind']=='background')
            visible.sort(key=lambda item:item[2])
            for item in visible[:-1]:
                for key in item[0]:
                    if assets[key]['kind']=='background':base[key].update(item[1][S][:4])
    return blur,dict(base)


def scene_proxy_usage(programs,assets):
    """Only persist extra scenery which remains exposed at a source event endpoint."""
    import math
    needed=set()
    fields={0:'x',1:'y',2:'scale',3:'angle',4:'opacity'}
    def initial():return dict(image=None,x=120*256,y=80*256,scale=256,angle=0,opacity=0,order=0,maskable=True)
    for program in programs:
        state=[initial() for _ in range(ENTITY_COUNT)];state[11]['x']=state[11]['y']=0
        def covers(item):
            if item['opacity']<256:return False
            if item['maskable'] and any(m['image'] and assets[m['image']]['kind']=='rectangle' and m['opacity']>0 and (m['order']+1)//20==item['order']//20 for m in state[13:]):return False
            camera=state[11];ca=math.radians(camera['angle']/256);cz=camera['scale']/256
            dx=item['x']/256-120-camera['x']/256;dy=item['y']/256-80-camera['y']/256
            cx=120+(dx*math.cos(ca)-dy*math.sin(ca))*cz;cy=80+(dx*math.sin(ca)+dy*math.cos(ca))*cz
            angle=math.radians(item['angle']/256)+ca;scale=item['scale']/256*cz
            w,h=assets[item['image']]['size']
            return all(abs((x-cx)*math.cos(angle)+(y-cy)*math.sin(angle))<=w*scale/2+.01 and abs(-(x-cx)*math.sin(angle)+(y-cy)*math.cos(angle))<=h*scale/2+.01 for x,y in ((0,0),(0,160),(240,0),(240,160)))
        for event in program['events']:
            for value,first,count,kind,target,prop,flags in program['commands'][event[0]:event[0]+event[1]]:
                if kind==0:
                    if not flags or not state[target]['image']:state[target]['image']=program['resources'][value]
                elif kind==1 and prop in fields:
                    key=program['keys'][first+count-1];field=fields[prop]
                    state[target][field]=key[0]+(state[target][field] if key[3] else 0)
                elif kind==3:
                    for index in range(first,first+count):state[index]=initial()
                elif kind==4:state[target]['order']=value
                elif kind==8:state[target]['maskable']=bool(value)
            visible=sorted((s for s in state[SCENERY_FIRST:SCENERY_FIRST+SCENERY_COUNT] if s['image'] and assets[s['image']]['kind']=='background' and s['opacity']>0),key=lambda s:s['order'])
            if len(visible)>2 and not any(covers(s) for s in visible[-2:]):needed.update(s['image'] for s in visible[:-2])
    return needed
