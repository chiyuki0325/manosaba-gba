"""Research pure-text ending rolls; generate source counts and pixel-font mockups.

This does not alter generated game assets or the runtime player.
"""
from build_paths import generated_path
from collections import Counter
from pathlib import Path
import json,re
import UnityPy
from PIL import Image
from font import read_font
from graphics_project import GBA,ROOT,ContentCache,digest

BUNDLES=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64'

def source(cache):
    bundle=BUNDLES/'naninovel-ui_assets_all.bundle'
    key=digest(dict(step='ending-credit-ui-research-v1',source=cache.file_key(bundle)))
    result=cache.get_json(key)
    if result:return result
    env=UnityPy.load(str(bundle));root=next(p for n,p in env.container.items() if n.endswith('CreditsUI.prefab'))
    result=dict(labels=[],directors=[])
    def visit(ptr,parent=''):
        go=ptr.read();path=parent+'/'+go.m_Name
        for component in go.m_Component:
            obj=component.component
            if obj.type.name=='RectTransform':
                for child in obj.read().m_Children:visit(child.read().m_GameObject,path)
            elif obj.type.name=='MonoBehaviour':
                data=obj.read_typetree()
                if data.get('m_text'):result['labels'].append(dict(path=path,text=re.sub('<[^>]*>','',data['m_text']),color=data.get('m_fontColor')))
                if '_bgmBpm' in data:
                    result['directors'].append(dict(path=path,parameters={k:v for k,v in data.items() if k.startswith('_') and isinstance(v,(str,int,float))}))
    visit(root);cache.put_json(key,result);return result

def width(text):return sum(6 if ord(c)<128 else 12 for c in text)

def pack(names):
    rows=[];line='';used=0
    for name in names:
        size=width(name)
        if size>216:
            if line:rows.append(line);line='';used=0
            part='';pixels=0
            for ch in name:
                if pixels+width(ch)>216:rows.append(part);part='';pixels=0
                part+=ch;pixels+=width(ch)
            if part:rows.append(part)
        else:
            if line and used+12+size>216:rows.append(line);line='';used=0
            if line:line+='  ';used+=12
            line+=name;used+=size
    if line:rows.append(line)
    return rows

def main():
    cache=ContentCache();ui=source(cache)
    master=json.loads((GBA/'assets/source-metadata/trial.json').read_text())['master']
    thanks=master['SpecialThanksData']
    packed=[];groups=[];names=[]
    for group in thanks['_textSizeByCourses']:
        values=[next(t['_text'] for t in item['_name'] if t['_locale']==0) for item in thanks['_items'] if item['_course']==group['_course']]
        rows=pack(values);packed.extend(rows);names.extend(values)
        groups.append(dict(course=group['_course'],phase=group['_phase'],names=len(values),rows=len(rows)))
    titles=json.loads((GBA/'assets/ending-roll-titles-zh.json').read_text())
    characters={}
    for actor in master['AuthorData']['_items']:
        names_by_locale={v['_locale']:re.sub('<[^>]*>','',v['_text']) for v in actor['_taggedText']}
        if names_by_locale.get(0) and names_by_locale.get(2):characters[names_by_locale[0]]=names_by_locale[2]
    staff={act:[characters.get(r['text'],titles.get(r['text'],r['text'])) if r['path'].endswith('LabelLeft') else titles.get(r['text'],r['text']) for r in ui['labels'] if f'/{act}/' in r['path']] for act in ('Act1','Act2')}
    required={ord(c) for c in ''.join(names+staff['Act1']+staff['Act2']) if not c.isspace()}
    generated=(GBA/'src/font_data.c').read_text().split('font_codes[] = {')[1].split('};')[0]
    current={int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+',generated)};font=read_font()
    report=dict(scope='research only; no slides; continuing live music',directors=ui['directors'],
                source_staff_labels={k:len(v) for k,v in staff.items()},special_thanks_names=len(names),special_thanks_groups=groups,
                packed_width=216,gap_pixels=12,packed_thanks_rows=len(packed),line_height=14,
                proposed_normal_pixels_per_frame=.5,proposed_thanks_pixels_per_frame=2,
                thanks_seconds_at_60fps=len(packed)*14/120,
                required_glyphs=len(required),additional_glyphs=len(required-current),additional_font_bytes_estimate=len((required-current)&set(font))*28,
                fusion_missing_glyphs=[f'U+{v:04X}' for v in sorted(required-set(font))],
                missing_glyph_policy='No replacement or glyph extraction; keep existing blank glyph advance.',
                text_shadow=dict(offset=[1,1],rgb=[232,224,200],scope='all ending text'),select_input='ignored',
                packed_thanks_utf8_bytes=sum(len(s.encode())+1 for s in packed),raw_text_utf8_bytes=sum(len(s.encode())+1 for s in names+staff['Act1']+staff['Act2']),
                vram_proposal=dict(text_tiles=896,text_tile_bytes=896*32,map_bytes=2048,total_bytes=896*32+2048),
                cpu_canvas_existing_bytes=1200*32,cpu_canvas_reused_bytes=896*32,
                packed_thanks=packed)
    out=generated_path('reports/ending-roll-research.json');out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    design=generated_path('design/ending-roll');design.mkdir(parents=True,exist_ok=True)
    def draw(mode,lines):
        bg=(255,255,255)
        body=(60,18,22)
        accent=(242,80,84)
        image=Image.new('RGB',(240,160),bg)
        for y,text,heading in lines:
            x=(240-width(text))//2 if heading else 12
            points=[]
            for ch in text:
                size=6 if ord(ch)<128 else 12
                for dy,bits in enumerate(font.get(ord(ch),[0]*12)):
                    for dx in range(size):
                        if bits&(1<<(11-dx)):points.append((x+dx,y+dy))
                x+=size
            for px,py in points:
                if 0<=px+1<240 and 0<=py+1<160:image.putpixel((px+1,py+1),(232,224,200))
            for px,py in points:
                if 0<=px<240 and 0<=py<160:image.putpixel((px,py),accent if heading else body)
        image.save(design/f'{mode}-240x160.png')
        image.resize((720,480),Image.Resampling.NEAREST).save(design/f'{mode}-3x.png')
    draw('act1',[(8,'配音',True),(34,'樱羽艾玛  三木谷奈々',False),(52,'二阶堂希罗  東雲はる',False),(70,'橘雪莉  柊優花',False),(88,'城崎诺亚  井口裕香',False),(106,'夏目安安  葵あずさ',False),(124,'宝生玛格  樹冬華',False),(142,'冰上梅露露  山下七海',False)])
    draw('act2',[(5,'片尾曲',True),(25,'《Bloom》',True),(52,'作词·作曲·编曲',False),(70,'daiki torii',False),(100,'演唱',False),(118,'三木谷奈々',False)])
    draw('thanks',[(2,'特别鸣谢',True)]+[(27+i*14,line,False) for i,line in enumerate(packed[12:22])])
    print(json.dumps({k:v for k,v in report.items() if k not in ('packed_thanks','directors')},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
