#include "save_store.h"
#include "flash_driver.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
static std::array<uint8_t,131072> flash;
static unsigned operations=0;
namespace manosaba::flash_driver {
bool detect(){return true;}
void read(unsigned address,void* destination,unsigned size){assert(address+size<=flash.size());std::memcpy(destination,flash.data()+address,size);}
void program(unsigned address,uint8_t value){assert(address<flash.size());flash[address]&=value;++operations;}
void erase(unsigned address){assert(address%4096==0 && address+4096<=flash.size());std::memset(flash.data()+address,255,4096);++operations;}
bool ready(){return true;}
}
static void finish(save_store& store)
{for(int i=0;i<100000 && store.busy();++i)store.step();assert(!store.busy() && !store.failed());}
int main()
{
    constexpr unsigned signature=0x12345678;
    alignas(4) uint8_t page[save_page_size];flash.fill(255);
    save_store store;assert(store.mount(signature));
    save_metadata meta{};meta.script=37;std::strcpy(meta.summary,"checkpoint");
    for(int slot=0;slot<15;++slot)
    {
        std::memset(page+save_payload_offset,slot+1,512);
        assert(store.begin_write(slot,page,400,112,meta));finish(store);
    }
    const auto original=flash;
    // Every possible interruption of an overwrite: both erase operations, header,
    // body, verification and final commit. The old record must stay readable.
    flash=original;save_store full;assert(full.mount(signature));
    std::memset(page+save_payload_offset,0x35,512);operations=0;
    assert(full.begin_write(5,page,400,112,meta));finish(full);
    unsigned last_cut=operations,cuts=0;
    for(unsigned cut=0;cut<=last_cut;++cut)
    {
        flash=original;save_store writing;assert(writing.mount(signature));
        std::memset(page+save_payload_offset,0x35,512);operations=0;
        assert(writing.begin_write(5,page,400,112,meta));
        for(int step=0;step<100000 && writing.busy() && operations<cut;++step)writing.step();
        save_store reboot;assert(reboot.mount(signature));
        assert(reboot.begin_read(5,page));finish(reboot);
        uint8_t value=page[save_payload_offset];assert(value==6 || value==0x35);
        for(int i=0;i<512;++i)assert(page[save_payload_offset+i]==value);
        ++cuts;
    }
    flash=original;assert(store.mount(signature));
    for(int round=0;round<40;++round)
    {
        std::memset(page+save_payload_offset,round,600);assert(store.begin_write(round%14,page,500,100,meta));finish(store);
    }
    save_store incompatible;assert(incompatible.mount(signature+1));assert(incompatible.status(0)==save_slot_status::incompatible);
    assert(!incompatible.begin_read(0,page));
    assert(incompatible.status(save_global_slot)==save_slot_status::valid);
    assert(incompatible.begin_read(save_global_slot,page));finish(incompatible);
    assert(page[save_payload_offset]==15); // Global collection survives story-signature changes.
    // A committed payload with later bit damage is rejected before application.
    assert(store.begin_read(0,page));finish(store);
    for(unsigned i=0;i<15;++i)
    {
        unsigned addr=i*8192;if(flash[addr+22]==0 && flash[addr+63]==0xa5)flash[addr+256]^=1;
    }
    assert(store.mount(signature));assert(store.begin_read(0,page));
    for(int n=0;n<10000 && store.busy();++n)store.step();
    assert(store.failed() && store.status(0)==save_slot_status::corrupt);
    std::printf("PASS: 128KiB banks, all slots/globals, %u power-cut prefixes, page reuse, compatibility and CRC damage\n",cuts);
}
