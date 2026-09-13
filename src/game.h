#pragma once

#include "config.h"
#include "combat.h"

namespace gc {

enum class GameState : uint8_t {
    Intro,
    Menu,
    About,
    Rules,
    SoundMenu,
    Playing,
    Paused,
    Shop,
    StageCleared,
    GameOver,
    Win
};
enum class AbilityId : uint8_t {
    None,
    Dash,
    TimeWarp,
    RecursiveCall,
    Free,
    BitShift,
    MarkAndSweep,
    StackOverflow,
    MemoryDump
};
inline uint8_t abilityCooldown(AbilityId id) {
    switch (id) {
    case AbilityId::Dash: return DASH_COOLDOWN;
    case AbilityId::TimeWarp: return TIME_WARP_COOLDOWN;
    case AbilityId::RecursiveCall: return RECURSIVE_COOLDOWN;
    case AbilityId::Free: return FREE_COOLDOWN;
    case AbilityId::BitShift: return BIT_SHIFT_COOLDOWN;
    case AbilityId::MarkAndSweep: return SWEEP_COOLDOWN;
    case AbilityId::StackOverflow: return STACK_OVERFLOW_COOLDOWN;
    case AbilityId::MemoryDump: return MEMORY_DUMP_COOLDOWN;
    default: return 0;
    }
}
inline uint8_t heartState(uint8_t hp, uint8_t index) {
    return hp > 4 + index ? 2 : (hp > index ? 1 : 0);
}

// Пассивные апгрейды
enum class PassiveId : uint8_t {
    None,
    CompilerOptimization,
    Overclock,
    OptimizedBuild,
    MemoryFragmentation,
    CollectionRange,
    RamCapacity
};

enum class ShopCategory : uint8_t { Passive, Active };

struct Game;

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
    uint8_t dashFrames;   // Кадры Dash (инвариант: после активации > 0)
    uint8_t walkPhase;    // Фаза анимации ходьбы: растёт при движении, 0 — покой
    uint8_t hp;           // Текущее HP (0 = мёртв)
    uint8_t maxHp;        // Макс HP (база 4, кап 6 через апгрейды)
    uint8_t iframes;      // Неуязвимость после урона (в кадрах)
    uint8_t invincible;   // Бессмертие от кода Конами (1 = урон игнорируется)
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

// Нанести урон игроку с IFrames; бессмертие кодом Конами всё блокирует.
inline void damagePlayer(Player& player) {
    if (!player.invincible && player.iframes == 0 && player.hp > 0) {
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
    uint16_t levels;
};

inline uint8_t passiveLevel(const PlayerPassives& passives, PassiveId id) {
    if (id == PassiveId::None) return 0;
    return (passives.levels >> ((static_cast<uint8_t>(id) - 1) * 2)) & 0x03;
}

inline void setPassiveLevel(PlayerPassives& passives, PassiveId id, uint8_t level) {
    const uint8_t shift = (static_cast<uint8_t>(id) - 1) * 2;
    passives.levels = (passives.levels & ~(uint16_t(0x03) << shift)) |
                      (uint16_t(level & 0x03) << shift);
}

uint8_t passiveCap(PassiveId id);
uint16_t passivePrice(const Game& game, PassiveId id);
uint16_t activePrice(AbilityId id);

// Магазин: одна из трёх карточек текущей категории.
struct ShopState {
    uint8_t selectedIndex;        // 0-2: текущая карточка карусели
    ShopCategory category;
    bool choosingSlot;
    int8_t previousMoveX;
    uint8_t seed;
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
    uint8_t konamiProgress;   // Код Конами: позиция в последовательности
    uint8_t konamiTimer;      // Кадры с последнего нажатия (таймаут сброса)
    uint8_t directionHeld;    // 1 = крестовина зажата (фронт нажатия)
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

// Детерминированная карточка текущего магазина; три карточки уникальны.
uint8_t shopChoice(const Game& game, ShopCategory category, uint8_t index);

// Обновление на один кадр
void updateGame(Game& game, const InputFrame& input);

#ifdef __AVR__
static_assert(sizeof(Player) == 15, "Packed player must use 15 AVR bytes");
static_assert(sizeof(Game) == 1 + sizeof(Player) + sizeof(PlayerPassives) + sizeof(Combat) +
                              sizeof(ShopState) + sizeof(MenuState) * 2 + 2 + 3,
              "Unexpected AVR game layout");
#endif

} // пространство имён gc
