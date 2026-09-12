#include "arena.h"
#include "game.h"

#include <cassert>
#include <cstdio>

namespace {

using namespace gc;

const InputFrame idle = {0, 0, false, false};

int16_t normalized(int value, int extent) {
    return static_cast<int16_t>((value % extent + extent) % extent);
}

Game playingAt(int16_t x, int16_t y) {
    Game game = {};
    startGame(game);
    game.player.x = x;
    game.player.y = y;
    assert(!playerBlocked(x, y));
    return game;
}

void assertSafe(const Game& game) {
    assert(game.player.x >= 0 && game.player.x < ARENA_WIDTH_FIXED);
    assert(game.player.y >= 0 && game.player.y < ARENA_HEIGHT_FIXED);
    assert(!playerBlocked(game.player.x, game.player.y));
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

void testTitleAndReset() {
    Game game = {};
    assert(game.state == GameState::Title);
    game.player.x = 123;
    game.player.y = 456;
    setFacing(game.player, -1, 1);
    game.player.dashFrames = 3;
    game.player.slots[0] = {AbilityId::None, 17};
    game.player.slots[1] = {AbilityId::Dash, 29};
    const Game title = game;
    for (unsigned frame = 0; frame < 300; ++frame) {
        const InputFrame movement = {
            static_cast<int8_t>(static_cast<int>(frame % 3) - 1),
            static_cast<int8_t>(static_cast<int>((frame / 3) % 3) - 1),
            false, false
        };
        updateGame(game, movement);
        assert(game.state == GameState::Title);
        assertSamePlayer(game.player, title.player);
    }
    for (unsigned buttons = 1; buttons <= 3; ++buttons) {
        game = title;
        const InputFrame start = {1, -1, (buttons & 1) != 0, (buttons & 2) != 0};
        updateGame(game, start);
        assertFreshRun(game); // Кадр старта не запускает движение или Dash.
        updateGame(game, idle);
        assertFreshRun(game);
    }
    game = title;
    game.state = GameState::Playing;
    startGame(game);
    assertFreshRun(game);
    startGame(game);
    assertFreshRun(game);
}

void testWalking() {
    for (int8_t dx = -1; dx <= 1; ++dx) {
        for (int8_t dy = -1; dy <= 1; ++dy) {
            Game game = playingAt(60 * FIXED_ONE + 3, 25 * FIXED_ONE + 7);
            const Player initial = game.player;
            const InputFrame input = {dx, dy, false, false};
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
                    const InputFrame input = {dx, dy, dash != 0, false};
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

// Независимая проверка: сдвигаем препятствия, а не координаты игрока.
bool referenceBlocked(int x, int y) {
    const int size = PLAYER_SIZE * FIXED_ONE;
    for (unsigned i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        for (int copyX = -1; copyX <= 1; ++copyX) {
            for (int copyY = -1; copyY <= 1; ++copyY) {
                const int left = obstacle.x * FIXED_ONE + copyX * ARENA_WIDTH_FIXED;
                const int top = obstacle.y * FIXED_ONE + copyY * ARENA_HEIGHT_FIXED;
                if (x < left + obstacle.width * FIXED_ONE && x + size > left &&
                    y < top + obstacle.height * FIXED_ONE && y + size > top) {
                    return true;
                }
            }
        }
    }
    return false;
}

void testObstacleAabb() {
    assert(OBSTACLE_COUNT == 4);
    const Obstacle seamObstacle = readObstacle(0);
    assert(seamObstacle.x == 3 && seamObstacle.y == 14);
    const Obstacle invalid = readObstacle(OBSTACLE_COUNT);
    assert(invalid.x == 0 && invalid.y == 0 && invalid.width == 0 && invalid.height == 0);
    for (unsigned i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        const int left = (static_cast<int>(obstacle.x) - PLAYER_SIZE) * FIXED_ONE;
        const int right = (obstacle.x + obstacle.width) * FIXED_ONE;
        const int top = (static_cast<int>(obstacle.y) - PLAYER_SIZE) * FIXED_ONE;
        const int bottom = (obstacle.y + obstacle.height) * FIXED_ONE;
        const int x = obstacle.x * FIXED_ONE;
        const int y = obstacle.y * FIXED_ONE;
        assert(playerBlocked(x, y));
        assert(!playerBlocked(normalized(left, ARENA_WIDTH_FIXED), y));
        assert(playerBlocked(normalized(left + 1, ARENA_WIDTH_FIXED), y));
        assert(!playerBlocked(right, y));
        assert(playerBlocked(right - 1, y));
        assert(!playerBlocked(x, top));
        assert(playerBlocked(x, top + 1));
        assert(!playerBlocked(x, bottom));
        assert(playerBlocked(x, bottom - 1));
        assert(!playerBlocked(normalized(left, ARENA_WIDTH_FIXED), top));
        assert(playerBlocked(normalized(left + 1, ARENA_WIDTH_FIXED), top + 1));
    }
    // При x=124 игрок через край лишь касается препятствия с x=3.
    assert(!playerBlocked(124 * FIXED_ONE, 14 * FIXED_ONE));
    assert(playerBlocked(124 * FIXED_ONE + 1, 14 * FIXED_ONE));
    assert(playerBlocked(ARENA_WIDTH_FIXED - 1, 14 * FIXED_ONE));
    assert(playerBlocked(0, 14 * FIXED_ONE));
    for (int y = 0; y < ARENA_HEIGHT_FIXED; y += FIXED_ONE) {
        for (int x = 0; x < ARENA_WIDTH_FIXED; x += FIXED_ONE) {
            const int fractions[] = {0, 1, FIXED_ONE - 1};
            for (unsigned fx = 0; fx < 3; ++fx) {
                for (unsigned fy = 0; fy < 3; ++fy) {
                    assert(playerBlocked(x + fractions[fx], y + fractions[fy]) ==
                           referenceBlocked(x + fractions[fx], y + fractions[fy]));
                }
            }
        }
    }
}

void testSlidingAndDashCollision() {
    // Подходим с четырёх сторон, включая переход через край к x=3.
    for (unsigned i = 0; i < OBSTACLE_COUNT; ++i) {
        const Obstacle obstacle = readObstacle(i);
        for (unsigned side = 0; side < 4; ++side) {
            const bool horizontal = side < 2;
            const int8_t direction = side % 2 == 0 ? 1 : -1;
            const int boundary = horizontal ?
                (direction > 0 ? static_cast<int>(obstacle.x) - PLAYER_SIZE :
                                 obstacle.x + obstacle.width) * FIXED_ONE :
                (direction > 0 ? static_cast<int>(obstacle.y) - PLAYER_SIZE :
                                 obstacle.y + obstacle.height) * FIXED_ONE;
            const int extent = horizontal ? ARENA_WIDTH_FIXED : ARENA_HEIGHT_FIXED;
            const int start = normalized(boundary - direction * (FIXED_ONE + 3), extent);
            const int16_t x = horizontal ? start : obstacle.x * FIXED_ONE;
            const int16_t y = horizontal ? obstacle.y * FIXED_ONE : start;
            Game game = playingAt(x, y);
            const InputFrame dash = {
                static_cast<int8_t>(horizontal ? direction : 0),
                static_cast<int8_t>(horizontal ? 0 : direction), true, false
            };
            updateGame(game, dash);
            // Первый подшаг свободен, следующий зайдёт в стену на 13/16 пикселя.
            const int stopped = normalized(boundary - direction * 3, extent);
            assert(game.player.x == (horizontal ? stopped : x));
            assert(game.player.y == (horizontal ? y : stopped));
            assert(game.player.dashFrames == 0);
            assert(game.player.slots[0].cooldown == DASH_COOLDOWN);
            assertSafe(game);
            const Player hit = game.player;
            updateGame(game, idle);
            assert(game.player.x == hit.x && game.player.y == hit.y);

            // Ходьба скользит вдоль стены, а Dash останавливается по обеим осям.
            const int16_t faceX = horizontal ? normalized(boundary, extent) : x;
            const int16_t faceY = horizontal ? y : normalized(boundary, extent);
            const InputFrame diagonal = {
                static_cast<int8_t>(horizontal ? direction : 1),
                static_cast<int8_t>(horizontal ? 1 : direction), false, false
            };
            game = playingAt(faceX, faceY);
            updateGame(game, diagonal);
            assert(game.player.x == faceX + (horizontal ? 0 : 11));
            assert(game.player.y == faceY + (horizontal ? 11 : 0));
            assertSafe(game);
            game = playingAt(faceX, faceY);
            InputFrame diagonalDash = diagonal;
            diagonalDash.activateA = true;
            updateGame(game, diagonalDash);
            assert(game.player.x == faceX && game.player.y == faceY);
            assert(game.player.dashFrames == 0);
            assertSafe(game);

            // Повторные шаги и попытки Dash не должны проходить сквозь стену.
            InputFrame intoWall = dash;
            intoWall.activateA = false;
            for (unsigned frame = 0; frame < 2 * DASH_COOLDOWN; ++frame) {
                intoWall.activateA = frame % 7 == 0;
                updateGame(game, intoWall);
                assert(game.player.x == faceX && game.player.y == faceY);
                assertSafe(game);
            }
        }
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
                    static_cast<int8_t>(tick == 1 ? dy : -dy), tick == 1, false
                };
                updateGame(game, input);
                assert(game.player.x == initial.x + tick * dx * speed);
                assert(game.player.y == initial.y + tick * dy * speed);
                assert(getFacingX(game.player) == dx && getFacingY(game.player) == dy);
                assert(game.player.dashFrames == 6 - tick);
                assert(game.player.slots[0].cooldown == DASH_COOLDOWN - tick + 1);
                assertSafe(game);
            }
            const int distanceSquared = (dx * dx + dy * dy) * speed * speed;
            const int error = DASH_SPEED * DASH_SPEED - distanceSquared;
            assert(error >= 0 && error <= 2 * DASH_SPEED);
            const Player end = game.player;
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            const InputFrame reverse = {
                static_cast<int8_t>(-dx), static_cast<int8_t>(-dy), false, false
            };
            updateGame(game, reverse);
            const int walkSpeed = dx != 0 && dy != 0 ? 11 : 16;
            assert(game.player.x == end.x - dx * walkSpeed);
            assert(game.player.y == end.y - dy * walkSpeed);
            assert(getFacingX(game.player) == -dx && getFacingY(game.player) == -dy);
        }
    }
    Game game = playingAt(60 * FIXED_ONE, 45 * FIXED_ONE);
    const InputFrame faceLeft = {-1, 0, false, false};
    updateGame(game, faceLeft);
    const int16_t x = game.player.x;
    const InputFrame stationaryDash = {0, 0, true, false};
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
        const InputFrame press = {1, 0, slot == 0, slot == 1};
        const InputFrame pressIdle = {0, 0, slot == 0, slot == 1};
        updateGame(game, press);
        assert(game.player.x == 60 * FIXED_ONE + DASH_SPEED);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN);
        assert(game.player.slots[other].cooldown == 18);
        for (int tick = 2; tick <= 6; ++tick) {
            updateGame(game, idle);
        }
        const Player end = game.player;
        // Нажимаем во время перезарядки, затем держим без новых нажатий.
        updateGame(game, pressIdle);
        assert(game.player.dashFrames == 0);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 6);
        for (unsigned frame = 0; frame < 3 * DASH_COOLDOWN; ++frame) {
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            assert(game.player.dashFrames == 0);
            const int cooldown = DASH_COOLDOWN - 7 - static_cast<int>(frame);
            assert(game.player.slots[slot].cooldown == (cooldown > 0 ? cooldown : 0));
            assertSafe(game);
        }
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
        const InputFrame otherPress = {0, 0, other == 0, other == 1};
        updateGame(game, otherPress);
        assert(game.player.dashFrames == 4);
        assert(game.player.slots[other].cooldown == 0);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 1);
        for (unsigned tick = 0; tick < 4; ++tick) {
            updateGame(game, idle);
        }
        const int16_t beforeOther = game.player.x;
        updateGame(game, otherPress);
        assert(game.player.x == beforeOther + DASH_SPEED);
        assert(game.player.dashFrames == 5);
        assert(game.player.slots[other].cooldown == DASH_COOLDOWN);
        assert(game.player.slots[slot].cooldown == DASH_COOLDOWN - 6);

        for (unsigned tick = 0; tick < 5; ++tick) {
            updateGame(game, idle);
        }
        game.player.slots[0].ability = AbilityId::None;
        game.player.slots[1].ability = AbilityId::None;
        const Player unequipped = game.player;
        const InputFrame both = {0, 0, true, true};
        for (unsigned frame = 0; frame < 2 * DASH_COOLDOWN; ++frame) {
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
        const InputFrame input = {0, 0, (buttons & 1) != 0, (buttons & 2) != 0};
        const Player before = game.player;
        updateGame(game, input);
        assertSamePlayer(game.player, before);
    }
    const InputFrame walkBoth = {1, 0, true, true};
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
        for (unsigned frame = 0; frame < 2 * DASH_COOLDOWN; ++frame) {
            updateGame(game, idle);
            assert(game.player.x == end.x && game.player.y == end.y);
            assert(game.player.dashFrames == 0);
        }
        assert(game.player.slots[0].cooldown == 0 && game.player.slots[1].cooldown == 0);
    }
}

void testMapConnectivity() {
    // На ПК обходим позиции игрока по пикселям с учётом переходов через края.
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
            if (!visited[next] && !referenceBlocked(nx * FIXED_ONE, ny * FIXED_ONE)) {
                assert(count < cells);
                visited[next] = true;
                queue[count++] = static_cast<uint16_t>(next);
            }
        }
    }
    unsigned freeCells = 0;
    for (unsigned cell = 0; cell < cells; ++cell) {
        const bool free = !referenceBlocked((cell % ARENA_WIDTH) * FIXED_ONE,
                                           (cell / ARENA_WIDTH) * FIXED_ONE);
        assert(visited[cell] == free);
        freeCells += free;
    }
    assert(count == freeCells);
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
            assert(game.state == GameState::Playing);
            assertSafe(game);
            assert(!referenceBlocked(game.player.x, game.player.y));
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

} // Конец анонимного пространства имён.

int main() {
    testFacingPacking();
    testTitleAndReset();
    testWalking();
    testWraps();
    testObstacleAabb();
    testSlidingAndDashCollision();
    testDashMotion();
    testSlotsAndInputEdges();
    testMapConnectivity();
    testRandomizedInput();
    std::puts("All gameplay tests passed.");
}
