#include "combat.h"

#include "arena.h"

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

// На локальной копии проверяем реальную траекторию с округлённой скоростью:
// она может задеть угол стены даже при свободной идеальной прямой до центра цели.
// Используем движение настоящей пули, но не наносим урон и не занимаем слот пула.
bool canReachTarget(Projectile preview, const Combat& combat, uint8_t target) {
    for (uint8_t frame = 0; frame < AIM_PREVIEW_FRAMES; ++frame) {
        const int8_t hit = advanceProjectile(preview, combat);
        if (hit != NO_HIT) {
            return hit == static_cast<int8_t>(target);
        }
    }
    return false;
}

} // внутренние функции модуля

// Сброс боевой системы: очищаем всё, возвращаем врагов на стартовые позиции.
void resetCombat(Combat& combat) {
    combat = {};
    for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
        Dummy& dummy = combat.dummies[i];
#ifdef __AVR__
        dummy.x = pgm_read_byte(&dummyPositions[i][0]);
        dummy.y = pgm_read_byte(&dummyPositions[i][1]);
#else
        dummy.x = dummyPositions[i][0];
        dummy.y = dummyPositions[i][1];
#endif
        setHpAndFlash(dummy, DUMMY_MAX_HP, 0);
    }
}

// Обновление боевой системы на текущий кадр.
void updateCombat(Combat& combat, int16_t playerX, int16_t playerY) {
    // Сначала уменьшаем старые таймеры. Таймер от нового попадания начнёт отсчёт позже.
    for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
        Dummy& dummy = combat.dummies[i];
        const uint8_t flash = getHitFlash(dummy);
        if (flash > 0) {
            setHitFlash(dummy, flash - 1);
        }
        if (getHp(dummy) == 0 && dummy.respawnFrames > 0) {
            --dummy.respawnFrames;
            if (dummy.respawnFrames == 0) {
                setHp(dummy, DUMMY_MAX_HP);
            }
        }
    }
    if (combat.shotCooldown > 0) {
        --combat.shotCooldown;
    }

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

    const int16_t originX = wrapCoordinate(playerX + PLAYER_SIZE * FIXED_ONE / 2,
                                            ARENA_WIDTH_FIXED);
    const int16_t originY = wrapCoordinate(playerY + PLAYER_SIZE * FIXED_ONE / 2,
                                            ARENA_HEIGHT_FIXED);
    const uint32_t rangeSquared = static_cast<uint32_t>(SHOT_RANGE_FIXED) * SHOT_RANGE_FIXED;
    uint8_t triedTargets = 0;

    // Проверяем ближайших первыми: симуляция полёта дороже сравнения расстояний.
    // В triedTargets бит i означает, что цель i уже отклонена в этом кадре.
    for (uint8_t attempt = 0; attempt < DUMMY_COUNT; ++attempt) {
        uint8_t target = DUMMY_COUNT;
        uint32_t nearestDistanceSquared = rangeSquared + 1;
        int16_t dx = 0;
        int16_t dy = 0;
        for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
            const Dummy& dummy = combat.dummies[i];
            if (getHp(dummy) == 0 || (triedTargets & (1 << i)) != 0) {
                continue;
            }
            const int16_t targetX = wrapCoordinate(dummy.x * FIXED_ONE + DUMMY_SIZE * FIXED_ONE / 2,
                                                    ARENA_WIDTH_FIXED);
            const int16_t targetY = wrapCoordinate(dummy.y * FIXED_ONE + DUMMY_SIZE * FIXED_ONE / 2,
                                                    ARENA_HEIGHT_FIXED);
            const int16_t candidateX = shortestDelta(originX, targetX, ARENA_WIDTH_FIXED);
            const int16_t candidateY = shortestDelta(originY, targetY, ARENA_HEIGHT_FIXED);
            const uint32_t distanceSquared = static_cast<int32_t>(candidateX) * candidateX
                                             + static_cast<int32_t>(candidateY) * candidateY;
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                target = i;
                dx = candidateX;
                dy = candidateY;
            }
        }
        if (target == DUMMY_COUNT) {
            return; // Нет живых целей в радиусе
        }
        triedTargets |= 1 << target;
        if (shotBlocked(originX, originY, dx, dy)) {
            continue; // Стена блокирует, пробуем следующую цель
        }

        const uint16_t length = aimLength(nearestDistanceSquared);
        Projectile shot = {originX, originY, aimVelocity(dx, length),
                           aimVelocity(dy, length), PROJECTILE_LIFETIME};
        if (dx == 0 && dy == 0) {
            shot.velocityX = PROJECTILE_SPEED; // Враг в точке спавна
        }
        if (canReachTarget(shot, combat, target)) {
            combat.projectiles[freeSlot] = shot;
            combat.shotCooldown = SHOT_INTERVAL;
            return;
        }
    }
}

} // пространство имён gc
