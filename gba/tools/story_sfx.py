"""Compile script SFX commands to the shared production sound catalog."""
from pathlib import Path
import json,struct
GBA=Path(__file__).resolve().parents[1]
class SfxCatalog:
    def __init__(self):
        self.cues=json.loads((GBA/'assets/sfx/manifest.json').read_text())['cues']
        self.ids={c['alias']:i for i,c in enumerate(self.cues)};self.contexts=[]
    def command(self,doc,event):
        p=event.get('params',{});stop=event['op']=='StopSfx';alias=p.get('SfxPath','')
        mapped='Sfx_12' if alias=='Sfx_87' else alias
        index=self.ids[mapped] if mapped else 65535
        volume=round(p.get('Volume',1)*1024);fade=round(p.get('FadeInDuration',0)*16777216/280896)
        self.contexts.append(dict(path=doc['path'],line=event['line'],op=event['op'],alias=alias,mapped=mapped,params=p))
        return struct.pack('<HHHBB',index,volume,fade,bool(p.get('Loop',False)),stop)
