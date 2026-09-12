#include "arena.h"

#include "assets/stage_layouts.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void const *const *)(addr))
#endif
#endif

namespace gc {
namespace {

// Пиксель стены из разметки стейджа: прямоугольники + горизонтальные штрихи.
// Штрихи отсортированы по (y, x), поэтому строку ищем двоичным поиском.
bool wallFromLayout(const uint8_t* rects, uint16_t rectCount,
                    const uint8_t* fills, uint16_t fillCount,
                    uint8_t x, uint8_t y) {
  for (uint16_t i = 0; i < rectCount; ++i) {
    const uint8_t x0 = pgm_read_byte(&rects[i * 4]);
    const uint8_t y0 = pgm_read_byte(&rects[i * 4 + 1]);
    const uint8_t x1 = pgm_read_byte(&rects[i * 4 + 2]);
    const uint8_t y1 = pgm_read_byte(&rects[i * 4 + 3]);
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
      return true;
    }
  }
  uint16_t lo = 0;
  uint16_t hi = fillCount;
  while (lo < hi) {
    const uint16_t mid = (lo + hi) / 2;
    if (pgm_read_byte(&fills[mid * 3 + 1]) < y) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  for (uint16_t i = lo; i < fillCount; ++i) {
    const uint8_t fy = pgm_read_byte(&fills[i * 3 + 1]);
    if (fy != y) {
      break;
    }
    const uint8_t fx = pgm_read_byte(&fills[i * 3]);
    const uint8_t length = pgm_read_byte(&fills[i * 3 + 2]);
    if (x >= fx && x < fx + length) {
      return true;
    }
  }
  return false;
}

// Разметка (прямоугольники + штрихи) для стейджа; пустое поле — false.
// Счётчики — раскрытые sizeof, без ОЗУ.
bool stageWallFromLayout(uint8_t stage, uint8_t x, uint8_t y) {
  switch (stage) {
  case 1:
    return wallFromLayout(stage2_layout, sizeof(stage2_layout) / 4,
                          stage2_fill, sizeof(stage2_fill) / 3, x, y);
  case 2:
    return wallFromLayout(stage3_layout, sizeof(stage3_layout) / 4,
                          stage3_fill, sizeof(stage3_fill) / 3, x, y);
  default:
    return false;
  }
}

// Пиксель стены c поворотом через край: fixed-координата оборачивается и
// проверяется через разметку стейджа.
bool wallAtFixed(uint8_t stage, int16_t fixedX, int16_t fixedY) {
  const uint16_t x = wrapCoordinate(fixedX, ARENA_WIDTH_FIXED) / FIXED_ONE;
  const uint16_t y = wrapCoordinate(fixedY, ARENA_HEIGHT_FIXED) / FIXED_ONE;
  return stageWallFromLayout(stage, x, y);
}

} // внутренние функции модуля

bool stageWallPixel(uint8_t stage, uint8_t x, uint8_t y) {
  if (x >= ARENA_WIDTH || y >= ARENA_HEIGHT) {
    return false;
  }
  return stageWallFromLayout(stage, x, y);
}

// Оборачиваем координату через границу арены (тор).
// Если вышли за край — переносим на противоположную сторону.
int16_t wrapCoordinate(int16_t coordinate, int16_t extent) {
  if (coordinate < 0) {
    coordinate += extent;
  } else if (coordinate >= extent) {
    coordinate -= extent;
  }
  return coordinate;
}

// Проверяем, пересекается ли игрок со стенами стейджа на позиции (x, y).
// Бокс [x, x + PLAYER_SIZE*FIXED_ONE) проверяем по всем накрытым пикселям —
// те же инклюзивные границы, что и прямоугольная коллизия с долями пикселя.
bool playerBlocked(uint8_t stage, int16_t x, int16_t y) {
  const int16_t size = PLAYER_SIZE * FIXED_ONE;
  const int16_t startX = x / FIXED_ONE;
  const int16_t endX = (x + size - 1) / FIXED_ONE;
  const int16_t startY = y / FIXED_ONE;
  const int16_t endY = (y + size - 1) / FIXED_ONE;
  for (int16_t py = startY; py <= endY; ++py) {
    for (int16_t px = startX; px <= endX; ++px) {
      if (stageWallPixel(stage, wrapCoordinate(px, ARENA_WIDTH),
                         wrapCoordinate(py, ARENA_HEIGHT))) {
        return true;
      }
    }
  }
  return false;
}

// Кратчайшее смещение на зацикленной оси.
// Например, от 10 до 120 при ширине 128 получаем -18: ближе через левый край.
int16_t shortestDelta(int16_t from, int16_t to, int16_t extent) {
  int16_t delta = to - from;
  if (delta > extent / 2) {
    delta -= extent;
  } else if (delta < -extent / 2) {
    delta += extent;
  }
  return delta;
}

bool segmentHitsBox(int16_t x, int16_t y, int16_t dx, int16_t dy,
                    const Obstacle& box) {
  if (box.width == 0 || box.height == 0) {
    return false;
  }
  // НЕ оборачиваем конечную точку. Выстрел из x=127 в x=129 — короткий отрезок.
  // Если обернуть конец в x=1, получится линия через всю арену (неверно).
  const int16_t endX = x + dx;
  const int16_t endY = y + dy;
  const int16_t minX = x < endX ? x : endX;
  const int16_t maxX = x > endX ? x : endX;
  const int16_t minY = y < endY ? y : endY;
  const int16_t maxY = y > endY ? y : endY;

  // По каждой оси смещение и размер препятствия не больше половины арены.
  // Поэтому отрезок может пересечь только одну копию прямоугольника по оси.
  // Сдвигаем прямоугольник к лучу вместо проверки 9 копий.
  int16_t left = box.x * FIXED_ONE;
  int16_t right = left + box.width * FIXED_ONE - 1;
  if (maxX < left) {
    left -= ARENA_WIDTH_FIXED;
    right -= ARENA_WIDTH_FIXED;
  } else if (minX > right) {
    left += ARENA_WIDTH_FIXED;
    right += ARENA_WIDTH_FIXED;
  }
  if (maxX < left || minX > right) {
    return false;
  }
  int16_t top = box.y * FIXED_ONE;
  int16_t bottom = top + box.height * FIXED_ONE - 1;
  if (maxY < top) {
    top -= ARENA_HEIGHT_FIXED;
    bottom -= ARENA_HEIGHT_FIXED;
  } else if (minY > bottom) {
    top += ARENA_HEIGHT_FIXED;
    bottom += ARENA_HEIGHT_FIXED;
  }
  if (maxY < top || minY > bottom) {
    return false;
  }

  // Проверка: по какую сторону от линии каждый угол прямоугольника.
  // Если все 4 угла с одной стороны — линия мимо.
  // Иначе (вместе с проверками границ выше) — попадание.
  // ВАЖНО: расширяем до int32_t ДО умножения (на AVR int = 16 бит).
  const int32_t topLeft = static_cast<int32_t>(dx) * (top - y)
                          - static_cast<int32_t>(dy) * (left - x);
  const int32_t topRight = topLeft - static_cast<int32_t>(dy) * (right - left);
  const int32_t bottomLeft = topLeft + static_cast<int32_t>(dx) * (bottom - top);
  const int32_t bottomRight = topRight + static_cast<int32_t>(dx) * (bottom - top);
  const bool allPositive = topLeft > 0 && topRight > 0 && bottomLeft > 0 && bottomRight > 0;
  const bool allNegative = topLeft < 0 && topRight < 0 && bottomLeft < 0 && bottomRight < 0;
  return !allPositive && !allNegative;
}

// Проверяем, попадает ли отрезок (x,y)→(x+dx, y+dy) в пиксельные стены
// стейджа. Шагаем по доминантной оси с шагом в один пиксель (в единицах
// 1/16 пикселя), каждую точку оборачиваем через края арены.
bool shotBlocked(uint8_t stage, int16_t x, int16_t y, int16_t dx, int16_t dy) {
  const int32_t absDx = dx < 0 ? -static_cast<int32_t>(dx) : dx;
  const int32_t absDy = dy < 0 ? -static_cast<int32_t>(dy) : dy;
  const int32_t steps = absDx > absDy ? absDx : absDy;
  if (steps == 0) {
    return wallAtFixed(stage, x, y);
  }
  const uint16_t samples =
      static_cast<uint16_t>((steps + FIXED_ONE - 1) / FIXED_ONE);
  for (uint16_t i = 0; i <= samples; ++i) {
    const int16_t px =
        x + static_cast<int16_t>(static_cast<int32_t>(dx) * i / samples);
    const int16_t py =
        y + static_cast<int16_t>(static_cast<int32_t>(dy) * i / samples);
    if (wallAtFixed(stage, px, py)) {
      return true;
    }
  }
  return false;
}

} // пространство имён gc