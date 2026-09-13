#pragma once

#include "config.h"
#include "enemies.h"

namespace gc {

constexpr uint8_t AUDIO_EVENT_ENEMY_DEATH = 1 << 0;
constexpr uint8_t AUDIO_EVENT_COIN = 1 << 1;

// Попадание пули в босса возвращается как маркер-сентинел: слоты врагов
// занимают 0..MAX_ENEMIES-1, поэтому индекс MAX_ENEMIES зарезервировать нельзя
// для врага.
constexpr int8_t BOSS_HIT = MAX_ENEMIES;

// Пуля: летит по прямой. Координаты и скорости в единицах 1/16 пикселя.
// framesLeft == 0 означает пустой слот.
struct Projectile {
    int16_t x;
    int16_t y;
    int8_t velocityX;
    int8_t velocityY;
    uint8_t framesLeft;
};

// Боевая система: враги, пули, кулдаун автострельбы
struct Combat {
    Projectile projectiles[MAX_PROJECTILES];
    uint8_t shotCooldown;
    uint8_t burstShots;         // Выпущенные пули текущей очереди (0 = новый заход)
    
    // Экземпляры врагов
    Enemy enemies[MAX_ENEMIES];
    ScoreOrb scoreOrbs[MAX_SCORE_ORBS];
    uint16_t playerScore;

    // Босс третьего стейджа (неактивен на остальных)
    Boss boss;
    
    // Прогресс по стейджам
    uint8_t currentStage;       // Текущий стейдж (0-based)
    uint8_t currentWave;        // Текущая волна в стейдже (0-based)
    uint8_t waveCompleted : 1;  // Все враги волны убиты
    uint8_t stageCleared : 1;   // Все волны этапа пройдены
    uint8_t audioEvents : 2;    // Одно-кадровые события для SFX
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
// Урон боссу автострельбой; на 0 HP закрывает стейдж (очки, очистка, бонус
// времени). Ничего не делает, пока босс не активен.
void damageBoss(Combat& combat, uint8_t damage);

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
static_assert(sizeof(ScoreOrb) == 6, "ScoreOrb must be 6 bytes");

} // пространство имён gc
