#include "game.h"

#include "arena.h"

namespace gc {
namespace {

// Dispatch by equipped ability, not by physical button. Failed activation is free.
void activateAbility(Player& player, ActiveSlot& slot) {
    if (slot.cooldown != 0) {
        return;
    }
    switch (slot.ability) {
    case AbilityId::None:
        break;
    case AbilityId::Dash:
        if (player.dashFrames == 0) {
            player.dashFrames = DASH_DURATION;
            slot.cooldown = DASH_COOLDOWN;
        }
        break;
    }
}

// Split displacement into <= 1-pixel steps, preserving the fixed-point remainder.
// Walk resolves axes separately; Dash stops entirely on its first blocked step.
void movePlayer(Player& player, int8_t dx, int8_t dy, bool dashing) {
    const uint8_t absX = dx < 0 ? -dx : dx;
    const uint8_t absY = dy < 0 ? -dy : dy;
    const uint8_t distance = absX > absY ? absX : absY;
    const uint8_t steps = (distance + FIXED_ONE - 1) / FIXED_ONE;
    int8_t previousX = 0;
    int8_t previousY = 0;
    for (uint8_t step = 1; step <= steps && step <= MAX_MOVE_STEPS; ++step) {
        const int8_t partialX = static_cast<int16_t>(dx) * step / steps;
        const int8_t partialY = static_cast<int16_t>(dy) * step / steps;
        const int16_t nextX = wrapCoordinate(player.x + partialX - previousX,
                                             ARENA_WIDTH_FIXED);
        const int16_t nextY = wrapCoordinate(player.y + partialY - previousY,
                                             ARENA_HEIGHT_FIXED);
        previousX = partialX;
        previousY = partialY;
        if (dashing) {
            if (playerBlocked(nextX, nextY)) {
                player.dashFrames = 0;
                return;
            }
            player.x = nextX;
            player.y = nextY;
        } else {
            if (!playerBlocked(nextX, player.y)) {
                player.x = nextX;
            }
            if (!playerBlocked(player.x, nextY)) {
                player.y = nextY;
            }
        }
    }
}

} // namespace

void startGame(Game& game) {
    game = {};
    game.state = GameState::Playing;
    game.player.x = PLAYER_START_X * FIXED_ONE;
    game.player.y = PLAYER_START_Y * FIXED_ONE;
    game.player.facingX = 1;
    game.player.slots[0].ability = AbilityId::Dash;
}

void updateGame(Game& game, const InputFrame& input) {
    if (game.state == GameState::Title) {
        if (input.activateA || input.activateB) {
            startGame(game);
        }
        return; // A start press must not also activate a slot in the same frame.
    }

    Player& player = game.player;
    for (uint8_t i = 0; i < ACTIVE_SLOT_COUNT; ++i) {
        if (player.slots[i].cooldown > 0) {
            --player.slots[i].cooldown;
        }
    }
    if (player.dashFrames == 0 && (input.moveX != 0 || input.moveY != 0)) {
        player.facingX = input.moveX;
        player.facingY = input.moveY;
    }
    if (input.activateA) {
        activateAbility(player, player.slots[0]);
    }
    if (input.activateB) {
        activateAbility(player, player.slots[1]);
    }

    const bool dashing = player.dashFrames > 0;
    const int8_t directionX = dashing ? player.facingX : input.moveX;
    const int8_t directionY = dashing ? player.facingY : input.moveY;
    uint8_t speed = dashing ? DASH_SPEED : WALK_SPEED;
    if (directionX != 0 && directionY != 0) {
        speed = (static_cast<uint16_t>(speed) * 181 + 128) / 256;
    }
    movePlayer(player, directionX * speed, directionY * speed, dashing);
    if (player.dashFrames > 0) {
        --player.dashFrames;
    }
}

} // namespace gc
