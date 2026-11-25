#ifndef MANOSABA_REENACT_SCENE_H
#define MANOSABA_REENACT_SCENE_H
#include "reenact_data.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_bg_palette_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_vector.h"
#include "screen_effect.h"
namespace manosaba
{
class reenact_scene
{
  public:
    void protect() const;
    void reset();
    bool release();
    void request(int scene, bool settled = false, bool closed = false);
    void visible(bool enabled);
    void tint(palette_effect effect);
    void prepare();
    void update(bool display_ready, int frames = 1);
    void open();
    void close();
    bool transitioning() const
    {
        return _transition != 0;
    }
    bool ready() const
    {
        return _bg && _background_uploaded == 600 && _curtain_uploaded == 600 && _pose == _target;
    }
    bool active() const
    {
        return _bg.has_value();
    }
    int scene() const
    {
        return _scene;
    }
    int clock() const
    {
        return _clock;
    }
    int sprites() const
    {
        return _sprites.size();
    }
    int tile_bytes() const;
    int commit(int bytes);

  private:
    bn::optional<bn::regular_bg_tiles_ptr> _background_tiles, _curtain_tiles;
    bn::optional<bn::regular_bg_ptr> _bg, _curtains[2];
    bn::optional<bn::bg_palette_ptr> _background_palette;
    bn::optional<bn::sprite_palette_ptr> _palette;
    bn::vector<bn::sprite_ptr, 128> _sprites;
    bn::vector<bn::sprite_tiles_ptr, 128> _tiles[2];
    int _scene = -1, _clock = 0, _pose = -1, _target = 0, _front = 0;
    int _background_uploaded = 0, _curtain_uploaded = 0, _prepared = 0, _upload_part = 0, _upload_tile = 0;
    int _opening = 0, _transition = 0, _fade = 0, _position = -1;
    const uint16_t* _background = nullptr;
    bool _visible = false, _objects_ready = false, _releasing = false, _fade_out = false, _retiring = false,
         _panel = false;
    void allocate();
    void request_pose(int pose);
    void publish();
    void position();
    int phase() const;
    int target_pose() const;
    void panel(bool enabled);
    void curtains();
};
} // namespace manosaba
#endif
