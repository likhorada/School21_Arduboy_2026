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

// Пиксель стены из попиксельной битовой маски стейджа: page-column упаковка
// (один байт на (страницу, колонку)), поэтому запрос — одно чтение + бит-тест.
// Стейдж 1 (индекс 0) пустой и маски не имеет.
bool stageMapPixel(const uint8_t* map, uint8_t x, uint8_t y) {
  const uint8_t byte = pgm_read_byte(&map[(y >> 3) * ARENA_WIDTH + x]);
  return (byte & (1u << (y & 7))) != 0;
}

const uint8_t* stageWallMap(uint8_t stage) {
  switch (stage) {
  case 1:
    return stage2_map;
  case 2:
    return stage3_map;
  default:
    return nullptr;
  }
}

// Пиксель стены c поворотом через край: fixed-координата оборачивается и
// проверяется через битовую маску стейджа.
bool wallAtFixed(uint8_t stage, int16_t fixedX, int16_t fixedY) {
  const uint8_t* map = stageWallMap(stage);
  if (map == nullptr) {
    return false;
  }
  const uint8_t x = wrapCoordinate(fixedX, ARENA_WIDTH_FIXED) / FIXED_ONE;
  const uint8_t y = wrapCoordinate(fixedY, ARENA_HEIGHT_FIXED) / FIXED_ONE;
  return stageMapPixel(map, x, y);
}

} // внутренние функции модуля

bool stageWallPixel(uint8_t stage, uint8_t x, uint8_t y) {
  if (x >= ARENA_WIDTH || y >= ARENA_HEIGHT) {
    return false;
  }
  const uint8_t* map = stageWallMap(stage);
  return map != nullptr && stageMapPixel(map, x, y);
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