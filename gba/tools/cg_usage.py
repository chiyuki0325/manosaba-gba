"""CG geometry admission from source CFG and compiled stage data.

This is compiler analysis, not a report reader. No gallery or diagnostic files
are needed, and no report is written by the asset build.
"""
import json,math,collections
from graphics_project import SOURCE
from cg_stage_analysis import *

class GeometryUsage:
    def __init__(self, imported, candidates):
        self.imported=imported
        self.candidates={key:None for key in candidates}
        self.records={key:dict(contexts={},categories=set()) for key in candidates}
        self.stats=collections.Counter()

    def observe(self,state,path,line,phase,src,changes=(),cross=()):
     cam=state[11][1];sc,an=cam[S],cam[A]
     overlay=state[12]
     if overlay[0] and overlay[1][O][0]>=256:
      self.stats['fully_opaque_overlay_observations_excluded']+=1
      return
     def exact_geometry(en):
      ps=en[1]
      if any(ps[k][0]!=ps[k][1] or cam[k][0]!=cam[k][1] for k in (S,A,X,Y)):return None
      scale=ps[S][0]*cam[S][0]/65536
      ca=math.radians(cam[A][0]/256);dx=ps[X][0]/256-120-cam[X][0]/256;dy=ps[Y][0]/256-80-cam[Y][0]/256
      zoom=cam[S][0]/256
      return (120+(dx*math.cos(ca)-dy*math.sin(ca))*zoom,80+(dx*math.sin(ca)+dy*math.cos(ca))*zoom,scale,math.radians((ps[A][0]+cam[A][0])/256))
     def covers(en):
      if en[1][O][0]<256 or en[1][R][0]<256 or en[3] or en[2]==-32768:return False
      # The renderer may clip this plane with an independently spawned rectangle.
      # Without proving the mask group/geometry, it is not a full-screen occluder.
      if any(state[m][0] and state[m][1][O][1]>0 for m in range(13,16)):return False
      geo=exact_geometry(en)
      if not geo:return False
      cx,cy,scale,ang=geo
      for key in en[0]:
       asset=self.imported.get(key,{})
       if asset.get('kind') not in ('background','solid'):return False
       w,h=asset.get('size',(0,0))
       if not all(abs((x-cx)*math.cos(ang)+(y-cy)*math.sin(ang))<=abs(w*scale)/2+.001 and abs(-(x-cx)*math.sin(ang)+(y-cy)*math.cos(ang))<=abs(h*scale)/2+.001 for x,y in ((0,0),(240,0),(0,160),(240,160))):return False
      return bool(en[0])
     visible=[(i,e) for i,e in enumerate(state[3:11],3) if e[0] and e[1][O][1]>0]
     for i,en in visible:
      ims,ps,order,mask=en
      for key in ims:
       if key not in self.records:continue
       geo=exact_geometry(en)
       if geo:
        cx,cy,zz,ang=geo;w,h=self.imported[key]['size'];hx=abs(math.cos(ang)*w*zz)/2+abs(math.sin(ang)*h*zz)/2;hy=abs(math.sin(ang)*w*zz)/2+abs(math.cos(ang)*h*zz)/2
        if cx+hx<=0 or cx-hx>=240 or cy+hy<=0 or cy-hy>=160:
         self.stats['offscreen_observations_excluded']+=1;continue
       if en[2]!=-32768 and any(j!=i and other[2]>en[2] and covers(other) for j,other in visible):
        self.stats['fully_covered_scenery_observations_excluded']+=1;continue
       row=self.records[key]
       products=[a*b/65536 for a in ps[S][:2] for b in sc[:2]]
       scale=[min(products),max(products)];angle=[(ps[A][0]+an[0])/256,(ps[A][1]+an[1])/256]
       nonunit=scale[0]!=1 or scale[1]!=1;rotated=any(abs(a-round(a/360)*360)>1e-7 for a in angle) or angle[0]!=angle[1]
       stableunit=(ps[S][2]*sc[2]==65536 and ps[S][3]*sc[3]==65536 and ps[A][2]+an[2]==0 and ps[A][3]+an[3]==0)
       complex_reasons=[]
       cgplanes=[j for j,e in visible if e[0]&self.candidates.keys()]
       if len(cgplanes)>1:complex_reasons.append('multiple_cg_layers_possible')
       if ps[O][0]<256:complex_reasons.append('opacity_transition_or_transparency')
       if ps[R][0]<256:complex_reasons.append('dissolve_or_crossfade')
       if mask:complex_reasons.append('dissolve_mask')
       if any(state[m][0] and state[m][1][O][1]>0 for m in range(13,16)):complex_reasons.append('visible_rectangular_mask_possible')
       # A plane behind a self.candidates is not by itself evidence that a second renderer is needed.
       if any(j!=i and e[2]>order and e[1][O][1]>0 for j,e in visible):complex_reasons.append('higher_scenery_layer_possible')
       if nonunit:row['categories'].add('scale')
       if rotated:row['categories'].add('rotation')
       if complex_reasons:row['categories'].add('complex')
       ident=(path,line,phase,i)
       ob=dict(path=path,line=line,op=src['op'],phase=phase,slot=i,
        asset_alternatives=len(ims),opacity_interval=[x/256 for x in ps[O][:2]],
        local_scale_interval=[x/256 for x in ps[S][:2]],camera_scale_interval=[x/256 for x in sc[:2]],
        effective_scale_interval=scale,local_angle_interval=[x/256 for x in ps[A][:2]],camera_angle_interval=[x/256 for x in an[:2]],
        effective_angle_interval=angle,eventual_unit_unrotated=stableunit,
        possible_nonunit=nonunit,possible_rotation=rotated,complex_reasons=complex_reasons,
        transform_changes=list(changes),source_params=src.get('params',{}),geometric_visibility_exact=geo is not None)
       higher=[(j,other) for j,other in visible if j!=i and (other[2]>order or other[2]==order and j>i)]
       definite_higher=[x for x in higher if x[1][1][O][0]>0]
       ob['renderer_candidates']=dict(top_affine_possible=not definite_higher,lower_regular_or_proxy_possible=bool(higher),note='Logical source transform above is exact composition rule, not proof current lower regular BG executes that transform. Lower BG selects pre-baked scale variants; third layers may use OBJ proxies.')
       row['contexts'][ident]=ob

    def analyze(self,path,program):
     doc=json.loads((SOURCE/(path+'.json')).read_text());es=doc['events'];edges,branches,notes=cfg(doc)
     mapping=program['event_lines'];states={0:initial()};todo=collections.deque([0]);updates=collections.Counter();processed=0
     while todo:
      i=todo.popleft();state=states[i];e=es[i];processed+=1
      if processed>150000:
       notes.append(dict(line=e['line'],reason='worklist_limit'));break
      stage=mapping.get(str(e['line']))
      if stage is not None:
       ev=program['events'][stage]
       before=state
       envelope,state,changes,cross,wait,finish=apply_event(state,program,ev)
       # Scene commands are atomic. Invisible initialization is never observed halfway
       # through an event. The envelope includes every key, including transient geometry.
       self.observe(envelope,path,e['line'],'transition_envelope',e,changes,cross)
       self.observe(state,path,e['line'],'after_guaranteed_wait',e)
       for slot,old,new in cross:
        if envelope[slot][1][O][1]>0 and envelope[slot][1][R][0]<256:
         oldstate=list(envelope);en=envelope[slot];oldstate[slot]=(old,en[1],en[2],en[3])
         self.observe(tuple(oldstate),path,e['line'],'previous_image_during_crossfade',e,changes)
      for j in edges[i]:
       new=state if j not in states else join(states[j],state)
       if j in states and new==states[j]:continue
       updates[j]+=1
       if updates[j]>40:
        notes.append(dict(line=es[j]['line'],reason='relative_loop_or_state_growth_limit'));continue
       states[j]=new;todo.append(j)

def geometry_usage(cache,programs,asset_keys):
    imported=json.loads((cache.root/'import.json').read_text())['assets']
    candidates=[key for key in asset_keys if imported.get(key,{}).get('namespace')=='Stills']
    usage=GeometryUsage(imported,candidates)
    for path,key in programs.items():usage.analyze(path,cache.get_json(key))
    return {key:list(row['contexts'].values()) for key,row in usage.records.items()}
