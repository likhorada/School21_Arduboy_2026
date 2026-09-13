#include "arena.h"
#include "game.h"
#include "stages.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace gc;
const InputFrame idle = {0, 0, false, false, false, false};
const InputFrame pressA = {0, 0, true, false, false, false};
const InputFrame pressB = {0, 0, false, true, false, false};
constexpr int16_t HALF_PLAYER = PLAYER_SIZE * FIXED_ONE / 2;

unsigned alive(const Combat& combat) {
    unsigned count = 0;
    for (const Enemy& enemy : combat.enemies) count += getEnemyType(enemy) != EnemyType::None;
    return count;
}

Game fixture() {
    Game game = {};
    startGame(game);
    game.player.x = 48 * FIXED_ONE;
    game.player.y = 48 * FIXED_ONE;
    game.combat.spawnTimer = 0;
    game.combat.waveCompleted = true;
    return game;
}

void selectChoice(Game& game, ShopCategory category, uint8_t choice) {
    game.shop.category = category;
    for (unsigned seed = 0; seed < 256; ++seed) {
        game.shop.seed = static_cast<uint8_t>(seed);
        for (uint8_t index = 0; index < 3; ++index) {
            if (shopChoice(game, category, index) == choice) {
                game.shop.selectedIndex = index;
                return;
            }
        }
    }
    assert(false);
}

unsigned projectiles(const Combat& combat) {
    unsigned count = 0;
    for (const Projectile& projectile : combat.projectiles)
        count += projectile.framesLeft != 0;
    return count;
}

void assertSeparated(const Game& game) {
    assert(!checkPlayerEnemyCollisions(game.combat, game.player.x, game.player.y));
    for (const Enemy& e : game.combat.enemies) {
        if (getEnemyType(e) == EnemyType::None) continue;
        assert(e.x >= 0 && e.x < ARENA_WIDTH_FIXED);
        assert(e.y >= 0 && e.y < ARENA_HEIGHT_FIXED);
        assert(enemyPositionValid(game.combat.currentStage, e.x, e.y));
    }
}

void parkAwayFromMarkers(Game& game) {
    for (uint8_t y = 0; y < ARENA_HEIGHT; y += 8) {
        for (uint8_t x = 0; x < ARENA_WIDTH; x += 8) {
            if (playerBlocked(game.combat.currentStage, x * 16, y * 16)) continue;
            bool safe = true;
            for (uint8_t i = 0; i < getWaveEnemyCount(game.combat.currentStage, game.combat.currentWave); ++i) {
                uint8_t mx, my;
                getSpawnPixel(game.combat.currentStage, game.combat.currentWave, i, mx, my);
                safe &= !enemyOverlapsPlayer(mx * 16, my * 16, x * 16 + HALF_PLAYER, y * 16 + HALF_PLAYER, true);
            }
            if (safe) {
                game.player.x = x * 16;
                game.player.y = y * 16;
                return;
            }
        }
    }
    assert(false);
}

void testTimerAndWaves() {
    static_assert(STAGE_TIME_FRAMES == 3750, "Identical AVR/host timer");
    assert(remainingSeconds(3750) == 60);
    assert(remainingSeconds(3749) == 59);
    assert(remainingSeconds(63) == 1 && remainingSeconds(62) == 0);
    Game game = {};
    updateGame(game, pressA);
    assert(game.state == GameState::Menu);
    updateGame(game, pressA);
    assert(game.player.hp == 4 && game.player.maxHp == 4);
    assert(game.player.slots[0].ability == AbilityId::Dash);
    assert(game.player.slots[1].ability == AbilityId::None);
    parkAwayFromMarkers(game);
    for (unsigned frame = 1; frame < SPAWN_DELAY_FRAMES; ++frame) {
        updateGame(game, idle);
        assert(game.combat.currentWave == 0 && alive(game.combat) == 0);
        assert(game.combat.playerScore == 0);
    }
    updateGame(game, idle);
    assert(alive(game.combat) == getWaveEnemyCount(0, 0));
    assert(game.combat.currentWave == 0);

    game = fixture();
    for (unsigned i = 0; i < STAGE_TIME_FRAMES + 100; ++i) updateGame(game, idle);
    assert(game.combat.stageTimer == 0 && game.state == GameState::Playing);
    // Timeout cannot clear even the final wave while an enemy remains alive.
    game.combat.waveCompleted = false;
    game.combat.currentWave = getStageWaveCount(0) - 1;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 80 * 16, 55 * 16, 3,
               game.combat.currentStage);
    checkWaveCompletion(game.combat);
    assert(!game.combat.stageCleared);
    damageEnemy(game.combat, 0, 31);
    checkWaveCompletion(game.combat);
    assert(game.combat.stageCleared && game.combat.playerScore == WAVE_CLEAR_BONUS + 5);
    const uint16_t score = game.combat.playerScore;
    for (unsigned i = 0; i < 100; ++i) checkWaveCompletion(game.combat);
    assert(game.combat.playerScore == score);

    game = fixture();
    game.combat.waveCompleted = false;
    game.player.hp = 1;
    game.combat.currentWave = getStageWaveCount(0) - 1;
    game.combat.stageTimer = 126; // 2.016 seconds, award two whole seconds.
    checkWaveCompletion(game.combat);
    assert(game.combat.playerScore == WAVE_CLEAR_BONUS + 200);
    updateGame(game, idle);
    assert(game.state == GameState::StageCleared);
    // Пауза между стейджами полностью восстанавливает HP.
    assert(game.player.hp == game.player.maxHp);
    const Game frozen = game;
    for (unsigned i = 0; i < 20; ++i) updateGame(game, idle);
    assert(std::memcmp(&game, &frozen, sizeof(Game)) == 0);
    updateGame(game, pressA);
    assert(game.state == GameState::Shop);
    updateGame(game, pressB);
    assert(game.state == GameState::Shop && game.shop.category == ShopCategory::Active);
    updateGame(game, pressB);
    assert(game.state == GameState::Playing && game.combat.currentStage == 1);
    assert(game.combat.currentWave == 0 && game.combat.stageTimer == 3750);
    parkAwayFromMarkers(game);
    for (unsigned i = 0; i < SPAWN_DELAY_FRAMES; ++i) updateGame(game, idle);
    assert(alive(game.combat) == getWaveEnemyCount(1, 0));

    // Exercise every shipped wave including splitter descendants, then final Win.
    game = {};
    startGame(game);
    for (uint8_t stage = 0; stage < TOTAL_STAGES; ++stage) {
        for (uint8_t wave = 0; wave < getStageWaveCount(stage); ++wave) {
            assert(game.combat.currentWave == wave);
            parkAwayFromMarkers(game);
            for (unsigned i = 0; i < SPAWN_DELAY_FRAMES; ++i) updateGame(game, idle);
            assert(alive(game.combat) == getWaveEnemyCount(stage, wave));
            for (unsigned pass = 0; alive(game.combat) && pass < 40; ++pass)
                for (uint8_t i = 0; i < MAX_ENEMIES; ++i) damageEnemy(game.combat, i, 31);
            assert(alive(game.combat) == 0);
            updateGame(game, idle);
        }
        if (stage == BOSS_STAGE) {
            assert(getStageWaveCount(stage) == 0);
            for (unsigned i = 0; i < SPAWN_DELAY_FRAMES; ++i)
                updateGame(game, idle);
            assert(game.combat.boss.hpFifths == BOSS_MAX_HP * 5);
            for (uint8_t i = 0; i < BOSS_MAX_HP; ++i)
                damageBossFifths(game.combat, 5);
            updateGame(game, idle);
        }
        if (stage + 1 < TOTAL_STAGES) {
            assert(game.state == GameState::StageCleared);
            updateGame(game, pressA);
            updateGame(game, pressB);
            // Карусель прототипа: после выбора пассивки B переключает категорию,
            // второй B завершает магазин. Новый стейдж всегда ставит игрока в
            // стартовый центр поля.
            updateGame(game, pressB);
            assert(game.combat.currentStage == stage + 1);
            assert(game.player.x == PLAYER_START_X * FIXED_ONE);
            assert(game.player.y == PLAYER_START_Y * FIXED_ONE);
            assert(!playerBlocked(game.combat.currentStage, game.player.x, game.player.y));
        }
    }
    assert(game.state == GameState::Win);
    const Game won = game;
    updateGame(game, idle);
    assert(std::memcmp(&game, &won, sizeof(Game)) == 0);
    updateGame(game, pressA);
    assert(game.state == GameState::Menu);
    updateGame(game, pressA);
    assert(game.state == GameState::Playing);
    assert(game.combat.currentStage == 0 && game.combat.playerScore == 0);
}

void testShop() {
    Game game = fixture();
    initShop(game);
    assert(game.shop.category == ShopCategory::Passive && game.shop.selectedIndex == 0);
    updateGame(game, pressA);
    assert(game.shop.category == ShopCategory::Passive && game.combat.playerScore == 0);

    const PassiveId passiveIds[] = {
        PassiveId::CompilerOptimization, PassiveId::Overclock,
        PassiveId::OptimizedBuild, PassiveId::MemoryFragmentation,
        PassiveId::CollectionRange, PassiveId::RamCapacity
    };
    const uint8_t caps[] = {2, 3, 2, 3, 2, 2};
    for (uint8_t i = 0; i < 6; ++i) {
        assert(passiveCap(passiveIds[i]) == caps[i]);
        Game priced = fixture();
        const uint16_t base = passiveIds[i] == PassiveId::RamCapacity
                                  ? RAM_CAPACITY_PRICE : PASSIVE_PRICE;
        for (uint8_t level = 0; level <= caps[i]; ++level) {
            setPassiveLevel(priced.passives, passiveIds[i], level);
            assert(passivePrice(priced, passiveIds[i]) ==
                   base + static_cast<uint32_t>(base) * level / 2);
        }
        initShop(priced);
        selectChoice(priced, ShopCategory::Passive,
                     static_cast<uint8_t>(passiveIds[i]));
        priced.combat.playerScore = 65535;
        applyShopChoice(priced);
        assert(passiveLevel(priced.passives, passiveIds[i]) == caps[i]);
        assert(priced.combat.playerScore == 65535);
    }
    assert(passiveCap(PassiveId::None) == 0);
    for (uint8_t id = static_cast<uint8_t>(AbilityId::TimeWarp);
         id <= static_cast<uint8_t>(AbilityId::MemoryDump); ++id) {
        const AbilityId ability = static_cast<AbilityId>(id);
        const uint16_t expected = ability == AbilityId::TimeWarp ||
                                          ability == AbilityId::RecursiveCall
                                      ? EXPENSIVE_ACTIVE_PRICE : ACTIVE_PRICE;
        assert(activePrice(ability) == expected);
        assert(abilityCooldown(ability) > 0);
    }

    // Every seed produces three deterministic, in-range, unique cards.
    for (unsigned seed = 0; seed < 256; ++seed) {
        game.shop.seed = static_cast<uint8_t>(seed);
        for (uint8_t category = 0; category < 2; ++category) {
            const ShopCategory kind = static_cast<ShopCategory>(category);
            const uint8_t a = shopChoice(game, kind, 0);
            const uint8_t b = shopChoice(game, kind, 1);
            const uint8_t c = shopChoice(game, kind, 2);
            assert(a != b && a != c && b != c);
            assert(a >= (category ? 2 : 1) && a <= (category ? 8 : 6));
            assert(a == shopChoice(game, kind, 0));
        }
    }

    // Purchases do not advance the category and the next level costs 50% more.
    game = fixture();
    initShop(game);
    selectChoice(game, ShopCategory::Passive,
                 static_cast<uint8_t>(PassiveId::CompilerOptimization));
    game.combat.playerScore = 2000;
    updateGame(game, pressA);
    assert(passiveLevel(game.passives, PassiveId::CompilerOptimization) == 1);
    assert(game.combat.playerScore == 1500 && game.shop.category == ShopCategory::Passive);
    updateGame(game, pressA);
    assert(passiveLevel(game.passives, PassiveId::CompilerOptimization) == 2);
    assert(game.combat.playerScore == 750 && game.shop.category == ShopCategory::Passive);
    updateGame(game, pressA);
    assert(passiveLevel(game.passives, PassiveId::CompilerOptimization) == 2);
    assert(game.combat.playerScore == 750);

    game = fixture();
    initShop(game);
    selectChoice(game, ShopCategory::Passive,
                 static_cast<uint8_t>(PassiveId::RamCapacity));
    game.combat.playerScore = 2500;
    updateGame(game, pressA);
    updateGame(game, pressA);
    assert(passiveLevel(game.passives, PassiveId::RamCapacity) == 2);
    assert(game.player.hp == 6 && game.player.maxHp == 6);
    assert(game.combat.playerScore == 0);

    // The same active offer may be bought into both slots.
    game = fixture();
    initShop(game);
    selectChoice(game, ShopCategory::Active,
                 static_cast<uint8_t>(AbilityId::TimeWarp));
    game.combat.playerScore = 4000;
    updateGame(game, pressA);
    assert(game.shop.choosingSlot);
    updateGame(game, {-1, 0, false, false, false, false});
    assert(!game.shop.choosingSlot && game.combat.playerScore == 4000);
    updateGame(game, pressA);
    updateGame(game, pressA);
    updateGame(game, pressA);
    updateGame(game, pressB);
    assert(game.player.slots[0].ability == AbilityId::TimeWarp);
    assert(game.player.slots[1].ability == AbilityId::TimeWarp);
    assert(game.combat.playerScore == 1000);
    assert(game.state == GameState::Shop && game.shop.category == ShopCategory::Active);
    updateGame(game, pressB);
    assert(game.state == GameState::Playing);

    initShop(game);
    const InputFrame right = {1, 0, false, false, false, false};
    const InputFrame left = {-1, 0, false, false, false, false};
    for (unsigned i = 0; i < 10; ++i) updateGame(game, right);
    assert(game.shop.selectedIndex == 1);
    updateGame(game, idle);
    updateGame(game, right);
    assert(game.shop.selectedIndex == 2);
    updateGame(game, idle);
    updateGame(game, right);
    assert(game.shop.selectedIndex == 0);
    updateGame(game, idle);
    updateGame(game, left);
    assert(game.shop.selectedIndex == 2);

    // Недостаток денег блокирует активку, но оставляет карточку доступной.
    game.combat.playerScore = 0;
    updateGame(game, pressB);
    updateGame(game, pressA);
    assert(game.state == GameState::Shop && !game.shop.choosingSlot);

    for (uint8_t hp = 0; hp <= 6; ++hp) {
        for (uint8_t i = 0; i < 4; ++i)
            assert(heartState(hp, i) == (hp > i + 4 ? 2 : (hp > i ? 1 : 0)));
    }
}

void testContactAndSpawns() {
    Game game = fixture();
    const int16_t px = game.player.x + HALF_PLAYER, py = game.player.y + HALF_PLAYER;
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
        spawnEnemy(game.combat.enemies[i], EnemyType::Fast, px + (10 + i) * 16, py, 1,
               game.combat.currentStage);
    const int16_t oldX = game.player.x, oldY = game.player.y;
    updateGame(game, idle);
    assert(game.player.hp == 3 && game.player.iframes == 60);
    assert(game.player.x == oldX && game.player.y == oldY);
    for (unsigned frame = 1; frame < 180; ++frame) {
        // Keep enemies alive to test sustained swarm pressure independently of auto fire.
        game.combat.shotCooldown = 2;
        for (Projectile& p : game.combat.projectiles) p.framesLeft = 0;
        updateGame(game, idle);
        assertSeparated(game);
        assert(game.player.hp == 3 - frame / 60);
    }
    updateGame(game, idle);
    assert(game.state == GameState::GameOver && game.player.hp == 0);
    const Game dead = game;
    updateGame(game, idle);
    assert(std::memcmp(&game, &dead, sizeof(Game)) == 0);
    updateGame(game, pressB);
    assert(game.state == GameState::Menu);

    for (unsigned axis = 0; axis < 2; ++axis) {
        for (int direction = -1; direction <= 1; direction += 2) {
            game = fixture();
            game.player.x = axis == 0 ? (direction > 0 ? ARENA_WIDTH_FIXED - 8 * 16 : 0) : 48 * 16;
            game.player.y = axis == 1 ? (direction > 0 ? ARENA_HEIGHT_FIXED - 8 * 16 : 0) : 48 * 16;
            const int16_t cx = wrapCoordinate(game.player.x + HALF_PLAYER, ARENA_WIDTH_FIXED);
            const int16_t cy = wrapCoordinate(game.player.y + HALF_PLAYER, ARENA_HEIGHT_FIXED);
            spawnEnemy(game.combat.enemies[0], EnemyType::Basic,
                       axis == 0 ? (direction > 0 ? ENEMY_HALF_WIDTH * 16 : (ARENA_WIDTH - ENEMY_HALF_WIDTH) * 16) : cx,
                       axis == 1 ? (direction > 0 ? ENEMY_HALF_HEIGHT * 16 : (ARENA_HEIGHT - ENEMY_HALF_HEIGHT) * 16) : cy, 3,
                       game.combat.currentStage);
            for (unsigned i = 0; i < 15; ++i) {
                updateGame(game, {int8_t(axis == 0 ? direction : 0), int8_t(axis == 1 ? direction : 0), i == 0, false, false, false});
                assertSeparated(game);
            }
            assert(game.player.hp < 4);
        }
    }
    // Занятый маркер сохраняется, а волна не засчитывается пустой.
    for (uint8_t stage = 0; stage < TOTAL_STAGES; ++stage) {
        for (uint8_t wave = 0; wave < getStageWaveCount(stage); ++wave) {
            for (uint8_t i = 0; i < getWaveEnemyCount(stage, wave); ++i) {
                uint8_t x, y;
                getSpawnPixel(stage, wave, i, x, y);
                game = fixture();
                game.player.x = wrapCoordinate(x * 16 - HALF_PLAYER, ARENA_WIDTH_FIXED);
                game.player.y = wrapCoordinate(y * 16 - HALF_PLAYER, ARENA_HEIGHT_FIXED);
                game.combat.currentStage = stage;
                game.combat.currentWave = wave;
                spawnCurrentWave(game.combat, x * 16, y * 16);
                assert(alive(game.combat) == 0 && game.combat.spawnTimer == 2);
                for (unsigned frame = 0; frame < 10; ++frame) {
                    updateCombat(game.combat, game.player.x, game.player.y);
                    assert(alive(game.combat) == 0 && game.combat.currentWave == wave);
                    uint8_t mx, my;
                    getSpawnPixel(stage, wave, i, mx, my);
                    assert(mx == x && my == y);
                }
                // Найдём безопасное место игрока, не меняя ни один маркер.
                bool spawned = false;
                for (uint8_t py = 0; py < ARENA_HEIGHT && !spawned; py += 8) {
                    for (uint8_t px = 0; px < ARENA_WIDTH && !spawned; px += 8) {
                        if (playerBlocked(game.combat.currentStage, px * 16, py * 16)) continue;
                        game.player.x = px * 16;
                        game.player.y = py * 16;
                        game.combat.spawnTimer = 1;
                        updateCombat(game.combat, game.player.x, game.player.y);
                        spawned = alive(game.combat) > 0;
                    }
                }
                assert(spawned && alive(game.combat) == getWaveEnemyCount(stage, wave));
                for (uint8_t j = 0; j < getWaveEnemyCount(stage, wave); ++j) {
                    uint8_t mx, my;
                    getSpawnPixel(stage, wave, j, mx, my);
                    assert(game.combat.enemies[j].x == mx * 16 && game.combat.enemies[j].y == my * 16);
                }
                assertSeparated(game);
            }
        }
    }
    Player player = {};
    player.hp = 4;
    damagePlayer(player);
    for (uint8_t i = 0; i < 60; ++i) {
        player.iframes = 60 - i;
        assert(isPlayerBlinking(player) == (((i / 4) & 1) == 0));
        damagePlayer(player);
        assert(player.hp == 3);
    }
}

void testEnemyBoundariesAndSpeed() {
    const int16_t edges[][2] = {{6 * 16, 52 * 16}, {(ARENA_WIDTH - ENEMY_HALF_WIDTH) * 16, 52 * 16}, {70 * 16, 3 * 16}, {70 * 16, 61 * 16}};
    const int16_t targets[][2] = {{90 * 16, 52 * 16}, {10 * 16, 52 * 16}, {70 * 16, 50 * 16}, {70 * 16, 10 * 16}};
    const EnemyType types[] = {EnemyType::Basic, EnemyType::Fast, EnemyType::Splitter};
    for (uint8_t edge = 0; edge < 4; ++edge) {
        for (EnemyType type : types) {
            Game game = fixture();
            spawnEnemy(game.combat.enemies[0], type, edges[edge][0], edges[edge][1], 8,
                   game.combat.currentStage);
            updateEnemies(game.combat.enemies, game.combat.scoreOrbs, targets[edge][0], targets[edge][1],
                          game.combat.currentStage);
            const Enemy& e = game.combat.enemies[0];
            if (edge == 0) assert(e.x > edges[edge][0]);
            if (edge == 1) assert(e.x < edges[edge][0]);
            if (edge == 2) assert(e.y > edges[edge][1]);
            if (edge == 3) assert(e.y < edges[edge][1]);
        }
        Game game = fixture();
        spawnEnemy(game.combat.enemies[0], EnemyType::Splitter, edges[edge][0], edges[edge][1], 16,
                   game.combat.currentStage);
        damageEnemy(game.combat, 0, 31);
        assert(alive(game.combat) == 2);
        for (uint8_t i = 0; i < 2; ++i) {
            assert(game.combat.enemies[i].x == edges[edge][0]);
            assert(game.combat.enemies[i].y == edges[edge][1]);
        }
        for (unsigned frame = 0; frame < 100; ++frame) {
            const Enemy before[] = {game.combat.enemies[0], game.combat.enemies[1]};
            // Контакт на границе не должен переносить врага; дети расталкиваются локально.
            const int16_t px = frame < 50 || edge < 2 ? edges[edge][0] : 40 * 16;
            const int16_t py = frame < 50 || edge >= 2 ? edges[edge][1] : 30 * 16;
            updateEnemies(game.combat.enemies, game.combat.scoreOrbs, px, py,
                          game.combat.currentStage);
            for (uint8_t i = 0; i < 2; ++i) {
                const Enemy& e = game.combat.enemies[i];
                assert(enemyPositionValid(game.combat.currentStage, e.x, e.y));
                assert(e.x - before[i].x <= 10 && before[i].x - e.x <= 10);
                assert(e.y - before[i].y <= 10 && before[i].y - e.y <= 10);
            }
        }
    }
    Enemy invalid = {};
    spawnEnemy(invalid, EnemyType::Basic, -1, 52 * 16, 2, 0);
    assert(getEnemyType(invalid) == EnemyType::None);
    spawnEnemy(invalid, EnemyType::Basic, ARENA_WIDTH_FIXED, 52 * 16, 2, 0);
    assert(getEnemyType(invalid) == EnemyType::None);
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
        if (!dx && !dy) continue;
        int previousSpeedSquared = 0;
        for (uint8_t hp = FAST_MAX_HP; hp > 0; --hp) {
            Game game = fixture();
            Enemy& e = game.combat.enemies[0];
            spawnEnemy(e, EnemyType::Fast, 65 * 16, 52 * 16, 0, game.combat.currentStage);
            setEnemyHp(e, hp);
            updateEnemies(game.combat.enemies, game.combat.scoreOrbs, (65 + dx * 20) * 16,
                          (52 + dy * 10) * 16, game.combat.currentStage);
            const int vx = e.x - 65 * 16, vy = e.y - 52 * 16;
            const int speedSquared = vx * vx + vy * vy;
            assert(speedSquared >= previousSpeedSquared);
            assert(speedSquared < WALK_SPEED * WALK_SPEED && speedSquared < 2 * 11 * 11);
            previousSpeedSquared = speedSquared;
        }
        assert(previousSpeedSquared >= FAST_MAX_SPEED * FAST_MAX_SPEED);
    }
}

void testAbilitiesAndCombat() {
    Game game = fixture();
    const int16_t centerX = game.player.x + HALF_PLAYER;
    const int16_t centerY = game.player.y + HALF_PLAYER;

    // Sweep damages only nearby enemies and pushes them away from the player.
    game.player.slots[0] = {AbilityId::MarkAndSweep, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, centerX + 20 * 16,
               centerY, 8, game.combat.currentStage);
    spawnEnemy(game.combat.enemies[1], EnemyType::Basic, centerX - 35 * 16,
               centerY, 8, game.combat.currentStage);
    const int16_t nearX = game.combat.enemies[0].x;
    updateGame(game, pressA);
    assert(getEnemyHp(game.combat.enemies[0]) == 8);
    assert(getEnemyHp(game.combat.enemies[1]) == 10);
    assert(game.combat.enemies[0].x > nearX);
    assert(game.player.slots[0].cooldown == SWEEP_COOLDOWN);
    // Wrapped neighbours are pushed away along the same shortest path used by
    // the radius test, not toward the player through the opposite edge.
    game = fixture();
    game.player.x = 0;
    game.player.y = 0;
    game.player.slots[0] = {AbilityId::MarkAndSweep, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Fast, 96 * FIXED_ONE,
               HALF_PLAYER, 0, 0);
    spawnEnemy(game.combat.enemies[1], EnemyType::Fast, 8 * FIXED_ONE,
               60 * FIXED_ONE, 0, 0);
    const int16_t wrappedX = game.combat.enemies[0].x;
    const int16_t wrappedY = game.combat.enemies[1].y;
    updateGame(game, pressA);
    assert(game.combat.enemies[0].x < wrappedX);
    assert(game.combat.enemies[1].y < wrappedY);
    // Time Warp halves enemy movement; Bit Shift reverses it.
    game = fixture();
    game.player.slots[0] = {AbilityId::TimeWarp, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, centerX + 20 * 16,
               centerY, 0, game.combat.currentStage);
    const int16_t startX = game.combat.enemies[0].x;
    updateGame(game, pressA);
    assert(startX - game.combat.enemies[0].x == (BASIC_SPEED + 1) / 2);
    assert(game.combat.timeWarpTicks == TIME_WARP_DURATION);
    assert(game.player.slots[0].cooldown == TIME_WARP_COOLDOWN);
    game = fixture();
    game.player.slots[0] = {AbilityId::BitShift, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, centerX + 20 * 16,
               centerY, 0, game.combat.currentStage);
    const int16_t reversedX = game.combat.enemies[0].x;
    updateGame(game, pressA);
    assert(game.combat.enemies[0].x > reversedX);
    assert(game.combat.bitShiftTicks == BIT_SHIFT_DURATION);
    // Free deletes basics and deals six damage to tougher enemies.
    game = fixture();
    game.player.slots[0] = {AbilityId::Free, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 75 * 16, 50 * 16, 8,
               game.combat.currentStage);
    spawnEnemy(game.combat.enemies[1], EnemyType::Fast, 85 * 16, 50 * 16, 0,
               game.combat.currentStage);
    setEnemyHp(game.combat.enemies[1], FAST_MAX_HP);
    updateGame(game, pressA);
    assert(getEnemyType(game.combat.enemies[0]) == EnemyType::None);
    assert(getEnemyHp(game.combat.enemies[1]) == FAST_MAX_HP - FREE_DAMAGE);
    assert(game.player.slots[0].cooldown == FREE_COOLDOWN);
    // Recursive Call adds two half-damage side shots for each normal target.
    game = fixture();
    game.player.slots[0] = {AbilityId::RecursiveCall, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, centerX + 25 * 16,
               centerY, 8, game.combat.currentStage);
    updateGame(game, pressA);
    assert(game.combat.recursiveTicks == RECURSIVE_DURATION);
    assert(projectiles(game.combat) == 3);
    for (const Projectile& projectile : game.combat.projectiles) {
        if (!projectile.framesLeft) continue;
        assert(projectileX(projectile) >= 0 && projectileX(projectile) < ARENA_WIDTH_FIXED);
        assert(projectileY(projectile) >= 0 && projectileY(projectile) < ARENA_HEIGHT_FIXED);
        assert(projectileVelocityX(projectile) || projectileVelocityY(projectile));
    }
    // Stack Overflow emits twelve double-damage spiral shots at fixed intervals.
    game = fixture();
    game.player.slots[0] = {AbilityId::StackOverflow, 0};
    game.combat.shotCooldown = 255;
    updateGame(game, pressA);
    assert(game.combat.spiralShots == STACK_OVERFLOW_SHOTS - 1);
    assert(projectiles(game.combat) == 1);
    for (unsigned frame = 0; frame < (STACK_OVERFLOW_SHOTS - 1) * STACK_OVERFLOW_DELAY;
         ++frame)
        updateGame(game, idle);
    assert(game.combat.spiralShots == 0 && projectiles(game.combat) == STACK_OVERFLOW_SHOTS);
    // Memory Dump snapshots pixel coordinates and pulses damage in its radius.
    game = fixture();
    game.player.slots[0] = {AbilityId::MemoryDump, 0};
    game.combat.shotCooldown = 255;
    spawnEnemy(game.combat.enemies[0], EnemyType::Fast, centerX, centerY, 0,
               game.combat.currentStage);
    setEnemyHp(game.combat.enemies[0], FAST_MAX_HP);
    updateGame(game, pressA);
    assert(game.combat.puddleX == centerX / FIXED_ONE);
    assert(game.combat.puddleY == centerY / FIXED_ONE);
    assert(game.combat.puddleTicks == MEMORY_DUMP_DURATION);
    for (unsigned frame = 0; frame < 7 * COOLDOWN_TICK_FRAMES; ++frame)
        updateGame(game, idle);
    assert(getEnemyHp(game.combat.enemies[0]) == FAST_MAX_HP - 1);
    // Fifths accumulate exactly, preserving fractional projectile damage.
    game = fixture();
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 75 * 16, 50 * 16, 8,
               game.combat.currentStage);
    const uint8_t hp = getEnemyHp(game.combat.enemies[0]);
    damageEnemyFifths(game.combat, 0, 2);
    assert(getEnemyHp(game.combat.enemies[0]) == hp &&
           getDamageRemainder(game.combat.enemies[0]) == 2);
    damageEnemyFifths(game.combat, 0, 3);
    assert(getEnemyHp(game.combat.enemies[0]) == hp - 1 &&
           getDamageRemainder(game.combat.enemies[0]) == 0);

    game = fixture();
    spawnEnemy(game.combat.enemies[0], EnemyType::Splitter, 75 * 16, 50 * 16, 16,
               game.combat.currentStage);
    damageEnemy(game.combat, 0, 31);
    assert(alive(game.combat) == 2);
    assert(game.combat.audioEvents & AUDIO_EVENT_ENEMY_DEATH);
    assert(getEnemyHp(game.combat.enemies[0]) == 8 && getEnemyHp(game.combat.enemies[1]) == 8);
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
        spawnEnemy(game.combat.enemies[i], EnemyType::Splitter, 75 * 16, 50 * 16, 16,
                   game.combat.currentStage);
    for (ScoreOrb& orb : game.combat.scoreOrbs) orb = {0, 0, 1, 100};
    damageEnemy(game.combat, 0, 31);
    assert(alive(game.combat) == MAX_ENEMIES && game.combat.playerScore == 16);
    damageEnemy(game.combat, MAX_ENEMIES, 31); // Invalid index is harmless.
    game = fixture();
    game.combat.currentStage = 1;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 75 * 16, 50 * 16, 3,
               game.combat.currentStage);
    // Стена col59 на row 50 закрывает луч влево; видимый враг справа.
    assert(shotBlocked(1, 60 * 16, 50 * 16, (59 - 60) * 16, 0));
    assert(!shotBlocked(1, 60 * 16, 50 * 16, (75 - 60) * 16, 0));
    updateCombat(game.combat, 60 * 16 - HALF_PLAYER, 50 * 16 - HALF_PLAYER);
    assert(game.combat.projectiles[0].framesLeft == PROJECTILE_LIFETIME);
    assert(projectileVelocityX(game.combat.projectiles[0]) > 0);
    createScoreOrb(game.combat.scoreOrbs, 0, 0, 5);
    assert(collectOrbs(game.combat.scoreOrbs, ARENA_WIDTH_FIXED - 16, ARENA_HEIGHT_FIXED - 16) == 5);
}

void testPassiveFireUpgrades() {
    // Optimized Build adds fifths of damage without discarding the remainder.
    for (uint8_t level = 0; level <= 2; ++level) {
        Combat combat = {};
        combat.waveCompleted = true;
        spawnEnemy(combat.enemies[0], EnemyType::Basic, 75 * FIXED_ONE,
                   50 * FIXED_ONE, 8, 0);
        combat.projectiles[0] = {68 * 2, 50 * 2, 0, 1};
        updateCombat(combat, 20 * FIXED_ONE, 20 * FIXED_ONE, level);
        assert(getEnemyHp(combat.enemies[0]) == 9);
        assert(getDamageRemainder(combat.enemies[0]) == level);
    }

    // Fragmentation chooses distinct targets, up to one plus its level.
    for (uint8_t targets = 1; targets <= 4; ++targets) {
        Combat combat = {};
        combat.waveCompleted = true;
        for (uint8_t i = 0; i < targets; ++i)
            spawnEnemy(combat.enemies[i], EnemyType::Basic,
                       (44 + i * 8) * FIXED_ONE, 30 * FIXED_ONE, i, 0);
        updateCombat(combat, 20 * FIXED_ONE - HALF_PLAYER,
                     30 * FIXED_ONE - HALF_PLAYER, 0, 0, targets - 1, 0);
        assert(projectiles(combat) == targets);
    }

    // Overclock scales the interval; range level one reaches a target outside base range.
    for (uint8_t level = 0; level <= 3; ++level) {
        Combat combat = {};
        combat.waveCompleted = true;
        combat.timeWarpTicks = 1;
        spawnEnemy(combat.enemies[0], EnemyType::Basic, 64 * FIXED_ONE,
                   48 * FIXED_ONE, 0, 0);
        updateCombat(combat, 20 * FIXED_ONE - HALF_PLAYER,
                     20 * FIXED_ONE - HALF_PLAYER, 0, level, 0, 1);
        assert(projectiles(combat) == 1);
        const uint8_t rate = 3 + level;
        assert(combat.shotCooldown == (SHOT_INTERVAL * 3 + rate - 1) / rate);
    }
    Combat combat = {};
    combat.waveCompleted = true;
    combat.timeWarpTicks = 1;
    spawnEnemy(combat.enemies[0], EnemyType::Basic, 64 * FIXED_ONE,
               48 * FIXED_ONE, 0, 0);
    updateCombat(combat, 20 * FIXED_ONE - HALF_PLAYER,
                 20 * FIXED_ONE - HALF_PLAYER, 0, 0, 0, 0);
    assert(projectiles(combat) == 0);

    // Quantized auto-aim still reaches shallow, diagonal and wrapped targets.
    const int16_t player[][2] = {{20, 20}, {20, 20}, {0, 28}};
    const int16_t target[][2] = {{60, 25}, {50, 50}, {96, 31}};
    for (uint8_t scenario = 0; scenario < 3; ++scenario) {
        Combat aimed = {};
        aimed.waveCompleted = true;
        spawnEnemy(aimed.enemies[0], EnemyType::Basic,
                   target[scenario][0] * FIXED_ONE,
                   target[scenario][1] * FIXED_ONE, 8, 0);
        const uint8_t initialHp = getEnemyHp(aimed.enemies[0]);
        updateCombat(aimed, player[scenario][0] * FIXED_ONE - HALF_PLAYER,
                     player[scenario][1] * FIXED_ONE - HALF_PLAYER);
        aimed.shotCooldown = 255;
        for (uint8_t frame = 0; frame < PROJECTILE_LIFETIME &&
                                getEnemyHp(aimed.enemies[0]) == initialHp;
             ++frame) {
            aimed.enemies[0].x = target[scenario][0] * FIXED_ONE;
            aimed.enemies[0].y = target[scenario][1] * FIXED_ONE;
            updateCombat(aimed,
                         player[scenario][0] * FIXED_ONE - HALF_PLAYER,
                         player[scenario][1] * FIXED_ONE - HALF_PLAYER);
        }
        assert(getEnemyHp(aimed.enemies[0]) < initialHp);
    }
}

// Independent floating-point reference is test-only; gameplay uses integer geometry.
bool referenceSegment(int x, int y, int dx, int dy, const Obstacle& box) {
    if (!box.width || !box.height) return false;
    for (int cy = -2; cy <= 2; ++cy) for (int cx = -2; cx <= 2; ++cx) {
        const double low[] = {double(box.x * 16 + cx * ARENA_WIDTH_FIXED), double(box.y * 16 + cy * ARENA_HEIGHT_FIXED)};
        const double high[] = {low[0] + box.width * 16 - 1, low[1] + box.height * 16 - 1};
        const double origin[] = {double(x), double(y)}, delta[] = {double(dx), double(dy)};
        double enter = 0, leave = 1;
        bool possible = true;
        for (unsigned axis = 0; axis < 2; ++axis) {
            if (!delta[axis]) { if (origin[axis] < low[axis] || origin[axis] > high[axis]) possible = false; continue; }
            const double a = (low[axis] - origin[axis]) / delta[axis], b = (high[axis] - origin[axis]) / delta[axis];
            const double near = a < b ? a : b, far = a > b ? a : b;
            if (near > enter) enter = near;
            if (far < leave) leave = far;
        }
        if (possible && enter <= leave) return true;
    }
    return false;
}

// Independent oracle for shotBlocked on axis-aligned rays: every pixel the
// segment crosses, walked in whole-pixel steps. The game marches dominant-axis
// samples (spacing <= 1px), so both report the exact same pixel set.
int normalizeTest(int value, int extent) {
    return (value % extent + extent) % extent;
}

bool referencePixelRay(int stage, int x, int y, int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return stageWallPixel(stage, uint8_t(x / FIXED_ONE), uint8_t(y / FIXED_ONE));
    }
    const int delta = dx != 0 ? dx : dy;
    const int extent = dx != 0 ? ARENA_WIDTH_FIXED : ARENA_HEIGHT_FIXED;
    const int sign = delta > 0 ? 1 : -1;
    const int magnitude = sign * delta;
    const int start = dx != 0 ? x : y;
    // Каждая целая пиксельная граница (кратно FIXED_ONE) и сам последний
    // подкамень отрезка: ровно те пиксели, которые видит марш игры.
    const int steps = magnitude / FIXED_ONE;
    for (int k = 0; k <= steps; ++k) {
        const int sample = start + sign * k * FIXED_ONE;
        const int pos = (sample % extent + extent) % extent;
        if (stageWallPixel(stage, uint8_t(dx != 0 ? pos / FIXED_ONE : x / FIXED_ONE),
                           uint8_t(dy != 0 ? pos / FIXED_ONE : y / FIXED_ONE))) {
            return true;
        }
    }
    int end = (start + delta) % extent;
    if (end < 0) end += extent;
    return stageWallPixel(stage, uint8_t(dx != 0 ? end / FIXED_ONE : x / FIXED_ONE),
                          uint8_t(dy != 0 ? end / FIXED_ONE : y / FIXED_ONE));
}

void testGeometry() {
    uint32_t rng = 0x94ab276du;
    auto random = [&rng]() -> uint32_t { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (unsigned trial = 0; trial < 50000; ++trial) {
        const int x = random() % ARENA_WIDTH_FIXED, y = random() % ARENA_HEIGHT_FIXED;
        const int dx = int(random() % (ARENA_WIDTH_FIXED + 1)) - ARENA_WIDTH_FIXED / 2;
        const int dy = int(random() % (ARENA_HEIGHT_FIXED + 1)) - ARENA_HEIGHT_FIXED / 2;
        const Obstacle box = {uint8_t(random() % ARENA_WIDTH), uint8_t(random() % ARENA_HEIGHT), uint8_t(random() % 20), uint8_t(random() % 20)};
        assert(segmentHitsBox(x, y, dx, dy, box) == referenceSegment(x, y, dx, dy, box));
    }
    // Стейдж 1 — пустое поле: никакой выстрел не блокируется.
    for (int x = 0; x < ARENA_WIDTH_FIXED; x += 3) {
        for (int dx = -ARENA_WIDTH_FIXED / 2; dx <= ARENA_WIDTH_FIXED / 2; dx += 7) {
            assert(!shotBlocked(0, normalizeTest(x, ARENA_WIDTH_FIXED), 0, dx, 0));
            for (int dy = -ARENA_HEIGHT_FIXED / 2; dy <= ARENA_HEIGHT_FIXED / 2; dy += 7) {
                assert(!shotBlocked(0, 0, normalizeTest(x, ARENA_HEIGHT_FIXED), 0, dy));
            }
        }
    }
    // Оси-выровненные лучи по пиксельным стенам stейджей 2 и 3.
    for (uint8_t stage = 1; stage <= 2; ++stage) {
        for (int y = 0; y < ARENA_HEIGHT_FIXED; y += FIXED_ONE) {
            for (int x = 0; x < ARENA_WIDTH_FIXED; x += 13) {
                for (int dx = -ARENA_WIDTH_FIXED / 2; dx <= ARENA_WIDTH_FIXED / 2; dx += 5) {
                    assert(shotBlocked(stage, x, y, dx, 0) ==
                           referencePixelRay(stage, x, y, dx, 0));
                }
            }
        }
        for (int x = 0; x < ARENA_WIDTH_FIXED; x += 13) {
            for (int y = 0; y < ARENA_HEIGHT_FIXED; y += FIXED_ONE) {
                for (int dy = -ARENA_HEIGHT_FIXED / 2; dy <= ARENA_HEIGHT_FIXED / 2; dy += 5) {
                    assert(shotBlocked(stage, x, y, 0, dy) ==
                           referencePixelRay(stage, x, y, 0, dy));
                }
            }
        }
    }
}

void testLiveCombatStress() {
    Game game = {};
    uint32_t rng = 0xabcdef01u;
    unsigned deaths = 0;
    for (unsigned frame = 0; frame < 20000; ++frame) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
const InputFrame input = {int8_t(int(rng % 3) - 1), int8_t(int((rng >> 8) % 3) - 1),
                                   frame % 251 == 0, frame % 137 == 0, false, false};
        if (game.state == GameState::GameOver) ++deaths;
        if (game.state == GameState::Playing) updateGame(game, input);
        else startGame(game);
        assert(game.combat.stageTimer <= STAGE_TIME_FRAMES);
        assert(game.player.hp <= game.player.maxHp && game.player.maxHp <= 6);
        if (game.state == GameState::Playing) {
            assert(!playerBlocked(game.combat.currentStage, game.player.x, game.player.y));
            assertSeparated(game);
        }
    }
    assert(deaths > 0);
    std::printf("Live combat stress: 20000 frames, %u death/restart flows\n", deaths);
}

void testBoss() {
    static_assert(sizeof(Projectile) == 4, "Projectile pool must stay packed");
    Game game = fixture();
    game.combat.currentStage = BOSS_STAGE;
    resetBossStage(game.combat);
    assert(!game.combat.boss.hpFifths);
    for (unsigned i = 0; i < SPAWN_DELAY_FRAMES; ++i)
        updateCombat(game.combat, game.player.x, game.player.y);
    assert(game.combat.boss.hpFifths == BOSS_MAX_HP * 5);
    assert(game.combat.spawnTimer == BOSS_AWAKE_FRAMES);

    uint8_t spawnX, spawnY;
    getBossSpawnPixel(spawnX, spawnY);
    assert(game.combat.boss.x == spawnX * FIXED_ONE);
    assert(game.combat.boss.y == spawnY * FIXED_ONE);

    const int16_t startX = game.combat.boss.x;
    const int16_t startY = game.combat.boss.y;
    for (uint8_t step = 0; step < BOSS_PATROL_LEG_LEN * 4; ++step) {
        for (uint8_t frame = 0; frame < BOSS_STEP_INTERVAL_FRAMES; ++frame) {
            updateBoss(game.combat, game.player.x, game.player.y);
            const int16_t centerX = game.combat.boss.x / FIXED_ONE;
            const int16_t centerY = game.combat.boss.y / FIXED_ONE;
            for (int16_t y = centerY - BOSS_HALF_HEIGHT;
                 y < centerY + BOSS_HALF_HEIGHT; ++y)
                for (int16_t x = centerX - BOSS_HALF_WIDTH;
                     x < centerX + BOSS_HALF_WIDTH; ++x)
                    assert(!stageWallPixel(BOSS_STAGE, x, y));
        }
    }
    assert(game.combat.boss.phase == 0);
    assert(game.combat.boss.x == startX && game.combat.boss.y == startY);
    assert(alive(game.combat) >= 1);
    for (const Enemy& enemy : game.combat.enemies)
        if (getEnemyType(enemy) != EnemyType::None)
            assert(enemyPositionValid(BOSS_STAGE, enemy.x, enemy.y));

    // Пробуждение блокирует прицеливание; затем Recursive Call безопасно
    // выбирает босса вместо индекса массива врагов.
    for (Projectile& projectile : game.combat.projectiles) projectile.framesLeft = 0;
    for (Enemy& enemy : game.combat.enemies) setEnemyType(enemy, EnemyType::None);
    game.combat.shotCooldown = 0;
    const uint16_t awakeHp = game.combat.boss.hpFifths;
    game.combat.projectiles[0] = {142, 64, 0, 4};
    updateCombat(game.combat, 51 * FIXED_ONE, 29 * FIXED_ONE);
    assert(game.combat.boss.hpFifths == awakeHp);
    for (Projectile& projectile : game.combat.projectiles) projectile.framesLeft = 0;
    game.combat.spawnTimer = 0;
    game.combat.recursiveTicks = 1;
    game.combat.shotCooldown = 0;
    updateCombat(game.combat, 51 * FIXED_ONE, 29 * FIXED_ONE, 0, 0, 3, 0);
    assert(projectiles(game.combat) == 3);
    const uint16_t beforeAutoFire = game.combat.boss.hpFifths;
    game.combat.shotCooldown = 255;
    for (uint8_t frame = 0; frame < PROJECTILE_LIFETIME &&
                            game.combat.boss.hpFifths == beforeAutoFire;
         ++frame)
        updateCombat(game.combat, 51 * FIXED_ONE, 29 * FIXED_ONE);
    assert(game.combat.boss.hpFifths < beforeAutoFire);

    // Обычный и двойной снаряды используют те же пятые доли, что улучшения.
    game.combat.boss.x = 79 * FIXED_ONE;
    game.combat.boss.y = 32 * FIXED_ONE;
    game.combat.boss.waveTimer = 100;
    game.combat.recursiveTicks = 0;
    game.combat.shotCooldown = 255;
    for (Projectile& projectile : game.combat.projectiles) projectile.framesLeft = 0;
    const uint16_t hp = game.combat.boss.hpFifths;
    game.combat.projectiles[0] = {142, 64, 0, 4};
    updateCombat(game.combat, 20 * FIXED_ONE, 20 * FIXED_ONE, 2);
    assert(game.combat.boss.hpFifths == hp - 7);
    game.combat.projectiles[0] = {142, 64, uint8_t(2 << 5), 4};
    updateCombat(game.combat, 20 * FIXED_ONE, 20 * FIXED_ONE, 0);
    assert(game.combat.boss.hpFifths == hp - 17);

    assert(checkPlayerEnemyCollisions(
        game.combat, game.combat.boss.x - HALF_PLAYER,
        game.combat.boss.y - HALF_PLAYER));

    game.combat.stageTimer = 126;
    game.combat.scoreOrbs[0] = {1, 1, 9, 10};
    damageBossFifths(game.combat, 255);
    damageBossFifths(game.combat, 255);
    assert(!game.combat.boss.hpFifths && game.combat.stageCleared);
    assert(game.combat.playerScore == BOSS_SCORE + 9 + 200);
    assert(alive(game.combat) == 0 && projectiles(game.combat) == 0);
}
} // namespace

int main() {
    testTimerAndWaves();
    testShop();
    testContactAndSpawns();
    testEnemyBoundariesAndSpeed();
    testAbilitiesAndCombat();
    testPassiveFireUpgrades();
    testGeometry();
    testBoss();
    testLiveCombatStress();
    std::puts("Combat regressions passed: waves, timer, shop, health, abilities, pools, 50000 geometry rays.");
}
