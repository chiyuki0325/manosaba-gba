#include "sfx_backend.h"
#include "bn_sound_items.h"
#include "maxmod.h"
namespace manosaba::sfx_backend {
namespace {
const int samples[]={
#define SFX_SAMPLE(name) bn::sound_items::name.id(),
#include "sfx_samples.inc"
#undef SFX_SAMPLE
};
}
uint16_t play(int sample,int volume,int rate)
{
    mm_sound_effect sound={};sound.id=samples[sample];sound.rate=rate;
    sound.volume=volume*255/1024;sound.panning=128;
    return mmEffectEx(&sound);
}
void stop(uint16_t handle){mmEffectCancel(handle);}
bool active(uint16_t handle){return mmEffectActive(handle);}
void volume(uint16_t handle,int value){mmEffectVolume(handle,value*255/1024);}
}
