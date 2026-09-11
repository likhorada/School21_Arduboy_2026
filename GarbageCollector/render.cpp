#include "render.h"

#include <Arduboy2.h>
#include "arena.h"

namespace gc {
namespace {

const uint8_t playerBitmap[] PROGMEM = {0x3e, 0x7f, 0x55, 0x5d, 0x55, 0x7f, 0x3e};
static_assert(sizeof(playerBitmap) == PLAYER_SIZE && PLAYER_SIZE == 7,
              "Update the player bitmap when changing the hitbox");

// Wrap the small sprite pixel-by-pixel so even a corner crossing cannot paint HUD.
void drawPlayer(Arduboy2& arduboy, const Player& player) {
    const int16_t x = player.x / FIXED_ONE;
    const int16_t y = player.y / FIXED_ONE;
    for (uint8_t column = 0; column < PLAYER_SIZE; ++column) {
        const uint8_t bits = pgm_read_byte(&playerBitmap[column]);
        for (uint8_t row = 0; row < PLAYER_SIZE; ++row) {
            const bool eye = column == 3 + player.facingX * 2 &&
                             row == 3 + player.facingY * 2;
            const bool lit = !eye && ((bits & (1 << row)) || player.dashFrames > 0);
            arduboy.drawPixel(wrapCoordinate(x + column, ARENA_WIDTH),
                              HUD_HEIGHT + wrapCoordinate(y + row, ARENA_HEIGHT),
                              lit ? WHITE : BLACK);
        }
    }
}

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

} // namespace

void renderGame(Arduboy2& arduboy, const Game& game) {
    arduboy.clear();
    if (game.state == GameState::Title) {
        arduboy.setCursor(19, 5);
        arduboy.print(F("GARBAGE COLLECTOR"));
        arduboy.setCursor(22, 21);
        arduboy.print(F("MOVEMENT TEST"));
        arduboy.setCursor(10, 33);
        arduboy.print(F("D-PAD MOVE / A DASH"));
        arduboy.setCursor(19, 43);
        arduboy.print(F("ALL EDGES WRAP"));
        arduboy.setCursor(28, 55);
        arduboy.print(F("A/B TO START"));
        return;
    }

    for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
        for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
            arduboy.drawPixel(x, y + HUD_HEIGHT);
        }
    }
    for (uint8_t i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        arduboy.fillRect(obstacle.x, obstacle.y + HUD_HEIGHT,
                         obstacle.width, obstacle.height, BLACK);
        arduboy.drawRect(obstacle.x, obstacle.y + HUD_HEIGHT,
                         obstacle.width, obstacle.height);
        arduboy.drawFastHLine(obstacle.x + 2, obstacle.y + HUD_HEIGHT + 2,
                              obstacle.width - 4);
    }
    drawPlayer(arduboy, game.player);
    drawSlot(arduboy, 0, 'A', game.player.slots[0]);
    drawSlot(arduboy, 66, 'B', game.player.slots[1]);
}

} // namespace gc
