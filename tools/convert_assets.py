#!/usr/bin/env python3
"""Convert RGBA menu PNGs and stage maps to compact Arduboy headers.

Menu screens (128x64 alpha masks, 1 byte/pixel block-center) are packed to
Arduboy page-column bitmaps and stored as ONE concatenated LZSS stream so the
eight 1024-byte frames cost a few hundred bytes of flash total.

Stage maps are turned into layout markup instead of bitmaps: greedy maximal
axially-aligned rectangles cover the solid parts, the leftover fragments become
horizontal fill runs. A pixel is a wall iff a rectangle contains it or a fill
run draws it. Emits probes exactly reproduce the original masks.
"""

import argparse
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NAMES = (
    "intro",
    "mainmenu_play", "mainmenu_about", "mainmenu_sound", "mainmenu_exit",
    "soundmenu_on", "soundmenu_off", "soundmenu_exit",
)
WIDTH, HEIGHT, SCALE = 128, 64, 8
SOURCE_SIZE = (WIDTH * SCALE, HEIGHT * SCALE)

# Stage collision/decoration maps: 1:1 alpha masks over the playfield. Stage 1
# (index 0) is an empty field. Each map is ARENA_WIDTH x ARENA_HEIGHT pixels.
STAGE_SOURCES = {
    "stage2": "stage_2.png",
    "stage3": "stage_3.png",
}
STAGE_W, STAGE_H = 103, 64

# Stage layout decomposition: minimal solid rectangle area (in pixels) kept as a
# rect primitive; everything smaller becomes a fill run.
RECT_MIN_AREA = 4

# LZSS framing shared with src/lzss.cpp.
LEN_BITS, LEN_MIN = 5, 3
LMAX = (1 << LEN_BITS) - 1 + LEN_MIN
OFF_BITS = 10
WINDOW = 1 << OFF_BITS
BLOB_PAD = 4


def parse_source(path):
    """Read and validate an exact 8x RGBA alpha-mask export."""
    try:
        with Image.open(path) as image:
            image.load()
            if image.format != "PNG":
                raise ValueError(f"{path}: expected PNG input")
            if image.mode != "RGBA":
                raise ValueError(f"{path}: expected RGBA, got {image.mode}")
            if image.size != SOURCE_SIZE:
                raise ValueError(
                    f"{path}: expected {SOURCE_SIZE[0]}x{SOURCE_SIZE[1]}, "
                    f"got {image.width}x{image.height}"
                )
            red, green, blue, alpha = image.split()
            if any(red.tobytes()) or any(green.tobytes()) or any(blue.tobytes()):
                raise ValueError(f"{path}: expected black RGB channels")
            alpha_bytes = alpha.tobytes()
    except (OSError, SyntaxError) as error:
        raise ValueError(f"{path}: invalid PNG: {error}") from error

    if set(alpha_bytes) - {0, 255}:
        raise ValueError(f"{path}: alpha must contain only 0 or 255")

    pixels = []
    source_width = SOURCE_SIZE[0]
    for y in range(0, SOURCE_SIZE[1], SCALE):
        for x in range(0, SOURCE_SIZE[0], SCALE):
            value = alpha_bytes[y * source_width + x]
            for dy in range(SCALE):
                row = (y + dy) * source_width + x
                if alpha_bytes[row:row + SCALE] != bytes([value]) * SCALE:
                    raise ValueError(f"{path}: nonuniform 8x8 block at {x},{y}")
            pixels.append(value == 255)
    return pixels


def convert(path):
    pixels = parse_source(path)
    data = bytes(
        sum(pixels[(page * 8 + bit) * WIDTH + x] << bit for bit in range(8))
        for page in range(HEIGHT // 8)
        for x in range(WIDTH)
    )
    if len(data) != 1024 or not any(data):
        raise ValueError(f"{path}: expected a nonblank 1024-byte bitmap")
    for y in range(HEIGHT):
        for x in range(WIDTH):
            packed = (data[(y // 8) * WIDTH + x] >> (y % 8)) & 1
            if packed != pixels[y * WIDTH + x]:
                raise ValueError(f"{path}: packing round-trip failed at {x},{y}")
    return data


def parse_stage_source(path):
    """Read and validate a 1:1 RGBA alpha-mask stage map (103x64)."""
    try:
        with Image.open(path) as image:
            image.load()
            if image.format != "PNG":
                raise ValueError(f"{path}: expected PNG input")
            if image.mode != "RGBA":
                raise ValueError(f"{path}: expected RGBA, got {image.mode}")
            if image.size != (STAGE_W, STAGE_H):
                raise ValueError(
                    f"{path}: expected {STAGE_W}x{STAGE_H}, got {image.width}x{image.height}"
                )
            red, green, blue, alpha = image.split()
            if any(red.tobytes()) or any(green.tobytes()) or any(blue.tobytes()):
                raise ValueError(f"{path}: expected black RGB channels")
            alpha_bytes = alpha.tobytes()
    except (OSError, SyntaxError) as error:
        raise ValueError(f"{path}: invalid PNG: {error}") from error

    if set(alpha_bytes) - {0, 255}:
        raise ValueError(f"{path}: alpha must contain only 0 or 255")
    return [value == 255 for value in alpha_bytes]


def convert_stage(path):
    pixels = parse_stage_source(path)
    data = bytes(
        sum(pixels[(page * 8 + bit) * STAGE_W + x] << bit for bit in range(8))
        for page in range(STAGE_H // 8)
        for x in range(STAGE_W)
    )
    if len(data) != (STAGE_W * STAGE_H // 8) or not any(data):
        raise ValueError(f"{path}: expected a nonblank {STAGE_W}x{STAGE_H} bitmap")
    for y in range(STAGE_H):
        for x in range(STAGE_W):
            packed = (data[(y // 8) * STAGE_W + x] >> (y % 8)) & 1
            if packed != pixels[y * STAGE_W + x]:
                raise ValueError(f"{path}: packing round-trip failed at {x},{y}")
    return data


def decompose_stage(pixels):
    """Split a stage mask into (rects, fills) that reproduce it exactly.

    rects: list of (x0, y0, x1, y1) inclusive axis-aligned boxes, greedy maximal
    rectangles with area >= RECT_MIN_AREA.
    fills: list of (x, y, length) horizontal runs, length >= 1, for leftovers.
    """
    width, height = STAGE_W, STAGE_H
    remaining = bytearray(pixels)
    rects = []
    while True:
        heights = [0] * width
        best = None
        best_area = 0
        for y in range(height):
            for x in range(width):
                if remaining[y * width + x]:
                    heights[x] += 1
                else:
                    heights[x] = 0
            stack = []
            for x in range(width + 1):
                h = heights[x] if x < width else 0
                start = x
                while stack and stack[-1][1] > h:
                    x0, bar_h = stack.pop()
                    area = (x - x0) * bar_h
                    if area >= RECT_MIN_AREA and area > best_area:
                        best_area = area
                        best = (x0, y - bar_h + 1, x - 1, y)
                if stack and stack[-1][1] == h:
                    continue
                stack.append((start, h))
        if best is None:
            break
        x0, y0, x1, y1 = best
        rects.append(best)
        for yy in range(y0, y1 + 1):
            for xx in range(x0, x1 + 1):
                remaining[yy * width + xx] = 0
    fills = []
    for y in range(height):
        x = 0
        while x < width:
            if remaining[y * width + x]:
                x2 = x
                while x2 + 1 < width and remaining[y * width + x2 + 1]:
                    x2 += 1
                fills.append((x, y, x2 - x + 1))
                x = x2 + 1
            else:
                x += 1
    return rects, fills


def render_layout(rects, fills, width=STAGE_W, height=STAGE_H):
    """Reconstruct a mask from layout primitives (rects + fills)."""
    mask = bytearray(width * height)
    for x0, y0, x1, y1 in rects:
        for yy in range(y0, y1 + 1):
            for xx in range(x0, x1 + 1):
                mask[yy * width + xx] = 1
    for x, y, length in fills:
        for xx in range(x, x + length):
            mask[y * width + xx] = 1
    return mask


class _Writer:
    """MSB-first bit accumulator."""

    def __init__(self):
        self.parts = []
        self.acc = 0
        self.nbits = 0

    def put(self, bits, count):
        self.acc = (self.acc << count) | bits
        self.nbits += count
        while self.nbits >= 8:
            self.nbits -= 8
            self.parts.append((self.acc >> self.nbits) & 0xFF)
        if self.nbits:
            self.acc &= (1 << self.nbits) - 1
        else:
            self.acc = 0

    def finish(self):
        if self.nbits:
            self.parts.append((self.acc << (8 - self.nbits)) & 0xFF)
        return bytes(self.parts)


def lzss_encode(screens):
    """Encode the 1024-byte screens into one LZSS stream.

    Screens are encoded in order; each screen may reference any of the previous
    screens' bytes through the 1 KiB window. Matches never cross a screen
    boundary (a token's length is capped by the end of the current screen) so
    the stream can be decoded screen by screen into a single 1024-byte buffer.
    """
    blob = b"".join(screens)
    writer = _Writer()
    for screen_index, screen in enumerate(screens):
        start = screen_index * 1024
        end = start + len(screen)
        i = start
        while i < end:
            best_len, best_offset = 0, 0
            candidates = 0
            max_len = min(LMAX, end - i)
            for o in range(max(0, i - WINDOW), i):
                candidates += 1
                match = 0
                while match < max_len and blob[o + match] == blob[i + match]:
                    match += 1
                if match > best_len:
                    best_len, best_offset = match, i - o
                if match == max_len:
                    break
                if candidates > 2048:
                    break
            if best_len >= LEN_MIN:
                writer.put(0, 1)
                writer.put(best_len - LEN_MIN, LEN_BITS)
                writer.put(best_offset - 1, OFF_BITS)
                i += best_len
            else:
                writer.put(1, 1)
                writer.put(blob[i], 8)
                i += 1
    return writer.finish()


class _Reader:
    """MSB-first bit reader over a padded byte stream."""

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.acc = 0
        self.nbits = 0

    def get(self, count):
        while self.nbits < count:
            self.acc = (self.acc << 8) | self.data[self.pos]
            self.pos += 1
            self.nbits += 8
        self.nbits -= count
        return (self.acc >> self.nbits) & ((1 << count) - 1)


def lzss_decode_screens(stream, count):
    """Decode `count` 1024-byte screens sequentially into a shared buffer."""
    reader = _Reader(stream)
    buffer = bytearray(WINDOW)
    result = []
    for _ in range(count):
        op = 0
        while op < WINDOW:
            if reader.get(1):
                buffer[op] = reader.get(8)
                op += 1
            else:
                length = reader.get(LEN_BITS) + LEN_MIN
                offset = reader.get(OFF_BITS) + 1
                for _ in range(length):
                    buffer[op] = buffer[(op - offset) & (WINDOW - 1)]
                    op += 1
        result.append(bytes(buffer))
    return result


def common_preamble():
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "#if defined(__AVR__)",
        "#include <avr/pgmspace.h>",
        "#elif !defined(PROGMEM)",
        "#define PROGMEM",
        "#endif",
        "",
    ]
    return lines


def menu_screens_header(blob, screens):
    crcs = [zlib.crc32(packed) & 0xFFFFFFFF for packed in screens]
    rows = ["    " + ", ".join(f"0x{value:02x}" for value in blob[i:i + 16]) + ","
            for i in range(0, len(blob), 16)]
    crc_rows = ["    " + ", ".join(f"0x{value:08x}" for value in crcs[i:i + 4]) + ","
                for i in range(0, len(crcs), 4)]
    lines = common_preamble()
    lines += [
        "// Generated by tools/convert_assets.py from the eight menu PNG exports.",
        "// LZSS stream of the 128x64 menu screens, page-column packed (1024 bytes",
        "// each). Decode screens 0..N sequentially into the 1 KiB framebuffer;",
        "// the buffer acts as the match window ((pos - off) & 1023). Tokens:",
        "// MSB-first: '1' + 8-bit literal; '0' + 5-bit (len-3) + 10-bit (off-1).",
        "// Trailing zero bytes pad the final token's read-ahead.",
        f"#define MENU_SCREEN_COUNT {len(NAMES)}",
        "#define MENU_SCREEN_BYTES 1024",
        f"const uint8_t menu_screens[] PROGMEM = {{",
    ] + rows + [
        "};",
        "",
        "#if !defined(__AVR__)",
        "// Per-screen CRC32 of the packed 1024 bytes; host tests cross-check the",
        "// decoder against tools/convert_assets.py.",
        f"const uint32_t menu_screen_crcs[{len(NAMES)}] = {{",
    ] + crc_rows + [
        "};",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def stage_layouts_header(stages):
    lines = common_preamble()
    lines += [
        "// Generated by tools/convert_assets.py from the stage PNG exports.",
        "// Stage collision maps as layout markup (exactly reproducible): solid",
        "// rectangles (x0,y0,x1,y1 inclusive) plus horizontal fill runs (x,y,len)",
        "// sorted by (y, x) ascending, all raw bytes in PROGMEM. A pixel is a wall",
        "// iff a rect contains it or a fill run draws it. Stage 1 (index 0) has no",
        "// primitives.",
        "",
    ]
    for name, rects, fills in stages:
        symbol = name + "_layout"
        rect_rows = ["    " + ", ".join(f"0x{value:02x}" for value in rects[i:i + 16]) + ","
                     for i in range(0, len(rects), 16)]
        lines += [
            f"const uint8_t {symbol}[] PROGMEM = {{",
        ] + rect_rows + ["};"]
        lines.append(f'static_assert(sizeof({symbol}) % 4 == 0, "Rect layout must be 4-byte");')
        lines.append("")
        fills_symbol = name + "_fill"
        fill_rows = ["    " + ", ".join(f"0x{value:02x}" for value in fills[i:i + 16]) + ","
                     for i in range(0, len(fills), 16)]
        lines += [
            f"const uint8_t {fills_symbol}[] PROGMEM = {{",
        ] + fill_rows + ["};"]
        lines.append(
            f'static_assert(sizeof({fills_symbol}) % 3 == 0, "Fill runs must be 3-byte");')
        lines.append("")
    return "\n".join(lines)


def write_preview(path, mask, width, height):
    pixels = bytes(255 if mask[y * width + x] else 0
                   for y in range(height) for x in range(width))
    Image.frombytes("L", (width, height), pixels).save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify committed outputs without writing")
    parser.add_argument("--inspect", action="store_true",
                        help="report conversion statistics without writing")
    parser.add_argument("--preview-dir", type=Path,
                        help="write reconstructed PNG previews")
    args = parser.parse_args()

    menus = []
    for name in NAMES:
        data = convert(ROOT / "assets" / (name + ".PNG"))
        menus.append(data)
        print(f"{name}: {sum(b.bit_count() for b in data)} lit pixels; "
              f"{len(data)} bytes raw")
    blob = lzss_encode(menus)
    decoded = lzss_decode_screens(blob + bytes(BLOB_PAD), len(menus))
    if decoded != menus:
        raise ValueError("LZSS round-trip failed")
    menu_text = menu_screens_header(blob + bytes(BLOB_PAD), menus)

    stages = []
    stage_total = 0
    for name, source in STAGE_SOURCES.items():
        mask = parse_stage_source(ROOT / "assets" / source)
        rects, fills = decompose_stage(mask)
        if render_layout(rects, fills) != bytes(mask):
            raise ValueError(f"{source}: layout does not reproduce the mask")
        if any(b[1] < a[1] or (b[1] == a[1] and b[0] < a[0])
               for a, b in zip(fills, fills[1:])):
            raise ValueError(f"{source}: fills must be sorted by (y, x)")
        rect_flat = [v for rect in rects for v in rect]
        fill_flat = [v for run in fills for v in run]
        stages.append((name, tuple(rect_flat), tuple(fill_flat)))
        cost = len(rect_flat) + len(fill_flat)
        stage_total += cost
        print(f"{source}: {sum(mask)} lit px; {len(rects)} rects "
              f"({len(rect_flat)} B) + {len(fills)} fills ({len(fill_flat)} B); "
              f"{cost} B total")
    stage_text = stage_layouts_header(stages)

    raw_menus = sum(len(m) for m in menus)
    print(f"Menu screens: {raw_menus} raw B -> {len(blob)} B LZSS blob "
          f"({len(blob) / raw_menus * 100:.0f}%); stage layouts {stage_total} B")
    print(f"Total generated payload: {len(blob) + len(stage_text):6d} B flash "
          f"(blob {len(blob)} B, stage headers {len(stage_text)} B)")

    outputs = {
        ROOT / "src" / "assets" / "menu_screens.h": menu_text,
        ROOT / "src" / "assets" / "stage_layouts.h": stage_text,
    }
    for destination, text in outputs.items():
        if args.check:
            if not destination.exists() or destination.read_text(encoding="ascii") != text:
                raise ValueError(f"{destination}: missing or stale; rerun converter")
        elif not args.inspect:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(text, encoding="ascii")

    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        for index, name in enumerate(NAMES):
            packed = menus[index]
            mask = bytes(
                1 if packed[(y // 8) * WIDTH + x] & (1 << (y % 8)) else 0
                for y in range(HEIGHT) for x in range(WIDTH)
            )
            write_preview(args.preview_dir / (name + ".png"), mask, WIDTH, HEIGHT)
        for name, rects, fills in stages:
            mask = render_layout(
                [tuple(rects[i:i + 4]) for i in range(0, len(rects), 4)],
                [tuple(fills[i:i + 3]) for i in range(0, len(fills), 3)],
            )
            write_preview(args.preview_dir / (name + "_map.png"), mask, STAGE_W, STAGE_H)

    if args.check:
        print("menu_screens.h and stage_layouts.h verified.")


if __name__ == "__main__":
    main()