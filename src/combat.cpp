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

// Бинарным поиском находим целочисленную длину с округлением вверх, без float.
// Минимум 1 защищает деление при совпадении центров игрока и цели.
uint16_t aimLength(uint32_t distanceSquared) {
  uint16_t low = 1;
  uint16_t high = SHOT_RANGE_FIXED;
  while (low < high) {
    const uint16_t middle = (low + high) / 2;
    if (static_cast<uint32_t>(middle) * middle < distanceSquared) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

// Считаем скорость пули по одной оси (нормализуем вектор без float).
int8_t aimVelocity(int16_t delta, uint16_t length) {
  const uint16_t magnitude = delta < 0 ? -delta : delta;
  const uint8_t velocity =
      (static_cast<uint32_t>(magnitude) * PROJECTILE_SPEED + length / 2) /
      length;
  return delta < 0 ? -velocity : velocity;
}

// Проверяем, попал ли отрезок (x,y)→(x+dx,y+dy) во что-то.
// Возвращает индекс врага, WALL_HIT или NO_HIT. Стены имеют приоритет; урона
// здесь нет.
int8_t findHit(const Combat &combat, int16_t x, int16_t y, int16_t dx,
               int16_t dy) {
  if (shotBlocked(combat.currentStage, x, y, dx, dy)) {
    return WALL_HIT;
  }
  for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
    const Dummy &dummy = combat.dummies[i];
    if (getHp(dummy) == 0) {
      continue;
    }
    const Obstacle hitbox = {dummy.x, dummy.y, DUMMY_SIZE, DUMMY_SIZE};
    if (segmentHitsBox(x, y, dx, dy, hitbox)) {
      return i;
    }
  }
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    const Enemy &enemy = combat.enemies[i];
    if (getEnemyType(enemy) == EnemyType::None)
      continue;
    const Obstacle box = {
        uint8_t(wrapCoordinate(enemy.x / FIXED_ONE - ENEMY_HALF_WIDTH,
                               ARENA_WIDTH)),
        uint8_t(wrapCoordinate(enemy.y / FIXED_ONE - ENEMY_HALF_HEIGHT,
                               ARENA_HEIGHT)),
        2 * ENEMY_HALF_WIDTH, 2 * ENEMY_HALF_HEIGHT};
    if (segmentHitsBox(x, y, dx, dy, box))
      return DUMMY_COUNT + i;
  }
  return NO_HIT;
}

// Двигаем активную пулю шагами не больше пикселя. Попадание освобождаёт её
// слот.
int8_t advanceProjectile(Projectile &projectile, const Combat &combat) {
  int8_t previousX = 0;
  int8_t previousY = 0;
  for (uint8_t step = 1; step <= PROJECTILE_STEPS; ++step) {
    const int8_t partialX =
        static_cast<int16_t>(projectile.velocityX) * step / PROJECTILE_STEPS;
    const int8_t partialY =
        static_cast<int16_t>(projectile.velocityY) * step / PROJECTILE_STEPS;
    const int8_t dx = partialX - previousX;
    const int8_t dy = partialY - previousY;
    previousX = partialX;
    previousY = partialY;

    const int8_t hit = findHit(combat, projectile.x, projectile.y, dx, dy);
    if (hit != NO_HIT) {
      projectile.framesLeft = 0;
      return hit;
    }
    projectile.x = wrapCoordinate(projectile.x + dx, ARENA_WIDTH_FIXED);
    projectile.y = wrapCoordinate(projectile.y + dy, ARENA_HEIGHT_FIXED);
  }
  --projectile.framesLeft;
  return NO_HIT;
}

} // namespace

// Сброс боевой системы: очищаем всё, возвращаем врагов на стартовые позиции.
void resetCombat(Combat &combat) {
  combat = {};

  // Инициализация новой системы врагов
  combat.enemyRandomState = 0x12345678u;
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
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    const Enemy &enemy = combat.enemies[i];
    if (getEnemyType(enemy) == EnemyType::None) {
      continue;
    }
    // Player coordinates are top-left; enemies store centers.
    const int16_t playerCenterX = playerX + (PLAYER_SIZE * FIXED_ONE) / 2;
    const int16_t playerCenterY = playerY + (PLAYER_SIZE * FIXED_ONE) / 2;
    if (enemyOverlapsPlayer(enemy.x, enemy.y, playerCenterX, playerCenterY,
                            touching)) {
      return true;
    }
  }
  return false;
}

// Обновление боевой системы на текущий кадр.
void updateCombat(Combat &combat, int16_t playerX, int16_t playerY,
                  uint8_t damage) {
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

  // Обновляем новую систему врагов
  if (combat.freezeFrames > 0) {
    --combat.freezeFrames;
  } else {
    updateEnemies(combat.enemies, combat.scoreOrbs, originX, originY,
                  combat.currentStage, combat.enemyRandomState);
  }

  // Новые враги первый кадр остаются точно на индикаторах.
  if (combat.spawnTimer > 0 && --combat.spawnTimer == 0) {
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
    if (hit >= DUMMY_COUNT) {
      damageEnemy(combat, hit - DUMMY_COUNT, damage);
    } else if (hit >= 0) {
      Dummy &dummy = combat.dummies[hit];
      const uint8_t hp = getHp(dummy);
      // Проверяем смерть до вычитания: без этого unsigned HP может
      // переполниться.
      if (hp <= SHOT_DAMAGE) {
        setHpAndFlash(dummy, 0, 0);
        dummy.respawnFrames = DUMMY_RESPAWN_FRAMES;
      } else {
        setHpAndFlash(dummy, hp - SHOT_DAMAGE, HIT_FLASH_FRAMES);
      }
    }
  }

  // Собираем сферы очков
  const uint8_t collectedScore =
      collectOrbs(combat.scoreOrbs, originX, originY);
  combat.playerScore += collectedScore;
  if (collectedScore) combat.audioEvents |= AUDIO_EVENT_COIN;
  checkWaveCompletion(combat);
  if (combat.stageCleared)
    return;

  // Автострельба: кулдаун тратится только после успешного создания пули.
  if (combat.shotCooldown != 0) {
    return;
  }
  uint8_t freeSlot = MAX_PROJECTILES;
  for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
    if (combat.projectiles[i].framesLeft == 0) {
      freeSlot = i;
      break;
    }
  }
  if (freeSlot == MAX_PROJECTILES) {
    return; // Пул забит, пропускаем выстрел
  }

  const uint32_t rangeSquared =
      static_cast<uint32_t>(SHOT_RANGE_FIXED) * SHOT_RANGE_FIXED;

  // Ищем ближайшего врага (только новая система)
  uint8_t bestTarget = 255;
  uint32_t bestDistanceSquared = rangeSquared + 1;
  int16_t bestDx = 0;
  int16_t bestDy = 0;

  // Проверяем новых врагов
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    const Enemy &enemy = combat.enemies[i];
    if (getEnemyType(enemy) == EnemyType::None) {
      continue;
    }

    const int16_t targetX = wrapCoordinate(enemy.x, ARENA_WIDTH_FIXED);
    const int16_t targetY = wrapCoordinate(enemy.y, ARENA_HEIGHT_FIXED);
    const int16_t candidateX =
        shortestDelta(originX, targetX, ARENA_WIDTH_FIXED);
    const int16_t candidateY =
        shortestDelta(originY, targetY, ARENA_HEIGHT_FIXED);
    const uint32_t distanceSquared =
        static_cast<int32_t>(candidateX) * candidateX +
        static_cast<int32_t>(candidateY) * candidateY;
    if (distanceSquared < bestDistanceSquared &&
        !shotBlocked(combat.currentStage, originX, originY, candidateX,
                     candidateY)) {
      bestDistanceSquared = distanceSquared;
      bestTarget = i;
      bestDx = candidateX;
      bestDy = candidateY;
    }
  }

  if (bestTarget == 255) {
    return; // Нет целей в радиусе
  }

  const uint16_t length = aimLength(bestDistanceSquared);
  Projectile shot = {originX, originY, aimVelocity(bestDx, length),
                     aimVelocity(bestDy, length), PROJECTILE_LIFETIME};
  if (bestDx == 0 && bestDy == 0) {
    shot.velocityX = PROJECTILE_SPEED; // Враг в точке спавна
  }

  combat.projectiles[freeSlot] = shot;
  // Очередь выстрелов: первый пуля захода ставит короткую паузу до следующей,
  // последняя пуля очереди — полный SHOT_INTERVAL до следующего захода.
  // burstShots считает уже выпущенные пули текущей очереди (0 = новый заход).
  ++combat.burstShots;
  if (combat.burstShots >= SHOT_BURST_COUNT) {
    combat.burstShots = 0;
    combat.shotCooldown = SHOT_INTERVAL;
  } else {
    combat.shotCooldown = SHOT_BURST_DELAY;
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

// Проверка завершения волны и переход к следующей
void checkWaveCompletion(Combat &combat) {
  if (combat.spawnTimer > 0 || combat.stageCleared)
    return;
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
      // Bank remaining drops before leaving; no rewards disappear in the shop.
      for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        if (combat.scoreOrbs[i].lifetime)
          combat.playerScore += combat.scoreOrbs[i].value;
        combat.scoreOrbs[i].lifetime = 0;
      }
      for (uint8_t i = 0; i < MAX_PROJECTILES; ++i)
        combat.projectiles[i].framesLeft = 0;
      // Стейдж завершён: бонус за оставшееся время
      const uint16_t timeBonus =
          remainingSeconds(combat.stageTimer) * STAGE_TIME_BONUS_MULT;
      combat.playerScore += timeBonus;

      // Keep this stage's index/timer for its results. Shop advances the index.
      combat.stageCleared = true;
      combat.spawnTimer = 0;
      return;
    }

    // Задержка перед спавном следующей волны (показываем индикаторы)
    combat.spawnTimer = SPAWN_DELAY_FRAMES;
  }
}

void damageEnemy(Combat &combat, uint8_t index, uint8_t damage) {
  if (index >= MAX_ENEMIES || damage == 0)
    return;
  Enemy &enemy = combat.enemies[index];
  const EnemyType type = getEnemyType(enemy);
  if (type == EnemyType::None)
    return;
  if (getEnemyHp(enemy) > damage) {
    setEnemyHp(enemy, getEnemyHp(enemy) - damage);
    return;
  }
  const uint8_t size = enemy.splitLevel;
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
