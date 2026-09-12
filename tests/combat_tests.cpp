#include "arena.h"
#include "combat.h"
#include "game.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

using namespace gc;

const InputFrame idle = {0, 0, false, false};
constexpr int HALF_PLAYER = PLAYER_SIZE * FIXED_ONE / 2;

int16_t normalized(int value, int extent) {
    return static_cast<int16_t>((value % extent + extent) % extent);
}

uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

unsigned liveProjectiles(const Combat& combat) {
    unsigned count = 0;
    for (unsigned i = 0; i < MAX_PROJECTILES; ++i) {
        count += combat.projectiles[i].framesLeft != 0;
    }
    return count;
}

void assertSameCombat(const Combat& actual, const Combat& expected) {
    // Сравниваем поля без байтов выравнивания структуры на ПК.
    assert(actual.shotCooldown == expected.shotCooldown);
    for (unsigned i = 0; i < DUMMY_COUNT; ++i) {
        const Dummy& a = actual.dummies[i];
        const Dummy& b = expected.dummies[i];
        assert(a.x == b.x && a.y == b.y && getHp(a) == getHp(b));
        assert(a.respawnFrames == b.respawnFrames);
        assert(getHitFlash(a) == getHitFlash(b));
    }
    for (unsigned i = 0; i < MAX_PROJECTILES; ++i) {
        const Projectile& a = actual.projectiles[i];
        const Projectile& b = expected.projectiles[i];
        assert(a.x == b.x && a.y == b.y);
        assert(a.velocityX == b.velocityX && a.velocityY == b.velocityY);
        assert(a.framesLeft == b.framesLeft);
    }
}

Combat targetAt(uint8_t x, uint8_t y) {
    Combat combat = {};
    // При нулевых HP и таймере возрождения остальные слоты остаются пустыми.
    combat.dummies[0] = {x, y, 0, 0};
    setHpAndFlash(combat.dummies[0], DUMMY_MAX_HP, 0);
    return combat;
}

void tickWithoutFire(Combat& combat) {
    combat.shotCooldown = SHOT_INTERVAL;
    updateCombat(combat, 60 * FIXED_ONE, 25 * FIXED_ONE);
}

// Независимая проверка пересечения на ПК: граница включает последнюю 1/16 пикселя.
// Берём больше копий за краями арены, чем игровой код, чтобы не повторять его.
bool referenceSegment(int x, int y, int dx, int dy, const Obstacle& box) {
    if (box.width == 0 || box.height == 0) {
        return false;
    }
    for (int copyY = -2; copyY <= 2; ++copyY) {
        for (int copyX = -2; copyX <= 2; ++copyX) {
            const double low[] = {
                static_cast<double>(box.x * FIXED_ONE + copyX * ARENA_WIDTH_FIXED),
                static_cast<double>(box.y * FIXED_ONE + copyY * ARENA_HEIGHT_FIXED)
            };
            const double high[] = {
                low[0] + box.width * FIXED_ONE - 1,
                low[1] + box.height * FIXED_ONE - 1
            };
            const double origin[] = {static_cast<double>(x), static_cast<double>(y)};
            const double delta[] = {static_cast<double>(dx), static_cast<double>(dy)};
            double enter = 0;
            double leave = 1;
            bool possible = true;
            for (unsigned axis = 0; axis < 2; ++axis) {
                if (delta[axis] == 0) {
                    if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                        possible = false;
                    }
                    continue;
                }
                const double a = (low[axis] - origin[axis]) / delta[axis];
                const double b = (high[axis] - origin[axis]) / delta[axis];
                const double near = a < b ? a : b;
                const double far = a > b ? a : b;
                if (near > enter) {
                    enter = near;
                }
                if (far < leave) {
                    leave = far;
                }
            }
            if (possible && enter <= leave) {
                return true;
            }
        }
    }
    return false;
}

bool referenceWall(int x, int y, int dx, int dy) {
    for (unsigned i = 0; i < OBSTACLE_COUNT; ++i) {
        if (referenceSegment(x, y, dx, dy, readObstacle(i))) {
            return true;
        }
    }
    return false;
}

void testGeometry() {
    const Obstacle boxes[] = {{20, 20, 7, 7}, {127, 55, 7, 7},
                              {0, 0, 1, 1}, {20, 20, 0, 7}, {20, 20, 7, 0}};
    for (const Obstacle& box : boxes) {
        const int left = box.x * FIXED_ONE;
        const int top = box.y * FIXED_ONE;
        const int right = left + box.width * FIXED_ONE - 1;
        const int bottom = top + box.height * FIXED_ONE - 1;
        const int rays[][4] = {
            {left, top, 0, 0}, {right, bottom, 0, 0},
            {left - 1, top, 0, 0}, {right + 1, bottom, 0, 0},
            {left - 32, top, 160, 0}, {left - 32, top - 1, 160, 0},
            {left, top - 32, 0, 160}, {left - 1, top - 32, 0, 160},
            {left - 16, top + 16, 32, -32}, // Точное касание угла.
            {left - 16, top + 15, 32, -32}, // Промах на 1/16 пикселя.
            {left - 8, top + 6, 16, -10},   // Оба конца снаружи.
            {left - 8, top + 6, 7, -10},    // Прямая пересекает, отрезок не достаёт.
            {left - 100, bottom + 1, 300, 0}
        };
        for (const auto& ray : rays) {
            const int x = normalized(ray[0], ARENA_WIDTH_FIXED);
            const int y = normalized(ray[1], ARENA_HEIGHT_FIXED);
            const bool expected = referenceSegment(x, y, ray[2], ray[3], box);
            assert(segmentHitsBox(x, y, ray[2], ray[3], box) == expected);
            const int endX = normalized(x + ray[2], ARENA_WIDTH_FIXED);
            const int endY = normalized(y + ray[3], ARENA_HEIGHT_FIXED);
            assert(segmentHitsBox(endX, endY, -ray[2], -ray[3], box) == expected);
        }
    }
    const Obstacle box = {20, 20, 7, 7};
    assert(segmentHitsBox(19 * 16, 21 * 16, 32, -32, box));
    assert(!segmentHitsBox(19 * 16, 21 * 16 - 1, 32, -32, box));
    assert(!segmentHitsBox(20 * 16, 27 * 16, 16, 0, box));
    assert(segmentHitsBox(20 * 16, 27 * 16 - 1, 16, 0, box));

    uint32_t random = 0x94ab276du;
    unsigned hits = 0;
    for (unsigned trial = 0; trial < 50000; ++trial) {
        const int x = nextRandom(random) % ARENA_WIDTH_FIXED;
        const int y = nextRandom(random) % ARENA_HEIGHT_FIXED;
        // Кратчайший путь через края не длиннее половины арены по каждой оси.
        const int dx = static_cast<int>(nextRandom(random) % (ARENA_WIDTH_FIXED + 1))
                       - ARENA_WIDTH_FIXED / 2;
        const int dy = static_cast<int>(nextRandom(random) % (ARENA_HEIGHT_FIXED + 1))
                       - ARENA_HEIGHT_FIXED / 2;
        const Obstacle randomBox = {
            static_cast<uint8_t>(nextRandom(random) % ARENA_WIDTH),
            static_cast<uint8_t>(nextRandom(random) % ARENA_HEIGHT),
            static_cast<uint8_t>(nextRandom(random) % 20),
            static_cast<uint8_t>(nextRandom(random) % 20)
        };
        const bool expected = referenceSegment(x, y, dx, dy, randomBox);
        const bool actual = segmentHitsBox(x, y, dx, dy, randomBox);
        if (actual != expected) {
            std::fprintf(stderr, "Geometry trial %u: (%d,%d)+(%d,%d), box %u,%u %ux%u\n",
                         trial, x, y, dx, dy, randomBox.x, randomBox.y,
                         randomBox.width, randomBox.height);
        }
        assert(actual == expected);
        assert(shotBlocked(x, y, dx, dy) == referenceWall(x, y, dx, dy));
        hits += expected;
    }
    assert(hits > 1000 && hits < 49000);
    std::puts("Geometry: 50000 deterministic rays plus degenerate/grazing cases");
}

void testDummyPacking() {
    for (uint8_t hp = 0; hp <= 3; ++hp) {
        for (uint8_t flash = 0; flash <= 7; ++flash) {
            Dummy dummy = {91, 43, 0, 83};
            setHpAndFlash(dummy, hp, flash);
            assert(getHp(dummy) == hp && getHitFlash(dummy) == flash);
            assert(dummy.x == 91 && dummy.y == 43 && dummy.respawnFrames == 83);
            const Dummy initial = dummy;
            // Каждый сеттер меняет только своё поле при всех допустимых значениях.
            for (uint8_t nextHp = 0; nextHp <= 3; ++nextHp) {
                dummy = initial;
                setHp(dummy, nextHp);
                assert(getHp(dummy) == nextHp && getHitFlash(dummy) == flash);
                assert(dummy.x == 91 && dummy.y == 43 && dummy.respawnFrames == 83);
            }
            for (uint8_t nextFlash = 0; nextFlash <= 7; ++nextFlash) {
                dummy = initial;
                setHitFlash(dummy, nextFlash);
                assert(getHp(dummy) == hp && getHitFlash(dummy) == nextFlash);
                assert(dummy.x == 91 && dummy.y == 43 && dummy.respawnFrames == 83);
            }
            // Общий сеттер должен полностью заменять оба старых значения.
            for (uint8_t nextHp = 0; nextHp <= 3; ++nextHp) {
                for (uint8_t nextFlash = 0; nextFlash <= 7; ++nextFlash) {
                    dummy = initial;
                    setHpAndFlash(dummy, nextHp, nextFlash);
                    assert(getHp(dummy) == nextHp && getHitFlash(dummy) == nextFlash);
                    assert(dummy.x == 91 && dummy.y == 43 && dummy.respawnFrames == 83);
                }
            }
        }
    }
}

void testResetAndTitle() {
    assert(DUMMY_COUNT == 3 && DUMMY_SIZE == 7 && DUMMY_MAX_HP == 3);
    assert(DUMMY_RESPAWN_FRAMES == 125 && HIT_FLASH_FRAMES == 6);
    assert(MAX_PROJECTILES == 4 && SHOT_INTERVAL == 25 && SHOT_DAMAGE == 1);
    assert(SHOT_RANGE_FIXED == 48 * 16 && PROJECTILE_SPEED == 32);
    assert(PROJECTILE_LIFETIME == 64);
    Combat fresh = {};
    resetCombat(fresh);
    const uint8_t positions[][2] = {{64, 7}, {88, 45}, {120, 3}};
    Combat expected = {};
    for (unsigned i = 0; i < DUMMY_COUNT; ++i) {
        expected.dummies[i] = {positions[i][0], positions[i][1], 0, 0};
        setHpAndFlash(expected.dummies[i], 3, 0);
        assert(!playerBlocked(positions[i][0] * 16, positions[i][1] * 16));
    }
    assertSameCombat(fresh, expected);

    Game title = {};
    title.combat.shotCooldown = 19;
    for (unsigned i = 0; i < DUMMY_COUNT; ++i) {
        title.combat.dummies[i] = {static_cast<uint8_t>(60 + i), 24, 0, 83};
        setHpAndFlash(title.combat.dummies[i], static_cast<uint8_t>(i), 5);
    }
    for (unsigned i = 0; i < MAX_PROJECTILES; ++i) {
        title.combat.projectiles[i] = {static_cast<int16_t>(960 + i), 400,
                                      -23, 23, static_cast<uint8_t>(32 + i)};
    }
    const Combat frozen = title.combat;
    for (unsigned frame = 0; frame < 300; ++frame) {
        const InputFrame movement = {1, -1, false, false};
        updateGame(title, movement);
        assert(title.state == GameState::Title);
        assertSameCombat(title.combat, frozen);
    }
    Combat dirty = frozen;
    resetCombat(dirty);
    assertSameCombat(dirty, fresh);
    resetCombat(dirty);
    assertSameCombat(dirty, fresh);
    for (unsigned buttons = 1; buttons <= 3; ++buttons) {
        Game game = title;
        const InputFrame start = {1, -1, (buttons & 1) != 0, (buttons & 2) != 0};
        updateGame(game, start);
        assert(game.state == GameState::Playing);
        assert(game.player.x == PLAYER_START_X * 16);
        assert(game.player.y == PLAYER_START_Y * 16);
        assert(game.player.dashFrames == 0);
        assertSameCombat(game.combat, fresh); // Кадр старта не стреляет и не меняет таймеры.
        updateGame(game, idle);
        assert(liveProjectiles(game.combat) == 1);
        assert(game.combat.shotCooldown == 25);
        game.combat = frozen;
        startGame(game);
        assertSameCombat(game.combat, fresh);
    }
}

void testTargetSelectionAndRange() {
    Combat combat = targetAt(80, 20);
    combat.dummies[1] = {60, 5, 0, 0};
    setHpAndFlash(combat.dummies[1], 3, 0);
    combat.dummies[2] = {60, 45, 0, 0};
    setHpAndFlash(combat.dummies[2], 3, 0);
    updateCombat(combat, 60 * 16, 20 * 16);
    assert(liveProjectiles(combat) == 1);
    assert(combat.projectiles[0].velocityX == 0);
    assert(combat.projectiles[0].velocityY == -32);

    combat = targetAt(78, 24);
    combat.dummies[1] = {72, 36, 0, 0};
    setHpAndFlash(combat.dummies[1], 3, 0);
    updateCombat(combat, 60 * 16, 24 * 16);
    assert(liveProjectiles(combat) == 1);
    // Диагональ (12, 12) короче 18, хотя сумма смещений равна 24.
    assert(combat.projectiles[0].velocityX > 0);
    assert(combat.projectiles[0].velocityX == combat.projectiles[0].velocityY);

    combat = targetAt(88, 45); // Ближе, но за нижним укрытием.
    combat.dummies[1] = {40, 25, 0, 0};
    setHpAndFlash(combat.dummies[1], 3, 0);
    combat.dummies[2] = {120, 25, 0, 0}; // Видна, но вне дальности.
    setHpAndFlash(combat.dummies[2], 3, 0);
    assert(referenceWall(70 * 16 + 56, 25 * 16 + 56, 18 * 16, 20 * 16));
    assert(!referenceWall(70 * 16 + 56, 25 * 16 + 56, -30 * 16, 0));
    updateCombat(combat, 70 * 16, 25 * 16);
    assert(liveProjectiles(combat) == 1);
    assert(combat.projectiles[0].velocityX == -32);
    assert(combat.projectiles[0].velocityY == 0);

    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        combat = targetAt(reverse ? 50 : 70, 20);
        combat.dummies[1] = {static_cast<uint8_t>(reverse ? 70 : 50), 20, 0, 0};
        setHpAndFlash(combat.dummies[1], 3, 0);
        updateCombat(combat, 60 * 16, 20 * 16);
        assert(liveProjectiles(combat) == 1);
        assert(combat.projectiles[0].velocityX == (reverse ? -32 : 32));
    }
    const int offsets[][3] = {
        {0, 0, 1}, {1, 0, 1}, {-1, 0, 0}, {-16, 0, 0},
        {0, 1, 0}, {0, -1, 0}, {1, 1, 1}, {1, -1, 1}
    };
    for (const auto& offset : offsets) {
        combat = targetAt(70, 3);
        updateCombat(combat, 22 * 16 + offset[0], 3 * 16 + offset[1]);
        assert(liveProjectiles(combat) == static_cast<unsigned>(offset[2]));
        assert(combat.shotCooldown == (offset[2] ? 25 : 0));
        assert(getHp(combat.dummies[0]) == 3);
    }
    // Дальность измеряется между центрами, а не до ближнего края цели.
    combat = targetAt(71, 3);
    updateCombat(combat, 22 * 16, 3 * 16);
    assert(liveProjectiles(combat) == 0 && combat.shotCooldown == 0);
}

void testSeamShots() {
    for (int sx = -1; sx <= 1; ++sx) {
        for (int sy = -1; sy <= 1; ++sy) {
            if (sx == 0 && sy == 0) {
                continue;
            }
            const int px = sx == 0 ? 60 : (sx > 0 ? 120 : 8);
            const int py = sy == 0 ? 45 : (sy > 0 ? 48 : 6);
            const int tx = sx == 0 ? px : (sx > 0 ? 8 : 120);
            const int ty = sy == 0 ? py : (sy > 0 ? 6 : 48);
            Combat combat = targetAt(tx, ty);
            updateCombat(combat, px * 16, py * 16);
            assert(liveProjectiles(combat) == 1);
            const Projectile shot = combat.projectiles[0];
            assert((sx == 0 && shot.velocityX == 0) || sx * shot.velocityX > 0);
            assert((sy == 0 && shot.velocityY == 0) || sy * shot.velocityY > 0);
            assert(shot.x == px * 16 + 56 && shot.y == py * 16 + 56);
            unsigned frames = 0;
            while (combat.projectiles[0].framesLeft != 0 && frames < 20) {
                tickWithoutFire(combat);
                ++frames;
            }
            assert(frames > 1 && frames < 20);
            assert(getHp(combat.dummies[0]) == 2);
            assert(combat.dummies[0].x == tx && combat.dummies[0].y == ty);
        }
    }
    // Центр тела 7x7 тоже переносится через край арены.
    Combat combat = targetAt(7, 5);
    updateCombat(combat, 127 * 16 + 7, 55 * 16 + 3);
    assert(liveProjectiles(combat) == 1);
    assert(combat.projectiles[0].x == 47 && combat.projectiles[0].y == 43);
    assert(combat.projectiles[0].velocityX > 0 && combat.projectiles[0].velocityY > 0);
    combat = targetAt(127, 55);
    updateCombat(combat, 7 * 16, 5 * 16);
    assert(liveProjectiles(combat) == 1);
    assert(combat.projectiles[0].velocityX < 0 && combat.projectiles[0].velocityY < 0);
}

void testCadenceAndUnavailableShots() {
    Game game = {};
    startGame(game);
    game.player.x = 60 * 16;
    game.player.y = 20 * 16;
    game.combat = targetAt(80, 20);
    unsigned shots = 0;
    for (unsigned frame = 0; frame < 101; ++frame) {
        setHp(game.combat.dummies[0], 3); // Сохраняем живую цель для проверки частоты выстрелов.
        updateGame(game, idle);
        const bool fired = frame % 25 == 0;
        assert(game.combat.shotCooldown == 25 - frame % 25);
        unsigned newborn = 0;
        for (const Projectile& shot : game.combat.projectiles) {
            newborn += shot.framesLeft == 64;
        }
        assert(newborn == static_cast<unsigned>(fired));
        shots += newborn;
        assert(game.player.x == 60 * 16 && game.player.y == 20 * 16);
        assert(game.player.dashFrames == 0);
        assert(game.player.slots[0].cooldown == 0 && game.player.slots[1].cooldown == 0);
    }
    assert(shots == 5);

    Combat combat = {};
    for (unsigned frame = 0; frame < 300; ++frame) {
        updateCombat(combat, 60 * 16, 20 * 16);
        assert(liveProjectiles(combat) == 0 && combat.shotCooldown == 0);
        for (const Dummy& dummy : combat.dummies) {
            assert(getHp(dummy) == 0 && dummy.respawnFrames == 0);
        }
    }
    combat.dummies[0] = {80, 20, 0, 0};
    setHpAndFlash(combat.dummies[0], 3, 0);
    updateCombat(combat, 60 * 16, 20 * 16);
    assert(liveProjectiles(combat) == 1 && combat.shotCooldown == 25);

    combat = targetAt(80, 20);
    for (unsigned i = 0; i < MAX_PROJECTILES; ++i) {
        combat.projectiles[i] = {static_cast<int16_t>((20 + i) * 16), 48 * 16, 32, 0, 64};
    }
    updateCombat(combat, 60 * 16, 20 * 16);
    assert(liveProjectiles(combat) == 4 && combat.shotCooldown == 0);
    for (const Projectile& shot : combat.projectiles) {
        assert(shot.framesLeft == 63);
    }
    combat.projectiles[2].framesLeft = 1;
    updateCombat(combat, 60 * 16, 20 * 16);
    assert(liveProjectiles(combat) == 4 && combat.shotCooldown == 25);
    assert(combat.projectiles[2].framesLeft == 64); // Слот переиспользуется в кадр исчезновения пули.
    assert(combat.projectiles[2].x == 60 * 16 + 56);
    assert(combat.projectiles[2].y == 20 * 16 + 56);
}

void testVelocityAndNoHoming() {
    unsigned headings = 0;
    for (int dx = -10; dx <= 10; ++dx) {
        for (int dy = -10; dy <= 10; ++dy) {
            if (dx * dx + dy * dy < 36) {
                continue;
            }
            Combat combat = targetAt(60 + dx, 24 + dy);
            updateCombat(combat, 60 * 16, 24 * 16);
            assert(liveProjectiles(combat) == 1);
            const Projectile shot = combat.projectiles[0];
            assert(shot.x == 60 * 16 + 56 && shot.y == 24 * 16 + 56);
            assert(shot.framesLeft == 64 && getHp(combat.dummies[0]) == 3);
            const double speed = std::sqrt(shot.velocityX * shot.velocityX +
                                           shot.velocityY * shot.velocityY);
            assert(std::fabs(speed - 32.0) < 1.0);
            const double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
            assert(std::fabs(shot.velocityX - 32.0 * dx / length) < 1.0);
            assert(std::fabs(shot.velocityY - 32.0 * dy / length) < 1.0);
            // Переносим цель после выстрела: пуля не должна поворачивать за ней.
            combat.dummies[0].x = 10;
            combat.dummies[0].y = 45;
            tickWithoutFire(combat);
            assert(combat.projectiles[0].x == shot.x + shot.velocityX);
            assert(combat.projectiles[0].y == shot.y + shot.velocityY);
            assert(combat.projectiles[0].framesLeft == 63);
            assert(combat.projectiles[0].velocityX == shot.velocityX);
            assert(combat.projectiles[0].velocityY == shot.velocityY);
            setHp(combat.dummies[0], 0);
            tickWithoutFire(combat);
            assert(combat.projectiles[0].x == shot.x + 2 * shot.velocityX);
            assert(combat.projectiles[0].y == shot.y + 2 * shot.velocityY);
            assert(combat.projectiles[0].framesLeft == 62);
            ++headings;
        }
    }
    assert(headings > 300);
}

void testSweptHitsAndWalls() {
    // Первый полушаг задевает угол, хотя оба конца отрезка снаружи.
    Combat combat = targetAt(64, 24);
    combat.projectiles[0] = {64 * 16 - 8, 24 * 16 + 6, 32, -20, 64};
    tickWithoutFire(combat);
    assert(combat.projectiles[0].framesLeft == 0);
    assert(getHp(combat.dummies[0]) == 2 && getHitFlash(combat.dummies[0]) == 6);
    tickWithoutFire(combat);
    assert(getHitFlash(combat.dummies[0]) == 5);
    combat.projectiles[0] = {66 * 16, 27 * 16, 32, 0, 64};
    tickWithoutFire(combat);
    assert(getHp(combat.dummies[0]) == 1 && getHitFlash(combat.dummies[0]) == 6);

    combat = targetAt(37, 8); // Цель внутри укрытия: проверяем приоритет стены.
    combat.projectiles[0] = {37 * 16 - 8, 8 * 16 + 6, 32, -20, 64};
    tickWithoutFire(combat);
    assert(combat.projectiles[0].framesLeft == 0);
    assert(getHp(combat.dummies[0]) == 3);

    combat = targetAt(98, 33);
    combat.projectiles[0] = {80 * 16, 36 * 16, 32, 0, 64};
    for (unsigned frame = 0; frame < 64; ++frame) {
        tickWithoutFire(combat);
        assert(getHp(combat.dummies[0]) == 3);
    }
    assert(liveProjectiles(combat) == 0);

    combat = targetAt(64, 24);
    combat.dummies[1] = combat.dummies[0];
    combat.projectiles[0] = {63 * 16, 27 * 16, 32, 0, 64};
    tickWithoutFire(combat);
    assert(getHp(combat.dummies[0]) + getHp(combat.dummies[1]) == 5);
    assert(liveProjectiles(combat) == 0); // Одна пуля не ранит две совпавшие цели.

    combat = targetAt(64, 24);
    setHp(combat.dummies[0], 1);
    for (Projectile& shot : combat.projectiles) {
        shot = {66 * 16, 27 * 16, 32, 0, 64};
    }
    tickWithoutFire(combat);
    assert(getHp(combat.dummies[0]) == 0);
    assert(combat.dummies[0].respawnFrames == 125);
    assert(liveProjectiles(combat) == 3); // Остальные пули пропускают уже убитую цель.

    combat = targetAt(64, 24);
    updateCombat(combat, 64 * 16, 24 * 16); // Совпавшие центры обрабатываются корректно.
    assert(liveProjectiles(combat) == 1 && getHp(combat.dummies[0]) == 3);
    assert(combat.projectiles[0].framesLeft == 64);
    tickWithoutFire(combat);
    assert(liveProjectiles(combat) == 0 && getHp(combat.dummies[0]) == 2);
}

void testDeathRespawnAndFlash() {
    Combat combat = targetAt(64, 24);
    for (unsigned hit = 1; hit <= 3; ++hit) {
        combat.projectiles[0] = {66 * 16, 27 * 16, 32, 0, 64};
        tickWithoutFire(combat);
        assert(getHp(combat.dummies[0]) == 3 - hit);
        assert(liveProjectiles(combat) == 0);
        if (hit < 3) {
            assert(getHitFlash(combat.dummies[0]) == 6);
            for (unsigned frame = 1; frame <= 7; ++frame) {
                tickWithoutFire(combat);
                assert(getHitFlash(combat.dummies[0]) == (frame < 6 ? 6 - frame : 0));
                assert(getHp(combat.dummies[0]) == 3 - hit);
                assert(combat.dummies[0].respawnFrames == 0);
            }
        }
    }
    assert(getHitFlash(combat.dummies[0]) == 0);
    assert(combat.dummies[0].respawnFrames == 125);
    for (unsigned frame = 1; frame <= 125; ++frame) {
        tickWithoutFire(combat);
        assert(combat.dummies[0].respawnFrames == 125 - frame);
        assert(getHp(combat.dummies[0]) == (frame == 125 ? 3 : 0));
        assert(combat.dummies[0].x == 64 && combat.dummies[0].y == 24);
        assert(getHitFlash(combat.dummies[0]) == 0);
    }
    combat.shotCooldown = 0;
    updateCombat(combat, 60 * 16, 24 * 16);
    assert(liveProjectiles(combat) == 1); // Возрождённая цель снова доступна для выстрела.

    Game game = {};
    startGame(game);
    game.player.x = 60 * 16;
    game.player.y = 24 * 16;
    game.combat = targetAt(64, 24);
    for (int frame = 1; frame <= 10; ++frame) {
        const InputFrame right = {1, 0, false, false};
        game.combat.shotCooldown = 25;
        updateGame(game, right);
        assert(game.player.x == (60 + frame) * 16); // Безвредные цели не мешают движению.
        assert(game.player.y == 24 * 16);
        assert(getHp(game.combat.dummies[0]) == 3);
        assert(game.combat.dummies[0].x == 64 && game.combat.dummies[0].y == 24);
    }
}

void testLifetimeAndReuse() {
    for (int direction = -1; direction <= 1; direction += 2) {
        Combat combat = {};
        combat.projectiles[0] = {17, 48 * 16 + 3, static_cast<int8_t>(direction * 32), 0, 64};
        for (int frame = 1; frame <= 64; ++frame) {
            tickWithoutFire(combat);
            assert(combat.projectiles[0].framesLeft == 64 - frame);
            assert(combat.projectiles[0].x == normalized(17 + direction * 32 * frame,
                                                        ARENA_WIDTH_FIXED));
            assert(combat.projectiles[0].y == 48 * 16 + 3);
        }
        const Combat expired = combat;
        for (unsigned frame = 0; frame < 70; ++frame) {
            tickWithoutFire(combat);
            assertSameCombat(combat, expired);
        }
        combat.dummies[0] = {80, 20, 0, 0};
        setHpAndFlash(combat.dummies[0], 3, 0);
        combat.shotCooldown = 0;
        updateCombat(combat, 60 * 16, 20 * 16);
        assert(liveProjectiles(combat) == 1 && combat.projectiles[0].framesLeft == 64);
        assert(combat.projectiles[0].x == 60 * 16 + 56);
        assert(combat.projectiles[0].y == 20 * 16 + 56);
        assert(combat.projectiles[0].velocityX == 32 && combat.projectiles[0].velocityY == 0);
    }
}

void testAimPreflight() {
    Combat combat = targetAt(88, 45);
    const Combat hidden = combat;
    for (unsigned frame = 0; frame < 150; ++frame) {
        updateCombat(combat, 88 * 16, 25 * 16);
        assertSameCombat(combat, hidden); // Не ищем длинный обход укрытия через край.
    }

    // Ищем случаи, где прямая к центру свободна, но округлённая скорость ведёт в стену.
    // Проверяем путь независимым расчётом, без игровых функций столкновений.
    unsigned rejected = 0;
    unsigned fallbacks = 0;
    for (int px = 54 * 16; px < 70 * 16; px += 3) {
        for (int py = 0; py < 20 * 16; py += 3) {
            const int tx = 30;
            const int ty = 20;
            const int x = px + HALF_PLAYER;
            const int y = py + HALF_PLAYER;
            const int dx = tx * 16 + 56 - x;
            const int dy = ty * 16 + 56 - y;
            if (referenceWall(x, y, dx, dy)) {
                continue;
            }
            const double length = std::ceil(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
            const int vx = static_cast<int>(std::round(32.0 * dx / length));
            const int vy = static_cast<int>(std::round(32.0 * dy / length));
            const Obstacle target = {tx, ty, 7, 7};
            int bx = x;
            int by = y;
            bool blocked = false;
            bool reached = false;
            for (unsigned step = 0; step < 52 && !blocked && !reached; ++step) {
                const int sx = step % 2 == 0 ? vx / 2 : vx - vx / 2;
                const int sy = step % 2 == 0 ? vy / 2 : vy - vy / 2;
                blocked = referenceWall(bx, by, sx, sy);
                reached = referenceSegment(bx, by, sx, sy, target);
                bx = normalized(bx + sx, ARENA_WIDTH_FIXED);
                by = normalized(by + sy, ARENA_HEIGHT_FIXED);
            }
            if (!blocked) {
                continue;
            }
            combat = targetAt(tx, ty);
            const Combat before = combat;
            updateCombat(combat, px, py);
            assertSameCombat(combat, before);
            // Недоступная ближняя цель не мешает выстрелу в дальнюю открытую.
            combat.dummies[1] = {103, static_cast<uint8_t>(py / 16), 0, 0};
            setHpAndFlash(combat.dummies[1], 3, 0);
            const int farDx = 103 * 16 + 56 - x;
            const int farDy = (py / 16) * 16 + 56 - y;
            if (farDx * farDx + farDy * farDy > dx * dx + dy * dy &&
                farDx * farDx + farDy * farDy <= 48 * 16 * 48 * 16) {
                updateCombat(combat, px, py);
                assert(liveProjectiles(combat) == 1 && combat.shotCooldown == 25);
                assert(combat.projectiles[0].velocityX > 0);
                assert(getHp(combat.dummies[0]) == 3 && getHp(combat.dummies[1]) == 3);
                ++fallbacks;
            }
            ++rejected;
        }
    }
    assert(rejected > 0); // Проверяем, что случаи ошибки округления действительно найдены.
    assert(fallbacks > 0);
    std::printf("Aim preflight: %u rounded wall paths rejected without mutation\n", rejected);

    combat = targetAt(80, 20);
    combat.dummies[1] = {60, 5, 0, 0};
    setHpAndFlash(combat.dummies[1], 3, 0);
    updateCombat(combat, 60 * 16, 20 * 16);
    assert(liveProjectiles(combat) == 1);
    for (unsigned i = 0; i < 2; ++i) {
        assert(getHp(combat.dummies[i]) == 3);
        assert(getHitFlash(combat.dummies[i]) == 0);
        assert(combat.dummies[i].respawnFrames == 0);
    }
    for (unsigned i = 1; i < MAX_PROJECTILES; ++i) {
        const Projectile& shot = combat.projectiles[i];
        assert(shot.framesLeft == 0 && shot.x == 0 && shot.y == 0);
        assert(shot.velocityX == 0 && shot.velocityY == 0);
    }
}

void testRandomizedCombat() {
    uint32_t random = 0x6d2b79f5u;
    unsigned births = 0;
    unsigned deaths = 0;
    unsigned respawns = 0;
    for (unsigned run = 0; run < 4; ++run) {
        struct GuardedGame {
            uint32_t before;
            Game game;
            uint32_t after;
        } guarded = {};
        guarded.before = 0x1234abcdu;
        guarded.after = 0xdcba4321u;
        Game& game = guarded.game;
        startGame(game);
        game.player.slots[0].ability = run & 1 ? AbilityId::Dash : AbilityId::None;
        game.player.slots[1].ability = run & 2 ? AbilityId::Dash : AbilityId::None;
        InputFrame input = idle;
        for (unsigned frame = 0; frame < 25000; ++frame) {
            const uint32_t value = nextRandom(random);
            if (frame % 23 == 0) {
                input.moveX = static_cast<int8_t>(static_cast<int>(value % 3) - 1);
                input.moveY = static_cast<int8_t>(static_cast<int>((value >> 8) % 3) - 1);
            }
            input.activateA = (value & 31) == 0;
            input.activateB = ((value >> 5) & 31) == 0;
            const Combat before = game.combat;
            updateGame(game, input);
            assert(guarded.before == 0x1234abcdu && guarded.after == 0xdcba4321u);
            assert(game.state == GameState::Playing);
            assert(game.player.x >= 0 && game.player.x < ARENA_WIDTH_FIXED);
            assert(game.player.y >= 0 && game.player.y < ARENA_HEIGHT_FIXED);
            assert(!playerBlocked(game.player.x, game.player.y));
            assert(game.combat.shotCooldown <= 25);
            assert(liveProjectiles(game.combat) <= 4);
            unsigned newborn = 0;
            for (unsigned i = 0; i < MAX_PROJECTILES; ++i) {
                const Projectile& shot = game.combat.projectiles[i];
                const Projectile& old = before.projectiles[i];
                assert(shot.framesLeft <= 64);
                assert(shot.x >= 0 && shot.x < ARENA_WIDTH_FIXED);
                assert(shot.y >= 0 && shot.y < ARENA_HEIGHT_FIXED);
                assert(shot.velocityX >= -32 && shot.velocityX <= 32);
                assert(shot.velocityY >= -32 && shot.velocityY <= 32);
                if (shot.framesLeft == 64) {
                    ++newborn;
                    assert(game.combat.shotCooldown == 25);
                    assert(shot.x == normalized(game.player.x + 56, ARENA_WIDTH_FIXED));
                    assert(shot.y == normalized(game.player.y + 56, ARENA_HEIGHT_FIXED));
                } else if (shot.framesLeft > 0) {
                    assert(old.framesLeft == shot.framesLeft + 1);
                    assert(shot.velocityX == old.velocityX && shot.velocityY == old.velocityY);
                    assert(shot.x == normalized(old.x + old.velocityX, ARENA_WIDTH_FIXED));
                    assert(shot.y == normalized(old.y + old.velocityY, ARENA_HEIGHT_FIXED));
                }
            }
            assert(newborn <= 1);
            births += newborn;
            for (unsigned i = 0; i < DUMMY_COUNT; ++i) {
                const Dummy& dummy = game.combat.dummies[i];
                const Dummy& old = before.dummies[i];
                assert(dummy.x == old.x && dummy.y == old.y);
                assert(dummy.x < ARENA_WIDTH && dummy.y < ARENA_HEIGHT);
                assert(getHp(dummy) <= 3 && dummy.respawnFrames <= 125);
                assert(getHitFlash(dummy) <= 6);
                assert(getHp(dummy) == 0 || dummy.respawnFrames == 0);
                if (getHp(dummy) == 0) {
                    assert(getHitFlash(dummy) == 0);
                }
                if (getHp(old) > 0 && getHp(dummy) == 0) {
                    ++deaths;
                    assert(dummy.respawnFrames == 125);
                }
                if (getHp(old) == 0 && old.respawnFrames > 1) {
                    assert(getHp(dummy) == 0 && dummy.respawnFrames == old.respawnFrames - 1);
                }
                if (getHp(old) == 0 && getHp(dummy) > 0) {
                    ++respawns;
                    assert(old.respawnFrames == 1);
                }
            }
        }
    }
    assert(births > 100 && deaths > 10 && respawns > 10);
    std::printf("Randomized combat: 100000 frames, %u shots, %u deaths, %u respawns\n",
                births, deaths, respawns);
}

} // Конец анонимного пространства имён.

int main() {
    testDummyPacking();
    testGeometry();
    testResetAndTitle();
    testTargetSelectionAndRange();
    testSeamShots();
    testCadenceAndUnavailableShots();
    testVelocityAndNoHoming();
    testSweptHitsAndWalls();
    testDeathRespawnAndFlash();
    testLifetimeAndReuse();
    testAimPreflight();
    testRandomizedCombat();
    std::puts("All combat tests passed.");
}
