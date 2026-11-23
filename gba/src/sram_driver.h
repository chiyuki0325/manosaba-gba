#pragma once
#include <cstdint>
namespace manosaba::sram_driver {
void read(unsigned address,void* destination,unsigned length);
void write(unsigned address,const void* source,unsigned length);
}
