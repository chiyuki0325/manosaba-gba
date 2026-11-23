#ifndef IMAGE_SCENE_H
#define IMAGE_SCENE_H
#include "stage_player.h"
#include "screen_effect.h"
#include "graphics_data.h"
#include "graphics_profile.h"
#include "bn_optional.h"
#include "bn_affine_bg_ptr.h"
#include "bn_affine_bg_tiles_ptr.h"
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_bg_palette_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_sprite_affine_mat_ptr.h"
#include "bn_vector.h"
#include "bn_blending_transparency_attributes_hbe_ptr.h"
#include "bn_rect_window_boundaries_hbe_ptr.h"
#include "bn_regular_bg_attributes.h"
#include "bn_regular_bg_attributes_hbe_ptr.h"
#include "bn_regular_bg_position_hbe_ptr.h"
namespace manosaba
{
class text_canvas;
class image_scene
{
public:
    void protect() const;
    int visible_background(int plane) const;
    void archive(save_archive& data);
    void checkpoint(save_archive& data,int bank,int frame,bool double_buffer);
    void settle(){_player.settle();}
    void set_bank(int bank,bool double_buffer=true);
    void request(int frame) { _frame=frame; }
    void invalidate() {}
    bool update(int frames=1,bool input=false);
    bool ready() const { return _ready; }
    void apply(int event);
    // Resource uploads freeze the clock, not dispatch of parallel commands.
    bool blocked() const { return _player.blocked(); }
    void set_visible(bool visible);
    void reset();
    void render(const text_canvas& canvas);
    void dim(bool enabled);
    bool has_content() const;
    void tint(palette_effect effect);
    int commit(int bytes);
    const stage_player& player() const { return _player; }
private:
    struct actor_slot
    {
        bn::vector<bn::sprite_ptr,64> sprites;
        bn::vector<bn::sprite_tiles_ptr,64> tiles;
        bn::vector<bn::sprite_palette_ptr,4> palettes;
        bn::optional<bn::sprite_affine_mat_ptr> matrix;
        uint64_t material=~uint64_t(0);
        uint32_t clip_key=0,viewport_key=0,display_key=0;
        int published_parts=0;
        int image=-1,phase=-1,cursor=0,decoded=0;
        int allocating=0,part_cursor=0;
        bool silhouette_shared=false;
        bn::vector<int,64> part_indices;
    };
    struct background
    {
        bn::optional<bn::regular_bg_ptr> regular;
        bn::optional<bn::affine_bg_ptr> affine;
        bn::optional<bn::rect_window_boundaries_hbe_ptr> clip_hbe;
        uint32_t clip_key=0;
        bn::optional<bn::regular_bg_tiles_ptr> regular_tiles;
        bn::optional<bn::affine_bg_tiles_ptr> affine_tiles[2];
        int entity=-1,image=-1,variant=-1,cursor=0,target=-1,target_variant=0,front=0,bytes=0,decoded=0;
        int window_x=-1024,window_y=-1024;
        uint64_t material=~uint64_t(0);
        bool map_pending=false,publish=false;
    };
    stage_player _player{nullptr,nullptr};
    const graphics_bank* _bank=nullptr;
    bn::optional<bn::bg_palette_ptr> _palette;
    background _backgrounds[2];
    actor_slot _actors[3];
    int _actor_entities[3]={0,1,2};
    uint32_t _selection_key=0;
    int _selected_actor_images[3]={-1,-1,-1};
    bool _scene_proxy=false;
    bn::optional<bn::regular_bg_tiles_ptr> _panel_tiles;
    bn::optional<bn::regular_bg_map_ptr> _panel_map;
    bn::optional<bn::regular_bg_attributes_hbe_ptr> _panel_hbe;
    bn::optional<bn::regular_bg_position_hbe_ptr> _panel_scroll_hbe;
    bn::optional<bn::regular_bg_position_hbe_ptr> _panel_scroll_x_hbe;
    bn::vector<bn::regular_bg_attributes,160> _panel_rows;
    int _panel_image=-2,_panel_priority=-1;
    bn::fixed _panel_y=-1024,_panel_x=-1024;
    bn::optional<bn::blending_transparency_attributes_hbe_ptr> _alpha_hbe;
    int _last_alpha=-1;bool _last_printer=false;
    int _frame=-1,_bank_id=-1,_allocate_delay=1,_allocation_budget=4,_decode_budget=64;
    bool _visible=false,_ready=false,_loading=false,_prepared=false;
    bool _dim=false,_double_buffer=true;
    void allocate();
    bool background_backend(bool regular);
    bool select_background_backend(int entity);
    bool begin_background(int plane,int entity,int image,int variant);
    void regular_position(int image,int x,int y);
    void prepare();
    void select_actor_cache(int* images,const uint32_t* viewport_keys);
    void positions();
    void update_panel(bool printer,int priority);
    void release_panel();
    bool retire_actor(int slot,int image,uint32_t viewport_key);
    void prepare_actor(int slot,int image,uint32_t clip_key,uint32_t viewport_key);
    int image_for(int entity) const;
    int clip_for(int entity) const;
    void clip_background(int plane,int entity);
};
}
#endif
