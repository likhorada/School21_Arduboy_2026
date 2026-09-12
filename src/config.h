#pragma once

#include <stdint.h>

namespace gc {

constexpr uint8_t HUD_HEIGHT = 0;
constexpr uint8_t ARENA_WIDTH = 104;
constexpr uint8_t ARENA_HEIGHT = 64 - HUD_HEIGHT;
// Позиции игрока и пуль храним в 1/16 пикселя: 24 единицы означают 1,5 пикселя.
constexpr uint8_t FIXED_ONE = 16;
constexpr int16_t ARENA_WIDTH_FIXED = ARENA_WIDTH * FIXED_ONE;
constexpr int16_t ARENA_HEIGHT_FIXED = ARENA_HEIGHT * FIXED_ONE;

constexpr uint8_t FRAME_DURATION_MS =
    16; // 62,5 кадра/с, если укладываемся в бюджет.
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
// Предупреждение перед спавном волны: кадры мигающих квадратов-индикаторов.
constexpr uint8_t SPAWN_DELAY_FRAMES = 30;

// Пауза: удержание A+B на полсекунды (~0.5s при 62.5 FPS).
constexpr uint8_t PAUSE_HOLD_FRAMES = 31;

constexpr uint8_t MAX_PROJECTILES = 4;
constexpr uint8_t SHOT_INTERVAL = 15; // 0,4 секунды между подходами очереди.
constexpr uint8_t SHOT_BURST_COUNT =
    2; // Пуль за подход (1 = обычный одиночный).
constexpr uint8_t SHOT_BURST_DELAY = 4; // Пауза между пулями очередью.
constexpr uint8_t SHOT_DAMAGE = 1;
constexpr uint8_t SHOT_RANGE = 48; // Пиксели от центра игрока до центра цели.
constexpr int16_t SHOT_RANGE_FIXED = SHOT_RANGE * FIXED_ONE;
constexpr uint8_t PROJECTILE_SPEED = 2 * FIXED_ONE;
constexpr uint8_t PROJECTILE_LIFETIME = 64;
constexpr uint8_t PROJECTILE_STEPS =
    (PROJECTILE_SPEED + FIXED_ONE - 1) / FIXED_ONE;
// Проверяем только короткий путь. Два кадра запаса учитывают округление
// скорости.
constexpr uint8_t AIM_PREVIEW_FRAMES =
    (SHOT_RANGE_FIXED + PROJECTILE_SPEED - 1) / PROJECTILE_SPEED + 2;

static_assert(WALK_SPEED > 0 && WALK_SPEED <= DASH_SPEED, "Invalid walk speed");
static_assert(DASH_SPEED <= 127 && DASH_DURATION > 0,
              "Invalid dash parameters");
static_assert(DASH_COOLDOWN >= DASH_DURATION, "Cooldown must cover the dash");
static_assert(PLAYER_SIZE < ARENA_HEIGHT && PLAYER_SIZE < ARENA_WIDTH,
              "Player must fit within one period of the arena");
static_assert(PROJECTILE_SPEED > 0 && PROJECTILE_SPEED <= 127,
              "Projectile velocity must fit in int8_t");
static_assert(SHOT_RANGE > 0 && SHOT_RANGE <= ARENA_WIDTH / 2,
              "Aim preview is limited to the short route");
static_assert(PROJECTILE_LIFETIME >= AIM_PREVIEW_FRAMES && SHOT_DAMAGE > 0,
              "Shots must live long enough to reach a target");
static_assert(SHOT_BURST_COUNT >= 1 && SHOT_BURST_DELAY > 0 &&
                  SHOT_BURST_DELAY < SHOT_INTERVAL,
              "Burst bullets must be quicker than the full reload");

// === ИГРОВЫЕ КОНСТАНТЫ ===
// HP и неуязвимость
constexpr uint8_t PLAYER_BASE_MAX_HP = 4;
constexpr uint8_t PLAYER_MAX_HP_CAP = 6;
constexpr uint8_t IFRAME_DURATION = 60; // 1 секунда при 62.5 FPS
constexpr uint8_t BLINK_INTERVAL = 4;   // Мигание каждые 4 кадра

// Стейдж таймер
constexpr uint16_t STAGE_TIME_SECONDS = 60;
constexpr uint16_t STAGE_TIME_FRAMES =
    uint32_t(STAGE_TIME_SECONDS) * 1000 / FRAME_DURATION_MS;
static_assert(STAGE_TIME_FRAMES == 3750,
              "Stage timer must be exactly 60 seconds");
inline uint8_t remainingSeconds(uint16_t frames) {
  return uint32_t(frames) * FRAME_DURATION_MS / 1000;
}
constexpr uint16_t PASSIVE_PRICE = 100;
constexpr uint16_t ACTIVE_PRICE = 200;
constexpr uint8_t SWEEP_DAMAGE = 4;
constexpr uint8_t SWEEP_RADIUS = 24;
constexpr uint8_t FREEZE_DURATION = 90;
constexpr uint8_t COMPACT_SHIELD_DURATION = 60;

// UI: правая колонка x=104 (128-24), 6 строк по 8px
constexpr uint8_t UI_COLUMN_X = 104;
constexpr uint8_t UI_ROW_HEIGHT = 8;
constexpr uint8_t UI_HEART_SIZE = 5;

// Очки за волну/стадию
constexpr uint16_t WAVE_CLEAR_BONUS = 50;
constexpr uint16_t STAGE_TIME_BONUS_MULT = 100;

// Магазин: количество вариантов
constexpr uint8_t SHOP_PASSIVE_CHOICES = 3;
constexpr uint8_t SHOP_ACTIVE_CHOICES = 3;

} // namespace gc
