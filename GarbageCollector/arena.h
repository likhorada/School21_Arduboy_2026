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

// Read one immutable pixel-space rectangle without copying the map into SRAM.
Obstacle readObstacle(uint8_t index);

// Normalize a coordinate at most one period outside [0, extent).
int16_t wrapCoordinate(int16_t coordinate, int16_t extent);

// Test the player's full AABB, including its copies across either seam.
// x/y are normalized top-left coordinates in 1/16 pixel units.
bool playerBlocked(int16_t x, int16_t y);

} // namespace gc
