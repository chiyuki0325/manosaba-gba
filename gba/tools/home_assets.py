"""Bake title/logo and the existing choice-style home menus into ROM assets."""
from build_paths import generated_path
from pathlib import Path
import json,struct
import numpy as np
from PIL import Image
from image_encoding import UI_COLORS,tiles,rgb555
from title_screen_mockups import art,prompt
from start_menu_mockups import background,frame
from story import compress
ROOT=Path(__file__).resolve().parents[2];GBA=ROOT/'gba';OUT=GBA/'assets/home'

def main():
 OUT.mkdir(exist_ok=True);pages=[];patches=[]
 for kind in range(2):
  base=background()
  for row in range(4 if kind else 5):frame(base,36 if not kind else 16,4+31*row if not kind else 24+25*row,168 if not kind else 208,28 if not kind else 24,False)
  pages.append(base.convert('RGB'))
  group=[]
  for row in range(5):
   pair=[];x,y,w,h=(36,4+31*row,168,28) if not kind else (16,24+25*row,208,24)
   for selected in range(2):
    im=background();frame(im,x,y,w,h,bool(selected));pair.append(im.convert('RGB').crop((x,y,x+w,y+h)))
   group.append((x,y,w,h,pair))
  patches.append(group)
 images=[prompt(art(1)),prompt(art(2)),*pages,*[im for group in patches for _,_,_,_,pair in group for im in pair]]
 sample=Image.new('RGB',(sum(im.width for im in images),160));at=0
 for im in images:sample.paste(im,(at,0));at+=im.width
 extra=sample.quantize(colors=240,method=Image.Quantize.MEDIANCUT).getpalette()[:720];colors=UI_COLORS+[tuple(extra[i:i+3]) for i in range(0,720,3)];p=np.asarray(colors,np.int16)//8
 def quant(im):
  a=np.asarray(im.convert('RGB'))>>3;unique,inverse=np.unique(a.reshape(-1,3),axis=0,return_inverse=True);distance=((unique.astype(np.int16)[:,None,:]-p[None,1:,:])**2).sum(axis=2);idx=(distance.argmin(axis=1)+1).astype(np.uint8);return Image.fromarray(idx[inverse].reshape(a.shape[:2]))
 binary=bytearray();decl=['#include "home_art.h"','namespace manosaba {','extern const uint8_t home_bytes[];'];pal=struct.pack('<256H',*[rgb555(c) for c in colors]);binary.extend(pal)
 def put(data):
  binary.extend(bytes(-len(binary)%4));pos=len(binary);binary.extend(data);return pos
 entries=[]
 for index,im in enumerate(images[:4]):
  raw=tiles(quant(im),8);blocks=[]
  for start in range(0,len(raw),4096):
   part=raw[start:start+4096];packed=compress(part);codec=int(len(packed)<len(part));offset=put(packed if codec else part);blocks.append('{home_bytes+%d,%d}'%(offset,codec))
  decl.append('static const ui_picture_block blocks_%d[]={%s};'%(index,','.join(blocks)));entries.append('{reinterpret_cast<const uint16_t*>(home_bytes),nullptr,blocks_%d}'%index)
 decl+=['const ui_picture home_titles[2]={'+','.join(entries[:2])+'};','const ui_picture home_menus[2]={'+','.join(entries[2:])+'};'];groups=[]
 for group in patches:
  rows=[]
  for x,y,w,h,pair in group:
   rows.append('{'+','.join('{home_bytes+%d,%d,%d,%d,%d}'%(put(quant(im).tobytes()),w,h,x,y) for im in pair)+'}')
  groups.append('{'+','.join(rows)+'}')
 decl+=['const home_row_art home_rows[2][5][2]={'+','.join(groups)+'};','}'];(GBA/'src/home_art.cpp').write_text('\n'.join(decl)+'\n');(OUT/'home.bin').write_bytes(binary)
 (GBA/'src/home_bytes.s').write_text('.section .rodata.home_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba10home_bytesE\n_ZN8manosaba10home_bytesE:\n.incbin "../assets/home/home.bin"\n')
 (GBA/'src/home_art.h').write_text('#pragma once\n#include "ui_picture.h"\nnamespace manosaba {\nstruct home_row_art {const uint8_t* pixels;uint16_t width,height;uint8_t x,y;};\nextern const ui_picture home_titles[2],home_menus[2];\nextern const home_row_art home_rows[2][5][2];\n}\n')
 (generated_path('reports/home-art.json')).write_text(json.dumps(dict(bytes=len(binary),title='prebaked logo and A start',palette=256),indent=2)+'\n');print('Home assets',len(binary))
if __name__=='__main__':main()
