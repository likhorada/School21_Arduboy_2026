#include <Arduboy2.h>
#include "render.h"
#include "arena.h"
#include "assets/menu_screens.h"
#include "lzss.h"
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
        assert(display.pixels[34][UI_COLUMN_X + 7] == WHITE);
        assert(display.pixels[34][UI_COLUMN_X + 23] == BLACK);
        assert(display.pixels[35][UI_COLUMN_X + 9] == BLACK);
        game.player.slots[0].cooldown = 0;
        renderGame(display, game);
        assert(display.pixels[34][127 - 1] == WHITE);
    }
    game.combat.scoreOrbs[0] = {70 * FIXED_ONE, 20 * FIXED_ONE, 5, 1};
    renderGame(display, game);
    const uint8_t coinShape[] = {14, 23, 23, 31, 14};
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
    // Голден-проверка LZSS-декодера против tools/convert_assets.py: каждый экран
    // перекодируется в общий буфер (как на устройстве) и сверяется с CRC.
    {
        uint32_t crc_table[256];
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (uint8_t k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
        const auto crc32 = [&crc_table](const uint8_t* data, size_t n) -> uint32_t {
            uint32_t c = 0xFFFFFFFFu;
            for (size_t i = 0; i < n; ++i)
                c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        };
        uint8_t frame[MENU_SCREEN_BYTES] = {};
        for (uint8_t screen = 0; screen < MENU_SCREEN_COUNT; ++screen) {
            lzssDecodeScreens(screen + 1, frame, menu_screens);
            assert(crc32(frame, sizeof(frame)) == menu_screen_crcs[screen]);
        }
    }
    // Экран меню рисуется по декодированным пикселям и совпадает с буфером.
    const auto screenMatches = [&display](uint16_t count) {
        uint8_t frame[MENU_SCREEN_BYTES] = {};
        lzssDecodeScreens(count, frame, menu_screens);
        for (uint8_t y = 0; y < 64; ++y)
            for (uint8_t x = 0; x < 128; ++x)
                assert(display.pixels[y][x] ==
                       bool(frame[(y / 8) * 128 + x] & (1 << (y % 8))));
    };
    for (uint8_t sel = 0; sel < 4; ++sel) {
        game.state = GameState::Menu;
        game.menu.selectedIndex = sel;
        renderGame(display, game);
        screenMatches(2 + sel);
    }
    for (uint8_t sel = 0; sel < 3; ++sel) {
        game.state = GameState::SoundMenu;
        game.soundMenu.selectedIndex = sel;
        renderGame(display, game);
        screenMatches(6 + sel);
    }
    // Байтовые писатели спрайтов (AVR-путь рендера) обязаны давать те же
    // пиксели, что и попиксельный drawPixel в плоском буфере Arduboy.
    const auto refSet = [](uint8_t* buf, int x, int y) {
        assert(x >= 0 && x < 128 && y >= 0 && y < 64);
        buf[(y >> 3) * 128 + x] |= static_cast<uint8_t>(1 << (y & 7));
    };
    {
        uint8_t got[1024] = {}, want[1024] = {};
        for (unsigned bits = 0; bits < 256; ++bits) {
            for (uint8_t height = 1; height <= 8; ++height) {
                for (int y = -2; y < 70; ++y) {
                    for (int x = -3; x < 132; ++x) {
                        std::memset(got, 0, sizeof got);
                        std::memset(want, 0, sizeof want);
                        fillFrameColumn(got, x, y, static_cast<uint8_t>(bits), height);
                        for (uint8_t r = 0; r < height; ++r) {
                            if (!(bits & (1u << r))) continue;
                            if (x >= 0 && x < 128 && y + r >= 0 && y + r < 64)
                                refSet(want, x, static_cast<int>(y + r));
                        }
                        assert(std::memcmp(got, want, sizeof got) == 0);
                    }
                }
            }
        }
    }
    {
        // Тот же спрайт игрока, что в render.cpp (7 байт-колонок, бит = строка).
        static const uint8_t kPlayerBitmap[] = {0x3e, 0x7f, 0x55, 0x5d,
                                                0x55, 0x7f, 0x3e};
        static_assert(sizeof(kPlayerBitmap) == PLAYER_SIZE, "player sprite size");
        const auto wrapc = [](int v, int e) {
            v %= e;
            return v < 0 ? v + e : v;
        };
        uint8_t got[1024] = {}, want[1024] = {};
        for (int y = 0; y < ARENA_HEIGHT; ++y) {
            for (int x = 0; x < ARENA_WIDTH; ++x) {
                for (int8_t fx = -1; fx <= 1; ++fx) {
                    for (int8_t fy = -1; fy <= 1; ++fy) {
                        for (uint8_t dash = 0; dash < 2; ++dash) {
                            Player p = {};
                            p.x = x * FIXED_ONE;
                            p.y = y * FIXED_ONE;
                            setFacing(p, fx, fy);
                            p.dashFrames = dash ? 1 : 0;
                            std::memset(got, 0, sizeof got);
                            std::memset(want, 0, sizeof want);
                            renderPlayerFrame(got, p);
                            for (uint8_t column = 0; column < PLAYER_SIZE; ++column) {
                                for (uint8_t row = 0; row < PLAYER_SIZE; ++row) {
                                    const bool eye =
                                        column == 3 + fx * 2 && row == 3 + fy * 2;
                                    const bool lit =
                                        !eye && (dash ||
                                                 (kPlayerBitmap[column] &
                                                  (1u << row)));
                                    if (!lit) continue;
                                    refSet(want, wrapc(x + column, ARENA_WIDTH),
                                           wrapc(y + row, ARENA_HEIGHT));
                                }
                            }
                            assert(std::memcmp(got, want, sizeof got) == 0);
                        }
                    }
                }
            }
        }
    }
    // Перенос шрифта врагов [строка][колонка] → [колонка][строка] не должен
    // зеркалить глифы: pixel(r, c) обязан совпадать с эталоном.
    {
        static const uint8_t reference[18][5] = {
            {0x06, 0x09, 0x09, 0x09, 0x06}, // 0
            {0x02, 0x06, 0x02, 0x02, 0x07}, // 1
            {0x0e, 0x01, 0x06, 0x08, 0x0f}, // 2
            {0x0e, 0x01, 0x06, 0x01, 0x0e}, // 3
            {0x09, 0x09, 0x0f, 0x01, 0x01}, // 4
            {0x0f, 0x08, 0x0e, 0x01, 0x0e}, // 5
            {0x06, 0x08, 0x0e, 0x09, 0x06}, // 6
            {0x0f, 0x01, 0x02, 0x04, 0x04}, // 7
            {0x06, 0x09, 0x06, 0x09, 0x06}, // 8
            {0x06, 0x09, 0x07, 0x01, 0x06}, // 9
            {0x06, 0x09, 0x0f, 0x09, 0x09}, // A
            {0x0e, 0x09, 0x0e, 0x09, 0x0e}, // B
            {0x07, 0x08, 0x08, 0x08, 0x07}, // C
            {0x0e, 0x09, 0x09, 0x09, 0x0e}, // D
            {0x0f, 0x08, 0x0e, 0x08, 0x0f}, // E
            {0x0f, 0x08, 0x0e, 0x08, 0x08}, // F
            {0x09, 0x06, 0x06, 0x06, 0x09}, // x
            {0x04, 0x04, 0x0e, 0x09, 0x09}, // d
        };
        for (uint8_t g = 0; g < 18; ++g) {
            for (uint8_t c = 0; c < 4; ++c) {
                const uint8_t column = pgm_read_byte(&tinyFont[g * 4 + c]);
                for (uint8_t r = 0; r < 5; ++r) {
                    assert(bool(column & (1u << r)) ==
                           bool(reference[g][r] & (0x08u >> c)));
                }
            }
        }
    }
    std::puts("Render regressions passed: screen bounds, menu text, hearts, score, cooldowns, arena isolation.");
}
