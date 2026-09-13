#pragma once

#include "config.h"

namespace gc {

// Типы врагов (отображаются как префикс)
enum class EnemyType : uint8_t {
  None = 0,
  Basic,    // 0x - базовый медленный
  Splitter, // 0d - делится при смерти
  Fast,     // 0f - быстрый, HP влияет на скорость
};

// Упрощённый враг с минимальной памятью
struct Enemy {
  int16_t x; // Субпиксели (1/16 пикселя)
  int16_t y;
  uint8_t typeAndHp;  // Биты 0-2: type (0-7), биты 3-7: HP (0-31)
  uint8_t splitLevel; // Для Splitter: текущий размер (HP до деления, 8/16)
};

// Дроп очков после смерти врага
struct ScoreOrb {
  uint8_t x;
  uint8_t y;
  uint8_t value;    // Количество очков
  uint8_t lifetime; // Кадры до исчезновения
};

// Геттеры/сеттеры для упакованных полей
inline EnemyType getEnemyType(const Enemy &e) {
  return static_cast<EnemyType>(e.typeAndHp & 0x07);
}

inline uint8_t getEnemyHp(const Enemy &e) { return (e.typeAndHp >> 3) & 0x1F; }

inline void setEnemyType(Enemy &e, EnemyType type) {
  e.typeAndHp = (e.typeAndHp & 0xF8) | (static_cast<uint8_t>(type) & 0x07);
}

inline void setEnemyHp(Enemy &e, uint8_t hp) {
  e.typeAndHp = (e.typeAndHp & 0x07) | ((hp & 0x1F) << 3);
}

inline void setEnemyTypeAndHp(Enemy &e, EnemyType type, uint8_t hp) {
  e.typeAndHp = (static_cast<uint8_t>(type) & 0x07) | ((hp & 0x1F) << 3);
}

inline uint8_t getSplitLevel(const Enemy &e) { return e.splitLevel & 0x1F; }
inline uint8_t getDamageRemainder(const Enemy &e) { return e.splitLevel >> 5; }
inline void setSplitLevel(Enemy &e, uint8_t level) {
  e.splitLevel = (e.splitLevel & 0xE0) | (level & 0x1F);
}
inline void setDamageRemainder(Enemy &e, uint8_t remainder) {
  e.splitLevel = (e.splitLevel & 0x1F) | ((remainder & 0x07) << 5);
}

// Константы
constexpr uint8_t MAX_ENEMIES = 8;    // Максимум врагов одновременно
constexpr uint8_t MAX_SCORE_ORBS = 3; // Максимум дропов
constexpr uint8_t ORB_LIFETIME = 125; // 2 секунды (125 кадров)

// Параметры врагов
constexpr uint8_t BASIC_SPEED = 6; // Медленная скорость
constexpr uint8_t BASIC_MIN_HP = 2;
constexpr uint8_t BASIC_MAX_HP = 10;

constexpr uint8_t SPLITTER_BASE_HP = 8; // Один уровень: N→N/2→…→1
constexpr uint8_t SPLITTER_SPEED = 8;

constexpr uint8_t FAST_BASE_SPEED = 4;
constexpr uint8_t FAST_MAX_HP = 15; // 0F в hex
constexpr uint8_t FAST_MAX_SPEED = 13;
// Вектор (13,6) медленнее даже диагонального шага игрока (11,11).
static_assert(FAST_MAX_SPEED * FAST_MAX_SPEED +
                      (FAST_MAX_SPEED / 2) * (FAST_MAX_SPEED / 2) <
                  2 * ((WALK_SPEED * 181 + 128) / 256) *
                      ((WALK_SPEED * 181 + 128) / 256),
              "Fast enemy must remain slower than walking");

// Хитбокс врага: половины размера компактного текста в пикселях.
// Максимальная подпись "d10" = 12x5, поэтому хитбокс 12x6.
constexpr uint8_t ENEMY_HALF_WIDTH = 6;
constexpr uint8_t ENEMY_HALF_HEIGHT = 3;

// Упаковка не должна молча терять тип или HP при будущих правках настроек.
static_assert(static_cast<uint8_t>(EnemyType::Fast) <= 7,
              "Packed type uses three bits");
static_assert(SPLITTER_BASE_HP <= 31, "Packed HP uses five bits");

// Функции
// Спавн врага. stage нужен для проверки занятости препятствиями текущей арены.
void spawnEnemy(Enemy &enemy, EnemyType type, int16_t x, int16_t y,
                uint8_t variant, uint8_t stage);
bool enemyOverlapsPlayer(int16_t x, int16_t y, int16_t playerCenterX,
                         int16_t playerCenterY, bool touching = false);
bool enemyPositionValid(uint8_t stage, int16_t x, int16_t y);

void updateEnemies(Enemy enemies[MAX_ENEMIES], ScoreOrb orbs[MAX_SCORE_ORBS],
                    int16_t playerX, int16_t playerY, uint8_t stage,
                    bool slowed = false, bool reversed = false);

void pushEnemyAway(Enemy& enemy, int16_t playerX, int16_t playerY,
                   uint8_t stage, uint8_t distance);

void createScoreOrb(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t x, int16_t y,
                    uint8_t value);

uint8_t collectOrbs(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t playerX,
                    int16_t playerY);

static_assert(sizeof(Enemy) == 6, "Enemy must be 6 bytes");
static_assert(sizeof(ScoreOrb) == 4, "ScoreOrb must be 4 bytes");

} // namespace gc
