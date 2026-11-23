"""Embed converter metadata from prepared compiler outputs, without reports or a ROM."""
from pathlib import Path
import json,re
from story import decompress

GBA=Path(__file__).resolve().parents[1]

def generate():
    source=(GBA/'src/story_data.cpp').read_text()
    body=lambda name:re.search(name+r'\[\]\s*=\s*\{(.*?)\};',source,re.S)[1]
    blob=bytes(map(int,re.findall(r'\d+',body('story_bytes'))))
    blocks=[tuple(map(int,row.split(','))) for row in re.findall(r'\{([\d,]+)\}',body('story_blocks'))]
    scripts=[tuple(map(int,re.search(r',([\d,]+)\},?$',row)[1].split(',')))
             for row in body('story_scripts').splitlines() if row.strip()]
    maps=[];curtains=[]
    for index,(size,first,count,bank,mode) in enumerate(scripts):
        raw=bytearray()
        for offset,length,codec in blocks[first:first+count]:
            data=blob[offset:offset+length];raw.extend(decompress(data,min(4096,size-len(raw))) if codec else data)
        at=0
        while at<len(raw):
            end=at+3+int.from_bytes(raw[at+1:at+3],'little')
            if raw[at]==4 and raw[at+3]==1:maps.append([index,end,at])
            if raw[at]==34 and raw[at+3]<2:curtains.append([index,end,int(raw[at+3]==1)])
            at=end
    signature=int(re.search(r'save_content_signature=(\d+)',(GBA/'src/save_ui_data.cpp').read_text())[1])
    sfx=(GBA/'src/sfx_data.cpp').read_text().split('const sfx_cue sfx_cues[]={',1)[1].split('};',1)[0]
    ambient=[int(row.rsplit(',',3)[1]) for row in sfx.splitlines() if row.strip()]
    data=dict(signature=signature,mapAnchors=maps,curtainChanges=curtains,scriptSizes=[s[0] for s in scripts],scriptModes=[s[4] for s in scripts],sfxAmbient=ambient)
    header='''// Generated from prepared story bytecode by save_converter_assets.py.
#pragma once
#include <cstdint>
namespace manosaba {
struct legacy_map_anchor {uint16_t script,end,start;};
struct legacy_curtain_change {uint16_t script,end;bool closed;};
'''
    header+='inline constexpr legacy_map_anchor legacy_map_anchors[]={'+','.join('{'+','.join(map(str,row))+'}' for row in maps)+'};\n'
    header+='inline constexpr legacy_curtain_change legacy_curtain_changes[]={'+','.join('{'+','.join(map(str,row))+'}' for row in curtains)+'};\n}\n'
    path=GBA/'src/save_legacy_data.h'
    if not path.exists() or path.read_text()!=header:path.write_text(header)
    html=GBA/'tools/save-converter.html';text=html.read_text()
    replacement='/* SAVE_CONVERTER_DATA_BEGIN */\nconst SAVE_CONTENT='+json.dumps(data,separators=(',',':'))+';\n/* SAVE_CONVERTER_DATA_END */'
    text=re.sub(r'/\* SAVE_CONVERTER_DATA_BEGIN \*/.*?/\* SAVE_CONVERTER_DATA_END \*/',lambda _:replacement,text,flags=re.S)
    html.write_text(text)
    print(f'converter metadata: {len(scripts)} scripts, {len(maps)} map anchors, {len(ambient)} SFX')

if __name__=='__main__':generate()
