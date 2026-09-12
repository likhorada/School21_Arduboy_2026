#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/enemies.cpp"
#include "enemies.h"
#include "arena.h"

namespace gc {
namespace {

// Проверка коллизий врага (хитбокс 12x6, центр в centerX, centerY) с препятствиями.
// Возвращает true, если позиция занята стеной.
bool enemyHitsObstacle(int16_t centerX, int16_t centerY) {
    const int16_t pixelX = centerX / FIXED_ONE;
    const int16_t pixelY = centerY / FIXED_ONE;
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obs = readObstacle(i);
        if (pixelX - ENEMY_HALF_WIDTH < obs.x + obs.width &&
            pixelX + ENEMY_HALF_WIDTH > obs.x &&
            pixelY - ENEMY_HALF_HEIGHT < obs.y + obs.height &&
            pixelY + ENEMY_HALF_HEIGHT > obs.y) {
            return true;
        }
    }
    return false;
}

// Проверка столкновения точки с врагом (текст 12x6 пикселей, центрированный)
bool pointHitsEnemy(int16_t px, int16_t py, int16_t ex, int16_t ey) {
    // ex, ey - центр врага
    // Текст и хитбокс: [-ENEMY_HALF_WIDTH..+ENEMY_HALF_WIDTH] по X,
    //                    [-ENEMY_HALF_HEIGHT..+ENEMY_HALF_HEIGHT] по Y
    const int16_t dx = px - ex;
    const int16_t dy = py - ey;
    const int16_t halfWidth = ENEMY_HALF_WIDTH * FIXED_ONE;
    const int16_t halfHeight = ENEMY_HALF_HEIGHT * FIXED_ONE;
    return dx >= -halfWidth && dx <= halfWidth && dy >= -halfHeight && dy <= halfHeight;
}

// Простое движение к игроку с проверкой коллизий и скольжением вдоль стен
void moveTowardsPlayer(Enemy& enemy, int16_t playerX, int16_t playerY, uint8_t speed) {
    const int16_t dx = shortestDelta(enemy.x, playerX, ARENA_WIDTH_FIXED);
    const int16_t dy = shortestDelta(enemy.y, playerY, ARENA_HEIGHT_FIXED);
    
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
    const int16_t newX = wrapCoordinate(enemy.x + vx, ARENA_WIDTH_FIXED);
    const bool blockedX = enemyHitsObstacle(newX, enemy.y);
    
    // Пробуем двигаться по Y
    const int16_t newY = wrapCoordinate(enemy.y + vy, ARENA_HEIGHT_FIXED);
    const bool blockedY = enemyHitsObstacle(enemy.x, newY);
    
    if (!blockedX) {
        enemy.x = newX;
    }
    if (!blockedY) {
        enemy.y = newY;
    }
    
    // Если оба направления заблокированы, пробуем скольжение перпендикулярно стене
    if (blockedX && blockedY && (vx != 0 || vy != 0)) {
        const int16_t slideX = wrapCoordinate(enemy.x + vy, ARENA_WIDTH_FIXED);
        if (!enemyHitsObstacle(slideX, enemy.y)) {
            enemy.x = slideX;
        } else {
            const int16_t slideY = wrapCoordinate(enemy.y + vx, ARENA_HEIGHT_FIXED);
            if (!enemyHitsObstacle(enemy.x, slideY)) {
                enemy.y = slideY;
            }
        }
    }
}

} // anonymous namespace

// Спавн врага
void spawnEnemy(Enemy& enemy, EnemyType type, int16_t x, int16_t y, uint8_t variant) {
    enemy.x = x;
    enemy.y = y;
    enemy.splitLevel = 0;
    
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
        enemy.splitLevel = variant;
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
                   int16_t playerX, int16_t playerY, uint32_t& randomState) {
    (void)randomState; // Не используется пока
    
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
                const uint8_t lostHp = FAST_MAX_HP - currentHp;
                speed = FAST_BASE_SPEED + lostHp * FAST_SPEED_INCREMENT;
            }
            break;
            
        default:
            break;
        }
        
        moveTowardsPlayer(enemy, playerX, playerY, speed);
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
            const int16_t dx = shortestDelta(enemy1.x, enemy2.x, ARENA_WIDTH_FIXED);
            const int16_t dy = shortestDelta(enemy1.y, enemy2.y, ARENA_HEIGHT_FIXED);
            const int16_t absDx = dx < 0 ? -dx : dx;
            const int16_t absDy = dy < 0 ? -dy : dy;
            const int16_t minDist = 6 * FIXED_ONE;
            
            // Если слишком близко, расталкиваем (только если позиция не внутри стены)
            if (absDx < minDist && absDy < minDist) {
                const int8_t pushX = (dx > 0) ? 2 : -2;
                const int8_t pushY = (dy > 0) ? 2 : -2;
                
                const int16_t newX1 = wrapCoordinate(enemy1.x - pushX, ARENA_WIDTH_FIXED);
                const int16_t newY1 = wrapCoordinate(enemy1.y - pushY, ARENA_HEIGHT_FIXED);
                if (!enemyHitsObstacle(newX1, enemy1.y)) enemy1.x = newX1;
                if (!enemyHitsObstacle(enemy1.x, newY1)) enemy1.y = newY1;
                
                const int16_t newX2 = wrapCoordinate(enemy2.x + pushX, ARENA_WIDTH_FIXED);
                const int16_t newY2 = wrapCoordinate(enemy2.y + pushY, ARENA_HEIGHT_FIXED);
                if (!enemyHitsObstacle(newX2, enemy2.y)) enemy2.x = newX2;
                if (!enemyHitsObstacle(enemy2.x, newY2)) enemy2.y = newY2;
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

// Проверка попадания в врага
int8_t checkEnemyHit(Enemy& enemy, int16_t projectileX, int16_t projectileY) {
    const EnemyType type = getEnemyType(enemy);
    if (type == EnemyType::None) {
        return -1;
    }
    
    if (!pointHitsEnemy(projectileX, projectileY, enemy.x, enemy.y)) {
        return -1;
    }
    
    // Попадание!
    const uint8_t hp = getEnemyHp(enemy);
    if (hp <= 1) {
        // Смерть: тип НЕ обнуляем здесь — обработчик в combat.cpp читает тип
        // (например, для деления Splitter) и сам убирает врага.
        return 1; // Смерть
    } else {
        setEnemyHp(enemy, hp - 1);
        return 0; // Попадание без смерти
    }
}

// Создание дропа очков
void createScoreOrb(ScoreOrb orbs[MAX_SCORE_ORBS], int16_t x, int16_t y, uint8_t value) {
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        if (orbs[i].lifetime == 0) {
            orbs[i].x = x;
            orbs[i].y = y;
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
        const int16_t dx = orbs[i].x - playerX;
        const int16_t dy = orbs[i].y - playerY;
        const int32_t distSq = static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy;
        const int16_t collectRadius = 10 * FIXED_ONE;
        
        if (distSq < static_cast<int32_t>(collectRadius) * collectRadius) {
            collected += orbs[i].value;
            orbs[i].lifetime = 0;
        }
    }
    
    return collected;
}

} // namespace gc
