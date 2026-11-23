#include "build_config.h"
#include "sram_driver.h"
#if MANOSABA_SAVE_SRAM
namespace manosaba::sram_driver {
void read(unsigned address,void* destination,unsigned length)
{
    auto* out=static_cast<uint8_t*>(destination);
    const volatile uint8_t* memory=reinterpret_cast<volatile uint8_t*>(0x0e000000+address);
    while(length--)*out++=*memory++;
}
void write(unsigned address,const void* source,unsigned length)
{
    const auto* in=static_cast<const uint8_t*>(source);
    volatile uint8_t* memory=reinterpret_cast<volatile uint8_t*>(0x0e000000+address);
    while(length--)*memory++=*in++;
}
}
#endif
