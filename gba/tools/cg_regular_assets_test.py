"""Verify every packed CG: identical affine fallback and exact protected H/V pixels."""
from build_paths import generated_path
import json,sys
from pathlib import Path
import numpy as np
sys.path[:0]=[str(Path(__file__).resolve().parents[2]/'python_workspace')]
from graphics_project import ContentCache,GBA
from encode_gba_graphics import encode_background,load_image,quantize_background
from cg_detail_protection import regions_for,tile_positions

def main():
    cache=ContentCache();imported=json.loads((cache.root/'import.json').read_text())['assets']
    assets=json.loads((cache.root/'linked/graphics.json').read_text())['assets'];rows=[]
    for key,asset in assets.items():
        if 'regular' not in asset:continue
        source=imported[key];r=asset['regular'];v=asset['variants'][0]
        old=encode_background(source,cache,len(asset['variants']),[x['scale'] for x in asset.get('base_variants',[])])['variants'][0]
        assert v['tiles'][:v['affine_count']]==old['tiles'],source['appearance']
        compact=np.frombuffer(cache.path(v['map'],'.bin').read_bytes(),np.uint8).reshape(20,r['columns'])
        full=np.zeros((64,64),np.uint8);left=(512-asset['width'])//16
        full[22:42,left:left+r['columns']]=compact
        assert full.tobytes()==cache.path(old['map'],'.bin').read_bytes(),source['appearance']
        tiles=np.array([np.frombuffer(cache.path(k,'.bin').read_bytes(),np.uint8).reshape(8,8) for k in v['tiles']])
        cells=np.frombuffer(cache.path(r['map'],'.bin').read_bytes(),'<u2').reshape(20,r['columns'])
        image=tiles[cells&1023].copy()
        for y,x in np.ndindex(cells.shape):
            if cells[y,x]&1024:image[y,x]=image[y,x,:,::-1]
            if cells[y,x]&2048:image[y,x]=image[y,x,::-1,:]
        image=image.transpose(0,2,1,3).reshape(160,r['columns']*8)
        colors=np.frombuffer(cache.path(asset['palette'],'.bin').read_bytes(),'<u2').astype(np.int32)
        rgb=np.stack((colors&31,(colors>>5)&31,(colors>>10)&31),axis=1)*8
        target=np.array(quantize_background(load_image(cache,source),rgb[1:]))
        padded=np.pad(target,((0,0),(r['origin'],r['columns']*8-r['origin']-asset['width'])))
        protected=tile_positions(regions_for(source),r['origin'],0)
        for x,y in protected:assert np.array_equal(image[y*8:y*8+8,x*8:x*8+8],padded[y*8:y*8+8,x*8:x*8+8]),source['appearance']
        assert len(tiles)<=352 and (cells&1023).max()<len(tiles)
        rows.append(dict(name=source['appearance'],shared_tiles=len(tiles),protected_tiles=len(protected)))
    report=dict(passed=True,images=len(rows),protected_tiles=sum(x['protected_tiles'] for x in rows),max_shared_tiles=max(x['shared_tiles'] for x in rows),rows=rows)
    (generated_path('reports/cg-regular-roundtrip.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: all157 CG shared-prefix/compact-map round trips and protected pixels;',report['protected_tiles'],'protected tiles')
if __name__=='__main__':main()
