#ifndef LZSS_H
#define LZSS_H
#include <cstdint>
namespace manosaba
{
void lzss_decode(const uint8_t* source, uint8_t* destination, int size);
}
#endif
