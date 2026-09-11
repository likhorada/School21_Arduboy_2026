#pragma once

#include "config.h"

namespace gc {

enum class GameState : uint8_t { Title, Playing };
enum class AbilityId : uint8_t { None, Dash };

struct ActiveSlot {
    AbilityId ability;
    uint8_t cooldown;
};

struct Player {
    int16_t x; // Normalized top-left position, in 1/16 pixel units.
    int16_t y;
    int8_t facingX;
    int8_t facingY;
    ActiveSlot slots[ACTIVE_SLOT_COUNT];
    uint8_t dashFrames;
};

struct Game {
    GameState state;
    Player player;
};

struct InputFrame {
    int8_t moveX; // -1, 0, +1. Opposing directions cancel in the input adapter.
    int8_t moveY;
    bool activateA; // Press edges, never held-button states.
    bool activateB;
};

// Reset all run state; Dash starts in A, B is empty.
void startGame(Game& game);

// Advance exactly one fixed-duration frame; hardware and rendering stay outside.
void updateGame(Game& game, const InputFrame& input);

} // namespace gc
