#include "lzss.h"

namespace manosaba
{
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void lzss_decode(const uint8_t* source, uint8_t* destination, int size)
{
    int at = 0;
    while(at < size)
    {
        unsigned flags = *source++;
        for(unsigned bit = 128; bit && at < size; bit >>= 1)
        {
            if(flags & bit)
            {
                unsigned token = unsigned(*source++) << 8;
                token |= *source++;
                int distance = int(token & 4095) + 1;
                int count = int(token >> 12) + 3;
                while(count--)
                {
                    destination[at] = destination[at - distance];
                    ++at;
                }
            }
            else
            {
                destination[at++] = *source++;
            }
        }
    }
}
}
