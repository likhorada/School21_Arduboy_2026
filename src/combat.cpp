#include "combat.h"

#include "arena.h"
#include "enemies.h"
#include "stages.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

namespace gc {
namespace {

constexpr int8_t NO_HIT = -1;
constexpr int8_t WALL_HIT = -2;
constexpr uint8_t PROJECTILE_DIRECTION_MASK = 0x1F;
constexpr uint8_t PROJECTILE_DAMAGE_SHIFT = 5;
enum ProjectileDamage : uint8_t { NormalDamage, HalfDamage, DoubleDamage };

const int8_t projectileVelocityX2[] PROGMEM = {
    4, 4, 4, 4, 3, 2, 1, 1, 0, -1, -1, -2, -3, -4, -4, -4,
    -4, -4, -4, -4, -3, -2, -1, -1, 0, 1, 1, 2, 3, 4, 4, 4};
const int8_t projectileVelocityY2[] PROGMEM = {
    0, 1, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 1,
    0, -1, -1, -2, -3, -4, -4, -4, -4, -4, -4, -4, -3, -2, -1, -1};

int8_t directionVelocity(const int8_t* table, uint8_t direction) {
  return static_cast<int8_t>(pgm_read_byte(&table[direction & 0x1F]));
}

uint8_t directionFor(int16_t dx, int16_t dy) {
  const uint16_t ax = dx < 0 ? -dx : dx;
  const uint16_t ay = dy < 0 ? -dy : dy;
  const bool horizontal = ax >= ay;
  const uint16_t major = horizontal ? ax : ay;
  const uint16_t minor = horizontal ? ay : ax;
  if (major == 0) return 0;
  const uint8_t bend = minor * 16 < major ? 0
                       : minor * 16 < major * 3 ? 1
                       : minor * 8 < major * 3 ? 2
                       : minor * 4 < major * 3 ? 3 : 4;
  if (horizontal) {
    if (dx >= 0) return dy >= 0 ? bend : (-bend & 0x1F);
    return dy >= 0 ? 16 - bend : 16 + bend;
  }
  if (dy >= 0) return dx >= 0 ? 8 - bend : 8 + bend;
  return dx >= 0 ? 24 + bend : 24 - bend;
}

bool createProjectile(Combat& combat, int16_t x, int16_t y, uint8_t direction,
                      ProjectileDamage damage) {
  for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
    if (combat.projectiles[i].framesLeft == 0) {
      Projectile& shot = combat.projectiles[i];
      shot.x2 = wrapCoordinate(x, ARENA_WIDTH_FIXED) / (FIXED_ONE / 2);
      shot.y2 = wrapCoordinate(y, ARENA_HEIGHT_FIXED) / (FIXED_ONE / 2);
      shot.directionAndDamage =
          (direction & PROJECTILE_DIRECTION_MASK) | (damage << PROJECTILE_DAMAGE_SHIFT);
      shot.framesLeft = PROJECTILE_LIFETIME;
      return true;
    }
  }
  return false;
}

// Проверяем, попал ли отрезок (x,y)→(x+dx,y+dy) во что-то.
// Возвращает индекс врага, WALL_HIT или NO_HIT. Стены имеют приоритет; урона
// здесь нет.
int8_t findHit(const Combat &combat, int16_t x, int16_t y, int16_t dx,
               int16_t dy) {
  if (shotBlocked(combat.currentStage, x, y, dx, dy)) {
    return WALL_HIT;
  }
  // Дешёвый отсев до дорогого segmentHitsBox (int32): центр врага должен быть
  // в пределах полосы вокруг отрезка, иначе попадание невозможно. Отрезок на
  // шаг не длиннее пикселя, поэтому большинство врагов отсеиваются парой
  // вычитаний без умножений.
  const int16_t spanX = (dx < 0 ? -dx : dx) + ENEMY_HALF_WIDTH * FIXED_ONE;
  const int16_t spanY = (dy < 0 ? -dy : dy) + ENEMY_HALF_HEIGHT * FIXED_ONE;
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    const Enemy &enemy = combat.enemies[i];
    if (getEnemyType(enemy) == EnemyType::None)
      continue;
    const int16_t sx =
        shortestDelta(x, wrapCoordinate(enemy.x, ARENA_WIDTH_FIXED),
                      ARENA_WIDTH_FIXED);
    const int16_t sy =
        shortestDelta(y, wrapCoordinate(enemy.y, ARENA_HEIGHT_FIXED),
                      ARENA_HEIGHT_FIXED);
    if (sx > spanX || sx < -spanX || sy > spanY || sy < -spanY)
      continue;
    const Obstacle box = {
        uint8_t(wrapCoordinate(enemy.x / FIXED_ONE - ENEMY_HALF_WIDTH,
                               ARENA_WIDTH)),
        uint8_t(wrapCoordinate(enemy.y / FIXED_ONE - ENEMY_HALF_HEIGHT,
                               ARENA_HEIGHT)),
        2 * ENEMY_HALF_WIDTH, 2 * ENEMY_HALF_HEIGHT};
    if (segmentHitsBox(x, y, dx, dy, box))
      return i;
  }
  if (combat.currentStage == BOSS_STAGE && combat.boss.hpFifths &&
      combat.spawnTimer == 0) {
    const Boss& boss = combat.boss;
    const int16_t sx = shortestDelta(x, boss.x, ARENA_WIDTH_FIXED);
    const int16_t sy = shortestDelta(y, boss.y, ARENA_HEIGHT_FIXED);
    const int16_t bossSpanX = (dx < 0 ? -dx : dx) + BOSS_HALF_WIDTH * FIXED_ONE;
    const int16_t bossSpanY = (dy < 0 ? -dy : dy) + BOSS_HALF_HEIGHT * FIXED_ONE;
    if (sx <= bossSpanX && sx >= -bossSpanX && sy <= bossSpanY &&
        sy >= -bossSpanY) {
      const Obstacle box = {
          uint8_t(wrapCoordinate(boss.x / FIXED_ONE - BOSS_HALF_WIDTH,
                                 ARENA_WIDTH)),
          uint8_t(wrapCoordinate(boss.y / FIXED_ONE - BOSS_HALF_HEIGHT,
                                 ARENA_HEIGHT)),
          2 * BOSS_HALF_WIDTH, 2 * BOSS_HALF_HEIGHT};
      if (segmentHitsBox(x, y, dx, dy, box)) return BOSS_HIT;
    }
  }
  return NO_HIT;
}

// Двигаем активную пулю шагами не больше пикселя. Попадание освобождаёт её
// слот.
int8_t advanceProjectile(Projectile &projectile, const Combat &combat) {
  const int16_t x = projectile.x2 * (FIXED_ONE / 2);
  const int16_t y = projectile.y2 * (FIXED_ONE / 2);
  const int16_t dx = projectileVelocityX(projectile);
  const int16_t dy = projectileVelocityY(projectile);
  const int8_t hit = findHit(combat, x, y, dx, dy);
  if (hit != NO_HIT) {
    projectile.framesLeft = 0;
    return hit;
  }
  projectile.x2 = wrapCoordinate(projectile.x2 + dx / (FIXED_ONE / 2),
                                 ARENA_WIDTH * 2);
  projectile.y2 = wrapCoordinate(projectile.y2 + dy / (FIXED_ONE / 2),
                                 ARENA_HEIGHT * 2);
  --projectile.framesLeft;
  return NO_HIT;
}

} // namespace

int16_t projectileX(const Projectile& projectile) {
  return projectile.x2 * (FIXED_ONE / 2);
}

int16_t projectileY(const Projectile& projectile) {
  return projectile.y2 * (FIXED_ONE / 2);
}

int8_t projectileVelocityX(const Projectile& projectile) {
  const uint8_t direction = projectile.directionAndDamage & PROJECTILE_DIRECTION_MASK;
  int8_t velocity = directionVelocity(projectileVelocityX2, direction);
  if (((direction & 7) == 1 || (direction & 7) == 7) &&
      !(projectile.framesLeft & 1) && (velocity == 1 || velocity == -1))
    velocity = 0;
  return velocity * (FIXED_ONE / 2);
}

int8_t projectileVelocityY(const Projectile& projectile) {
  const uint8_t direction = projectile.directionAndDamage & PROJECTILE_DIRECTION_MASK;
  int8_t velocity = directionVelocity(projectileVelocityY2, direction);
  if (((direction & 7) == 1 || (direction & 7) == 7) &&
      !(projectile.framesLeft & 1) && (velocity == 1 || velocity == -1))
    velocity = 0;
  return velocity * (FIXED_ONE / 2);
}

// Сброс боевой системы: очищаем всё, возвращаем врагов на стартовые позиции.
void resetCombat(Combat &combat) {
  combat = {};

  combat.playerScore = 0;
  combat.currentStage = 0;
  combat.currentWave = 0;
  combat.waveCompleted = false;
  combat.stageCleared = false;
  combat.stageTimer = STAGE_TIME_FRAMES; // 60 секунд

  // Предупреждение перед первой волной
  combat.spawnTimer = SPAWN_DELAY_FRAMES;
}

// Read-only contact query; health and invulnerability belong to the player.
bool checkPlayerEnemyCollisions(const Combat &combat, int16_t playerX,
                                int16_t playerY, bool touching) {
  const int16_t playerCenterX = playerX + (PLAYER_SIZE * FIXED_ONE) / 2;
  const int16_t playerCenterY = playerY + (PLAYER_SIZE * FIXED_ONE) / 2;
  if (combat.currentStage == BOSS_STAGE && combat.boss.hpFifths &&
      bossOverlapsPlayer(combat.boss, playerCenterX, playerCenterY, touching))
    return true;
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    const Enemy &enemy = combat.enemies[i];
    if (getEnemyType(enemy) == EnemyType::None) {
      continue;
    }
    if (enemyOverlapsPlayer(enemy.x, enemy.y, playerCenterX, playerCenterY,
                            touching)) {
      return true;
    }
  }
  return false;
}

// Обновление боевой системы на текущий кадр.
void updateCombat(Combat &combat, int16_t playerX, int16_t playerY,
                  uint8_t damageLevel, uint8_t overclockLevel,
                  uint8_t fragmentationLevel, uint8_t rangeLevel) {
  if (combat.stageCleared)
    return;
  // Bonus clock includes spawn warnings; expiration never changes progression.
  if (combat.stageTimer > 0) {
    --combat.stageTimer;
  }

  if (combat.shotCooldown > 0) {
    --combat.shotCooldown;
  }

  const int16_t originX =
      wrapCoordinate(playerX + PLAYER_SIZE * FIXED_ONE / 2, ARENA_WIDTH_FIXED);
  const int16_t originY =
      wrapCoordinate(playerY + PLAYER_SIZE * FIXED_ONE / 2, ARENA_HEIGHT_FIXED);

  updateEnemies(combat.enemies, combat.scoreOrbs, originX, originY,
                combat.currentStage, combat.timeWarpTicks != 0,
                combat.bitShiftTicks != 0);

  if (combat.currentStage == BOSS_STAGE) {
    if (combat.spawnTimer > 0 && --combat.spawnTimer == 0 &&
        !combat.boss.hpFifths)
      activateBoss(combat);
    if (combat.boss.hpFifths) updateBoss(combat, originX, originY);
  } else if (combat.spawnTimer > 0 && --combat.spawnTimer == 0) {
    spawnCurrentWave(combat, originX, originY);
  }

  // Существующие пули обновляем до создания новой: порядок слотов не влияет на
  // полёт.
  for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
    Projectile &projectile = combat.projectiles[i];
    if (projectile.framesLeft == 0) {
      continue;
    }
    const int8_t hit = advanceProjectile(projectile, combat);
    if (hit >= 0) {
      const uint8_t baseDamage = 5 + damageLevel;
      const uint8_t mode = projectile.directionAndDamage >> PROJECTILE_DAMAGE_SHIFT;
      const uint8_t damage = mode == HalfDamage ? baseDamage / 2
                             : mode == DoubleDamage ? baseDamage * 2
                                                    : baseDamage;
      if (hit == BOSS_HIT) damageBossFifths(combat, damage);
      else damageEnemyFifths(combat, hit, damage);
    }
  }

  if (combat.tickPhase == 0 && combat.puddleTicks &&
      combat.puddleTicks % MEMORY_DUMP_PULSE_TICKS == 0) {
    uint8_t targets = 0;
    const int16_t radius = MEMORY_DUMP_RADIUS * FIXED_ONE;
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
      const Enemy& enemy = combat.enemies[i];
      const int16_t dx = shortestDelta(combat.puddleX * FIXED_ONE, enemy.x,
                                       ARENA_WIDTH_FIXED);
      const int16_t dy = shortestDelta(combat.puddleY * FIXED_ONE, enemy.y,
                                       ARENA_HEIGHT_FIXED);
      if (getEnemyType(enemy) != EnemyType::None &&
          static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy <=
              static_cast<int32_t>(radius) * radius)
        targets |= uint8_t(1 << i);
    }
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
      if (targets & (1 << i)) damageEnemy(combat, i, 1);
  }

  // Собираем сферы очков
  const uint8_t collectedScore =
      collectOrbs(combat.scoreOrbs, originX, originY);
  combat.playerScore += collectedScore;
  if (collectedScore) combat.audioEvents |= AUDIO_EVENT_COIN;
  checkWaveCompletion(combat);
  if (combat.stageCleared)
    return;

  if (combat.spiralShots) {
    if (combat.spiralDelay) {
      --combat.spiralDelay;
    } else {
      const uint8_t emitted = STACK_OVERFLOW_SHOTS - combat.spiralShots;
      if (createProjectile(combat, originX, originY, (emitted * 3) & 0x1F,
                           DoubleDamage)) {
        --combat.spiralShots;
        combat.spiralDelay = STACK_OVERFLOW_DELAY - 1;
      }
    }
  }

  if (combat.shotCooldown != 0) return;

  const int16_t range = uint16_t(SHOT_RANGE_FIXED) * (10 + rangeLevel * 3) / 10;
  const uint32_t rangeSquared = static_cast<uint32_t>(range) * range;
  uint8_t usedTargets = 0;
  bool bossUsed = false;
  bool fired = false;
  for (uint8_t shotIndex = 0; shotIndex <= fragmentationLevel; ++shotIndex) {
    constexpr uint8_t NO_TARGET = 0xFF;
    uint8_t bestTarget = NO_TARGET;
    uint32_t bestDistanceSquared = rangeSquared + 1;
    int16_t bestDx = 0, bestDy = 0;
    int16_t targetX = 0, targetY = 0;
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
      const Enemy& enemy = combat.enemies[i];
      if ((usedTargets & (1 << i)) || getEnemyType(enemy) == EnemyType::None)
        continue;
      const int16_t dx = shortestDelta(originX, enemy.x, ARENA_WIDTH_FIXED);
      const int16_t dy = shortestDelta(originY, enemy.y, ARENA_HEIGHT_FIXED);
      const uint32_t distanceSquared = static_cast<int32_t>(dx) * dx +
                                       static_cast<int32_t>(dy) * dy;
      if (distanceSquared < bestDistanceSquared &&
          !shotBlocked(combat.currentStage, originX, originY, dx, dy)) {
        bestDistanceSquared = distanceSquared;
        bestTarget = i;
        bestDx = dx;
        bestDy = dy;
        targetX = enemy.x;
        targetY = enemy.y;
      }
    }
    if (!bossUsed && combat.currentStage == BOSS_STAGE &&
        combat.boss.hpFifths && combat.spawnTimer == 0) {
      const int16_t dx = shortestDelta(originX, combat.boss.x, ARENA_WIDTH_FIXED);
      const int16_t dy = shortestDelta(originY, combat.boss.y, ARENA_HEIGHT_FIXED);
      const uint32_t distanceSquared = static_cast<int32_t>(dx) * dx +
                                       static_cast<int32_t>(dy) * dy;
      if (distanceSquared < bestDistanceSquared &&
          !shotBlocked(BOSS_STAGE, originX, originY, dx, dy)) {
        bestTarget = BOSS_HIT;
        bestDx = dx;
        bestDy = dy;
        targetX = combat.boss.x;
        targetY = combat.boss.y;
      }
    }
    if (bestTarget == NO_TARGET) break;
    if (bestTarget == BOSS_HIT) bossUsed = true;
    else usedTargets |= uint8_t(1 << bestTarget);
    fired |= createProjectile(combat, originX, originY,
                              directionFor(bestDx, bestDy), NormalDamage);
    if (combat.recursiveTicks) {
      const int16_t leftX = wrapCoordinate(originX - 8 * FIXED_ONE,
                                            ARENA_WIDTH_FIXED);
      const int16_t rightX = wrapCoordinate(originX + 8 * FIXED_ONE,
                                             ARENA_WIDTH_FIXED);
      createProjectile(combat, leftX, originY,
                        directionFor(shortestDelta(leftX, targetX, ARENA_WIDTH_FIXED),
                                     shortestDelta(originY, targetY, ARENA_HEIGHT_FIXED)),
                        HalfDamage);
      createProjectile(combat, rightX, originY,
                        directionFor(shortestDelta(rightX, targetX, ARENA_WIDTH_FIXED),
                                     shortestDelta(originY, targetY, ARENA_HEIGHT_FIXED)),
                        HalfDamage);
    }
  }
  if (fired) {
    const uint8_t rate = 3 + overclockLevel;
    combat.shotCooldown = (SHOT_INTERVAL * 3 + rate - 1) / rate;
  }
}

void spawnCurrentWave(Combat &combat, int16_t playerX, int16_t playerY) {
  const uint8_t enemyCount =
      getWaveEnemyCount(combat.currentStage, combat.currentWave);
  // Занятый маркер задерживает всю волну, но не меняет её позиции.
  for (uint8_t i = 0; i < enemyCount && i < MAX_ENEMIES; ++i) {
    if (readWaveEnemy(combat.currentStage, combat.currentWave, i).type ==
        EnemyType::None)
      continue;
    uint8_t x, y;
    getSpawnPixel(combat.currentStage, combat.currentWave, i, x, y);
    if (!enemyPositionValid(combat.currentStage, x * FIXED_ONE, y * FIXED_ONE) ||
        enemyOverlapsPlayer(x * FIXED_ONE, y * FIXED_ONE, playerX, playerY,
                            true)) {
      combat.spawnTimer = 2;
      return;
    }
  }
  combat.spawnTimer = 0;
  // Очищаем всех врагов
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    setEnemyType(combat.enemies[i], EnemyType::None);
  }

  // Спавним врагов из данных волны на детерминированных случайных позициях
  for (uint8_t i = 0; i < enemyCount && i < MAX_ENEMIES; ++i) {
    const WaveEnemy waveEnemy =
        readWaveEnemy(combat.currentStage, combat.currentWave, i);

    if (waveEnemy.type != EnemyType::None) {
      uint8_t px = 0;
      uint8_t py = 0;
      getSpawnPixel(combat.currentStage, combat.currentWave, i, px, py);

      spawnEnemy(combat.enemies[i], waveEnemy.type, px * FIXED_ONE,
                 py * FIXED_ONE, waveEnemy.hp, combat.currentStage);
    }
  }

  combat.waveCompleted = false;
}

// Детерминированная случайная позиция спавна i-го врага волны.
// Та же самая для индикаторов и самого спавна: seed зависит только от волны.
void getSpawnPixel(uint8_t stage, uint8_t wave, uint8_t index, uint8_t &x,
                   uint8_t &y) {
  uint32_t state = 0x9E3779B9u;
  state ^= static_cast<uint32_t>(stage) * 0x9E37u;
  state ^= static_cast<uint32_t>(wave) * 0x51EDu;
  state ^= static_cast<uint32_t>(index) * 0xE7u;

  // Ищем свободную от препятствий точку: детерминированный xorshift
  for (uint8_t attempt = 0; attempt < 28; ++attempt) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    const uint8_t px = static_cast<uint8_t>(state % ARENA_WIDTH);
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    const uint8_t py = static_cast<uint8_t>(state % ARENA_HEIGHT);

    if (enemyPositionValid(stage, px * FIXED_ONE, py * FIXED_ONE)) {
      x = px;
      y = py;
      return;
    }
  }

  // Запасной вариант: центр арены (обычно не занят)
  x = ARENA_WIDTH / 2;
  y = ARENA_HEIGHT / 2;
}

void finishStage(Combat& combat) {
  for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
    if (combat.scoreOrbs[i].lifetime)
      combat.playerScore += combat.scoreOrbs[i].value;
    combat.scoreOrbs[i].lifetime = 0;
  }
  for (uint8_t i = 0; i < MAX_PROJECTILES; ++i)
    combat.projectiles[i].framesLeft = 0;
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
    setEnemyType(combat.enemies[i], EnemyType::None);
  combat.playerScore += remainingSeconds(combat.stageTimer) *
                        STAGE_TIME_BONUS_MULT;
  combat.stageCleared = true;
  combat.spawnTimer = 0;
}

// Проверка завершения волны и переход к следующей
void checkWaveCompletion(Combat &combat) {
  if (combat.spawnTimer > 0 || combat.stageCleared)
    return;
  if (combat.currentStage == BOSS_STAGE) return;
  // Проверяем, есть ли живые враги
  bool anyAlive = false;
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    if (getEnemyType(combat.enemies[i]) != EnemyType::None) {
      anyAlive = true;
      break;
    }
  }

  if (!anyAlive && !combat.waveCompleted) {
    combat.waveCompleted = true;

    // Бонус за волну
    combat.playerScore += WAVE_CLEAR_BONUS;

    // Переходим к следующей волне
    ++combat.currentWave;

    // Проверяем, есть ли ещё волны в текущем стейдже
    const uint8_t totalWaves = getStageWaveCount(combat.currentStage);

    if (combat.currentWave >= totalWaves) {
      finishStage(combat);
      return;
    }

    // Задержка перед спавном следующей волны (показываем индикаторы)
    combat.spawnTimer = SPAWN_DELAY_FRAMES;
  }
}

void damageBossFifths(Combat& combat, uint8_t damage) {
  if (!combat.boss.hpFifths || !damage) return;
  if (combat.boss.hpFifths > damage) {
    combat.boss.hpFifths -= damage;
    return;
  }
  combat.boss.hpFifths = 0;
  combat.playerScore += BOSS_SCORE;
  combat.audioEvents |= AUDIO_EVENT_ENEMY_DEATH;
  finishStage(combat);
}

void damageEnemyFifths(Combat &combat, uint8_t index, uint8_t damage) {
  if (index >= MAX_ENEMIES || damage == 0)
    return;
  Enemy &enemy = combat.enemies[index];
  const EnemyType type = getEnemyType(enemy);
  if (type == EnemyType::None)
    return;
  const uint8_t total = getDamageRemainder(enemy) + damage;
  const uint8_t wholeDamage = total / 5;
  setDamageRemainder(enemy, total % 5);
  if (wholeDamage == 0) return;
  if (getEnemyHp(enemy) > wholeDamage) {
    setEnemyHp(enemy, getEnemyHp(enemy) - wholeDamage);
    return;
  }
  const uint8_t size = getSplitLevel(enemy);
  const int16_t x = enemy.x, y = enemy.y;
  const uint8_t value = getEnemyScoreValue(type, size);
  combat.audioEvents |= AUDIO_EVENT_ENEMY_DEATH;
  bool freeOrb = false;
  for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i)
    freeOrb |= combat.scoreOrbs[i].lifetime == 0;
  if (freeOrb)
    createScoreOrb(combat.scoreOrbs, x, y, value);
  else
    combat.playerScore += value;
  setEnemyType(enemy, EnemyType::None);
  if (type == EnemyType::Splitter && size > 1) {
    uint8_t spawned = 0;
    for (uint8_t i = 0; i < MAX_ENEMIES && spawned < 2; ++i) {
      if (getEnemyType(combat.enemies[i]) == EnemyType::None) {
        spawnEnemy(combat.enemies[i], type, x, y, size / 2,
               combat.currentStage);
        ++spawned;
      }
    }
  }
}

void damageEnemy(Combat &combat, uint8_t index, uint8_t damage) {
  damageEnemyFifths(combat, index, damage * 5);
}

// Получение стоимости врага в очках
uint8_t getEnemyScoreValue(EnemyType type, uint8_t hp) {
  switch (type) {
  case EnemyType::Basic:
    return 5; // Фиксированная стоимость
  case EnemyType::Splitter:
    return hp; // Стоимость = текущий HP
  case EnemyType::Fast:
    return 15; // Сложная цель
  default:
    return 1;
  }
}

} // namespace gc
