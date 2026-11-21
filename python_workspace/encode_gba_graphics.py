#!/usr/bin/env python3
"""Encode shared import objects into bounded native BG/OBJ resources (no full-frame movies)."""
from __future__ import annotations
import argparse
import collections
import io
import json
import math
import struct
import sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageFilter
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'gba/tools')]
from graphics_project import ContentCache,canonical,config,digest,write_changed
from graphics_archive import build_archive
from cg_detail_protection import regions_for,tile_positions,transformed_regions
from image_encoding import rgb555, quantized, train
from obj_parts import split_tall_parts

SHAPES=[(8,8),(8,4),(4,8),(4,4),(4,2),(2,4),(2,2),(4,1),(1,4),(2,1),(1,2),(1,1)]


def load_image(cache,asset):
    return Image.open(cache.path(asset['png'],'.png')).convert('RGBA')


def palette_bytes(colors):return struct.pack('<'+'H'*len(colors),*[rgb555(c) for c in colors])


def color_centers(samples, count):
    """Keep small accents: sample native pixels and soften frequency bias."""
    packed, frequency = np.unique(np.concatenate(samples), return_counts=True)
    colors = np.column_stack((packed & 31, (packed >> 5) & 31, (packed >> 10) & 31)).astype(np.float32)*8
    if len(colors) <= count:
        result = colors.astype(np.uint8).tolist()
        return [tuple(c) for c in result] + [tuple(result[-1])]*(count-len(result))
    weights = np.sqrt(frequency).astype(np.float32)
    centers = [colors[np.argmax(frequency)]]
    distance = np.full(len(colors), np.inf, dtype=np.float32)
    for _ in range(1, count):
        distance = np.minimum(distance, np.sum((colors-centers[-1])**2, axis=1))
        centers.append(colors[np.argmax(distance*weights)])
    centers = np.array(centers)
    for _ in range(8):
        distances = np.sum((colors[:,None,:]-centers[None,:,:])**2, axis=2)
        assignment = np.argmin(distances, axis=1)
        for index in range(count):
            selected = assignment == index
            if np.any(selected): centers[index] = np.average(colors[selected], axis=0, weights=weights[selected])
    return [tuple(int(v)//8*8 for v in row) for row in centers]


def train_family(assets,cache):
    key=digest(dict(step='obj-palette-v2-accents',poses=sorted(a['png'] for a in assets),colors=60))
    old=cache.get_json(key)
    if old:
        old['colors']=[[tuple(c) for c in bank] for bank in old['colors']]
        return old
    samples=[[] for _ in range(4)]
    for asset in assets:
        image=np.asarray(load_image(cache,asset),dtype=np.uint16)
        for bank in range(4):
            # Subsample without averaging eyelashes, eyes and hair ornaments away.
            part=image[image.shape[0]*bank//4:image.shape[0]*(bank+1)//4:2,::2]
            rgb=part[:,:,:3][part[:,:,3]>=200]>>3
            if len(rgb): samples[bank].append(rgb[:,0]|(rgb[:,1]<<5)|(rgb[:,2]<<10))
    banks=[color_centers(sample,15) if sample else [(0,0,0)]*15 for sample in samples]
    result=dict(key=key,colors=banks,anchor_y=collections.Counter(a['bbox'][1] for a in assets).most_common(1)[0][0]-7,blob=cache.put_blob(palette_bytes(sum(([(0,0,0)]+b for b in banks),[]))))
    cache.put_json(key,result);return result


def rectangles(occupied):
    remaining=set(occupied);result=[]
    while remaining:
        x,y=min(remaining,key=lambda p:(p[1],p[0]))
        candidates=[]
        for w,h in SHAPES:
            covered={(px,py) for px in range(x,x+w) for py in range(y,y+h)}&remaining
            if len(covered)*4>=w*h*3: candidates.append((len(covered),-w*h,w,h,covered))
        _,_,w,h,covered=max(candidates,key=lambda p:(p[0],p[1]))
        result.append((x,y,w,h));remaining-=covered
    return result


def encode_actor(asset,family,cache):
    key=digest(dict(step='obj-native-v1',png=asset['png'],palette=family['key'],anchor=family['anchor_y']))
    old=cache.get_json(key)
    if old:return old
    image=load_image(cache,asset);alpha=image.getchannel('A')
    occupied={(x//8,y//8) for y in range(0,image.height,8) for x in range(0,image.width,8)
              if alpha.crop((x,y,x+8,y+8)).point(lambda a:255 if a>=128 else 0).getbbox()}
    parts=[];blobs={family['blob']};tiles=0
    for x,y,w,h in rectangles(occupied):
        part=image.crop((x*8,y*8,(x+w)*8,(y+h)*8))
        rgb=np.array(part.convert('RGB'),dtype=np.int16);mask=np.array(part.getchannel('A'))>=128
        choices=[]
        for bank,colors in enumerate(family['colors']):
            q=quantized(part,colors)
            if asset['kind']=='background':
                q=quantize_background(part,colors).point(lambda v:v-1).convert('P')
                q.putpalette(sum((list(c) for c in colors),[])+[0]*(768-3*len(colors)))
            reconstructed=np.array(q.convert('RGB'),dtype=np.int16)
            choices.append((int(((rgb-reconstructed).astype(np.int32)**2)[mask].sum()),bank,q))
        _,bank,q=min(choices,key=lambda t:t[0])
        pixels=np.array(q,dtype=np.uint8)+1;pixels[~mask]=0
        refs=[]
        for ty in range(h):
            for tx in range(w):
                t=pixels[ty*8:ty*8+8,tx*8:tx*8+8]
                raw=(t[:,0::2]|(t[:,1::2]<<4)).tobytes()
                tile=cache.put_blob(raw);refs.append(tile);blobs.add(tile)
        parts.append(dict(x=x*8,y=y*8,width=w*8,height=h*8,bank=bank,tiles=refs));tiles+=w*h
    # The original canvas coordinate origin survives pose changes and cropping.
    result=dict(id=asset['id'],kind='obj',width=image.width,height=image.height,
                pivot=[image.width//2,family['anchor_y']],parts=parts,palette=family['blob'],
                tile_bytes=tiles*32,blobs=sorted(blobs),rom_directory_bytes=40+len(parts)*12+tiles*4)
    cache.put_json(key,result);return result


def background_features(rgb):
    rgb=np.asarray(rgb,dtype=np.float32)
    r,g,b=rgb[...,0],rgb[...,1],rgb[...,2]
    # Background error favors luminance and proportions, unlike OBJ accent sampling.
    return np.stack(((r+2*g+b)*.5,(r-b)*.375,(-r+2*g-b)*.1875),axis=-1)


def nearest(features,centers):
    result=np.empty(len(features),dtype=np.int32)
    norms=np.sum(centers*centers,axis=1)
    for at in range(0,len(features),256):
        batch=features[at:at+256]
        distances=np.sum(batch*batch,axis=1)[:,None]+norms[None,:]-2*batch@centers.T
        result[at:at+256]=np.argmin(distances,axis=1)
    return result


def background_palette(image,count=111):
    rgb=np.asarray(image.convert('RGB'),dtype=np.uint16).reshape(-1,3)>>3
    packed=rgb[:,0]|(rgb[:,1]<<5)|(rgb[:,2]<<10)
    values,frequency=np.unique(packed,return_counts=True)
    colors=np.column_stack((values&31,(values>>5)&31,(values>>10)&31)).astype(np.float32)*8
    features=background_features(colors)
    centers=np.array(train(list(image.convert('RGB').get_flattened_data()),count),dtype=np.float32)
    for _ in range(6):
        assignment=nearest(features,background_features(centers))
        for i in range(count):
            selected=assignment==i
            if np.any(selected):centers[i]=np.average(colors[selected],axis=0,weights=frequency[selected])
    return [tuple(int(v) for v in row) for row in np.clip(np.rint(centers/8)*8,0,248)]


def quantize_background(image,colors):
    rgb=np.array(image.convert('RGB'),dtype=np.float32)
    assignment=nearest(background_features(rgb.reshape(-1,3)),background_features(colors))
    out=Image.fromarray((assignment.reshape(rgb.shape[:2])+1).astype(np.uint8))
    return out


def vector_tiles(indexed,colors,count=255,protected=()):
    pixels=np.array(indexed,dtype=np.uint8)
    height,width=pixels.shape
    canvas=np.zeros((512,512),dtype=np.uint8)
    left=(512-width)//2;top=(512-height)//2
    canvas[top:top+height,left:left+width]=pixels
    tiles=canvas.reshape(64,8,64,8).transpose(0,2,1,3).reshape(-1,64)
    unique,inverse,frequency=np.unique(tiles,axis=0,return_inverse=True,return_counts=True)
    nonzero=np.where(np.any(unique,axis=1))[0]
    positions=tile_positions(protected,left,top)
    protected_ids=np.unique([inverse[y*64+x] for x,y in positions]).astype(np.int32)
    protected_ids=protected_ids[np.any(unique[protected_ids],axis=1)] if len(protected_ids) else protected_ids
    if len(protected_ids)>count:raise ValueError('Protected details exceed the tile budget')
    locked=np.searchsorted(nonzero,protected_ids)
    rgb_table=np.asarray([(0,0,0)]+colors,dtype=np.float32)
    table=np.column_stack((background_features(rgb_table),np.r_[0,np.full(len(colors),255)]))
    features=table[unique[nonzero]].reshape(-1,256).astype(np.float32)
    if len(nonzero)<=count:
        assignment=np.arange(len(nonzero));dictionary=unique[nonzero]
    else:
        chosen=[int(np.argmax(frequency[nonzero]))]
        distance=np.full(len(nonzero),np.inf,dtype=np.float32)
        for _ in range(1,count):
            delta=features-features[chosen[-1]]
            distance=np.minimum(distance,np.sum(delta*delta,axis=1))
            chosen.append(int(np.argmax(distance*frequency[nonzero])))
        centers=features[chosen]
        dictionary=unique[nonzero[chosen]]
        weights=frequency[nonzero].astype(np.float32)
        # Refine actual 8x8 patterns, then snap each averaged pixel to the BG palette.
        # This reduces block substitution errors without increasing tile count or ROM.
        for _ in range(4):
            assignment=nearest(features,centers)
            sums=np.zeros_like(centers);totals=np.zeros(count,dtype=np.float32)
            np.add.at(sums,assignment,features*weights[:,None]);np.add.at(totals,assignment,weights)
            present=totals>0;centers[present]=sums[present]/totals[present,None]
            dictionary=nearest(centers.reshape(-1,4),table).reshape(count,64).astype(np.uint8)
            centers=table[dictionary].reshape(count,256).astype(np.float32)
        assignment=nearest(features,centers)
        if len(locked):
            # Keep the original codebook stable. Each exact detail displaces the
            # unlocked tile with the smallest weighted reassignment loss.
            distances=np.maximum(0,np.sum(features*features,axis=1)[:,None]+np.sum(centers*centers,axis=1)[None,:]-2*features@centers.T)
            reserved=np.zeros(count,bool);slots=[]
            for source_id in protected_ids:
                tile=unique[source_id];existing=np.flatnonzero(np.all(dictionary==tile,axis=1))
                if len(existing):slot=int(existing[0])
                else:
                    tile_features=table[tile].reshape(256);new_cost=np.sum((features-tile_features)**2,axis=1)
                    best=distances.argmin(axis=1);first=distances[np.arange(len(features)),best]
                    second=np.partition(distances,1,axis=1)[:,1] if count>1 else np.full(len(features),np.inf)
                    loss=np.bincount(best,weights=np.maximum(0,np.minimum(second,new_cost)-np.minimum(first,new_cost))*weights,minlength=count)
                    loss[reserved]=np.inf;slot=int(loss.argmin());dictionary[slot]=tile;distances[:,slot]=new_cost
                reserved[slot]=True;slots.append(slot)
            assignment=distances.argmin(axis=1)
            assignment[locked]=slots
    dictionary=np.concatenate([np.zeros((1,64),dtype=np.uint8),dictionary])
    mapping=np.zeros(len(unique),dtype=np.uint8);mapping[nonzero]=assignment+1
    cells=mapping[inverse].reshape(64,64)
    actual=dictionary[cells].reshape(64,64,8,8).transpose(0,2,1,3).reshape(512,512)
    error=rgb_table[canvas]-rgb_table[actual]
    mse=float(np.mean((error[top:top+height,left:left+width])**2))
    return dictionary,cells,99. if not mse else 10*math.log10(255*255/mse)


def encode_background(asset,cache,blur_levels,base_scales=()):
    protected=regions_for(asset)
    key_data=dict(step='affine-native-v2-tone',png=asset['png'],colors=111,tiles=255,blur_levels=blur_levels,base_scales=sorted(base_scales))
    if protected:key_data.update(protected=protected,protection_version=2)
    key=digest(key_data)
    old=cache.get_json(key)
    if old:return old
    image=load_image(cache,asset)
    if image.width>496 or image.height>496: image.thumbnail((496,496),Image.Resampling.LANCZOS)
    colors=background_palette(image)
    palette=cache.put_blob(palette_bytes([(0,0,0)]+colors));blobs={palette};variants=[]
    for level in range(blur_levels):
        current=image if not level else image.filter(ImageFilter.GaussianBlur(2.2*level/(blur_levels-1)))
        indexed=quantize_background(current,colors)
        dictionary,cells,psnr=vector_tiles(indexed,colors,protected=protected if not level else ())
        refs=[]
        for tile in dictionary:
            ref=cache.put_blob(tile.tobytes());refs.append(ref);blobs.add(ref)
        map_id=cache.put_blob(cells.tobytes());blobs.add(map_id)
        variants.append(dict(tiles=refs,map=map_id,psnr=round(psnr,2)))
    base_variants=[]
    for scale in sorted(base_scales):
        size=(max(1,round(image.width*scale/256)),max(1,round(image.height*scale/256)))
        scaled=image.resize(size,Image.Resampling.LANCZOS)
        # The regular lower BG needs only its 256x256 viewport neighborhood.
        cx,cy=scaled.width//2,scaled.height//2
        scaled=scaled.crop((cx-128,cy-128,cx+128,cy+128))
        indexed=quantize_background(scaled,colors)
        mapped_protection=transformed_regions(protected,size[0]/image.width,size[1]/image.height,128-cx,128-cy,256,256)
        dictionary,cells,psnr=vector_tiles(indexed,colors,127,protected=mapped_protection)
        refs=[]
        for tile in dictionary:
            ref=cache.put_blob(tile.tobytes());refs.append(ref);blobs.add(ref)
        regular=cells[16:48,16:48].astype('<u2')
        map_id=cache.put_blob(regular.tobytes());blobs.add(map_id)
        base_variants.append(dict(tiles=refs,map=map_id,scale=scale,psnr=round(psnr,2)))
    result=dict(id=asset['id'],kind='bg',width=image.width,height=image.height,variants=variants,base_variants=base_variants,palette=palette,
                blobs=sorted(blobs),rom_directory_bytes=40+len(variants)*(16+4*256)+len(base_variants)*(16+4*128))
    cache.put_json(key,result);return result


def encode_scene_proxy(asset,cache,blur_levels):
    """An occasional third BG uses spare OBJ slots, with BG tone-weighted palettes."""
    key=digest(dict(step='bg-obj-proxy-v1',png=asset['png'],levels=blur_levels))
    old=cache.get_json(key)
    if old:return old
    image=load_image(cache,asset)
    banks=[background_palette(image.crop((0,image.height*i//4,image.width,image.height*(i+1)//4)),15) for i in range(4)]
    family=dict(key=key,colors=banks,anchor_y=image.height//2,blob=cache.put_blob(palette_bytes(sum(([(0,0,0)]+b for b in banks),[]))))
    result=[]
    for level in range(blur_levels):
        current=image if not level else image.filter(ImageFilter.GaussianBlur(2.2*level/(blur_levels-1)))
        stream=io.BytesIO();current.save(stream,format='PNG');png=cache.put_blob(stream.getvalue(),'.png')
        source=dict(asset,png=png,id=digest(dict(proxy=key,level=level)))
        result.append(encode_actor(source,family,cache))
    cache.put_json(key,result);return result


def encode_mask(asset,cache,steps):
    key=digest(dict(step='mask-native-v2-columns',png=asset['png'],steps=steps))
    old=cache.get_json(key)
    if old:return old
    mask=np.array(Image.open(cache.path(asset['png'],'.png')).convert('L').resize((240,160)),dtype=np.int16)
    bayer=np.array([[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]],dtype=np.int16)
    thresholds=np.tile(bayer,(40,60));frames=[];blobs=set()
    for phase in range(steps+1):
        coverage=np.clip((round(phase*288/steps)-mask)*16//32,0,16)
        if not phase: coverage[:]=0
        if phase==steps: coverage[:]=16
        visible=np.where(thresholds<coverage,224,0).astype(np.uint8)
        if np.all(visible==np.tile(visible[:8],(20,1))):
            # Vertical blinds repeat every tile row. A fixed column map removes
            # all per-phase maps and permits updating only changed column tiles.
            columns=visible[:8].reshape(8,30,8).transpose(1,0,2).reshape(30,64)
            refs=[cache.put_blob(bytes(64))]+[cache.put_blob(tile.tobytes()) for tile in columns]
            blobs.update(refs);frames.append(dict(tiles=refs,map=None));continue
        canvas=np.zeros((256,256),dtype=np.uint8);canvas[:160,:240]=visible
        tiles=canvas.reshape(32,8,32,8).transpose(0,2,1,3).reshape(-1,64)
        unique,inverse=np.unique(tiles,axis=0,return_inverse=True)
        if len(unique)>256:raise ValueError(f'Mask requires {len(unique)} tiles (limit 256), phase {phase}/{steps}')
        refs=[]
        for tile in unique:
            ref=cache.put_blob(tile.tobytes());refs.append(ref);blobs.add(ref)
        map_id=cache.put_blob(inverse.astype('<u2').tobytes());blobs.add(map_id)
        frames.append(dict(tiles=refs,map=map_id))
    result=dict(id=asset['id'],kind='mask',frames=frames,blobs=sorted(blobs),
                rom_directory_bytes=32+sum(12+4*len(f['tiles']) for f in frames))
    cache.put_json(key,result);return result


def run(cache_root=None,link=True):
    cache=ContentCache(cache_root);project=config()
    imported=json.loads((cache.root/'import.json').read_text())
    if imported['stats']['errors']:raise ValueError('Resolve import errors before encoding/linking')
    assets=imported['assets'];encoded={};families=collections.defaultdict(list)
    programs=json.loads((cache.root/'programs.json').read_text())
    selected=sorted(programs) if project['rom_scripts']=='all' else project['rom_scripts']
    wanted=set()
    trial_poses=cache.root/'trial-poses.json'
    extras=json.loads(trial_poses.read_text())['selected'] if trial_poses.exists() else []
    wanted.update(extras)
    for path in selected:
        program=cache.get_json(programs[path]);wanted.update(program['resources']);wanted.update(program.get('mask_resources',{}).values())
    from graphics_usage import resource_usage,scene_proxy_usage
    active_programs=[cache.get_json(programs[p]) for p in selected]
    blurred,base_scales=resource_usage(active_programs,assets)
    scene_proxies=scene_proxy_usage(active_programs,assets)
    pending=list(wanted)
    while pending:
        key=pending.pop()
        for child in assets[key].get('frames',[]):
            if child not in wanted:wanted.add(child);pending.append(child)
    for asset in assets.values():
        if asset['kind'] in ('character','sprite'):families[asset['who']].append(asset)
    for who,poses in sorted(families.items()):
        print('encode OBJ',who,len(poses),flush=True)
        family=train_family(poses,cache)
        for asset in poses:
            if asset['id'] in wanted: encoded[asset['id']]=encode_actor(asset,family,cache)
    for asset in assets.values():
        if asset['id'] not in wanted:continue
        if asset['kind']=='background':
            print('encode BG',asset.get('namespace'),asset.get('appearance'),flush=True)
            encoded[asset['id']]=encode_background(asset,cache,project['effects']['blur_levels'] if asset['id'] in blurred else 1,base_scales.get(asset['id'],()))
            if asset.get('namespace')=='Stills':
                from cg_regular_assets import encode as encode_regular_cg
                encoded[asset['id']]=encode_regular_cg(asset,encoded[asset['id']],cache)
        elif asset['kind']=='mask':encoded[asset['id']]=encode_mask(asset,cache,project['effects']['mask_steps'])
        elif asset['kind']=='animation':encoded[asset['id']]=dict(asset,blobs=[],rom_directory_bytes=24+4*len(asset['frames']))
        elif asset['kind'] in ('solid','rectangle'):
            encoded[asset['id']]=dict(asset,blobs=[],rom_directory_bytes=32)
    for key in sorted(scene_proxies):
        print('encode BG OBJ proxy',key[:12],flush=True)
        proxies=encode_scene_proxy(assets[key],cache,project['effects']['blur_levels'] if key in blurred else 1)
        encoded[key]['obj_frames']=[proxy['id'] for proxy in proxies]
        for proxy in proxies:encoded[proxy['id']]=proxy
    for asset in encoded.values():
        if asset['kind']=='obj':
            previous=len(asset['parts'])
            asset['parts']=split_tall_parts(asset['parts'])
            asset['rom_directory_bytes']+=12*(len(asset['parts'])-previous)
    write_changed(cache.root/'encoded.json',canonical(encoded))
    return build_archive(cache,encoded,programs,project=project,extra_resources=extras) if link else dict(assets=len(encoded),cache_hits=cache.hits,cache_misses=cache.misses)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cache',type=Path);parser.add_argument('--no-link',action='store_true')
    args=parser.parse_args();print(json.dumps(run(args.cache,not args.no_link),ensure_ascii=False,indent=2))
