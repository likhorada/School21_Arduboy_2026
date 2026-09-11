#pragma once

#include <stdint.h>

namespace gc {

constexpr uint8_t HUD_HEIGHT = 8;
constexpr uint8_t ARENA_WIDTH = 128;
constexpr uint8_t ARENA_HEIGHT = 64 - HUD_HEIGHT;
constexpr uint8_t FIXED_ONE = 16;
constexpr int16_t ARENA_WIDTH_FIXED = ARENA_WIDTH * FIXED_ONE;
constexpr int16_t ARENA_HEIGHT_FIXED = ARENA_HEIGHT * FIXED_ONE;

constexpr uint8_t FRAME_DURATION_MS = 16;
constexpr uint8_t PLAYER_SIZE = 7;
constexpr uint8_t PLAYER_START_X = 60;
constexpr uint8_t PLAYER_START_Y = 25;
// Speeds are in 1/16 pixel per frame; 181/256 approximates 1/sqrt(2).
constexpr uint8_t WALK_SPEED = 16;
constexpr uint8_t DASH_SPEED = 64;
constexpr uint8_t DASH_DURATION = 6;
constexpr uint8_t DASH_COOLDOWN = 250;
constexpr uint8_t ACTIVE_SLOT_COUNT = 2;
constexpr uint8_t MAX_MOVE_STEPS = (DASH_SPEED + FIXED_ONE - 1) / FIXED_ONE;

static_assert(WALK_SPEED > 0 && WALK_SPEED <= DASH_SPEED, "Invalid walk speed");
static_assert(DASH_SPEED <= 127 && DASH_DURATION > 0, "Invalid dash parameters");
static_assert(DASH_COOLDOWN >= DASH_DURATION, "Cooldown must cover the dash");
static_assert(PLAYER_SIZE < ARENA_HEIGHT && PLAYER_SIZE < ARENA_WIDTH,
              "Player must fit within one period of the arena");

} // namespace gc
