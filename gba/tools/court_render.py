#!/usr/bin/env python3
"""Render the original Court prefab offline for prepared GBA court backgrounds."""
from pathlib import Path
import csv
import inspect
import itertools
import json
import math
import struct
import sys
from functools import lru_cache

import numpy as np
from PIL import Image
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler

GBA=Path(__file__).resolve().parents[1]
ROOT=GBA.parent
sys.path.insert(0,str(ROOT/'python_workspace'))
from graphics_project import ContentCache,digest,write_changed
from encode_gba_graphics import background_features,SHAPES

OUT=GBA/'assets/graphics-cache/court/views'
BUNDLE=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/general-prefabs_assets_all.bundle'
# Wide views deliberately exercise three seats; close views retain the original FOV.
VIEW_SETTINGS=[(256,4,5,0,0,False,60),(256,10,5.2,2.77,-4,False,30),(256,13,5.6,-2.77,4,False,30),(1024,7,4.6,0,0,True,60)]
# Formal framing: the original stand spans world Y=1..3.8 at Z=14.8.
# Project its top to y=90 and its base to y=160 in the 240x160 viewport.
FORMAL_FOCAL=160/(2*math.tan(math.radians(30/2)))
FORMAL_DEPTH=FORMAL_FOCAL*(3.8-1)/(160-90)
FORMAL_DISTANCE=14.8-FORMAL_DEPTH
FORMAL_HEIGHT=1+(160-80)*(3.8-1)/(160-90)
FORMAL_VIEW_SETTINGS=[(256,FORMAL_DISTANCE,FORMAL_HEIGHT,yaw,0,False,30) for yaw in (0,7,-7)]


def transform(t):
    p,q,s=t.m_LocalPosition,t.m_LocalRotation,t.m_LocalScale
    x,y,z,w=q.x,q.y,q.z,q.w
    result=np.eye(4,dtype=np.float32)
    result[:3,:3]=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                          [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                          [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])*[s.x,s.y,s.z]
    result[:3,3]=[p.x,p.y,p.z]
    return result


@lru_cache(maxsize=2)
def court_geometry(stage="Court"):
    env=UnityPy.load(str(BUNDLE));objects={o.path_id:o for o in env.objects}
    rows=list(csv.DictReader((ROOT/'original_sprites/manifest.csv').open()))
    texture_paths={int(r['path_id']):ROOT/'original_sprites'/r['output'] for r in rows if r['type']=='Texture2D'}
    textures={};meshes={};parts=[]
    def texture(ptr):
        key=(ptr.m_FileID,ptr.m_PathID)
        if key not in textures:
            im=objects[ptr.m_PathID].read().image if not ptr.m_FileID else Image.open(texture_paths[ptr.m_PathID])
            im=im.convert('RGBA');im.thumbnail((512,512),Image.Resampling.LANCZOS)
            textures[key]=np.asarray(im.convert('RGBA'))
        return textures[key]
    def visit(pointer,parent,path):
        go=pointer.read();components=[c.component for c in go.m_Component]
        tr=next(c.read() for c in components if c.type.name=='Transform')
        matrix=parent@transform(tr);path=path+'/'+go.m_Name
        mesh=next((c.read() for c in components if c.type.name=='MeshFilter'),None)
        renderer=next((c.read() for c in components if c.type.name=='MeshRenderer'),None)
        if mesh and renderer:
            ptr=mesh.m_Mesh;key=(ptr.m_FileID,ptr.m_PathID)
            if key not in meshes:
                if ptr.m_FileID:
                    assert ptr.m_PathID==10210,(path,key) # Unity's built-in quad.
                    vertices=np.array([[-.5,-.5,0],[.5,-.5,0],[.5,.5,0],[-.5,.5,0]],dtype=np.float32)
                    uv=np.array([[0,0],[1,0],[1,1],[0,1]],dtype=np.float32)
                    triangles=np.array([[0,1,2],[0,2,3]])
                else:
                    helper=MeshHandler(ptr.read());helper.process()
                    vertices=np.asarray(helper.m_Vertices,dtype=np.float32)[:,:3]
                    uv=np.asarray(helper.m_UV0,dtype=np.float32)[:,:2]
                    triangles=np.asarray(helper.get_triangles()[0])
                meshes[key]=(vertices,uv,triangles)
            vertices,uv,triangles=meshes[key]
            material=renderer.m_Materials[0].read()
            props=material.m_SavedProperties
            tex=dict(props.m_TexEnvs)['_BaseMap'];image=texture(tex.m_Texture)
            color=dict(props.m_Colors)['_BaseColor']
            image=np.rint(image.astype(np.float32)*[color.r,color.g,color.b,color.a]).clip(0,255).astype(np.uint8)
            mapped=uv*[tex.m_Scale.x,tex.m_Scale.y]+[tex.m_Offset.x,tex.m_Offset.y]
            world=np.c_[vertices,np.ones(len(vertices))]@matrix.T
            parts.append((world[:,:3],mapped,triangles,image,path))
        if go.m_Name!='FireVfx':
            for child in tr.m_Children:visit(child.read().m_GameObject,matrix,path)
    root=next(p for name,p in env.container.items() if name.endswith('/'+stage+'.prefab'))
    visit(root,np.eye(4),'')
    print('Court geometry:',len(parts),'meshes,',sum(len(p[2]) for p in parts),'triangles',flush=True)
    return parts


def render(parts,width,height,distance,camera_height,yaw=0,roll=0,panorama=False,fov=30):
    focal=160*(height/192)/(2*math.tan(math.radians(fov/2)))
    yy,xx=np.mgrid[:height,:width].astype(np.float32)
    sx=(xx-width/2+.5)/focal;sy=(height/2-yy-.5)/focal
    angle=math.radians(yaw)
    if panorama:
        theta=sx+angle
        origin=np.stack((distance*np.sin(theta),np.full_like(xx,camera_height),distance*np.cos(theta)),axis=-1).reshape(-1,3)
        rays=np.stack((np.sin(theta),sy,np.cos(theta)),axis=-1).reshape(-1,3)
    else:
        r=math.radians(roll);rx=sx*np.cos(r)+sy*np.sin(r);ry=-sx*np.sin(r)+sy*np.cos(r)
        rays=np.stack((rx*np.cos(angle)+np.sin(angle),ry,-rx*np.sin(angle)+np.cos(angle)),axis=-1).reshape(-1,3)
        origin=np.broadcast_to([distance*math.sin(angle),camera_height,distance*math.cos(angle)],rays.shape).astype(np.float32)
    zbuffer=np.full(len(rays),np.inf,dtype=np.float32);stand_depth=zbuffer.copy()
    output=np.zeros((len(rays),4),dtype=np.uint8);output[:,3]=255
    for vertices,uv,triangles,tex,path in parts:
        for tri in triangles:
            a,b,c=vertices[tri];ab=b-a;ac=c-a
            h=np.cross(rays,ac);det=h@ab
            valid=np.abs(det)>1e-7
            inv=np.zeros_like(det);inv[valid]=1/det[valid]
            s=origin-a;u=np.einsum('ij,ij->i',s,h)*inv
            q=np.cross(s,ab);v=np.einsum('ij,ij->i',rays,q)*inv
            depth=(q@ac)*inv
            valid&=(u>=-1e-5)&(v>=-1e-5)&(u+v<=1.00001)&(depth>.3)&(depth<zbuffer)
            selected=np.flatnonzero(valid)
            if not len(selected):continue
            coords=uv[tri[0]]+u[selected,None]*(uv[tri[1]]-uv[tri[0]])+v[selected,None]*(uv[tri[2]]-uv[tri[0]])
            tx=(coords[:,0]%1)*(tex.shape[1]-1);ty=(1-coords[:,1]%1)*(tex.shape[0]-1)
            x=tx.astype(int);y=ty.astype(int);fx=(tx-x)[:,None];fy=(ty-y)[:,None]
            sampled=tex[y,x]*(1-fx)*(1-fy)+tex[y,np.minimum(x+1,tex.shape[1]-1)]*fx*(1-fy)+tex[np.minimum(y+1,tex.shape[0]-1),x]*(1-fx)*fy+tex[np.minimum(y+1,tex.shape[0]-1),np.minimum(x+1,tex.shape[1]-1)]*fx*fy
            opaque=sampled[:,3]>=128;selected=selected[opaque]
            output[selected,:3]=sampled[opaque,:3].astype(np.uint8);zbuffer[selected]=depth[selected]
            stand_depth[selected]=depth[selected] if '/Stands/' in path else np.inf
    return Image.fromarray(output.reshape(height,width,4)),stand_depth.reshape(height,width)


def render_key(cache,settings=VIEW_SETTINGS,stage="Court"):
    renderer=''.join(inspect.getsource(f) for f in (transform,court_geometry,render))
    return digest(dict(step='trial-render-v2',bundle=cache.file_key(BUNDLE),renderer=digest(renderer.encode()),views=settings,stage=stage))


def backgrounds(cache,settings=VIEW_SETTINGS,output_dir=OUT,stage="Court"):
    output_dir.mkdir(parents=True,exist_ok=True)
    key=render_key(cache,settings,stage)
    targets=[output_dir/f'view-{i}.png' for i in range(len(settings))]
    stamp=output_dir/'render-key.txt'
    depths=[output_dir/f'stands-{i}.npy' for i in range(len(settings))]
    if stamp.exists() and stamp.read_text()==key and all(p.exists() for p in targets+depths):
        return [Image.open(p).convert('RGBA') for p in targets],[np.load(p) for p in depths]
    parts=court_geometry(stage);views=[]
    masks=[]
    for i,(width,distance,height,yaw,roll,pano,fov) in enumerate(settings):
        print('render Court view',i,flush=True)
        im,depth=render(parts,width*2,384,distance,height,yaw,roll,pano,fov)
        im=im.resize((width,192),Image.Resampling.LANCZOS);im.save(targets[i]);views.append(im)
        depth=depth[1::2,1::2];np.save(depths[i],depth);masks.append(depth)
    stamp.write_text(key);return views,masks


def encode_backgrounds(images,output_dir=OUT,write_expected=True,existing_palette=None):
    output_dir.mkdir(parents=True,exist_ok=True)
    tiles=np.concatenate([np.asarray(im.convert('RGB')).reshape(24,8,im.width//8,8,3).transpose(2,0,1,3,4).reshape(-1,64,3) for im in images]).astype(np.int16)>>3
    if existing_palette is None:
        features=np.c_[tiles.mean(axis=1),tiles.std(axis=1)]
        centers=features[np.linspace(0,len(features)-1,15,dtype=int)].copy()
        for _ in range(12):
            assigned=((features[:,None,:]-centers[None,:,:])**2).sum(axis=2).argmin(axis=1)
            for k in range(15):
                if np.any(assigned==k):centers[k]=features[assigned==k].mean(axis=0)
        palettes=[]
        for k in range(15):
            sample=tiles[assigned==k].reshape(-1,3)
            if not len(sample):sample=tiles.reshape(-1,3)[::100]
            im=Image.fromarray((sample*8).astype(np.uint8).reshape(1,-1,3))
            trained=im.quantize(colors=15,method=Image.Quantize.MEDIANCUT).getpalette()[:45]
            trained+=trained[-3:]*((45-len(trained))//3)
            palette=np.array(trained,dtype=np.int16).reshape(15,3)//8
            palettes.append(palette)
    else:
        colors=np.frombuffer(existing_palette,dtype="<u2").reshape(15,16)[:,1:]
        palettes=np.stack((colors&31,(colors>>5)&31,(colors>>10)&31),axis=-1)
    palettes=np.asarray(palettes);rgb=np.array([(v&31,(v>>5)&31,(v>>10)&31) for v in range(32768)],dtype=np.float32)*8
    reference=background_features(rgb);dist=[];ink=[]
    for bank in palettes:
        d=((reference[:,None,:]-background_features(bank*8)[None,:,:])**2).sum(axis=2)
        dist.append(d.min(axis=1));ink.append(d.argmin(axis=1)+1)
    dist=np.asarray(dist);ink=np.asarray(ink,dtype=np.uint8)
    ids=tiles[:,:,0]|(tiles[:,:,1]<<5)|(tiles[:,:,2]<<10)
    banks=dist[:,ids].sum(axis=2).argmin(axis=0)
    indexed=ink[banks[:,None],ids]
    packed=indexed[:,0::2]|(indexed[:,1::2]<<4)
    palette_rgb555=np.c_[np.zeros((15,1),dtype=int),palettes[:,:,0]|(palettes[:,:,1]<<5)|(palettes[:,:,2]<<10)].astype('<u2')
    result=[];at=0;expected=bytearray()
    for i,im in enumerate(images):
        count=im.width//8*24;data=packed[at:at+count];bank=banks[at:at+count].astype(np.uint8)
        decoded=palettes[bank[:,None],indexed[at:at+count]-1]*8
        reconstructed=decoded.reshape(im.width//8,24,8,8,3).transpose(1,2,0,3,4).reshape(192,im.width,3)
        Image.fromarray(reconstructed.astype(np.uint8)).save(output_dir/f'view-{i}-gba.png')
        native=reconstructed.astype(np.uint16)//8
        expected.extend((native[:,:,0]|(native[:,:,1]<<5)|(native[:,:,2]<<10)).astype('<u2').tobytes())
        result.append((data.tobytes(),bank.tobytes(),im.width//8));at+=count
    if write_expected:write_changed(output_dir/'expected.bin',bytes(expected))
    return palette_rgb555.tobytes(),result


def actors(cache):
    chapter=json.loads((cache.root/'chapters/Act01_Chapter01/Act01_Chapter01_Adv04.json').read_text())
    encoded=json.loads((cache.root/'encoded.json').read_text());result=[]
    for who in ('Ema','Hiro','Leia'):
        request=next(r for r in chapter['characters'] if r['who']==who)
        asset=encoded[chapter['character_assets'][str(request['line'])]]
        result.append((who,asset))
    return result


def cropped_pieces(asset,placement,view,cache):
    result=[];scale=placement['scale'];scroll=392 if view==3 else 8
    for index,part in enumerate(asset['parts']):
        pw,ph=part['width']//8,part['height']//8;remaining=set()
        for ty in range(ph):
            for tx in range(pw):
                x,y=part['x']+tx*8,part['y']+ty*8
                left=placement['x']+(x-asset['pivot'][0])*scale/256-scroll
                top=placement['y']+(y-asset['pivot'][1])*scale/256
                if top>=160 or top+8*scale/256<=0:continue
                if view!=3 and (left>=240 or left+8*scale/256<=0):continue
                if not np.any(placement['bottom'][x:x+8]>y):continue
                if any(cache.path(part['tiles'][ty*pw+tx],'.bin').read_bytes()):remaining.add((tx,ty))
        while remaining:
            x,y=min(remaining,key=lambda p:(p[1],p[0]));candidates=[]
            for w,h in SHAPES:
                if x+w>pw or y+h>ph:continue
                covered={(tx,ty) for ty in range(y,y+h) for tx in range(x,x+w)}&remaining
                if len(covered)*4>=w*h*3:candidates.append((len(covered),-w*h,w,h,covered))
            _,_,w,h,covered=max(candidates,key=lambda p:(p[0],p[1]));remaining-=covered
            result.append(dict(part=index,x=x,y=y,width=w,height=h))
    return result


def placements(actors,depths,cache,settings=VIEW_SETTINGS,scales=(160,224,256,160),clip_lower_body=False,seats=(0,-1,1)):
    profiles=[]
    for view,(width,distance,height,yaw,roll,pano,fov) in enumerate(settings):
        focal=160/(2*math.tan(math.radians(fov/2)));row=[]
        for seat,(_,asset) in zip(seats,actors):
            angle=seat*math.tau/13-math.radians(yaw)
            depth=16-distance if pano else 16*math.cos(angle)-distance
            projected=focal*angle if pano else focal*16*math.sin(angle)/depth
            x=round(width/2+projected);y=8;scale=scales[view]
            limit=asset['height']
            if clip_lower_body and view<3:
                desktop=np.flatnonzero(depths[view][:,min(width-1,max(0,x))]<depth)
                assert len(desktop),(view,x,depth)
                screen_limit=int(desktop[0])-16+8
                limit=min(limit,math.floor((screen_limit-y)*256/scale+asset['pivot'][1]))
            bottom=np.full(asset['width'],limit,dtype='<i2')
            for u in range(asset['width']):
                sx=round(x+(u-asset['pivot'][0])*scale/256)
                if not 0<=sx<width:continue
                for v in range(asset['height']):
                    sy=round(y+(v-asset['pivot'][1])*scale/256)+16
                    if 0<=sy<192 and depths[view][sy,sx]<depth:
                        bottom[u]=min(bottom[u],v);break
            p=dict(x=x,y=y,scale=scale,bottom=bottom)
            p['pieces']=cropped_pieces(asset,p,3 if pano else view,cache);row.append(p)
        profiles.append(row)
    return profiles

