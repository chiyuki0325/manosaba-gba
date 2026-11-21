"""Gallery index + tiny thumbnail palettes; CG pixels remain in the shared archive."""
from pathlib import Path
import json,struct
import numpy as np
from PIL import Image
from graphics_project import ContentCache
GBA=Path(__file__).resolve().parents[1]
ORDER='1 2 140 490 500 10 270 580 200 410 120 520 220 100 510 130 280 420 421 190 560 400 40 290 50 180 550 260 30 300 310 210 160 20 430 360 370 150 80 110 320 590 440 230 530 330 450 451 250 540 340 460 240 350 690 470 170 570 700 480 380 600 610 620 640 660 670 650 680 630'.split()
def main():
    cache=ContentCache();imported=json.loads((cache.root/'import.json').read_text())['assets'];linked=json.loads((cache.root/'linked/graphics.json').read_text())['assets'];encoded=json.loads((cache.root/'encoded.json').read_text());ids={k:i for i,k in enumerate(sorted(linked))}
    widths=sorted(set(a['size'][0] for k,a in imported.items() if k in ids and a.get('namespace')=='Stills'))
    groups=[];images=[];cpp=['#include "gallery_data.h"','namespace manosaba {'];meta=[]
    for number in ORDER:
        assets=sorted((a for k,a in imported.items() if k in ids and a.get('namespace')=='Stills' and a['appearance'].split('_')[0]==number),key=lambda a:tuple(map(int,a['appearance'].split('_'))))
        if not assets:continue
        first=len(images)
        for a in assets:
            colors=np.array(struct.unpack('<112H',cache.path(encoded[a['id']]['palette'],'.bin').read_bytes()),dtype=np.uint16)
            rgb=np.stack((colors&31,(colors>>5)&31,(colors>>10)&31),axis=-1).astype(np.int32)*8
            strip=Image.fromarray(rgb.astype(np.uint8)[None,1:,:]).quantize(colors=32,method=Image.Quantize.MEDIANCUT)
            p=np.array((strip.getpalette()+[0]*96)[:96],np.int32).reshape(32,3)
            remap=np.argmin(((rgb[:,None,:]-p[None,:,:])**2).sum(axis=2),axis=1)
            palette=(p[:,0]//8)|((p[:,1]//8)<<5)|((p[:,2]//8)<<10)
            i=len(images);cpp.append('const uint16_t palette%d[]={%s};'%(i,','.join(map(str,palette))));cpp.append('const uint8_t remap%d[]={%s};'%(i,','.join(map(str,remap))))
            images.append((ids[a['id']],i));meta.append(dict(index=i,asset=ids[a['id']],name=a['appearance'],group=len(groups)))
        groups.append((first,len(assets),int(number)))
    cpp+=['const uint16_t gallery_columns[][72]={'+','.join('{'+','.join(str(((512-w)//2+x*w//72)//8 | ((((512-w)//2+x*w//72)%8)<<6)) for x in range(72))+'}' for w in widths)+'};','const gallery_image gallery_images[]={'+','.join('{%d,%d,palette%d,remap%d}'%(asset,widths.index(imported[sorted(linked)[asset]]['size'][0]),i,i) for asset,i in images)+'};','const gallery_group gallery_groups[]={'+','.join('{%d,%d,%d}'%g for g in groups)+'};','const uint8_t gallery_asset_order[]={'+','.join(str(i) for _,i in sorted(images))+'};','}']
    (GBA/'src/gallery_data.cpp').write_text('\n'.join(cpp)+'\n')
    (GBA/'src/gallery_data.h').write_text('#pragma once\n#include <cstdint>\nnamespace manosaba {\nstruct gallery_image {uint16_t asset,columns;const uint16_t* palette;const uint8_t* remap;};\nstruct gallery_group {uint8_t first,count;uint16_t number;};\ninline constexpr int gallery_image_count=%d,gallery_group_count=%d,gallery_flag_bytes=(gallery_image_count+7)/8;\nextern const uint16_t gallery_columns[][72];\nextern const gallery_image gallery_images[];\nextern const gallery_group gallery_groups[];\nextern const uint8_t gallery_asset_order[];\n}\n'%(len(images),len(groups)))
    out=GBA/'assets/gallery';out.mkdir(exist_ok=True);(out/'catalog.json').write_text(json.dumps(dict(groups=[dict(first=f,count=c,number=n) for f,c,n in groups],images=meta,pixel_bytes=0,palette_remap_bytes=len(images)*(64+112)),indent=2)+'\n')
    print('Gallery:',len(groups),'groups,',len(images),'variants; no new image pixels')
if __name__=='__main__':main()
