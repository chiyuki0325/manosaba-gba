#pragma once
#include "bn_tile.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_affine_bg_map_ptr.h"
#include "bn_regular_bg_ptr.h"
#include "bn_affine_bg_ptr.h"
namespace manosaba::video_upload {
// Finish CPU painting without advancing the story clock or publishing half a page.
extern bool finishing;
extern bn::tile scenery[1024];
extern uint8_t book_pixels[216*120];
extern bool book_cache_dirty;
void reclaim();
void start_frame();
void protect(const bn::regular_bg_ptr& bg);
void protect(const bn::affine_bg_ptr& bg);
void protect(const void* address,int bytes);
void prepare();
bool pending();
const bn::tile& stable_tile(const bn::tile& tile);
bn::regular_bg_map_ptr map(const bn::regular_bg_map_item& item,const bn::regular_bg_tiles_ptr& tiles,const bn::bg_palette_ptr& palette);
bn::affine_bg_map_ptr map(const bn::affine_bg_map_item& item,const bn::affine_bg_tiles_ptr& tiles,const bn::bg_palette_ptr& palette);
// VRAM publication only; CPU canvas composition must finish synchronously.
void copy(const void* source,void* target,int bytes);
bn::tile& sprite_tile(bn::tile* target);
void begin();
void end();
void transfer(const void* source,void* target,int bytes);
template<class T> void upload(const T& source,int count,T& target){transfer(&source,&target,count*sizeof(T));}
void commit();
}
