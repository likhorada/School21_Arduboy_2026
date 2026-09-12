#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/arena.cpp"
#include "arena.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

namespace gc {
namespace {

// Препятствия на арене. На AVR хранятся в flash (PROGMEM), не в SRAM.
const Obstacle obstacles[]
#ifdef __AVR__
    PROGMEM
#endif
    = {
        {3, 14, 10, 10},
        {37, 8, 16, 8},
        {82, 32, 16, 10},
        {108, 12, 8, 14},
    };

static_assert(sizeof(obstacles) / sizeof(obstacles[0]) == OBSTACLE_COUNT,
              "Obstacle count does not match the map");

// Проверка пересечения по одной оси (X или Y) с учётом перехода через край.
// Игрок размером PLAYER_SIZE может быть на стыке границы арены.
bool overlapsAxis(int16_t position, uint8_t start, uint8_t length, int16_t extent) {
    const int16_t low = start * FIXED_ONE;
    const int16_t high = low + length * FIXED_ONE;
    const int16_t size = PLAYER_SIZE * FIXED_ONE;
    return (position < high && position + size > low) ||
           (position - extent < high && position - extent + size > low);
}

} // внутренние функции модуля

// На AVR читаем из flash, в тестах на компьютере из обычной памяти.
Obstacle readObstacle(uint8_t index) {
    if (index >= OBSTACLE_COUNT) {
        return {};
    }
#ifdef __AVR__
    Obstacle result;
    memcpy_P(&result, &obstacles[index], sizeof(result));
    return result;
#else
    return obstacles[index];
#endif
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

// Проверяем, пересекается ли игрок с препятствиями на позиции (x, y).
bool playerBlocked(int16_t x, int16_t y) {
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        if (overlapsAxis(x, obstacle.x, obstacle.width, ARENA_WIDTH_FIXED) &&
            overlapsAxis(y, obstacle.y, obstacle.height, ARENA_HEIGHT_FIXED)) {
            return true;
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

// Проверяем, попадает ли отрезок (x,y)→(x+dx, y+dy) в препятствия.
bool shotBlocked(int16_t x, int16_t y, int16_t dx, int16_t dy) {
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        if (segmentHitsBox(x, y, dx, dy, readObstacle(i))) {
            return true;
        }
    }
    return false;
}

} // пространство имён gc
