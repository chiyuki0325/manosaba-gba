#include "save_store.h"
#include "flash_driver.h"
#include <cstring>
namespace manosaba {
#if !MANOSABA_SAVE_SRAM
namespace {
constexpr uint32_t magic=0x56534e4d;
constexpr uint16_t format=5;
constexpr int record_pages=30;
unsigned prefix(int slot,int version){return version==5 && slot==save_retry_slot ? 64 : save_payload_offset;}
struct page_header {
    uint32_t magic,sequence,signature,crc;
    uint16_t version,length,seed;
    uint8_t slot,flags;
    uint32_t header_crc;
    uint8_t reserved[35],commit;
};
static_assert(sizeof(page_header)==64);
bool header_valid(const page_header& h)
{return h.magic==magic && h.commit==0xa5 && h.header_crc==save_crc32(&h,24) && h.length+h.seed<=(h.version==3 ? 8192 : save_page_size)-prefix(h.slot,h.version);}
}
#endif
uint32_t save_crc32(const void* data,unsigned size,uint32_t initial)
{
    uint32_t crc=~initial;const auto* p=static_cast<const uint8_t*>(data);
    static constexpr uint32_t table[]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    while(size--){crc^=*p++;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}
    return ~crc;
}
#if !MANOSABA_SAVE_SRAM
unsigned save_store::address(int slot,int page) const
{// The global journal is a two-sector ring; keep a stale page inside it.
 return slot==save_global_slot ? 120*1024+(unsigned(page)&1u)*4096 : unsigned(page)*save_page_size;}
bool save_store::mount(uint32_t signature)
{
    _signature=signature;_phase=idle;_failed=_pending=_pending_erase=false;_watchdog=0;_sequence=0;_next_page=0;
    for(auto& s:_slots)s={};
    _available=flash_driver::detect();if(!_available)return false;
    for(int p=0;p<32;++p)
    {
        page_header h{};flash_driver::read(unsigned(p)*4096,&h,sizeof(h));
        if(!header_valid(h))continue;
        int slot=h.slot;
        if(h.version==3)
        {
            if(slot>14 || (p<record_pages && (p&1)))continue;
            if(slot>=12)slot=slot==12 ? save_auto_slot : slot==13 ? save_retry_slot : save_global_slot;
        }
        if(slot>save_global_slot || (p<record_pages)!=(slot<save_global_slot) ||
            (p>=record_pages && h.length+h.seed>4096-save_payload_offset))continue;
        auto& s=_slots[slot];if(s.page!=255 && int32_t(h.sequence-s.sequence)<=0)continue;
        s.sequence=h.sequence;s.length=h.length;s.seed=h.seed;s.version=uint8_t(h.version);s.page=uint8_t(p<record_pages ? p : p-record_pages);
        s.status=(h.version==format || h.version==3) && (slot==save_global_slot || h.signature==signature) ? save_slot_status::valid : save_slot_status::incompatible;
        if(int32_t(h.sequence-_sequence)>0){_sequence=h.sequence;if(p<record_pages)_next_page=unsigned(p+1)%record_pages;}
    }
    return true;
}
bool save_store::metadata(int slot,save_metadata& value) const
{
    if(slot==save_retry_slot || _slots[slot].page==255)return false;
    flash_driver::read(address(slot,_slots[slot].page)+64,&value,sizeof(value));return true;
}
bool save_store::needs_compaction(int slot) const
{
    if(slot==save_global_slot || _slots[slot].page!=255)return false;
    unsigned free=0;
    for(unsigned page=0;page<record_pages;++page)
    {
        bool owned=false;
        for(int id=0;id<save_global_slot;++id)
        {const auto& s=_slots[id];owned|=s.page==page || (s.version==3 && unsigned(s.page)+1==page);}
        free+=!owned;
    }
    return free<2;
}
int save_store::legacy_slot() const
{for(int slot=0;slot<save_global_slot;++slot)if(_slots[slot].version==3 && _slots[slot].status==save_slot_status::valid)return slot;return -1;}
bool save_store::begin_write(int slot,uint8_t* page,unsigned size,unsigned seed_size,const save_metadata& metadata)
{
    if(!_available || busy() || needs_compaction(slot) || size>save_state_limit || seed_size>save_seed_limit || size+seed_size>save_page_size-save_payload_offset)return false;
    _slot=slot;_page=-1;
    if(slot==save_global_slot)_page=_slots[slot].page==255 ? 0 : _slots[slot].page^1;
    else for(unsigned i=0;i<record_pages;++i)
    {
        unsigned candidate=(_next_page+i)%record_pages;bool owned=false;
        for(int s=0;s<save_global_slot;++s)
        {const auto& owner=_slots[s];owned|=owner.page==candidate || (owner.version==3 && unsigned(owner.page)+1==candidate);}
        if(!owned){_page=int(candidate);break;}
    }
    if(_page<0)return false;
    _buffer=page;_length=size;_seed_length=seed_size;_address=address(slot,_page);_version=format;_used=prefix(slot,_version)+size+seed_size;
    if(slot==save_global_slot && _used>4096)return false;
    if(slot==save_retry_slot)std::memmove(page+64,page+save_payload_offset,size+seed_size);
    else std::memcpy(page+64,&metadata,sizeof(metadata));
    std::memset(page,255,64);
    auto& h=*reinterpret_cast<page_header*>(page);
    h.magic=magic;h.sequence=++_sequence;h.signature=_signature;
    h.version=format;h.length=uint16_t(size);h.seed=uint16_t(seed_size);h.slot=uint8_t(slot);h.flags=0;h.commit=0xa5;
    _cursor=64;_crc=0;_failed=_pending=_pending_erase=false;_watchdog=0;_phase=prepare_data;return true;
}
bool save_store::begin_read(int slot,uint8_t* page)
{
    if(!_available || busy() || status(slot)!=save_slot_status::valid)return false;
    _slot=slot;_page=_slots[slot].page;_address=address(slot,_page);_buffer=page;
    _length=_slots[slot].length;_seed_length=_slots[slot].seed;_version=_slots[slot].version;_used=prefix(slot,_version)+_length+_seed_length;
    _cursor=0;_crc=0;_failed=_pending=_pending_erase=false;_watchdog=0;_phase=read_data;return true;
}
void save_store::complete()
{
    _phase=idle;auto& s=_slots[_slot];s.sequence=_sequence;s.length=uint16_t(_length);s.seed=uint16_t(_seed_length);s.page=uint8_t(_page);s.version=format;s.status=save_slot_status::valid;
    if(_slot<save_global_slot)_next_page=unsigned(_page+1)%record_pages;
}
unsigned save_store::progress() const
{return _phase==idle ? 100 : _phase==commit_data || _phase==commit_wait ? 99 : _phase==program_data || _phase==read_data ? _cursor*90/_used : _phase==verify_data ? 95 : 0;}
void save_store::step()
{
    if(_phase==idle)return;
    if(_pending){if(!flash_driver::ready())return;_pending=false;_watchdog=0;}
    switch(_phase)
    {
    case prepare_data:
    {
        unsigned count=_used-_cursor;if(count>32)count=32;
        _crc=save_crc32(_buffer+_cursor,count,_crc);_cursor+=count;
        if(_cursor==_used)
        {auto& h=*reinterpret_cast<page_header*>(_buffer);h.crc=_crc;h.header_crc=save_crc32(_buffer,24);_cursor=0;_phase=erase_first;}
        break;
    }
    case erase_first:
        flash_driver::erase(_address);_pending=_pending_erase=true;_phase=program_data;break;
    case program_data:
        while(_cursor<_used && (_cursor==63 || _buffer[_cursor]==255))++_cursor;
        if(_cursor==_used){_cursor=64;_crc=0;_phase=verify_data;break;}
        flash_driver::program(_address+_cursor,_buffer[_cursor]);++_cursor;_pending=true;_pending_erase=false;break;
    case verify_data:
    {
        uint8_t bytes[32];unsigned count=_used-_cursor;if(count>sizeof(bytes))count=sizeof(bytes);
        flash_driver::read(_address+_cursor,bytes,count);_crc=save_crc32(bytes,count,_crc);_cursor+=count;
        if(_cursor==_used)
        {
            if(_crc!=reinterpret_cast<page_header*>(_buffer)->crc){_failed=true;_phase=idle;}
            else _phase=commit_data;
        }
        break;
    }
    case commit_data:flash_driver::program(_address+63,0xa5);_pending=true;_pending_erase=false;_phase=commit_wait;break;
    case commit_wait:complete();break;
    case read_data:
    {
        unsigned count=_used-_cursor;if(count>64)count=64;
        flash_driver::read(_address+_cursor,_buffer+_cursor,count);
        if(_cursor>=64)_crc=save_crc32(_buffer+_cursor,count,_crc);
        _cursor+=count;
        if(_cursor==_used)
        {
            auto& h=*reinterpret_cast<page_header*>(_buffer);
            _failed=!header_valid(h) || h.crc!=_crc;
            if(_failed)_slots[_slot].status=save_slot_status::corrupt;
            else if(prefix(_slot,_version)==64)std::memmove(_buffer+save_payload_offset,_buffer+64,_length+_seed_length);
            _phase=idle;
        }
        break;
    }
    default:break;
    }
}
void save_store::frame()
{
    if(busy() && _pending && ++_watchdog>300){_failed=true;_phase=idle;}
}
#endif
}
