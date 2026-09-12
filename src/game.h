#pragma once

#include "config.h"
#include "combat.h"

namespace gc {

enum class GameState : uint8_t {
    Intro,
    Menu,
    About,
    SoundMenu,
    Playing,
    Paused,
    Shop,
    StageCleared,
    GameOver,
    Win
};
enum class AbilityId : uint8_t { None, Dash, MarkAndSweep, StopTheWorld, Compact };
inline uint8_t abilityCooldown(AbilityId id) {
    return id == AbilityId::None ? 0 : (id == AbilityId::MarkAndSweep ? 180 :
           (id == AbilityId::Compact ? 200 : 250));
}
inline uint8_t heartState(uint8_t hp, uint8_t index) {
    return hp > 4 + index ? 2 : (hp > index ? 1 : 0);
}

// Пассивные апгрейды
enum class PassiveId : uint8_t { None, DamageUp, MaxHpUp, MoveSpeedUp };

// Активные апгрейды (покупаются в магазине)
enum class ActiveUpgradeId : uint8_t { None, MarkAndSweep, StopTheWorld, Compact };

// Слот активки: какая способность + кулдаун
struct ActiveSlot {
    AbilityId ability;
    uint8_t cooldown;
};

// Игрок: позиция, взгляд, слоты способностей, состояние Dash, HP, неуязвимость.
// x/y: левый верхний угол внутри арены, в единицах 1/16 пикселя.
// facing упакован (экономия 1 байта SRAM): биты 0-1 = X+1, биты 2-3 = Y+1.
struct Player {
    int16_t x;
    int16_t y;
    uint8_t facing;
    ActiveSlot slots[ACTIVE_SLOT_COUNT];
    uint8_t dashFrames;
    uint8_t hp;           // Текущее HP (0 = мёртв)
    uint8_t maxHp;        // Макс HP (база 4, кап 6 через апгрейды)
    uint8_t iframes;      // Неуязвимость после урона (в кадрах)
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

// Нанести урон игроку с IFrames
inline void damagePlayer(Player& player) {
    if (player.iframes == 0 && player.hp > 0) {
        player.hp--;
        player.iframes = IFRAME_DURATION;
    }
}

// Проверить, мигает ли игрок (для рендеринга неуязвимости)
inline bool isPlayerBlinking(const Player& player) {
    return player.iframes > 0 && (((IFRAME_DURATION - player.iframes) / BLINK_INTERVAL) & 1) == 0;
}

// Пассивные апгрейды игрока
struct PlayerPassives {
    uint8_t damageLevel;      // 0-3: +урон за уровень
    uint8_t maxHpLevel;       // 0-2: +1 макс HP за уровень (до 6)
    uint8_t moveSpeedLevel;   // 0-3: +скорость за уровень
};

// Магазин: выбор апгрейда
struct ShopState {
    uint8_t selectedIndex;        // 0-5: 0-2 пассивки, 3-5 активки, 6 = Skip
    uint8_t passiveChoices[3];    // PassiveId для 3 вариантов
    uint8_t activeChoices[3];     // ActiveUpgradeId для 3 вариантов
    bool passiveBought;
    bool activeBought;
    bool choosingSlot;
    int8_t previousMoveY;
};

struct MenuState {
    uint8_t selectedIndex;
    int8_t previousMoveY;
};

// Единственный владелец состояния игры: без дубликатов игрока и боевых пулов.
struct Game {
    GameState state;
    Player player;
    PlayerPassives passives;
    Combat combat;
    ShopState shop;
    MenuState menu;
    MenuState soundMenu;
    uint8_t soundEnabled;
    uint8_t pauseHoldFrames;  // Счётчик удержания A+B для паузы
};

// Ввод на один кадр
struct InputFrame {
    int8_t moveX;  // -1, 0, +1
    int8_t moveY;
    bool activateA;  // Только новое нажатие, не удержание.
    bool activateB;
    bool holdA;      // Удержание кнопки A
    bool holdB;      // Удержание кнопки B
};

// Полный сброс забега: взгляд вправо, Dash в A, слот B пустой.
void startGame(Game& game);

// Инициализация магазина перед входом
void initShop(Game& game);

// Обработка ввода в магазине
void updateShop(Game& game, const InputFrame& input);

// Применение выбранного апгрейда
void applyShopChoice(Game& game);

// Обновление на один кадр
void updateGame(Game& game, const InputFrame& input);

#ifdef __AVR__
static_assert(sizeof(Player) == 13, "Packed player must use 13 AVR bytes");
static_assert(sizeof(Game) == 1 + sizeof(Player) + sizeof(PlayerPassives) + sizeof(Combat) +
                              sizeof(ShopState) + sizeof(MenuState) * 2 + 2,
              "Unexpected AVR game layout");
#endif

} // пространство имён gc
