"""Native-size trial bubbles, shared selector backdrop and bounded full-screen evidence art."""
from build_paths import generated_path
from pathlib import Path
import csv,gzip,json,struct,re
import numpy as np
from PIL import Image
from graphics_project import GBA,ROOT,ContentCache,digest,write_changed
from choice_ui import artwork
from image_encoding import rgb555,UI_COLORS
from font import read_font
from nameplates import label_tiles,nameplate_palette
from story import compress

ART=ROOT/'manosaba-web/public/UI_Trial/Objection'
# 0 transparent, 1 white, 6 shadow; all full-screen trial artwork shares these UI inks.
INKS=[(0,0,0),(248,248,240),(152,144,144),(248,128,168),(32,24,32),(216,192,136),(0,0,0),(40,24,32),(48,152,120),(216,56,88),(216,184,72),(88,120,184),(88,64,96),(128,64,88),(168,88,120),(216,112,144)]
LABELS=[('',(0,0,0)),('反驳',(48,152,120)),('赞同',(72,152,184)),('质疑',(216,184,72)),('伪证',(216,56,88)),
        ('魔法',(248,128,168)),('魔法',(216,152,64)),('魔法',(216,184,72)),('魔法',(88,120,184)),('魔法',(168,104,192)),('魔法',(136,144,144))]


def tile_bytes(indexed):
    return indexed.reshape(20,8,30,8).transpose(0,2,1,3).tobytes() if indexed.shape==(160,240) else indexed.reshape(indexed.shape[0]//8,8,indexed.shape[1]//8,8).transpose(0,2,1,3).tobytes()


def native_palette(images,count):
    rgb=np.concatenate([np.asarray(im.convert('RGB')).reshape(-1,3) for im in images])
    im=Image.fromarray(rgb.reshape(1,-1,3));p=im.quantize(colors=count,method=Image.Quantize.MEDIANCUT).getpalette()[:count*3]
    colors=[tuple(v&248 for v in p[i:i+3]) for i in range(0,len(p),3)]
    return colors+[colors[-1]]*(count-len(colors))


def indexed(image,colors,start=1,transparent=True):
    rgba=np.asarray(image.convert('RGBA'));rgb=rgba[:,:,:3]>>3
    unique,inverse=np.unique(rgb.reshape(-1,3),axis=0,return_inverse=True)
    pal=np.asarray(colors[start:],dtype=np.int32)>>3
    distances=((unique.astype(np.int32)[:,None,:]-pal[None,:,:])**2).sum(axis=2)
    result=(distances.argmin(axis=1)+start)[inverse].astype(np.uint8).reshape(image.height,image.width)
    if transparent:result[rgba[:,:,3]<128]=0
    return result


def bubble(height,selected):
    image=Image.open(ART/('Balloon_Selected.png' if selected else 'Balloon_Default.png')).convert('RGBA')
    cap=min(height,64);source_cap=round(cap*image.height/height)
    result=Image.new('RGBA',(168,height))
    result.alpha_composite(image.crop((0,0,source_cap,image.height)).resize((cap,height),Image.Resampling.LANCZOS))
    result.alpha_composite(image.crop((source_cap,0,image.width-source_cap,image.height)).resize((168-cap*2,height),Image.Resampling.LANCZOS),(cap,0))
    result.alpha_composite(image.crop((image.width-source_cap,0,image.width,image.height)).resize((cap,height),Image.Resampling.LANCZOS),(168-cap,0))
    return result


def cancel_bubble(selected):
    image=Image.open(ART/'Balloon_Cancel.png').convert('RGBA').resize((120,24),Image.Resampling.LANCZOS)
    if selected:
        pixels=np.asarray(image).copy();pixels[:,:,:3]=(pixels[:,:,:3].astype(np.uint16)*np.array([.65,.25,.34])).astype(np.uint8)
        image=Image.fromarray(pixels)
    return image

def clock_frame():
    source=Image.open(ART.parent/'ClockBase.png').convert('RGBA')
    result=Image.new('RGBA',(64,24))
    for crop,x,width in [((0,0,151,151),0,24),((151,0,400,151),24,30),((400,0,465,151),54,10)]:
        result.alpha_composite(source.crop(crop).resize((width,24),Image.Resampling.LANCZOS),(x,0))
    palette=list(UI_COLORS);palette[12:16]=nameplate_palette()
    rgba=np.asarray(result);allowed=np.array([1,2,4,5,6,7,8,9,10,11,12,13,14,15])
    distance=((rgba[:,:,:3].astype(np.int32)[:,:,None,:]-np.asarray(palette)[allowed][None,None,:,:])**2).sum(axis=3)
    pixels=allowed[distance.argmin(axis=2)].astype(np.uint8);pixels[rgba[:,:,3]<128]=0
    # Clock art must not use dynamic speaker/link, inline-color or timer inks.
    pixels[pixels==4]=8
    pixels[pixels==5]=2
    # Repainting the frame must erase all possible old digits and their shadows.
    font=read_font()
    for at in range(5):
        for char in (':' if at==2 else '0123456789'):
            for y,row in enumerate(font[ord(char)]):
                for x in range(6):
                    if row&(1<<(11-x)):
                        assert pixels[6+y,24+at*6+x] and pixels[7+y,25+at*6+x]
    image=Image.fromarray(pixels,'P')
    return image,palette


def badges():
    glyphs=read_font();big={};wanted={ord(v[0][0]) for v in LABELS[1:]}
    with gzip.open(GBA/'assets/unifont-17.0.03.hex.gz','rt') as font:
        for line in font:
            code,bits=line.strip().split(':')
            if int(code,16) in wanted:big[int(code,16)]=[int(bits[n:n+4],16) for n in range(0,64,4)]
    result=[]
    for text,color in LABELS:
        im=Image.new('P',(40,32),0)
        if text:
            masks=[]
            for at,c in enumerate(text):
                width=16 if at==0 else 12;left=5 if at==0 else 22;top=4 if at==0 else 8
                points=[]
                for y,row in enumerate(big[ord(c)] if at==0 else glyphs[ord(c)]):
                    for x in range(width):
                        if row&(1<<(width-1-x)):points.append((left+x,top+y))
                masks.append(points)
            for points in masks:
                for x,y in points:im.putpixel((x+1,y+1),6)
            for at,points in enumerate(masks):
                for x,y in points:im.putpixel((x,y),3 if at==0 else 1)
        result.append(im)
    return result


def main():
    cache=ContentCache();target=GBA/'assets/trial';target.mkdir(parents=True,exist_ok=True)
    base,_=artwork();bases=[base]
    for who in ('Ema','Hiro'):
        im=base.copy();portrait=Image.open(ART/f'ChoicePortrait_{who}.png').convert('RGBA');portrait.thumbnail((96,160),Image.Resampling.LANCZOS)
        im.alpha_composite(portrait,(240-portrait.width,0));bases.append(im)
    bubbles=[bubble(h,s) for h in (32,48,64,80,96) for s in (False,True)];labels=badges()
    cancels=[cancel_bubble(s) for s in (False,True)]
    palette=json.loads((GBA/'assets/trial-ui-palette.json').read_text())['colors']
    colors=[(v&31,(v>>5)&31,(v>>10)&31) for v in palette]
    colors=[tuple(c*8 for c in rgb) for rgb in colors]
    blob=bytearray();lines=['// Generated by trial_ui.py.','#include "trial_ui_data.h"','namespace manosaba {']
    def add(raw):blob.extend(bytes((-len(blob))%4));off=len(blob);blob.extend(raw);return off
    def ptr(off,kind='uint8_t'):return f'trial_ui_bytes+{off}' if kind=='uint8_t' else f'reinterpret_cast<const {kind}*>(trial_ui_bytes+{off})'
    pal=ptr(add(struct.pack('<256H',*[rgb555(c) for c in colors])),'uint16_t')
    rows=[]
    for i,im in enumerate(bases):
        pixels=indexed(im,colors,16,False);raw=tile_bytes(pixels);rows.append('{'+f'{pal},{ptr(add(raw),"uint32_t")},nullptr'+'}')
        Image.fromarray(np.asarray(colors,dtype=np.uint8)[pixels]).save(generated_path(f'design/trial/choice-{i}.png'))
    lines.append('const ui_picture trial_choice_pictures[]={'+','.join(rows)+'};')
    history_remap=[int(((np.asarray(colors[:240],dtype=np.int32)-np.asarray(rgb))**2).sum(axis=1).argmin()) for rgb in colors[240:]]
    lines.append('const uint8_t trial_history_remap[16]={'+','.join(map(str,history_remap))+'};')
    pointers=[ptr(add(tile_bytes(indexed(im,colors))),'uint32_t') for im in bubbles]
    lines.append('const uint32_t* const trial_bubbles[]={'+','.join(pointers)+'};')
    pointers=[ptr(add(tile_bytes(indexed(im,colors))),'uint32_t') for im in cancels]
    lines.append('const uint32_t* const trial_cancel_bubbles[]={'+','.join(pointers)+'};')
    label_rows=[];label_colors=[]
    def nearest(rgb):return int(((np.asarray(colors[1:],dtype=np.int32)-np.asarray(rgb,dtype=np.int32))**2).sum(axis=1).argmin())+1
    for im,(_,color) in zip(labels,LABELS):
        positions,raw=label_tiles(im)
        label_rows.append('{'+f'{ptr(add(raw),"uint32_t")},{ptr(add(bytes(positions)))},{len(positions)}'+'}')
        label_colors.append(nearest(color))
    inks=[nearest(c) for c in UI_COLORS];inks[0]=0;inks[1]=1;inks[6]=6
    for level,color in enumerate(nameplate_palette()):inks[12+level]=nearest(color)
    lines.append('const label_art trial_badges[]={'+','.join(label_rows)+'};')
    lines.append('const uint8_t trial_label_inks[16]={'+','.join(map(str,inks))+'};')
    lines.append('const uint8_t trial_badge_colors[11]={'+','.join(map(str,label_colors))+'};')
    clock,clock_colors=clock_frame();positions,raw=label_tiles(clock)
    lines.append('const label_art trial_clock={'+f'{ptr(add(raw),"uint32_t")},{ptr(add(bytes(positions)))},{len(positions)}'+'};')
    choice_source=(GBA/'src/choice_ui_data.cpp').read_text()
    choice_palette=list(map(int,re.search(r'choice_ui_palette\[256\]=\{([^}]+)\}',choice_source)[1].split(',')))
    choice_colors=np.array([(v&31,(v>>5)&31,(v>>10)&31) for v in choice_palette])*8
    clock_inks=[int(((choice_colors[1:]-c)**2).sum(axis=1).argmin())+1 for c in clock_colors];clock_inks[0]=0
    lines.append('const uint8_t trial_clock_choice_inks[16]={'+','.join(map(str,clock_inks))+'};')
    rgba=np.asarray(clock_colors,dtype=np.uint8)[np.asarray(clock)];Image.fromarray(rgba).save(generated_path('design/trial/clock-native.png'))
    manifest=list(csv.DictReader((ROOT/'original_sprites/manifest.csv').open()));proofs={}
    for row in manifest:
        if row['type']=='Texture2D' and row['name'].startswith('Clue_'):
            parts=row['name'].split('_')
            if len(parts)==3:proofs[f'{int(parts[1])}-{int(parts[2])}']=ROOT/'original_sprites'/row['output']
    bg_colors=INKS+native_palette([base],16);bg_pixels=indexed(base,bg_colors,16,False);proof_rows=[];stored=0;hits=0
    for at,(ident,path) in enumerate(sorted(proofs.items())):
        key=digest(dict(step='trial-evidence-v2',source=cache.file_key(path),base=digest(base.tobytes()),size=(216,120),inks=INKS))
        encoded=cache.get_json(key)
        if encoded:hits+=1
        else:
            image=Image.open(path).convert('RGBA');image.thumbnail((216,120),Image.Resampling.LANCZOS)
            palette=bg_colors+native_palette([image],224);pixels=bg_pixels.copy()
            proof=indexed(image,palette,32);x=(240-image.width)//2;y=18+(120-image.height)//2
            mask=np.asarray(image)[:,:,3]>=128;area=pixels[y:y+image.height,x:x+image.width];area[mask]=proof[mask]
            raw=tile_bytes(pixels);blocks=[]
            for offset in range(0,len(raw),4096):
                chunk=raw[offset:offset+4096];packed=compress(chunk);codec=len(packed)<len(chunk)
                blocks.append(dict(data=cache.put_blob(packed if codec else chunk),codec=int(codec)))
            encoded=dict(palette=cache.put_blob(struct.pack('<256H',*[rgb555(c) for c in palette])),blocks=blocks)
            cache.put_json(key,encoded)
            if ident in ('1-1','1-11','10-25'):Image.fromarray(np.asarray(palette,dtype=np.uint8)[pixels]).save(generated_path(f'design/trial/evidence-{ident}.png'))
        palette=cache.path(encoded['palette'],'.bin').read_bytes();palette_ptr=ptr(add(palette),'uint16_t');block_rows=[]
        for block in encoded['blocks']:
            raw=cache.path(block['data'],'.bin').read_bytes();stored+=len(raw);block_rows.append('{'+f'{ptr(add(raw))},{block["codec"]}'+'}')
        name=f'evidence_{at}_blocks';lines.append(f'const ui_picture_block {name}[]={{'+','.join(block_rows)+'};')
        w,h=Image.open(path).size;scale=min(1,216/w,120/h);w,h=round(w*scale),round(h*scale)
        proof_rows.append('{'+json.dumps(ident)+',{'+f'{palette_ptr},nullptr,{name}'+'},'+f'{(240-w)//2},{18+(120-h)//2},{w},{h}'+'}')
    lines.append('const trial_evidence_asset trial_evidence_pictures[]={'+','.join(proof_rows)+'};')
    lines.append(f'const int trial_evidence_count={len(proof_rows)};\n}}')
    write_changed(target/'ui.bin',bytes(blob));write_changed(GBA/'src/trial_ui_data.cpp','\n'.join(lines)+'\n')
    write_changed(GBA/'src/trial_ui_bytes.s','// '+digest(bytes(blob))+'\n.section .rodata.trial_ui_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba14trial_ui_bytesE\n_ZN8manosaba14trial_ui_bytesE:\n.incbin "../assets/trial/ui.bin"\n')
    print('trial UI bytes:',len(blob),'evidence images:',len(proofs),'cached:',hits,'compressed evidence:',stored,flush=True)


if __name__=='__main__':main()
