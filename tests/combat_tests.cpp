#include "arena.h"
#include "game.h"
#include "stages.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace gc;
const InputFrame idle = {0, 0, false, false};
const InputFrame pressA = {0, 0, true, false};
const InputFrame pressB = {0, 0, false, true};
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
        if (stage + 1 < TOTAL_STAGES) {
            assert(game.state == GameState::StageCleared);
            updateGame(game, pressA);
            updateGame(game, pressB);
        }
    }
    assert(game.state == GameState::Win);
    const Game won = game;
    updateGame(game, idle);
    assert(std::memcmp(&game, &won, sizeof(Game)) == 0);
    updateGame(game, pressA);
    assert(game.state == GameState::Title);
    updateGame(game, pressB);
    assert(game.combat.currentStage == 0 && game.combat.playerScore == 0);
}

void testShop() {
    Game game = fixture();
    initShop(game);
    updateGame(game, pressA);
    assert(!game.shop.passiveBought && game.combat.playerScore == 0);
    game.shop.selectedIndex = 3;
    updateGame(game, pressA);
    assert(!game.shop.choosingSlot);
    game.combat.playerScore = 1000;
    game.shop.selectedIndex = 0;
    updateGame(game, pressA);
    assert(game.state == GameState::Shop && game.shop.passiveBought);
    assert(game.passives.damageLevel == 1 && game.combat.playerScore == 900);
    game.shop.selectedIndex = 1;
    updateGame(game, pressA);
    assert(game.player.maxHp == 4 && game.combat.playerScore == 900);
    game.shop.selectedIndex = 3;
    updateGame(game, pressA);
    assert(game.shop.choosingSlot && game.combat.playerScore == 900);
    updateGame(game, {-1, 0, false, false});
    assert(!game.shop.choosingSlot && game.combat.playerScore == 900);
    updateGame(game, pressA);
    updateGame(game, pressB);
    assert(game.player.slots[0].ability == AbilityId::Dash);
    assert(game.player.slots[1].ability == AbilityId::MarkAndSweep);
    assert(game.shop.activeBought && game.combat.playerScore == 700);
    game.shop.selectedIndex = 4;
    updateGame(game, pressA);
    assert(!game.shop.choosingSlot && game.combat.playerScore == 700);
    updateGame(game, pressB);
    assert(game.state == GameState::Playing);

    initShop(game);
    game.shop.selectedIndex = 4;
    updateGame(game, pressA);
    updateGame(game, pressA);
    assert(game.player.slots[0].ability == AbilityId::StopTheWorld);
    assert(game.player.slots[1].ability == AbilityId::MarkAndSweep);
    // Buying an active first still leaves the passive available.
    game.shop.selectedIndex = 2;
    updateGame(game, pressA);
    assert(game.passives.moveSpeedLevel == 1 && game.combat.playerScore == 400);
    initShop(game);
    const InputFrame down = {0, 1, false, false};
    for (unsigned i = 0; i < 10; ++i) updateGame(game, down);
    assert(game.shop.selectedIndex == 1);
    updateGame(game, idle);
    updateGame(game, down);
    assert(game.shop.selectedIndex == 2);

    for (unsigned i = 0; i < 3; ++i) {
        initShop(game);
        game.combat.playerScore = 1000;
        game.shop.selectedIndex = 1;
        updateGame(game, pressA);
        assert(game.player.maxHp == (i == 0 ? 5 : 6));
        assert(game.combat.playerScore == (i < 2 ? 900 : 1000));
    }
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
    assert(game.state == GameState::Title);

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
            game.combat.freezeFrames = 200;
            for (unsigned i = 0; i < 15; ++i) {
                updateGame(game, {int8_t(axis == 0 ? direction : 0), int8_t(axis == 1 ? direction : 0), i == 0, false});
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
    const int16_t edges[][2] = {{6 * 16, 52 * 16}, {98 * 16, 52 * 16}, {70 * 16, 3 * 16}, {70 * 16, 61 * 16}};
    const int16_t targets[][2] = {{90 * 16, 52 * 16}, {10 * 16, 52 * 16}, {70 * 16, 50 * 16}, {70 * 16, 10 * 16}};
    const EnemyType types[] = {EnemyType::Basic, EnemyType::Fast, EnemyType::Splitter};
    for (uint8_t edge = 0; edge < 4; ++edge) {
        for (EnemyType type : types) {
            Game game = fixture();
            spawnEnemy(game.combat.enemies[0], type, edges[edge][0], edges[edge][1], 8,
                   game.combat.currentStage);
            updateEnemies(game.combat.enemies, game.combat.scoreOrbs, targets[edge][0], targets[edge][1],
                          game.combat.currentStage, game.combat.enemyRandomState);
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
                          game.combat.currentStage, game.combat.enemyRandomState);
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
                          (52 + dy * 10) * 16, game.combat.currentStage,
                          game.combat.enemyRandomState);
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
    const int16_t x = game.player.x, y = game.player.y;
    game.player.slots[0] = {AbilityId::MarkAndSweep, 0};
    game.player.slots[1] = {AbilityId::StopTheWorld, 0};
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, x + 20 * 16, y, 8,
                   game.combat.currentStage);
    spawnEnemy(game.combat.enemies[1], EnemyType::Basic, x - 35 * 16, y, 8,
                   game.combat.currentStage);
    updateGame(game, pressA);
    assert(getEnemyHp(game.combat.enemies[0]) == 6);
    assert(getEnemyHp(game.combat.enemies[1]) == 10);
    assert(game.player.x == x && game.player.y == y && game.player.dashFrames == 0);
    assert(game.player.slots[0].cooldown == 180);
    updateGame(game, pressB);
    const Enemy frozen = game.combat.enemies[1];
    assert(game.combat.freezeFrames == FREEZE_DURATION - 1);
    for (unsigned i = 1; i < FREEZE_DURATION; ++i) {
        game.combat.shotCooldown = 2;
        for (Projectile& p : game.combat.projectiles) p.framesLeft = 0;
        updateGame(game, idle);
        assert(game.combat.enemies[1].x == frozen.x && game.combat.enemies[1].y == frozen.y);
    }
    updateGame(game, idle);
    assert(game.combat.enemies[1].x != frozen.x || game.combat.enemies[1].y != frozen.y);
    game = fixture();
    game.player.slots[1] = {AbilityId::Compact, 0};
    createScoreOrb(game.combat.scoreOrbs, 0, 0, 15);
    updateGame(game, pressB);
    assert(game.combat.playerScore == 15 && game.player.iframes == COMPACT_SHIELD_DURATION);
    assert(game.player.slots[1].cooldown == 200);
    assert(game.player.dashFrames == 0);

    game = fixture();
    game.passives.moveSpeedLevel = 3;
    updateGame(game, {1, 0, false, false});
    assert(game.player.x == x + WALK_SPEED + 6);
    // Damage upgrade affects real moving projectiles, including their last live frame.
    for (uint8_t level = 0; level < 4; ++level) {
        game = fixture();
        game.passives.damageLevel = level;
        game.combat.freezeFrames = 10;
        spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 75 * 16, 50 * 16, 8,
                   game.combat.currentStage);
        game.combat.projectiles[0] = {68 * 16, 50 * 16, PROJECTILE_SPEED, 0, 1};
        updateGame(game, idle);
        assert(getEnemyHp(game.combat.enemies[0]) == 9 - level);
    }
    game = fixture();
    spawnEnemy(game.combat.enemies[0], EnemyType::Splitter, 75 * 16, 50 * 16, 16,
               game.combat.currentStage);
    damageEnemy(game.combat, 0, 31);
    assert(alive(game.combat) == 2);
    assert(getEnemyHp(game.combat.enemies[0]) == 8 && getEnemyHp(game.combat.enemies[1]) == 8);
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i)
        spawnEnemy(game.combat.enemies[i], EnemyType::Splitter, 75 * 16, 50 * 16, 16,
                   game.combat.currentStage);
    for (ScoreOrb& orb : game.combat.scoreOrbs) orb = {0, 0, 1, 100};
    damageEnemy(game.combat, 0, 31);
    assert(alive(game.combat) == MAX_ENEMIES && game.combat.playerScore == 16);
    damageEnemy(game.combat, MAX_ENEMIES, 31); // Invalid index is harmless.
    // Pool exhaustion keeps existing projectiles and retries on later frames.
    for (Projectile& p : game.combat.projectiles) p = {48 * 16, 0, 0, 0, 10};
    updateCombat(game.combat, game.player.x, game.player.y);
    for (const Projectile& p : game.combat.projectiles) assert(p.framesLeft == 9);

    game = fixture();
    game.combat.freezeFrames = 10;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 90 * 16, 45 * 16, 3,
               game.combat.currentStage);
    spawnEnemy(game.combat.enemies[1], EnemyType::Basic, 30 * 16, 28 * 16, 3,
               game.combat.currentStage);
    assert(shotBlocked(game.combat.currentStage, 65 * 16 + HALF_PLAYER, 25 * 16 + HALF_PLAYER,
                       90 * 16 - (65 * 16 + HALF_PLAYER), 45 * 16 - (25 * 16 + HALF_PLAYER)));
    updateCombat(game.combat, 65 * 16, 25 * 16);
    assert(game.combat.projectiles[0].framesLeft == PROJECTILE_LIFETIME);
    assert(game.combat.projectiles[0].velocityX < 0); // Visible target, not closer covered target.
    game = fixture();
    game.combat.freezeFrames = 10;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 6 * 16, 50 * 16, 8,
               game.combat.currentStage);
    game.combat.projectiles[0] = {103 * 16, 50 * 16, PROJECTILE_SPEED, 0, 2};
    updateCombat(game.combat, game.player.x, game.player.y);
    assert(getEnemyHp(game.combat.enemies[0]) == 9);
    createScoreOrb(game.combat.scoreOrbs, 0, 0, 5);
    assert(collectOrbs(game.combat.scoreOrbs, ARENA_WIDTH_FIXED - 16, ARENA_HEIGHT_FIXED - 16) == 5);
}

// Очередь выстрелов: две пули подряд, потом полный перезаряд.
void testBurstFire() {
    Game game = fixture();
    game.combat.freezeFrames = 0;
    spawnEnemy(game.combat.enemies[0], EnemyType::Basic, 68 * 16, 48 * 16, 8,
               game.combat.currentStage);
    // Первая пуля захода: короткая пауза до следующей.
    updateCombat(game.combat, game.player.x, game.player.y);
    assert(game.combat.projectiles[0].framesLeft == PROJECTILE_LIFETIME);
    assert(game.combat.burstShots == 1);
    assert(game.combat.shotCooldown == SHOT_BURST_DELAY);
    // Вторая пуля завершает заход и ставит полный SHOT_INTERVAL.
    for (uint8_t i = 1; i < SHOT_BURST_DELAY; ++i)
        updateCombat(game.combat, game.player.x, game.player.y);
    updateCombat(game.combat, game.player.x, game.player.y);
    assert(game.combat.projectiles[1].framesLeft == PROJECTILE_LIFETIME);
    assert(game.combat.burstShots == 0);
    assert(game.combat.shotCooldown == SHOT_INTERVAL);
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

void testGeometry() {
    uint32_t rng = 0x94ab276du;
    auto random = [&rng]() -> uint32_t { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (unsigned trial = 0; trial < 50000; ++trial) {
        const int x = random() % ARENA_WIDTH_FIXED, y = random() % ARENA_HEIGHT_FIXED;
        const int dx = int(random() % (ARENA_WIDTH_FIXED + 1)) - ARENA_WIDTH_FIXED / 2;
        const int dy = int(random() % (ARENA_HEIGHT_FIXED + 1)) - ARENA_HEIGHT_FIXED / 2;
        const Obstacle box = {uint8_t(random() % ARENA_WIDTH), uint8_t(random() % ARENA_HEIGHT), uint8_t(random() % 20), uint8_t(random() % 20)};
        assert(segmentHitsBox(x, y, dx, dy, box) == referenceSegment(x, y, dx, dy, box));
        bool wall = false;
        for (uint8_t i = 0; i < getStageObstacleCount(0); ++i) wall |= referenceSegment(x, y, dx, dy, readObstacle(0, i));
        assert(shotBlocked(0, x, y, dx, dy) == wall);
    }
}

void testLiveCombatStress() {
    Game game = {};
    uint32_t rng = 0xabcdef01u;
    unsigned deaths = 0;
    for (unsigned frame = 0; frame < 20000; ++frame) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const InputFrame input = {int8_t(int(rng % 3) - 1), int8_t(int((rng >> 8) % 3) - 1),
                                  frame % 251 == 0, frame % 137 == 0};
        if (game.state == GameState::GameOver) ++deaths;
        if (game.state == GameState::Playing) updateGame(game, input);
        else updateGame(game, pressB);
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
} // namespace

int main() {
    testTimerAndWaves();
    testShop();
    testContactAndSpawns();
    testEnemyBoundariesAndSpeed();
    testAbilitiesAndCombat();
    testBurstFire();
    testGeometry();
    testLiveCombatStress();
    std::puts("Combat regressions passed: waves, timer, shop, health, abilities, pools, 50000 geometry rays.");
}
