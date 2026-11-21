"""Import resident hardware tilemaps and prepainted area highlights."""
from build_paths import generated_path
import json
import numpy as np
from PIL import Image, ImageDraw
from graphics_project import GBA, ROOT, ContentCache, digest, write_changed
from witch_book_map_art import PALETTE, PLANE, SOURCE, floor_data, floor_image, geometry
from witch_book_catalog import BookCatalog
from image_encoding import rgb555
from story import AREAS, compress

NAMES = dict(AREAS, UnknownArea='？？？', Kitchen='厨房', Parlor='会客室', StorageRoom='储藏室',
             Restroom='卫生间', Sunroom='日光室', SecondFloorHallway='二楼走廊', Incinerator='焚烧炉')


def indices(image):
    rgb = np.asarray(image.convert('RGB'))
    result = np.zeros(rgb.shape[:2], np.uint8)
    for i, color in enumerate(PALETTE):
        result[np.all(rgb == color, axis=2)] = i
    return result


def main():
    cache = ContentCache()
    master = json.loads((GBA/'assets/source-metadata/trial.json').read_text())['master']
    catalog = BookCatalog(master, lambda _: 0)
    blob, lines, pictures = bytearray(), [], []
    def add(data):
        blob.extend(bytes(-len(blob)%4)); offset = len(blob); blob.extend(data)
        return 'book_map_bytes+'+str(offset)
    def quote(value):
        return json.dumps(value, ensure_ascii=False)
    def encode(raw):
        packed=compress(raw)
        return dict(key=cache.put_blob(packed if len(packed)<len(raw) else raw),codec=int(len(packed)<len(raw)))
    def block(part):
        return '{'+add(cache.path(part['key'], '.bin').read_bytes())+','+str(part['codec'])+'}'
    def hw_cell(tx,ty):
        return (tx//32)*1024+ty*32+tx%32
    planes, tile_counts, patches = {}, [], {}
    for ident in ('F1', 'F2', 'B1', 'Blueprint'):
        path = ROOT/f'manosaba-web/public/UI_WitchBook_2/Layout_{ident}_1.png'
        key = digest(dict(step='book-map-hardware-v1', image=cache.file_key(path), palette=PALETTE,
                          geometry=SOURCE['floors'], plane=PLANE))
        result=cache.get_json(key)
        if result is None:
            image,_=floor_image(floor_data(ident,1));pixels=indices(image)
            tiles=[bytes([0x88]*32)];lookup={tiles[0]:0};cells=np.zeros(2048,dtype='<u2')
            for ty in range(28):
                for tx in range(48):
                    tile=pixels[ty*8:ty*8+8,tx*8:tx*8+8]
                    raw=(tile[:,0::2]|tile[:,1::2]<<4).tobytes()
                    if raw not in lookup:lookup[raw]=len(tiles);tiles.append(raw)
                    cells[hw_cell(tx,ty)]=lookup[raw]
            data=b''.join(tiles)
            result=dict(parts=[encode(data[i:i+4096]) for i in range(0,len(data),4096)],
                        cells=encode(cells.tobytes()),tile_count=len(tiles),pixels=cache.put_blob(pixels.tobytes()),
                        mini=cache.put_blob(indices(image.resize((48,28),Image.Resampling.NEAREST)).tobytes()))
            cache.put_json(key,result)
        planes[ident]=np.frombuffer(cache.path(result['pixels'],'.bin').read_bytes(),np.uint8).reshape(224,384)
        parts=[block(part) for part in result['parts']];name='map_blocks_'+ident
        lines.append('const ui_picture_block '+name+'[]={'+','.join(parts)+'};')
        pictures.append('{'+name+','+block(result['cells'])+','+add(cache.path(result['mini'],'.bin').read_bytes())+','+str(result['tile_count'])+'}')
        tile_counts.append(result['tile_count'])
    areas,floors=[],[]
    for ident,version in [('F1',1),('F2',1),('F2',2),('B1',1),('Blueprint',1)]:
        floor=floor_data(ident,version);_,_,regions=geometry(floor);first=len(areas)
        for region_id,region in regions.items():
            key=digest(dict(step='book-map-highlight-v1',image=digest(planes[ident].tobytes()),polygons=region['polygons']))
            result=cache.get_json(key)
            if result is None:
                mask=Image.new('L',PLANE)
                for polygon in region['polygons']:ImageDraw.Draw(mask).polygon(polygon,fill=1,outline=2)
                mask=np.asarray(mask);paint=planes[ident].copy()
                bright=np.array([c[0]>=112 for c in PALETTE])
                paint[(mask==1)&bright[paint]]=14;paint[mask==2]=4
                patch_cells=[];data=bytearray()
                for ty in range(28):
                    for tx in range(48):
                        if not np.any(mask[ty*8:ty*8+8,tx*8:tx*8+8]):continue
                        patch_cells.append(hw_cell(tx,ty));tile=paint[ty*8:ty*8+8,tx*8:tx*8+8]
                        data.extend((tile[:,0::2]|tile[:,1::2]<<4).tobytes())
                assert len(patch_cells)<=128
                result=dict(cells=patch_cells,tiles=encode(bytes(data)) if data else None)
                cache.put_json(key,result)
            if key not in patches:
                name='map_patch_'+str(len(patches));patches[key]=name
                lines.append('const uint16_t '+name+'[]={'+','.join(map(str,result['cells'] or [0]))+'};')
                high=block(result['tiles']) if result['tiles'] else '{nullptr,0}'
                lines.append('const map_highlight '+name+'_data={'+high+','+name+','+str(len(result['cells']))+'};')
            points=[p for poly in region['polygons'] for p in poly];cx,cy=map(round,region['center'])
            bounds=[round(f(p[i] for p in points)) for f,i in [(min,0),(min,1),(max,0),(max,1)]] if points else [cx-6,cy-6,cx+6,cy+6]
            areas.append('{'+','.join([quote(region_id),quote(NAMES[region_id]),'&'+patches[key]+'_data',str(cx),str(cy),*map(str,bounds),
                                       str(list(AREAS).index(region_id)+1 if region_id in AREAS else 0)])+'}')
        floors.append('{'+','.join(map(str,[catalog.lookup['Map',ident],version,('F1','F2','B1','Blueprint').index(ident),first,len(areas)-first]))+'}')
    palette = ','.join(str(rgb555(c)) for c in PALETTE)
    header = '''// Generated by witch_book_maps.py.
#ifndef BOOK_MAP_DATA_H
#define BOOK_MAP_DATA_H
#include "ui_picture.h"
namespace manosaba {
struct map_highlight {ui_picture_block tiles;const uint16_t* cells;uint16_t count;};
struct map_area {const char* id;const char* name;const map_highlight* highlight;int16_t x,y,left,top,right,bottom;uint8_t normal_id;};
struct map_floor {uint8_t entry,version,image,first,count;};
struct map_picture {const ui_picture_block* blocks;ui_picture_block cells;const uint8_t* mini;uint16_t tile_count;};
constexpr int map_highlight_tiles=128;
constexpr int map_tile_capacity=MAP_CAPACITY;
extern const uint8_t book_map_bytes[];
extern const uint16_t book_map_palette[16];
extern const map_area map_areas[];
extern const map_floor map_floors[5];
extern const map_picture map_pictures[4];
}
#endif
'''.replace('MAP_CAPACITY',str(max(tile_counts)+256))
    cpp = ['// Generated by witch_book_maps.py.', '#include "book_map_data.h"', 'namespace manosaba {', *lines,
           'const uint16_t book_map_palette[16]={'+palette+'};',
           'const map_picture map_pictures[4]={'+','.join(pictures)+'};',
           'const map_area map_areas[]={'+','.join(areas)+'};',
           'const map_floor map_floors[5]={'+','.join(floors)+'};', '}']
    write_changed(GBA/'src/book_map_data.h', header)
    write_changed(GBA/'src/book_map_data.cpp', '\n'.join(cpp)+'\n')
    write_changed(GBA/'assets/trial/book-maps.bin', bytes(blob))
    write_changed(GBA/'src/book_map_bytes.s', '// '+digest(bytes(blob))+'\n.section .rodata.book_map_bytes,"a",%progbits\n.balign 4\n.global _ZN8manosaba14book_map_bytesE\n_ZN8manosaba14book_map_bytesE:\n.incbin "../assets/trial/book-maps.bin"\n')
    strings = {(area['name'], NAMES[area['name']]) for f in SOURCE['floors'] for area in f['tree']['children']
               if area['name'] in NAMES}
    string_bytes = sum((len(value.encode())+4)//4*4 for pair in strings for value in pair)
    total=len(blob)+len(areas)*28+5*5+4*20+32+string_bytes
    patch_cells=sum(line.count(',')+1 for line in lines if line.startswith('const uint16_t map_patch_'))
    total+=patch_cells*2+len(patches)*16
    total+=sum((count*32+4095)//4096*8 for count in tile_counts)
    assert total<65536,total
    report=dict(bitmap_bytes=len(blob),areas=len(areas),estimated_total_bytes=total,plane=list(PLANE),bpp=4,
                tile_counts=tile_counts,tile_capacity=max(tile_counts)+256,highlight_tiles=128,
                patch_cells=patch_cells,cached_images=cache.hits,cache_misses=cache.misses)
    write_changed(generated_path('reports/book-maps.json'), json.dumps(report, indent=2)+'\n')
    print(report)


if __name__ == '__main__': main()
