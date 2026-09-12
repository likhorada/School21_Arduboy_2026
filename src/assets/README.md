# Arduboy Menu and Stage Assets

Generated from `assets/intro.PNG`, the seven `assets/mainmenu_*.PNG` and
`assets/soundmenu_*.PNG` exports, plus the `assets/stage_2.png` and
`assets/stage_3.png` wall maps. Originals are never modified.

Pillow is required. From the repository root:

```sh
python3 tools/convert_assets.py
python3 tools/convert_assets.py --check
python3 tools/test_convert_assets.py
python3 tools/convert_assets.py --preview-dir /tmp/opencode/menu-previews
```

## Menu screens

The source files are RGBA 1024x512 images with black RGB channels and binary art
in alpha. Each source pixel is an exact uniform 8x8 block. The converter samples
those blocks to 128x64, maps alpha 255 to white, and rejects partial alpha,
nonblack RGB, malformed dimensions, mixed blocks, and blank output.

All eight screens are stored as ONE LZSS stream in `assets/menu_screens.h`
(`menu_screens[] PROGMEM`), not as eight 1,024-byte bitmaps: 8,192 raw bytes
become roughly 3,100. Tokens are MSB-first: `1` + 8-bit literal, or `0` +
5-bit (len-3) + 10-bit (off-1), with a 1 KiB window. Screens decode sequentially
0..N into the framebuffer, which doubles as the match window
(`(pos - off) & 1023`). The trailing zero bytes pad the final token's read-ahead.
`MENU_SCREEN_COUNT`/`MENU_SCREEN_BYTES` are the framing constants; host builds
replace the AVR-only `menu_screen_crcs[]` golden table used by `tests/render_tests.cpp`.

Rendering decodes the needed screen right into Arduboy's framebuffer (`render.cpp`
`drawMenuScreen`); on the host it decodes into a local array and draws it as a
128x64 bitmap. Eight raw screens would have needed 8,192 flash bytes; the stream
needs ~3,100 plus a ~200-byte decoder.

## Stage maps

`assets/stage_2.png` and `assets/stage_3.png` are 1:1 RGBA masks over the
playfield (103x64 = the arena inside the right sidebar). Black RGB with alpha
0 or 255; alpha 255 is a solid wall pixel. They drive both collision
(`src/arena.cpp`) and rendering (`src/render.cpp`); stage 1 has no map and is
an empty field. The converter rejects partial alpha and blank output.

Rather than shipping the 824-byte page-column masks, `assets/stage_layouts.h`
stores each map as layout markup that reproduces the mask exactly:

- `stageN_layout[]` — solid rectangles `(x0, y0, x1, y1)` inclusive, 4 bytes
  each. Found by greedy maximal-rectangle extraction (area >= 4).
- `stageN_fill[]` — horizontal runs `(x, y, len)` for the leftover ring/arc
  pixels, sorted by `(y, x)` ascending, 3 bytes each. The collision reader
  binary-searches the run for a row; rendering draws `fillRect`/runs over the
  HUD offset.

A pixel is a wall iff a rectangle contains it or a fill run draws it. Both lists
live in `PROGMEM`; the total for both stages is ~840 bytes versus 1,648 for the
original masks. Run the converter and its unit tests after editing the art so
`--check` stays green and the host tests (pixel facts, connectivity 4376/4384,
lzss CRCs) still pass.