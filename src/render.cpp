#include "render.h"

#include "arena.h"
#include "assets/intro.h"
#include "assets/mainmenu_sound.h"
#include "assets/menu_frames.h"
#include "assets/soundmenu_off.h"
#include "stages.h"
#include <Arduboy2.h>

namespace gc {
namespace {

// Сердечко 5×5 пикселей (PROGMEM)
const uint8_t heartBitmap[] PROGMEM = {
    0b01010, // .#.#.
    0b11111, // #####
    0b11111, // #####
    0b01110, // .###.
    0b00100  // ..#..
};
static_assert(sizeof(heartBitmap) == 5, "Heart bitmap must be 5 bytes");

// Пустое/потерянное сердечко (контур)
const uint8_t heartEmptyBitmap[] PROGMEM = {
    0b01010, // .#.#.
    0b10101, // #.#.#
    0b10001, // #...#
    0b01010, // .#.#.
    0b00100  // ..#..
};

// Монетка очков: круглый контур и вертикальный штрих внутри.
const uint8_t scoreCoinBitmap[] PROGMEM = {
    0b01110, 0b10111, 0b10111, 0b11111, 0b01110,
};

// Кадры игрока: холст 9x7, колонно-мажорно (байт на колонку, бит = строка).
#include "assets/player_frames.h"

constexpr uint8_t PLAYER_FRAME_W = sizeof(playerFrames[0]);
constexpr uint8_t PLAYER_FRAME_H = 7;
constexpr uint8_t WALK_ANIM_FRAMES = 8; // Смена кадра ходьбы каждые ~0.13 с.
// Цикл ходьбы: левая поза, широкие ноги, правая поза, снова широкие ноги.
const uint8_t playerAnimCycle[] PROGMEM = {0, 1, 2, 1};
static_assert(sizeof(playerFrames) % PLAYER_FRAME_W == 0,
              "Player frames must have equal column counts");
static_assert(PLAYER_FRAME_W == 9 && PLAYER_FRAME_H == 7,
              "Player sprite must stay on its 9x7 canvas");

// Рисуем пиксель арены с переходом через края.
// Сначала оборачиваем координаты, потом добавляем смещение HUD.
void drawArenaPixel(Arduboy2 &arduboy, int16_t x, int16_t y, uint8_t color) {
  arduboy.drawPixel(wrapCoordinate(x, ARENA_WIDTH),
                    HUD_HEIGHT + wrapCoordinate(y, ARENA_HEIGHT), color);
}

// Применяем XOR-дельту кадра к буферу поверх уже отрисованного base.
// Дельты во flash: runs [off_lo, off_hi, len, payload...], конец 0xFF 0xFF.
void applyScreenDelta(uint8_t *buf, const uint8_t *delta) {
  uint16_t offset =
      pgm_read_byte(delta) | (uint16_t(pgm_read_byte(delta + 1)) << 8);
  delta += 2;
  while (offset != 0xFFFF) {
    const uint8_t length = pgm_read_byte(delta++);
    for (uint8_t k = 0; k < length; ++k) {
      buf[offset + k] ^= pgm_read_byte(delta + k);
    }
    delta += length;
    offset = pgm_read_byte(delta) | (uint16_t(pgm_read_byte(delta + 1)) << 8);
    delta += 2;
  }
}

// Хитбокс игрока остаётся PLAYER_SIZE, визуальный холст 9x7 центрируем по
// нему: сдвиг влево на (7-9)/2 = -1 пиксель. Во время Dash весь холст белый.
// Кадр ходьбы выбирается фазой walkPhase; покой показывает нулевой кадр.
void drawPlayer(Arduboy2 &arduboy, const Player &player) {
  const int16_t x = player.x / FIXED_ONE - (PLAYER_SIZE - PLAYER_FRAME_W) / 2;
  const int16_t y = player.y / FIXED_ONE - (PLAYER_SIZE - PLAYER_FRAME_H) / 2;
  if (player.dashFrames > 0) {
    for (uint8_t column = 0; column < PLAYER_FRAME_W; ++column)
      for (uint8_t row = 0; row < PLAYER_FRAME_H; ++row)
        drawArenaPixel(arduboy, x + column, y + row, WHITE);
    return;
  }
  const uint8_t frame = player.walkPhase
                          ? pgm_read_byte(&playerAnimCycle[(player.walkPhase /
                                                            WALK_ANIM_FRAMES) &
                                                           (sizeof(playerAnimCycle) - 1)])
                          : 0;
  const uint8_t *bits = &playerFrames[frame][0];
  for (uint8_t column = 0; column < PLAYER_FRAME_W; ++column) {
    const uint8_t columnBits = pgm_read_byte(bits + column);
    for (uint8_t row = 0; row < PLAYER_FRAME_H; ++row) {
      if (columnBits & (1 << row))
        drawArenaPixel(arduboy, x + column, y + row, WHITE);
    }
  }
}

// Рисуем пулю: короткий след из 3 точек для видимости.
// Хитбокс — только головная точка, хвост чисто визуальный.
void drawProjectile(Arduboy2 &arduboy, const Projectile &projectile) {
  if (projectile.framesLeft == 0) {
    return;
  }
  for (uint8_t part = 0; part < 3; ++part) {
    const int16_t x = wrapCoordinate(
        projectile.x - projectile.velocityX * part / 2, ARENA_WIDTH_FIXED);
    const int16_t y = wrapCoordinate(
        projectile.y - projectile.velocityY * part / 2, ARENA_HEIGHT_FIXED);
    drawArenaPixel(arduboy, x / FIXED_ONE, y / FIXED_ONE, WHITE);
  }
}

// Маленький пиксельный шрифт 4×5 для миниатюрных врагов.
// Глифы: 0–9, A–F, 'x' (Basic), 'd' (Splitter). Итого 18 штук, по 5 байт.
// Каждый байт — одна строка, биты 3..0 слева направо (маска 0x8..0x1).
const uint8_t tinyFont[] PROGMEM = {// 0
                                    0b0110, 0b1001, 0b1001, 0b1001, 0b0110,
                                    // 1
                                    0b0010, 0b0110, 0b0010, 0b0010, 0b0111,
                                    // 2
                                    0b1110, 0b0001, 0b0110, 0b1000, 0b1111,
                                    // 3
                                    0b1110, 0b0001, 0b0110, 0b0001, 0b1110,
                                    // 4
                                    0b1001, 0b1001, 0b1111, 0b0001, 0b0001,
                                    // 5
                                    0b1111, 0b1000, 0b1110, 0b0001, 0b1110,
                                    // 6
                                    0b0110, 0b1000, 0b1110, 0b1001, 0b0110,
                                    // 7
                                    0b1111, 0b0001, 0b0010, 0b0100, 0b0100,
                                    // 8
                                    0b0110, 0b1001, 0b0110, 0b1001, 0b0110,
                                    // 9
                                    0b0110, 0b1001, 0b0111, 0b0001, 0b0110,
                                    // A
                                    0b0110, 0b1001, 0b1111, 0b1001, 0b1001,
                                    // B
                                    0b1110, 0b1001, 0b1110, 0b1001, 0b1110,
                                    // C
                                    0b0111, 0b1000, 0b1000, 0b1000, 0b0111,
                                    // D
                                    0b1110, 0b1001, 0b1001, 0b1001, 0b1110,
                                    // E
                                    0b1111, 0b1000, 0b1110, 0b1000, 0b1111,
                                    // F
                                    0b1111, 0b1000, 0b1110, 0b1000, 0b1000,
                                    // x (Basic)
                                    0b1001, 0b0110, 0b0110, 0b0110, 0b1001,
                                    // d (Splitter)
                                    0b0100, 0b0100, 0b1110, 0b1001, 0b1001};

// Индекс в tinyFont: '0'-'9' → 0-9, 'A'-'F' → 10-15, 'x' → 16, 'd' → 17
uint8_t tinyFontIndex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  if (c == 'x')
    return 16;
  return 17; // 'd'
}

// Глиф врага обрезается краем арены, не переносится на другую сторону.
void drawTinyGlyph(Arduboy2 &arduboy, char c, int16_t x, int16_t y) {
  const uint8_t *data = &tinyFont[tinyFontIndex(c) * 5];
  for (uint8_t row = 0; row < 5; ++row) {
    const uint8_t bits = pgm_read_byte(data + row);
    for (uint8_t col = 0; col < 4; ++col) {
      if ((bits & (0x08 >> col)) && x + col >= 0 && x + col < ARENA_WIDTH &&
          y + row >= 0 && y + row < ARENA_HEIGHT) {
        arduboy.drawPixel(x + col, HUD_HEIGHT + y + row, WHITE);
      }
    }
  }
}

// Рисуем врага: компактная строка, например "d10", "x2", "F15"
void drawEnemy(Arduboy2 &arduboy, const Enemy &enemy) {
  const EnemyType type = getEnemyType(enemy);
  const uint8_t hp = getEnemyHp(enemy);
  if (type == EnemyType::None || hp == 0)
    return;

  // Собираем короткую строку: префикс типа + HP в hex
  char buf[4];
  uint8_t len = 1;
  switch (type) {
  case EnemyType::Basic:
    buf[0] = 'x';
    break;
  case EnemyType::Splitter:
    buf[0] = 'd';
    break;
  case EnemyType::Fast:
    buf[0] = 'F';
    break;
  default:
    return;
  }
  if (hp >= 16) {
    buf[len++] = (char)('0' + (hp >> 4));
    const uint8_t lo = hp & 0x0F;
    buf[len++] = lo < 10 ? (char)('0' + lo) : (char)('A' + lo - 10);
  } else if (hp >= 10) {
    buf[len++] = (char)('A' + hp - 10);
  } else {
    buf[len++] = (char)('0' + hp);
  }

  // Центрируем текст относительно позиции врага (enny = 4×len × 5)
  const int16_t centerX = enemy.x / FIXED_ONE;
  const int16_t centerY = enemy.y / FIXED_ONE;
  const int16_t startX =
      centerX - static_cast<int16_t>(len) * 2; // (4 * len) / 2
  const int16_t startY = centerY - 2;          // 5 / 2

  for (uint8_t i = 0; i < len; ++i) {
    drawTinyGlyph(arduboy, buf[i], startX + i * 4, startY);
  }
}

// Рисуем монетку очков 5x5 с переносом через края арены.
void drawScoreOrb(Arduboy2 &arduboy, const ScoreOrb &orb) {
  // Если lifetime = 0, орб не активен
  if (orb.lifetime == 0) {
    return;
  }

  const int16_t screenX = wrapCoordinate(orb.x / FIXED_ONE, ARENA_WIDTH);
  const int16_t screenY =
      HUD_HEIGHT + wrapCoordinate(orb.y / FIXED_ONE, ARENA_HEIGHT);

  for (uint8_t row = 0; row < 5; ++row) {
    const uint8_t bits = pgm_read_byte(&scoreCoinBitmap[row]);
    for (uint8_t col = 0; col < 5; ++col) {
      if (bits & (0x10 >> col))
        drawArenaPixel(arduboy, screenX + col - 2, screenY + row - 2, WHITE);
    }
  }
}

// Рисуем предупреждающие квадраты перед спавном волны
void drawSpawnIndicators(Arduboy2 &arduboy, const Combat &combat) {
  if (combat.spawnTimer == 0) {
    return;
  }

  const uint8_t enemyCount =
      getWaveEnemyCount(combat.currentStage, combat.currentWave);
  for (uint8_t i = 0; i < enemyCount; ++i) {
    const WaveEnemy we =
        readWaveEnemy(combat.currentStage, combat.currentWave, i);
    if (we.type == EnemyType::None) {
      continue;
    }

    // Та же детерминированная позиция, что и при фактическом спавне
    uint8_t px = 0;
    uint8_t py = 0;
    getSpawnPixel(combat.currentStage, combat.currentWave, i, px, py);

    const int16_t screenX = wrapCoordinate(px, ARENA_WIDTH);
    const int16_t screenY = HUD_HEIGHT + wrapCoordinate(py, ARENA_HEIGHT);

    // Мигаем: показываем чётные кадры, скрываем нечётные
    if (combat.spawnTimer & 1) {
      for (int8_t y = -2; y < 2; ++y)
        for (int8_t x = -2; x < 2; ++x)
          drawArenaPixel(arduboy, screenX + x, screenY + y, WHITE);
    }
  }
}

// Рисуем один слот активной способности в HUD.
// label — буква A или B, показываем название способности и заряд.
void printAbility(Arduboy2 &arduboy, AbilityId ability) {
  switch (ability) {
  case AbilityId::Dash:
    arduboy.print(F("Dash"));
    break;
  case AbilityId::MarkAndSweep:
    arduboy.print(F("Sweep"));
    break;
  case AbilityId::StopTheWorld:
    arduboy.print(F("Freeze"));
    break;
  case AbilityId::Compact:
    arduboy.print(F("Compact"));
    break;
  default:
    arduboy.print(F("Empty"));
    break;
  }
}

// Рисуем сердечко 5x5 в указанной позиции
void drawHeart(Arduboy2 &arduboy, int16_t x, int16_t y, const uint8_t *bitmap) {
  for (uint8_t row = 0; row < 5; ++row) {
    const uint8_t bits = pgm_read_byte(&bitmap[row]);
    for (uint8_t col = 0; col < 5; ++col) {
      if (bits & (0x10 >> col)) {
        arduboy.drawPixel(x + col, y + row, WHITE);
      }
    }
  }
}

void drawShopHeader(Arduboy2 &arduboy, const Game &game) {
  const bool passive = game.shop.category == ShopCategory::Passive;
  arduboy.setCursor(19, 0);
  arduboy.print(passive ? F("PASSIVE UPGRADE") : F("ACTIVE UPGRADE"));
  arduboy.setCursor(92, 8);
  arduboy.print(F("$"));
  arduboy.print(game.combat.playerScore);
}

bool passiveAtCap(const Game &game, PassiveId id) {
  switch (id) {
  case PassiveId::DamageUp:
    return game.passives.damageLevel >= 3;
  case PassiveId::MaxHpUp:
    return game.player.maxHp >= PLAYER_MAX_HP_CAP;
  case PassiveId::MoveSpeedUp:
    return game.passives.moveSpeedLevel >= 3;
  default:
    return true;
  }
}

void drawPassiveCard(Arduboy2 &arduboy, PassiveId id) {
  arduboy.setCursor(43, 16);
  switch (id) {
  case PassiveId::DamageUp:
    arduboy.print(F("DAMAGE"));
    arduboy.setCursor(31, 24);
    arduboy.print(F("SHOT DMG +1"));
    break;
  case PassiveId::MaxHpUp:
    arduboy.print(F("MAX HP"));
    arduboy.setCursor(28, 24);
    arduboy.print(F("HP+1 HEAL+1"));
    break;
  case PassiveId::MoveSpeedUp:
    arduboy.print(F("SPEED"));
    arduboy.setCursor(34, 24);
    arduboy.print(F("MOVE +2/16"));
    break;
  default:
    break;
  }
}

void drawActiveCard(Arduboy2 &arduboy, ActiveUpgradeId id) {
  arduboy.setCursor(43, 16);
  switch (id) {
  case ActiveUpgradeId::MarkAndSweep:
    arduboy.print(F("SWEEP"));
    arduboy.setCursor(25, 24);
    arduboy.print(F("DMG4 R24 CD3S"));
    break;
  case ActiveUpgradeId::StopTheWorld:
    arduboy.print(F("FREEZE"));
    arduboy.setCursor(22, 24);
    arduboy.print(F("FREEZE90F CD4S"));
    break;
  case ActiveUpgradeId::Compact:
    arduboy.print(F("COMPACT"));
    arduboy.setCursor(16, 24);
    arduboy.print(F("DROPS SH60F CD3S"));
    break;
  default:
    break;
  }
}

// Рисуем правую колонку UI (x=104, 6 строк по 8px)
// Строка 0: lvl<N>
// Строка 1: 4 сердечка HP (+ подсветка 5-6)
// Строка 2: Score (4 цифры или 10k/11k...)
// Строка 3: Time remaining (секунды)
// Строка 4: Slot A cooldown bar
// Строка 6: Slot B cooldown bar
void drawUIColumn(Arduboy2 &arduboy, const Game &game) {
  const uint8_t colX = UI_COLUMN_X;
  const uint8_t rowH = UI_ROW_HEIGHT;

  // Строка 0: номер стейджа "lvl1", "lvl2"...
  arduboy.setCursor(colX, 0 * rowH);
  arduboy.print(F("LVL"));
  arduboy.print(game.combat.currentStage + 1);

  // Строка 1: сердечка HP
  const uint8_t heartY = 1 * rowH + 1; // +1 для центрирования в 8px
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t hx = colX + i * 6; // 5px сердечко + 1px отступ
    const uint8_t state = heartState(game.player.hp, i);
    drawHeart(arduboy, hx, heartY, state ? heartBitmap : heartEmptyBitmap);
    if (state == 2)
      arduboy.drawPixel(hx + 2, heartY + 2, BLACK);
  }

  // Строка 2: Score (4 цифры или 10k/11k...)
  arduboy.setCursor(colX, 2 * rowH);
  const uint16_t score = game.combat.playerScore;
  if (score >= 10000) {
    arduboy.print(score / 1000);
    arduboy.print(F("k"));
  } else {
    // Выравнивание в 4 символа
    if (score < 1000)
      arduboy.print(F(" "));
    if (score < 100)
      arduboy.print(F(" "));
    if (score < 10)
      arduboy.print(F(" "));
    arduboy.print(score);
  }

  // Строка 3: Time remaining в секундах
  arduboy.setCursor(colX, 3 * rowH);
  const uint16_t timeLeft = remainingSeconds(game.combat.stageTimer);
  if (timeLeft < 10)
    arduboy.print(F(" "));
  if (timeLeft < 100)
    arduboy.print(F(" "));
  arduboy.print(timeLeft);

  // Строка 4: Slot A cooldown bar (мини-версия)
  {
    const ActiveSlot &slot = game.player.slots[0];
    arduboy.setCursor(colX, 4 * rowH);
    arduboy.print(F("A"));
    if (slot.ability != AbilityId::None) {
      // Маленькая полоска кд: 16px ширина
      const uint8_t maxCd = abilityCooldown(slot.ability);
      const uint8_t readyWidth =
          (maxCd > slot.cooldown)
              ? (static_cast<uint16_t>(maxCd - slot.cooldown) * 14 / maxCd)
              : 0;
      arduboy.drawRect(colX + 7, 4 * rowH + 2, 16, 4);
      if (readyWidth > 0) {
        arduboy.fillRect(colX + 8, 4 * rowH + 3, readyWidth, 2);
      }
    } else {
      arduboy.print(F(":--"));
    }
  }

  // Slot B uses its own ability's full cooldown.
  {
    const ActiveSlot &slot = game.player.slots[1];
    arduboy.setCursor(colX, 5 * rowH);
    arduboy.print(F("B"));
    if (slot.ability != AbilityId::None) {
      const uint8_t maxCd = abilityCooldown(slot.ability);
      const uint8_t readyWidth =
          (maxCd > slot.cooldown)
              ? (static_cast<uint16_t>(maxCd - slot.cooldown) * 14 / maxCd)
              : 0;
      arduboy.drawRect(colX + 7, 5 * rowH + 1, 16, 4);
      if (readyWidth > 0) {
        arduboy.fillRect(colX + 8, 5 * rowH + 2, readyWidth, 2);
      }
    } else {
      arduboy.print(F(":--"));
    }
  }
}

} // namespace

// Отрисовка всего игрового экрана.
void renderGame(Arduboy2 &arduboy, const Game &game) {
  arduboy.clear();
  arduboy.setTextWrap(false);
  if (game.state == GameState::Intro) {
    arduboy.drawBitmap(0, 0, intro_bitmap, 128, 64, WHITE);
    return;
  }

  if (game.state == GameState::Menu) {
    // База кадра — mainmenu_sound, остальные кадры — XOR-дельты поверх неё.
    arduboy.drawBitmap(0, 0, mainmenu_sound_bitmap, 128, 64, WHITE);
    const uint8_t *delta = nullptr;
    switch (game.menu.selectedIndex) {
    case 0:
      delta = menu_delta_play;
      break;
    case 1:
      delta = menu_delta_about;
      break;
    case 3:
      delta = menu_delta_main_exit;
      break;
    default:
      break; // Sound — сама база
    }
    if (delta)
      applyScreenDelta(arduboy.sBuffer, delta);
    return;
  }

  if (game.state == GameState::SoundMenu) {
    arduboy.drawBitmap(0, 0, soundmenu_off_bitmap, 128, 64, WHITE);
    const uint8_t *delta = nullptr;
    switch (game.soundMenu.selectedIndex) {
    case 0:
      delta = menu_delta_soundmenu_on;
      break;
    case 2:
      delta = menu_delta_soundmenu_exit;
      break;
    default:
      break; // Off — сама база
    }
    if (delta)
      applyScreenDelta(arduboy.sBuffer, delta);
    return;
  }

  if (game.state == GameState::About) {
    arduboy.setCursor(40, 12);
    arduboy.print(F("made for"));
    arduboy.setCursor(40, 28);
    arduboy.print(F("school21"));
    arduboy.setCursor(28, 44);
    arduboy.print(F("ardujam 2026"));
    return;
  }

  if (game.state == GameState::Shop) {
    drawShopHeader(arduboy, game);
    if (game.shop.choosingSlot) {
      arduboy.setCursor(0, 16);
      arduboy.print(F("EQUIP "));
      printAbility(arduboy, abilityFromUpgrade(static_cast<ActiveUpgradeId>(
                                game.shop.activeChoices[game.shop.selectedIndex])));
      arduboy.setCursor(0, 24);
      arduboy.print(F("A: "));
      printAbility(arduboy, game.player.slots[0].ability);
      arduboy.setCursor(0, 32);
      arduboy.print(F("B: "));
      printAbility(arduboy, game.player.slots[1].ability);
      arduboy.setCursor(0, 48);
      arduboy.print(F("A->A B->B"));
      arduboy.setCursor(0, 56);
      arduboy.print(F("< CANCEL"));
      return;
    }
    const bool active = game.shop.category == ShopCategory::Active;
    const uint8_t choice = active
        ? game.shop.activeChoices[game.shop.selectedIndex]
        : game.shop.passiveChoices[game.shop.selectedIndex];
    if (active)
      drawActiveCard(arduboy, static_cast<ActiveUpgradeId>(choice));
    else
      drawPassiveCard(arduboy, static_cast<PassiveId>(choice));

    const uint16_t price = active ? ACTIVE_PRICE : PASSIVE_PRICE;
    arduboy.setCursor(52, 40);
    arduboy.print(F("$"));
    arduboy.print(price);
    if (game.combat.playerScore < price) {
      arduboy.setCursor(43, 48);
      arduboy.print(F("X FUNDS"));
    } else if (!active && passiveAtCap(game, static_cast<PassiveId>(choice))) {
      arduboy.setCursor(49, 48);
      arduboy.print(F("X MAX"));
    }
    arduboy.setCursor(4, 56);
    arduboy.print(F("A BUY  <"));
    arduboy.print(game.shop.selectedIndex + 1);
    arduboy.print(F("/3>  B SKIP"));
    return;
  }

  if (game.state == GameState::GameOver || game.state == GameState::Win) {
    arduboy.setCursor(10, 8);
    arduboy.print(game.state == GameState::Win ? F("WIN: MEMORY CLEAN")
                                               : F("GAME OVER"));
    arduboy.setCursor(10, 24);
    arduboy.print(game.state == GameState::Win ? F("HELLO, WORLD!")
                                               : F("OUT OF MEMORY"));
    arduboy.setCursor(10, 40);
    arduboy.print(F("SCORE: "));
    arduboy.print(game.combat.playerScore);
    arduboy.setCursor(10, 56);
    arduboy.print(F("A/B TO MENU"));
    return;
  }

  if (game.state == GameState::StageCleared) {
    arduboy.setCursor(24, 5);
    arduboy.print(F("STAGE CLEARED"));
    arduboy.setCursor(10, 21);
    arduboy.print(F("SCORE: "));
    arduboy.print(game.combat.playerScore);

    // Бонус за время
    const uint16_t timeBonus =
        remainingSeconds(game.combat.stageTimer) * STAGE_TIME_BONUS_MULT;
    arduboy.setCursor(10, 32);
    arduboy.print(F("TIME BONUS: "));
    arduboy.print(timeBonus);

    arduboy.setCursor(20, 48);
    arduboy.print(F("A/B -> SHOP"));
    return;
  }

  if (game.state == GameState::Paused) {
    // Рисуем игровой мир позади (без обновления), затем накладываем надпись
    // PAUSED Сначала рисуем арену как в обычном режиме
    for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
      for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
        arduboy.drawPixel(x, y + HUD_HEIGHT);
      }
    }
    const uint8_t obstacleCount =
        getStageObstacleCount(game.combat.currentStage);
    for (uint8_t i = 0; i < obstacleCount; ++i) {
      const Obstacle obstacle = readObstacle(game.combat.currentStage, i);
      arduboy.fillRect(obstacle.x, obstacle.y + HUD_HEIGHT, obstacle.width,
                       obstacle.height, BLACK);
      arduboy.drawRect(obstacle.x, obstacle.y + HUD_HEIGHT, obstacle.width,
                       obstacle.height);
      arduboy.drawFastHLine(obstacle.x + 2, obstacle.y + HUD_HEIGHT + 2,
                            obstacle.width - 4);
    }
    // Враги
    for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
      drawEnemy(arduboy, game.combat.enemies[i]);
    }
    // Сферы очков
    for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
      drawScoreOrb(arduboy, game.combat.scoreOrbs[i]);
    }
    // Игрок (без мигания)
    drawPlayer(arduboy, game.player);
    // Пули
    for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
      drawProjectile(arduboy, game.combat.projectiles[i]);
    }
    // UI колонка
    drawUIColumn(arduboy, game);

    // Надпись PAUSED по центру экрана (128x64)
    // "PAUSED" = 6 символов * 6px = 36px ширина, центрируем: (128-36)/2 = 46
    arduboy.setCursor(46, 28);
    arduboy.print(F("PAUSED"));
    arduboy.setCursor(34, 38);
    arduboy.print(F("A+B TO RESUME"));
    return;
  }

  // Фоновая сетка для ориентации на арене
  for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
    for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
      arduboy.drawPixel(x, y + HUD_HEIGHT);
    }
  }
  // Препятствия (тёмные прямоугольники с обводкой), зависят от стейджа
  const uint8_t obstacleCount = getStageObstacleCount(game.combat.currentStage);
  for (uint8_t i = 0; i < obstacleCount; ++i) {
    const Obstacle obstacle = readObstacle(game.combat.currentStage, i);
    arduboy.fillRect(obstacle.x, obstacle.y + HUD_HEIGHT, obstacle.width,
                     obstacle.height, BLACK);
    arduboy.drawRect(obstacle.x, obstacle.y + HUD_HEIGHT, obstacle.width,
                     obstacle.height);
    arduboy.drawFastHLine(obstacle.x + 2, obstacle.y + HUD_HEIGHT + 2,
                          obstacle.width - 4);
  }
  // Предупреждающие квадраты перед спавном волны
  drawSpawnIndicators(arduboy, game.combat);
  // Рисуем новых врагов
  for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
    drawEnemy(arduboy, game.combat.enemies[i]);
  }
  // Рисуем сферы очков
  for (uint8_t i = 0; i < MAX_SCORE_ORBS; ++i) {
    drawScoreOrb(arduboy, game.combat.scoreOrbs[i]);
  }

  // Hidden phases affect only rendering, not collision.
  if (!isPlayerBlinking(game.player)) {
    drawPlayer(arduboy, game.player);
  }

  for (uint8_t i = 0; i < MAX_PROJECTILES; ++i) {
    drawProjectile(arduboy, game.combat.projectiles[i]);
  }

  // Правая колонка UI
  drawUIColumn(arduboy, game);
}

} // namespace gc
