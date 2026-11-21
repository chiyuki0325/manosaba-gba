"""Shared structural CFG and stage-property interval model for offline CG admission."""
import math,re

def prop(v):return (v,v,v,v,0,False)
S,A,O,R,X,Y,B=0,1,2,3,4,5,6
PROPMAP={2:S,3:A,4:O,7:R,0:X,1:Y,6:B}
def entity(camera=False):return (frozenset(),(prop(256),prop(0),prop(0),prop(256),prop(0 if camera else 120*256),prop(0 if camera else 80*256),prop(0)),0,False)
def initial():return tuple(entity(i==11) for i in range(16))
def join_prop(a,b):return (min(a[0],b[0]),max(a[1],b[1]),min(a[2],b[2]),max(a[3],b[3]),max(a[4],b[4]),a[5] or b[5])
def join(x,y):
 return tuple((a[0]|b[0],tuple(join_prop(p,q) for p,q in zip(a[1],b[1])),a[2] if a[2]==b[2] else -32768,a[3] or b[3]) for a,b in zip(x,y))
def advance(st,frames,finish=False):
 out=[]
 for im,ps,order,mask in st:
  pp=[]
  for p in ps:
   lo,hi,el,eh,rem,loop=p
   if rem and not loop and (finish or rem<=frames):p=(el,eh,el,eh,0,False)
   elif rem and not loop:p=(lo,hi,el,eh,max(0,rem-frames),loop)
   pp.append(p)
  out.append((im,tuple(pp),order,mask))
 return tuple(out)
def apply_event(state,program,event):
 st=list(state);wait=0;finish=False;blocking=0;changes=[];cross=[]
 for value,first,count,kind,target,pr,flags in program['commands'][event[0]:event[0]+event[1]]:
  images,ps,order,mask=st[target];ps=list(ps)
  if kind==0:
   new=program['resources'][value]
   if not flags:st[target]=(frozenset([new]),tuple(ps),order,mask)
   elif not images:st[target]=(frozenset([new]),tuple(ps),order,mask)
   # flags preserve an existing image, not a new fallback background.
   if images and new not in images and not flags:cross.append((target,images,new))
  elif kind in (1,9) and pr in PROPMAP:
   pindex=PROPMAP[pr];old=ps[pindex]
   keys=program['keys'][first:first+count] if kind==1 else [[value,0,0,0]]
   lo,hi=old[:2];endlo,endhi=lo,hi;total=0;envelope=None
   for v,frames,ease,relative in keys:
    tl,th=(endlo+v,endhi+v) if relative else (v,v)
    if frames:
     segment=(min(endlo,tl),max(endhi,th))
     if ease==8:
      extra=math.ceil(max(abs(th-endlo),abs(tl-endhi))*.101)
      segment=(segment[0]-extra,segment[1]+extra)
     envelope=segment if envelope is None else (min(envelope[0],segment[0]),max(envelope[1],segment[1]))
     total+=frames
    endlo,endhi=tl,th
   loop=bool(flags&127) if kind==1 else False
   if envelope is None:lo,hi=endlo,endhi
   else:lo,hi=min(envelope[0],endlo),max(envelope[1],endhi)
   # Bounds, not sampled points; relative loops require runtime confirmation.
   lo,hi,endlo,endhi=[max(-1048576,min(1048576,x)) for x in (lo,hi,endlo,endhi)]
   ps[pindex]=(lo,hi,endlo,endhi,total,loop)
   st[target]=(images,tuple(ps),order,mask)
   if flags&128:blocking=max(blocking,total)
   if pr in (2,3):changes.append(dict(slot=target,property='scale' if pr==2 else 'angle',from_interval=list(old[:2]),keyframes=[dict(value=k[0],frames=k[1],ease=k[2],relative=bool(k[3])) for k in keys],loop=loop))
  elif kind==2:
   if flags&2:finish=True
   elif not flags&1:wait=max(wait,max(0,value))
  elif kind==3:
   for target2 in range(first,first+count):st[target2]=entity(target2==11)
  elif kind==4:st[target]=(images,tuple(ps),value,mask)
  elif kind==5:st[target]=(images,tuple(ps),order,True)
  elif kind==8:pass
 return tuple(st),advance(tuple(st),max(wait,blocking),finish),changes,cross,max(wait,blocking),finish

def cfg(doc):
 es=doc['events'];n=len(es);labels={e['name']:i for i,e in enumerate(es) if e['op']=='label'}
 def end(i):
  j=i+1;indent=es[i].get('indent',0)
  while j<n and es[j].get('indent',0)>indent:j+=1
  return j
 edges={i:[i+1] if i+1<n else [] for i in range(n)}
 notes=[];branches=set();extra_returns=set()
 def dest(s):
  if not isinstance(s,str):return None
  path,sep,label=s.rpartition('.')
  if sep and (not path or path==doc['path']):return labels.get(label)
  return None
 for i,e in enumerate(es):
  op=e['op'];p=e.get('params',{})
  if op in ('BeginIf','BeginIfCurrentLanguage'):
   j=end(i);false=j+1 if j<n and es[j]['op']=='Else' else j
   true=i+1
   expr=p.get('Expression')
   known=None
   if op=='BeginIfCurrentLanguage':known=(p.get('LanguageName')=='zh-Hans') != bool(p.get('Unless',0))
   elif expr=='isDebugDirectJump':known=False
   elif expr=='!enableDebugSkip':known=True
   elif expr in ('true','false'):known=expr=='true'
   edges[i]=[x for x in ([true] if known is True else [false] if known is False else [true,false]) if x<n]
   if known is None:branches.add(i)
  elif op=='Else':edges[i]=[end(i)] if end(i)<n else []
  elif op=='GotoModified':
   d=dest(p.get('Path'))
   edges[i]=[d] if d is not None else []
   if not isinstance(p.get('Path'),str):notes.append(dict(line=e['line'],reason='dynamic_jump_not_resolved'))
  elif op in ('ReturnToTitle','ProcessBadEnd','Return'):edges[i]=[]
  elif op=='PrintDebateText':
   links=re.findall(r'<link="([^"]+)"',p.get('Text',''))
   for s in links:
    d=dest('.'+s)
    if d is not None:edges[i].append(d);extra_returns.add(i+1)
  elif op=='CancelGosub':edges[i]=[]
 # Choice bodies are alternatives, not sequential source lines.
 visited=set()
 for i,e in enumerate(es):
  if e['op']!='AddChoice' or i in visited:continue
  headers=[];j=i
  while j<n and es[j]['op']!='StopForChoice':
   if es[j]['op']=='label':j+=1;continue
   if es[j]['op']!='AddChoice':break
   headers.append(j);visited.add(j);j=end(j)
  if j<n and es[j]['op']=='StopForChoice':
   edges[i]=[h+1 for h in headers if h+1<n];branches.add(i)
   for h in headers:
    k=end(h)
    # The preceding body's last event is followed by the common continuation.
    if k>h+1 and es[k-1]['op'] not in ('GotoModified','Return','ReturnToTitle','ProcessBadEnd','CancelGosub'):edges[k-1]=[j+1] if j+1<n else []
  else:notes.append(dict(line=e['line'],reason='choice_structure_unresolved'))
 for i,e in enumerate(es):
  if e['op']=='CancelGosub':
   # This intentionally keeps every possible return site: no invented path proof.
   edges[i]=sorted(x for x in extra_returns if x<n)
 return edges,branches,notes

