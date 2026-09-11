#include "arena.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

namespace gc {
namespace {

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

// A normalized body can straddle only the far edge; its second copy is at -extent.
bool overlapsAxis(int16_t position, uint8_t start, uint8_t length, int16_t extent) {
    const int16_t low = start * FIXED_ONE;
    const int16_t high = low + length * FIXED_ONE;
    const int16_t size = PLAYER_SIZE * FIXED_ONE;
    return (position < high && position + size > low) ||
           (position - extent < high && position - extent + size > low);
}

} // namespace

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

int16_t wrapCoordinate(int16_t coordinate, int16_t extent) {
    if (coordinate < 0) {
        coordinate += extent;
    } else if (coordinate >= extent) {
        coordinate -= extent;
    }
    return coordinate;
}

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

} // namespace gc
