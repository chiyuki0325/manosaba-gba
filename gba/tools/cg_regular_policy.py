"""Conservative geometry admission plus per-appearance blur analysis."""
import json
from collections import defaultdict

def entries(cache,paths,ids,programs):
    from cg_usage import geometry_usage
    usage=geometry_usage(cache,programs,ids)
    accepted=[]
    for key,contexts in usage.items():
        by_path=defaultdict(list)
        for context in contexts:by_path[context['path']].append(context)
        for path,rows in by_path.items():
            if all(not c['possible_nonunit'] and not c['possible_rotation'] and
                   not set(c['complex_reasons']) & {'multiple_cg_layers_possible','visible_rectangular_mask_possible'}
                   for c in rows):accepted.append((paths.index(path)<<16)|ids[key])
    return sorted(set(accepted))

def appearances(cache,paths,ids,programs):
    from cg_appearance_policy import analyze
    from graphics_project import SOURCE
    banks=entries(cache,paths,ids,programs);by_bank=defaultdict(set);reverse={v:k for k,v in ids.items()}
    for value in banks:by_bank[value>>16].add(reverse[value&65535])
    flags={}
    for bank,candidates in by_bank.items():
        path=paths[bank];doc=json.loads((SOURCE/(path+'.json')).read_text())
        found,_=analyze(doc,cache.get_json(programs[path]),candidates)
        flags[bank]=found
    return flags
