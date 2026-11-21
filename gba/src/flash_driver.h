#ifndef MANOSABA_FLASH_DRIVER_H
#define MANOSABA_FLASH_DRIVER_H
#include <cstdint>
namespace manosaba::flash_driver {
bool detect();
void read(unsigned address,void* destination,unsigned length);
void erase(unsigned address);
void program(unsigned address,uint8_t value);
bool ready();
}
#endif
