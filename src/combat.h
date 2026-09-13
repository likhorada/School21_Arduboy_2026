#pragma once

#include "config.h"
#include "enemies.h"

namespace gc {

constexpr uint8_t AUDIO_EVENT_ENEMY_DEATH = 1 << 0;
constexpr uint8_t AUDIO_EVENT_COIN = 1 << 1;

// Пуля: летит по прямой. Координаты и скорости в единицах 1/16 пикселя.
// framesLeft == 0 означает пустой слот.
struct Projectile {
    uint8_t x2;
    uint8_t y2;
    uint8_t directionAndDamage;
    uint8_t framesLeft;
};

// Боевая система: враги, пули, кулдаун автострельбы
struct Combat {
    Projectile projectiles[MAX_PROJECTILES];
    uint8_t shotCooldown;
    
    // Экземпляры врагов
    Enemy enemies[MAX_ENEMIES];
    ScoreOrb scoreOrbs[MAX_SCORE_ORBS];
    uint16_t playerScore;
    
    // Прогресс по стейджам
    uint8_t currentStage;       // Текущий стейдж (0-based)
    uint8_t currentWave;        // Текущая волна в стейдже (0-based)
    uint8_t waveCompleted : 1;  // Все враги волны убиты
    uint8_t stageCleared : 1;   // Все волны этапа пройдены
    uint8_t audioEvents : 2;    // Одно-кадровые события для SFX
    uint8_t spawnTimer;         // Кадры до спавна волны; >0 — показывать индикаторы
    uint16_t stageTimer;        // Таймер стейджа в кадрах (60 сек)
    uint8_t tickPhase;
    uint8_t timeWarpTicks;
    uint8_t recursiveTicks;
    uint8_t bitShiftTicks;
    uint8_t puddleTicks;
    uint8_t puddleX;
    uint8_t puddleY;
    uint8_t spiralShots;
    uint8_t spiralDelay;
    uint8_t visualEffect;
};

// Сброс: враги на стартовые позиции, пули очищены
void resetCombat(Combat& combat);

// Таймеры -> пули -> автострельба. playerX/Y: левый верхний угол в 1/16 пикселя.
// Новая пуля впервые двигается на следующем кадре, независимо от номера слота.
void updateCombat(Combat& combat, int16_t playerX, int16_t playerY,
                  uint8_t damageLevel = 0, uint8_t overclockLevel = 0,
                  uint8_t fragmentationLevel = 0, uint8_t rangeLevel = 0);
void damageEnemy(Combat& combat, uint8_t index, uint8_t damage);
void damageEnemyFifths(Combat& combat, uint8_t index, uint8_t damage);

int16_t projectileX(const Projectile& projectile);
int16_t projectileY(const Projectile& projectile);
int8_t projectileVelocityX(const Projectile& projectile);
int8_t projectileVelocityY(const Projectile& projectile);

// Спавн врагов текущей волны. playerX/Y: центр игрока (1/16 пикселя),
// чтобы враги не появлялись прямо на нём.
void spawnCurrentWave(Combat& combat, int16_t playerX, int16_t playerY);

// Детерминированная позиция спавна i-го врага волны: одна и та же и для
// индикаторов, и для фактического спавна. Позиция не пересекает препятствия.
void getSpawnPixel(uint8_t stage, uint8_t wave, uint8_t index, uint8_t& x, uint8_t& y);

// Проверка завершения волны и переход к следующей
void checkWaveCompletion(Combat& combat);

// Получение стоимости врага в очках
uint8_t getEnemyScoreValue(EnemyType type, uint8_t variant);

// Read-only wrapped hitbox query; touching adds one movement step of tolerance.
bool checkPlayerEnemyCollisions(const Combat& combat, int16_t playerX, int16_t playerY, bool touching = false);

static_assert(sizeof(Enemy) == 6, "Enemy must be 6 bytes");
static_assert(sizeof(ScoreOrb) == 4, "ScoreOrb must be 4 bytes");

} // пространство имён gc
