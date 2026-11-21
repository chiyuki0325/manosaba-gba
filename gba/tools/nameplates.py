#!/usr/bin/env python3
from build_paths import generated_path
"""Bake pixel name labels with the original mixed-size Chinese lettering."""
from pathlib import Path
import json
import gzip
import struct
import re
from functools import lru_cache
import numpy as np
from PIL import Image
from font import read_font
from image_encoding import rgb555, tiles, UI_COLORS

ROOT=Path(__file__).resolve().parents[2]

def nameplate_reference(size=(52,25)):
    source=Image.open(ROOT/'manosaba-web/public/UI_Adv/NamePlateBase.png').convert('RGBA').resize(size,Image.Resampling.LANCZOS)
    rgba=np.asarray(source,dtype=np.int32);alpha=rgba[:,:,3:4]
    rgb=(rgba[:,:,:3]*alpha+np.array([24,24,28])*(255-alpha)+127)//255
    return rgb,rgba[:,:,3]>=48

@lru_cache(maxsize=1)
def nameplate_palette():
    rgb,mask=nameplate_reference();samples=rgb[mask]
    raw=json.loads((ROOT/'gba/assets/trial-ui-palette.json').read_text())['colors']
    pool=np.array([(v&31,(v>>5)&31,(v>>10)&31) for v in raw])*8
    distances=((samples[:,None,:]-pool[None,:,:])**2).sum(axis=2)
    errors=np.full(len(samples),10**9);chosen=[]
    for _ in range(4):
        k=int(np.minimum(errors[:,None],distances).sum(axis=0).argmin());chosen.append(k)
        errors=np.minimum(errors,distances[:,k])
    for _ in range(4):
        assignment=distances[:,chosen].argmin(axis=1)
        for i in range(4):
            members=assignment==i
            if members.any():chosen[i]=int(distances[members].sum(axis=0).argmin())
    return [tuple(map(int,c)) for c in pool[chosen]]

def nameplate_base(size=(52,25)):
    rgb,mask=nameplate_reference(size);palette=np.asarray(nameplate_palette())
    pixels=(((rgb[:,:,None,:]-palette[None,None,:,:])**2).sum(axis=3).argmin(axis=2)+12).astype(np.uint8)
    pixels[~mask]=0
    return Image.fromarray(pixels)

def label_tiles(image):
    raw=tiles(image,4)
    used=[i for i in range(len(raw)//32) if any(raw[i*32:(i+1)*32])]
    columns=image.width//8
    positions=[i//columns*16+i%columns for i in used]
    return positions,b''.join(raw[i*32:(i+1)*32] for i in used)

def main():
    config=json.loads((ROOT/'gba/assets/nameplates.json').read_text())
    assert ROOT/config['body_font'] == ROOT/'gba/assets/fusion-pixel-12px-monospaced-zh_hans.bdf'
    glyphs=read_font()
    master=json.loads((ROOT/'gba/assets/source-metadata/trial.json').read_text())['master']
    large_positions={}
    for author in master['AuthorData']['_items']:
        text=next((r['_text'] for r in author['_taggedText'] if r['_locale']==2),'')
        name='';positions=set()
        for size,part in re.findall(r'<size=(\d+)>([^<]+)</size>',text):
            if int(size)>=100:positions.update(range(len(name),len(name)+len(part)))
            name+=part
        if name:large_positions[name]=positions
    initials={ord(char) for entry in config['characters'] for at,char in enumerate(entry['name']) if at in large_positions[entry['name']]}
    big_glyphs={}
    with gzip.open(ROOT/config['font'],'rt') as font:
        for line in font:
            code,bitmap=line.strip().split(':')
            if int(code,16) in initials:
                assert len(bitmap)==64
                big_glyphs[int(code,16)]=[int(bitmap[i:i+4],16) for i in range(0,64,4)]
    assert set(big_glyphs)==initials
    base=nameplate_base()
    rows=[]
    sheet=Image.new('RGB',(384,4*48),(24,24,28))
    with (ROOT/'gba/src/nameplate_data.cpp').open('w') as f:
        f.write('#include "nameplate_data.h"\nnamespace manosaba {\n')
        diamond=Image.new('P',(96,32),0);diamond.paste(base,(6,3))
        f.write('const uint32_t nameplate_diamond[384]={'+','.join(map(str,struct.unpack('<384I',tiles(diamond,4))))+'};\n')
        f.write('const uint16_t nameplate_diamond_colors[4]={'+','.join(str(rgb555(c)) for c in nameplate_palette())+'};\n')
        for index,entry in enumerate(config['characters']):
            image=Image.new('P',(96,32),0)
            lettering=Image.new('P',(96,32),0)
            palette=list(UI_COLORS)
            palette[12:16]=nameplate_palette()
            palette[3]=tuple(entry['color'])
            image.putpalette(sum((list(c) for c in palette),[])+[0]*720)
            for y in range(25):
                for x in range(52):
                    ink=base.getpixel((x,y))
                    if ink:image.putpixel((x+6,y+3),ink)
            mask_big=Image.new('L',(96,32));mask_body=Image.new('L',(96,32))
            baseline=22
            left=11
            for at,char in enumerate(entry['name']):
                dest=mask_big if at==0 else mask_body
                large=at in large_positions[entry['name']]
                top=baseline-(14 if large else 10)
                width=16 if large else 12
                glyph_rows=big_glyphs[ord(char)] if large else glyphs[ord(char)]
                for y,row in enumerate(glyph_rows):
                    for x in range(width):
                        if row & (1<<(width-1-x)):dest.putpixel((left+x,top+y),255)
                left+=18 if at==0 else width
            for mask in (mask_big,mask_body):
                for y in range(31):
                    for x in range(95):
                        if mask.getpixel((x,y))>=80:
                            image.putpixel((x+1,y+1),6);lettering.putpixel((x+1,y+1),6)
            for mask,colors in [(mask_big,(13,14,15,3)),(mask_body,(10,11,1,1))]:
                for y in range(32):
                    for x in range(96):
                        a=mask.getpixel((x,y))
                        if a>=32:
                            ink=colors[min(3,a//64)];image.putpixel((x,y),ink);lettering.putpixel((x,y),ink)
            positions,raw=label_tiles(lettering)
            words=struct.unpack('<'+str(len(raw)//4)+'I',raw)
            f.write(f'alignas(4) static const uint32_t label_{index}_pixels[]={{\n')
            for at in range(0,len(words),16):f.write(','.join(map(str,words[at:at+16]))+',\n')
            f.write('};\n')
            f.write(f'static const uint8_t label_{index}_positions[]={{'+','.join(map(str,positions))+'};\n')
            f.write(f'static const label_art label_{index}={{label_{index}_pixels,label_{index}_positions,{len(positions)}}};\n')
            rows.append('{'+json.dumps(entry['speaker'],ensure_ascii=False)+f',&label_{index},{rgb555(entry["color"])}}}')
            rgba=image.convert('RGBA')
            # Palette values are indices: derive alpha explicitly, not through RGB.
            rgba.putalpha(Image.frombytes('L',image.size,bytes(255 if v else 0 for v in image.tobytes())))
            rgba.save(generated_path(f'design/nameplates/nameplate_{index}.png'))
            sheet.paste(rgba,(index%4*96,index//4*48),rgba)
        f.write('const nameplate nameplates[]={\n'+',\n'.join(rows)+'\n};\n')
        f.write(f'const int nameplate_count={len(rows)};\n}}\n')
    sheet.resize((768,384),Image.Resampling.NEAREST).save(generated_path('design/nameplates/preview.png'))
    print(f'Generated {len(rows)} pixel nameplates: {len(initials)} Unifont 16px glyphs, original large-name positions / Fusion Pixel 12px body, 25px diamond')

if __name__=='__main__':main()
