#include "render.h"

#include <Arduboy2.h>
#include "arena.h"

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

// Ромб с одной меткой на каждое HP; при попадании ненадолго заливаем его целиком.
void drawDummy(Arduboy2& arduboy, const Dummy& dummy) {
    const uint8_t hp = getHp(dummy);
    if (hp == 0) {
        return;
    }
    constexpr uint8_t center = DUMMY_SIZE / 2;
    static_assert(DUMMY_SIZE == 7 && DUMMY_MAX_HP <= 3,
                  "Update the three-mark dummy drawing when changing its size/HP");
    const uint8_t flash = getHitFlash(dummy);
    for (uint8_t row = 0; row < DUMMY_SIZE; ++row) {
        for (uint8_t column = 0; column < DUMMY_SIZE; ++column) {
            const uint8_t dx = column > center ? column - center : center - column;
            const uint8_t dy = row > center ? row - center : center - row;
            const bool outline = dx + dy == center;
            const bool hpMark = row == center && column % 2 == 1 && column / 2 < hp;
            const bool flashFill = flash > 0 && dx + dy <= center;
            drawArenaPixel(arduboy, dummy.x + column, dummy.y + row,
                           outline || hpMark || flashFill ? WHITE : BLACK);
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
    for (uint8_t i = 0; i < DUMMY_COUNT; ++i) {
        drawDummy(arduboy, game.combat.dummies[i]);
    }
    drawPlayer(arduboy, game.player);
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
        drawProjectile(arduboy, game.combat.projectiles[i]);
    }
    drawSlot(arduboy, 0, 'A', game.player.slots[0]);
    drawSlot(arduboy, 66, 'B', game.player.slots[1]);
}

} // пространство имён gc
