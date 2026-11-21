"""Full source-context index for trial effects and deferred systems (not ROM data)."""
from build_paths import generated_path
from collections import Counter,defaultdict
from pathlib import Path
import json,re
from story_layout import clean_text
GBA=Path(__file__).resolve().parents[1]
ROOT=GBA.parent
LABELS={
 'background':'特殊背景切换','overlay':'纯色叠层与闪屏','camera':'连续运镜','split':'双画面分屏','shake':'震动',
 'fire':'法庭火焰','cutin':'异议动画','reenact':'重现与回忆','spawn':'其他生成演出',
 'destroy':'销毁演出','title':'讨论开始／结束','book':'图鉴版本与解锁',
 'court':'法庭初始化与14席','printer':'打印器样式','visibility':'镜位显隐',
 'creature':'魔女希罗形态','selector':'证物、人物与地图选择','deferred':'其他专用演出'}
STATUS={
 'background':'已接入107条图片背景命令／39份剧本：CG、推理图、回忆底图及二维人物，复用故事图片渲染器与图块字典，支持显隐、位置／旋转／染色／透明度、等待及返回法庭；图片镜头缩放降级为目标倍率切镜，单缓冲换图时隐藏上传中的图层。见[实现记录](trial-special-implementation.md)。',
 'overlay':'已接入全部514条纯色Overlay／Transparent命令：原色、透明度、时长、Wait标记及数值等待；场景调色板渐变，不增加整屏图像或显存。详见[实现与性能](trial-screen-effects.md)。',
 'camera':'已接入78条横向轨迹（69条短转动、9条长横扫），按原时长／缓动及各章座位播放；高度／俯仰固定，仍是二维近似。另8条推拉／升降等复合镜头与完整原作镜位 TODO。详见[实现记录](trial-implementation-notes.md)。',
 'split':'按用户要求明确降级为单镜位切换，不做分屏；Trial14的分屏前景矩形一并省略，保留原有对白、等待与镜位顺序。',
 'shake':'已接入41条法庭镜头震动：背景与人物同步1～3px位移，保持原时长与衰减；跨脚本保留，直接选章恢复持续震动。另接入可可SpawnShake（Act02_Chapter05_Trial13:388），仅当前人物震动；三个ADV独立ShakeBackground／ShakeCamera也复用现有舞台摇动。见[状态恢复](trial-effect-continuity.md)。',
 'fire':'已接入启停状态、跨脚本继承及选章恢复；画面降级为仅法庭背景的动态热浪与橙色染色，人物及UI不受影响。见[热浪实现](court-heat.md)。原作火焰图像仍未移植。',
 'cutin':'已接入三种角色、各三姿态的原作图层与位移／透明度曲线；粒子和辉光细节 TODO。',
 'reenact':'已接入56场正式重演：4bpp背景、稀疏人物OBJ、75姿态、幕布进出及原有对白；复杂旋转／骨骼变形降级为局部关键姿态。见[实现记录](reenact-implementation.md)。',
 'spawn':'三套审判地图、11次生成／3次销毁已接入：共用图鉴4bpp底图与硬件平移，人物标记按原时间轴移动、闪烁与显隐。分屏矩形按用户要求随分屏省略。见[实现记录](trial-special-implementation.md)。',
 'destroy':'Clue清除、Reenact闭幕／切场、三套地图0.5秒淡出已实现；分屏矩形销毁按单镜位降级省略。',
 'title':'已接入审问开始／结束动画，使用原作简中文字图与时间轴离线图块增量，直接叠在法庭与角色上；按用户要求删除纸张遮罩层。播放后自动继续，期间讨论计时冻结、音乐继续。细节见[实现记录](trial-implementation-notes.md)。',
 'book':'已接入原作版本、解锁、隐藏与重置子程序；候选项按当前状态过滤，Select全局查阅。见[图鉴实现记录](witch-book-implementation-notes.md)。',
 'court':'运镜已读取各章座位表，13席 Court 与14席 Court_Final 分别预渲染；静态14组原作镜位及完整初始化语义 TODO。',
 'printer':'已区分普通对白与 Simple／Stream：仅普通对白绘制底角。',
 'visibility':'降级到单镜位，原作多屏显隐语义 TODO。',
 'creature':'已接入共享魔女希罗代表立绘与独立 cut-in；完整姿态变化 TODO。',
 'selector':'人物／证物共用图鉴列表与详情，二级出示及版本过滤已接入；地图版本化热区／名称、上下楼层／左右地点、原候选ID提交已接入。见[地图实现记录](witch-book-map-implementation.md)。',
 'deferred':'TODO：专用演出、影片或系统流程。'}
def feature(e):
 op=e['op'];p=e.get('params',{});path=p.get('Path','')
 if op=='ModifyBackgroundExtended':
  return 'overlay' if p.get('SubId')=='Overlay' and p.get('AppearanceAndTransition','').split('.')[0] in ('SolidColor','Transparent') else 'background'
 if op=='ModifyPerspectiveStageCamera':return 'camera'
 if op=='GosubToSplitPerspectiveStageScreens':return 'split'
 if op in ('ShakePerspectiveStageCamera','SpawnShake'):return 'shake'
 if op=='ChangeCourtFireFxActivity':return 'fire'
 if op=='GosubToObjectionCutIn':return 'cutin'
 if op=='SpawnExtended':
  if path=='Clue':return None
  return 'cutin' if path.startswith('ObjectionCutIn') else 'reenact' if path.startswith('Reenact') else 'spawn'
 if op=='DestroySpawnedExtended':return 'destroy'
 if op=='Gosub':
  if path.endswith(('BeginDebate-Effect','EndDebate-Effect')):return 'title'
  if path.endswith(('BeginAdv','EndAdv')):return 'reenact'
  if 'AdditionalDebate-CutIn' in path:return 'cutin'
 if op in ('GosubResetWitchBook','UpdateWitchBook'):return 'book'
 if op=='ModifyTextPrinter':return 'printer'
 if op in ('HidePerspectiveStageScreen','ShowPerspectiveStageScreen'):return 'visibility'
 if op=='ModifyCharacterExtended' and 'CreatureHiro' in str(p):return 'creature'
 if op in ('ChoiceEvidence','ChoiceMapArea'):return 'selector'
 if op in ('PlayMovie','Stop','ShowCreditsUi','EmoteJump','EmoteJumpDouble','EmoteNod','EmoteShake','AnimateActorExtended','ChangeActorMaskability','ChangeActorPivot','ModifyCamera'):return 'deferred'
 if op=='SetCustomVariable':
  expr=p.get('Expression','')
  if isinstance(expr,dict):return 'camera'
  if 'stageName' in expr or 'courtStandCount' in expr:return 'court'
  if 'isReenact' in expr:return 'reenact'
 return None

def generate():
 groups=defaultdict(list);counts=Counter();audit=[]
 for source in sorted((ROOT/'story/zh-Hans').glob('Act*/*Trial*.json')):
  raw=source.read_text();doc=json.loads(raw);events=doc['events'];label='入口'
  physical={int(m[1]):raw.count('\n',0,m.start())+1 for m in re.finditer(r'"line":\s*(\d+)',raw)}
  before=[];last='（本文件无前文）'
  for e in events:
   before.append(last)
   if e['op'] in ('PrintDebateText','PrintTextModified'):last=clean_text(e['params'].get('Text',''))
  after=['']*len(events);last='（本文件无后文）'
  for i in range(len(events)-1,-1,-1):
   after[i]=last
   if events[i]['op'] in ('PrintDebateText','PrintTextModified'):last=clean_text(events[i]['params'].get('Text',''))
  for i,e in enumerate(events):
   if e['op']=='label':label=e['name']
   kind=feature(e)
   if not kind:continue
   entry=dict(feature=kind,path=doc['path'],line=e['line'],json_line=physical[e['line']],label=label,before=before[i],after=after[i],op=e['op'],params=e.get('params',{}))
   audit.append(entry);groups[kind].append(entry);counts[kind]+=1
 lines=['# 审判特殊演出与待办：全部源上下文','',
 '由 `tools/trial_contexts.py` 从全部编号审判脚本和 TrialInit 生成。每项链接定位实际 JSON 文件行号，另保留原剧本指令行、标签、前后台词和全部参数。正文包含剧透。',
 '', '范围包含特殊背景、连续运镜、分屏、震动、生成演出、cut-in、重现、法庭初始化、打印器与显隐，以及需占位的系统功能。常规角色姿态、离散对焦、音频命令不当作特殊效果；当前正式审判只用共享代表立绘，完整姿态及全部镜位仍是 TODO。',
 '', '实现状态见 [审判记录](trial-implementation-notes.md)。此文件不进入 ROM。','']
 for kind,title in LABELS.items():
  if not counts[kind]:continue
  lines+=['## '+title+'（'+str(counts[kind])+'处）','',STATUS[kind],'']
  current=None
  for e in groups[kind]:
   if e['path']!=current:current=e['path'];lines+=['### '+current,'']
   link='../story/zh-Hans/'+e['path']+'.json#L'+str(e['json_line'])
   safe=lambda s:s.replace('\n',' / ').replace('`','\\`')
   lines += [f"- [源指令 {e['line']}]({link}) · `{e['label']}` · `{e['op']}`",
             '  - 前文：'+safe(e['before']), '  - 后文：'+safe(e['after']),
             '  - 参数：`'+safe(json.dumps(e['params'],ensure_ascii=False,separators=(',',':')))+'`','']
 (generated_path('reports/trial-contexts.md')).write_text('\n'.join(lines).rstrip()+'\n')
 (generated_path('reports/trial-contexts.json')).write_text(json.dumps(dict(counts=counts,contexts=audit),ensure_ascii=False,indent=2)+'\n')
 print('trial effects:',len(audit),'source contexts')
if __name__=='__main__':generate()
