"""Native 240x160 title-screen proposals, using original art and a pre-baked logo."""
from build_paths import generated_path
from pathlib import Path
import json
import numpy as np
from PIL import Image,ImageDraw,ImageFont
from font import read_font
from witch_book_icons import key_icon
ROOT=Path(__file__).resolve().parents[2];GBA=ROOT/'gba';ART=ROOT/'manosaba-web/public/Home';OUT=generated_path('design/title-screen');FONT=read_font()
LOGO_BOX=(108,6,124,73)
PROMPT=(192,140)

def art(which):
 source=Image.open(ART/'Background'/f'Background_{which}.png').convert('RGB')
 # Keep proportions; anchor each composition towards its protagonist's face.
 left=24 if which==1 else 80
 source=source.resize((320,160),Image.Resampling.LANCZOS).crop((left,0,left+240,160)).convert('RGBA')
 logo=Image.open(ART/'TitleLogo@ZhHans.png').convert('RGBA').resize(LOGO_BOX[2:],Image.Resampling.LANCZOS)
 source.alpha_composite(logo,LOGO_BOX[:2]);return source.convert('RGB')

def rgb555(im):
 c=np.asarray(im.convert('RGB'),dtype=np.uint16)>>3
 return Image.fromarray(((c<<3)|(c>>2)).astype(np.uint8))

def prompt(im):
 im=im.convert('RGBA');im.alpha_composite(key_icon('A'),PROMPT)
 d=ImageDraw.Draw(im);x=PROMPT[0]+17;y=PROMPT[1]+1
 for char in '开始':
  points=[(x+xx,y+yy) for yy,row in enumerate(FONT[ord(char)]) for xx in range(12) if row&(1<<(11-xx))]
  for px,py in points:d.point((px+1,py+1),fill=(0,0,0,255))
  for px,py in points:d.point((px,py),fill=(248,248,240,255))
  x+=12
 return rgb555(im)

def faded(im,level,white=True):
 c=np.asarray(im,dtype=np.uint16)>>3;c=c+((31-c)*level//16) if white else c-c*level//16
 return Image.fromarray(((c<<3)|(c>>2)).astype(np.uint8))

def main():
 OUT.mkdir(exist_ok=True);original=[art(1),art(2)];both=Image.new('RGB',(480,160))
 for n,im in enumerate(original):both.paste(im,(n*240,0))
 # One 240-color RGB555 background palette, leaving the final16 colors for UI.
 pal=both.quantize(colors=240,method=Image.Quantize.MEDIANCUT).getpalette();palette=Image.new('P',(1,1));palette.putpalette(pal)
 final=[];stats=[]
 for name,im in zip(('ema','hiro'),original):
  quantized=rgb555(im.quantize(palette=palette,dither=Image.Dither.NONE).convert('RGB'));quantized.save(OUT/(name+'-baked.png'));screen=prompt(quantized);screen.save(OUT/(name+'-native.png'));screen.resize((960,640),Image.Resampling.NEAREST).save(OUT/(name+'-preview.png'));final.append(screen)
  frames=[faded(screen,k,white=name=='hiro').resize((720,480),Image.Resampling.NEAREST) for k in range(16,-1,-1)];frames[0].save(OUT/(name+'-fade.gif'),save_all=True,append_images=frames[1:],duration=[120]+[33]*15+[1600],loop=0,disposal=2)
  pixels=np.asarray(quantized);tiles=pixels.reshape(20,8,30,8,3).transpose(0,2,1,3,4).reshape(-1,192);stats.append(dict(hero=name,unique_background_tiles=len(np.unique(tiles,axis=0)),scope='Design encoding only; <=600 exact8bpp blocks, no near-tile substitution.'))
 sheet=Image.new('RGB',(2000,750),'#e9e6e1');d=ImageDraw.Draw(sheet);f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',27)
 for n,(name,im) in enumerate(zip(('EMA','HIRO'),final)):
  x=20+n*1000;d.text((x,14),name,fill='#292328',font=f);sheet.paste(im.resize((960,640),Image.Resampling.NEAREST),(x,61))
 sheet.save(OUT/'title-screen-comparison.png')
 strip=Image.new('RGB',(1440,160))
 for n,screen in enumerate(final):
  for j,level in enumerate((16,8,0)):strip.paste(faded(screen,level,white=n==1),(240*(n*3+j),0))
 strip.resize((2880,320),Image.Resampling.NEAREST).save(OUT/'fade-stages.png')
 (OUT/'layout.json').write_text(json.dumps(dict(canvas=[240,160],logo_box=list(LOGO_BOX),logo='TitleLogo@ZhHans.png precomposited into background',background_crop=dict(ema=[24,0,264,160],hiro=[80,0,320,160]),background_intermediate=[320,160],background_colors=240,prompt_group=[192,140,42,14],prompt='existing A icon font + 12px 开始 with1px black shadow',entry_fade=dict(ema='black',hiro='white',frames=32,hardware_levels=16),ema_background_offset=[-24,0],music=dict(alias='Bgm_36_1',title='gDie Divil JIO',loop=True),selection='Any valid current manual/auto Flash slot with Hiro hero metadata selects Hiro; otherwise Ema. Do not use global chapter unlock flags or RAM-only state.',pictures=stats,stage='Expected appearance only; not yet integrated into application.'),ensure_ascii=False,indent=2)+'\n');print(OUT)
if __name__=='__main__':main()
