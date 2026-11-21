"""Build the H/V CG supplement while retaining the original affine dictionary prefix."""
import copy
import numpy as np
from graphics_project import digest
from cg_detail_protection import regions_for


def encode(asset, encoded, cache):
    key = digest(dict(step='cg-regular-shared-v1', original=encoded,
                      protection=regions_for(asset)))
    old = cache.get_json(key)
    if old:
        return old
    # The quality tool is also the offline encoder: one algorithm and one cache.
    from cg_regular_economy import task
    out = cache.root/'cg-regular'
    out.mkdir(exist_ok=True)
    result = task((asset, encoded, out, True))
    source = out/result['adaptive']['cache']
    variant = encoded['variants'][0]
    combined = list(variant['tiles'])
    lookup = {key: i for i, key in enumerate(combined)}
    with np.load(source) as candidate:
        remap = []
        for tile in candidate['tiles']:
            ref = cache.put_blob(tile.tobytes())
            if ref not in lookup:
                lookup[ref] = len(combined)
                combined.append(ref)
            remap.append(lookup[ref])
        width, height = encoded['width'], encoded['height']
        origin = ((512-width)//2) % 8
        columns = (width+origin+7)//8
        cells = candidate['map'][:height//8, :columns]
        cells = np.asarray(remap, np.uint16)[cells & 1023] | (cells & 3072)
    # The affine view keeps identical numbering and pixels, with just its empty border omitted.
    original_map = np.frombuffer(cache.path(variant['map'], '.bin').read_bytes(), np.uint8).reshape(64,64)
    left, top = (512-width)//16, (512-height)//16
    compact = original_map[top:top+height//8,left:left+columns].copy()
    restored = np.zeros((64,64),np.uint8)
    restored[top:top+height//8,left:left+columns] = compact
    assert np.array_equal(original_map, restored)
    result_asset = copy.deepcopy(encoded)
    compact_ref = cache.put_blob(compact.tobytes())
    map_ref = cache.put_blob(cells.astype('<u2').tobytes())
    result_asset['variants'][0].update(tiles=combined, map=compact_ref, scale=0,
                                     affine_count=len(variant['tiles']))
    result_asset['regular'] = dict(map=map_ref, count=len(combined), columns=columns, origin=origin,
                                   gain_db=result['achieved_gain_db'])
    blobs = {result_asset['palette'], map_ref}
    for v in result_asset['variants']+result_asset.get('base_variants',[]):
        blobs.update(v['tiles']); blobs.add(v['map'])
    result_asset['blobs'] = sorted(blobs)
    result_asset['rom_directory_bytes'] += 16+4*(len(combined)-len(variant['tiles']))
    cache.put_json(key,result_asset)
    return result_asset
