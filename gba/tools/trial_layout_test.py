"""Audit every real debate layout, button cell and compiled link range."""
import json,re
from pathlib import Path
from trial_story import debate_layout
from story_layout import pixel_width,clean_text

def main():
    statements=links_seen=0
    for path in sorted((Path(__file__).resolve().parents[2]/'story/zh-Hans').glob('Act*/*Trial*.json')):
        position=[70,50]
        for event in json.loads(path.read_text())['events']:
            if event['op']!='PrintDebateText':continue
            source=event['params']['Text'];position=event['params'].get('ScenePosition',position)
            text,links,box=debate_layout(source,position);rows=text.split('\n')
            context=(path.name,event['line'])
            assert 12<=box[0]<box[2]<=228 and 22<=box[1]<box[3]<=150,context
            assert len(rows)<=8,context
            assert all(pixel_width(row)<=box[2]-box[0] for row in rows),context
            assert all(not(row and '\ue000'<=row[0]<='\ue002') for row in rows),context
            plain=lambda s:''.join(c for c in clean_text(s) if not c.isspace() and not '\ue000'<=c<='\ue002')
            assert plain(text)==plain(source),context
            original=list(re.finditer(r'<link\s*=\s*["\']([^"\']+)["\']\s*>(.*?)</link>',source,re.S))
            assert len(original)==len(links),context
            for i,((target,first,last),original_link) in enumerate(zip(links,original)):
                label=text.encode()[first:last].decode()
                assert target==original_link[1] and label.endswith(chr(0xe000+i)),context
                assert plain(label)==plain(original_link[2]),context
                if pixel_width(label.replace('\n',''))<=box[2]-box[0]:assert '\n' not in label,context
            statements+=1;links_seen+=len(links)
    print(f'PASS: {statements} source layouts, {links_seen} positioned links; safe bounds, no split short point/key, no lost text')
if __name__=='__main__':main()
