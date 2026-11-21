"""Offline native-coordinate CG detail protection; never stored in the game ROM."""
from functools import lru_cache
from pathlib import Path
import json,math
GBA=Path(__file__).resolve().parents[1]
PATH=GBA/'assets/cg-detail-protection.json'

@lru_cache(maxsize=1)
def document():
 return json.loads(PATH.read_text())

def regions_for(asset):
 if asset.get('namespace')!='Stills':return []
 return [tuple(region['rect']) for region in document()['images'].get(asset['appearance'],{}).get('regions',[])]

def tile_positions(regions,origin_x=0,origin_y=0):
 return sorted({(tx,ty) for x,y,w,h in regions for ty in range((y+origin_y)//8,(y+origin_y+h+7)//8) for tx in range((x+origin_x)//8,(x+origin_x+w+7)//8)},key=lambda p:(p[1],p[0]))

def transformed_regions(regions,sx,sy,dx,dy,width,height):
 result=[]
 for x,y,w,h in regions:
  left=max(0,math.floor(x*sx+dx));top=max(0,math.floor(y*sy+dy));right=min(width,math.ceil((x+w)*sx+dx));bottom=min(height,math.ceil((y+h)*sy+dy))
  if left<right and top<bottom:result.append((left,top,right-left,bottom-top))
 return result

def main():
 data=document()
 assert data['coordinates']=='native-image-pixels'
 for name,entry in data['images'].items():
  for region in entry['regions']:
   x,y,w,h=region['rect']
   assert all(type(n)==int for n in (x,y,w,h)) and min(x,y)>=0 and min(w,h)>0,(name,region)
 print('Approved CG detail regions:',sum(len(e['regions']) for e in data['images'].values()))
if __name__=='__main__':main()
