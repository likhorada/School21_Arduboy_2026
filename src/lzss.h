#pragma once

#include <stdint.h>

namespace gc {

// Decode `screen_count` full 1024-byte menu screens from the PROGMEM LZSS stream
// into `out`. Screens decode sequentially; `out` doubles as the 1 KiB match
// window, so decoding screens 0..N reproduces screen N exactly as if the whole
// concatenated bitmap had been decoded.
//
// Token framing (MSB-first): '1' + 8-bit literal; '0' + 5-bit (len-3) +
// 10-bit (off-1); offset < 1024. The stream is padded with trailing zero bytes.
// Defined in MENU_SCREEN_* in assets/menu_screens.h.
void lzssDecodeScreens(uint16_t screenCount, uint8_t* out,
                       const uint8_t* stream);

} // namespace gc