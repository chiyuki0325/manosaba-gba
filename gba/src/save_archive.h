#ifndef MANOSABA_SAVE_ARCHIVE_H
#define MANOSABA_SAVE_ARCHIVE_H
#include <cstdint>
#include <cstring>
#include <type_traits>
namespace manosaba
{
// Explicit logical fields; rendering handles, pointers and decode caches stay out.
class save_archive
{
  public:
    save_archive(uint8_t* data, int size, bool reading) : _data(data), _size(size), _reading(reading)
    {
    }
    bool reading() const
    {
        return _reading;
    }
    bool good() const
    {
        return _good;
    }
    int size() const
    {
        return _position;
    }
    void reject()
    {
        _good = false;
    }
    void skip(int count)
    {
        if (count < 0 || _position + count > _size)
            _good = false;
        else
            _position += count;
    }
    // v5 uses explicit little-endian widths; field()/text() below read legacy v3.
    template <class T> void fixed(T& value)
    {
        using U = std::conditional_t<(sizeof(T) > 4), uint64_t, uint32_t>;
        uint8_t data[sizeof(T)] = {};
        U bits = 0;
        if (!_reading)
        {
            bits = U(value);
            for (unsigned i = 0; i < sizeof(T); ++i)
                data[i] = uint8_t(bits >> (8 * i));
        }
        bytes(data, sizeof(data));
        if (_reading)
        {
            for (unsigned i = 0; i < sizeof(T); ++i)
                bits |= U(data[i]) << (8 * i);
            value = T(bits);
        }
    }
    template <class T> void field(T& value)
    {
        uint32_t encoded = 0;
        if (!_reading)
        {
            if constexpr (std::is_signed_v<T>)
                encoded = (uint32_t(value) << 1) ^ uint32_t(int32_t(value) >> 31);
            else
                encoded = uint32_t(value);
        }
        integer(encoded);
        if (_reading)
        {
            if constexpr (std::is_signed_v<T>)
                value = T(int32_t(encoded >> 1) ^ -int32_t(encoded & 1));
            else
                value = T(encoded);
        }
    }
    void bytes(void* data, int count)
    {
        if (!_good || count < 0 || _position + count > _size)
        {
            _good = false;
            return;
        }
        if (_reading)
            std::memcpy(data, _data + _position, count);
        else
            std::memcpy(_data + _position, data, count);
        _position += count;
    }
    void text(char* data, int capacity)
    {
        unsigned length = 0;
        if (!_reading)
            while (length < unsigned(capacity) && data[length])
                ++length;
        field(length);
        if (length >= unsigned(capacity))
        {
            reject();
            return;
        }
        bytes(data, int(length));
        if (_reading && _good)
            data[length] = 0;
    }

  private:
    uint8_t* _data;
    int _size, _position = 0;
    bool _reading, _good = true;
    void integer(uint32_t& value)
    {
        if (_reading)
        {
            value = 0;
            for (unsigned shift = 0; shift < 35; shift += 7)
            {
                uint8_t b = 0;
                bytes(&b, 1);
                if (!_good)
                    return;
                if (shift == 28 && (b & 0xf0))
                {
                    reject();
                    return;
                }
                value |= uint32_t(b & 127) << shift;
                if (!(b & 128))
                    return;
            }
            reject();
        }
        else
        {
            do
            {
                uint8_t b = uint8_t(value & 127);
                value >>= 7;
                if (value)
                    b |= 128;
                bytes(&b, 1);
            } while (value && _good);
        }
    }
};
} // namespace manosaba
#endif
