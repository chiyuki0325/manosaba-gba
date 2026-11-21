"""Compile source yaw motion and chapter seating into small shared-camera records."""
from collections import Counter
from functools import lru_cache
from pathlib import Path
import ast,json,math,operator,re

GBA=Path(__file__).resolve().parents[1]
SOURCE=GBA.parent/'story/zh-Hans'
ACTORS=['Ema','Hiro','Sherry','AnAn','Coco','Hanna','Noah','Leia','Miria','Meruru',
        'Margo','Nanoka','Alisa','Yuki','Warden','Jailer','CreatureHiro']
PAN_WIDTH=2048
OPS={ast.Add:operator.add,ast.Sub:operator.sub,ast.Mult:operator.mul,ast.Div:operator.truediv}


def number(expression,variables):
    if isinstance(expression,dict):expression=expression['expression']
    if isinstance(expression,(list,int,float)):return expression
    node=ast.parse(expression.replace('{','').replace('}','').strip(),mode='eval').body
    def read(n):
        if isinstance(n,ast.Constant):return n.value
        if isinstance(n,ast.Name):return variables[n.id]
        if isinstance(n,(ast.Tuple,ast.List)):return [read(v) for v in n.elts]
        if isinstance(n,ast.BinOp):return OPS[type(n.op)](read(n.left),read(n.right))
        if isinstance(n,ast.UnaryOp):return -read(n.operand) if isinstance(n.op,ast.USub) else read(n.operand)
        raise ValueError(ast.dump(n))
    return read(node)


@lru_cache(maxsize=1)
def catalog():
    layouts=[];chapters={}
    for path in sorted(SOURCE.glob('Act*/*TrialInit.json')):
        variables={}
        for e in json.loads(path.read_text())['events']:
            if e['op']=='SetCustomVariable' and isinstance(e['params']['Expression'],str):
                key,expr=e['params']['Expression'].split('=',1)
                try:variables[key.strip()]=number(expr,variables)
                except (KeyError,ValueError,SyntaxError):pass
        count=variables['courtStandCount'];seats=[255]*count
        for i,who in enumerate(ACTORS):
            seat=variables.get(who.lower()+'Idx',-1)
            if seat>=0:seats[seat]=i
        chapter=path.parent.name;chapters[chapter]=(len(layouts),variables)
        layouts.append(dict(chapter=chapter,stage=0 if count==13 else 1,seats=seats,count=count))
    motions=[];unmapped=[];lookup={}
    for path in sorted(SOURCE.glob('Act*/*Trial[0-9]*.json')):
        doc=json.loads(path.read_text());layout,initial=chapters[path.parent.name];variables=dict(initial)
        count=layouts[layout]['count'];cameras=[None,None];speaker='Ema'
        for e in doc['events']:
            op=e['op'];p=e.get('params',{})
            if op=='PrintTextModified':speaker=p.get('AuthorId',speaker)
            elif op=='SetCustomVariable':
                expr=p['Expression'];expr=expr['expression'] if isinstance(expr,dict) else expr
                if '=' in expr:
                    key,value=expr.split('=',1)
                    try:variables[key.strip()]=number(value,variables)
                    except (KeyError,ValueError,SyntaxError):pass
            elif op=='GosubToModifyPerspectiveStageCamera':
                who=p.get('LookCharacterId',speaker);seat=variables.get(str(who).lower()+'Idx',-1)
                if seat>=0:
                    offset={'Center':0,'Left':.1,'Right':-.1}[p.get('Composition','Center')]
                    cameras[p.get('Index',0)]=(seat+offset)*360/count
            elif op=='ModifyPerspectiveStageCamera':
                index=p.get('Index',0);duration=p.get('Duration',0);before=cameras[index]
                try:rotation=number(p['Rotation'],variables) if 'Rotation' in p else None
                except (KeyError,ValueError,SyntaxError):rotation=None
                yaw=rotation[1] if rotation is not None else None
                if duration>0:
                    changed=set(p)-{'Index','Duration','EaseName','Wait'}
                    kind='yaw' if changed=={'Rotation'} else 'orbit' if changed=={'Height','Rotation'} and duration>=20 else None
                    reason=None
                    if kind is None:reason='perspective_or_vertical'
                    elif before is None or yaw is None:reason='unresolved_yaw'
                    elif p.get('EaseName','Linear') not in ('Linear','OutQuad'):reason='unsupported_ease'
                    if reason:
                        unmapped.append(dict(path=doc['path'],line=e['line'],reason=reason,params=p))
                    else:
                        # Unity's ordinary rotation tween uses the nearest turn.
                        delta=(yaw-before+180)%360-180
                        start=round(904+before/360*PAN_WIDTH);end=round(904+(before+delta)/360*PAN_WIDTH)
                        lookup[(doc['path'],e['line'])]=len(motions)
                        motions.append(dict(path=doc['path'],line=e['line'],kind=kind,layout=layout,
                            start=start,end=end,frames=round(duration*60),ease=int(p.get('EaseName')=='OutQuad'),wait=int(bool(p.get('Wait',0)))))
                if yaw is not None:cameras[index]=yaw
    return dict(layouts=layouts,motions=motions,unmapped=unmapped,lookup=lookup,
                counts=dict(mapped=len(motions),unmapped=len(unmapped),kinds=Counter(m['kind'] for m in motions)))
