"""Warm protected CG encodings in parallel and verify exact detail pixels in native sharp/base variants."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import argparse,json,sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from PIL import Image
from graphics_project import GBA,ContentCache
from cg_detail_protection import regions_for,tile_positions,transformed_regions
sys.path.insert(0,str(GBA.parent/'python_workspace'))
from encode_gba_graphics import encode_background,load_image,quantize_background

def decode(cache,variant,size):
 tiles=np.array([np.frombuffer(cache.path(t,'.bin').read_bytes(),np.uint8) for t in variant['tiles']]);mapping=np.frombuffer(cache.path(variant['map'],'.bin').read_bytes(),np.uint8 if size==512 else '<u2').reshape(size//8,size//8)
 return tiles[mapping].reshape(size//8,size//8,8,8).transpose(0,2,1,3).reshape(size,size)

def check_detail(target,actual,regions,origin_x=0,origin_y=0):
 pixels=0
 for tx,ty in tile_positions(regions,origin_x,origin_y):
  a=actual[ty*8:ty*8+8,tx*8:tx*8+8];b=target[ty*8:ty*8+8,tx*8:tx*8+8];assert np.array_equal(a,b),f'protected tile {(tx,ty)} is not exact';pixels+=64
 return pixels

def work(spec):
 asset,old=spec;cache=ContentCache();regions=regions_for(asset);result=encode_background(asset,cache,len(old['variants']),[v['scale'] for v in old.get('base_variants',[])]);image=load_image(cache,asset);p=np.frombuffer(cache.path(result['palette'],'.bin').read_bytes(),'<u2').astype(np.int32);colors=np.stack((p&31,(p>>5)&31,(p>>10)&31),axis=1)*8;native=np.array(quantize_background(image,colors[1:]));left=(512-image.width)//2;top=(512-image.height)//2;target=np.zeros((512,512),np.uint8);target[top:top+image.height,left:left+image.width]=native;actual=decode(cache,result['variants'][0],512);pixels=check_detail(target,actual,regions,left,top)
 base_pixels=0
 for variant in result.get('base_variants',[]):
  scale=variant['scale'];size=(max(1,round(image.width*scale/256)),max(1,round(image.height*scale/256)));scaled=image.resize(size,Image.Resampling.LANCZOS);cx,cy=scaled.width//2,scaled.height//2;scaled=scaled.crop((cx-128,cy-128,cx+128,cy+128));mapped=transformed_regions(regions,size[0]/image.width,size[1]/image.height,128-cx,128-cy,256,256);base_target=np.array(quantize_background(scaled,colors[1:]));base_pixels+=check_detail(base_target,decode(cache,variant,256),mapped)
 protected_old=decode(cache,old['variants'][0],512);fixed=sum(np.count_nonzero(protected_old[ty*8:ty*8+8,tx*8:tx*8+8]!=actual[ty*8:ty*8+8,tx*8:tx*8+8]) for tx,ty in tile_positions(regions,left,top))
 return dict(name=asset['appearance'],regions=len(regions),protected_tiles=pixels//64,sharp_exact_pixels=pixels,base_exact_pixels=base_pixels,changed_pixels=int(fixed),old_psnr=old['variants'][0]['psnr'],new_psnr=result['variants'][0]['psnr'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path);args=p.parse_args()
 cache=ContentCache();imports=json.loads((cache.root/'import.json').read_text())['assets'];encoded=json.loads((args.baseline or cache.root/'encoded.json').read_text());selected=[(a,encoded[k]) for k,a in imports.items() if k in encoded and regions_for(a)]
 with ProcessPoolExecutor(max_workers=12) as pool:rows=list(pool.map(work,selected))
 out=GBA/'build-butano/cg-detail-protection';out.mkdir(exist_ok=True);report=dict(passed=True,images=len(rows),regions=sum(r['regions'] for r in rows),protected_tiles=sum(r['protected_tiles'] for r in rows),sharp_exact_pixels=sum(r['sharp_exact_pixels'] for r in rows),base_exact_pixels=sum(r['base_exact_pixels'] for r in rows),changed_pixels=sum(r['changed_pixels'] for r in rows),pictures=sorted(rows,key=lambda r:r['name']))
 (out/'formal-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='pictures'},indent=2))
if __name__=='__main__':main()
