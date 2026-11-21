"""Pixel-accurate save/load proposals only; does not generate game assets."""
from build_paths import generated_path
import gzip
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from font import read_font
from witch_book_icons import key_icon

ROOT = Path(__file__).resolve().parents[2]
OUT = generated_path('design/save-load')
ART = ROOT/'manosaba-web/public/UI_SaveLoad'
FONT = read_font()
BIG = {}
with gzip.open(ROOT/'gba/assets/unifont-17.0.03.hex.gz', 'rt') as source:
    for line in source:
        code,bits=line.strip().split(':')
        if chr(int(code,16)) in '暂停存读档确认':
            BIG[int(code,16)]=[int(bits[i:i+4],16) for i in range(0,64,4)]

WHITE=(248,248,240)
MUTED=(208,200,192)
EMA=(248,128,168)
HIRO=(216,56,88)
PALETTE=[(0,0,0),WHITE,MUTED,EMA,HIRO,(24,24,24),(32,24,32),(40,32,40),
         (56,48,48),(72,56,64),(96,80,80),(120,104,104),(152,136,128),
         (176,160,152),(64,32,48),(80,24,32)]
ICONS={key:key_icon(key) for key in 'ABLR'}
THUMBS=[]


def text(im,x,y,value,color=WHITE,large=False):
    draw=ImageDraw.Draw(im)
    for char in value:
        width=16 if large else 6 if ord(char)<128 else 12
        rows=BIG[ord(char)] if large else FONT[ord(char)]
        points=[(x+dx,y+dy) for dy,row in enumerate(rows) for dx in range(width)
                if row & (1<<((15 if large else 11)-dx))]
        assert x>=0 and x+width<=240 and y>=0 and y+(16 if large else 12)<=159,(value,x,y)
        for px,py in points:draw.point((px+1,py+1),fill=(0,0,0,255))
        for p in points:draw.point(p,fill=(*color,255))
        x+=width


def quantize(im,colors):
    pixels=np.asarray(im.convert('RGB')).astype(np.int32)
    unique,inverse=np.unique(pixels.reshape(-1,3),axis=0,return_inverse=True)
    chosen=((unique[:,None,:]-np.asarray(colors)[None,:,:])**2).sum(axis=2).argmin(axis=1)
    return Image.fromarray(np.asarray(colors,dtype=np.uint8)[chosen[inverse]].reshape(pixels.shape)).convert('RGBA')


def nine_patch(im,box):
    source=Image.open(ART/'SaveLoadSlotFrame.png').convert('RGBA')
    x,y,w,h=box;cut=45;edge=5
    sx=(0,cut,source.width-cut,source.width);sy=(0,cut,source.height-cut,source.height)
    dx=(x,x+edge,x+w-edge,x+w);dy=(y,y+edge,y+h-edge,y+h)
    for row in range(3):
        for col in range(3):
            part=source.crop((sx[col],sy[row],sx[col+1],sy[row+1]))
            part=part.resize((dx[col+1]-dx[col],dy[row+1]-dy[row]),Image.Resampling.LANCZOS)
            im.alpha_composite(part,(dx[col],dy[row]))


def base():
    source=Image.open(ART/'SaveLoadBase.png').convert('RGBA').resize((240,160),Image.Resampling.LANCZOS)
    im=Image.new('RGBA',(240,160),(24,24,24,255));im.alpha_composite(source)
    return im


def action(im,x,key,label):
    im.alpha_composite(ICONS[key],(x,144))
    text(im,x+ICONS[key].width+1,145,label)


def direction(im,x,y,glyph):
    draw=ImageDraw.Draw(im)
    for dy,row in enumerate(FONT[ord(glyph)]):
        for dx in range(12):
            if row&(1<<(11-dx)):
                draw.point((x+dx+1,y+dy+1),fill=(0,0,0,255))
                draw.point((x+dx,y+dy),fill=(*MUTED,255))


def tabs(im,mode,accent):
    draw=ImageDraw.Draw(im)
    for index,label in enumerate(('存档','读档')):
        x=32+index*92;top=4 if index==mode else 8
        draw.polygon([(x,top+4),(x+4,top),(x+76,top),(x+80,top+4),
                      (x+80,29),(x,29)],fill=(40,32,40) if index==mode else (24,24,24),
                     outline=accent if index==mode else (96,80,80))
        if index==mode:
            text(im,x+23,8,label[0],accent,True)
            text(im,x+39,12,label[1])
        else:text(im,x+28,12,label,MUTED)
    im.alpha_composite(ICONS['L'],(7,18));im.alpha_composite(ICONS['R'],(215,18))


def thumbnail(im,source,box):
    image=Image.open(source).convert('RGB') if source else Image.new('RGB',(48,24),(24,24,24));image.thumbnail((box[2],box[3]),Image.Resampling.LANCZOS)
    p=image.quantize(colors=16,method=Image.Quantize.MEDIANCUT).convert('RGB')
    p=Image.fromarray((np.asarray(p)//8*8).astype(np.uint8)).convert('RGBA')
    x,y,w,h=box;image=Image.new('RGBA',(w,h),(24,24,24,255))
    image.alpha_composite(p,((w-p.width)//2,(h-p.height)//2))
    THUMBS.append((box,image))


def slot(im,index,y,accent,selected=False,empty=False,automatic=False,trial=False):
    draw=ImageDraw.Draw(im);draw.rectangle((8,y,229,y+49),fill=(24,24,24))
    nine_patch(im,(8,y,222,50))
    if selected:
        draw.rectangle((10,y+2,227,y+47),outline=accent)
        draw.rectangle((11,y+3,12,y+46),fill=accent)
    label='自动' if automatic else f'{index:02d}'
    text(im,17,y+5,label,accent if selected else MUTED)
    if empty:
        text(im,81,y+15,'空白记录',MUTED)
        return
    preview=ROOT/'gba/assets/graphics-cache/court/reduced/Court/view-4-gba.png' if trial else None
    thumbnail(im,preview,(17,y+20,48,24))
    text(im,78,y+5,'第1章  审问01' if trial else '第1章  日常04')
    text(im,78,y+19,'用血画下的蝴蝶绘画' if trial else '哇……好大。')
    text(im,78,y+32,'游玩06:42 剩余09:36' if trial else '游玩 02:18',MUTED)


def scrollbar(im,selected,total):
    draw=ImageDraw.Draw(im);draw.line((234,35,234,133),fill=(72,56,64))
    height=max(12,99*2//total);top=35+(99-height)*selected//max(1,total-1)
    draw.line((234,top,234,top+height-1),fill=MUTED,width=2)


def footer(im,mode,selected,total):
    direction(im,8,145,'\ue003');text(im,22,145,'选择',MUTED)
    action(im,63,'A','存入' if mode==0 else '读取');action(im,131,'B','返回')
    text(im,199,145,f'{selected}/{total}',MUTED)


def save_page():
    im=base();tabs(im,0,EMA)
    slot(im,1,34,EMA,True);slot(im,2,88,EMA,empty=True)
    scrollbar(im,0,12);footer(im,0,1,12)
    return im


def load_page():
    im=base();tabs(im,1,HIRO)
    slot(im,0,34,HIRO,True,automatic=True,trial=True)
    slot(im,1,88,HIRO)
    scrollbar(im,0,13);footer(im,1,1,13)
    return im


def scroll_page():
    im=base();tabs(im,0,EMA)
    slot(im,2,34,EMA,True);slot(im,3,88,EMA,empty=True)
    scrollbar(im,1,12);footer(im,0,2,12)
    return im


def pause_page():
    im=base();draw=ImageDraw.Draw(im)
    text(im,104,6,'暂停',EMA,True)
    text(im,105,30,'第1章',MUTED)
    nine_patch(im,(18,49,204,85))
    for i,label in enumerate(('继续游戏','存档','读档','返回选章')):
        y=53+i*19
        if i==1:
            draw.rectangle((25,y-1,214,y+14),fill=(64,32,48))
            draw.line((25,y-1,25,y+14),fill=EMA,width=2)
        text(im,96 if len(label)==4 else 108,y,label,EMA if i==1 else WHITE)
    action(im,66,'A','确认');action(im,133,'B','返回')
    return im


def confirm_page():
    im=base();tabs(im,0,EMA);draw=ImageDraw.Draw(im)
    draw.rectangle((16,38,223,134),fill=(24,24,24));nine_patch(im,(16,38,208,97))
    text(im,72,47,'覆盖记录 01？')
    text(im,36,69,'原记录：第1章 日常04',MUTED)
    text(im,36,85,'新记录：第1章 日常09')
    draw.rectangle((41,107,107,126),fill=(64,32,48),outline=EMA)
    draw.rectangle((129,107,195,126),fill=(32,24,32),outline=(96,80,80))
    text(im,62,111,'取消');text(im,150,111,'覆盖',MUTED)
    direction(im,8,145,'\ue004');text(im,22,145,'选择',MUTED)
    action(im,80,'A','确认');action(im,151,'B','取消')
    return im


def save(name,image):
    # UI uses one palette bank; each visible scene preview gets one bank.
    image=quantize(image,PALETTE)
    for (x,y,w,h),thumbnail_image in THUMBS:image.alpha_composite(thumbnail_image,(x,y))
    image=image.convert('RGB')
    image.save(OUT/(name+'-240x160.png'))
    image.resize((720,480),Image.Resampling.NEAREST).save(OUT/(name+'-3x.png'))
    count=len(image.getcolors(100000));assert count<=48,(name,count)
    THUMBS.clear()
    return image


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pages=[]
    for name,make in [('01-pause',pause_page),('02-save',save_page),('03-load',load_page),('04-overwrite',confirm_page)]:
        pages.append(save(name,make()))
    save('05-scroll',scroll_page())
    board=Image.new('RGB',(488,328),(16,16,16))
    for i,page in enumerate(pages):board.paste(page,((i%2)*248,(i//2)*168))
    board.resize((1464,984),Image.Resampling.NEAREST).save(OUT/'overview-3x.png')
    from story import compress
    rgba=quantize(base(),PALETTE);pixels=np.asarray(rgba)[:,:,:3]
    lookup={tuple(color):i for i,color in enumerate(PALETTE)}
    indices=np.array([lookup[tuple(c)] for c in pixels.reshape(-1,3)],dtype=np.uint8).reshape(160,240)
    raw=(indices[:,::2]|indices[:,1::2]<<4).reshape(20,8,30,4).transpose(0,2,1,3).tobytes()
    report=dict(size=[240,160],flash_bytes=128*1024,manual_slots=12,automatic_slots=1,retry_slots=1,
                record_page_bytes=8192,physical_record_pages=15,global_bytes=8192,
                erase_sector_bytes=4096,live_record_pages=14,spare_record_pages=1,
                ui_background_raw_bytes=len(raw),ui_background_compressed_bytes=sum(len(compress(raw[i:i+4096])) for i in range(0,len(raw),4096)),
                visible_slots=2,ui_colors=16,max_total_colors=48,
                thumbnail=[48,24],thumbnail_source='existing scene art, reconstructed from stored asset IDs',
                files=[n for n in ('01-pause','02-save','03-load','04-overwrite')])
    (OUT/'design-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    assert report['physical_record_pages']*report['record_page_bytes']+report['global_bytes']==report['flash_bytes']
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
