#!/usr/bin/env python3
"""Independent source-level interpreter used to check the compiled C++ VM."""
import json
from pathlib import Path
import re
import sys
from story import SOURCE, plain, SPEAKERS, AREAS
from story_layout import dialogue_text


def traces(include_music=False):
    docs = [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Act*/*.json'))
            if re.search(r'_(Adv|Bad)\d+\.json$', p.name)]
    docs += [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Act*/*Trial*.json')) if re.search(r'_Trial\d+\.json$',p.name)]
    from trial_demo_story import document,presentation_document
    docs.extend((document(),presentation_document()))
    docs += [json.loads(p.read_text()) for p in sorted(SOURCE.glob('Common/CommonBad*.json'))]
    ids = {d['path']: i for i, d in enumerate(docs)}
    labels = {(d['path'], e['name']): i for d in docs for i, e in enumerate(d['events']) if e['op'] == 'label'}
    for start in range(len(docs)):
        if not (re.search(r'_(Adv|Bad)\d+$',docs[start]['path']) or docs[start]['path'].startswith('Common/CommonBad')):continue
        for strategy in (0, 1):
            script, pc, flags, area, direct = start, 0, 0, '', True
            count, hash_value = 0, 2166136261
            def record(kind, text='', options=(), speaker='', bad=()):
                nonlocal hash_value, count
                data = bytes([kind]) + script.to_bytes(2, 'little') + flags.to_bytes(2, 'little')
                data += text.encode() + b'\0'
                if kind == 1: data += speaker.encode() + b'\0'
                for index,option in enumerate(options): data += bytes([bool(bad and bad[index])]) + option.encode() + b'\0'
                for b in data: hash_value = ((hash_value ^ b) * 16777619) & 0xffffffff
                count += 1
            def end_block(at):
                end = at+1
                while end < len(events) and events[end]['indent'] > events[at]['indent']: end += 1
                return end
            def go(target):
                nonlocal script, pc, direct
                if '.' in target: path, label = target.rsplit('.', 1); path = path or docs[script]['path']
                else: path, label = target, ''
                if re.search(r'_Trial\d+$',path):
                    # End this normal-story trace at the now-playable trial boundary.
                    nonlocal hash_value,count
                    for b in b'\x05'+path.encode()+b'\0':hash_value=((hash_value^b)*16777619)&0xffffffff
                    count+=1
                    return False
                if path not in ids:
                    message = '审判部分尚未实现。' if '_Trial' in target else '后续系统尚未实现。'
                    record(4, message+'\n'+target.split('/')[-1])
                    return False
                new_script = ids[path]
                if new_script != script: direct = False
                script = new_script
                pc = labels[path, label] if label else 0
                return True
            def music(event):
                nonlocal hash_value,count
                import struct
                p=event['params'];stop=event['op']=='StopBgm'
                keys=('BgmPath','Volume','Loop','IntroBgmPath','FadeOutDuration' if stop else 'Duration','FadeInDuration')
                mask=sum(1<<bit for bit,key in enumerate(keys) if key in p)
                frame=lambda value:round(float(value)*16777216/280896)
                data=bytes([6])+script.to_bytes(2,'little')+flags.to_bytes(2,'little')
                data+=p.get('BgmPath','').encode()+b'\0'+p.get('IntroBgmPath','').encode()+b'\0'
                data+=struct.pack('<4H3B',round(p.get('Volume',1)*1024),frame(p.get(keys[4],.35)),
                                  frame(p.get('FadeInDuration',0)),mask,bool(p.get('Loop',True)),bool(p.get('Wait',False)),stop)
                for b in data:hash_value=((hash_value^b)*16777619)&0xffffffff
                count+=1
            for _ in range(150000):
                events = docs[script]['events']
                if pc == len(events): record(4, '本段剧本结束。'); break
                e = events[pc]; p = e.get('params', {}); op = e['op']; old_pc = pc; pc += 1
                if op == 'PrintTextModified': record(1, dialogue_text(p['Text'],docs[script]['path']), speaker=SPEAKERS[p.get('AuthorId', '')])
                elif op=='ShowUI' and 'ExecutionButton' in p.get('UINames',[]):
                    record(4);pc=labels[docs[script]['path'],'ExecutionConfirmed']
                elif include_music and op in ('PlayBgm','StopBgm'):music(e)
                elif op == 'GotoModified':
                    if not go(p['Path']): break
                elif op in {'BeginIf', 'BeginIfCurrentLanguage'}:
                    expr = p['Expression']
                    if op == 'BeginIfCurrentLanguage': condition = (p['LanguageName']=='zh-Hans') != bool(p.get('Unless'))
                    elif expr == 'isDebugDirectJump': condition = direct
                    elif expr == '!enableDebugSkip': condition = True
                    elif m := re.fullmatch(r'chosenAreaFlag\s*==\s*(\d+)', expr): condition = flags == int(m[1])
                    elif m := re.fullmatch(r'chosenAreaId\s*==\s*"(\w+)"', expr): condition = area == m[1]
                    else: raise AssertionError(expr)
                    if not condition:
                        pc = end_block(old_pc)
                        if pc < len(events) and events[pc]['op']=='Else': pc += 1
                elif op == 'Else': pc = end_block(old_pc)
                elif op == 'AddChoice':
                    options = []
                    pc = old_pc
                    while events[pc]['op'] != 'StopForChoice':
                        if events[pc]['op'] == 'label': pc += 1; continue
                        choice = events[pc]
                        end = end_block(pc)
                        target = next(c['params']['Path'] for c in events[pc+1:end] if c['op']=='GotoModified')
                        options.append((plain(choice['params']['ChoiceSummary']), target, choice['params'].get('ButtonPath')=='ChoiceButtons/Adv/Bad'))
                        pc = end
                    pc += 1
                    record(2, options=[x[0] for x in options],bad=[x[2] for x in options])
                    if not go(options[-1 if strategy else 0][1]): break
                elif op == 'ChoiceMapArea':
                    options = [name for bit, name in enumerate(p['MapAreaNames']) if not flags & (1<<bit)]
                    if options:
                        record(2, options=[AREAS[name] for name in options])
                        area = options[-1 if strategy else 0]
                elif op == 'SetCustomVariableAsFlag': flags |= 1 << p['Number']
                elif op == 'SetCustomVariable' and p['Expression']=='chosenAreaFlag = 0': flags = 0
                elif op == 'ShowCreditsUi':
                    record(4)
                    pc=labels[docs[script]['path'],f"EndCredits{int(p['ActNumber'])}"]
                elif op == 'Stop': record(3, '事件操作占位：按 A 继续。')
                elif op == 'ProcessBadEnd': record(4, '坏结局 '+str(p['Number'])); break
                elif op == 'ReturnToTitle': record(4, '故事结束。'); break
            else: raise AssertionError(('source loop', start, strategy))
            yield start, strategy, count, hash_value


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(''.join('%d %d %d %d\n' % t for t in traces('--music' in sys.argv)))
