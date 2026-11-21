"""Compile trial control flow, timed utterances and interruptible objection links."""
from pathlib import Path
from collections import Counter
import html,json,re,struct
from story_layout import clean_text,reflow,PROFILES,pixel_width
from trial_camera import catalog as camera_catalog
from trial_views import catalog as view_catalog,reduced_view,preset_view
from witch_book_catalog import BookCatalog
from trial_effects import shake_command

GBA=Path(__file__).resolve().parents[1]
SOURCE=GBA.parent/'story/zh-Hans'
u16=lambda n:struct.pack('<H',n)
u32=lambda n:struct.pack('<I',n)
TRIAL_OPS=dict(trial=14,debate=15,trial_menu=16,test=17,variable=18,resume=19,
              cancel_resume=20,inventory=21,timer=22,damage=23,cutin=24,picture=25,bad_id=26,
              book_update=27,book_clear=28,book_reset=29,bad_end=30,title=31)
KINDS={'Objection':1,'Approval':2,'Question':3,'Perjury':4,'MagicEma':5,'MagicLeia':6,
       'MagicAnAn':7,'MagicCoco':8,'MagicMargo':9,'MagicNanoka':10,'Cancel':128}
ACTORS=['Ema','Hiro','Sherry','AnAn','Coco','Hanna','Noah','Leia','Miria','Meruru',
        'Margo','Nanoka','Alisa','Yuki','Warden','Jailer','CreatureHiro']


def toast_command(params):
    return bytes([{'':0,'魔女图鉴 更新…':1,'讨论前进…':2}[params.get('Text','')]])

def printer_command(params):
    appearance=params.get('IdAndAppearance','Normal.Default')
    return bytes([3,2 if appearance=='Normal.Stream' else int(appearance!='Normal.Default')])

def screen_command(params):
    value=params.get('TintColor');inherit=value is None
    if value is not None:
        value={'white':'#ffffff','black':'#000000','red':'#ff0000'}.get(value,value)
        channels=bytes.fromhex(value.lstrip('#'))
    else:channels=bytes((255,255,255))
    color=sum((channels[i]>>3)<<(i*5) for i in range(3))
    amount=0 if params['AppearanceAndTransition']=='Transparent' else round((channels[3] if len(channels)==4 else 255)*256/255)
    frames=round(float(params.get('Duration',.35))*60)
    return b'\0'+u16(color)+u16(amount)+u16(frames)+bytes([int(inherit)|int(bool(params.get('Wait')))*128])


def localized(values):
    return next((x['_text'] for x in values if x['_locale']==2),'')


class TrialCatalog:
    def __init__(self,docs,speakers):
        self.source=json.loads((GBA/'assets/source-metadata/trial.json').read_text())
        self.voices=self.source['voices'];self.variables=[];self.items=[];self.ids={};self.speakers=speakers
        self.cameras=camera_catalog()
        self.views=view_catalog()
        self.camera_lookup=dict(self.cameras['lookup'])
        self.camera_lookup.update({(row['path'],row['line']):len(self.cameras['motions'])+i for i,row in enumerate(self.cameras['unmapped'])})
        self.pose_lookup={(row['path'],row['line']):row for row in json.loads((GBA/'assets/trial/poses.json').read_text())['references']}
        master=self.source['master']
        characters={x['_id']:localized(x['_name']) for x in master['CharacterData']['_items']}
        for x in master['ClueData']['_items']:
            if x['_id'] not in self.ids:self.item(x['_id'],clean_text(localized(x['_item']['_name'])),0)
        for who in ACTORS[:-1]:
            self.item(who,characters.get(who,who),1)
        self.book=BookCatalog(master,self.item)
        self.bad_entries={};current_bad=1
        for d in docs:
            if not re.search(r'_Trial\d+$',d['path']):continue
            self.bad_entries[d['path']]=current_bad
            for e in d['events']:
                if e['op']=='SetBadId':current_bad=e['params']['CommonBadEndId']
        for d in docs:
            for e in d['events']:
                p=e.get('params',{});expr=p.get('Expression','')
                if e['op']=='SetCustomVariable' and isinstance(expr,str):
                    m=re.fullmatch(r'\s*(\w+)\s*=\s*(true|false)\s*',expr,re.I)
                    if m and m[1] not in self.variables:self.variables.append(m[1])
                if e['op']=='ChoiceMapArea':
                    for area in p['MapAreaNames']:self.item(area,area,2)
        assert len(self.variables)<=64
        self.stats=Counter();self.contexts=[];self.screen_contexts=[]
        self.reenacts=json.loads((GBA/'assets/reenact/catalog.json').read_text())['names']
        maps=json.loads((GBA/'assets/trial-maps/catalog.json').read_text())
        self.maps={(r['path'],r['line']):r['segment'] for r in maps['contexts']}

    def item(self,key,label=None,kind=0):
        if key not in self.ids:
            self.ids[key]=len(self.items)+1;self.items.append(dict(id=key,label=label or key,kind=kind))
        return self.ids[key]

    def expression(self,expr):
        expr=expr.replace(' ','')
        for separator,token in (('||',0x80),('&&',0x81)):
            parts=expr.split(separator)
            if len(parts)>1:
                result=self.expression(parts[0])
                for part in parts[1:]:result+=self.expression(part)+bytes([token])
                return result
        if expr.startswith('!'):return self.expression(expr[1:])+b'\x82'
        if expr=='isDebugDirectJump':return b'\x03'
        if expr=='enableDebugSkip':return b'\x00\x00'
        if m:=re.fullmatch(r'chosenEvidenceId=="([^"]+)"',expr):return b'\x02'+u16(self.item(m[1]))
        if m:=re.fullmatch(r'(\w+)(?:={1,2}(true|false))?',expr,re.I):
            if m[1].lower() in ('true','false'):return bytes([0,int(m[1].lower()=='true')])
            assert m[1] in self.variables,expr
            result=bytes([1,self.variables.index(m[1])])
            return result+(b'\x82' if m[2] and m[2].lower()=='false' else b'')
        raise ValueError(('trial condition',expr))

    def context(self,doc,e,feature):
        self.contexts.append(dict(path=doc['path'],line=e['line'],op=e['op'],feature=feature,params=e.get('params',{})))


def link_spans(text,source):
    normalized=lambda s:''.join(c for c in clean_text(s) if not c.isspace())
    visible=[(i,c) for i,c in enumerate(text) if not c.isspace()];flat=''.join(c for _,c in visible);links=[]
    for m in re.finditer(r'<link\s*=\s*["\']([^"\']+)["\']\s*>(.*?)</link>',source,re.S):
        at=len(normalized(source[:m.start()]));needle=normalized(m[2])
        assert flat[at:at+len(needle)]==needle,(text,m[2],at)
        first,last=visible[at][0],visible[at+len(needle)-1][0]+1
        links.append((m[1],first,last))
    return links


def debate_text(source,width=216):
    markers=iter(('\ue000','\ue001','\ue002'))
    source=re.sub(r'(<link\s*=\s*["\'][^"\']+["\']\s*>)(.*?)</link>',lambda m:m[1]+m[2]+next(markers)+'</link>',source,flags=re.S)
    def protect(match):
        phrase=clean_text(match[2])
        return match[1]+('\u2060'.join(phrase) if pixel_width(phrase)<=width else match[2])+'</link>'
    wrapped=re.sub(r'(<link\s*=\s*["\'][^"\']+["\']\s*>)(.*?)</link>',protect,source,flags=re.S)
    text=reflow(wrapped,width).replace('\u2060','')
    links=[(target,len(text[:first].encode()),len(text[:last].encode())) for target,first,last in link_spans(text,source)]
    assert len(links)<=3
    return text,links


def debate_layout(source,position):
    cx=round(position[0]*2.4);cy=round((100-position[1])*1.6)
    available=2*min(cx-12,228-cx)//6*6
    text,links=debate_text(source,available)
    while (text.count('\n')>=8 or any(row and '\ue000'<=row[0]<='\ue002' for row in text.split('\n'))) and available<216:
        available=min(216,available+12);text,links=debate_text(source,available)
    rows=text.split('\n');width=max(pixel_width(row) for row in rows);height=12+16*(min(8,len(rows))-1)
    left=max(12,min(228-width,round(cx-width/2)))
    top=max(22,min(150-height,round(cy-height/2)))
    return text,links,(left,top,left+width,top+height)


def compile_trial(compiler,doc,string):
    catalog=compiler.trials;events=doc['events'];path=doc['path'];script=compiler.ids[path]
    out=bytearray();event_pc={};patches=[];skip_headers=set();body_ends={};groups={};speaker='Ema';actor='Ema';view=0;text_position=(70,50)
    explicit_focus=False;camera_active=False
    reenact_document=any(e['op']=='SpawnExtended' and str(e.get('params',{}).get('Path','')).startswith('Reenact') for e in events)
    curtain_credit=0
    if not doc.get('_demo'):view=4
    def emit(op,payload=b''):
        number=TRIAL_OPS[op] if op in TRIAL_OPS else compiler.ops[op]
        out.extend(bytes([number])+u16(len(payload))+payload);compiler.report[op]+=1
    def end(index):
        indent=events[index].get('indent',0);index+=1
        while index<len(events) and events[index].get('indent',0)>indent:index+=1
        return index
    def jump_event(index):
        emit('jump',u16(script)+u32(0));patches.append((len(out)-4,index))
    def destination(target,offset):
        target_path,label=compiler.target(target,path)
        if target_path not in compiler.ids:return None
        compiler.relocations.append((out,offset,(target_path,label)))
        return u16(compiler.ids[target_path])+u32(0)
    def placeholder(e,feature):catalog.context(doc,e,feature)
    def screen_event(event,payload):
        emit('screen',payload)
        catalog.screen_contexts.append(dict(path=path,line=event['line'],pc=len(out),payload=list(payload)))
    emit('trial',bytes([0,catalog.bad_entries.get(path,1)]))
    layout=None
    if not doc.get('_demo'):
        chapter=path.split('/')[0]
        layout=doc['_layout'] if '_layout' in doc else next(i for i,row in enumerate(catalog.cameras['layouts']) if row['chapter']==chapter)
        emit('trial',bytes([7,layout]))
    def focus_actor(who):
        index=next((i for i,name in enumerate(ACTORS) if name.lower()==who.lower()),0)
        if layout is not None and (1 if index==16 else index) not in catalog.cameras['layouts'][layout]['seats']:return 255
        return index
    for i,e in enumerate(events):
        if i in body_ends:jump_event(body_ends[i])
        event_pc[i]=len(out);op=e['op'];p=e.get('params',{})
        graphics=compiler.graphics.get(path)
        if graphics and str(e['line']) in graphics['event_lines']:emit('stage',u16(graphics['event_lines'][str(e['line'])]))
        book_command=catalog.book.command(path,e)
        if book_command:emit(*book_command)
        elif op=='label':compiler.labels[(path,e['name'])]=len(out)
        elif op in ('PrintTextModified','PrintDebateText'):
            if op=='PrintTextModified':
                box=PROFILES['graphical'];speaker=p.get('AuthorId','');text=reflow(p.get('Text',''),box['right']-box['left'])
                if not explicit_focus and not doc.get('_demo') and speaker in ACTORS:
                    emit('trial',bytes([1,focus_actor(speaker),view]))
                compiler.colored_text(doc,e,text,len(out)+4,len(out)+5+len(text.encode()))
                emit('say',bytes([compiler.speakers.index(speaker)])+string(text,384))
            else:
                voice_ids=e.get('text_ids',{}).get('Text',[])
                if voice_ids:
                    m=re.search(r'_([A-Za-z]+)\d+$',voice_ids[0])
                    if m and m[1] in compiler.speakers:speaker=m[1]
                if not explicit_focus and not doc.get('_demo') and speaker in ACTORS:
                    emit('trial',bytes([1,focus_actor(speaker),view]))
                text_position=p.get('ScenePosition',text_position)
                text,links,box=debate_layout(p['Text'],text_position)
                frames=max((catalog.voices.get(key,0) for key in voice_ids),default=0)
                if not frames:frames=round((len(clean_text(p['Text']))*.15+.6)*60);catalog.stats['fallback_voice']+=1
                if doc.get('_demo'):frames=max(frames,900) # Noncanonical UI demo: time to inspect three points.
                if i+1<len(events) and events[i+1]['op']=='Wait':
                    gap=events[i+1]['params'].get('WaitMode','')
                    if isinstance(gap,str) and re.fullmatch(r'i?\d+(?:\.\d+)?',gap):frames+=round(float(gap.lstrip('i'))*60)
                # Original CalculateWaitTime returns the clip length directly.
                duration=max(1,frames)
                if i+1<len(events) and events[i+1]['op']=='Stop':duration|=0x8000
                payload=bytearray([compiler.speakers.index(speaker)])+u16(duration)+bytes(box)+string(text,384)+bytes([len(links)])
                for target,first,last in links:
                    offset=len(out)+3+len(payload)+4
                    payload+=u16(first)+u16(last)+destination('.'+target,offset)
                compiler.colored_text(doc,e,text,len(out)+10,len(out)+3+len(payload))
                emit('debate',payload);catalog.stats['debate']+=1;catalog.stats['links']+=len(links)
        elif op=='GotoModified':
            if isinstance(p['Path'],str):
                dest=destination(p['Path'],len(out)+3)
                if dest is None:emit('end',string('后续章节待接入：'+p['Path'].split('/')[-1],384))
                else:emit('jump',dest)
            else:placeholder(e,'dynamic_jump');emit('pause',string('特殊剧情跳转待适配。',384))
        elif op in ('BeginIf','Else'):
            after=end(i)
            if op=='Else':jump_event(after)
            else:
                expression=catalog.expression(p['Expression'])
                if after<len(events) and events[after]['op']=='Else':after+=1
                emit('test',expression+b'\xff'+u32(0));patches.append((len(out)-4,after))
        elif op=='AddChoice':
            if i in skip_headers:continue
            headers=[];j=i
            while j<len(events) and events[j]['op']!='StopForChoice':
                if events[j]['op']=='label':j+=1;continue
                assert events[j]['op']=='AddChoice',(path,events[j])
                headers.append(j);skip_headers.add(j);j=end(j)
            assert j<len(events) and len(headers)<=8,(path,i)
            objection=any('/Trial/' in events[k]['params'].get('ButtonPath','') for k in headers)
            payload=bytearray([2 if objection else 0,len(headers)])
            for h in headers:
                q=events[h]['params'];role=q.get('ButtonPath','').split('/')[-1]
                value=KINDS.get(role,0)|(32 if q.get('HandlerId')=='TrialHiro' else 0)
                payload+=bytes([value])+u16(script)+u32(0)
                patches.append((len(out)+3+len(payload)-4,h+1))
                payload+=string(reflow(clean_text(q['ChoiceSummary']),144),160)
                body_ends[end(h)]=j+1
            emit('trial_menu' if objection else 'menu',payload);jump_event(j+1)
        elif op=='TrialView':
            if camera_active:emit('trial',bytes([5,255])+u16(0)+b'\x00');camera_active=False
            view=p['View'];explicit_focus=True
            emit('trial',bytes([1,p.get('Actor',0),view]))
        elif op=='TrialLayout':emit('trial',bytes([7,p['Layout']]))
        elif op=='TrialPose':emit('trial',bytes([8,p['Actor']])+u16(p['Image']))
        elif op=='TrialMotion':
            emit('trial',bytes([5,p['Motion']])+u16(p['Frames'])+b'\x01');camera_active=True
        elif op=='Return':emit('resume')
        elif op=='CancelGosub':emit('cancel_resume')
        elif op=='SetTrialTimer':emit('timer',u32(round(p['Time']*60)))
        elif op=='Damage':emit('damage')
        elif op=='SetBadId':emit('bad_id',bytes([p['CommonBadEndId']]))
        elif op=='SetCustomVariable':
            expr=p['Expression']
            if isinstance(expr,str) and (m:=re.fullmatch(r'\s*(\w+)\s*=\s*(true|false)\s*',expr,re.I)):
                emit('variable',bytes([catalog.variables.index(m[1]),int(m[2].lower()=='true')]))
            elif isinstance(expr,dict):placeholder(e,'dynamic_camera_variable')
        elif op=='GosubToObjectionCutIn':
            emit('cutin',bytes([{'Ema':0,'Hiro':1,'CreatureHiro':2}[p['CutInKind']],p.get('Index',1)]))
        elif op=='ChoiceEvidence':
            kind=1 if p['CategoryNames']==['Profile'] else 0
            emit('inventory',bytes([kind,int(p.get('Back',0)),0]))
        elif op=='ChoiceMapArea':
            ids=[catalog.item(x) for x in p['MapAreaNames']]
            emit('inventory',bytes([2,int(p.get('Back',0)),len(ids)])+b''.join(u16(x) for x in ids))
        elif op=='SpawnExtended':
            spawn=p.get('Path','')
            if spawn=='Clue':emit('picture',u16(catalog.item(p['Params'][0])))
            elif isinstance(spawn,str) and spawn.startswith('ObjectionCutIn_'):
                emit('cutin',bytes([{'Ema':0,'Hiro':1,'CreatureHiro':2}[spawn.split('_')[-1]],int(p.get('Params',['1'])[0])]))
            elif isinstance(spawn,str) and spawn.startswith('Reenact'):
                emit('reenact',b'\x00'+u16(catalog.reenacts.index(spawn)));curtain_credit=40
            elif spawn=='MaskingRectangle':catalog.context(doc,e,'split_mask_reduced')
            elif (path,e['line']) in catalog.maps:
                emit('map',u16(catalog.maps[path,e['line']])+bytes([int(bool(p.get('Wait')))]));catalog.context(doc,e,'trial_map')
            else:placeholder(e,'reenact' if isinstance(spawn,str) and spawn.startswith('Reenact') else 'spawn_effect')
        elif op=='DestroySpawnedExtended':
            if p.get('Path')=='Clue':emit('picture',u16(0))
            elif str(p.get('Path','')).startswith('Reenact'):
                emit('reenact',b'\x01');curtain_credit=40
            elif p.get('Path')=='MaskingRectangle':catalog.context(doc,e,'split_mask_reduced')
            elif str(p.get('Path','')).startswith('Map_A'):
                emit('map',u16(65535)+bytes([int(bool(p.get('Wait')))]));catalog.context(doc,e,'trial_map')
        elif op=='GosubToModifyPerspectiveStageCamera':
            if camera_active:emit('trial',bytes([5,255])+u16(0)+b'\x00');camera_active=False
            explicit_focus=True
            actor=p.get('LookCharacterId',actor);speaker=actor if actor in compiler.speakers else speaker
            view=preset_view(p)
            emit('trial',bytes([1,focus_actor(actor),view]))
        elif op=='ModifyCharacterExtended':
            appearance=p.get('IdAndAppearance',p.get('Id',''))
            if isinstance(appearance,str) and appearance:
                emit('character',string(appearance.split(',')[0],96))
                pose=catalog.pose_lookup.get((path,e['line']))
                if pose:emit('trial',bytes([8,pose['actor']])+u16(pose['image']))
                if appearance.startswith('CreatureHiro'):emit('trial',bytes([1,16,view]))
        elif op=='GosubToSplitPerspectiveStageScreens':
            explicit_focus=False
            placeholder(e,'split_screen');actor=p.get('LookCharacterId','Ema')
            emit('trial',bytes([1,ACTORS.index(actor) if actor in ACTORS else 0,0]))
        elif op=='Gosub':
            target=p['Path']
            if target.endswith('AdditionalDebate-CutIn-2'):emit('cutin',b'\x00\x02')
            elif target.endswith(('BeginDebate-Effect','EndDebate-Effect')):
                emit('title',bytes([int(target.endswith('EndDebate-Effect'))]))
            elif target.endswith(('BeginAdv','EndAdv')):
                emit('trial',bytes([4,int(target.endswith('BeginAdv'))]))
        elif op=='ModifyTextPrinter':
            emit('trial',printer_command(p))
        elif op=='ResetText':emit('trial',bytes([9]))
        elif op=='ModifyBackgroundExtended':
            if p.get('SubId')=='Overlay' and p.get('AppearanceAndTransition','').split('.')[0] in ('SolidColor','Transparent'):
                screen_event(e,screen_command(p));catalog.stats['screen_overlay']+=1
            elif graphics and str(e['line']) in graphics['event_lines']:catalog.context(doc,e,'background_image_live')
            else:placeholder(e,'background_image')
        elif op in ('PlaySfxModified','StopSfx'):emit('sfx',compiler.sfx.command(doc,e))
        elif op in ('PlayBgm','StopBgm'):
            emit('music',compiler.music.command(doc,e))
        elif op=='ShowToastUI':emit('toast',toast_command(p))
        elif op=='Wait':
            gap=p.get('WaitMode','')
            if reenact_document and isinstance(gap,str) and re.fullmatch(r'i?\d+(?:\.\d+)?',gap):
                frames=max(0,round(float(gap.lstrip('i'))*60)-curtain_credit);curtain_credit=0
                if frames:emit('reenact',b'\x02'+u16(frames))
            # Debate gaps are already included in the preceding voice duration.
            elif camera_active and (i==0 or events[i-1]['op']!='PrintDebateText') and isinstance(gap,str) and re.fullmatch(r'i?\d+(?:\.\d+)?',gap):
                emit('trial',bytes([6])+u16(round(float(gap.lstrip('i'))*60)))
            elif (i==0 or events[i-1]['op']!='PrintDebateText') and isinstance(gap,str) and re.fullmatch(r'i?\d+(?:\.\d+)?',gap):
                frames=round(float(gap.lstrip('i'))*60)
                if frames:screen_event(e,b'\x01'+u16(frames))
        elif op=='ModifyPerspectiveStageCamera':
            explicit_focus=False
            motion=catalog.camera_lookup.get((path,e['line']))
            if motion is not None:
                emit('trial',bytes([5,motion])+u16(round(p['Duration']*60))+bytes([int(bool(p.get('Wait',0)))]));camera_active=True
                if motion<len(catalog.cameras['motions']):catalog.stats['camera_'+catalog.cameras['motions'][motion]['kind']]+=1
                else:catalog.stats['camera_reduced_compound']+=1;placeholder(e,'camera_reduced_compound')
            else:
                if camera_active:emit('trial',bytes([5,255])+u16(0)+b'\x00');camera_active=False
                reference=catalog.views['lookup'][(path,e['line'])]
                state=catalog.views['views'][reference['view']];count=catalog.views['layouts'][reference['layout']]['count']
                view=reduced_view(dict(distance=state['distance'],height=state['height'],rotation=[state['pitch'],state['yaw'],state['roll']]),count)
                who=catalog.views['layouts'][reference['layout']]['seats'][reference['seat']%count]
                if who!=255:actor=ACTORS[who]
                emit('trial',bytes([1,who,view]));explicit_focus=True
                catalog.stats['camera_reduced_cut']+=1
        elif op=='ShakePerspectiveStageCamera':
            emit('trial',shake_command(p));catalog.stats['camera_shake']+=1
        elif op=='SpawnShake':
            count=int(p.get('ShakesCount',3));duration=float(p.get('ShakeDuration',.15));amplitude=min(3,max(1,round(float(p.get('ShakeAmplitude',.5))*240/25.6)))
            emit('trial',bytes([12,ACTORS.index(p['ActorId'])])+struct.pack('<HbbBB',round(count*duration*60),amplitude,0,max(4,round(duration*60)),0));catalog.context(doc,e,'actor_shake')
        elif op=='ChangeCourtFireFxActivity':
            emit('trial',bytes([11,int(bool(p['Enable']))]));catalog.context(doc,e,'court_heat');catalog.stats['court_heat']+=1
        elif op=='ProcessBadEnd':emit('bad_end',string('本段结束。',384))
        elif op=='ReturnToTitle':emit('end',string('本段结束。',384))
        elif op=='Stop':catalog.context(doc,e,'debate_stop')
        elif op in ('PlayMovie','ShowCreditsUi'):placeholder(e,'movie_or_stop');emit('pause',string('演出待适配，按 A 继续。',384))
        elif op in {'HidePrinter','HidePerspectiveStageScreen','ShowPerspectiveStageScreen',
                    'ChangeTrialPauseMode','HideUI','ShowUI','WaitForNextFrame','Skip','PlaySfxModified','StopVoice','PlayVoice',
                    'HideActors','ProcessInput','RecordCorrectChoice','QuickSave','EmoteJump','EmoteJumpDouble',
                    'EmoteShake','EmoteNod','StopSfx','StopForChoice','HideAllCharacters','SpawnShake',
                    'ModifyCamera','ChangeActorMaskability','ChangeActorPivot','ShowActors','AnimateActorExtended','ClearBacklog'}:pass
        else:placeholder(e,'unhandled_command')
    if len(events) in body_ends:jump_event(body_ends[len(events)])
    event_pc[len(events)]=len(out);emit('end',string('本段审判结束。',384));compiler.labels[(path,'')]=0
    for offset,target in patches:out[offset:offset+4]=u32(event_pc[target])
    return out
