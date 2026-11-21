// Project-local replacement for Butano's SRAM identifier; vendor stays untouched.
#include "bn_hw_sram.h"
namespace bn::hw::sram {
const char* init()
{
    *reinterpret_cast<volatile unsigned short*>(0x04000204)|=3;
    static const char identifier[]="FLASH1M_V103";
    return identifier;
}
}
