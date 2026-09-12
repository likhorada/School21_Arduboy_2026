#pragma once

#include "config.h"
#include "combat.h"

namespace gc {

enum class GameState : uint8_t { Title, Playing };
enum class AbilityId : uint8_t { None, Dash };

// Слот активки: какая способность + кулдаун
struct ActiveSlot {
    AbilityId ability;
    uint8_t cooldown;
};

// Игрок: позиция, взгляд, слоты способностей, состояние Dash.
// x/y: левый верхний угол внутри арены, в единицах 1/16 пикселя.
// facing упакован (экономия 1 байта SRAM): биты 0-1 = X+1, биты 2-3 = Y+1.
struct Player {
    int16_t x;
    int16_t y;
    uint8_t facing;
    ActiveSlot slots[ACTIVE_SLOT_COUNT];
    uint8_t dashFrames;
};

// Получить направление взгляда X из упакованного facing
inline int8_t getFacingX(const Player& player) {
    return static_cast<int8_t>((player.facing & 0x03) - 1);
}

// Получить направление взгляда Y из упакованного facing
inline int8_t getFacingY(const Player& player) {
    return static_cast<int8_t>(((player.facing >> 2) & 0x03) - 1);
}

// Установить направление взгляда (x и y: -1, 0, +1)
inline void setFacing(Player& player, int8_t x, int8_t y) {
    player.facing = (x + 1) | ((y + 1) << 2);
}

// Единственный владелец состояния игры: без дубликатов игрока и боевых пулов.
struct Game {
    GameState state;
    Player player;
    Combat combat;
};

// Ввод на один кадр
struct InputFrame {
    int8_t moveX;  // -1, 0, +1
    int8_t moveY;
    bool activateA;  // Только новое нажатие, не удержание.
    bool activateB;
};

// Полный сброс забега: взгляд вправо, Dash в A, слот B пустой.
void startGame(Game& game);

// Обновление на один кадр
void updateGame(Game& game, const InputFrame& input);

#ifdef __AVR__
static_assert(sizeof(Player) == 10, "Packed player must use ten AVR bytes");
static_assert(sizeof(Game) == 1 + sizeof(Player) + sizeof(Combat),
              "Unexpected AVR game layout");
#endif

} // пространство имён gc
