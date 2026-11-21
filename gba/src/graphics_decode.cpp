#include "graphics_decode.h"
#include "bn_common.h"
#include <cstring>
namespace manosaba {
alignas(4) BN_DATA_EWRAM_BSS uint8_t graphic_map_cells[4096];
const graphic_regular* regular_graphic(int image)
{
    int first=0,last=graphic_regular_count;
    while(first<last){int mid=(first+last)/2;if(graphic_regular_images[mid].image<image)first=mid+1;else last=mid;}
    return first<graphic_regular_count && graphic_regular_images[first].image==image ? &graphic_regular_images[first] : nullptr;
}
uint8_t affine_cell(const graphic_asset& asset,const graphic_variant& variant,int x,int y)
{
    if(variant.scale)return variant.map[y*64+x];
    int left=(512-asset.width)/16,top=(512-asset.height)/16;
    int columns=(asset.width+((512-asset.width)/2)%8+7)/8;
    x-=left;y-=top;
    return unsigned(x)<unsigned(columns) && unsigned(y)<asset.height/8u ? variant.map[y*columns+x] : 0;
}
void expand_affine_map(const graphic_asset& asset,const graphic_variant& variant,uint8_t* output)
{
    if(variant.scale){std::memcpy(output,variant.map,4096);return;}
    std::memset(output,0,4096);
    int columns=(asset.width+((512-asset.width)/2)%8+7)/8;
    int left=(512-asset.width)/16,top=(512-asset.height)/16;
    for(int y=0;y<asset.height/8;++y)std::memcpy(output+(top+y)*64+left,variant.map+y*columns,unsigned(columns));
}
void regular_window(const graphic_regular& image,int x,int y,uint16_t* output)
{
    for(int row=0;row<32;++row)for(int col=0;col<32;++col)
    {
        int sx=x+col,sy=y+row;
        output[row*32+col]=unsigned(sx)<image.columns && unsigned(sy)<20 ? image.map[sy*image.columns+sx] : 0;
    }
}
}
