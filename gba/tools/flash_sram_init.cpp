// Project-local replacement for Butano's SRAM identifier; vendor stays untouched.
#include "bn_hw_sram.h"
#include "build_config.h"
namespace bn::hw::sram
{
const char* init()
{
    *reinterpret_cast<volatile unsigned short*>(0x04000204) |= 3;
#if MANOSABA_SAVE_SRAM
    static const char identifier[] = "SRAM_V113";
#else
    static const char identifier[] = "FLASH1M_V103";
#endif
    return identifier;
}
} // namespace bn::hw::sram
