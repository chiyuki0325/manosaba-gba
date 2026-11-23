#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
from PIL import Image
sys.path[:0]=[str(Path(__file__).resolve().parents[2]/'python_workspace')]
from graphics_project import ContentCache
from import_gba_graphics import png_object
from encode_gba_graphics import rectangles,vector_tiles,encode_mask,encode_actor,train_family,color_centers,background_palette,quantize_background

class EncodingTest(unittest.TestCase):
    def test_baked_portrait_sampling_is_independent_of_obj_partition(self):
        from actor_cache import bake
        raw=bytes([0x11])*32;payload=raw;offsets={'filled':0}
        def part(x,y,w,h):return dict(x=x,y=y,width=w,height=h,bank=0,tiles=['filled']*(w*h//64))
        whole=dict(width=64,height=64,pivot=[32,32],palette='palette',parts=[part(0,0,64,64)])
        split=dict(whole,parts=[part(x,y,32,32) for y in (0,32) for x in (0,32)])
        def pixels(result):
            asset,blobs,_=result
            image=np.zeros((max(p['y']+p['height'] for p in asset['parts']),max(p['x']+p['width'] for p in asset['parts'])),dtype=np.uint8)
            for p in asset['parts']:
                for i,ref in enumerate(p['tiles']):
                    tile=np.frombuffer(blobs[ref],dtype=np.uint8).reshape(8,4)
                    unpacked=np.empty((8,8),dtype=np.uint8);unpacked[:,::2]=tile&15;unpacked[:,1::2]=tile>>4
                    x=p['x']+i%(p['width']//8)*8;y=p['y']+i//(p['width']//8)*8
                    target=image[y:y+8,x:x+8];take=unpacked!=0;target[take]=unpacked[take]
            return image
        for scale,phase,matrix in ((230,0,(4034,700,279,49,-50,279)),(307,51,(4034,700,209,37,-38,209)),
                                   (614,0,(4045,-641,105,-17,17,105))):
            for viewport in (None,(-24,-32,24,32)):
                a=pixels(bake(whole,scale,2560,phase,0,matrix,payload,offsets,viewport))
                b=pixels(bake(split,scale,2560,phase,0,matrix,payload,offsets,viewport))
                np.testing.assert_array_equal(a,b)
                for row in b:
                    ink=np.flatnonzero(row)
                    if len(ink):self.assertTrue(np.all(row[ink[0]:ink[-1]+1]))

    def test_clipped_cached_entry_only_settles_its_initial_turn(self):
        from actor_cache import cached_entry_angles
        program=dict(resources=['actor'],keys=[[486,0,0,0],[10240,21,0,0]],
                     commands=[[0,0,1,3,0,0,0],[0,0,0,0,0,0,0],
                               [0,0,1,1,0,2,0],[0,1,1,1,0,3,0],
                               [0,1,1,1,0,3,0]],events=[[0,4,0],[4,1,0]])
        cached=[dict(base=7,scale=486,angle=10240,clipped=True)]
        self.assertEqual(cached_entry_angles(program,{'actor':7},cached),{3})
        self.assertFalse(cached_entry_angles(program,{'actor':7},[]))
        program['commands'][3][-1]=127
        self.assertFalse(cached_entry_angles(program,{'actor':7},cached))

    def test_large_portrait_turn_keeps_position_and_fade_but_limits_affine_entrance(self):
        from actor_cache import large_tilt_fallbacks
        keys=[[614,21,3,0],[-2304,21,3,0],[120,21,3,0],[256,21,3,0]]
        commands=[[0,0,0,0,0,0,0]]+[[0,i,1,1,0,p,0] for i,p in enumerate((2,3,0,4))]
        program=dict(resources=['actor'],keys=keys,commands=commands,events=[[0,5,0]])
        assets={'actor':dict(parts=[dict(width=32,height=32)])};ids={'actor':7}
        self.assertEqual(large_tilt_fallbacks(program,assets,ids,[]),{1:(512,21,3,0),2:(0,0,3,0)})
        keys[0][0]=486
        self.assertEqual(large_tilt_fallbacks(program,assets,ids,[]),{2:(0,0,3,0)})
        self.assertFalse(large_tilt_fallbacks(program,assets,ids,[dict(base=7,scale=486,angle=-2304,clipped=True)]))
        keys[0][0]=358
        self.assertFalse(large_tilt_fallbacks(program,assets,ids,[]))
        keys[0][0]=614;keys[0][3]=1
        self.assertFalse(large_tilt_fallbacks(program,assets,ids,[]))

    def test_tall_obj_split_retains_pixels_palette_and_sparse_coverage(self):
        from obj_parts import split_tall_parts
        original=[dict(x=24,y=32,width=64,height=64,bank=2,tiles=list(range(64))),
                  dict(x=96,y=80,width=8,height=16,bank=1,tiles=[64,65])]
        parts=split_tall_parts(original)
        def cells(rows):
            return {(p['x']//8+x,p['y']//8+y):(p['bank'],p['tiles'][y*(p['width']//8)+x])
                    for p in rows for y in range(p['height']//8) for x in range(p['width']//8)}
        self.assertEqual(cells(parts),cells(original))
        self.assertTrue(all(p['height']<=32 for p in parts))
        self.assertEqual(sum(len(p['tiles']) for p in parts),66)

    def test_fading_cg_has_lower_resources_before_its_hide_endpoint(self):
        from graphics_usage import resource_usage
        assets={key:dict(kind='background') for key in ('cg','room')}
        for blocking in (False,True):
            program=dict(resources=['cg','room'],keys=[[256,0,0,0],[0,21,0,0]],commands=[
                [0,0,0,0,3,0,0],[0,0,1,1,3,4,0],
                [0,1,1,1,3,4,128 if blocking else 0],
                [1,0,0,0,4,0,0],[0,0,1,1,4,4,0],[1,0,0,4,4,0,0]],
                events=[[0,2,1],[2,1,2],[3,3,3]])
            _,base=resource_usage([program],assets)
            if blocking:self.assertNotIn('cg',base)
            else:self.assertIn(256,base['cg'])

    def test_sparse_sprite_rectangle_coverage(self):
        occupied={(x,y) for y in range(12) for x in range(10) if abs(x-5)<2 or y<3}
        parts=rectangles(occupied)
        covered={(x,y) for left,top,w,h in parts for x in range(left,left+w) for y in range(top,top+h)}
        self.assertLess(len(parts),20)
        self.assertTrue(occupied<=covered)
        self.assertTrue(all(w*h<=64 for _,_,w,h in parts))

    def test_affine_dictionary_and_map_bounds(self):
        image=Image.fromarray(np.tile(np.arange(240,dtype=np.uint8)%2+1,(160,1)))
        colors=[(0,0,0),(255,255,255)]
        dictionary,cells,psnr=vector_tiles(image,colors)
        self.assertLessEqual(len(dictionary),256)
        self.assertEqual(cells.shape,(64,64))
        self.assertLess(cells.max(),len(dictionary))
        self.assertGreater(psnr,50)

    def test_protected_mouth_survives_a_tiny_dictionary(self):
        rng=np.random.default_rng(73)
        pixels=rng.integers(1,5,(16,40),dtype=np.uint8)
        pixels[8:16,16:24]=1
        pixels[11,19]=4
        colors=[(192,160,160),(32,80,112),(112,48,80),(32,16,32)]
        dictionary,cells,_=vector_tiles(Image.fromarray(pixels),colors,3,protected=[(19,11,1,1)])
        previous,_,_=vector_tiles(Image.fromarray(pixels),colors,3)
        self.assertGreaterEqual(len({t.tobytes() for t in previous}&{t.tobytes() for t in dictionary}),len(previous)-1)
        actual=dictionary[cells].reshape(64,64,8,8).transpose(0,2,1,3).reshape(512,512)
        left=(512-40)//2;top=(512-16)//2
        # Protection follows the 512px texture grid, including an unaligned image edge.
        x=(left+19)//8*8;y=(top+11)//8*8
        expected=np.zeros((512,512),np.uint8);expected[top:top+16,left:left+40]=pixels
        np.testing.assert_array_equal(actual[y:y+8,x:x+8],expected[y:y+8,x:x+8])
        self.assertLessEqual(len(dictionary),4)

    def test_protection_coordinates_keep_the_361px_grid_origin(self):
        from cg_detail_protection import tile_positions,transformed_regions
        self.assertEqual(tile_positions([(5,7,3,2)],3,0),[(1,0),(1,1)])
        self.assertEqual(transformed_regions([(144,48,8,8)],.5,.5,48,88,256,256),[(120,112,4,4)])

    def test_cached_mask_is_shared_between_backgrounds(self):
        with tempfile.TemporaryDirectory() as directory:
            cache=ContentCache(directory)
            image=Image.fromarray(np.tile(np.arange(240,dtype=np.uint8),(160,1)))
            asset=png_object(cache,image,dict(id='mask',kind='mask'))
            result=encode_mask(asset,cache,8)
            self.assertEqual(len(result['frames']),9)
            self.assertEqual(encode_mask(asset,cache,8),result)
            for frame in result['frames']:self.assertLessEqual(len(frame['tiles']),256)
            self.assertTrue(all(frame['map'] is None for frame in result['frames']))
            frame=result['frames'][4]
            columns=np.array([list(cache.path(ref,'.bin').read_bytes()) for ref in frame['tiles'][1:]],dtype=np.uint8)
            row=columns.reshape(30,8,8).transpose(1,0,2).reshape(8,240)
            self.assertTrue(np.all(row[:,:112]==224))
            self.assertTrue(np.all(row[:,144:]==0))
            self.assertEqual(row[:4,120].tolist(),[224,0,224,0])

    def test_actor_accents_survive_gray_majority(self):
        gray=np.full(6000,12|(12<<5)|(12<<10),dtype=np.uint16)
        pink=np.full(12,30|(6<<5)|(18<<10),dtype=np.uint16)
        colors=color_centers([gray,pink],15)
        self.assertIn((240,48,144),colors)

    def test_background_preserves_neutral_ramp(self):
        rgb=np.repeat(np.arange(0,128,8,dtype=np.uint8)[:,None,None],3,axis=2)
        image=Image.fromarray(np.tile(rgb,(1,16,1)))
        colors=background_palette(image,16)
        self.assertTrue(all(r==g==b for r,g,b in colors))
        indexed=np.array(quantize_background(image,colors))-1
        reconstructed=np.array(colors)[indexed]
        self.assertLessEqual(np.max(np.abs(reconstructed-np.array(image))),8)

    def test_only_exposed_third_background_gets_an_obj_proxy(self):
        from graphics_usage import scene_proxy_usage
        assets={name:dict(kind='background',size=[320,160]) for name in ('a','b','c')}
        commands=[];keys=[]
        for target,name in enumerate(assets,3):
            commands.extend([[target-3,0,0,0,target,0,0],[target*20,0,0,4,target,0,0]])
            keys.append([256,0,0,0]);commands.append([0,len(keys)-1,1,1,target,4,0])
        program=dict(resources=list(assets),commands=commands,keys=keys,events=[[0,len(commands),1]])
        self.assertEqual(scene_proxy_usage([program],assets),set())
        # Neither upper layer covers the screen; retaining the bottom image is necessary.
        for target in (4,5):
            keys.append([-200*256,0,0,0]);commands.append([0,len(keys)-1,1,1,target,0,0])
        program['events'][0][1]=len(commands)
        self.assertEqual(scene_proxy_usage([program],assets),{'a'})

    def test_actor_palette_and_warm_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            cache=ContentCache(directory)
            image=Image.new('RGBA',(24,40),(0,0,0,0))
            for y in range(8,32):
                for x in range(8,16):image.putpixel((x,y),(240,y*6,80,255))
            asset=png_object(cache,image,dict(id='pose',kind='character',who='Test',bbox=[8,8,16,32]))
            family=train_family([asset],cache);result=encode_actor(asset,family,cache)
            self.assertLessEqual(result['tile_bytes'],1024)
            self.assertEqual(encode_actor(asset,family,cache),result)
            self.assertEqual(len(cache.path(family['blob'],'.bin').read_bytes()),128)

if __name__=='__main__':unittest.main()
