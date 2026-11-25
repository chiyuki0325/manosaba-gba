#include "save_store.h"
#include "sram_driver.h"
#include <cstring>
#if MANOSABA_SAVE_SRAM
namespace manosaba
{
namespace
{
constexpr uint32_t magic = 0x56534e4d;
constexpr unsigned scratch_page = 10, global_first = 11;
constexpr unsigned retry_address = 9 * 3072, scratch_address = retry_address + 1536,
                   global_address = scratch_address + 3072;
static_assert(global_address + 2 * 256 == 32768);
struct page_header
{
    uint32_t magic, sequence, signature, crc;
    uint16_t version, length, seed;
    uint8_t slot, flags;
    uint32_t header_crc;
    uint8_t reserved[35], commit;
};
static_assert(sizeof(page_header) == 64);
unsigned location(unsigned page)
{ // The global journal is a two-page ring; keep a stale page inside it.
    return page < 9               ? page * 3072
           : page == 9            ? retry_address
           : page == scratch_page ? scratch_address
                                  : global_address + ((page - global_first) & 1u) * 256;
}
unsigned prefix(int slot)
{
    return slot >= save_retry_slot ? 64 : save_payload_offset;
}
unsigned capacity(int slot)
{
    return slot == save_global_slot ? 256 : slot == save_retry_slot ? 1536 : 3072;
}
bool valid(const page_header& h)
{
    return h.magic == magic && h.commit == 0xa5 && h.slot <= save_global_slot &&
           h.length + h.seed + prefix(h.slot) <= capacity(h.slot) && h.header_crc == save_crc32(&h, 24);
}
void byte(unsigned address, uint8_t value)
{
    sram_driver::write(address, &value, 1);
}
bool body_valid(unsigned address, const page_header& h)
{
    uint8_t data[32];
    unsigned crc = 0, end = prefix(h.slot) + h.length + h.seed;
    for (unsigned at = 64; at < end;)
    {
        unsigned count = end - at;
        if (count > sizeof(data))
            count = sizeof(data);
        sram_driver::read(address + at, data, count);
        crc = save_crc32(data, count, crc);
        at += count;
    }
    return crc == h.crc;
}
} // namespace
unsigned save_store::address(int, int page) const
{
    return location(unsigned(page));
}
bool save_store::needs_compaction(int) const
{
    return false;
}
int save_store::legacy_slot() const
{
    return -1;
}
bool save_store::mount(uint32_t signature)
{
    _signature = signature;
    _phase = idle;
    _failed = _pending = _pending_erase = false;
    _sequence = 0;
    _repair_slot = -1;
    _available = true;
    for (auto& slot : _slots)
        slot = {};
    for (unsigned page = 0; page < 13; ++page)
    {
        page_header h{};
        sram_driver::read(location(page), &h, sizeof(h));
        if (!valid(h))
            continue;
        if (page < scratch_page && h.slot != page)
            continue;
        if (page == scratch_page && (h.slot == save_global_slot || !body_valid(location(page), h)))
            continue;
        if (page >= global_first && h.slot != save_global_slot)
            continue;
        auto& s = _slots[h.slot];
        if (s.page != 255 && int32_t(h.sequence - s.sequence) <= 0)
            continue;
        s.sequence = h.sequence;
        s.length = h.length;
        s.seed = h.seed;
        s.version = uint8_t(h.version);
        s.page = uint8_t(page);
        s.status = h.version == 5 && (h.slot == save_global_slot || h.signature == signature)
                       ? save_slot_status::valid
                       : save_slot_status::incompatible;
        if (int32_t(h.sequence - _sequence) > 0)
            _sequence = h.sequence;
    }
    return true;
}
bool save_store::metadata(int slot, save_metadata& value) const
{
    if (slot >= save_retry_slot || _slots[slot].page == 255)
        return false;
    sram_driver::read(location(_slots[slot].page) + 64, &value, sizeof(value));
    return true;
}
bool save_store::begin_write(int slot, uint8_t* page, unsigned size, unsigned seed_size, const save_metadata& metadata)
{
    if (busy() || size > save_state_limit || seed_size > save_seed_limit ||
        (slot == save_retry_slot && (size > save_seed_limit || seed_size)) ||
        prefix(slot) + size + seed_size > capacity(slot))
        return false;
    _slot = slot;
    _page =
        slot == save_global_slot ? (_slots[slot].page == global_first ? global_first + 1 : global_first) : scratch_page;
    _buffer = page;
    _length = size;
    _seed_length = seed_size;
    _address = location(_page);
    _used = prefix(slot) + size + seed_size;
    _version = 5;
    if (prefix(slot) == 64)
        std::memmove(page + 64, page + save_payload_offset, size + seed_size);
    else
        std::memcpy(page + 64, &metadata, sizeof(metadata));
    std::memset(page, 255, 64);
    auto& h = *reinterpret_cast<page_header*>(page);
    h.magic = magic;
    h.sequence = ++_sequence;
    h.signature = _signature;
    h.version = 5;
    h.length = uint16_t(size);
    h.seed = uint16_t(seed_size);
    h.slot = uint8_t(slot);
    h.flags = 0;
    h.commit = 0xa5;
    _cursor = 64;
    _crc = 0;
    _failed = false;
    _phase = prepare_data;
    return true;
}
bool save_store::begin_read(int slot, uint8_t* page)
{
    if (busy() || status(slot) != save_slot_status::valid)
        return false;
    _slot = slot;
    _page = _slots[slot].page;
    _address = location(_page);
    _buffer = page;
    _length = _slots[slot].length;
    _seed_length = _slots[slot].seed;
    _used = prefix(slot) + _length + _seed_length;
    _version = 5;
    _cursor = 0;
    _crc = 0;
    _failed = false;
    _phase = read_data;
    return true;
}
void save_store::complete()
{
    auto& s = _slots[_slot];
    s.sequence = _sequence;
    s.length = uint16_t(_length);
    s.seed = uint16_t(_seed_length);
    s.page = uint8_t(_page);
    s.version = 5;
    s.status = save_slot_status::valid;
    _phase = idle;
}
unsigned save_store::progress() const
{
    return busy() ? _phase == copy_old ? 0 : _cursor * 90 / _used : 100;
}
void save_store::frame()
{
}
void save_store::step()
{
    switch (_phase)
    {
    case prepare_data:
    {
        unsigned count = _used - _cursor;
        if (count > 32)
            count = 32;
        _crc = save_crc32(_buffer + _cursor, count, _crc);
        _cursor += count;
        if (_cursor != _used)
            break;
        auto& h = *reinterpret_cast<page_header*>(_buffer);
        h.crc = _crc;
        h.header_crc = save_crc32(&h, 24);
        _cursor = 0;
        _phase = invalidate_data;
        _repair_slot = -1;
        if (_slot != save_global_slot)
            for (int slot = 0; slot < save_global_slot; ++slot)
                if (_slots[slot].page == scratch_page)
                {
                    _repair_slot = slot;
                    _repair_used = prefix(slot) + _slots[slot].length + _slots[slot].seed;
                    byte(location(slot) + 63, 0);
                    _phase = copy_old;
                    break;
                }
        break;
    }
    case copy_old:
    {
        // A committed scratch record remains authoritative until its home copy
        // is complete. Reset at any byte keeps one entire valid record.
        uint8_t data[32];
        unsigned count = _repair_used - _cursor;
        if (count > sizeof(data))
            count = sizeof(data);
        sram_driver::read(scratch_address + _cursor, data, count);
        if (_cursor <= 63 && _cursor + count > 63)
            data[63 - _cursor] = 0;
        sram_driver::write(location(_repair_slot) + _cursor, data, count);
        _cursor += count;
        if (_cursor == _repair_used)
        {
            byte(location(_repair_slot) + 63, 0xa5);
            _slots[_repair_slot].page = uint8_t(_repair_slot);
            _cursor = 0;
            _phase = invalidate_data;
        }
        break;
    }
    case invalidate_data:
        byte(_address + 63, 0);
        _cursor = 0;
        _phase = program_data;
        break;
    case program_data:
    {
        unsigned count = _used - _cursor;
        if (count > 32)
            count = 32;
        if (_cursor == 32)
            _buffer[63] = 0;
        sram_driver::write(_address + _cursor, _buffer + _cursor, count);
        _cursor += count;
        if (_cursor == _used)
        {
            _buffer[63] = 0xa5;
            _cursor = 64;
            _crc = 0;
            _phase = verify_data;
        }
        break;
    }
    case verify_data:
    {
        uint8_t data[32];
        unsigned count = _used - _cursor;
        if (count > sizeof(data))
            count = sizeof(data);
        sram_driver::read(_address + _cursor, data, count);
        _crc = save_crc32(data, count, _crc);
        _cursor += count;
        if (_cursor == _used)
        {
            if (_crc != reinterpret_cast<page_header*>(_buffer)->crc)
            {
                _failed = true;
                _phase = idle;
            }
            else
                _phase = commit_data;
        }
        break;
    }
    case commit_data:
        byte(_address + 63, 0xa5);
        complete();
        break;
    case read_data:
    {
        unsigned count = _used - _cursor;
        if (count > 64)
            count = 64;
        sram_driver::read(_address + _cursor, _buffer + _cursor, count);
        if (_cursor >= 64)
            _crc = save_crc32(_buffer + _cursor, count, _crc);
        _cursor += count;
        if (_cursor == _used)
        {
            auto& h = *reinterpret_cast<page_header*>(_buffer);
            _failed = !valid(h) || h.crc != _crc;
            if (_failed)
                _slots[_slot].status = save_slot_status::corrupt;
            else if (prefix(_slot) == 64)
                std::memmove(_buffer + save_payload_offset, _buffer + 64, _length + _seed_length);
            _phase = idle;
        }
        break;
    }
    default:
        break;
    }
}
} // namespace manosaba
#endif
