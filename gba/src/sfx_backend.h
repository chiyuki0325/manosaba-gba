#ifndef MANOSABA_SFX_BACKEND_H
#define MANOSABA_SFX_BACKEND_H
#include <stdint.h>
namespace manosaba::sfx_backend
{
uint16_t play(int sample, int volume, int rate);
void stop(uint16_t handle);
bool active(uint16_t handle);
void volume(uint16_t handle, int volume);
} // namespace manosaba::sfx_backend
#endif
