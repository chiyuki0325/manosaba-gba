"""Propagate visible CG appearance identities through branches; reject any reachable blur."""
from collections import deque,Counter
from cg_stage_analysis import initial,join,apply_event,cfg,O,B

def analyze(doc,program,candidates):
    events=doc['events'];edges,_,_=cfg(doc);mapping=program['event_lines']
    states={0:(initial(),tuple(frozenset() for _ in range(16)))}
    queue=deque([0]);updates=Counter();blurred=set();stamps={};origins={};iterations=0
    while queue:
        i=queue.popleft();before,tokens=states[i];after=before;next_tokens=list(tokens)
        event=events[i];index=mapping.get(str(event['line']));iterations+=1
        if index is not None:
            descriptor=program['events'][index]
            envelope,after,*_=apply_event(before,program,descriptor)
            commands=program['commands'][descriptor[0]:descriptor[0]+descriptor[1]]
            image_slots={c[4] for c in commands if c[3]==0}
            for slot in range(3,11):
                visible=envelope[slot][1][O][1]>0
                image=envelope[slot][0]
                live=set()
                if visible:
                    for key in image & candidates:
                        retained={t for t in tokens[slot] if t[2]==key} if before[slot][1][O][1]>0 else set()
                        # A merge can inherit the same image on one route and replace it on another.
                        if not retained or before[slot][0]!=image:
                            origin=(index,slot,key);retained.add(origin)
                            origins[origin]=event['line']
                        live.update(retained)
                if live and envelope[slot][1][B][1]>0:blurred.update(live)
                if (slot in image_slots and image & candidates) or (live and not tokens[slot]):
                    stamps.setdefault((index,slot),set()).update(live)
                next_tokens[slot]=frozenset(live) if after[slot][1][O][1]>0 else frozenset()
        state=(after,tuple(next_tokens))
        for target in edges[i]:
            old=states.get(target)
            merged=state if old is None else (join(old[0],state[0]),tuple(a|b for a,b in zip(old[1],state[1])))
            if old==merged:continue
            updates[target]+=1
            if updates[target]>40:
                return {},dict(converged=False,processed_nodes=iterations,appearances=[])
            states[target]=merged;queue.append(target)
    # When control-flow merges cannot distinguish live identities, use one backend
    # for their entire lifetimes rather than changing it at a later repeated show.
    while True:
        previous=len(blurred)
        for live in stamps.values():
            if live&blurred:blurred.update(live)
        if len(blurred)==previous:break
    flags={event:{slot:bool(live) and not bool(live&blurred) for (ev,slot),live in stamps.items() if ev==event}
           for event,_ in stamps}
    report=dict(converged=True,processed_nodes=iterations,appearances=[dict(event=e,slot=s,asset=k,line=origins[(e,s,k)],blur=(e,s,k) in blurred) for e,s,k in sorted(origins)])
    return flags,report
