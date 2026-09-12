#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/combat.cpp"
#include "combat.h"

#include "arena.h"
#include "enemies.h"
#include "stages.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

namespace gc {
namespace {

// Стартовые позиции врагов на арене (flash/PROGMEM, экономим SRAM)
const uint8_t dummyPositions[DUMMY_COUNT][2]
#ifdef __AVR__
    PROGMEM
#endif
    = {{64, 7}, {88, 45}, {120, 3}};

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
    const uint8_t velocity = (static_cast<uint32_t>(magnitude) * PROJECTILE_SPEED
                             + length / 2) / length;
    return delta < 0 ? -velocity : velocity;
}

// Проверяем, попал ли отрезок (x,y)→(x+dx,y+dy) во что-то.
// Возвращает индекс врага, WALL_HIT или NO_HIT. Стены имеют приоритет; урона здесь нет.
int8_t findHit(const Combat& combat, int16_t x, int16_t y, int16_t dx, int16_t dy) {
    if (shotBlocked(x, y, dx, dy)) {
        return WALL_HIT;
    }
    for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
        const Dummy& dummy = combat.dummies[i];
        if (getHp(dummy) == 0) {
            continue;
        }
        const Obstacle hitbox = {dummy.x, dummy.y, DUMMY_SIZE, DUMMY_SIZE};
        if (segmentHitsBox(x, y, dx, dy, hitbox)) {
            return i;
        }
    }
    return NO_HIT;
}

// Двигаем активную пулю шагами не больше пикселя. Попадание освобождает её слот.
int8_t advanceProjectile(Projectile& projectile, const Combat& combat) {
    int8_t previousX = 0;
    int8_t previousY = 0;
    for (uint8_t step = 1; step <= PROJECTILE_STEPS; ++step) {
        const int8_t partialX = static_cast<int16_t>(projectile.velocityX) * step
                               / PROJECTILE_STEPS;
        const int8_t partialY = static_cast<int16_t>(projectile.velocityY) * step
                               / PROJECTILE_STEPS;
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

} // внутренние функции модуля

// Предварительный прототип: проверка позиции от препятствий (используется при спавне)
bool isPositionValid(int16_t x, int16_t y);

// Сброс боевой системы: очищаем всё, возвращаем врагов на стартовые позиции.
void resetCombat(Combat& combat) {
    combat = {};
    
    // Инициализация новой системы врагов
    combat.enemyRandomState = 0x12345678u;
    combat.playerScore = 0;
    combat.currentStage = 0;
    combat.currentWave = 0;
    combat.waveCompleted = false;
    
    // Предупреждение перед первой волной
    combat.spawnTimer = SPAWN_DELAY_FRAMES;
}

// Обновление боевой системы на текущий кадр.
void updateCombat(Combat& combat, int16_t playerX, int16_t playerY) {
    if (combat.shotCooldown > 0) {
        --combat.shotCooldown;
    }

    const int16_t originX = wrapCoordinate(playerX + PLAYER_SIZE * FIXED_ONE / 2,
                                            ARENA_WIDTH_FIXED);
    const int16_t originY = wrapCoordinate(playerY + PLAYER_SIZE * FIXED_ONE / 2,
                                            ARENA_HEIGHT_FIXED);

    // Отсчёт таймера спавна волны
    if (combat.spawnTimer > 0) {
        --combat.spawnTimer;
        if (combat.spawnTimer == 0) {
            spawnCurrentWave(combat, originX, originY);
        }
    }

    // Обновляем новую систему врагов
    updateEnemies(combat.enemies, combat.scoreOrbs,
                  originX, originY, combat.enemyRandomState);

    // Проверяем завершение волны
    checkWaveCompletion(combat);

    // Существующие пули обновляем до создания новой: порядок слотов не влияет на полёт.
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
        Projectile& projectile = combat.projectiles[i];
        if (projectile.framesLeft == 0) {
            continue;
        }
        const int8_t hit = advanceProjectile(projectile, combat);
        if (hit >= 0) {
            Dummy& dummy = combat.dummies[hit];
            const uint8_t hp = getHp(dummy);
            // Проверяем смерть до вычитания: без этого unsigned HP может переполниться.
            if (hp <= SHOT_DAMAGE) {
                setHpAndFlash(dummy, 0, 0);
                dummy.respawnFrames = DUMMY_RESPAWN_FRAMES;
            } else {
                setHpAndFlash(dummy, hp - SHOT_DAMAGE, HIT_FLASH_FRAMES);
            }
        }
    }
    
    // Проверяем попадания пуль по новым врагам
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
        Projectile& projectile = combat.projectiles[i];
        if (projectile.framesLeft == 0) {
            continue;
        }
        
        // Проверяем попадание по обычным врагам
        for (uint8_t j = 0; j < MAX_ENEMIES; ++j) {
            Enemy& enemy = combat.enemies[j];
            if (getEnemyType(enemy) == EnemyType::None) {
                continue;
            }
            
            const int8_t result = checkEnemyHit(enemy, projectile.x, projectile.y);
            if (result > 0) {
                // Враг убит
                const EnemyType type = getEnemyType(enemy);
                // К моменту смерти HP выстрелами сбито до 1, поэтому для
                // Splitter используем размер из splitLevel (аналог Minecraft-слизня).
                const uint8_t size = (type == EnemyType::Splitter)
                                     ? enemy.splitLevel : getEnemyHp(enemy);
                
                // Создаём дроп очков
                const uint8_t scoreValue = getEnemyScoreValue(type, size);
                createScoreOrb(combat.scoreOrbs, enemy.x, enemy.y, scoreValue);
                
                // Обрабатываем деление для Splitter: делим размер пополам (до 1 HP).
                // Потомки появляются сразу на позиции родителя, расталкивание разведёт их.
                if (type == EnemyType::Splitter && size > 1) {
                    // Сохраняем координаты до обнуления
                    const int16_t parentX = enemy.x;
                    const int16_t parentY = enemy.y;
                    const uint8_t childHp = size / 2;
                    
                    // Сначала обнуляем родителя
                    setEnemyType(enemy, EnemyType::None);
                    
                    // Создаём двух потомков в точку смерти
                    uint8_t spawned = 0;
                    for (uint8_t k = 0; k < MAX_ENEMIES && spawned < 2; ++k) {
                        if (getEnemyType(combat.enemies[k]) == EnemyType::None) {
                            spawnEnemy(combat.enemies[k], EnemyType::Splitter,
                                      parentX, parentY, childHp);
                            ++spawned;
                        }
                    }
                } else {
                    setEnemyType(enemy, EnemyType::None);
                }
                
                projectile.framesLeft = 0;
                break;
            } else if (result == 0) {
                // Попадание без смерти
                projectile.framesLeft = 0;
                break;
            }
        }
    }
    
    // Собираем сферы очков
    const uint8_t collectedScore = collectOrbs(combat.scoreOrbs, originX, originY);
    combat.playerScore += collectedScore;

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

    const uint32_t rangeSquared = static_cast<uint32_t>(SHOT_RANGE_FIXED) * SHOT_RANGE_FIXED;
    
    // Ищем ближайшего врага (только новая система)
    uint8_t bestTarget = 255;
    uint32_t bestDistanceSquared = rangeSquared + 1;
    int16_t bestDx = 0;
    int16_t bestDy = 0;
    
    // Проверяем новых врагов
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        const Enemy& enemy = combat.enemies[i];
        if (getEnemyType(enemy) == EnemyType::None) {
            continue;
        }
        
        const int16_t targetX = wrapCoordinate(enemy.x, ARENA_WIDTH_FIXED);
        const int16_t targetY = wrapCoordinate(enemy.y, ARENA_HEIGHT_FIXED);
        const int16_t candidateX = shortestDelta(originX, targetX, ARENA_WIDTH_FIXED);
        const int16_t candidateY = shortestDelta(originY, targetY, ARENA_HEIGHT_FIXED);
        const uint32_t distanceSquared = static_cast<int32_t>(candidateX) * candidateX
                                         + static_cast<int32_t>(candidateY) * candidateY;
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestTarget = i;
            bestDx = candidateX;
            bestDy = candidateY;
        }
    }
    
    if (bestTarget == 255) {
        return; // Нет целей в радиусе
    }
    
    if (shotBlocked(originX, originY, bestDx, bestDy)) {
        return; // Стена блокирует
    }

    const uint16_t length = aimLength(bestDistanceSquared);
    Projectile shot = {originX, originY, aimVelocity(bestDx, length),
                       aimVelocity(bestDy, length), PROJECTILE_LIFETIME};
    if (bestDx == 0 && bestDy == 0) {
        shot.velocityX = PROJECTILE_SPEED; // Враг в точке спавна
    }
    
    combat.projectiles[freeSlot] = shot;
    combat.shotCooldown = SHOT_INTERVAL;
}
void spawnCurrentWave(Combat& combat, int16_t playerX, int16_t playerY) {
    // Очищаем всех врагов
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        setEnemyType(combat.enemies[i], EnemyType::None);
    }
    
    // Получаем количество врагов в текущей волне
    const uint8_t enemyCount = getWaveEnemyCount(combat.currentStage, combat.currentWave);
    
    // Спавним врагов из данных волны на детерминированных случайных позициях
    for (uint8_t i = 0; i < enemyCount && i < MAX_ENEMIES; ++i) {
        const WaveEnemy waveEnemy = readWaveEnemy(combat.currentStage, combat.currentWave, i);
        
        if (waveEnemy.type != EnemyType::None) {
            uint8_t px = 0;
            uint8_t py = 0;
            getSpawnPixel(combat.currentStage, combat.currentWave, i, px, py);
            
            const int16_t spawnX = px * FIXED_ONE;
            const int16_t spawnY = py * FIXED_ONE;
            
            // Если позиция слишком близко к игроку, отодвигаем врага в сторону
            const int16_t dx = shortestDelta(spawnX, playerX, ARENA_WIDTH_FIXED);
            const int16_t dy = shortestDelta(spawnY, playerY, ARENA_HEIGHT_FIXED);
            const int32_t distSq = static_cast<int32_t>(dx) * dx
                                   + static_cast<int32_t>(dy) * dy;
            const int16_t safeDist = 14 * FIXED_ONE;
            
            if (distSq < static_cast<int32_t>(safeDist) * safeDist) {
                int16_t moveX = dx < 0 ? safeDist : (dx > 0 ? -safeDist : 0);
                int16_t moveY = dy < 0 ? safeDist : (dy > 0 ? -safeDist : 0);
                if (moveX == 0 && moveY == 0) {
                    moveY = -safeDist; // Спавн точно в игроке: уходим вверх
                }
                const int16_t nudgeX = wrapCoordinate(spawnX + moveX, ARENA_WIDTH_FIXED);
                const int16_t nudgeY = wrapCoordinate(spawnY + moveY, ARENA_HEIGHT_FIXED);
                if (isPositionValid(nudgeX, nudgeY)) {
                    px = nudgeX / FIXED_ONE;
                    py = nudgeY / FIXED_ONE;
                }
            }
            
            spawnEnemy(combat.enemies[i], waveEnemy.type,
                       px * FIXED_ONE, py * FIXED_ONE, waveEnemy.hp);
        }
    }
    
    combat.waveCompleted = false;
}

// Детерминированная случайная позиция спавна i-го врага волны.
// Та же самая для индикаторов и самого спавна: seed зависит только от волны.
void getSpawnPixel(uint8_t stage, uint8_t wave, uint8_t index, uint8_t& x, uint8_t& y) {
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
        
        if (isPositionValid(px * FIXED_ONE, py * FIXED_ONE)) {
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
void checkWaveCompletion(Combat& combat) {
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
        
        // Переходим к следующей волне
        ++combat.currentWave;
        
        // Проверяем, есть ли ещё волны в текущем стейдже
        const uint8_t totalWaves = getStageWaveCount(combat.currentStage);
        
        if (combat.currentWave >= totalWaves) {
            // Стейдж завершён: готовим следующий, но не спавним его.
            // Игрок видит меню отдыха и продолжает нажатием A/B.
            combat.currentWave = 0;
            ++combat.currentStage;
            if (combat.currentStage >= TOTAL_STAGES) {
                combat.currentStage = 0; // Игра зацикливается
            }
            combat.stageCleared = true;
            combat.spawnTimer = 0;
            return;
        }
        
        // Задержка перед спавном следующей волны (показываем индикаторы)
        combat.spawnTimer = SPAWN_DELAY_FRAMES;
    }
}

// Проверка, свободна ли позиция от препятствий
bool isPositionValid(int16_t x, int16_t y) {
    const int16_t pixelX = x / FIXED_ONE;
    const int16_t pixelY = y / FIXED_ONE;
    
    // Проверяем коллизию с препятствиями (враг 12x6, центрированный)
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obs = readObstacle(i);
        if (pixelX - ENEMY_HALF_WIDTH < obs.x + obs.width &&
            pixelX + ENEMY_HALF_WIDTH > obs.x &&
            pixelY - ENEMY_HALF_HEIGHT < obs.y + obs.height &&
            pixelY + ENEMY_HALF_HEIGHT > obs.y) {
            return false;
        }
    }
    return true;
}

// Старые функции удалены - теперь используется система волн из stages.h

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

} // пространство имён gc
