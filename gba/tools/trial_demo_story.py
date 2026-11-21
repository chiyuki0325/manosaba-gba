"""A named, non-canonical workflow demo, compiled by the same trial compiler."""
def document():
    events=[]
    def e(op,**params):events.append(dict(op=op,line=len(events)+1,indent=0,params=params))
    def say(text):e('PrintTextModified',AuthorId='Ema',Text=text)
    e('SetTrialTimer',Time=120)
    e('UpdateWitchBook',Id='1-1',Category='Clue',Version=1)
    e('UpdateWitchBook',Id='1-2',Category='Clue',Version=1)
    e('TrialView',View=0);say('审判场景演示。\n三个人物共用正式立绘图格。')
    e('TrialView',View=3);say('背景滚动与人物一起移动。\n选择菜单会暂停讨论计时。')
    e('TrialView',View=1);say('近景切换完成。\n接下来连续播放三种异议动画。')
    for who in ('Ema','Hiro','CreatureHiro'):e('GosubToObjectionCutIn',CutInKind=who,Index=1)
    e('TrialView',View=0);e('ChoiceEvidence',CategoryNames=['Clue'],Back=0,Global=0)
    say('物品选择已记录。\n接下来展示一张全屏证据。')
    e('SpawnExtended',Path='Clue',Params=['1-11'])
    say('证据展示可以暂停并返回。')
    e('DestroySpawnedExtended',Path='Clue')
    e('PrintDebateText',ScenePosition=[50,25],Text='这是<link="point1">反驳点</link>、<link="point2">赞同点</link>和<link="point3">伪证点</link>。\n把握每句话的剩余时间。')
    e('GotoModified',Path='.end')
    for n,kind in enumerate(('Objection','Approval','Perjury'),1):
        events.append(dict(op='label',name=f'point{n}',line=len(events)+1,indent=0))
        e('AddChoice',ChoiceSummary='确认这项异议',ButtonPath='ChoiceButtons/Trial/'+kind,HandlerId='TrialHiro' if n==3 else 'Trial')
        e('CancelGosub');events[-1]['indent']=1
        e('GotoModified',Path='.end');events[-1]['indent']=1
        e('AddChoice',ChoiceSummary='返回',ButtonPath='ChoiceButtons/Trial/Cancel',HandlerId='Trial')
        e('Return');events[-1]['indent']=1
        e('StopForChoice')
    events.append(dict(op='label',name='end',line=len(events)+1,indent=0))
    say('功能演示结束。\nSelect 打开图鉴，Start 打开暂停。')
    return dict(format='manosaba.story',version=1,language='zh-Hans',path='Demo/TrialStage',title='审判场景与操作演示',events=events,_trial=True,_demo=True)


def presentation_document():
    """Small selectable regression scene; uses the production presentation backend."""
    import json
    from graphics_project import GBA
    data=json.loads((GBA/'assets/trial/poses.json').read_text())
    cameras=json.loads((GBA/'assets/trial/camera.json').read_text())
    events=[]
    def event(op,**params):events.append(dict(op=op,line=len(events)+1,indent=0,params=params))
    def say(text,who='Ema'):event('PrintTextModified',AuthorId=who,Text=text)
    images=list(dict.fromkeys(row['image'] for row in data['references'] if row['actor']==0))
    for layout in (0,10):
        event('TrialLayout',Layout=layout)
        event('TrialPose',Actor=0,Image=images[0])
        for view in (4,5,6,7,10):
            event('TrialView',View=view)
            say(('十三席' if layout==0 else '十四席')+'镜位 '+str(view)+'。')
        event('TrialView',View=4);event('TrialPose',Actor=0,Image=images[1]);say('同一人物切换姿态。')
    event('TrialLayout',Layout=10);event('TrialView',View=4,Actor=13);say('第十四席人物。','Yuki')
    for index,motion in enumerate(cameras['reduced_compound']):
        event('TrialLayout',Layout=motion['layout']);event('TrialView',View=4)
        say('复合镜头 '+str(index+1)+'：A 开始。')
        event('TrialMotion',Motion=motion['motion'],Frames=motion['frames'])
        say('复合镜头 '+str(index+1)+' 完成。')
    return dict(format='manosaba.story',version=1,language='zh-Hans',path='Demo/TrialPresentation',
                title='审判降级演出验证（非剧情）',events=events,_trial=True,_trial_fixture=True,_layout=0)
