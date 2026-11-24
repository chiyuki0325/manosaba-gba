#include "save_store.h"
#include "flash_driver.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace manosaba;
static std::array<uint8_t,131072> flash;
static unsigned operations=0,last_erase=~0u;
namespace manosaba::flash_driver {
bool detect(){return true;}
void read(unsigned address,void* destination,unsigned size){assert(address+size<=flash.size());std::memcpy(destination,flash.data()+address,size);}
void program(unsigned address,uint8_t value){assert(address<flash.size());flash[address]&=value;++operations;}
void erase(unsigned address){assert(address%4096==0 && address+4096<=flash.size());last_erase=address;std::memset(flash.data()+address,255,4096);++operations;}
bool ready(){return true;}
}
static void finish(save_store& store)
{for(int i=0;i<100000 && store.busy();++i)store.step();assert(!store.busy() && !store.failed());}
int main()
{
    constexpr unsigned signature=0x12345678;
    alignas(4) uint8_t page[save_buffer_size];flash.fill(255);
    save_store store;assert(store.mount(signature));
    save_metadata meta{};meta.script=37;std::strcpy(meta.summary,"checkpoint");
    for(int slot=0;slot<=save_global_slot;++slot)
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
        std::memset(page+save_payload_offset,round,600);assert(store.begin_write(round%save_global_slot,page,500,100,meta));finish(store);
    }
    save_store incompatible;assert(incompatible.mount(signature+1));assert(incompatible.status(0)==save_slot_status::incompatible);
    assert(!incompatible.begin_read(0,page));
    assert(incompatible.status(save_global_slot)==save_slot_status::valid);
    assert(incompatible.begin_read(save_global_slot,page));finish(incompatible);
    assert(page[save_payload_offset]==save_global_slot+1); // Global collection survives story-signature changes.
    // A committed payload with later bit damage is rejected before application.
    assert(store.begin_read(0,page));finish(store);
    for(unsigned i=0;i<30;++i)
    {
        unsigned addr=i*4096;if(flash[addr+22]==0 && flash[addr+63]==0xa5)flash[addr+256]^=1;
    }
    assert(store.mount(signature));assert(store.begin_read(0,page));
    for(int n=0;n<10000 && store.busy();++n)store.step();
    assert(store.failed() && store.status(0)==save_slot_status::corrupt);

    // Old 8KiB pages occupy both sectors until a new 4KiB record commits.
    flash.fill(255);
    struct old_header {uint32_t magic,sequence,signature,crc;uint16_t version,length,seed;uint8_t slot,flags;uint32_t header_crc;uint8_t reserved[35],commit;};
    static_assert(sizeof(old_header)==64);
    auto legacy=[&](unsigned sector,int slot,unsigned size,uint8_t value)
    {
        auto* target=flash.data()+sector*4096;std::memcpy(target+64,&meta,sizeof(meta));std::memset(target+256,value,size);
        old_header h{};h.magic=0x56534e4d;h.sequence=sector+1;h.signature=signature;h.version=3;h.length=uint16_t(size);h.slot=uint8_t(slot);h.commit=0xa5;
        h.crc=save_crc32(target+64,192+size);h.header_crc=save_crc32(&h,24);std::memcpy(target,&h,sizeof(h));
    };
    legacy(0,2,5000,0x44);legacy(2,12,400,0x55);legacy(4,13,300,0x66);legacy(30,14,120,0x77);
    save_store old;assert(old.mount(signature));assert(old.status(12)==save_slot_status::empty);
    assert(old.begin_read(2,page));finish(old);assert(old.version()==3 && old.length()==5000);
    for(int i=0;i<5000;++i)assert(page[256+i]==0x44);
    for(int slot:{save_auto_slot,save_retry_slot,save_global_slot}){assert(old.begin_read(slot,page));finish(old);assert(old.version()==3);}
    auto old_image=flash;
    std::memset(page+256,0x35,1254);operations=0;assert(old.begin_write(2,page,1254,0,meta));finish(old);unsigned migrate_cuts=operations;
    for(unsigned cut=0;cut<=migrate_cuts;++cut)
    {
        flash=old_image;save_store w;assert(w.mount(signature));std::memset(page+256,0x35,1254);operations=0;assert(w.begin_write(2,page,1254,0,meta));
        for(int guard=0;guard<100000 && w.busy() && operations<cut;++guard)w.step();
        save_store reboot;assert(reboot.mount(signature));assert(reboot.begin_read(2,page));finish(reboot);
        assert((reboot.version()==3 && reboot.length()==5000) || (reboot.version()==5 && reboot.length()==1254));
        uint8_t expected=reboot.version()==3 ? 0x44 : 0x35;for(unsigned i=0;i<reboot.length();++i)assert(page[256+i]==expected);
        assert(reboot.status(save_auto_slot)==save_slot_status::valid && reboot.status(save_retry_slot)==save_slot_status::valid);
    }
    // A stale global journal page must never address bank 2 and erase a record sector.
    flash.fill(255);
    auto v5=[&](unsigned sector,int slot,uint32_t sequence,unsigned size,uint8_t value)
    {
        auto* target=flash.data()+sector*4096;std::memset(target+256,value,size);
        old_header h{};h.magic=0x56534e4d;h.sequence=sequence;h.signature=signature;h.version=5;h.length=uint16_t(size);h.slot=uint8_t(slot);h.commit=0xa5;
        h.crc=save_crc32(target+64,192+size);h.header_crc=save_crc32(&h,24);std::memcpy(target,&h,sizeof(h));
    };
    v5(14,7,5000,800,0x5c);
    save_store guarded;assert(guarded.mount(signature));assert(guarded.status(7)==save_slot_status::valid);
    uint8_t record_copy[4096];std::memcpy(record_copy,flash.data()+14*4096,sizeof(record_copy));
    guarded.test_page(save_global_slot,16);
    for(int round=0;round<2;++round)
    {
        std::memset(page,0xff,save_buffer_size);std::memset(page+save_payload_offset,0x77,120);
        assert(guarded.begin_write(save_global_slot,page,120,0,meta));finish(guarded);
        assert(last_erase==120*1024 || last_erase==124*1024);
    }
    assert(!std::memcmp(record_copy,flash.data()+14*4096,sizeof(record_copy)));
    save_store recovered;assert(recovered.mount(signature));
    assert(recovered.begin_read(7,page));finish(recovered);assert(page[save_payload_offset]==0x5c);
    assert(recovered.begin_read(save_global_slot,page));finish(recovered);assert(page[save_payload_offset]==0x77);

    std::printf("PASS: 27 manual + auto/retry, one-sector v5, legacy auto/retry mapping and %u legacy migration power cuts\n",migrate_cuts+1);
    std::printf("PASS: 128KiB banks, all slots/globals, %u power-cut prefixes, page reuse, compatibility and CRC damage\n",cuts);
}
