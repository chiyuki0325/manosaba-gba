#ifndef MANOSABA_CREDITS_DATA_H
#define MANOSABA_CREDITS_DATA_H
#include <cstdint>
extern "C" {extern const char credits_text[];}
namespace manosaba {
enum credits_ink {credits_heading=1,credits_center=2,credits_fast=4};
struct credits_entry {const uint32_t* rows;uint16_t count;};
extern const credits_entry credits_entries[2];
}
#endif
