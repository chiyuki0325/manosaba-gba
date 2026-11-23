#ifndef MANOSABA_SAVE_STORE_H
#define MANOSABA_SAVE_STORE_H
#include <cstdint>
#include "build_config.h"
namespace manosaba {
#if MANOSABA_SAVE_SRAM
constexpr int save_manual_slots=8,save_auto_slot=8,save_retry_slot=9,save_global_slot=10;
constexpr int save_page_size=3072,save_payload_offset=256,save_state_limit=2816,save_seed_limit=1280;
constexpr int save_buffer_size=save_page_size;
#else
constexpr int save_manual_slots=27,save_auto_slot=27,save_retry_slot=28,save_global_slot=29;
constexpr int save_page_size=4096,save_payload_offset=256,save_state_limit=3840,save_seed_limit=1536;
constexpr int save_buffer_size=8192; // Legacy v3 records can span two sectors.
#endif
enum class save_slot_status:uint8_t {empty,valid,corrupt,incompatible};
struct save_timestamp {uint8_t year,month,day,hour,minute,second,weekday,valid;};
struct save_metadata
{
    uint32_t legacy_frames=0,trial_left=0;
    uint16_t script=0,preview=65535;
    uint8_t hero=0,trial=0;
    save_timestamp timestamp{};
    char summary[96]={};
    uint8_t reserved[74]={};
};
static_assert(sizeof(save_metadata)==192);
uint32_t save_crc32(const void* data,unsigned size,uint32_t initial=0);

class save_store
{
public:
    bool mount(uint32_t signature);
    save_slot_status status(int slot) const {return _slots[slot].status;}
    bool metadata(int slot,save_metadata& value) const;
    bool begin_write(int slot,uint8_t* page,unsigned size,unsigned seed_size,const save_metadata& metadata);
    bool begin_read(int slot,uint8_t* page);
    bool needs_compaction(int slot) const;
    int legacy_slot() const;
    void step();
    void frame();
    bool waiting_erase() const {return _pending && _pending_erase;}
    bool busy() const {return _phase!=idle;}
    bool failed() const {return _failed;}
    bool available() const {return _available;}
    unsigned progress() const;
    unsigned length() const {return _length;}
    unsigned seed_length() const {return _seed_length;}
    int version() const {return _version;}
private:
    struct record {uint32_t sequence=0;uint16_t length=0,seed=0;uint8_t page=255,version=0;save_slot_status status=save_slot_status::empty;};
    record _slots[save_global_slot+1];
    enum phase {idle,prepare_data,erase_first,program_data,verify_data,commit_data,commit_wait,read_data,copy_old,invalidate_data};
    phase _phase=idle;uint8_t* _buffer=nullptr;
    uint32_t _signature=0,_sequence=0,_crc=0;
    unsigned _address=0,_cursor=0,_used=0,_length=0,_seed_length=0,_next_page=0;
    int _version=5;
    int _slot=0,_page=0,_watchdog=0;bool _available=false,_failed=false,_pending=false,_pending_erase=false;
#if MANOSABA_SAVE_SRAM
    int _repair_slot=-1;
    unsigned _repair_used=0;
#endif
    unsigned address(int slot,int page) const;
    void complete();
};
}
#endif
