"""Original book versions and reset subroutines, shared by both compilers."""
import json
import re
from pathlib import Path
from story_layout import clean_text, reflow

ROOT = Path(__file__).resolve().parents[2]
CATEGORIES = ('Clue', 'Profile', 'Map', 'Rule', 'Note')


def localized(values):
    return next((v['_text'] for v in values if v['_locale'] == 2), '')


class BookCatalog:
    def __init__(self, master, item):
        self.entries = []
        self.lookup = {}
        self.texts = []
        colors={x['name']:x['color'] for x in json.loads((ROOT/'gba/assets/nameplates.json').read_text())['characters']}
        characters = {v['_id']: localized(v['_familyName'])+localized(v['_name'])
                      for v in master['CharacterData']['_items']}
        for kind, category in enumerate(CATEGORIES):
            for row in master[category+'Data']['_items']:
                key = (category, row['_id'])
                if key not in self.lookup:
                    self.lookup[key] = len(self.entries)
                    self.entries.append(dict(id=row['_id'], kind=kind, first=len(self.texts), versions=0,
                                             story_id=item(row['_id']) if kind < 2 else 0,color=0))
                entry = self.entries[self.lookup[key]]
                assert row['_version'] == entry['versions']+1
                entry['versions'] += 1
                data = row['_item']
                if kind == 0:
                    name = localized(data['_name'])
                elif kind == 1:
                    name = characters.get(row['_id'], row['_id'])
                elif kind == 2:
                    name = localized(data['_buttonText']).replace('<br>', ' ')
                elif kind == 3:
                    name = data['_numbering']+' '+localized(data['_subtitle'])
                else:
                    name = localized(data['_title'])
                description = localized(data.get('_description', []))
                if kind==1:
                    rgb=colors.get(name,[208,200,192])
                    entry['color']=(rgb[0]>>3)|((rgb[1]>>3)<<5)|((rgb[2]>>3)<<10)
                self.texts.append(dict(name=clean_text(name).replace('\n', ' '),
                                       compact=reflow(description, 120), detail=reflow(description, 216)))
        assert len(self.entries) == 168
        events = json.loads((ROOT/'story/zh-Hans/System/System_ResetWitchBook.json').read_text())['events']
        self.labels = {e['name']: i+1 for i, e in enumerate(events) if e['op'] == 'label'}
        self.initial = []
        self.reset_ids = {}
        for label in self.labels:
            state = [0]*len(self.entries)
            self._run_reset(events, label, state)
            self.reset_ids[label] = len(self.initial)
            self.initial.append(state)

    def _run_reset(self, events, label, state):
        for e in events[self.labels[label]:]:
            op = e['op']; p = e.get('params', {})
            if op == 'Return':
                return
            if op == 'Gosub':
                self._run_reset(events, p['Path'].split('.')[-1], state)
            elif op == 'ClearWitchBook':
                state[:] = [0]*len(state)
            elif op == 'UpdateWitchBook':
                state[self.lookup[p['Category'], p['Id']]] = p['Version']
            else:
                raise ValueError(e)

    def command(self, path, event):
        op = event['op']; p = event.get('params', {})
        if op == 'UpdateWitchBook':
            return 'book_update', bytes([self.lookup[p['Category'], p['Id']], p['Version']])
        if op == 'ClearWitchBook':
            categories = p.get('CategoryNames', CATEGORIES)
            return 'book_clear', bytes([sum(1 << CATEGORIES.index(c) for c in categories)])
        if op == 'GosubResetWitchBook':
            match = re.match(r'Act(\d+)_Chapter(\d+)/', path)
            if not match:
                return None
            part = 'Trial'+str(p.get('Version', 1)) if '_Trial' in path else 'Adv'
            label = f'Reset{int(match[1])}-{int(match[2])}-{part}'
            return 'book_reset', bytes([self.reset_ids[label], int('IsDebugDirectJump' in p)])
        return None

    def cpp(self, string):
        rows = ['const book_entry book_entries[book_entry_count]={']
        for entry in self.entries:
            rows.append('{'+','.join([string(entry['id']), str(entry['story_id']), str(entry['first']),
                                      str(entry['versions']), str(entry['kind']),str(entry['color'])])+'},')
        rows += ['};', 'const book_text book_texts[]={']
        for entry in self.texts:
            rows.append('{'+','.join(string(entry[k]) for k in ('name', 'compact', 'detail'))+'},')
        rows += ['};', 'const uint8_t book_initial_states[][book_entry_count]={']
        rows += ['{'+','.join(map(str, state))+'},' for state in self.initial]
        chapters=[int(label[5])*16+int(label[7]) for label in self.reset_ids]
        return rows+['};','const uint8_t book_initial_chapters[]={'+','.join(map(str,chapters))+'};']
