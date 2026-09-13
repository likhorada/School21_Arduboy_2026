#pragma once

#include "game.h"

class Arduboy2;

namespace gc {

// Рисуем текущее состояние, не меняя игру и не создавая второй экранный буфер.
void renderGame(Arduboy2& arduboy, const Game& game);

// Байтовые писатели плоского кадрового буфера Arduboy (128×64, колонка = байт,
// бит = строка внутри 8-строчной страницы). Используются AVR-ветками рендера и
// тестами эквивалентности на хосте.
void fillFrameColumn(uint8_t* frame, int16_t x, int16_t y,
                     uint8_t columnBits, uint8_t height);
void renderPlayerFrame(uint8_t* frame, const Player& player);

// Колонко-ориентированный шрифт врагов (4 байта на глиф, бит r = строка r).
// Доступен тестам, чтобы сверять перенос [строка][колонка] с эталоном.
extern const uint8_t tinyFont[] PROGMEM;
extern const uint8_t bossFont[] PROGMEM;

constexpr uint8_t BOSS_TEXT_COLS = 3;
constexpr uint8_t BOSS_COL_PITCH = 5;
constexpr uint8_t BOSS_ROW_PITCH = 6;
constexpr uint8_t BOSS_VIS_W = BOSS_COL_PITCH * 3 - 1;
constexpr uint8_t BOSS_VIS_H = BOSS_ROW_PITCH * 3 - 1;

} // пространство имён gc
