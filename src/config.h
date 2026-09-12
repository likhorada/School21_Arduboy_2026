#pragma once

#include <stdint.h>

namespace gc {

constexpr uint8_t HUD_HEIGHT = 8;
constexpr uint8_t ARENA_WIDTH = 128;
constexpr uint8_t ARENA_HEIGHT = 64 - HUD_HEIGHT;
// Позиции игрока и пуль храним в 1/16 пикселя: 24 единицы означают 1,5 пикселя.
constexpr uint8_t FIXED_ONE = 16;
constexpr int16_t ARENA_WIDTH_FIXED = ARENA_WIDTH * FIXED_ONE;
constexpr int16_t ARENA_HEIGHT_FIXED = ARENA_HEIGHT * FIXED_ONE;

constexpr uint8_t FRAME_DURATION_MS = 16; // 62,5 кадра/с, если укладываемся в бюджет.
constexpr uint8_t PLAYER_SIZE = 7;
constexpr uint8_t PLAYER_START_X = 60;
constexpr uint8_t PLAYER_START_Y = 25;
// Скорости в единицах 1/16 пикселя за кадр; таймеры считают кадры.
constexpr uint8_t WALK_SPEED = 16;
constexpr uint8_t DASH_SPEED = 64;
constexpr uint8_t DASH_DURATION = 6;
constexpr uint8_t DASH_COOLDOWN = 250;
constexpr uint8_t ACTIVE_SLOT_COUNT = 2;
constexpr uint8_t MAX_MOVE_STEPS = (DASH_SPEED + FIXED_ONE - 1) / FIXED_ONE;

// Размеры пулов фиксированы: при заполнении новая память не выделяется.
constexpr uint8_t DUMMY_COUNT = 3;
constexpr uint8_t DUMMY_SIZE = 7;
constexpr uint8_t DUMMY_MAX_HP = 3;
constexpr uint8_t DUMMY_RESPAWN_FRAMES = 125; // 2 секунды после смерти.
constexpr uint8_t HIT_FLASH_FRAMES = 6;
constexpr uint8_t MAX_PROJECTILES = 4;
constexpr uint8_t SHOT_INTERVAL = 25; // 0,4 секунды между успешными выстрелами.
constexpr uint8_t SHOT_DAMAGE = 1;
constexpr uint8_t SHOT_RANGE = 48; // Пиксели от центра игрока до центра цели.
constexpr int16_t SHOT_RANGE_FIXED = SHOT_RANGE * FIXED_ONE;
constexpr uint8_t PROJECTILE_SPEED = 2 * FIXED_ONE;
constexpr uint8_t PROJECTILE_LIFETIME = 64;
constexpr uint8_t PROJECTILE_STEPS = (PROJECTILE_SPEED + FIXED_ONE - 1) / FIXED_ONE;
// Проверяем только короткий путь. Два кадра запаса учитывают округление скорости.
constexpr uint8_t AIM_PREVIEW_FRAMES =
    (SHOT_RANGE_FIXED + PROJECTILE_SPEED - 1) / PROJECTILE_SPEED + 2;

static_assert(WALK_SPEED > 0 && WALK_SPEED <= DASH_SPEED, "Invalid walk speed");
static_assert(DASH_SPEED <= 127 && DASH_DURATION > 0, "Invalid dash parameters");
static_assert(DASH_COOLDOWN >= DASH_DURATION, "Cooldown must cover the dash");
static_assert(PLAYER_SIZE < ARENA_HEIGHT && PLAYER_SIZE < ARENA_WIDTH,
              "Player must fit within one period of the arena");
static_assert(PROJECTILE_SPEED > 0 && PROJECTILE_SPEED <= 127,
              "Projectile velocity must fit in int8_t");
static_assert(SHOT_RANGE > 0 && SHOT_RANGE <= ARENA_WIDTH / 2,
              "Aim preview is limited to the short route");
static_assert(PROJECTILE_LIFETIME >= AIM_PREVIEW_FRAMES && SHOT_DAMAGE > 0,
              "Shots must live long enough to reach a target");
static_assert(DUMMY_COUNT <= 8, "Target rejection mask has only eight bits");
static_assert(DUMMY_SIZE <= ARENA_WIDTH / 2 && DUMMY_SIZE <= ARENA_HEIGHT / 2,
              "Segment collision expects boxes no larger than half the arena");
// Упаковка не должна молча обрезать HP или длительность вспышки при смене настроек.
static_assert(DUMMY_MAX_HP > 0 && DUMMY_MAX_HP <= 3, "Packed HP uses two bits");
static_assert(HIT_FLASH_FRAMES <= 7, "Packed hit flash uses three bits");

} // пространство имён gc
