#include "music_backend.h"
#include "bn_music.h"
#include "bn_music_items.h"
#include "bn_common.h"
#include "maxmod.h"
#include <stddef.h>

extern "C"
{
    typedef struct tmm_mas_head mm_mas_head;
#include "player_types.h"
    extern mpl_layer_information mmLayerMain;
    uintptr_t mmGetModuleAddress(mm_word id);
}

// This bridge is tied to the checked-in Maxmod ABI. Loop changes alter only
// the byte consumed at sequence end, preserving current notes/order/row/tick.
static_assert(sizeof(mpl_layer_information) == 60);
static_assert(offsetof(mpl_layer_information, mode) == 50);
static_assert(offsetof(mpl_layer_information, songadr) == 24);

namespace manosaba::music_backend
{
namespace
{
const bn::music_item modules[] = {
#define MUSIC_MODULE(name) bn::music_items::name,
#include "music_modules.inc"
#undef MUSIC_MODULE
};
struct loop_request
{
    unsigned module_id;
    bool pending;
    bool loop;
};
BN_DATA_EWRAM_BSS loop_request request;

void apply_loop(bool consume)
{
    if (request.pending && mmLayerMain.isplaying &&
        uintptr_t(mmLayerMain.songadr) == mmGetModuleAddress(request.module_id) + 8)
    {
        mmLayerMain.mode = request.loop ? MM_PLAY_LOOP : MM_PLAY_ONCE;
        if (consume)
            request.pending = false;
    }
}
} // namespace
void start(int module, int volume_q10, bool loop, int order)
{
    request.module_id = unsigned(modules[module].id());
    request.pending = false;
    modules[module].play(bn::fixed(volume_q10) / 1024, loop);
    if (order)
        bn::music::set_position(order);
}
void stop()
{
    request.pending = false;
    bn::music::stop();
}
void set_volume(int volume_q10)
{
    bn::music::set_volume(bn::fixed(volume_q10) / 1024);
}
// Apply now for an imminent sequence end, then once again after Butano has
// flushed this frame's queued stop/play commands. The old instance can still
// be active when reset/play/loop-change are submitted in the same frame.
void set_loop(bool loop)
{
    request.pending = true;
    request.loop = loop;
    apply_loop(false);
}
void update()
{
    apply_loop(true);
}
void pause()
{
    bn::music::pause();
}
void resume()
{
    bn::music::resume();
}
bool playing()
{
    return bn::music::playing();
}
bool looping()
{
    return request.pending ? request.loop : mmLayerMain.mode == MM_PLAY_LOOP;
}
} // namespace manosaba::music_backend
