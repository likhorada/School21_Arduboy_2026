#pragma once

#include "config.h"
#include "enemies.h"

namespace gc {

// Тестовый враг: неподвижный, можно убить выстрелами, возрождается.
// x/y: левый верхний угол в пикселях. HP и вспышка делят один байт вместо двух.
struct Dummy {
    uint8_t x;
    uint8_t y;
    uint8_t hpAndFlash;     // Биты 0-1: HP (0-3), биты 2-4: flash (0-7)
    uint8_t respawnFrames;
};

// Получить HP из упакованного поля
inline uint8_t getHp(const Dummy& d) {
    return d.hpAndFlash & 0x03;
}

// Получить вспышку попадания из упакованного поля
inline uint8_t getHitFlash(const Dummy& d) {
    return (d.hpAndFlash >> 2) & 0x07;
}

// Установить HP, сохранив flash
inline void setHp(Dummy& d, uint8_t hp) {
    d.hpAndFlash = (d.hpAndFlash & 0xFC) | (hp & 0x03);
}

// Установить flash, сохранив HP
inline void setHitFlash(Dummy& d, uint8_t flash) {
    d.hpAndFlash = (d.hpAndFlash & 0xE3) | ((flash & 0x07) << 2);
}

// Установить HP и flash одновременно
inline void setHpAndFlash(Dummy& d, uint8_t hp, uint8_t flash) {
    d.hpAndFlash = (hp & 0x03) | ((flash & 0x07) << 2);
}

// Пуля: летит по прямой. Координаты и скорости в единицах 1/16 пикселя.
// framesLeft == 0 означает пустой слот.
struct Projectile {
    int16_t x;
    int16_t y;
    int8_t velocityX;
    int8_t velocityY;
    uint8_t framesLeft;
};

// Боевая система: старые dummy + новые враги, пули, кулдаун автострельбы
struct Combat {
    Dummy dummies[DUMMY_COUNT];
    Projectile projectiles[MAX_PROJECTILES];
    uint8_t shotCooldown;
    uint8_t burstShots;         // Выпущенные пули текущей очереди (0 = новый заход)
    
    // Новая система врагов
    Enemy enemies[MAX_ENEMIES];
    ScoreOrb scoreOrbs[MAX_SCORE_ORBS];
    uint32_t enemyRandomState;
    uint16_t playerScore;
    
    // Прогресс по стейджам
    uint8_t currentStage;       // Текущий стейдж (0-based)
    uint8_t currentWave;        // Текущая волна в стейдже (0-based)
    bool waveCompleted;         // Флаг завершения волны (все враги убиты)
    bool stageCleared;          // Флаг: все волны стейджа пройдены, показать меню отдыха
    uint8_t spawnTimer;         // Кадры до спавна волны; >0 — показывать индикаторы
    uint16_t stageTimer;        // Таймер стейджа в кадрах (60 сек)
    uint8_t freezeFrames;
};

// Сброс: враги на стартовые позиции, пули очищены
void resetCombat(Combat& combat);

// Таймеры -> пули -> автострельба. playerX/Y: левый верхний угол в 1/16 пикселя.
// Новая пуля впервые двигается на следующем кадре, независимо от номера слота.
void updateCombat(Combat& combat, int16_t playerX, int16_t playerY, uint8_t damage = SHOT_DAMAGE);
void damageEnemy(Combat& combat, uint8_t index, uint8_t damage);

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

// Поддержание минимального количества врагов на арене
void maintainEnemyCount(Combat& combat);

// Read-only wrapped hitbox query; touching adds one movement step of tolerance.
bool checkPlayerEnemyCollisions(const Combat& combat, int16_t playerX, int16_t playerY, bool touching = false);

static_assert(sizeof(Dummy) == 4, "Dummy must fit in four bytes");
static_assert(sizeof(Enemy) == 6, "Enemy must be 6 bytes");
static_assert(sizeof(ScoreOrb) == 6, "ScoreOrb must be 6 bytes");

} // пространство имён gc
