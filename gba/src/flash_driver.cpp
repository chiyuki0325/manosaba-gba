#include "flash_driver.h"
namespace manosaba::flash_driver
{
namespace
{
volatile uint8_t* const memory = reinterpret_cast<volatile uint8_t*>(0x0e000000);
volatile uint16_t* const ime = reinterpret_cast<volatile uint16_t*>(0x04000208);
__attribute__((section(".ewram_bss"))) int bank;
__attribute__((section(".ewram_bss"))) unsigned pending_address;
__attribute__((section(".ewram_bss"))) uint8_t pending_value;
void unlock()
{
    memory[0x5555] = 0xaa;
    memory[0x2aaa] = 0x55;
}
void select(unsigned address)
{
    int target = int(address >> 16);
    if (target == bank)
        return;
    uint16_t enabled = *ime;
    *ime = 0;
    unlock();
    memory[0x5555] = 0xb0;
    memory[0] = uint8_t(target);
    *ime = enabled;
    bank = target;
}
} // namespace
void forget_bank()
{
    bank = -1;
}
__attribute__((section(".ewram"), noinline)) bool detect()
{
    *reinterpret_cast<volatile uint16_t*>(0x04000204) |= 3;
    uint16_t enabled = *ime;
    *ime = 0;
    // Exercise the 1M bank command before ID; auto-detecting emulators start at 512K.
    unlock();
    memory[0x5555] = 0xb0;
    memory[0] = 1;
    unlock();
    memory[0x5555] = 0x90;
    unsigned id = unsigned(memory[0]) | (unsigned(memory[1]) << 8);
    unlock();
    memory[0x5555] = 0xf0;
    *ime = enabled;
    bank = -1;
    return id == 0x09c2 || id == 0x1362;
}
__attribute__((section(".ewram"), noinline)) void read(unsigned address, void* destination, unsigned length)
{
    auto* out = static_cast<uint8_t*>(destination);
    while (length--)
    {
        select(address);
        *out++ = memory[address & 65535];
        ++address;
    }
}
void erase(unsigned address)
{
    select(address);
    uint16_t enabled = *ime;
    *ime = 0;
    unlock();
    memory[0x5555] = 0x80;
    unlock();
    memory[address & 65535] = 0x30;
    *ime = enabled;
    pending_address = address;
    pending_value = 255;
}
void program(unsigned address, uint8_t value)
{
    select(address);
    uint16_t enabled = *ime;
    *ime = 0;
    unlock();
    memory[0x5555] = 0xa0;
    memory[address & 65535] = value;
    *ime = enabled;
    pending_address = address;
    pending_value = value;
}
__attribute__((section(".ewram"), noinline)) bool ready()
{
    select(pending_address);
    return memory[pending_address & 65535] == pending_value;
}
} // namespace manosaba::flash_driver
