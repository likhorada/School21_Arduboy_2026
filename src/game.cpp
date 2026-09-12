#include "game.h"

#include "arena.h"

namespace gc {
namespace {

// Активируем содержимое слота, а не действие кнопки. Пустой слот и кулдаун безопасно игнорируем.
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

// Делим движение на шаги не больше пикселя, чтобы рывок не перескакивал стены.
// Ходьба скользит вдоль стены, а столкновение при Dash полностью прерывает рывок.
void movePlayer(Player& player, int8_t dx, int8_t dy, bool dashing) {
    const uint8_t absX = dx < 0 ? -dx : dx;
    const uint8_t absY = dy < 0 ? -dy : dy;
    const uint8_t distance = absX > absY ? absX : absY;
    const uint8_t steps = (distance + FIXED_ONE - 1) / FIXED_ONE;  // Округление вверх
    int8_t previousX = 0;
    int8_t previousY = 0;

    for (uint8_t step = 1; step <= steps && step <= MAX_MOVE_STEPS; ++step) {
        // Смещение от начала пути; разность с previous даёт только текущий шаг.
        // Так остаток от деления не теряется: сумма шагов равна исходному смещению.
        const int8_t partialX = static_cast<int16_t>(dx) * step / steps;
        const int8_t partialY = static_cast<int16_t>(dy) * step / steps;

        // Оборачиваем координаты через границы арены (тор)
        const int16_t nextX = wrapCoordinate(player.x + partialX - previousX,
                                             ARENA_WIDTH_FIXED);
        const int16_t nextY = wrapCoordinate(player.y + partialY - previousY,
                                             ARENA_HEIGHT_FIXED);
        previousX = partialX;
        previousY = partialY;

        if (dashing) {
            // Рывок: первая стена останавливает всё
            if (playerBlocked(nextX, nextY)) {
                player.dashFrames = 0;
                return;
            }
            player.x = nextX;
            player.y = nextY;
        } else {
            // Ходьба: оси независимы (скольжение вдоль стен)
            if (!playerBlocked(nextX, player.y)) {
                player.x = nextX;
            }
            if (!playerBlocked(player.x, nextY)) {
                player.y = nextY;
            }
        }
    }
}

} // внутренние функции модуля

void startGame(Game& game) {
    game = {};
    game.state = GameState::Playing;
    game.player.x = PLAYER_START_X * FIXED_ONE;
    game.player.y = PLAYER_START_Y * FIXED_ONE;
    setFacing(game.player, 1, 0);  // Смотрим вправо
    game.player.slots[0].ability = AbilityId::Dash;
    resetCombat(game.combat);
}

void updateGame(Game& game, const InputFrame& input) {
    if (game.state == GameState::Title) {
        // Запускаем по A или B; D-pad на титульном экране ничего не делает.
        if (input.activateA || input.activateB) {
            startGame(game);
        }
        return; // Стартовое нажатие не должно одновременно запускать Dash.
    }

    Player& player = game.player;

    // Уменьшаем старые таймеры до активации новых, чтобы не сократить их на кадр.
    for (uint8_t i = 0; i < ACTIVE_SLOT_COUNT; ++i) {
        if (player.slots[i].cooldown > 0) {
            --player.slots[i].cooldown;
        }
    }

    // Во время Dash взгляд не меняется: он задаёт зафиксированное направление рывка.
    if (player.dashFrames == 0 && (input.moveX != 0 || input.moveY != 0)) {
        setFacing(player, input.moveX, input.moveY);
    }

    if (input.activateA) {
        activateAbility(player, player.slots[0]);
    }
    if (input.activateB) {
        activateAbility(player, player.slots[1]);
    }

    const bool dashing = player.dashFrames > 0;
    const int8_t directionX = dashing ? getFacingX(player) : input.moveX;
    const int8_t directionY = dashing ? getFacingY(player) : input.moveY;
    uint8_t speed = dashing ? DASH_SPEED : WALK_SPEED;

    // 181/256 приближает 1/sqrt(2), чтобы диагональ не ускоряла движение.
    // Половина делителя (128) нужна для округления, а не отбрасывания дробной части.
    if (directionX != 0 && directionY != 0) {
        speed = (static_cast<uint16_t>(speed) * 181 + 128) / 256;
    }

    movePlayer(player, directionX * speed, directionY * speed, dashing);

    if (player.dashFrames > 0) {
        --player.dashFrames;
    }

    // Автострельба использует позицию после движения, включая текущий шаг Dash.
    updateCombat(game.combat, player.x, player.y);
}

} // пространство имён gc
