#ifndef MANOSABA_SAVE_LEGACY_H
#define MANOSABA_SAVE_LEGACY_H
#include <cstdint>
namespace manosaba
{
// Pure conversion: never executes the VM or touches live graphics/audio/UI.
bool convert_legacy_save(uint8_t* source, int size, uint8_t* destination);
} // namespace manosaba
#endif
