#!/usr/bin/env python3
"""Exercise alpha-mask validation and Arduboy page packing."""

from pathlib import Path
import tempfile
import unittest

from PIL import Image

from convert_assets import convert, header, parse_source


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
        text = header("sample", data)
        self.assertIn("const uint8_t sample_bitmap[] PROGMEM", text)
        self.assertIn("#if defined(__AVR__)", text)

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


if __name__ == "__main__":
    unittest.main()
