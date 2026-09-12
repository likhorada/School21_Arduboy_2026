#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#define F(text) (text)
constexpr uint8_t WHITE = 1, BLACK = 0;

// Host-only drawing spy: checks bounds and text layout, not the hardware font.
class Arduboy2 {
public:
    uint8_t pixels[64][128] = {};
    char text[8][22] = {};
    bool occupied[64][128] = {};
    int16_t cursorX = 0, cursorY = 0;
    void clear() {
        std::memset(pixels, 0, sizeof(pixels));
        std::memset(text, 0, sizeof(text));
        std::memset(occupied, 0, sizeof(occupied));
    }
    void setTextWrap(bool) {}
    void setCursor(int16_t x, int16_t y) { cursorX = x; cursorY = y; }
    void drawPixel(int16_t x, int16_t y, uint8_t color = WHITE) {
        assert(x >= 0 && x < 128 && y >= 0 && y < 64);
        pixels[y][x] = color;
    }
    void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE) {
        for (uint8_t yy = 0; yy < h; ++yy)
            for (uint8_t xx = 0; xx < w; ++xx) drawPixel(x + xx, y + yy, color);
    }
    void drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color = WHITE) {
        for (uint8_t xx = 0; xx < w; ++xx) drawPixel(x + xx, y, color);
    }
    void drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE) {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        for (uint8_t yy = 0; yy < h; ++yy) {
            drawPixel(x, y + yy, color);
            drawPixel(x + w - 1, y + yy, color);
        }
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap,
                    uint8_t w, uint8_t h, uint8_t color = WHITE) {
        for (uint8_t yy = 0; yy < h; ++yy)
            for (uint8_t xx = 0; xx < w; ++xx)
                if (pgm_read_byte(bitmap + (yy / 8) * w + xx) & (1 << (yy & 7)))
                    drawPixel(x + xx, y + yy, color);
    }
    void print(char c) {
        assert(cursorX >= 0 && cursorX + 6 <= 128 && cursorY >= 0 && cursorY + 8 <= 64);
        for (int y = cursorY; y < cursorY + 8; ++y)
            for (int x = cursorX; x < cursorX + 6; ++x) {
                assert(!occupied[y][x]);
                occupied[y][x] = true;
            }
        text[cursorY / 8][cursorX / 6] = c;
        cursorX += 6;
    }
    void print(const char* s) { while (*s) print(*s++); }
    void print(unsigned int n) {
        char buffer[12];
        std::snprintf(buffer, sizeof(buffer), "%u", n);
        print(buffer);
    }
    void print(int n) { assert(n >= 0); print(static_cast<unsigned int>(n)); }
};
