#!/usr/bin/env python3
"""Compile ID-localized story JSON to versioned, independently compressed VM blocks."""
from build_paths import generated_path
from collections import Counter, defaultdict, deque
from pathlib import Path
import json
import hashlib
import re
import struct
from story_layout import clean_text, dialogue_text, write_runtime_header, catalog, report as layout_report
from trial_story import TrialCatalog,compile_trial,TRIAL_OPS,ACTORS,toast_command,printer_command
from story_music import MusicCatalog
from story_sfx import SfxCatalog
from story_text_color import color_spans, COLORS

GBA = Path(__file__).resolve().parents[1]
ROOT = GBA.parent
SOURCE = ROOT / 'story/zh-Hans'
BLOCK = 4096
GLYPHS = set()
OPS = dict(say=1, jump=2, branch=3, menu=4, flags=5, flag=6,
           backdrop=7, character=8, music=9, pause=10, end=11, visual=12, stage=13, sfx=32, toast=33, reenact=34, screen=35, credits=36, execution=37, map=38)
OPS.update(TRIAL_OPS)
# Display labels only; actor IDs and original appearances remain in the source JSON.
SPEAKERS = {'': '旁白', 'Ema': '艾玛', 'EmaFake': '艾玛', 'Hiro': '希罗',
            'Sherry': '雪莉', 'AnAn': '安安', 'Coco': '可可', 'Hanna': '汉娜',
            'Noah': '诺亚', 'Leia': '蕾雅', 'Miria': '米莉亚', 'Meruru': '梅露露',
            'Margo': '玛格', 'Nanoka': '奈叶香', 'Alisa': '亚里沙', 'Yuki': '小雪',
            'Unknown': '？？？', 'Warden': '典狱长', 'Jailer': '看守',
            'Girl': '少女', 'Rabbit': '兔子', 'BigWitch': '大魔女',
            'StrayNarehate': '怪物', 'WitchCandidate1': '魔女候选人甲',
            'WitchCandidate2': '魔女候选人乙', 'WitchCandidate3': '魔女候选人丙', 'CreatureHiro':'希罗'}
AREAS = {'BasementHallway':'地下走廊', 'Lake':'湖边', 'Lounge':'休息室',
         'EntranceHall':'玄关大厅', 'Infirmary':'医务室', 'MessHall':'餐厅',
         'RecreationRoom':'娱乐室', 'Showers':'淋浴室', 'Library':'图书室',
         'SolitaryConfinement':'禁闭室', 'FrontGuesthouse':'房舍前',
         'ChamberSalamander':'火之室', 'ChamberUndine':'水之室', 'ChamberGnome':'土之室',
         'Warehouse':'仓库', 'Courtroom':'审判庭', 'CourtroomHallway':'审判庭走廊',
         'UnknownArea':'未知区域', 'NoahStudio':'诺亚的工作室', 'Courtyard':'中庭',
         'PrisonEntrance':'牢狱入口', 'Wall':'围墙', 'FlowerFields':'花田'}
SKIP = set('Wait WaitForNextFrame AnimateActorExtended ResetText HidePrinter SpawnExtended '
           'SpawnBlur DestroySpawnedExtended ChangeActorMaskability UpdateWitchBook ModifyCamera '
           'ModifyTextPrinter QuickSave RecordCorrectChoice ShowUI ProcessInput HideUI '
           'SpawnShake Skip EmoteJumpDouble EmoteJump StopSfx ChangeActorPivot HideCreditsUi '
           'EmoteShake ClearBacklog EmoteNod PlayVoice ShowActors PlaySfxModified GosubResetWitchBook PlayMovie'.split())


def plain(s):
    return clean_text(s)


def string(s, capacity=None):
    GLYPHS.update(s)
    data = s.encode('utf-8') + b'\0'
    assert '\0' not in s
    if capacity:
        assert len(data) <= capacity, (len(data), capacity, s)
    return data


def u16(n): return struct.pack('<H', n)
def u32(n): return struct.pack('<I', n)


def compress(data):
    """Small GBA LZ77 token stream, with size and codec in the block directory."""
    out = bytearray()
    chains = defaultdict(deque)
    pos = 0
    while pos < len(data):
        flag_pos = len(out)
        out.append(0)
        for bit in range(7, -1, -1):
            if pos == len(data): break
            candidates = chains[data[pos:pos+3]]
            while candidates and pos - candidates[0] > 4096:
                candidates.popleft()
            best, distance = 0, 0
            for previous in reversed(candidates):
                length = 3
                while length < 18 and pos+length < len(data) and data[previous+length] == data[pos+length]:
                    length += 1
                if length > best:
                    best, distance = length, pos - previous
                if best == 18: break
            if best >= 3 and pos + 3 <= len(data):
                out[flag_pos] |= 1 << bit
                token = ((best - 3) << 12) | (distance - 1)
                out.extend(token.to_bytes(2, 'big'))
                count = best
            else:
                out.append(data[pos])
                count = 1
            for at in range(pos, pos+count):
                chains[data[at:at+3]].append(at)
            pos += count
    return bytes(out)


def decompress(data, size):
    out = bytearray()
    pos = 0
    while len(out) < size:
        flags = data[pos]; pos += 1
        for bit in range(7, -1, -1):
            if len(out) == size: break
            if flags & (1 << bit):
                token = int.from_bytes(data[pos:pos+2], 'big'); pos += 2
                distance = (token & 4095) + 1
                for _ in range((token >> 12) + 3): out.append(out[-distance])
            else:
                out.append(data[pos]); pos += 1
    assert pos == len(data)
    return bytes(out)


class Compiler:
    def __init__(self, docs):
        self.docs = docs
        self.ids = {d['path']: i for i, d in enumerate(docs)}
        self.speakers = list(SPEAKERS)
        self.areas = list(AREAS)
        self.relocations = []
        self.text_colors = []
        self.labels = {}
        self.report = Counter()
        self.ops=OPS
        self.trials=TrialCatalog(docs,self.speakers)
        self.music=MusicCatalog(docs)
        self.sfx=SfxCatalog()
        for item in self.trials.items:
            if item['kind']==2:item['label']=AREAS.get(item['id'],item['id'])
        catalog_path=GBA/'assets/graphics/catalog.json'
        self.graphics={}
        for entry in json.loads(catalog_path.read_text())['chapters']:
            source=SOURCE/(entry['path']+'.json')
            assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['source_sha256'], 'Graphical chapter changed; run make graphics-import and graphics-link'
            self.graphics[entry['path']]=entry

    def colored_text(self, doc, event, text, first, end):
        style = color_spans(event['params'].get('Text', ''), text)
        if style:
            color, spans = style
            self.text_colors.append(dict(script=self.ids[doc['path']], pc=first, end=end,
                color=color, spans=spans, path=doc['path'], line=event['line'],
                text_ids=event.get('text_ids', {}).get('Text', []), text=text))

    def target(self, path, current):
        if '.' in path:
            script, label = path.rsplit('.', 1)
            return (script or current, label)
        return path, ''

    def compile(self, doc):
        if re.search(r'_Trial\d+$',doc['path']) or doc.get('_trial'):
            return compile_trial(self,doc,string)
        events, path = doc['events'], doc['path']
        out = bytearray()
        event_pc = {}
        patches = []
        def emit(op, payload=b''):
            assert len(payload) < 65536
            out.extend(bytes([OPS[op]]) + u16(len(payload)) + payload)
            self.report[op] += 1
        def destination(target, offset):
            script, label = self.target(target, path)
            if script not in self.ids:
                return None
            patches.append((offset, (script, label)))
            return u16(self.ids[script]) + u32(0)
        def block_end(index):
            indent = events[index]['indent']
            index += 1
            while index < len(events) and events[index]['indent'] > indent:
                index += 1
            return index
        i = 0
        visual_index = 0
        graphics=self.graphics.get(path)
        while i < len(events):
            e = events[i]; op = e['op']; p = e.get('params', {})
            event_pc[i] = len(out)
            if op=='ShowUI' and 'ExecutionButton' in p.get('UINames',[]):
                end=next(j for j in range(i+1,len(events)) if events[j]['op']=='label' and events[j]['name']=='ExecutionConfirmed')
                for j in range(i,end):
                    event_pc[j]=len(out)
                    if graphics and str(events[j]['line']) in graphics['event_lines']:emit('stage',u16(graphics['event_lines'][str(events[j]['line'])]))
                emit('execution');i=end;continue
            if op == 'ShowCreditsUi':
                act=int(p['ActNumber'])
                end=next(j for j in range(i+1,len(events)) if events[j]['op']=='label' and events[j]['name']==f'EndCredits{act}')
                # The removed slideshow contains a real profile update after ShowCreditsUi.
                for j in range(i+1,end):
                    event_pc[j]=len(out)
                    update=self.trials.book.command(path,events[j])
                    if update:emit(*update)
                    if events[j]['op']=='label':self.labels[(path,events[j]['name'])]=len(out)
                emit('credits',bytes([act]));i=end;continue
            if graphics and str(e['line']) in graphics['event_lines']:
                emit('stage', u16(graphics['event_lines'][str(e['line'])]))
            book_command = self.trials.book.command(path, e)
            if book_command:
                emit(*book_command)
            elif op == 'ShowToastUI':
                emit('toast', toast_command(p))
            elif op == 'label':
                self.labels[(path, e['name'])] = len(out)
            elif op == 'PrintTextModified':
                if graphics:
                    assert visual_index < len(graphics['source_lines']) and graphics['source_lines'][visual_index] == e['line'], 'Graphical cues changed; run make -C gba images'
                    emit('visual', u16(visual_index))
                    visual_index += 1
                text = dialogue_text(p.get('Text', ''), path)
                assert text, (path, e)
                author = p.get('AuthorId', '')
                self.colored_text(doc,e,text,len(out)+4,len(out)+5+len(text.encode()))
                emit('say', bytes([self.speakers.index(author)]) + string(text, 384))
            elif op == 'GotoModified':
                target = p['Path']
                dest = destination(target, len(out)+3)
                if dest is None:
                    message = '审判部分尚未实现。' if '_Trial' in target else '后续系统尚未实现。'
                    emit('end', string(message + '\n' + target.split('/')[-1], 384))
                    self.report['external_transitions'] += 1
                else:
                    emit('jump', dest)
            elif op in {'BeginIf', 'BeginIfCurrentLanguage', 'Else'}:
                end = block_end(i)
                if op == 'Else':
                    # Else is reached only after the successful preceding block.
                    emit('branch', b'\0' + u16(0) + u32(0))
                else:
                    expr = p['Expression']
                    if op == 'BeginIfCurrentLanguage':
                        mode, value = 0, int((p['LanguageName'] == 'zh-Hans') != bool(p.get('Unless', 0)))
                    elif expr == 'isDebugDirectJump': mode, value = 3, 0
                    elif expr == '!enableDebugSkip': mode, value = 0, 1
                    elif match := re.fullmatch(r'chosenAreaFlag\s*==\s*(\d+)', expr):
                        mode, value = 1, int(match[1])
                    elif match := re.fullmatch(r'chosenAreaId\s*==\s*"(\w+)"', expr):
                        mode, value = 2, self.areas.index(match[1]) + 1
                    else: raise ValueError((path, e))
                    if end < len(events) and events[end]['op'] == 'Else': end += 1
                    emit('branch', bytes([mode]) + u16(value) + u32(0))
                patches.append((len(out)-4, end))
            elif op == 'AddChoice':
                options = []
                start = i
                while i < len(events) and events[i]['op'] != 'StopForChoice':
                    if events[i]['op'] == 'label':
                        i += 1; continue
                    choice = events[i]
                    assert choice['op'] == 'AddChoice', (path, choice)
                    j = block_end(i)
                    body = [c for c in events[i+1:j] if c['op'] != 'label']
                    assert len(body) == 1 and body[0]['op'] == 'GotoModified', (path, body)
                    text = plain(choice['params']['ChoiceSummary'])
                    assert text, (path, choice)
                    options.append((text, body[0]['params']['Path'], choice['params'].get('ButtonPath') == 'ChoiceButtons/Adv/Bad'))
                    i = j
                assert i < len(events) and 1 <= len(options) <= 8, (path, start)
                payload = bytearray([0, len(options)])
                for text, target, bad in options:
                    offset = len(out)+3+len(payload)+1
                    payload.append(int(bad))
                    dest = destination(target, offset)
                    assert dest is not None, (path, target)
                    payload.extend(dest + string(text, 160))
                emit('menu', payload)
            elif op == 'ChoiceMapArea':
                areas = p['MapAreaNames']
                assert p['FlagVariableName'] == 'chosenAreaFlag'
                assert 1 <= len(areas) <= 8
                payload = bytearray([1, len(areas)])
                for index, area in enumerate(areas):
                    # bit index, area ID, display text; continuation follows the menu.
                    payload.extend(bytes([index, self.areas.index(area)+1]) + string(AREAS[area], 160))
                emit('menu', payload)
            elif op == 'SetCustomVariableAsFlag':
                assert p['VariableName'] == 'chosenAreaFlag'
                emit('flag', bytes([p['Number']]))
            elif op == 'SetCustomVariable':
                expr = p['Expression']
                if expr == 'chosenAreaFlag = 0': emit('flags', u16(0))
                else:
                    assert re.fullmatch(r'(g_gameProgress|g_moviePlayed) = max\(\d+, \1\)|hasWitchBook = (true|false)|bgmUnitDuration = 60 / 88', expr), expr
            elif op == 'ModifyBackgroundExtended':
                if p.get('SubId') != 'Overlay':
                    appearance = p.get('AppearanceAndTransition', p.get('Appearance', ''))
                    if isinstance(appearance, str) and appearance:
                        emit('backdrop', string(appearance, 96))
            elif op == 'ModifyCharacterExtended':
                appearance = p.get('IdAndAppearance', p.get('Id', ''))
                assert isinstance(appearance, str)
                if appearance: emit('character', string(appearance.split(',', 1)[0], 96))
            elif op in {'HideAllCharacters', 'HideAllActors'}:
                emit('character', string(''))
            elif op == 'HideActors':
                if any(a.split('-')[0] in self.speakers for a in p['ActorIds']):
                    emit('character', string(''))
            elif op=='ModifyTextPrinter':
                emit('trial',printer_command(p))
            elif op in ('PlaySfxModified','StopSfx'):
                emit('sfx',self.sfx.command(doc,e))
            elif op in ('PlayBgm','StopBgm'):
                emit('music',self.music.command(doc,e))
            elif op == 'Stop':
                emit('pause', string('事件操作占位：按 A 继续。', 384))
            elif op == 'ProcessBadEnd': emit('bad_end', string('坏结局 ' + str(p['Number']), 384))
            elif op == 'ReturnToTitle': emit('bad_end' if path.startswith('Common/CommonBad') else 'end', string('故事结束。', 384))
            elif op == 'Gosub':
                assert p['Path'] in {'System/System_Subroutine.BeginAdv', 'System/System_Subroutine.EndAdv'}, p
            elif op == 'StopForChoice':
                assert any(e['op'] == 'ChoiceMapArea' for e in events), path
            elif op not in SKIP: raise ValueError((path, e))
            i += 1
        if graphics:
            assert visual_index == len(graphics['source_lines']), 'Graphical cues changed; run make -C gba images'
        event_pc[len(events)] = len(out)
        emit('end', string('本段剧本结束。'))
        self.labels[(path, '')] = 0
        for offset, target in patches:
            if isinstance(target, int): out[offset:offset+4] = u32(event_pc[target])
            else: self.relocations.append((out, offset, target))
        return out

    def link(self):
        for data, offset, target in self.relocations:
            data[offset:offset+6] = u16(self.ids[target[0]]) + u32(self.labels[target])


def load_documents():
    docs = [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Act*/*.json'))
            if re.search(r'_(Adv|Bad)\d+\.json$', p.name)]
    docs += [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Act*/*Trial*.json')) if re.search(r'_Trial\d+\.json$',p.name)]
    from trial_demo_story import document,presentation_document
    docs.extend((document(),presentation_document()))
    selectable_count=len(docs)
    docs += [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Common/CommonBad*.json'))]
    assert docs
    assert len({d['path'] for d in docs}) == len(docs)
    for doc in docs:
        assert (doc['format'], doc['version'], doc['language']) == ('manosaba.story', 1, 'zh-Hans')
    return docs,selectable_count


def main():
    docs,selectable_count=load_documents()
    compiler = Compiler(docs)
    scripts = [compiler.compile(doc) for doc in docs]
    # History stores only 16-bit text offsets; unique utterances cannot overflow its index.
    capacity=int(re.search(r'story_history_capacity\s*=\s*(\d+)',(GBA/'src/story_format.h').read_text())[1])
    for code in scripts:
        at=count=rows=0
        assert len(code)<65536
        while at<len(code):
            if code[at] in (OPS['say'],OPS['debate']):
                count+=1
                first=at+(4 if code[at]==OPS['say'] else 10)
                lines=code[first:].split(b'\0',1)[0].count(b'\n')+1
                assert lines<256
                rows+=lines+2
            at+=3+int.from_bytes(code[at+1:at+3],'little')
        assert count<=capacity and rows<65536
    compiler.link()
    from trial_effects import entry_states,cpp_entries
    effect_seeds,effect_report=entry_states(docs,scripts,OPS)
    blocks, entries, blob = [], [], bytearray()
    for doc, script in zip(docs, scripts):
        first = len(blocks)
        for start in range(0, len(script), BLOCK):
            raw = bytes(script[start:start+BLOCK]); packed = compress(raw)
            assert decompress(packed, len(raw)) == raw
            codec = int(len(packed) < len(raw))
            data = packed if codec else raw
            blocks.append((len(blob), len(data), codec))
            blob.extend(data)
        title = doc.get('title') or doc['path'].split('/')[-1].replace('Act', '').replace('_Chapter', '-').replace('_', ' ')
        mode=3 if doc.get('_trial_fixture') else 2 if doc.get('_demo') else int(bool(re.search(r'_Trial\d+$',doc['path'])))
        entries.append((doc['path'], title, len(script), first, len(blocks)-first,compiler.graphics.get(doc['path'],{}).get('bank',65535),mode))
    assert len(blocks) < 65536
    q = lambda s: json.dumps(s, ensure_ascii=False)
    lines = ['// Generated by tools/story.py. Do not edit.', '#include "story_format.h"', '#include "witch_book_data.h"',
             'namespace manosaba {', 'static_assert(story_format_version == 3);', 'extern const uint8_t story_bytes[] = {']
    lines += [','.join(str(b) for b in blob[i:i+32]) + ',' for i in range(0, len(blob), 32)]
    lines += ['};', 'extern const story_block story_blocks[] = {']
    lines += ['{' + ','.join(map(str, b)) + '},' for b in blocks]
    lines += ['};', 'extern const story_script story_scripts[] = {']
    lines += ['{' + ','.join([q(e[0]), q(e[1]), *map(str, e[2:])]) + '},' for e in entries]
    lines += ['};', f'extern const int story_script_count = {len(entries)};',
              f'extern const int story_selectable_count = {selectable_count};',
              f'extern const int story_trial_first = {next(i for i,e in enumerate(entries) if e[-1]==1)};',
              f'extern const int story_trial_count = {sum(e[-1]==1 for e in entries)};',
              'extern const char* const story_speakers[] = {']
    lines += [q(SPEAKERS[s]) + ',' for s in compiler.speakers]
    lines += ['};','extern const story_item story_items[] = {','{ "", "", 0 },']
    lines += ['{'+q(x['id'])+','+q(x['label'])+','+str(x['kind'])+'},' for x in compiler.trials.items]
    lines += ['};',f'extern const int story_item_count={len(compiler.trials.items)+1};']
    lines += ['const uint16_t story_common_bad_scripts[13]={'+','.join(str(compiler.ids[f'Common/CommonBad{i:02}']) for i in range(1,14))+'};']
    lines += ['extern const char* const story_music_aliases[]={'+','.join(q(s) for s in compiler.music.aliases)+'};']
    def book_string(value):
        GLYPHS.update(value)
        return q(value)
    spans = []
    lines += ['extern const story_text_style story_text_styles[] = {']
    for style in compiler.text_colors:
        lines += ['{'+','.join(map(str,(style['script'],style['pc'],style['end'],len(spans),len(style['spans']),style['color'])))+'},']
        spans.extend(style['spans'])
    lines += ['};', f'extern const int story_text_style_count={len(compiler.text_colors)};',
              'extern const story_color_span story_color_spans[] = {']
    lines += ['{'+','.join(map(str,span))+'},' for span in spans]
    lines += ['};', 'extern const uint16_t story_text_colors[]={'+','.join(str((int(c[1:3],16)>>3)|((int(c[3:5],16)>>3)<<5)|((int(c[5:7],16)>>3)<<10)) for c in COLORS)+'};']
    (generated_path('reports/dialogue-colors.json')).write_text(json.dumps(dict(colors=COLORS,contexts=compiler.text_colors),ensure_ascii=False,indent=2)+'\n')
    lines += compiler.trials.book.cpp(book_string)+cpp_entries(effect_seeds)+['}']
    (generated_path('reports/trial-effect-entries.json')).write_text(json.dumps(dict(
        policy='Normal jumps keep live state; direct entry uses agreed reaching values; undamped shaking restarts its interval; transient impacts are not replayed.',
        unique_entries=len(set(effect_seeds)),entries=effect_report),ensure_ascii=False,indent=2)+'\n')
    (GBA / 'src/story_data.cpp').write_text('\n'.join(lines) + '\n')
    report = dict(version=3, scripts=len(scripts), blocks=len(blocks), block_bytes=BLOCK,
                  raw_bytes=sum(map(len, scripts)), stored_bytes=len(blob),
                  directory_bytes=len(blocks)*8+len(entries)*16,
                  index_string_bytes=sum(len(string(e[0]))+len(string(e[1])) for e in entries) +
                                     sum(len(string(SPEAKERS[s])) for s in compiler.speakers),
                  instructions=compiler.report, untranslated_story_references=0)
    (generated_path('reports/story.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    (generated_path('reports/sfx-contexts.json')).write_text(json.dumps(dict(contexts=compiler.sfx.contexts),ensure_ascii=False,indent=2)+'\n')
    (generated_path('reports/music-events.json')).write_text(json.dumps(dict(aliases=compiler.music.aliases,contexts=compiler.music.contexts),ensure_ascii=False,indent=2)+'\n')
    (generated_path('reports/text-layout.json')).write_text(json.dumps(layout_report(catalog()), ensure_ascii=False, indent=2)+'\n')
    write_runtime_header()
    GLYPHS.update(''.join(SPEAKERS.values()) + ''.join(AREAS.values()))
    GLYPHS.update(''.join(x['label'] for x in compiler.trials.items))
    (generated_path('reports/trial.json')).write_text(json.dumps(dict(stats=compiler.trials.stats,variables=compiler.trials.variables,contexts=compiler.trials.contexts,screen_contexts=compiler.trials.screen_contexts),ensure_ascii=False,indent=2)+'\n')
    from trial_contexts import generate
    generate()
    (GBA / 'src/story_glyphs.txt').write_text(''.join(sorted(GLYPHS)), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__': main()
