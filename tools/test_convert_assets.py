#!/usr/bin/env python3
"""Exercise alpha-mask validation, stage layout markup and LZSS framing."""

from pathlib import Path
import random
import tempfile
import unittest

from PIL import Image

from convert_assets import (NAMES, RECT_MIN_AREA, STAGE_H, STAGE_SOURCES,
                            STAGE_W, convert, convert_stage, decompose_stage,
                            lzss_decode_screens, lzss_encode, page_pack,
                            parse_source, parse_stage_source, render_layout,
                            row_run_pack)


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "sample.PNG"

    def write_source(self, alpha=None, size=(1024, 512), mode="RGBA", rgb=(0, 0, 0)):
        image = Image.new(mode, size, rgb + (0,) if mode == "RGBA" else rgb)
        if alpha is not None:
            image.putalpha(Image.frombytes("L", size, bytes(alpha)))
        image.save(self.path)

    def test_alpha_and_page_order(self):
        alpha = bytearray(1024 * 512)
        for x, y in ((0, 0), (1, 7), (2, 8), (127, 63), (10, 10)):
            for dy in range(8):
                start = (y * 8 + dy) * 1024 + x * 8
                alpha[start:start + 8] = b"\xff" * 8
        self.write_source(alpha)
        expected = bytearray(1024)
        expected[0] = 0x01
        expected[1] = 0x80
        expected[130] = 0x01
        expected[138] = 0x04
        expected[1023] = 0x80
        data = convert(self.path)
        self.assertEqual(data, bytes(expected))

    def test_bad_dimensions(self):
        self.write_source(size=(128, 64))
        with self.assertRaisesRegex(ValueError, "expected 1024x512"):
            parse_source(self.path)

    def test_requires_rgba(self):
        self.write_source(size=(1024, 512), mode="RGB")
        with self.assertRaisesRegex(ValueError, "expected RGBA"):
            parse_source(self.path)

    def test_requires_black_rgb(self):
        self.write_source(rgb=(1, 0, 0))
        with self.assertRaisesRegex(ValueError, "black RGB"):
            parse_source(self.path)

    def test_rejects_partial_alpha(self):
        alpha = bytearray(1024 * 512)
        alpha[0] = 128
        self.write_source(alpha)
        with self.assertRaisesRegex(ValueError, "only 0 or 255"):
            parse_source(self.path)

    def test_rejects_nonuniform_block(self):
        alpha = bytearray(1024 * 512)
        alpha[0] = 255
        self.write_source(alpha)
        with self.assertRaisesRegex(ValueError, "nonuniform 8x8 block at 0,0"):
            parse_source(self.path)

    def test_rejects_blank_bitmap(self):
        self.write_source()
        with self.assertRaisesRegex(ValueError, "nonblank 1024-byte"):
            convert(self.path)


class StageConversionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "stage_2.png"

    def write_source(self, alpha=None, size=(STAGE_W, STAGE_H), mode="RGBA",
                     rgb=(0, 0, 0)):
        image = Image.new(mode, size, rgb)
        if alpha is None:
            alpha = bytes(size[0] * size[1])
        image.putalpha(Image.frombytes("L", size, bytes(alpha)))
        image.save(self.path)

    def test_packs_1x1_masks(self):
        alpha = bytearray(STAGE_W * STAGE_H)
        alpha[0] = 255
        alpha[10 * STAGE_W + 5] = 255
        alpha[(1 * 8 + 3) * STAGE_W + 99] = 255
        self.write_source(alpha)
        data = convert_stage(self.path)
        self.assertEqual(len(data), STAGE_W * STAGE_H // 8)
        self.assertEqual(data[0] & 1, 1)          # (0,0)
        self.assertEqual((data[STAGE_W + 5] >> 2) & 1, 1)  # (5,10): page 1, bit 2
        self.assertEqual(data[STAGE_W + 99] & 0x08, 0x08)  # (99, 11): page 1, bit 3

    def test_page_pack_equals_convert_stage(self):
        alpha = bytearray(STAGE_W * STAGE_H)
        for index in (0, 37, 51 * STAGE_W + 7, STAGE_W * STAGE_H - 1):
            alpha[index] = 255
        self.write_source(alpha)
        mask = parse_stage_source(self.path)
        self.assertEqual(page_pack(mask), convert_stage(self.path))

    def test_page_pack_round_trip(self):
        rng = random.Random(7)
        mask = [bool(rng.randrange(2)) for _ in range(STAGE_W * STAGE_H)]
        data = page_pack(mask)
        self.assertEqual(len(data), STAGE_W * (STAGE_H // 8))
        for y in range(STAGE_H):
            for x in range(STAGE_W):
                self.assertEqual(bool((data[(y // 8) * STAGE_W + x] >> (y % 8)) & 1),
                                 mask[y * STAGE_W + x])

    def test_row_runs_round_trip(self):
        rng = random.Random(19)
        mask = [bool(rng.randrange(5) == 0) for _ in range(STAGE_W * STAGE_H)]
        data, blocks = row_run_pack(mask)
        decoded = [False] * len(mask)
        for y in range(STAGE_H):
            pos = blocks[y // 8]
            for _ in range(y & 7):
                pos += 1 + data[pos] * 2
            count = data[pos]
            pos += 1
            for _ in range(count):
                start, length = data[pos:pos + 2]
                pos += 2
                for x in range(start, start + length):
                    decoded[y * STAGE_W + x] = True
        self.assertEqual(decoded, mask)

    def test_rejects_overwide_source(self):
        for name, size in (("wrong size", (STAGE_W + 1, STAGE_H)),
                           ("wrong height", (STAGE_W, STAGE_H + 1))):
            with self.subTest(name=name):
                self.write_source(size=size)
                with self.assertRaisesRegex(ValueError, "expected 103x64"):
                    parse_stage_source(self.path)

    def test_rejects_partial_alpha(self):
        alpha = bytearray(STAGE_W * STAGE_H)
        alpha[0] = 128
        self.write_source(alpha)
        with self.assertRaisesRegex(ValueError, "only 0 or 255"):
            parse_stage_source(self.path)

    def test_rejects_blank_stage(self):
        self.write_source()
        with self.assertRaisesRegex(ValueError, "nonblank 103x64"):
            convert_stage(self.path)

    def test_sources_exist(self):
        root = Path(__file__).resolve().parents[1]
        for source in STAGE_SOURCES.values():
            self.assertTrue((root / "assets" / source).is_file())


class LayoutTests(unittest.TestCase):
    def random_mask(self, rng, width=STAGE_W, height=STAGE_H):
        return [bool(rng.randrange(2)) for _ in range(width * height)]

    def test_decompose_is_exact(self):
        for seed in range(6):
            with self.subTest(seed=seed):
                rng = random.Random(seed)
                mask = self.random_mask(rng)
                rects, fills = decompose_stage(mask)
                rebuilt = render_layout(rects, fills)
                self.assertEqual(bytes(rebuilt), bytes(mask))
                leftovers = sum(length for _, _, length in fills)
                self.assertLess(leftovers, len(mask))
                for x0, y0, x1, y1 in rects:
                    self.assertGreaterEqual((x1 - x0 + 1) * (y1 - y0 + 1),
                                            RECT_MIN_AREA)
                for _, _, length in fills:
                    self.assertGreaterEqual(length, 1)
                # Штрихи отсортированы по (y, x) — ищем их двоичным поиском.
                for (ax, ay, _), (bx, by, _) in zip(fills, fills[1:]):
                    self.assertTrue(by > ay or (by == ay and bx >= ax))

    def test_decompose_real_maps_exact(self):
        root = Path(__file__).resolve().parents[1]
        for source in STAGE_SOURCES.values():
            with self.subTest(source=source):
                mask = parse_stage_source(root / "assets" / source)
                rects, fills = decompose_stage(mask)
                self.assertEqual(bytes(render_layout(rects, fills)),
                                 bytes(mask))

    def test_decompose_empty(self):
        rects, fills = decompose_stage([False] * (STAGE_W * STAGE_H))
        self.assertEqual(rects, [])
        self.assertEqual(fills, [])

    def test_decompose_keeps_solid_box(self):
        mask = [False] * (STAGE_W * STAGE_H)
        for y in range(10, 20):
            for x in range(30, 50):
                mask[y * STAGE_W + x] = True
        rects, fills = decompose_stage(mask)
        reconstructed = bytes(render_layout(rects, fills))
        expected = bytes(mask)
        self.assertEqual(reconstructed, expected)
        self.assertEqual(fills, [])


class LzssTests(unittest.TestCase):
    def screens(self, seed, count=8):
        rng = random.Random(seed)
        return [bytes(rng.randrange(256) for _ in range(1024)) for _ in range(count)]

    def test_round_trip(self):
        screens = self.screens(1)
        stream = lzss_encode(screens)
        self.assertEqual(lzss_decode_screens(stream + bytes(8), len(screens)),
                         screens)

    def test_deterministic(self):
        screens = self.screens(2)
        self.assertEqual(lzss_encode(screens), lzss_encode(screens))

    def test_reflects_input(self):
        screens = self.screens(3)
        stream_a = lzss_encode(screens)
        screens[0] = bytes((b ^ 0xFF for b in screens[0]))
        stream_b = lzss_encode(screens)
        self.assertNotEqual(stream_a, stream_b)

    def test_sequential_window(self):
        screens = self.screens(4)
        stream = lzss_encode(screens)
        for i, screen in enumerate(screens):
            decoded = lzss_decode_screens(stream + bytes(8), i + 1)
            self.assertEqual(decoded[-1], screen)
        # Повторный проход даёт тот же результат (буфер-окно полностью пройден).
        self.assertEqual(lzss_decode_screens(stream + bytes(8), len(screens)),
                         screens)

    def test_menu_screens_round_trip(self):
        root = Path(__file__).resolve().parents[1]
        screens = [convert(root / "assets" / (name + ".PNG")) for name in NAMES]
        stream = lzss_encode(screens)
        self.assertLess(len(stream), len(b"".join(screens)))
        decoded = lzss_decode_screens(stream + bytes(8), len(screens))
        self.assertEqual(decoded, screens)


if __name__ == "__main__":
    unittest.main()
