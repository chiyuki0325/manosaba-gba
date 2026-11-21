"""Reuse packed CG dictionaries; greedily add a few exact tiles instead of replacing the whole codebook."""
from __future__ import annotations
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import argparse,hashlib,json,math,sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from PIL import Image
from tile_orientation import orient
NAMES=['140_1','110_1','40_1']
from graphics_project import ContentCache,GBA,digest
from encode_gba_graphics import load_image,quantize_background,nearest
from cg_detail_protection import regions_for,tile_positions
MODES=['OLD256','REUSE HV','PLUS 16','PLUS 32','PLUS 64','AUTO +2dB','PLUS 96','FULL 600 HV']

def reconstruct(dictionary,ids,turns,inverse,columns):
 return np.array([orient(dictionary[i:i+1],t)[0] for i,t in zip(ids[inverse],turns[inverse])]).reshape(20,columns,8,8).transpose(0,2,1,3).reshape(160,columns*8)

def measure(target,pixels,rgb,width):
 error=(rgb[target[:,:width]]-rgb[pixels[:,:width]]).astype(np.float32);mse=float(np.mean(error*error));mid=(width-240)//2;v=(rgb[target[:,mid:mid+240]]-rgb[pixels[:,mid:mid+240]]).astype(np.float32);vmse=float(np.mean(v*v))
 return dict(mse=mse,psnr=99 if not mse else 10*math.log10(255**2/mse),viewport_psnr=99 if not vmse else 10*math.log10(255**2/vmse))

def task(spec):
 asset,encoded,outdir,protected_mode=spec;cache=ContentCache();out=Path(outdir);raw=cache.path(encoded['palette'],'.bin').read_bytes();p=np.frombuffer(raw,'<u2').astype(np.int32);rgb=np.stack((p&31,(p>>5)&31,(p>>10)&31),axis=1)*8
 image=load_image(cache,asset);width=image.width;origin=((512-width)//2)%8;columns=(width+origin+7)//8;target=np.pad(np.array(quantize_background(image,rgb[1:])),((0,0),(origin,columns*8-width-origin)))
 old=encoded['variants'][0];base=np.array([np.frombuffer(cache.path(t,'.bin').read_bytes(),np.uint8) for t in old['tiles']]);old_map=np.frombuffer(cache.path(old['map'],'.bin').read_bytes(),np.uint8).reshape(64,64);old_pixels=base[old_map].reshape(64,64,8,8).transpose(0,2,1,3).reshape(512,512)[(512-160)//2:(512-160)//2+160,(512-width)//2:(512-width)//2+width]
 protected=regions_for(asset) if protected_mode else []
 original=measure(target[:,origin:origin+width],old_pixels,rgb,width);baseline=original['psnr'];key_data=dict(step='cg-additive-rgb-v1',png=asset['png'],palette=encoded['palette'],base=old,gain_db=2,max_extra=96);key_data.update(dict(tile_origin=origin) if origin else {});key_data.update(dict(protected=protected,protection_version=1) if protected else {});key=digest(key_data);meta=out/(key+'.json')
 if meta.exists():return json.loads(meta.read_text())
 tiles=target.reshape(20,8,columns,8).transpose(0,2,1,3).reshape(-1,64);unique,inverse,frequency=np.unique(tiles,axis=0,return_inverse=True,return_counts=True)
 # Alpha keeps the unused transparent tile from replacing opaque dark pixels.
 table=np.column_stack((rgb,np.r_[0,np.full(111,255)])).astype(np.float32);features=table[unique].reshape(-1,256);variants=np.concatenate([orient(base,t) for t in range(4)]);assigned=nearest(features,table[variants].reshape(-1,256));ids=assigned%len(base);turns=assigned//len(base);cost=((features-table[variants[assigned]].reshape(-1,256))**2).sum(axis=1);dictionary=base.copy()
 protected_positions=tile_positions(protected,origin,0);protected_ids=np.unique([inverse[y*columns+x] for x,y in protected_positions]).astype(np.int32)
 exact={}
 def index_exact(tile,index):
  for turn in range(4):exact.setdefault(orient(tile,turn)[0].tobytes(),(index,turn))
 for index,tile in enumerate(base):index_exact(tile[None,:],index)
 forced=[];covered=set(exact)
 for index in protected_ids:
  tile=unique[index:index+1]
  if tile[0].tobytes() not in covered:
   forced.append(tile)
   covered.update(orient(tile,t)[0].tobytes() for t in range(4))
 if len(forced)>96:raise ValueError(f'{asset["appearance"]}: critical details exceed the96-block supplement budget')
 def enforce():
  for index in protected_ids:
   match=exact.get(unique[index].tobytes())
   if match is not None:ids[index],turns[index]=match;cost[index]=0
 enforce()
 captures={};auto=None;curve=[];threshold=original['mse']*10**(-2/10)
 def capture(extra):
  used=np.unique(ids);remap=np.zeros(len(dictionary),np.int32);remap[used]=np.arange(len(used));cells=(remap[ids[inverse]]|((turns[inverse]&1)<<10)|((turns[inverse]>>1)<<11)).astype('<u2');mapping=np.zeros((32,64),'<u2');mapping[:20,:columns]=cells.reshape(20,columns);pixels=reconstruct(dictionary,ids,turns,inverse,columns);blocks=dictionary[used];m=measure(target[:,origin:origin+width],pixels[:,origin:origin+width],rgb,width);path=out/(key+f'-{extra}.npz');np.savez(path,tiles=blocks,map=mapping,reference=pixels)
  complete=extra>=len(forced)
  if complete:
   for tx,ty in protected_positions:assert np.array_equal(pixels[ty*8:ty*8+8,tx*8:tx*8+8],target[ty*8:ty*8+8,tx*8:tx*8+8])
  # Check independently against a complete nearest-neighbour assignment at each saved point.
  check_variants=np.concatenate([orient(dictionary,t) for t in range(4)]);check=nearest(features,table[check_variants].reshape(-1,256));check_cost=((features-table[check_variants[check]].reshape(-1,256))**2).sum(axis=1);assert np.allclose(check_cost,cost,atol=.1)
  return dict(name=asset['appearance'],cache=path.name,blocks=len(blocks),tile_bytes=int(blocks.size),map_bytes=4096,extra_requested=extra,protected_tiles=len(protected_positions),protection_complete=complete,reserved_new_tiles=len(forced),origin_x=origin,source_size=[width,160],reference_width=columns*8,**m)
 for extra in range(97):
  # For 320px originals this equals RGB MSE exactly. Recheck the real unpadded image before stopping for 361px originals.
  mse=float(np.sum(cost*frequency)/(len(tiles)*64*3));curve.append([extra,mse])
  want_auto=auto is None and extra>=len(forced) and (mse<=threshold or extra==96)
  if extra in (0,16,32,64,96) or want_auto:
   snap=capture(extra);captures[extra]=snap
   if want_auto and (snap['mse']<=threshold or extra==96):auto=snap
  if extra==96:break
  index=int(np.argmax(cost*frequency));tile=forced[extra] if extra<len(forced) else unique[index:index+1];new_features=table[np.concatenate([orient(tile,t) for t in range(4)])].reshape(4,256);dist=((features[:,None,:]-new_features[None,:,:])**2).sum(axis=2);turn=dist.argmin(axis=1);best=dist[np.arange(len(unique)),turn];replace=best<cost;ids[replace]=len(dictionary);turns[replace]=turn[replace];cost[replace]=best[replace];index_exact(tile,len(dictionary));dictionary=np.concatenate([dictionary,tile]);enforce()
 if auto is None:auto=captures[96]
 result=dict(name=asset['appearance'],id=asset['id'],original=original,adaptive=auto,achieved_gain_db=auto['psnr']-baseline,cap_hit=auto['psnr']<baseline+2-1e-5,checkpoints={str(k):v for k,v in captures.items()},curve=curve)
 meta.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(asset['appearance'],'auto',auto['extra_requested'],'extra',auto['blocks'],'used',round(result['achieved_gain_db'],2),'dB',flush=True);return result

def accounting(rows,archive,out):
 existing={b['id'] for b in archive['blobs']};new=set();refs=maps=0;used=0
 for row in rows:
  s=row['adaptive'];used+=s['blocks'];refs+=s['blocks']*4;maps+=((s['source_size'][0]+7)//8)*20*2
  with np.load(out/s['cache']) as a:
   new.update(hashlib.sha256(t.tobytes()).hexdigest() for t in a['tiles'] if hashlib.sha256(t.tobytes()).hexdigest() not in existing)
 return dict(pictures=len(rows),new_unique_tiles=len(new),new_tile_bytes=len(new)*64,reference_bytes=refs,map_bytes=maps,descriptor_bytes=len(rows)*24,additional_data_bytes=len(new)*64+refs+maps+len(rows)*24,method='Keep old resources, reuse byte-identical existing tile blobs and palettes. Compact unused dictionary entries. Full-width maps; no renderer code or linker padding included.')

def replacement_accounting(rows,archive,out):
 selected={row['id']:row for row in rows};remaining=set();old_top=set();new={};old_dirs=new_dirs=0
 for key,asset in archive['assets'].items():
  if key not in selected:remaining.update(asset['blobs']);continue
  variant=asset['variants'][0];old_top.update(variant['tiles']);old_top.add(variant['map']);remaining.add(asset['palette']);old_dirs+=16+len(variant['tiles'])*4
  for v in asset['variants'][1:]+asset.get('base_variants',[]):remaining.update(v['tiles']);remaining.add(v['map'])
  s=selected[key]['adaptive']
  with np.load(out/s['cache']) as data:
   for tile in data['tiles']:
    raw=tile.tobytes();new[hashlib.sha256(raw).hexdigest()]=raw
   raw=data['map'][:20,:(s['source_size'][0]+s.get('origin_x',0)+7)//8].astype('<u2').tobytes();new[hashlib.sha256(raw).hexdigest()]=raw
  new_dirs+=16+s['blocks']*4
 sizes={b['id']:(b['size']+3)&~3 for b in archive['blobs']};replaced=old_top-remaining;removed=sum(sizes[k] for k in replaced);added=sum((len(raw)+3)&~3 for k,raw in new.items() if k not in remaining);delta=added-removed+new_dirs-old_dirs
 return dict(pictures=len(rows),old_sharp_exclusive_bytes=removed,new_sharp_bytes_not_shared_with_remaining=added,old_sharp_directory_bytes=old_dirs,new_sharp_directory_bytes=new_dirs,estimated_replace_delta=delta,old_sharp_blobs_removed=len(replaced-new.keys()),preserved='All other graphics plus existing blur/base/palette data',method='Replace each selected sharp variant with one adaptive dictionary/map; exact global blob dedup. Same 16B variant descriptor assumed. No formal renderer, required affine fallback or code/link padding included.')

def shared_animation_accounting(rows,archive,out,cache):
 selected={row['id']:row for row in rows};remaining=set();old_top=set();new={};old_dirs=new_dirs=0;details=[]
 def add(raw):new[hashlib.sha256(raw).hexdigest()]=raw
 for key,asset in archive['assets'].items():
  if key not in selected:remaining.update(asset['blobs']);continue
  old=asset['variants'][0];old_top.update(old['tiles']);old_top.add(old['map']);remaining.add(asset['palette']);old_dirs+=16+len(old['tiles'])*4
  for v in asset['variants'][1:]+asset.get('base_variants',[]):remaining.update(v['tiles']);remaining.add(v['map'])
  row=selected[key];s=row['adaptive'];width=s['source_size'][0];origin=((512-width)//2)%8;columns=(width+origin+7)//8
  base=[cache.path(t,'.bin').read_bytes() for t in old['tiles']];lookup={t:i for i,t in enumerate(base)};combined=list(base)
  with np.load(out/s['cache']) as candidate:
   remap=[]
   for tile in candidate['tiles']:
    raw=tile.tobytes()
    if raw not in lookup:lookup[raw]=len(combined);combined.append(raw)
    remap.append(lookup[raw])
   mapping=candidate['map'].copy();cells=mapping[:20,:columns];mapping[:20,:columns]=np.asarray(remap,dtype=np.uint16)[cells&1023]|(cells&3072);full=np.array([np.frombuffer(t,np.uint8) for t in combined]);flat=mapping[:20,:columns].ravel()
   actual=np.array([orient(full[(i&1023):(i&1023)+1],(i>>10)&3)[0] for i in flat]).reshape(20,columns,8,8).transpose(0,2,1,3).reshape(160,columns*8)
   assert np.array_equal(actual,candidate['reference'])
   path=out/(Path(s['cache']).stem+'-shared.npz');np.savez(path,tiles=full,map=mapping,reference=candidate['reference'])
  old_map=np.frombuffer(cache.path(old['map'],'.bin').read_bytes(),np.uint8).reshape(64,64);left=(512-width)//16;affine=old_map[22:42,left:left+columns].copy();restored=np.zeros((64,64),np.uint8);restored[22:42,left:left+columns]=affine
  assert np.array_equal(restored,old_map),'compact affine map must restore every old cell exactly'
  assert combined[:len(base)]==base,'affine prefix must retain original tile numbering'
  for tile in combined:add(tile)
  add(mapping[:20,:columns].astype('<u2').tobytes());add(affine.tobytes());new_dirs+=32+len(combined)*4
  details.append(dict(name=row['name'],shared_blocks=len(combined),affine_prefix_blocks=len(base),ordinary_map_bytes=columns*20*2,affine_map_bytes=affine.nbytes,origin_x=origin,cache=path.name))
 sizes={b['id']:(b['size']+3)&~3 for b in archive['blobs']};replaced=old_top-remaining;removed=sum(sizes[k] for k in replaced);added=sum((len(raw)+3)&~3 for k,raw in new.items() if k not in remaining);delta=added-removed+new_dirs-old_dirs
 return dict(pictures=len(rows),old_sharp_exclusive_bytes=removed,new_sharp_bytes_not_shared_with_remaining=added,old_sharp_directory_bytes=old_dirs,new_sharp_directory_bytes=new_dirs,estimated_replace_delta=delta,details=details,method='One shared tile dictionary per picture; old affine numbering retained as its prefix. Separate compact 8-bit affine and H/V regular maps; verify old affine map/prefix and new regular pixels bit-exact for every picture. Preserve blur/base/other assets. Add16B map descriptor per picture. No runtime/code/link padding included.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--all',action='store_true');p.add_argument('--protected',action='store_true');args=p.parse_args();cache=ContentCache();imports=json.loads((cache.root/'import.json').read_text())['assets'];encoded=json.loads((cache.root/'encoded.json').read_text());archive=json.loads((cache.root/'linked/graphics.json').read_text());out=GBA/('build-butano/cg-regular-protected' if args.protected else 'build-butano/cg-regular-economy');out.mkdir(exist_ok=True)
 selected=sorted((a for k,a in imports.items() if k in archive['assets'] and a.get('namespace')=='Stills' and a.get('kind')=='background' and (args.all or a.get('appearance') in NAMES)),key=lambda a:a['appearance'])
 with ProcessPoolExecutor(max_workers=12) as pool:rows=list(pool.map(task,[(a,encoded[a['id']],str(out),args.protected) for a in selected]))
 byname={r['name']:r for r in rows};quality=[]
 # A small native probe uses the three original pictures; the all-picture scan is purely offline.
 old_quality=json.loads((GBA/'build-butano/cg-regular-probe/quality.json').read_text())['results']
 for name in NAMES:
  row=byname[name];original=next(r for r in old_quality if r['name']==name and r['mode']==0);full=next(r for r in old_quality if r['name']==name and r['mode']==7)
  for mode in range(8):
   if mode in (0,7):
    record=dict(original if mode==0 else full);shutil_path=GBA/'build-butano/cg-regular-probe'/record['cache'];(out/record['cache']).write_bytes(shutil_path.read_bytes())
   else:record=dict(row['adaptive'] if mode==5 else row['checkpoints'][str({1:0,2:16,3:32,4:64,6:96}[mode])])
   record.update(mode=mode,label=MODES[mode]);quality.append(record)
  pictures=[]
  palette=np.frombuffer(cache.path(encoded[row['id']]['palette'],'.bin').read_bytes(),'<u2').astype(np.int32);rgb=np.stack((palette&31,(palette>>5)&31,(palette>>10)&31),axis=1)*8
  for mode in (0,1,2,3,4,5,6,7):
   r=quality[-8+mode]
   with np.load(out/r['cache']) as a:pix=a['reference'];left=(row['adaptive']['source_size'][0]-240)//2 if mode else 0;pix=pix[:,left:left+240];im=Image.fromarray(rgb[pix].astype(np.uint8)).resize((720,480),Image.Resampling.NEAREST);im.save(out/f'{name}-mode{mode}.png');pictures.append(im)
  for modes,label in (((0,2,3,4),'fixed'),((0,5,7),'adaptive')):
   combined=Image.new('RGB',(720*len(modes),480))
   for column,mode in enumerate(modes):combined.paste(pictures[mode],(column*720,0))
   combined.save(out/f'compare-{label}-{name}.png')
 report=dict(rows=rows,replacement_storage=replacement_accounting(rows,archive,out),shared_animation_storage=shared_animation_accounting(rows,archive,out,cache),incremental_storage_for_comparison=accounting(rows,archive,out),policy='Build one replacement dictionary from nearest H/V of the original patterns and greedy source tiles. Target +2dB, cap96 new candidates; compact unused entries. Original tile grid alignment is preserved, including 361px pictures. Optional annotated details are reserved before optional quality supplements. All decisions offline.')
 (out/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');(out/'quality.json').write_text(json.dumps(dict(results=quality,scope=report['policy']),ensure_ascii=False,indent=2)+'\n');print(json.dumps(report['replacement_storage'],indent=2))
if __name__=='__main__':main()
