#!/usr/bin/env python3
"""Merge original Naninovel command metadata and ID-based Chinese localization."""
import argparse
from collections import Counter
import json
from pathlib import Path
from output_paths import report_path
from export_naninovel import ROOT, DEFAULT_GAME, STREAMING, read_ja_scripts, parse_zh

OUT = ROOT / 'story/zh-Hans'


def scalar(value):
    if isinstance(value, list):
        return [scalar(v) for v in value]
    if isinstance(value, dict):
        if 'name' in value and 'value' in value:
            a, b = scalar(value['name']), scalar(value['value'])
            return a + ('.' + b if b else '') if isinstance(b, str) else {'name': a, 'value': b}
        if 'value' in value:
            return scalar(value['value'])
    return value


def merge(tree, translated, missing):
    refs = {v['rid']: v for v in tree['references']['RefIds']}
    events = []

    def command(ref):
        entry = refs[ref['rid']]
        data = entry['data']
        event = dict(op=entry['type']['class'], line=data['playbackSpot']['lineIndex'] + 1,
                     indent=data.get('indent', 0), params={})
        ids = {}
        for name, param in data.items():
            if not isinstance(param, dict) or not param.get('hasValue'):
                continue
            parts = param.get('raw', {}).get('parts', [])
            keys = [p['id'] for p in parts if p.get('id')]
            if keys and name not in {'Path', 'GotoPath', 'GosubPath'}:
                chunks = []
                for part in parts:
                    key = part.get('id')
                    if key:
                        text = translated.get(key, '')
                        if not text.strip():
                            missing.append(dict(script=tree['path'], line=event['line'], id=key))
                        chunks.append(text)
                    elif part.get('expression'):
                        chunks.append('{' + part['expression'] + '}')
                    else:
                        chunks.append(part.get('text', ''))
                event['params'][name] = ''.join(chunks)
                ids[name] = keys
            elif any(p.get('expression') for p in parts):
                event['params'][name] = {'expression': ''.join(
                    '{' + p['expression'] + '}' if p.get('expression') else p.get('text', '')
                    for p in parts)}
            else:
                event['params'][name] = scalar(param['value'])
        if ids:
            event['text_ids'] = ids
        events.append(event)

    for line_index, ref in enumerate(tree['lines']):
        line = refs[ref['rid']]
        kind, data = line['type']['class'], line['data']
        if kind == 'LabelScriptLine':
            events.append(dict(op='label', name=data['labelText'], line=line_index + 1,
                               indent=data.get('indent', 0)))
        elif kind == 'CommandScriptLine':
            command(data['command'])
        elif kind == 'GenericTextScriptLine':
            for inline in data.get('inlinedCommands', []):
                command(inline)
    return dict(format='manosaba.story', version=1, language='zh-Hans', path=tree['path'], events=events,
                translations=translated)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trees', type=Path, help='Optional cached Unity trees for extraction development')
    args = parser.parse_args()
    trees = json.loads(args.trees.read_text()) if args.trees else read_ja_scripts(DEFAULT_GAME / STREAMING)[0]
    missing = []
    counts = Counter()
    for tree in sorted(trees, key=lambda t: t['path']):
        path = tree['path']
        doc_path, entries = parse_zh((ROOT / 'original_scripts/zh-Hans' / (path + '.nani')).read_text(encoding='utf-8-sig'))
        assert doc_path == path
        assert len(dict(entries)) == len(entries), path
        doc = merge(tree, dict(entries), missing)
        target = OUT / (path + '.json')
        target.parent.mkdir(parents=True, exist_ok=True)
        # One event per line keeps scripts readable without expanding every parameter.
        head = {k: v for k, v in doc.items() if k not in {'events', 'translations'}}
        content = json.dumps(head, ensure_ascii=False, indent=2)[:-2] + ',\n  "events": [\n'
        content += ',\n'.join('    ' + json.dumps(e, ensure_ascii=False) for e in doc['events'])
        content += '\n  ],\n  "translations": ' + json.dumps(doc['translations'], ensure_ascii=False, indent=2) + '\n}\n'
        target.write_text(content, encoding='utf-8')
        counts['scripts'] += 1
        counts['events'] += len(doc['events'])
        counts['translated_entries'] += sum(bool(v.strip()) for _, v in entries)
    report = dict(format='manosaba.story', version=1, **counts, missing_translations=missing)
    report_path('story-merge.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(dict(counts), 'untranslated references:', len(missing))


if __name__ == '__main__':
    main()
