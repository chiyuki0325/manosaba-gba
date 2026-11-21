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
