"""Court effect commands and direct-entry snapshots, inferred from linked VM flow.

Normal script jumps retain the live VM. These snapshots are only for start().
At joins, retain agreed values; report ambiguous fields instead of choosing a branch.
"""
from collections import deque
import struct

NEUTRAL=(32767,0,0,None)  # RGB, opacity, fire, undamped shake specification.
FIELDS=('color','opacity','fire','shake')


def shake_command(params):
    frames=round(float(params.get('Duration',.35))*60)
    force=params.get('Force',[1,0,0])
    # Single 2D court: at most three pixels; preserve even the .01 sustained tremor.
    amplitude=lambda f: (1 if f>0 else -1)*min(3,max(1,round(abs(f)*6))) if f else 0
    x,y=amplitude(force[0]),-amplitude(force[1])
    period=max(4,min(120,round(frames/max(1,params.get('Frequency',10)))))
    damping=max(0,min(255,round(float(params.get('DampingRatio',1))*255)))
    return bytes([10])+struct.pack('<HbbBBB',frames,x,y,period,damping,int(bool(params.get('Wait'))))


def successors(code,script,pc,ops):
    """Feasible control edges, with debug-direct branches disabled for normal play."""
    op=code[pc];size=int.from_bytes(code[pc+1:pc+3],'little');p=pc+3;end=p+size
    word=lambda at:int.from_bytes(code[at:at+2],'little')
    offset=lambda at:int.from_bytes(code[at:at+4],'little')
    target=lambda at:(word(at),offset(at+2))
    following=[(script,end)] if end<len(code) else []
    if op==ops['jump']:return [target(p)]
    if op in (ops['end'],ops['bad_end'],ops['resume']):return []
    if op==ops['branch']:
        mode=code[p];condition=bool(word(p+1)) if mode==0 else False if mode==3 else None
        return (following if condition is not False else [])+([(script,offset(p+3))] if condition is not True else [])
    if op==ops['test']:
        stack=[];at=p
        while code[at]!=255:
            token=code[at];at+=1
            if token in (0,1,2):
                stack.append(bool(code[at]) if token==0 else None);at+=2 if token==2 else 1
            elif token==3:stack.append(False)
            elif token==130:stack[-1]=None if stack[-1] is None else not stack[-1]
            else:
                b,a=stack.pop(),stack.pop()
                stack.append((True if True in (a,b) else None if None in (a,b) else False) if token==128 else
                             (False if False in (a,b) else None if None in (a,b) else True))
        return (following if stack[0] is not False else [])+([(script,offset(at+1))] if stack[0] is not True else [])
    if op in (ops['menu'],ops['trial_menu']):
        at=p+2;result=[]
        for _ in range(code[p+1]):
            result.append(target(at+1));at+=7
            at=code.index(0,at)+1
        return result
    if op==ops['debate']:
        at=code.index(0,p+7)+1;count=code[at];at+=1
        return following+[target(at+4+i*10) for i in range(count)]
    return following


def entry_states(docs,codes,ops):
    trial={i for i,d in enumerate(docs) if '_Trial' in d['path'] and not d.get('_demo')}
    instructions={};edges={};incoming=set()
    for script in trial:
        code=codes[script];pc=0
        while pc<len(code):
            node=(script,pc);end=pc+3+int.from_bytes(code[pc+1:pc+3],'little')
            instructions[node]=(code[pc],code[pc+3:end])
            edges[node]=[n for n in successors(code,script,pc,ops) if n[0] in trial and
                         docs[n[0]]['path'].split('/')[0]==docs[script]['path'].split('/')[0]]
            incoming.update(n[0] for n in edges[node] if n[0]!=script)
            pc=end
    # Objection subgraphs return through resume, with the original debate PC.
    # Connect their effect state to the debate continuation as well as its skip edge.
    returns={};return_edges=[]
    for node,(op,p) in instructions.items():
        if op!=ops['debate']:continue
        after=(node[0],node[1]+3+len(p))
        for target in edges[node]:
            if target==after:continue
            if target not in returns:
                pending=[target];seen=set();found=[]
                while pending:
                    current=pending.pop()
                    if current in seen:continue
                    seen.add(current);kind,_=instructions[current]
                    if kind==ops['resume']:found.append(current)
                    elif kind!=ops['cancel_resume']:pending.extend(edges[current])
                returns[target]=found
            for returning in returns[target]:
                if after in instructions:return_edges.append((returning,after))
    for returning,after in return_edges:
        if after not in edges[returning]:edges[returning].append(after)
    neutral=tuple(frozenset([v]) for v in NEUTRAL)
    values={};queue=deque()
    def offer(node,state):
        if node not in instructions:return
        previous=values.get(node)
        joined=state if previous is None else tuple(a|b for a,b in zip(previous,state))
        if joined!=previous:values[node]=joined;queue.append(node)
    # Trial00/01 chapter roots, plus isolated segments without a normal predecessor.
    chapters={}
    for script in sorted(trial):chapters.setdefault(docs[script]['path'].split('/')[0],script)
    for script in set(chapters.values())|(trial-incoming):offer((script,0),neutral)
    while queue:
        node=queue.popleft();state=list(values[node]);op,p=instructions[node]
        if op==ops['screen'] and p[0]==0:
            color,amount=struct.unpack_from('<HH',p,1)
            if not p[7]&1:state[0]=frozenset([color])
            state[1]=frozenset([amount])
        elif op==ops['trial'] and p[0]==10:
            frames,x,y,period,damping,_=struct.unpack_from('<HbbBBB',p,1)
            # A decaying impact is a transient cue: never replay it on direct entry.
            state[3]=frozenset([(frames,x,y,period,damping) if not damping and (x or y) else None])
        elif op==ops['trial'] and p[0]==11:state[2]=frozenset([p[1]])
        for target in edges[node]:offer(target,tuple(state))
    seeds=[];report=[]
    for script,doc in enumerate(docs):
        state=values.get((script,0),neutral)
        resolved=tuple(next(iter(v)) if len(v)==1 else NEUTRAL[i] for i,v in enumerate(state))
        seeds.append(resolved)
        if resolved!=NEUTRAL or any(len(v)>1 for v in state):
            report.append(dict(path=doc['path'],entry=dict(zip(FIELDS,resolved)),
                               ambiguous={FIELDS[i]:sorted(v,key=str) for i,v in enumerate(state) if len(v)>1}))
    return seeds,report


def cpp_entries(seeds):
    unique=list(dict.fromkeys(seeds));assert len(unique)<256
    lines=['const story_effect_entry story_effect_entries[]={']
    for color,amount,fire,shake in unique:
        frames,x,y,period,damping=shake or (0,0,0,4,0)
        lines.append('{'+f'{color},{amount},{{{frames},{x},{y},{period},{damping}}},{fire}'+'},')
    return lines+['};','const uint8_t story_effect_ids[]={'+','.join(str(unique.index(s)) for s in seeds)+'};']
