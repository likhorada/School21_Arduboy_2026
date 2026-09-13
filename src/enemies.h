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

struct Combat;

// Босс третьего стейджа: квадрат-надпись "LOV / I E / YOU". x/y — центр в 1/16
// пикселя. Почти стоит: медленно ходит по квадрату 4x4 вокруг якоря (phase),
// раз в waveTimer испускает волну прислужников. Ранят его только пули
// автострельбы; контакт с игроком ранит игрока. Босс на арене, когда hp>0;
// спавн-предупреждение и 'мёртвое' состояние имеют hp==0.
struct Boss {
  int16_t x;         // Центр (1/16 пикселя)
  int16_t y;
  uint16_t waveTimer; // Кадры до следующей волны прислужников
  uint8_t hp;         // Оставшиеся попадания (0 — неактивен/мёртв)
  uint8_t phase;      // Тик патруля (0..4*BOSS_PATROL_LEG_LEN-1)
};

static_assert(sizeof(Boss) == 8, "Boss must be 8 bytes");

// Упрощённый враг с минимальной памятью
struct Enemy {
  int16_t x; // Субпиксели (1/16 пикселя)
  int16_t y;
  uint8_t typeAndHp;  // Биты 0-2: type (0-7), биты 3-7: HP (0-31)
  uint8_t splitLevel; // Для Splitter: текущий размер (HP до деления, 8/16)
};

// Дроп очков после смерти врага
struct ScoreOrb {
  int16_t x;
  int16_t y;
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
                   int16_t playerX, int16_t playerY, uint8_t stage);

void createScoreOrb(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t x, int16_t y,
                    uint8_t value);

uint8_t collectOrbs(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t playerX,
                    int16_t playerY);

// === БОСС ===
// Сброс боевого состояния перед босс-стейджем: активный босс, предупреждение о
// появлении (spawnTimer = SPAWN_DELAY_FRAMES), прислужники и пули очищены.
void resetBossStage(Combat& combat);
// Активация босса по истечении предупреждения (ставит позицию кадра 0).
void activateBoss(Combat& combat);
// Ежекадровое обновление босса: движение по кругу и волны прислужников.
// playerX/playerY — центр игрока (1/16 пикселя).
void updateBoss(Combat& combat, int16_t playerX, int16_t playerY);
// Детерминированная точка появления босса для мигающего индикатора.
void getBossSpawnPixel(uint8_t& x, uint8_t& y);
// Пересечение хитбокса босса 14x16 с игроком (центры; touching добавляет шаг
// контакта как у врагов).
bool bossOverlapsPlayer(const Boss& boss, int16_t playerCenterX,
                        int16_t playerCenterY, bool touching = false);

static_assert(sizeof(Enemy) == 6, "Enemy must be 6 bytes");
static_assert(sizeof(ScoreOrb) == 6, "ScoreOrb must be 6 bytes");

} // namespace gc
