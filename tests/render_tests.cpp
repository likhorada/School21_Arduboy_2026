#include <Arduboy2.h>
#include "render.h"
#include "arena.h"
#include "stages.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace gc;

int main() {
    Arduboy2 display;
    Game game = {};
    renderGame(display, game);
    startGame(game);
    game.combat.playerScore = 65535;
    const GameState states[] = {GameState::StageCleared, GameState::GameOver, GameState::Win};
    for (GameState state : states) {
        game.state = state;
        renderGame(display, game);
    }
    initShop(game);
    for (uint8_t selected = 0; selected <= 6; ++selected) {
        game.shop.selectedIndex = selected;
        for (uint8_t bought = 0; bought < 2; ++bought) {
            game.shop.passiveBought = game.shop.activeBought = bought;
            renderGame(display, game);
        }
        if (selected >= 3 && selected < 6) {
            game.shop.choosingSlot = true;
            for (uint8_t id = 0; id <= 4; ++id) {
                game.player.slots[0].ability = game.player.slots[1].ability = static_cast<AbilityId>(id);
                renderGame(display, game);
            }
            game.shop.choosingSlot = false;
        }
    }
    game.state = GameState::Playing;
    game.combat.spawnTimer = 0;
    game.player.slots[0] = {AbilityId::Dash, 0};
    game.player.slots[1] = {AbilityId::None, 0};
    // Проверяем реальные пиксели маркеров всех волн, без перекрытия игроком.
    game.player.iframes = IFRAME_DURATION;
    for (uint8_t stage = 0; stage < TOTAL_STAGES; ++stage) {
        for (uint8_t wave = 0; wave < getStageWaveCount(stage); ++wave) {
            game.combat.currentStage = stage;
            game.combat.currentWave = wave;
            game.combat.spawnTimer = 1;
            renderGame(display, game);
            for (uint8_t i = 0; i < getWaveEnemyCount(stage, wave); ++i) {
                uint8_t x, y;
                getSpawnPixel(stage, wave, i, x, y);
                assert(enemyPositionValid(stage, x * FIXED_ONE, y * FIXED_ONE));
                for (int8_t dy = -2; dy < 2; ++dy)
                    for (int8_t dx = -2; dx < 2; ++dx)
                        assert(display.pixels[y + dy][x + dx] == WHITE);
            }
        }
    }
    game.combat.spawnTimer = 0;
    game.player.iframes = 0;
    for (uint8_t hp = 0; hp <= 6; ++hp) {
        game.player.hp = hp;
        game.player.maxHp = 6;
        renderGame(display, game);
        for (uint8_t heart = 0; heart < 4; ++heart) {
            const uint8_t shapes[][5] = {{10, 21, 17, 10, 4}, {10, 31, 31, 14, 4}, {10, 31, 27, 14, 4}};
            for (uint8_t row = 0; row < 5; ++row)
                for (uint8_t col = 0; col < 5; ++col)
                    assert(display.pixels[9 + row][UI_COLUMN_X + heart * 6 + col] ==
                           bool(shapes[heartState(hp, heart)][row] & (16 >> col)));
        }
    }
    const uint16_t scores[] = {0, 9999, 10000, 11000, 65535};
    for (uint16_t score : scores) {
        game.combat.playerScore = score;
        renderGame(display, game);
        if (score == 10000) assert(std::strncmp(&display.text[2][17], "10k", 3) == 0);
        if (score == 11000) assert(std::strncmp(&display.text[2][17], "11k", 3) == 0);
    }
    for (uint8_t id = 1; id <= 4; ++id) {
        game.player.slots[0].ability = static_cast<AbilityId>(id);
        game.player.slots[0].cooldown = abilityCooldown(game.player.slots[0].ability);
        renderGame(display, game);
        assert(display.pixels[33][UI_COLUMN_X + 7] == WHITE);
        assert(display.pixels[33][UI_COLUMN_X + 23] == BLACK);
        assert(display.pixels[34][UI_COLUMN_X + 9] == BLACK);
        game.player.slots[0].cooldown = 0;
        renderGame(display, game);
        assert(display.pixels[34][127 - 1] == WHITE);
    }
    game.combat.scoreOrbs[0] = {70 * FIXED_ONE, 20 * FIXED_ONE, 5, 1};
    renderGame(display, game);
    const uint8_t coinShape[] = {14, 21, 21, 21, 14};
    for (uint8_t row = 0; row < 5; ++row)
        for (uint8_t col = 0; col < 5; ++col)
            assert(display.pixels[18 + row][68 + col] ==
                   bool(coinShape[row] & (16 >> col)));
    game.combat.scoreOrbs[0].lifetime = 0;
    uint8_t sidebar[64][24];
    renderGame(display, game);
    for (uint8_t y = 0; y < 64; ++y) std::memcpy(sidebar[y], &display.pixels[y][104], 24);
    // Ни один объект не рисуется поверх боковой панели.
    const uint8_t edges[][2] = {{0, 0}, {103, 0}, {0, 63}, {103, 63}};
    for (const auto& edge : edges) {
        game.player.x = edge[0] * 16;
        game.player.y = edge[1] * 16;
        spawnEnemy(game.combat.enemies[0], EnemyType::Splitter,
                   (edge[0] ? ARENA_WIDTH - ENEMY_HALF_WIDTH : ENEMY_HALF_WIDTH) * 16,
                   (edge[1] ? ARENA_HEIGHT - ENEMY_HALF_HEIGHT : ENEMY_HALF_HEIGHT) * 16, 16,
                   game.combat.currentStage);
        game.combat.scoreOrbs[0] = {int16_t(edge[0] * 16), int16_t(edge[1] * 16), 5, 1};
        game.combat.projectiles[0] = {int16_t(edge[0] * 16), int16_t(edge[1] * 16), 32, 0, 1};
        game.combat.spawnTimer = 1;
        renderGame(display, game);
        for (uint8_t y = 0; y < 64; ++y) assert(std::memcmp(sidebar[y], &display.pixels[y][104], 24) == 0);
    }
    std::puts("Render regressions passed: screen bounds, menu text, hearts, score, cooldowns, arena isolation.");
}
