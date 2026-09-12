#include "game.h"
#include "arena.h"
#include "stages.h"

namespace gc {
namespace {

void activateAbility(Game& game, ActiveSlot& slot) {
    Player& player = game.player;
    if (slot.cooldown || slot.ability == AbilityId::None) return;
    switch (slot.ability) {
    case AbilityId::Dash:
        if (player.dashFrames) return;
        player.dashFrames = DASH_DURATION;
        break;
    case AbilityId::MarkAndSweep: {
        uint8_t targets = 0;
        const int16_t px = wrapCoordinate(player.x + PLAYER_SIZE * FIXED_ONE / 2, ARENA_WIDTH_FIXED);
        const int16_t py = wrapCoordinate(player.y + PLAYER_SIZE * FIXED_ONE / 2, ARENA_HEIGHT_FIXED);
        for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
            const Enemy& enemy = game.combat.enemies[i];
            const int16_t dx = shortestDelta(px, enemy.x, ARENA_WIDTH_FIXED);
            const int16_t dy = shortestDelta(py, enemy.y, ARENA_HEIGHT_FIXED);
            if (getEnemyType(enemy) != EnemyType::None &&
                int32_t(dx) * dx + int32_t(dy) * dy <= int32_t(SWEEP_RADIUS * FIXED_ONE) * (SWEEP_RADIUS * FIXED_ONE)) {
                targets |= uint8_t(1 << i);
            }
        }
        // Snapshot prevents newly split children being hit by the same activation.
        for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
            if (targets & (1 << i)) damageEnemy(game.combat, i, SWEEP_DAMAGE);
        }
        break;
    }
    case AbilityId::StopTheWorld:
        game.combat.freezeFrames = FREEZE_DURATION;
        break;
    case AbilityId::Compact:
        for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
            ScoreOrb& orb = game.combat.scoreOrbs[i];
            if (orb.lifetime) game.combat.playerScore += orb.value;
            orb.lifetime = 0;
        }
        if (player.iframes < COMPACT_SHIELD_DURATION) player.iframes = COMPACT_SHIELD_DURATION;
        break;
    default: return;
    }
    slot.cooldown = abilityCooldown(slot.ability);
}

bool movementBlocked(Game& game, int16_t x, int16_t y) {
    if (playerBlocked(game.combat.currentStage, x, y)) return true;
    if (!checkPlayerEnemyCollisions(game.combat, x, y)) return false;
    damagePlayer(game.player);
    return true;
}

// Substeps keep walking and Dash from crossing walls or enemy hitboxes.
void movePlayer(Game& game, int8_t dx, int8_t dy, bool dashing) {
    Player& player = game.player;
    const uint8_t absX = dx < 0 ? -dx : dx;
    const uint8_t absY = dy < 0 ? -dy : dy;
    const uint8_t distance = absX > absY ? absX : absY;
    const uint8_t steps = (distance + FIXED_ONE - 1) / FIXED_ONE;
    int8_t previousX = 0, previousY = 0;
    for (uint8_t step = 1; step <= steps && step <= MAX_MOVE_STEPS; ++step) {
        const int8_t partialX = int16_t(dx) * step / steps;
        const int8_t partialY = int16_t(dy) * step / steps;
        const int16_t nextX = wrapCoordinate(player.x + partialX - previousX, ARENA_WIDTH_FIXED);
        const int16_t nextY = wrapCoordinate(player.y + partialY - previousY, ARENA_HEIGHT_FIXED);
        previousX = partialX;
        previousY = partialY;
        if (dashing) {
            if (movementBlocked(game, nextX, nextY)) {
                player.dashFrames = 0;
                return;
            }
            player.x = nextX;
            player.y = nextY;
        } else {
            if (!movementBlocked(game, nextX, player.y)) player.x = nextX;
            if (!movementBlocked(game, player.x, nextY)) player.y = nextY;
        }
    }
}

} // namespace

void startGame(Game& game) {
    game = {};
    game.state = GameState::Playing;
    game.player.x = PLAYER_START_X * FIXED_ONE;
    game.player.y = PLAYER_START_Y * FIXED_ONE;
    setFacing(game.player, 1, 0);
    game.player.slots[0].ability = AbilityId::Dash;
    game.player.hp = game.player.maxHp = PLAYER_BASE_MAX_HP;
    resetCombat(game.combat);
}

void initShop(Game& game) {
    game.state = GameState::Shop;
    game.shop = {};
    for (uint8_t i = 0; i < 3; ++i) {
        game.shop.passiveChoices[i] = i + 1;
        game.shop.activeChoices[i] = i + 1;
    }
}

void applyShopChoice(Game& game) {
    if (game.state != GameState::Shop || game.shop.choosingSlot) return;
    const uint8_t idx = game.shop.selectedIndex;
    if (idx == 6) {
        ++game.combat.currentStage;
        game.combat.currentWave = 0;
        game.combat.stageCleared = false;
        game.combat.waveCompleted = false;
        game.combat.stageTimer = STAGE_TIME_FRAMES;
        game.combat.spawnTimer = SPAWN_DELAY_FRAMES;
        game.combat.freezeFrames = 0;
        game.combat.burstShots = 0;
        game.player.dashFrames = 0;
        game.state = GameState::Playing;
        return;
    }
    if (idx < 3) {
        if (game.shop.passiveBought || game.combat.playerScore < PASSIVE_PRICE) return;
        switch (static_cast<PassiveId>(game.shop.passiveChoices[idx])) {
        case PassiveId::DamageUp:
            if (game.passives.damageLevel == 3) return;
            ++game.passives.damageLevel;
            break;
        case PassiveId::MaxHpUp:
            if (game.player.maxHp == PLAYER_MAX_HP_CAP) return;
            ++game.passives.maxHpLevel;
            ++game.player.maxHp;
            ++game.player.hp;
            break;
        case PassiveId::MoveSpeedUp:
            if (game.passives.moveSpeedLevel == 3) return;
            ++game.passives.moveSpeedLevel;
            break;
        default: return;
        }
        game.combat.playerScore -= PASSIVE_PRICE;
        game.shop.passiveBought = true;
    } else if (idx < 6 && !game.shop.activeBought && game.combat.playerScore >= ACTIVE_PRICE) {
        game.shop.choosingSlot = true;
    }
}

void updateShop(Game& game, const InputFrame& input) {
    if (game.shop.choosingSlot) {
        if (input.moveX < 0) {
            game.shop.choosingSlot = false;
        } else if (input.activateA || input.activateB) {
            const uint8_t slot = input.activateA ? 0 : 1;
            const uint8_t choice = game.shop.activeChoices[game.shop.selectedIndex - 3];
            game.player.slots[slot] = {static_cast<AbilityId>(choice + 1), 0};
            game.combat.playerScore -= ACTIVE_PRICE;
            game.shop.activeBought = true;
            game.shop.choosingSlot = false;
        }
    } else {
        if (input.moveY != game.shop.previousMoveY) {
            if (input.moveY < 0 && game.shop.selectedIndex > 0) --game.shop.selectedIndex;
            if (input.moveY > 0 && game.shop.selectedIndex < 6) ++game.shop.selectedIndex;
        }
        if (input.activateB) game.shop.selectedIndex = 6;
        if (input.activateA || input.activateB) applyShopChoice(game);
    }
    game.shop.previousMoveY = input.moveY;
}

void updateGame(Game& game, const InputFrame& input) {
    // Удержание A+B на полсекунды ставит паузу, повторное удержание снимает её.
    if (input.holdA && input.holdB) {
        if (game.pauseHoldFrames < PAUSE_HOLD_FRAMES) ++game.pauseHoldFrames;
    } else {
        game.pauseHoldFrames = 0;
    }

    if (game.state == GameState::Title) {
        if (input.activateA || input.activateB) startGame(game);
        return;
    }
    if (game.state == GameState::GameOver || game.state == GameState::Win) {
        if (input.activateA || input.activateB) game.state = GameState::Title;
        return;
    }
    if (game.state == GameState::Shop) {
        updateShop(game, input);
        return;
    }
    if (game.state == GameState::StageCleared) {
        if (input.activateA || input.activateB) initShop(game);
        return;
    }
    if (game.state == GameState::Paused) {
        // Снятие с паузы так же, как вход: удержание A+B полсекунды.
        if (game.pauseHoldFrames >= PAUSE_HOLD_FRAMES) {
            game.state = GameState::Playing;
            game.pauseHoldFrames = 0;
        }
        return;
    }

    // Вход в паузу при удержании A+B полсекунды
    if (game.pauseHoldFrames >= PAUSE_HOLD_FRAMES) {
        game.state = GameState::Paused;
        game.pauseHoldFrames = 0;
        return;
    }
    Player& player = game.player;
    if (player.iframes) --player.iframes;
    for (uint8_t i = 0; i < ACTIVE_SLOT_COUNT; ++i) {
        if (player.slots[i].cooldown) --player.slots[i].cooldown;
    }
    if (checkPlayerEnemyCollisions(game.combat, player.x, player.y, true)) damagePlayer(player);
    if (!player.hp) { game.state = GameState::GameOver; return; }
    if (!player.dashFrames && (input.moveX || input.moveY)) setFacing(player, input.moveX, input.moveY);
    if (input.activateA) activateAbility(game, player.slots[0]);
    if (input.activateB) activateAbility(game, player.slots[1]);
    const bool dashing = player.dashFrames > 0;
    const int8_t dx = dashing ? getFacingX(player) : input.moveX;
    const int8_t dy = dashing ? getFacingY(player) : input.moveY;
    uint8_t speed = dashing ? DASH_SPEED : WALK_SPEED + game.passives.moveSpeedLevel * 2;
    if (dx && dy) speed = (uint16_t(speed) * 181 + 128) / 256;
    movePlayer(game, dx * speed, dy * speed, dashing);
    if (player.dashFrames) --player.dashFrames;
    if (!player.hp) { game.state = GameState::GameOver; return; }
    updateCombat(game.combat, player.x, player.y, SHOT_DAMAGE + game.passives.damageLevel);
    if (checkPlayerEnemyCollisions(game.combat, player.x, player.y, true)) damagePlayer(player);
    if (!player.hp) game.state = GameState::GameOver;
    else if (game.combat.stageCleared) {
        // Полное восстановление HP на паузе между стейджами.
        player.hp = player.maxHp;
        game.state = game.combat.currentStage + 1 == TOTAL_STAGES ? GameState::Win : GameState::StageCleared;
    }
}

} // namespace gc
