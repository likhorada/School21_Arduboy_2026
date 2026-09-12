# Arduboy Menu Assets

Generated from `assets/intro.PNG` and the seven `assets/mainmenu_*.PNG` and
`assets/soundmenu_*.PNG` exports. Originals are never modified.

The source files are RGBA 1024x512 images with black RGB channels and binary art
in alpha. Each source pixel is an exact uniform 8x8 block. The converter samples
those blocks to 128x64, maps alpha 255 to white, and rejects partial alpha,
nonblack RGB, malformed dimensions, mixed blocks, and blank output.

Pillow is required. From the repository root:

```sh
python3 tools/convert_assets.py
python3 tools/convert_assets.py --check
python3 tools/test_convert_assets.py
python3 tools/convert_assets.py --check --preview-dir /tmp/opencode/menu-previews
```

Each header contains exactly 1,024 bytes of `const uint8_t` in `PROGMEM`, with
a distinct `<filename>_bitmap` symbol and a compile-time size assertion. The
array has no width/height prefix: byte index is `(y / 8) * 128 + x`, and bit
`y % 8` is the white pixel. A host fallback defines `PROGMEM` as empty when the
AVR header is unavailable.

Example use:

```cpp
#include "assets/mainmenu_play.h"

arduboy.clear();
arduboy.drawBitmap(0, 0, mainmenu_play_bitmap, 128, 64, WHITE);
```

The game includes these headers only from `render.cpp`. Intro, main-menu and
sound-menu screens are drawn directly into Arduboy's existing framebuffer.
Eight raw screens require 8,192 flash bytes and no additional SRAM bitmap copies.
