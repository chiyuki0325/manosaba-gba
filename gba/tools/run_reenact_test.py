"""Parallel real-script reenactment playback, sparse OBJ and native pixel tests."""
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess
import numpy as np
from PIL import Image
from host_test_cache import GBA,build


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--jobs',type=int,default=10);parser.add_argument('--match',default='')
    args=parser.parse_args();rom=args.rom.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    data=rom.read_bytes();rows=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True).splitlines()
    symbols={r.split()[-1]:int(r.split()[0],16) for r in rows if len(r.split())>=3}
    profile=symbols['trial_runtime'];assets=symbols['_ZN8manosaba14reenact_assetsE'];palette=symbols['_ZN8manosaba15reenact_paletteE']
    def u16(p):return struct.unpack_from('<H',data,p-0x08000000)[0]
    def u32(p):return struct.unpack_from('<I',data,p-0x08000000)[0]
    dictionary=u32(symbols['_ZN8manosaba13reenact_tilesE'])
    def tile(index):
        raw=np.frombuffer(data,dtype=np.uint8,count=32,offset=dictionary-0x08000000+index*32).reshape(8,4)
        pixels=np.empty((8,8),dtype=np.uint8);pixels[:,::2]=raw&15;pixels[:,1::2]=raw>>4;return pixels
    colors=np.array([u16(palette+i*2) for i in range(16)],dtype=np.uint16)
    # mGBA expands RGB555 before blending into its RGB888 screenshot buffer.
    channel=np.arange(32,dtype=np.uint16);shade_channel=(80+((channel<<3)|(channel>>2))*6)>>7
    shaded=sum(shade_channel[(colors>>shift)&31]<<shift for shift in (0,5,10))
    yy,xx=np.indices((160,240));bayer=np.array(((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5)))
    shade_mask=(yy>=104)&((yy>=120)|(bayer[yy%4,xx%4]<yy-103))
    def expected(scene,pose,clock):
        asset=assets+scene*16;poses=u32(asset);p=poses+pose*12;parts=u32(p);count=u16(p+4);bg=u32(p+8)
        pixels=np.zeros((160,240),dtype=np.uint8)
        for n in range(600):pixels[n//30*8:n//30*8+8,n%30*8:n%30*8+8]=tile(u16(bg+n*2))
        duration=u16(asset+8);loop=data[asset-0x08000000+15]
        phase=clock%duration if loop and duration else clock
        moving=data[asset-0x08000000+14];mi=min(phase,u16(asset+12)-1);moves=u32(asset+4)
        dx,dy=struct.unpack_from('<bb',data,moves-0x08000000+mi*2)
        for n in range(count):
            part=parts+n*12;ids=u32(part);x,y,w,h,group=struct.unpack_from('<5B',data,part-0x08000000+4)
            if group==moving:x+=dx;y+=dy
            for ty in range(h//8):
                for tx in range(w//8):
                    im=tile(u16(ids+(ty*(w//8)+tx)*2));px=x+tx*8;py=y+ty*8
                    left=max(0,px);right=min(240,px+8);top=max(0,py);bottom=min(160,py+8)
                    if right<=left or bottom<=top:continue
                    source=im[top-py:bottom-py,left-px:right-px];target=pixels[top:bottom,left:right];mask=source!=0;target[mask]=source[mask]
        return np.where(shade_mask,shaded[pixels],colors[pixels])
    scripts=(GBA/'src/story_data.cpp').read_text().split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(row[1:].split(',',1)[0]) for row in scripts.splitlines() if row.startswith('{')]
    trial_first=next(i for i,p in enumerate(paths) if '_Trial' in p and not p.startswith('Common/'))
    # Use the actual generated selector boundary, not a guessed path order.
    boundary=re.search(r'extern const int story_trial_first = (\d+);',(GBA/'src/story_data.cpp').read_text())
    if boundary:trial_first=int(boundary[1])
    names=json.loads((GBA/'assets/reenact/catalog.json').read_text())['names']
    routes={}
    for source in sorted((GBA.parent/'story/zh-Hans').glob('*/*Trial*.json')):
        doc=json.loads(source.read_text())
        if doc['path'] not in paths:continue
        for event in doc['events']:
            if event['op']=='SpawnExtended':
                name=event.get('params',{}).get('Path','')
                if name in names:routes.setdefault(doc['path'],set()).add(names.index(name))
    driver=build('reenact_game_test',[GBA/'tools/reenact_game_test.c'])
    def run(item):
        path,ids=item;directory=out/path.split('/')[-1];directory.mkdir(exist_ok=True)
        command=[str(driver),str(rom),str(directory),f'{profile:x}',f'{assets:x}',f'{palette:x}',str(paths.index(path)-trial_first),str(paths.index(path)),str(min(ids)),str(max(ids)),str(int(path.endswith('Act01_Chapter01_Trial15'))),f'{symbols["save_runtime"]:x}']
        process=subprocess.run(command,capture_output=True,text=True,timeout=120);(directory/'native.log').write_text(process.stdout+process.stderr)
        reports=[json.loads(row) for row in process.stdout.splitlines() if row.startswith('{')]
        result=reports[-1] if reports else dict(passed=False,error=process.stderr[-1500:])
        result.update(path=path,returncode=process.returncode)
        matches=[]
        for row in (directory/'captures.txt').read_text().splitlines():
            scene,pose,clock,frame=map(int,row.split());picture=directory/f'scene-{scene:02d}-pose-{pose}.ppm'
            image=Image.open(picture).convert('RGB');rgb=np.asarray(image).astype(np.uint16)>>3;actual=rgb[:,:,0]|(rgb[:,:,1]<<5)|(rgb[:,:,2]<<10)
            errors=[int(np.count_nonzero(actual[:123]!=expected(scene,pose,max(0,clock+offset))[:123])) for offset in (-1,0,1)]
            matches.append(dict(scene=scene,pose=pose,clock=clock,mismatched_pixels=min(errors)))
            image.save(picture.with_suffix('.png'))
        result['captures']=matches;result['max_pixel_mismatch']=max((r['mismatched_pixels'] for r in matches),default=0)
        repeated=[]
        for row in (directory/'loop-captures.txt').read_text().splitlines():
            scene,pose,clock,sample=map(int,row.split());picture=directory/f'loop-{scene:02d}-sample-{sample}.ppm'
            rgb=np.asarray(Image.open(picture).convert('RGB')).astype(np.uint16)>>3;actual=rgb[:,:,0]|(rgb[:,:,1]<<5)|(rgb[:,:,2]<<10)
            errors=[int(np.count_nonzero(actual[:123]!=expected(scene,pose,max(0,clock+offset))[:123])) for offset in (-1,0,1)]
            repeated.append(dict(scene=scene,pose=pose,clock=clock,sample=sample,mismatched_pixels=min(errors)))
        result['loop_captures']=repeated
        result['max_pixel_mismatch']=max(result['max_pixel_mismatch'],max((r['mismatched_pixels'] for r in repeated),default=0))
        for picture in directory.glob('*.ppm'):Image.open(picture).save(picture.with_suffix('.png'))
        result['expected_poses']=sum(u16(assets+i*16+10) for i in ids)
        result['passed']=process.returncode==0 and len(matches)==result['expected_poses'] and result['max_pixel_mismatch']==0 and result.get('missed')==0 and result.get('peak_ticks',4389)<4389
        (directory/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');return result
    results=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures=[pool.submit(run,item) for item in routes.items() if not args.match or args.match in item[0]]
        for future in as_completed(futures):
            result=future.result();results.append(result);print(result['path'], 'PASS' if result['passed'] else 'FAIL', 'poses',len(result['captures']),'pixel mismatch',result['max_pixel_mismatch'],'peak',result.get('peak_ticks'),flush=True)
    report=dict(passed=all(r['passed'] for r in results),rom_sha256=hashlib.sha256(data).hexdigest(),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
