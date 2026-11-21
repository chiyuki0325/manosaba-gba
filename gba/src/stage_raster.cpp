#include "stage_raster.h"
#include <algorithm>
#include <cstring>
#ifdef __arm__
#define STAGE_ARM __attribute__((section(".iwram"),target("arm")))
#define STAGE_INLINE __attribute__((always_inline,target("arm"))) inline
#else
#define STAGE_ARM
#define STAGE_INLINE inline
#endif
namespace manosaba
{
namespace
{
const int16_t sine[] = {0,286,572,857,1143,1428,1713,1997,2280,2563,2845,3126,3406,3686,3964,4240,4516,4790,5063,5334,5604,5872,6138,6402,6664,6924,7182,7438,7692,7943,8192,8438,8682,8923,9162,9397,9630,9860,10087,10311,10531,10749,10963,11174,11381,11585,11786,11982,12176,12365,12551,12733,12911,13085,13255,13421,13583,13741,13894,14044,14189,14330,14466,14598,14726,14849,14968,15082,15191,15296,15396,15491,15582,15668,15749,15826,15897,15964,16026,16083,16135,16182,16225,16262,16294,16322,16344,16362,16374,16382,16384};
STAGE_INLINE int sin_angle(int angle)
{
    angle = (angle/256)%360;
    if(angle < 0) angle += 360;
    if(angle <= 90) return sine[angle];
    if(angle <= 180) return sine[180-angle];
    if(angle <= 270) return -sine[angle-180];
    return -sine[360-angle];
}
STAGE_INLINE int mask_alpha(int progress, int mask, int x, int y)
{
    if(progress <= 0) return 0;
    if(progress >= 256) return 16;
    if(mask < 0) return progress/16;
    return std::clamp((progress*288/256-stage_masks[mask][y*240+x])*16/32,0,16);
}
}
static STAGE_INLINE uint8_t blend_fast(uint8_t bottom, uint8_t top, int alpha)
{
    if(alpha >= 16) return top;
    if(alpha <= 0) return bottom;
    unsigned a = stage_palette[bottom], b = stage_palette[top];
    unsigned rb = (((a&0x7c1f)*(16-alpha)+(b&0x7c1f)*alpha)>>4)&0x7c1f;
    unsigned g = (((a&0x3e0)*(16-alpha)+(b&0x3e0)*alpha)>>4)&0x3e0;
    return stage_rgb_lookup[rb|g];
}
STAGE_ARM uint8_t stage_blend(uint8_t bottom, uint8_t top, int alpha)
{
    return blend_fast(bottom,top,alpha);
}
STAGE_ARM void stage_render(const stage_player& player, uint8_t* pixels, int first_y, int rows)
{
    std::memset(pixels+first_y*240,1,unsigned(rows*240));
    const auto& camera = player.entity(stage_camera);
    int zoom = std::max(16,camera.get(stage_property::scale));
    int cs = sin_angle(camera.get(stage_property::angle)+90*256);
    int sn = sin_angle(camera.get(stage_property::angle));
    int order[stage_entity_count-1], count = 0;
    for(int index = 0; index < stage_entity_count; ++index)
        if(index != stage_camera && player.entity(index).image >= 0 && player.entity(index).get(stage_property::opacity) > 0)
            order[count++] = index;
    for(int i = 1; i < count; ++i)
    {
        int current = order[i], j = i;
        while(j && player.entity(order[j-1]).order > player.entity(current).order)
        {
            order[j] = order[j-1]; --j;
        }
        order[j] = current;
    }
    for(int index = 0; index < count; ++index)
    {
        int target = order[index];
        const auto& entity = player.entity(target);
        const auto& texture = stage_textures[entity.image];
        int opacity = entity.get(stage_property::opacity);
        if(target >= stage_mask_first) opacity = opacity*entity.get(stage_property::fill)/256;
        if(opacity <= 0) continue;
        bool overlay = target == stage_overlay;
        if(overlay && entity.get(stage_property::scale) == 256 && entity.get(stage_property::angle) == 0)
        {
            // Overlay resources are solid colors. Dissolve only the scene pixels;
            // no texture transform or actor material work is needed here.
            int color = blend_fast(1,texture.pixels[0],std::clamp(entity.get(stage_property::tint)/16,0,16));
            int mask = entity.maskable ? entity.mask : -1;
            for(int py = first_y; py < first_y+rows; ++py)
                for(int px = 0; px < 240; ++px)
                {
                    int alpha = mask_alpha(opacity,mask,px,py);
                    auto& destination = pixels[py*240+px];
                    if(alpha) destination = alpha >= 16 ? uint8_t(color) : blend_fast(destination,uint8_t(color),alpha);
                }
            continue;
        }
        int scale = std::max(8,entity.get(stage_property::scale)*256/texture.nominal_scale);
        int angle = entity.get(stage_property::angle);
        int x = entity.get(stage_property::x), y = entity.get(stage_property::y);
        if(! overlay)
        {
            int dx = x-120*256-camera.get(stage_property::x), dy = y-80*256-camera.get(stage_property::y);
            x = 120*256 + int((int64_t(dx)*cs-int64_t(dy)*sn)*zoom/(16384*256));
            y = 80*256 + int((int64_t(dx)*sn+int64_t(dy)*cs)*zoom/(16384*256));
            scale = scale*zoom/256;
            angle += camera.get(stage_property::angle);
        }
        int c = sin_angle(angle+90*256), s = sin_angle(angle);
        // Matrix increments are 16.16 source pixels per destination pixel.
        int du = c*1024/scale, dv = s*1024/scale;
        int left = 240, top = 160, right = 0, bottom = 0;
        for(int corner = 0; corner < 4; ++corner)
        {
            int u = (corner&1 ? texture.width : 0)-texture.pivot_x;
            int v = (corner&2 ? texture.height : 0)-texture.pivot_y;
            int px = x/256+int((int64_t(u)*c-int64_t(v)*s)*scale/(16384*256));
            int py = y/256+int((int64_t(u)*s+int64_t(v)*c)*scale/(16384*256));
            left = std::min(left,px-1); right = std::max(right,px+1);
            top = std::min(top,py-1); bottom = std::max(bottom,py+1);
        }
        left = std::max(0,left); right = std::min(240,right);
        top = std::max(first_y,top); bottom = std::min(first_y+rows,bottom);
        int blur = target >= stage_mask_first ? 0 : std::clamp(entity.get(stage_property::blur)/16,0,16);
        const uint8_t* texels = blur == 16 && texture.blurred ? texture.blurred : texture.pixels;
        if(blur == 16) blur = 0;
        int tint = std::clamp(entity.get(stage_property::tint)/16,0,16);
        int reveal = entity.get(stage_property::reveal);
        int red=entity.get(stage_property::red), green=entity.get(stage_property::green), blue=entity.get(stage_property::blue);
        bool colored=red!=256 || green!=256 || blue!=256;
        // A rectangle belongs to the same depth group as the actors it clips.
        // Its own black/translucent fill is composed immediately below them.
        int clip_id = -1;
        if(entity.maskable && target < stage_overlay)
            for(int candidate = stage_mask_first; candidate < stage_entity_count; ++candidate)
            {
                const auto& mask = player.entity(candidate);
                if(mask.image >= 0 && mask.get(stage_property::opacity) > 0 &&
                   (mask.order+1)/20 == entity.order/20) clip_id = candidate;
            }
        int clip_x = 0, clip_y = 0, clip_du = 0, clip_dv = 0, clip_w = 0, clip_h = 0, clip_opacity = 0;
        if(clip_id >= 0)
        {
            const auto& mask = player.entity(clip_id);
            const auto& shape = stage_textures[mask.image];
            int dx = mask.get(stage_property::x)-120*256-camera.get(stage_property::x);
            int dy = mask.get(stage_property::y)-80*256-camera.get(stage_property::y);
            clip_x = 120*256+int((int64_t(dx)*cs-int64_t(dy)*sn)*zoom/(16384*256));
            clip_y = 80*256+int((int64_t(dx)*sn+int64_t(dy)*cs)*zoom/(16384*256));
            int clip_scale = std::max(8,mask.get(stage_property::scale)*zoom/256);
            int clip_angle = mask.get(stage_property::angle)+camera.get(stage_property::angle);
            clip_du = sin_angle(clip_angle+90*256)*1024/clip_scale;
            clip_dv = sin_angle(clip_angle)*1024/clip_scale;
            clip_w = shape.width; clip_h = shape.height;
            clip_opacity = mask.get(stage_property::opacity)/16;
        }
        bool simple = ! colored && clip_id < 0 && ! blur && tint == 16 && opacity >= 256 && reveal >= 256;
        for(int py = top; py < bottom; ++py)
        {
            int dx = left*256-x, dy = py*256-y;
            int u = texture.pivot_x*65536+int((int64_t(dx)*du+int64_t(dy)*dv)/256);
            int v = texture.pivot_y*65536+int((-int64_t(dx)*dv+int64_t(dy)*du)/256);
            int cdx = left*256-clip_x, cdy = py*256-clip_y;
            int cu = clip_w/2*65536+int((int64_t(cdx)*clip_du+int64_t(cdy)*clip_dv)/256);
            int cv = clip_h/2*65536+int((-int64_t(cdx)*clip_dv+int64_t(cdy)*clip_du)/256);
            if(simple && ! dv && unsigned(v>>16) < texture.height)
            {
                auto* source = texels+(v>>16)*texture.width;
                if(du == 65536 && texture.opaque && u >= 0 && (u>>16)+right-left <= texture.width)
                {
                    std::memcpy(pixels+py*240+left,source+(u>>16),unsigned(right-left));
                    continue;
                }
                for(int px = left; px < right; ++px,u+=du)
                {
                    unsigned tx = unsigned(u>>16);
                    if(tx < texture.width)
                    {
                        auto color = source[tx];
                        if(color) pixels[py*240+px] = color;
                    }
                }
                continue;
            }
            for(int px = left; px < right; ++px,u+=du,v-=dv,cu+=clip_du,cv-=clip_dv)
            {
                unsigned tx = unsigned(u>>16), ty = unsigned(v>>16);
                if(tx >= texture.width || ty >= texture.height) continue;
                unsigned at = ty*texture.width+tx;
                uint8_t color = texels[at];
                if(! color) continue;
                if(blur && texture.blurred) color = blend_fast(color,texture.blurred[at],blur);
                if(colored)
                {
                    unsigned rgb=stage_palette[color];
                    unsigned r=(rgb&31)*red/256, g=((rgb>>5)&31)*green/256, b=((rgb>>10)&31)*blue/256;
                    color=stage_rgb_lookup[r|(g<<5)|(b<<10)];
                }
                if(tint < 16) color = blend_fast(1,color,tint);
                int outside = clip_id >= 0 && (unsigned(cu>>16) >= unsigned(clip_w) || unsigned(cv>>16) >= unsigned(clip_h));
                if(outside && clip_opacity >= 16) continue;
                int alpha = mask_alpha(opacity,entity.maskable ? entity.mask : -1,px,py);
                if(outside) alpha = alpha*(16-clip_opacity)/16;
                if(reveal < 256)
                {
                    int blend = mask_alpha(reveal,entity.maskable ? entity.mask : -1,px,py);
                    if(entity.previous >= 0)
                    {
                        const auto& previous = stage_textures[entity.previous];
                        unsigned ox = tx*previous.width/texture.width, oy = ty*previous.height/texture.height;
                        uint8_t old = previous.pixels[oy*previous.width+ox];
                        if(old) color = blend_fast(old,color,blend);
                        else alpha = alpha*blend/16;
                    }
                    else alpha = alpha*blend/16;
                }
                auto& destination = pixels[py*240+px];
                destination = alpha >= 16 ? color : blend_fast(destination,color,alpha);
            }
        }
    }
}
}
