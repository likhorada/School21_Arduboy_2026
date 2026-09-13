#include "lzss.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#endif

namespace gc {
namespace {

constexpr uint8_t kLenBits = 5;
constexpr uint8_t kLenMin = 3;
constexpr uint8_t kOffBits = 10;
constexpr uint16_t kWindow = 1024;
constexpr uint16_t kScreenBytes = 1024;

// MSB-first bit reader over the PROGMEM stream; reads unpadded bytes strictly
// forward (the emitted blob carries trailing zero padding for read-ahead).
class BitReader {
public:
    explicit BitReader(const uint8_t* stream) : stream_(stream) {}

    uint16_t get(uint8_t count) {
        while (bits_ < count) {
            acc_ = (acc_ << 8) | pgm_read_byte(&stream_[pos_++]);
            bits_ += 8;
        }
        bits_ -= count;
        return static_cast<uint16_t>((acc_ >> bits_) & ((1u << count) - 1));
    }

private:
    const uint8_t* stream_;
    uint16_t pos_ = 0;
    uint32_t acc_ = 0;
    uint8_t bits_ = 0;
};

} // namespace

void lzssDecodeScreens(uint16_t screenCount, uint8_t* out,
                       const uint8_t* stream) {
    BitReader reader(stream);
    for (uint16_t screen = 0; screen < screenCount; ++screen) {
        uint16_t op = 0;
        while (op < kScreenBytes) {
            if (reader.get(1)) {
                out[op++] = static_cast<uint8_t>(reader.get(8));
            } else {
                const uint8_t length = static_cast<uint8_t>(reader.get(kLenBits)) + kLenMin;
                const uint16_t offset = reader.get(kOffBits) + 1;
                for (uint8_t i = 0; i < length; ++i) {
                    out[op] = out[(op - offset) & (kWindow - 1)];
                    ++op;
                }
            }
        }
    }
}

} // namespace gc