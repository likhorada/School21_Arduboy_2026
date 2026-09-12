#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/arena.h"
#pragma once

#include "config.h"

namespace gc {

struct Obstacle {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
};

constexpr uint8_t OBSTACLE_COUNT = 4;

// Читаем один прямоугольник из flash, не копируя всю карту в SRAM. Размеры в пикселях.
Obstacle readObstacle(uint8_t index);

// Возвращаем координату в [0, extent); вход должен быть в [-extent, 2*extent).
int16_t wrapCoordinate(int16_t coordinate, int16_t extent);

// Проверяем весь прямоугольник игрока, в том числе часть за краем арены.
// x/y: левый верхний угол внутри арены, в единицах 1/16 пикселя.
bool playerBlocked(int16_t x, int16_t y);

// Кратчайшее смещение с учётом краёв: от 126 до 2 при ширине 128 получается +4.
// На ровно половине ширины сохраняем знак (to - from); обе точки внутри арены.
int16_t shortestDelta(int16_t from, int16_t to, int16_t extent);

// Пересечение всего отрезка с прямоугольником, включая переходы через края.
// x/y и dx/dy в 1/16 пикселя, box в пикселях. Начальные позиции внутри арены.
// Смещение и размер прямоугольника по каждой оси не больше половины арены.
// Это проверка короткого пути, не луча на несколько оборотов вокруг поля.
bool segmentHitsBox(int16_t x, int16_t y, int16_t dx, int16_t dy,
                    const Obstacle& box);

// Проверяем отрезок против всех стен. Прицеливание и пули используют одну геометрию.
bool shotBlocked(int16_t x, int16_t y, int16_t dx, int16_t dy);

} // пространство имён gc
