#include "enemies.h"
#include "arena.h"
#include "combat.h"

namespace gc {
namespace {

// Проверка коллизий врага (хитбокс 12x6, центр в centerX, centerY) с
// попиксельными стенами текущего стейджа. Возвращает true, если позиция занята
// стеной. Центр врага не пересекает край арены (без тора для врагов).
bool enemyHitsObstacle(uint8_t stage, int16_t centerX, int16_t centerY) {
    if (centerX < ENEMY_HALF_WIDTH * FIXED_ONE ||
        centerX > ARENA_WIDTH_FIXED - ENEMY_HALF_WIDTH * FIXED_ONE ||
        centerY < ENEMY_HALF_HEIGHT * FIXED_ONE ||
        centerY > ARENA_HEIGHT_FIXED - ENEMY_HALF_HEIGHT * FIXED_ONE) return true;
    const int16_t startX = (centerX - ENEMY_HALF_WIDTH * FIXED_ONE) / FIXED_ONE;
    const int16_t endX = (centerX + ENEMY_HALF_WIDTH * FIXED_ONE - 1) / FIXED_ONE;
    const int16_t startY = (centerY - ENEMY_HALF_HEIGHT * FIXED_ONE) / FIXED_ONE;
    const int16_t endY = (centerY + ENEMY_HALF_HEIGHT * FIXED_ONE - 1) / FIXED_ONE;
    for (int16_t py = startY; py <= endY; ++py) {
        for (int16_t px = startX; px <= endX; ++px) {
            if (stageWallPixel(stage, static_cast<uint8_t>(px),
                               static_cast<uint8_t>(py))) {
                return true;
            }
        }
    }
    return false;
}

// Простое движение к игроку с проверкой коллизий и скольжением вдоль стен
void moveTowardsPlayer(uint8_t stage, Enemy& enemy, int16_t playerX,
                       int16_t playerY, uint8_t speed, bool reversed) {
    const int16_t oldX = enemy.x;
    const int16_t oldY = enemy.y;
    const int16_t dx = (playerX - enemy.x) * (reversed ? -1 : 1);
    const int16_t dy = (playerY - enemy.y) * (reversed ? -1 : 1);
    
    if (dx == 0 && dy == 0) {
        return;
    }
    
    // Простая нормализация без sqrt
    const int16_t absDx = dx < 0 ? -dx : dx;
    const int16_t absDy = dy < 0 ? -dy : dy;
    
    int8_t vx = 0;
    int8_t vy = 0;
    
    if (absDx > absDy) {
        vx = dx > 0 ? speed : -speed;
        if (absDy > 0) {
            vy = dy > 0 ? speed / 2 : -speed / 2;
        }
    } else {
        vy = dy > 0 ? speed : -speed;
        if (absDx > 0) {
            vx = dx > 0 ? speed / 2 : -speed / 2;
        }
    }
    
    // Пробуем двигаться по X
    const int16_t newX = enemy.x + vx;
    const bool blockedX = enemyHitsObstacle(stage, newX, enemy.y);
    
    // Пробуем двигаться по Y
    const int16_t newY = enemy.y + vy;
    const bool blockedY = enemyHitsObstacle(stage, enemy.x, newY);
    
    if (!blockedX) {
        enemy.x = newX;
    }
    if (!blockedY) {
        enemy.y = newY;
    }
    
    // Если оба направления заблокированы, пробуем скольжение перпендикулярно стене
    if (blockedX && blockedY && (vx != 0 || vy != 0)) {
        const int16_t slideX = enemy.x + vy;
        if (!enemyHitsObstacle(stage, slideX, enemy.y)) {
            enemy.x = slideX;
        } else {
            const int16_t slideY = enemy.y + vx;
            if (!enemyHitsObstacle(stage, enemy.x, slideY)) {
                enemy.y = slideY;
            }
        }
    }
    if (enemyOverlapsPlayer(enemy.x, enemy.y, playerX, playerY) || enemyHitsObstacle(stage, enemy.x, enemy.y)) {
        enemy.x = oldX;
        enemy.y = oldY;
    }
}

} // anonymous namespace

bool enemyPositionValid(uint8_t stage, int16_t x, int16_t y) {
    return !enemyHitsObstacle(stage, x, y);
}

bool enemyOverlapsPlayer(int16_t x, int16_t y, int16_t px, int16_t py, bool touching) {
    const int16_t dx = shortestDelta(px, x, ARENA_WIDTH_FIXED);
    const int16_t dy = shortestDelta(py, y, ARENA_HEIGHT_FIXED);
    // One movement step of contact tolerance lets blocked pursuit still hurt.
    const int16_t margin = touching ? FIXED_ONE + 1 : 0;
    const int16_t w = ENEMY_HALF_WIDTH * FIXED_ONE + PLAYER_SIZE * FIXED_ONE / 2 + margin;
    const int16_t h = ENEMY_HALF_HEIGHT * FIXED_ONE + PLAYER_SIZE * FIXED_ONE / 2 + margin;
    return dx > -w && dx < w && dy > -h && dy < h;
}

// Спавн врага
void spawnEnemy(Enemy& enemy, EnemyType type, int16_t x, int16_t y, uint8_t variant, uint8_t stage) {
    enemy.x = x;
    enemy.y = y;
    enemy.splitLevel = 0;
    if (!enemyPositionValid(stage, x, y)) {
        setEnemyTypeAndHp(enemy, EnemyType::None, 0);
        return;
    }
    
    switch (type) {
    case EnemyType::Basic:
        // variant используется как seed для рандома: HP от 2 до 10
        setEnemyTypeAndHp(enemy, EnemyType::Basic, 2 + (variant % 9));
        break;
        
    case EnemyType::Splitter:
        // variant содержит HP напрямую (для деления пополам).
        // splitLevel хранит размер родителя: HP сам падает от выстрелов до 1,
        // а после смерти потомки наследуют splitLevel/2.
        setEnemyTypeAndHp(enemy, EnemyType::Splitter, variant);
        setSplitLevel(enemy, variant);
        break;
        
    case EnemyType::Fast:
        // variant используется как seed для рандома: HP от 10 до 15
        setEnemyTypeAndHp(enemy, EnemyType::Fast, 10 + (variant % 6));
        break;
        
    default:
        setEnemyTypeAndHp(enemy, EnemyType::None, 0);
        break;
    }
}

// Обновление всех врагов
void updateEnemies(Enemy enemies[MAX_ENEMIES], ScoreOrb orbs[MAX_SCORE_ORBS],
                   int16_t playerX, int16_t playerY, uint8_t stage,
                   bool slowed, bool reversed) {
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        Enemy& enemy = enemies[i];
        const EnemyType type = getEnemyType(enemy);
        
        if (type == EnemyType::None) {
            continue;
        }
        
        uint8_t speed = BASIC_SPEED;
        
        switch (type) {
        case EnemyType::Basic:
            speed = BASIC_SPEED;
            break;
            
        case EnemyType::Splitter:
            speed = SPLITTER_SPEED;
            break;
            
        case EnemyType::Fast:
            {
                // Скорость растёт при потере HP
                const uint8_t currentHp = getEnemyHp(enemy);
                const uint8_t lostHp = FAST_MAX_HP - (currentHp > FAST_MAX_HP ? FAST_MAX_HP : currentHp);
                speed = FAST_BASE_SPEED + uint16_t(lostHp) * (FAST_MAX_SPEED - FAST_BASE_SPEED) / (FAST_MAX_HP - 1);
                if (speed > FAST_MAX_SPEED) speed = FAST_MAX_SPEED;
            }
            break;
            
        default:
            break;
        }
        
        if (slowed) speed = (speed + 1) / 2;
        moveTowardsPlayer(stage, enemy, playerX, playerY, speed, reversed);
    }
    
    // Расталкивание врагов друг от друга
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        Enemy& enemy1 = enemies[i];
        if (getEnemyType(enemy1) == EnemyType::None) {
            continue;
        }
        
        for (uint8_t j = i + 1; j < MAX_ENEMIES; ++j) {
            Enemy& enemy2 = enemies[j];
            if (getEnemyType(enemy2) == EnemyType::None) {
                continue;
            }
            
            // Проверяем расстояние (враги 6x6 пикселей)
            const int16_t dx = enemy2.x - enemy1.x;
            const int16_t dy = enemy2.y - enemy1.y;
            const int16_t absDx = dx < 0 ? -dx : dx;
            const int16_t absDy = dy < 0 ? -dy : dy;
            const int16_t minDist = 6 * FIXED_ONE;
            
            // Если слишком близко, расталкиваем (только если позиция не внутри стены)
            if (absDx < minDist && absDy < minDist) {
                const int8_t pushX = (dx > 0) ? 2 : -2;
                const int8_t pushY = (dy > 0) ? 2 : -2;
                
                const int16_t newX1 = enemy1.x - pushX;
                const int16_t newY1 = enemy1.y - pushY;
                if (!enemyHitsObstacle(stage, newX1, enemy1.y) && !enemyOverlapsPlayer(newX1, enemy1.y, playerX, playerY)) enemy1.x = newX1;
                if (!enemyHitsObstacle(stage, enemy1.x, newY1) && !enemyOverlapsPlayer(enemy1.x, newY1, playerX, playerY)) enemy1.y = newY1;
                
                const int16_t newX2 = enemy2.x + pushX;
                const int16_t newY2 = enemy2.y + pushY;
                if (!enemyHitsObstacle(stage, newX2, enemy2.y) && !enemyOverlapsPlayer(newX2, enemy2.y, playerX, playerY)) enemy2.x = newX2;
                if (!enemyHitsObstacle(stage, enemy2.x, newY2) && !enemyOverlapsPlayer(enemy2.x, newY2, playerX, playerY)) enemy2.y = newY2;
            }
        }
    }
    
    // Обновляем сферы очков
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        if (orbs[i].lifetime > 0) {
            --orbs[i].lifetime;
        }
    }
}

// Создание дропа очков
void createScoreOrb(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t x, int16_t y, uint8_t value) {
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        if (orbs[i].lifetime == 0) {
            orbs[i].x = wrapCoordinate(x, ARENA_WIDTH_FIXED) / FIXED_ONE;
            orbs[i].y = wrapCoordinate(y, ARENA_HEIGHT_FIXED) / FIXED_ONE;
            orbs[i].value = value;
            orbs[i].lifetime = ORB_LIFETIME;
            return;
        }
    }
}

// Сбор очков
uint8_t collectOrbs(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t playerX, int16_t playerY) {
    uint8_t collected = 0;
    
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        if (orbs[i].lifetime == 0) {
            continue;
        }
        
        // Проверка касания с игроком (радиус сбора)
        const int16_t dx = shortestDelta(playerX, orbs[i].x * FIXED_ONE,
                                         ARENA_WIDTH_FIXED);
        const int16_t dy = shortestDelta(playerY, orbs[i].y * FIXED_ONE,
                                         ARENA_HEIGHT_FIXED);
        const int32_t distSq = static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy;
        const int16_t collectRadius = 10 * FIXED_ONE;
        
        if (distSq < static_cast<int32_t>(collectRadius) * collectRadius) {
            collected += orbs[i].value;
            orbs[i].lifetime = 0;
        }
    }
    
    return collected;
}

void pushEnemyAway(Enemy& enemy, int16_t playerX, int16_t playerY,
                   uint8_t stage, uint8_t distance) {
    int16_t dx = shortestDelta(playerX, enemy.x, ARENA_WIDTH_FIXED);
    int16_t dy = shortestDelta(playerY, enemy.y, ARENA_HEIGHT_FIXED);
    if (dx == 0 && dy == 0) dx = FIXED_ONE;
    const int16_t magnitude = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                                  ? (dx < 0 ? -dx : dx)
                                  : (dy < 0 ? -dy : dy);
    const int8_t stepX = static_cast<int32_t>(dx) * FIXED_ONE / magnitude;
    const int8_t stepY = static_cast<int32_t>(dy) * FIXED_ONE / magnitude;
    for (uint8_t step = 0; step < distance; ++step) {
        const int16_t nextX = enemy.x + stepX;
        const int16_t nextY = enemy.y + stepY;
        bool moved = false;
        if (!enemyHitsObstacle(stage, nextX, enemy.y)) {
            enemy.x = nextX;
            moved = true;
        }
        if (!enemyHitsObstacle(stage, enemy.x, nextY)) {
            enemy.y = nextY;
            moved = true;
        }
        if (!moved) break;
    }
}

namespace {

void stepBossPatrol(Boss& boss) {
    const uint8_t direction = boss.phase / BOSS_PATROL_LEG_LEN;
    if (direction == 0) boss.x += FIXED_ONE;
    else if (direction == 1) boss.y += FIXED_ONE;
    else if (direction == 2) boss.x -= FIXED_ONE;
    else boss.y -= FIXED_ONE;
    boss.phase = static_cast<uint8_t>(
        boss.phase + 1 == BOSS_PATROL_LEG_LEN * 4 ? 0 : boss.phase + 1);
}

void spawnBossMinion(Combat& combat, int16_t x, int16_t y,
                     int16_t playerX, int16_t playerY) {
    if (!enemyPositionValid(BOSS_STAGE, x, y) ||
        enemyOverlapsPlayer(x, y, playerX, playerY, true)) return;
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        if (getEnemyType(combat.enemies[i]) == EnemyType::None) {
            spawnEnemy(combat.enemies[i], EnemyType::Basic, x, y, 0,
                       BOSS_STAGE);
            return;
        }
    }
}

void spawnBossMinions(Combat& combat, int16_t playerX, int16_t playerY) {
    const int16_t x = combat.boss.x, y = combat.boss.y;
    const int16_t gapX = (BOSS_HALF_WIDTH + 5) * FIXED_ONE;
    const int16_t gapY = (BOSS_HALF_HEIGHT + 5) * FIXED_ONE;
    spawnBossMinion(combat, x - gapX, y, playerX, playerY);
    spawnBossMinion(combat, x + gapX, y, playerX, playerY);
    spawnBossMinion(combat, x, y - gapY, playerX, playerY);
    spawnBossMinion(combat, x, y + gapY, playerX, playerY);
}

} // namespace

void activateBoss(Combat& combat) {
    combat.boss = {};
    combat.boss.x = BOSS_PATROL_ANCHOR_X * FIXED_ONE;
    combat.boss.y = BOSS_PATROL_ANCHOR_Y * FIXED_ONE;
    combat.boss.hpFifths = BOSS_MAX_HP * 5;
    combat.boss.waveTimer = BOSS_FIRST_WAVE_DELAY_FRAMES;
    combat.spawnTimer = BOSS_AWAKE_FRAMES;
}

void resetBossStage(Combat& combat) {
    combat.boss = {};
    combat.spawnTimer = SPAWN_DELAY_FRAMES;
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
        setEnemyType(combat.enemies[i], EnemyType::None);
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i)
        combat.projectiles[i].framesLeft = 0;
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i)
        combat.scoreOrbs[i].lifetime = 0;
}

void updateBoss(Combat& combat, int16_t playerX, int16_t playerY) {
    Boss& boss = combat.boss;
    if (!boss.hpFifths) return;
    if (--boss.waveTimer == 0) {
        boss.waveTimer = BOSS_WAVE_INTERVAL_FRAMES;
        spawnBossMinions(combat, playerX, playerY);
    }
    if ((boss.waveTimer & (BOSS_STEP_INTERVAL_FRAMES - 1)) == 0)
        stepBossPatrol(boss);
}

void getBossSpawnPixel(uint8_t& x, uint8_t& y) {
    x = BOSS_PATROL_ANCHOR_X;
    y = BOSS_PATROL_ANCHOR_Y;
}

bool bossOverlapsPlayer(const Boss& boss, int16_t playerCenterX,
                        int16_t playerCenterY, bool touching) {
    const int16_t dx = shortestDelta(playerCenterX, boss.x, ARENA_WIDTH_FIXED);
    const int16_t dy = shortestDelta(playerCenterY, boss.y, ARENA_HEIGHT_FIXED);
    const int16_t margin = touching ? FIXED_ONE + 1 : 0;
    const int16_t width = BOSS_HALF_WIDTH * FIXED_ONE +
                          PLAYER_SIZE * FIXED_ONE / 2 + margin;
    const int16_t height = BOSS_HALF_HEIGHT * FIXED_ONE +
                           PLAYER_SIZE * FIXED_ONE / 2 + margin;
    return dx > -width && dx < width && dy > -height && dy < height;
}

} // namespace gc
