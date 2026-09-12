#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/render.cpp"
#include "render.h"

#include <Arduboy2.h>
#include "arena.h"
#include "stages.h"

namespace gc {
namespace {

// Спрайт игрока 7×7 пикселей, хранится во flash (PROGMEM).
const uint8_t playerBitmap[] PROGMEM = {0x3e, 0x7f, 0x55, 0x5d, 0x55, 0x7f, 0x3e};
static_assert(sizeof(playerBitmap) == PLAYER_SIZE && PLAYER_SIZE == 7,
              "Update the player bitmap when changing the hitbox");

// Рисуем пиксель арены с переходом через края.
// Сначала оборачиваем координаты, потом добавляем смещение HUD.
void drawArenaPixel(Arduboy2& arduboy, int16_t x, int16_t y, uint8_t color) {
    arduboy.drawPixel(wrapCoordinate(x, ARENA_WIDTH),
                      HUD_HEIGHT + wrapCoordinate(y, ARENA_HEIGHT), color);
}

// Переносим пиксели через края по отдельности, не затрагивая HUD.
// Тёмный глаз показывает направление; при Dash остальной силуэт заполняется белым.
void drawPlayer(Arduboy2& arduboy, const Player& player) {
    const int16_t x = player.x / FIXED_ONE;
    const int16_t y = player.y / FIXED_ONE;
    const int8_t facingX = getFacingX(player);
    const int8_t facingY = getFacingY(player);
    for (uint8_t column = 0; column < PLAYER_SIZE; ++column) {
        const uint8_t bits = pgm_read_byte(&playerBitmap[column]);
        for (uint8_t row = 0; row < PLAYER_SIZE; ++row) {
            const bool eye = column == 3 + facingX * 2 &&
                             row == 3 + facingY * 2;
            const bool lit = !eye && ((bits & (1 << row)) || player.dashFrames > 0);
            drawArenaPixel(arduboy, x + column, y + row, lit ? WHITE : BLACK);
        }
    }
}

// Рисуем пулю: короткий след из 3 точек для видимости.
// Хитбокс — только головная точка, хвост чисто визуальный.
void drawProjectile(Arduboy2& arduboy, const Projectile& projectile) {
    if (projectile.framesLeft == 0) {
        return;
    }
    for (uint8_t part = 0; part < 3; ++part) {
        const int16_t x = wrapCoordinate(projectile.x - projectile.velocityX * part / 2,
                                         ARENA_WIDTH_FIXED);
        const int16_t y = wrapCoordinate(projectile.y - projectile.velocityY * part / 2,
                                         ARENA_HEIGHT_FIXED);
        drawArenaPixel(arduboy, x / FIXED_ONE, y / FIXED_ONE, WHITE);
    }
}

// Маленький пиксельный шрифт 4×5 для миниатюрных врагов.
// Глифы: 0–9, A–F, 'x' (Basic), 'd' (Splitter). Итого 18 штук, по 5 байт.
// Каждый байт — одна строка, биты 3..0 слева направо (маска 0x8..0x1).
const uint8_t tinyFont[] PROGMEM = {
    // 0
    0b0110, 0b1001, 0b1001, 0b1001, 0b0110,
    // 1
    0b0010, 0b0110, 0b0010, 0b0010, 0b0111,
    // 2
    0b1110, 0b0001, 0b0110, 0b1000, 0b1111,
    // 3
    0b1110, 0b0001, 0b0110, 0b0001, 0b1110,
    // 4
    0b1001, 0b1001, 0b1111, 0b0001, 0b0001,
    // 5
    0b1111, 0b1000, 0b1110, 0b0001, 0b1110,
    // 6
    0b0110, 0b1000, 0b1110, 0b1001, 0b0110,
    // 7
    0b1111, 0b0001, 0b0010, 0b0100, 0b0100,
    // 8
    0b0110, 0b1001, 0b0110, 0b1001, 0b0110,
    // 9
    0b0110, 0b1001, 0b0111, 0b0001, 0b0110,
    // A
    0b0110, 0b1001, 0b1111, 0b1001, 0b1001,
    // B
    0b1110, 0b1001, 0b1110, 0b1001, 0b1110,
    // C
    0b0111, 0b1000, 0b1000, 0b1000, 0b0111,
    // D
    0b1110, 0b1001, 0b1001, 0b1001, 0b1110,
    // E
    0b1111, 0b1000, 0b1110, 0b1000, 0b1111,
    // F
    0b1111, 0b1000, 0b1110, 0b1000, 0b1000,
    // x (Basic)
    0b1001, 0b0110, 0b0110, 0b0110, 0b1001,
    // d (Splitter)
    0b0100, 0b0100, 0b1110, 0b1001, 0b1001
};

// Индекс в tinyFont: '0'-'9' → 0-9, 'A'-'F' → 10-15, 'x' → 16, 'd' → 17
uint8_t tinyFontIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c == 'x') return 16;
    return 17;  // 'd'
}

// Рисуем 4×5 глиф на арене (с обёрткой через края).
void drawTinyGlyph(Arduboy2& arduboy, char c, int16_t x, int16_t y) {
    const uint8_t* data = &tinyFont[tinyFontIndex(c) * 5];
    for (uint8_t row = 0; row < 5; ++row) {
        const uint8_t bits = pgm_read_byte(data + row);
        for (uint8_t col = 0; col < 4; ++col) {
            if (bits & (0x08 >> col)) {
                drawArenaPixel(arduboy, x + col, y + row, WHITE);
            }
        }
    }
}

// Рисуем врага: компактная строка, например "d10", "x2", "F15"
void drawEnemy(Arduboy2& arduboy, const Enemy& enemy) {
    const EnemyType type = getEnemyType(enemy);
    const uint8_t hp = getEnemyHp(enemy);
    if (type == EnemyType::None || hp == 0) return;

    // Собираем короткую строку: префикс типа + HP в hex
    char buf[4];
    uint8_t len = 1;
    switch (type) {
        case EnemyType::Basic:    buf[0] = 'x'; break;
        case EnemyType::Splitter: buf[0] = 'd'; break;
        case EnemyType::Fast:     buf[0] = 'F'; break;
        default: return;
    }
    if (hp >= 16) {
        buf[len++] = (char)('0' + (hp >> 4));
        const uint8_t lo = hp & 0x0F;
        buf[len++] = lo < 10 ? (char)('0' + lo) : (char)('A' + lo - 10);
    } else if (hp >= 10) {
        buf[len++] = (char)('A' + hp - 10);
    } else {
        buf[len++] = (char)('0' + hp);
    }

    // Центрируем текст относительно позиции врага (enny = 4×len × 5)
    const int16_t centerX = wrapCoordinate(enemy.x / FIXED_ONE, ARENA_WIDTH);
    const int16_t centerY = wrapCoordinate(enemy.y / FIXED_ONE, ARENA_HEIGHT);
    const int16_t startX = centerX - static_cast<int16_t>(len) * 2;  // (4 * len) / 2
    const int16_t startY = centerY - 2;                                // 5 / 2

    for (uint8_t i = 0; i < len; ++i) {
        drawTinyGlyph(arduboy, buf[i], startX + i * 4, startY);
    }
}

// Рисуем сферу очков
void drawScoreOrb(Arduboy2& arduboy, const ScoreOrb& orb) {
    // Если lifetime = 0, орб не активен
    if (orb.lifetime == 0) {
        return;
    }
    
    const int16_t screenX = wrapCoordinate(orb.x / FIXED_ONE, ARENA_WIDTH);
    const int16_t screenY = HUD_HEIGHT + wrapCoordinate(orb.y / FIXED_ONE, ARENA_HEIGHT);
    
    // Простой круг 3x3
    arduboy.drawCircle(screenX + 1, screenY + 1, 1, WHITE);
}

// Рисуем предупреждающие квадраты перед спавном волны
void drawSpawnIndicators(Arduboy2& arduboy, const Combat& combat) {
    if (combat.spawnTimer == 0) {
        return;
    }
    
    const uint8_t enemyCount = getWaveEnemyCount(combat.currentStage, combat.currentWave);
    for (uint8_t i = 0; i < enemyCount; ++i) {
        const WaveEnemy we = readWaveEnemy(combat.currentStage, combat.currentWave, i);
        if (we.type == EnemyType::None) {
            continue;
        }
        
        // Та же детерминированная позиция, что и при фактическом спавне
        uint8_t px = 0;
        uint8_t py = 0;
        getSpawnPixel(combat.currentStage, combat.currentWave, i, px, py);
        
        const int16_t screenX = wrapCoordinate(px, ARENA_WIDTH);
        const int16_t screenY = HUD_HEIGHT + wrapCoordinate(py, ARENA_HEIGHT);
        
        // Мигаем: показываем чётные кадры, скрываем нечётные
        if (combat.spawnTimer & 1) {
            arduboy.fillRect(screenX - 2, screenY - 2, 4, 4, WHITE);
        }
    }
}

// Рисуем один слот активной способности в HUD.
// label — буква A или B, показываем название способности и заряд.
void drawSlot(Arduboy2& arduboy, uint8_t x, char label, const ActiveSlot& slot) {
    arduboy.setCursor(x, 0);
    arduboy.print(label);
    if (slot.ability == AbilityId::None) {
        arduboy.print(F(":--"));
        return;
    }
    arduboy.print(F(":DASH"));
    arduboy.drawRect(x + 38, 1, 23, 6);
    const uint8_t readyWidth = static_cast<uint16_t>(DASH_COOLDOWN - slot.cooldown)
                               * 21 / DASH_COOLDOWN;
    if (readyWidth > 0) {
        arduboy.fillRect(x + 39, 2, readyWidth, 4);
    }
}

} // внутренние функции модуля

// Отрисовка всего игрового экрана.
void renderGame(Arduboy2& arduboy, const Game& game) {
    arduboy.clear();
    if (game.state == GameState::Title) {
        arduboy.setCursor(19, 5);
        arduboy.print(F("GARBAGE COLLECTOR"));
        arduboy.setCursor(31, 21);
        arduboy.print(F("COMBAT TEST"));
        arduboy.setCursor(10, 33);
        arduboy.print(F("D-PAD MOVE / A DASH"));
        arduboy.setCursor(16, 43);
        arduboy.print(F("AUTO FIRE / 3 HP"));
        arduboy.setCursor(28, 55);
        arduboy.print(F("A/B TO START"));
        return;
    }

    if (game.state == GameState::StageCleared) {
        arduboy.setCursor(24, 5);
        arduboy.print(F("STAGE CLEARED"));
        arduboy.setCursor(38, 21);
        arduboy.print(F("SCORE: "));
        arduboy.print(game.combat.playerScore);
        arduboy.setCursor(20, 35);
        arduboy.print(F("TIME TO REST"));
        arduboy.setCursor(26, 50);
        arduboy.print(F("A/B TO CONTINUE"));
        return;
    }

    // Фоновая сетка для ориентации на арене
    for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
        for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
            arduboy.drawPixel(x, y + HUD_HEIGHT);
        }
    }
    // Препятствия (тёмные прямоугольники с обводкой)
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        arduboy.fillRect(obstacle.x, obstacle.y + HUD_HEIGHT,
                         obstacle.width, obstacle.height, BLACK);
        arduboy.drawRect(obstacle.x, obstacle.y + HUD_HEIGHT,
                         obstacle.width, obstacle.height);
        arduboy.drawFastHLine(obstacle.x + 2, obstacle.y + HUD_HEIGHT + 2,
                              obstacle.width - 4);
    }
    // Предупреждающие квадраты перед спавном волны
    drawSpawnIndicators(arduboy, game.combat);
    // Рисуем новых врагов
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
        drawEnemy(arduboy, game.combat.enemies[i]);
    }
    // Рисуем сферы очков
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
        drawScoreOrb(arduboy, game.combat.scoreOrbs[i]);
    }
    drawPlayer(arduboy, game.player);
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
        drawProjectile(arduboy, game.combat.projectiles[i]);
    }
    drawSlot(arduboy, 0, 'A', game.player.slots[0]);
    drawSlot(arduboy, 66, 'B', game.player.slots[1]);
}

} // пространство имён gc
