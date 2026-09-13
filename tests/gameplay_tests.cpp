#include "arena.h"
#include "game.h"

#include <cassert>
#include <cstdio>

namespace {

using namespace gc;

const InputFrame idle = {0, 0, false, false, false, false};

int16_t normalized(int value, int extent) {
    return static_cast<int16_t>((value % extent + extent) % extent);
}

Game playingAt(int16_t x, int16_t y, uint8_t stage = 0) {
    Game game = {};
    startGame(game);
    game.combat.currentStage = stage;
    game.player.x = x;
    game.player.y = y;
    // Movement fixtures have no waves; the real stage timer still ticks normally.
    game.combat.spawnTimer = 0;
    game.combat.waveCompleted = true;
    assert(!playerBlocked(game.combat.currentStage, x, y));
    return game;
}

void assertSafe(const Game& game) {
    assert(game.player.x >= 0 && game.player.x < ARENA_WIDTH_FIXED);
    assert(game.player.y >= 0 && game.player.y < ARENA_HEIGHT_FIXED);
    assert(!playerBlocked(game.combat.currentStage, game.player.x, game.player.y));
}

void assertSamePlayer(const Player& actual, const Player& expected) {
    // Сравниваем поля, а не байты выравнивания структуры.
    assert(actual.x == expected.x && actual.y == expected.y);
    assert(getFacingX(actual) == getFacingX(expected));
    assert(getFacingY(actual) == getFacingY(expected));
    assert(actual.dashFrames == expected.dashFrames);
    for (unsigned i = 0; i < ACTIVE_SLOT_COUNT; ++i) {
        assert(actual.slots[i].ability == expected.slots[i].ability);
        assert(actual.slots[i].cooldown == expected.slots[i].cooldown);
    }
}

void assertFreshRun(const Game& game) {
    assert(game.state == GameState::Playing);
    assert(game.player.x == PLAYER_START_X * FIXED_ONE);
    assert(game.player.y == PLAYER_START_Y * FIXED_ONE);
    assert(getFacingX(game.player) == 1 && getFacingY(game.player) == 0);
    assert(game.player.dashFrames == 0);
    assert(game.player.slots[0].ability == AbilityId::Dash);
    assert(game.player.slots[1].ability == AbilityId::None);
    assert(game.player.slots[0].cooldown == 0);
    assert(game.player.slots[1].cooldown == 0);
    assertSafe(game);
}

void testFacingPacking() {
    Player player = {};
    player.x = 123;
    player.y = 456;
    player.dashFrames = 3;
    player.slots[0] = {AbilityId::None, 17};
    player.slots[1] = {AbilityId::Dash, 29};
    const Player initial = player;
    for (int8_t x = -1; x <= 1; ++x) {
        for (int8_t y = -1; y <= 1; ++y) {
            // Проверяем все переходы, чтобы старые биты не оставались.
            for (int8_t nextX = -1; nextX <= 1; ++nextX) {
                for (int8_t nextY = -1; nextY <= 1; ++nextY) {
                    setFacing(player, x, y);
                    assert(getFacingX(player) == x && getFacingY(player) == y);
                    setFacing(player, nextX, nextY);
                    assert(getFacingX(player) == nextX && getFacingY(player) == nextY);
                    assert(player.x == initial.x && player.y == initial.y);
                    assert(player.dashFrames == initial.dashFrames);
                    for (unsigned slot = 0; slot < ACTIVE_SLOT_COUNT; ++slot) {
                        assert(player.slots[slot].ability == initial.slots[slot].ability);
                        assert(player.slots[slot].cooldown == initial.slots[slot].cooldown);
                    }
                }
            }
        }
    }
}

void testPassivePackingAndMovement() {
    PlayerPassives passives = {};
    const PassiveId ids[] = {
        PassiveId::CompilerOptimization, PassiveId::Overclock,
        PassiveId::OptimizedBuild, PassiveId::MemoryFragmentation,
        PassiveId::CollectionRange, PassiveId::RamCapacity
    };
    for (uint8_t selected = 0; selected < 6; ++selected) {
        for (uint8_t level = 0; level < 4; ++level) {
            setPassiveLevel(passives, ids[selected], level);
            assert(passiveLevel(passives, ids[selected]) == level);
            for (uint8_t other = 0; other < 6; ++other)
                if (other != selected) assert(passiveLevel(passives, ids[other]) == 0);
            setPassiveLevel(passives, ids[selected], 0);
        }
    }
    assert(passiveLevel(passives, PassiveId::None) == 0);

    for (uint8_t level = 0; level <= passiveCap(PassiveId::CompilerOptimization);
         ++level) {
        Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
        setPassiveLevel(game.passives, PassiveId::CompilerOptimization, level);
        updateGame(game, {1, 0, false, false, false, false});
        assert(game.player.x == 60 * FIXED_ONE + WALK_SPEED * (5 + level) / 5);
    }
}

void testIntroMenuAndReset() {
    Game game = {};
    assert(game.state == GameState::Intro);
    game.player.x = 123;
    game.player.y = 456;
    setFacing(game.player, -1, 1);
    game.player.dashFrames = 3;
    game.player.slots[0] = {AbilityId::None, 17};
    game.player.slots[1] = {AbilityId::Dash, 29};
    const Game intro = game;
    for (unsigned frame = 0; frame < 300; ++frame) {
        updateGame(game, idle);
        assert(game.state == GameState::Intro);
        assertSamePlayer(game.player, intro.player);
    }
    updateGame(game, {0, 0, true, false, false, false});
    assert(game.state == GameState::Menu && game.menu.selectedIndex == 0);
    updateGame(game, idle);
    updateGame(game, {0, 0, true, false, false, false});
    assertFreshRun(game);

    game = intro;
    startGame(game);
    assertFreshRun(game);
    startGame(game);
    assertFreshRun(game);
}

void testMainMenu() {
    const InputFrame pressA = {0, 0, true, false, false, false};
    const InputFrame pressB = {0, 0, false, true, false, false};
    const InputFrame down = {0, 1, false, false, false, false};
    Game game = {};

    updateGame(game, pressA);
    assert(game.state == GameState::Menu && game.menu.selectedIndex == 0);

    updateGame(game, down);
    assert(game.menu.selectedIndex == 1);
    updateGame(game, down); // Held direction does not repeat.
    assert(game.menu.selectedIndex == 1);
    updateGame(game, idle);
    updateGame(game, pressA);
    assert(game.state == GameState::About);
    updateGame(game, pressB);
    assert(game.state == GameState::Menu);

    updateGame(game, down);
    assert(game.menu.selectedIndex == 2);
    updateGame(game, idle);
    updateGame(game, pressA);
    assert(game.state == GameState::SoundMenu);
    updateGame(game, pressA);
    assert(game.soundEnabled == 1);
    updateGame(game, down);
    updateGame(game, idle);
    updateGame(game, pressA);
    assert(game.soundEnabled == 0);
    updateGame(game, down);
    updateGame(game, idle);
    updateGame(game, pressA);
    assert(game.state == GameState::Menu);

    updateGame(game, down);
    assert(game.menu.selectedIndex == 3);
    updateGame(game, idle);
    updateGame(game, pressA);
    assert(game.state == GameState::Intro);
}

void testWalking() {
    for (int8_t dx = -1; dx <= 1; ++dx) {
        for (int8_t dy = -1; dy <= 1; ++dy) {
            Game game = playingAt(60 * FIXED_ONE + 3, 25 * FIXED_ONE + 7);
            const Player initial = game.player;
            const InputFrame input = {dx, dy, false, false, false, false};
            const int speed = dx != 0 && dy != 0 ? 11 : 16;
            for (int frame = 1; frame <= 8; ++frame) {
                updateGame(game, input);
                assert(game.player.x == initial.x + frame * dx * speed);
                assert(game.player.y == initial.y + frame * dy * speed);
                assert(game.player.dashFrames == 0);
                assert(game.player.slots[0].cooldown == 0);
                assertSafe(game);
            }
            if (dx != 0 || dy != 0) {
                assert(getFacingX(game.player) == dx && getFacingY(game.player) == dy);
                const int distanceSquared = (dx * dx + dy * dy) * speed * speed;
                const int error = WALK_SPEED * WALK_SPEED - distanceSquared;
                assert(error >= 0 && error <= 2 * WALK_SPEED);
            }
            const Player stopped = game.player;
            updateGame(game, idle);
            assertSamePlayer(game.player, stopped);
        }
    }
}

void testWraps() {
    const int extents[] = {ARENA_WIDTH_FIXED, ARENA_HEIGHT_FIXED};
    for (unsigned axis = 0; axis < 2; ++axis) {
        const int extent = extents[axis];
        for (int coordinate = -extent; coordinate < 2 * extent; ++coordinate) {
            assert(wrapCoordinate(coordinate, extent) == normalized(coordinate, extent));
        }
    }
    for (unsigned dash = 0; dash < 2; ++dash) {
        for (int8_t dx = -1; dx <= 1; ++dx) {
            for (int8_t dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const bool diagonal = dx != 0 && dy != 0;
                const int speed = dash ? (diagonal ? 45 : 64) : (diagonal ? 11 : 16);
                for (int offset = 1; offset <= speed; ++offset) {
                    const int16_t x = dx < 0 ? offset - 1 :
                        (dx > 0 ? ARENA_WIDTH_FIXED - offset : 60 * FIXED_ONE + 3);
                    const int16_t y = dy < 0 ? offset - 1 :
                        (dy > 0 ? ARENA_HEIGHT_FIXED - offset : 45 * FIXED_ONE + 7);
                    Game game = playingAt(x, y);
                    const InputFrame input = {dx, dy, dash != 0, false, false, false};
                    updateGame(game, input);
                    assert(game.player.x == normalized(x + dx * speed, ARENA_WIDTH_FIXED));
                    assert(game.player.y == normalized(y + dy * speed, ARENA_HEIGHT_FIXED));
                    assert(game.player.dashFrames == (dash ? DASH_DURATION - 1 : 0));
                    assertSafe(game);
                }
            }
        }
    }
}

// Независимая проверка: сдвигаем бокс игрока, а не препятствия. Сравнивает
// пиксельную маску стены стейджа с попиксельной проверкой playerBlocked.
bool referenceStageBlocked(unsigned stage, int x, int y) {
    const int size = PLAYER_SIZE * FIXED_ONE;
    const int startX = x / FIXED_ONE;
    const int endX = (x + size - 1) / FIXED_ONE;
    const int startY = y / FIXED_ONE;
    const int endY = (y + size - 1) / FIXED_ONE;
    for (int py = startY; py <= endY; ++py) {
        for (int px = startX; px <= endX; ++px) {
            const int wx = (px % ARENA_WIDTH + ARENA_WIDTH) % ARENA_WIDTH;
            const int wy = (py % ARENA_HEIGHT + ARENA_HEIGHT) % ARENA_HEIGHT;
            if (stageWallPixel(uint8_t(stage), uint8_t(wx), uint8_t(wy))) {
                return true;
            }
        }
    }
    return false;
}

void testStageWalls() {
    // Стейдж 1 — пустое поле, а вот стейджи 2 и 3 читаются из масок.
    assert(stageWallPixel(0, 0, 0) == false);
    assert(stageWallPixel(0, 72, 33) == false);
    // Опорные пиксели стейджа 2: вертикали col52/col59 и диагональ col49.
    assert(stageWallPixel(1, 52, 0));
    assert(stageWallPixel(1, 52, 12));
    assert(!stageWallPixel(1, 52, 13));
    assert(stageWallPixel(1, 59, 8));
    assert(stageWallPixel(1, 59, 19));
    assert(!stageWallPixel(1, 59, 20));
    assert(stageWallPixel(1, 49, 14));
    assert(!stageWallPixel(1, 49, 17));
    assert(!stageWallPixel(1, 53, 8));
    // Стейдж 3: колонны по краям и диагональные распорки.
    assert(stageWallPixel(2, 87, 0));
    assert(stageWallPixel(2, 87, 60));
    assert(!stageWallPixel(2, 87, 4));
    assert(stageWallPixel(2, 94, 31));
    assert(!stageWallPixel(2, 94, 30));
    assert(stageWallPixel(2, 15, 60));
    assert(stageWallPixel(2, 21, 21));
    // Горизонтальная распорка через всю ширину на row 27.
    assert(stageWallPixel(2, 0, 27));
    assert(!stageWallPixel(2, 0, 28));
    // Вне диапазона маски нет.
    assert(stageWallPixel(2, 103, 15) == false);
    assert(stageWallPixel(1, 45, 64) == false);
    // Сверху вниз: playerBlocked согласен с попиксельным оракулом на всех
    // стейджах и всех долях пикселя.
    for (uint8_t stage = 0; stage <= 2; ++stage) {
        for (int y = 0; y < ARENA_HEIGHT_FIXED; y += 3) {
            for (int x = 0; x < ARENA_WIDTH_FIXED; x += 3) {
                const int fractions[] = {0, 1, FIXED_ONE - 1};
                for (unsigned fx = 0; fx < 3; ++fx) {
                    for (unsigned fy = 0; fy < 3; ++fy) {
                        const int px = x + fractions[fx];
                        const int py = y + fractions[fy];
                        assert(playerBlocked(stage, px, py) ==
                               referenceStageBlocked(stage, px, py));
                    }
                }
            }
        }
    }
}

void testSlidingAndDashCollision() {
    constexpr unsigned stage = 1;
    // Dash влево в восточный гребень col59 (rows 0..19): с (60,5) гасится
    // внутри первого кадра, игрок упирается в x=60*16.
    {
        Game game = playingAt(60 * 16, 5 * 16, stage);
        const InputFrame dash = {-1, 0, true, false, false, false};
        updateGame(game, dash);
        assert(game.player.x == 60 * 16);
        assert(game.player.y == 5 * 16);
        assert(game.player.dashFrames == 0);
        assert(game.player.slots[0].cooldown == DASH_COOLDOWN);
        assertSafe(game);
    }
    // Dash с дистанции проходит все подшаги и встаёт у той же стены.
    {
        Game game = playingAt(64 * 16, 5 * 16, stage);
        const InputFrame dash = {-1, 0, true, false, false, false};
        updateGame(game, dash);
        assert(game.player.x == 60 * 16 && game.player.y == 5 * 16);
        assert(game.player.dashFrames == DASH_DURATION - 1);
        for (unsigned frame = 1; frame < DASH_DURATION; ++frame) {
            updateGame(game, dash);
        }
        assert(game.player.x == 60 * 16 && game.player.dashFrames == 0);
        assertSafe(game);
    }
    // Ходьба влево той же дорогой останавливается на том же пикселе.
    {
        Game game = playingAt(64 * 16, 5 * 16, stage);
        const InputFrame walk = {-1, 0, false, false, false, false};
        for (unsigned frame = 0; frame < 12; ++frame) {
            updateGame(game, walk);
        }
        assert(game.player.x == 60 * 16 && game.player.y == 5 * 16);
        assertSafe(game);
        const Player hit = game.player;
        for (unsigned frame = 0; frame < 3; ++frame) {
            updateGame(game, walk);
        }
        assertSamePlayer(game.player, hit);
    }
    // Диагональная ходьба (скорость 11) скользит вниз по гребню col59: пока
    // бокс перекрывает верх стены, x прибит, y растёт; распорка col60
    // (rows 19..20) застопоривает бокс снизу.
    {
        Game game = playingAt(64 * 16, 5 * 16, stage);
        const InputFrame diag = {-1, 1, false, false, false, false};
        updateGame(game, diag);
        assert(game.player.x == 1013 && game.player.y == 91);
        assertSafe(game);
        for (int frame = 1; frame <= 4; ++frame) {
            updateGame(game, diag);
            assert(game.player.x == 1013 - 11 * frame);
            assertSafe(game);
        }
        for (int frame = 1; frame <= 4; ++frame) {
            updateGame(game, diag);
            assert(game.player.x == 969);
            assert(game.player.y == 135 + 11 * frame);
            assertSafe(game);
        }
        for (unsigned frame = 0; frame < 4; ++frame) {
            updateGame(game, diag);
        }
        assert(game.player.x == 969 && game.player.y == 190);
        const Player hit = game.player;
        for (unsigned frame = 0; frame < 4; ++frame) {
            updateGame(game, diag);
        }
        assertSamePlayer(game.player, hit);
    }
    // Ниже гребня (rows 20..44) стена отсутствует: ходьба влево свободна.
    {
        Game game = playingAt(70 * 16, 30 * 16, stage);
        const InputFrame walk = {-1, 0, false, false, false, false};
        for (unsigned frame = 0; frame < 6; ++frame) {
            updateGame(game, walk);
        }
        assert(game.player.x == 64 * 16);
        assertSafe(game);
    }
}

void testDashMotion() {
    assert(DASH_DURATION == 6);
    assert(DASH_SPEED == 64);
    for (int8_t dx = -1; dx <= 1; ++dx) {
        for (int8_t dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            // Свободные пути на шесть кадров выбраны без кода столкновений.
            const int y = dy == 0 || (dx == -1 && dy == -1) ? 45 :
                (dx == -1 && dy == 1 ? 20 : (dx == 1 && dy == 1 ? 5 : 25));
            Game game = playingAt(60 * FIXED_ONE + 3, y * FIXED_ONE + 7);
            const Player initial = game.player;
            const int speed = dx != 0 && dy != 0 ? 45 : 64;
            for (int tick = 1; tick <= 6; ++tick) {
                const InputFrame input = {
                    static_cast<int8_t>(tick == 1 ? dx : -dx),
                    static_cast<int8_t>(tick == 1 ? dy : -dy), tick == 1, false, false, false
                };
                updateGame(game, input);
                assert(game.player.x == initial.x + tick * dx * speed);
                assert(game.player.y == initial.y + tick * dy * speed);
                assert(getFacingX(game.player) == dx && getFacingY(game.player) == dy);
                assert(game.player.dashFrames == 6 - tick);
                assert(game.player.slots[0].cooldown == DASH_COOLDOWN - tick / COOLDOWN_TICK_FRAMES);
                assertSafe(game);
            }
            const int distanceSquared = (dx * dx + dy * dy) * speed * speed;
            const int error = DASH_SPEED * DASH_SPEED - distanceSquared;
            assert(error >= 0 && error <= 2 * DASH_SPEED);
            const Player end = game.player;
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            const InputFrame reverse = {
                static_cast<int8_t>(-dx), static_cast<int8_t>(-dy), false, false, false, false
            };
            updateGame(game, reverse);
            const int walkSpeed = dx != 0 && dy != 0 ? 11 : 16;
            assert(game.player.x == end.x - dx * walkSpeed);
            assert(game.player.y == end.y - dy * walkSpeed);
            assert(getFacingX(game.player) == -dx && getFacingY(game.player) == -dy);
        }
    }
    Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
    const InputFrame faceLeft = {-1, 0, false, false, false, false};
    updateGame(game, faceLeft);
    const int16_t x = game.player.x;
    const InputFrame stationaryDash = {0, 0, true, false, false, false};
    for (int tick = 1; tick <= 6; ++tick) {
        updateGame(game, tick == 1 ? stationaryDash : idle);
        assert(game.player.x == x - tick * DASH_SPEED);
        assert(game.player.y == 45 * FIXED_ONE);
        assert(getFacingX(game.player) == -1 && getFacingY(game.player) == 0);
    }
}

void testSlotsAndInputEdges() {
    assert(ACTIVE_SLOT_COUNT == 2);
    for (unsigned slot = 0; slot < ACTIVE_SLOT_COUNT; ++slot) {
        const unsigned other = 1 - slot;
        Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
        game.player.slots[slot] = {AbilityId::Dash, 0};
        game.player.slots[other] = {AbilityId::None, 19};
        const InputFrame press = {1, 0, slot == 0, slot == 1, false, false};
        const InputFrame pressIdle = {0, 0, slot == 0, slot == 1, false, false};
        updateGame(game, press);
        assert(game.player.x == 60 * FIXED_ONE + DASH_SPEED);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN);
        assert(game.player.slots[other].cooldown == 19);
        for (int tick = 2; tick <= 6; ++tick) {
            updateGame(game, idle);
        }
        const Player end = game.player;
        // Нажатие во время перезарядки не запускает новый Dash.
        updateGame(game, pressIdle);
        assert(game.player.dashFrames == 0);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 1);
        for (unsigned frame = 0; frame < COOLDOWN_TICK_FRAMES - 1; ++frame) {
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            assert(game.player.dashFrames == 0);
        }
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 2);
        for (unsigned frame = 0;
             frame < COOLDOWN_TICK_FRAMES * (DASH_COOLDOWN - 2); ++frame)
            updateGame(game, idle);
        assert(game.player.slots[slot].cooldown == 0);
        assert(game.player.slots[other].cooldown == 0);
        updateGame(game, pressIdle);
        assert(game.player.x == end.x + DASH_SPEED);
        assert(game.player.dashFrames == 5);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN);
        assert(game.player.slots[other].cooldown == 0);

        // Два Dash имеют отдельные перезарядки и не продлевают текущий рывок.
        game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
        game.player.slots[0].ability = AbilityId::Dash;
        game.player.slots[1].ability = AbilityId::Dash;
        updateGame(game, press);
        const InputFrame otherPress = {0, 0, other == 0, other == 1, false, false};
        updateGame(game, otherPress);
        assert(game.player.dashFrames == 4);
        assert(game.player.slots[other].cooldown == 0);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN);
        for (unsigned tick = 0; tick < 4; ++tick) {
            updateGame(game, idle);
        }
        const int16_t beforeOther = game.player.x;
        updateGame(game, otherPress);
        assert(game.player.x == beforeOther + DASH_SPEED);
        assert(game.player.dashFrames == 5);
        assert(game.player.slots[other].cooldown == DASH_COOLDOWN);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 1);

        for (unsigned tick = 0; tick < 5; ++tick) {
            updateGame(game, idle);
        }
        game.player.slots[0].ability = AbilityId::None;
        game.player.slots[1].ability = AbilityId::None;
        const Player unequipped = game.player;
        const InputFrame both = {0, 0, true, true, false, false};
        for (unsigned frame = 0; frame < COOLDOWN_TICK_FRAMES * DASH_COOLDOWN; ++frame) {
            updateGame(game, both);
            assert(game.player.x == unequipped.x && game.player.y == unequipped.y);
            assert(game.player.dashFrames == 0);
        }
        assert(game.player.slots[0].cooldown == 0 && game.player.slots[1].cooldown == 0);
    }

    // После снятия Dash кнопки A/B не должны запускать скрытый рывок.
    Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
    game.player.slots[0].ability = AbilityId::None;
    for (unsigned buttons = 1; buttons <= 3; ++buttons) {
        const InputFrame input = {0, 0, (buttons & 1) != 0, (buttons & 2) != 0, false, false};
        const Player before = game.player;
        updateGame(game, input);
        assertSamePlayer(game.player, before);
    }
    const InputFrame walkBoth = {1, 0, true, true, false, false};
    updateGame(game, walkBoth);
    assert(game.player.x == 61 * FIXED_ONE);
    assert(game.player.dashFrames == 0);
    assert(game.player.slots[0].cooldown == 0 && game.player.slots[1].cooldown == 0);

    // Два нажатия дают один рывок; отклонённый слот не уходит на перезарядку.
    for (unsigned abilities = 0; abilities < 4; ++abilities) {
        game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
        game.player.slots[0].ability = abilities & 1 ? AbilityId::Dash : AbilityId::None;
        game.player.slots[1].ability = abilities & 2 ? AbilityId::Dash : AbilityId::None;
        updateGame(game, walkBoth);
        assert(game.player.x == 60 * FIXED_ONE + (abilities ? DASH_SPEED : WALK_SPEED));
        assert(game.player.dashFrames == (abilities ? 5 : 0));
        assert(game.player.slots[0].cooldown == (abilities & 1 ? DASH_COOLDOWN : 0));
        assert(game.player.slots[1].cooldown == (abilities == 2 ? DASH_COOLDOWN : 0));
        for (unsigned tick = 0; tick < 5; ++tick) {
            updateGame(game, idle);
        }
        const Player end = game.player;
        for (unsigned frame = 0; frame < COOLDOWN_TICK_FRAMES * DASH_COOLDOWN; ++frame) {
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            assert(game.player.dashFrames == 0);
        }
        assert(game.player.slots[0].cooldown == 0 && game.player.slots[1].cooldown == 0);
    }

    // Dash grants immunity for the full movement window, including enemy contact.
    game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
    updateGame(game, {1, 0, true, false, false, false});
    const uint8_t hp = game.player.hp;
    const int16_t centerX = game.player.x + PLAYER_SIZE * FIXED_ONE / 2;
    const int16_t centerY = game.player.y + PLAYER_SIZE * FIXED_ONE / 2;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, centerX, centerY, 0,
               game.combat.currentStage);
    updateGame(game, idle);
    assert(game.player.hp == hp);
    assert(game.player.iframes > 0);
}

void testMapConnectivity() {
    // На ПК обходим позиции игрока по пикселям с учётом переходов через края.
    // Стейдж 1 — тёк-ток-тое: внешняя часть арены должна быть связной (кольца
    // внутри O-фигур — закрытые карманы, они не считаются игровым полем).
    const unsigned stage = 1;
    const unsigned cells = ARENA_WIDTH * ARENA_HEIGHT;
    bool visited[cells] = {};
    uint16_t queue[cells] = {};
    unsigned read = 0;
    unsigned count = 1;
    queue[0] = PLAYER_START_Y * ARENA_WIDTH + PLAYER_START_X;
    visited[queue[0]] = true;
    const int dx[] = {-1, 1, 0, 0};
    const int dy[] = {0, 0, -1, 1};
    while (read < count) {
        const unsigned cell = queue[read++];
        const int x = cell % ARENA_WIDTH;
        const int y = cell / ARENA_WIDTH;
        for (unsigned direction = 0; direction < 4; ++direction) {
            const int nx = normalized(x + dx[direction], ARENA_WIDTH);
            const int ny = normalized(y + dy[direction], ARENA_HEIGHT);
            const unsigned next = ny * ARENA_WIDTH + nx;
            if (!visited[next] &&
                !referenceStageBlocked(stage, nx * FIXED_ONE, ny * FIXED_ONE)) {
                assert(count < cells);
                visited[next] = true;
                queue[count++] = static_cast<uint16_t>(next);
            }
        }
    }
    unsigned freeCells = 0;
    for (unsigned cell = 0; cell < cells; ++cell) {
        const bool free = !referenceStageBlocked(stage,
                                                 (cell % ARENA_WIDTH) * FIXED_ONE,
                                                 (cell / ARENA_WIDTH) * FIXED_ONE);
        // Закрытые кольца O-фигур не достигаются, но их немного.
        if (free) {
            ++freeCells;
        }
        assert(!visited[cell] || free);
    }
    // Большая часть свободных клеток достижима от спавна игрока.
    assert(count >= freeCells - 120);
    assert(count >= freeCells * 19 / 20);
    std::printf("Map connectivity: %u/%u free integer-pixel positions reachable\n",
                count, freeCells);
}

void testRandomizedInput() {
    uint32_t random = 0x6d2b79f5u;
    unsigned dashTicks = 0;
    unsigned movedTicks = 0;
    for (unsigned run = 0; run < 4; ++run) {
        Game game = {};
        startGame(game);
        game.combat.spawnTimer = 0;
        game.combat.waveCompleted = true;
        game.player.slots[0].ability = run & 1 ? AbilityId::Dash : AbilityId::None;
        game.player.slots[1].ability = run & 2 ? AbilityId::Dash : AbilityId::None;
        InputFrame input = idle;
        bool previousA = false;
        bool previousB = false;
        for (unsigned frame = 0; frame < 25000; ++frame) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            // Держим направление несколько кадров, чтобы игрок двигался по карте.
            if (frame % 23 == 0) {
                input.moveX = static_cast<int8_t>(static_cast<int>(random % 3) - 1);
                input.moveY = static_cast<int8_t>(static_cast<int>((random >> 8) % 3) - 1);
            }
            const bool heldA = (random & 0x10000u) != 0;
            const bool heldB = (random & 0x20000u) != 0;
            input.activateA = heldA && !previousA;
            input.activateB = heldB && !previousB;
            previousA = heldA;
            previousB = heldB;
            const Player before = game.player;
            updateGame(game, input);
            // Новое состояние: после прохождения стейджа игра ждёт нажатия A/B.
            if (game.state == GameState::StageCleared) {
                const InputFrame continuePressed = {0, 0, true, false, false, false};
                updateGame(game, continuePressed);
            }
            assert(game.state == GameState::Playing);
            assertSafe(game);
            assert(!referenceStageBlocked(game.combat.currentStage, game.player.x,
                                          game.player.y));
            assert(game.player.dashFrames < DASH_DURATION);
            for (unsigned slot = 0; slot < ACTIVE_SLOT_COUNT; ++slot) {
                assert(game.player.slots[slot].cooldown <= DASH_COOLDOWN);
                assert(game.player.slots[slot].ability == before.slots[slot].ability);
                if (game.player.slots[slot].ability == AbilityId::None) {
                    assert(game.player.slots[slot].cooldown == 0);
                }
            }
            dashTicks += game.player.dashFrames > 0;
            movedTicks += game.player.x != before.x || game.player.y != before.y;
        }
    }
    assert(dashTicks > 100 && movedTicks > 10000);
    std::printf("Randomized input: 100000 frames, %u moving, %u with active Dash\n",
                movedTicks, dashTicks);
}

// Пауза: вход и выход по симметричному удержанию A+B на полсекунды.
void testPause() {
    const InputFrame hold = {0, 0, false, false, true, true};
    const InputFrame moveOnly = {1, 0, false, false, false, false};

    Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
    for (unsigned frame = 0; frame < PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, hold);
        if (frame + 1 < PAUSE_HOLD_FRAMES) {
            assert(game.state == GameState::Playing);
        }
    }
    assert(game.state == GameState::Paused);

    // Во время паузы мир заморожен: ходьба, таймеры, кулдауны и враги.
    const Player frozen = game.player;
    game.player.slots[1].cooldown = 5;
    game.player.iframes = IFRAME_DURATION;
    game.combat.stageTimer = 321;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 40 * FIXED_ONE,
               30 * FIXED_ONE, 0, game.combat.currentStage);
    const Enemy frozenEnemy = game.combat.enemies[0];
    for (unsigned frame = 0; frame < 2 * PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, moveOnly);
        assert(game.state == GameState::Paused);
        assert(game.player.x == frozen.x && game.player.y == frozen.y);
        assert(game.player.slots[1].cooldown == 5);
        assert(game.player.iframes == IFRAME_DURATION);
        assert(game.combat.stageTimer == 321);
        assert(game.combat.enemies[0].x == frozenEnemy.x);
        assert(game.combat.enemies[0].y == frozenEnemy.y);
        assert(getEnemyType(game.combat.enemies[0]) ==
               getEnemyType(frozenEnemy));
    }

    // Простое отпускание кнопок паузу не снимает.
    for (unsigned frame = 0; frame < PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, idle);
        assert(game.state == GameState::Paused);
    }

    // Повторное удержание A+B полсекунды снимает паузу.
    for (unsigned frame = 0; frame < PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, hold);
    }
    assert(game.state == GameState::Playing);
    assert(game.player.x == frozen.x && game.player.y == frozen.y);
    assert(game.player.slots[1].cooldown == 5);
    assert(game.combat.stageTimer == 321);

    // Ещё один полный цикл пауза-рестарт работает и обнуляет счётчик.
    for (unsigned frame = 0; frame < PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, hold);
    }
    assert(game.state == GameState::Paused);
    updateGame(game, idle);
    assert(game.state == GameState::Paused);
    updateGame(game, hold);
    for (unsigned frame = 0; frame + 2 < PAUSE_HOLD_FRAMES; ++frame) {
        updateGame(game, hold);
        assert(game.state == GameState::Paused);
    }
    updateGame(game, hold);
    assert(game.state == GameState::Playing);
}

void testKonamiCheat() {
    Game game = playingAt(5 * FIXED_ONE, 5 * FIXED_ONE);
    const InputFrame up = {0, -1, false, false, false, false};
    const InputFrame down = {0, 1, false, false, false, false};
    const InputFrame left = {-1, 0, false, false, false, false};
    const InputFrame right = {1, 0, false, false, false, false};
    const InputFrame pressB = {0, 0, false, true, false, true};
    const InputFrame pressA = {0, 0, true, false, true, false};
    // Нажатия крестовины — по фронту, поэтому между ними отпускаем.
    const InputFrame taps[] = {up, up, down, down, left, right, left, right,
                               pressB, pressA};
    assert(!game.player.invincible);
    for (const InputFrame& tap : taps) {
        updateGame(game, tap);
        updateGame(game, idle);
    }
    assert(game.player.invincible == 1);
    assert(game.combat.playerScore == KONAMI_BALANCE);
    // Бессмертие блокирует урон, не тратя HP.
    const uint8_t hp = game.player.hp;
    const uint8_t iframes = game.player.iframes;
    damagePlayer(game.player);
    assert(game.player.hp == hp && game.player.iframes == iframes);

    // Ошибка в середине сбрасывает прогресс: после ←→←→ жмём ↓ вместо B.
    game = playingAt(5 * FIXED_ONE, 5 * FIXED_ONE);
    assert(!game.player.invincible);
    updateGame(game, up); updateGame(game, idle);
    updateGame(game, up); updateGame(game, idle);
    updateGame(game, down); updateGame(game, idle);
    updateGame(game, down); updateGame(game, idle);
    updateGame(game, left); updateGame(game, idle);
    updateGame(game, right); updateGame(game, idle);
    updateGame(game, left); updateGame(game, idle);
    updateGame(game, right); updateGame(game, idle);
    updateGame(game, down); updateGame(game, idle); // неверно: ожидается B
    assert(game.konamiProgress == 0 && !game.player.invincible);
    // Корректный полный код после сброса даёт бессмертие.
    for (const InputFrame& tap : taps) { updateGame(game, tap); updateGame(game, idle); }
    assert(game.player.invincible == 1);
    assert(game.combat.playerScore == KONAMI_BALANCE);

    // Повторный код выключает бессмертие (тумблер для дебага).
    for (const InputFrame& tap : taps) {
        updateGame(game, tap);
        updateGame(game, idle);
    }
    assert(game.player.invincible == 0);

    // Пауза между нажатиями дольше таймаута сбрасывает последовательность.
    game = playingAt(5 * FIXED_ONE, 5 * FIXED_ONE);
    updateGame(game, up); updateGame(game, idle);
    updateGame(game, up); updateGame(game, idle);
    assert(game.konamiProgress == 2);
    for (unsigned frame = 0; frame <= KONAMI_TIMEOUT; ++frame) updateGame(game, idle);
    assert(game.konamiProgress == 0);
}

// Переход на следующий стейдж из магазина должен вернуть игрока
// в стартовый центр, а не оставить его на месте прошлого стейджа: там его
// позиция может оказаться в стене или у самого края карты (как на третьем
// стейдже с плотным угловым артом).
void testStageTransitionRepositionsToSpawn() {
    Game game = {};
    startGame(game);
    game.state = GameState::Shop;
    game.shop = {};
    game.combat.currentStage = 1; // стоим на пороге третьего стейджа
    game.player.x = 8 * FIXED_ONE; // угловая позиция с прошлого поля
    game.player.y = 8 * FIXED_ONE;
    // Карусель прототипа: первый B переключает категорию на активную, второй
    // B завершает магазин и запускает следующий стейдж.
    const InputFrame pressB = {0, 0, false, true, true, true};
    updateGame(game, pressB);
    updateGame(game, pressB);
    assert(game.state == GameState::Playing);
    assert(game.combat.currentStage == 2);
    assert(game.player.x == PLAYER_START_X * FIXED_ONE);
    assert(game.player.y == PLAYER_START_Y * FIXED_ONE);
    assert(getFacingX(game.player) == 1 && getFacingY(game.player) == 0);
    assert(!playerBlocked(game.combat.currentStage, game.player.x, game.player.y));
}

} // Конец анонимного пространства имён.

int main() {
    testFacingPacking();
    testPassivePackingAndMovement();
    testIntroMenuAndReset();
    testMainMenu();
    testWalking();
    testWraps();
    testStageWalls();
    testSlidingAndDashCollision();
    testDashMotion();
    testSlotsAndInputEdges();
    testMapConnectivity();
    testStageTransitionRepositionsToSpawn();
    testRandomizedInput();
    testPause();
    testKonamiCheat();
    std::puts("All gameplay tests passed.");
}
