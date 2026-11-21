"""Native-size music collection mockups only; no ROM/runtime changes."""
from build_paths import generated_path
from pathlib import Path
import json,csv
from PIL import Image,ImageDraw,ImageEnhance
from font import read_font
from witch_book_icons import key_icon

ROOT=Path(__file__).resolve().parents[2]
OUT=generated_path('design/music-room')
FONT=read_font()
INK=(232,224,216);MUTED=(176,168,160);DIM=(112,104,104)
ACCENT=(232,160,176);SHADOW=(64,56,56)
CATALOG=json.loads((ROOT/'gba/assets/music/catalog.json').read_text())
with (ROOT/'gba/assets/music/room-titles.tsv').open(encoding='utf-8-sig',newline='') as source:
    TRACKS=[dict(id=r['曲目标识'],title=r['曲名']) for r in csv.DictReader(source,delimiter='\t')]

def width(value):return sum(13 if 0xe000<=ord(c)<=0xe004 else 6 if ord(c)<128 else 12 for c in value)

def text(im,x,y,value,color=INK,right=240):
    draw=ImageDraw.Draw(im)
    if x+width(value)>right:
        while value and x+width(value+'...')>right:value=value[:-1]
        value+='...'
    points=[]
    for char in value:
        w=13 if 0xe000<=ord(char)<=0xe004 else 6 if ord(char)<128 else 12
        rows=FONT[ord(char)]
        points.extend((x+xx,y+yy) for yy,row in enumerate(rows) for xx in range(min(w,12)) if row&(1<<(11-xx)))
        x+=w
    for xx,yy in points:draw.point((xx+1,yy+1),fill=SHADOW)
    for point in points:draw.point(point,fill=color)

def background():
    source=Image.open(ROOT/'manosaba-web/public/UI_Adv/Choice/ChoiceScreenBase.png').convert('L').resize((240,160),Image.Resampling.LANCZOS)
    im=Image.new('RGB',(240,160))
    im.putdata([(24+v//16*2,22+v//16*2,24+v//16*2) for v in source.tobytes()])
    return im

def key(im,x,label,key_name,color=MUTED):
    icon=key_icon(key_name)
    if color==DIM:
        icon=ImageEnhance.Brightness(icon).enhance(.5)
    im.paste(icon,(x,146),icon)
    text(im,x+icon.width+1,146,label,color)

def page(selected=0,playing=0,empty=False):
    im=Image.new('RGBA',(240,160));draw=ImageDraw.Draw(im)
    text(im,9,10,'音乐馆')
    text(im,142,10,'已收集',MUTED)
    collected=0 if empty else 12
    count=f'{collected}/{len(TRACKS)}';text(im,232-width(count),10,count)
    draw.line((9,25,231,25),fill=(104,88,88))
    titles=[TRACKS[i]['title'] for i in range(5)]
    locked=set(range(5)) if empty else {2,4}
    for row,title in enumerate(titles):
        y=31+row*18
        if row==selected:
            draw.rectangle((8,y-2,231,y+14),fill=(56,44,50),outline=(104,80,88))
            draw.line((9,y-1,9,y+13),fill=ACCENT,width=2)
        if row not in locked and row==playing:draw.polygon([(13,y+4),(13,y+10),(18,y+7)],fill=ACCENT)
        text(im,24,y+1,f'{row+1:02}',DIM if row in locked else MUTED)
        text(im,43,y+1,'未收集' if row in locked else title,DIM if row in locked else INK,right=228)
    draw.line((9,123,231,123),fill=(88,72,80))
    if playing>=0:
        draw.polygon([(10,130),(10,136),(15,133)],fill=ACCENT)
        text(im,22,127,'播放中',ACCENT)
        text(im,65,127,titles[playing],right=191)
    else:text(im,10,127,'随剧情收集',MUTED)
    text(im,232-width('1/10'),127,'1/10',MUTED)
    text(im,6,146,'\ue003',MUTED);text(im,22,146,'选曲',MUTED)
    im.paste(key_icon('L'),(53,146),key_icon('L'));im.paste(key_icon('R'),(73,146),key_icon('R'))
    text(im,93,146,'翻页',MUTED)
    key(im,124,'停止' if playing==selected else '播放','A',DIM if selected in locked else MUTED)
    key(im,188,'返回','B')
    result=background();result.paste(im,(0,-4),im);im=result
    # Match the GBA's RGB555 output, without smoothing the pixel font.
    pixels=im.tobytes();im.putdata([tuple((v//8)*8 for v in rgb) for rgb in zip(pixels[::3],pixels[1::3],pixels[2::3])])
    return im

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pages={'playing':page(),'locked':page(selected=2),'empty':page(playing=-1,empty=True)}
    for name,im in pages.items():
        im.save(OUT/(name+'-native.png'))
        im.resize((960,640),Image.Resampling.NEAREST).save(OUT/(name+'-4x.png'))
    sheet=Image.new('RGB',(1944,664),(224,216,208))
    for i,name in enumerate(('playing','locked')):sheet.paste(pages[name].resize((960,640),Image.Resampling.NEAREST),(8+i*968,12))
    sheet.save(OUT/'comparison.png')
    (OUT/'design.json').write_text(json.dumps(dict(stage='Mockups only; no runtime changes',size=[240,160],rows=5,header='collected / playable catalog entries',playable_tracks=len(TRACKS),catalog_filter='approved playable music entries; exclude unmade tracks and ambient/ringtone cues',locked='gray 未收集, hide title; no lock icon',playing='independent of highlighted row; changing selection does not change playback',controls={'up_down':'select; continue across page boundaries','left_right_or_L_R':'page','A':'play selected; stop when selected track is already playing; locked entries do nothing','B':'return to start menu'},titles='original catalog titles; omit debug IDs and arrangement descriptions; selected long names may scroll during implementation',unlock='proposed: first successful script music play unlocks globally, demo/stress/music-room playback does not unlock',persistence='proposed cartridge-global Flash bitset, separate from story-slot rollback',preview_collected_count=12),ensure_ascii=False,indent=2)+'\n')
    print(OUT,'playable',len(TRACKS))

if __name__=='__main__':main()
