"""Start/options designs using the existing story-choice artwork and font."""
from build_paths import generated_path
from pathlib import Path
import json
from PIL import Image,ImageDraw,ImageFont
from font import read_font
from title_screen_mockups import rgb555
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/'manosaba-web/public/UI_Adv/Choice';OUT=generated_path('design/title-screen');FONT=read_font()
WHITE=(248,248,240,255)

def text(im,x,y,value,color=WHITE):
 d=ImageDraw.Draw(im)
 for ch in value:
  rows=FONT[ord(ch)];width=6 if ord(ch)<128 else 12
  points=[(x+xx,y+yy) for yy,row in enumerate(rows) for xx in range(width) if row&(1<<(11-xx))]
  for xx,yy in points:d.point((xx+1,yy+1),fill=(0,0,0,255))
  for point in points:d.point(point,fill=color)
  x+=width

def center(im,y,value):
 width=sum(6 if ord(ch)<128 else 12 for ch in value);text(im,(240-width)//2,y,value)

def background():
 result=Image.new('RGBA',(240,160),(64,64,64,255));result.alpha_composite(Image.open(ART/'ChoiceScreenBase.png').convert('RGBA').resize((240,160),Image.Resampling.LANCZOS));return result

def frame(im,x,y,w,h,selected):
 path='ChoicesFrame_Highlighted' if selected else 'ChoiceFrame_Default';im.alpha_composite(Image.open(ART/(path+'.png')).convert('RGBA').resize((w,h),Image.Resampling.LANCZOS),(x,y))

def main_page(selected=0):
 im=background()
 for index,label in enumerate(('新游戏','载入','画廊','音乐馆','选项')):
  y=4+31*index;frame(im,36,y,168,28,index==selected);center(im,y+7,label)
 return im

def options(skulls=True,selected=3):
 im=background();center(im,5,'选项')
 for index,(label,value) in enumerate((('对话速度','< 标准 >'),('音乐音量','<100%>'),('音效音量','<100%>'),('重要分支选项提示','< 开 >' if skulls else '< 关 >'),('debug入口',''))):
  y=24+25*index;frame(im,16,y,208,24,index==selected)
  if value:
   text(im,39,y+5,label);width=sum(6 if ord(ch)<128 else 12 for ch in value);text(im,197-width,y+5,value)
  else:center(im,y+5,label)
 return im

def main():
 OUT.mkdir(exist_ok=True)
 pages={'start-menu':main_page(),'options':options(),'options-hints-off':options(False),'options-debug':options(selected=4)}
 # Preserve the original contrast and render glyphs in the same native RGB555 space.
 for name,im in pages.items():
  im=im.convert('RGB').quantize(colors=240,method=Image.Quantize.MEDIANCUT).convert('RGB');im=rgb555(im);im.save(OUT/(name+'-native.png'));im.resize((960,640),Image.Resampling.NEAREST).save(OUT/(name+'-preview.png'));pages[name]=im
 f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27);sheet=Image.new('RGB',(2000,1490),'#e9e6e1');d=ImageDraw.Draw(sheet)
 for index,(label,path) in enumerate((('TITLE / EMA','ema-native.png'),('TITLE / HIRO','hiro-native.png'),('START MENU','start-menu-native.png'),('OPTIONS','options-native.png'))):
  x=20+(index%2)*1000;y=14+(index//2)*740;d.text((x,y),label,fill='#292328',font=f);sheet.paste(Image.open(OUT/path).resize((960,640),Image.Resampling.NEAREST),(x,y+47))
 sheet.save(OUT/'flow-comparison.png')
 (OUT/'menus.json').write_text(json.dumps(dict(flow=['title:A -> start-menu','start-menu: 新游戏 / 载入 / 画廊 / 音乐馆 / 选项','options: 对话速度 / 音乐音量 / 音效音量 / 重要分支选项提示 / debug入口','debug入口 -> existing demo menu'],choice_art='Existing UI_Adv/Choice background and highlighted/default frames; no danger skulls on ordinary menu rows.',font='Existing12px pixel font with1px black shadow',settings=dict(dialogue_speed=['慢','标准','快','即时'],music_volume='independent0..100%, current mix preserved at100%',sfx_volume='independent0..100%, current mix preserved at100%',important_choice_hints='on by default; controls only the danger skull icon',debug_entry='opens current demo/debug screen'),persistence='Options are cartridge-global, independent of story-slot rollback; preserve old Flash records.',stage='Mockups only; implementation pending after first appearance review.'),ensure_ascii=False,indent=2)+'\n');print(OUT)
if __name__=='__main__':main()
