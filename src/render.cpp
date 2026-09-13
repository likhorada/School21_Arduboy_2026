#include <Arduboy2.h>

#include "render.h"

#include "arena.h"
#include "assets/menu_screens.h"
#include "assets/stage_layouts.h"
#include "lzss.h"
#include "stages.h"

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

// Спрайт игрока 7×7 пикселей, хранится во flash (PROGMEM).
const uint8_t playerBitmap[] PROGMEM = {0x3e, 0x7f, 0x55, 0x5d,
                                        0x55, 0x7f, 0x3e};
static_assert(sizeof(playerBitmap) == PLAYER_SIZE && PLAYER_SIZE == 7,
              "Update the player bitmap when changing the hitbox");

// Рисуем пиксель арены с переходом через края.
// Сначала оборачиваем координаты, потом добавляем смещение HUD.
void drawArenaPixel(Arduboy2 &arduboy, int16_t x, int16_t y, uint8_t color) {
  arduboy.drawPixel(wrapCoordinate(x, ARENA_WIDTH),
                    HUD_HEIGHT + wrapCoordinate(y, ARENA_HEIGHT), color);
}

// Декодируем и рисуем экран меню. На AVR пишем прямо в кадровый буфер (окно
// LZSS); на хосте декодируем во временный буфер и рисуем как bitmap-маску.
void drawMenuScreen(Arduboy2 &arduboy, uint8_t screenCount) {
#if defined(__AVR__)
  lzssDecodeScreens(screenCount, arduboy.getBuffer(), menu_screens);
#else
  uint8_t frame[MENU_SCREEN_BYTES] = {};
  lzssDecodeScreens(screenCount, frame, menu_screens);
  arduboy.drawBitmap(0, 0, frame, 128, 64, WHITE);
#endif
}

// Рисуем белую разметку стен стейджа поверх арены (влево от колонки UI).
// Стейдж 1 — пустое поле. Маски — page-column битовые карты: на AVR копируем
// байты прямо в кадровый буфер (страница*128+колонка), на хосте — попиксельно.
void drawStageMap(Arduboy2 &arduboy, uint8_t stage) {
  const uint8_t *map;
  switch (stage) {
  case 1:
    map = stage2_map;
    break;
  case 2:
    map = stage3_map;
    break;
  default:
    return;
  }
#if defined(__AVR__)
  uint8_t *frame = arduboy.getBuffer();
  for (uint8_t page = 0; page < ARENA_HEIGHT / 8; ++page) {
    for (uint8_t x = 0; x < ARENA_WIDTH; ++x) {
      const uint8_t column = pgm_read_byte(&map[page * ARENA_WIDTH + x]);
      if (column) {
        frame[page * 128 + x] |= column;
      }
    }
  }
#else
  for (uint8_t y = 0; y < ARENA_HEIGHT; ++y) {
    for (uint8_t x = 0; x < ARENA_WIDTH; ++x) {
      const uint8_t byte = pgm_read_byte(&map[(y >> 3) * ARENA_WIDTH + x]);
      if (byte & (1u << (y & 7))) {
        arduboy.drawPixel(x, HUD_HEIGHT + y, WHITE);
      }
    }
  }
#endif
}

// Переносим пиксели через края по отдельности, не затрагивая HUD.
// Тёмный глаз показывает направление; при Dash остальной силуэт заполняется
// белым. На AVR весь спрайт пишется байтовыми колонками в кадровый буфер.
void drawPlayer(Arduboy2 &arduboy, const Player &player) {
#if defined(__AVR__)
  renderPlayerFrame(arduboy.getBuffer(), player);
#else
  const int16_t x = player.x / FIXED_ONE;
  const int16_t y = player.y / FIXED_ONE;
  const int8_t facingX = getFacingX(player);
  const int8_t facingY = getFacingY(player);
  for (uint8_t column = 0; column < PLAYER_SIZE; ++column) {
    const uint8_t bits = pgm_read_byte(&playerBitmap[column]);
    for (uint8_t row = 0; row < PLAYER_SIZE; ++row) {
      const bool eye = column == 3 + facingX * 2 && row == 3 + facingY * 2;
      const bool lit = !eye && ((bits & (1 << row)) || player.dashFrames > 0);
      drawArenaPixel(arduboy, x + column, y + row, lit ? WHITE : BLACK);
    }
  }
#endif
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

// Маленький пиксельный шрифт 4×5 для миниатюрных врагов живёт в gc (не в
// анонимном пространстве), чтобы тесты могли сверять перенос данных с эталоном.

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
// На AVR рисуем колонку-байтом в кадровый буфер, на хосте — пикселями.
void drawTinyGlyph(Arduboy2 &arduboy, char c, int16_t x, int16_t y) {
  const uint8_t *data = &tinyFont[tinyFontIndex(c) * 4];
#if defined(__AVR__)
  uint8_t *frame = arduboy.getBuffer();
  for (uint8_t col = 0; col < 4; ++col) {
    if (x + col >= 0 && x + col < ARENA_WIDTH) {
      fillFrameColumn(frame, x + col, y, pgm_read_byte(data + col), 5);
    }
  }
#else
  for (uint8_t row = 0; row < 5; ++row) {
    for (uint8_t col = 0; col < 4; ++col) {
      if ((pgm_read_byte(data + col) & (1 << row)) && x + col >= 0 &&
          x + col < ARENA_WIDTH && y + row >= 0 && y + row < ARENA_HEIGHT) {
        arduboy.drawPixel(x + col, HUD_HEIGHT + y + row, WHITE);
      }
    }
  }
#endif
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

// Маленький пиксельный шрифт 4×5 для миниатюрных врагов.
// Глифы: 0–9, A–F, 'x' (Basic), 'd' (Splitter). Итого 18 штук.
// Хранится по колонкам: каждый байт — одна колонка, бит r = строка r (бит 0 —
// верхняя строка). Такая ориентация совпадает с кадровым буфером Arduboy
// (колонка = байт, бит = строка страницы), что позволяет рисовать глиф
// байт-за-байтом, а не пиксель-за-пикселем. Определение вне анонимного
// пространства: тесты сверяют перенос данных с построчным эталоном.
const uint8_t tinyFont[] PROGMEM = {
    // 0  1  2  3  4  5  6  7
    0x0e, 0x11, 0x11, 0x0e, 0x10, 0x1f, 0x12, 0x00,
    0x12, 0x15, 0x15, 0x19, 0x0a, 0x15, 0x15, 0x11,
    0x1f, 0x04, 0x04, 0x07, 0x09, 0x15, 0x15, 0x17,
    0x08, 0x15, 0x15, 0x0e, 0x03, 0x05, 0x19, 0x01,
    0x0a, 0x15, 0x15, 0x0a, 0x0e, 0x15, 0x15, 0x02,
    // 8  9  A  B  C  D  E  F
    0x1e, 0x05, 0x05, 0x1e, 0x0a, 0x15, 0x15, 0x1f,
    0x11, 0x11, 0x11, 0x0e, 0x0e, 0x11, 0x11, 0x1f,
    0x11, 0x15, 0x15, 0x1f, 0x01, 0x05, 0x05, 0x1f,
    // x (Basic)   d (Splitter)
    0x11, 0x0e, 0x0e, 0x11, 0x18, 0x04, 0x07, 0x1c};

// Пишет одну колонку спрайта (байт columnBits, бит r = строка r) в плоский
// кадровый буфер 128×64 по индексу (y/8)*128 + x, бит (y&7). При высоте до 8
// строк задевает максимум два байта кадра. Столбцы вне экрана и строки ниже 64
// обрезаются; бит = 1 только ставит пиксель (буфер за кадр уже очищен).
void fillFrameColumn(uint8_t *frame, int16_t x, int16_t y, uint8_t columnBits,
                     uint8_t height) {
  if (x < 0 || x >= 128 || y >= 64 || height == 0 || columnBits == 0) {
    return;
  }
  columnBits &= static_cast<uint8_t>((1u << height) - 1);
  if (columnBits == 0) {
    return;
  }
  if (y < 0) {
    const int drop = -y;
    if (drop >= height) {
      return;
    }
    columnBits >>= drop;
    height = static_cast<uint8_t>(height - drop);
    y = 0;
  }
  if (y + height > 64) {
    height = static_cast<uint8_t>(64 - y);
    columnBits &= static_cast<uint8_t>((1u << height) - 1);
  }
  const uint8_t shift = y & 7;
  const uint8_t inFirst = 8 - shift;
  uint8_t *byte0 = frame + (y >> 3) * 128 + x;
  if (height <= inFirst) {
    byte0[0] |= static_cast<uint8_t>(columnBits << shift);
  } else {
    byte0[0] |= static_cast<uint8_t>(columnBits << shift);
    const uint8_t hi = static_cast<uint8_t>(columnBits >> inFirst) &
                       static_cast<uint8_t>((1u << (height - inFirst)) - 1);
    if (hi) {
      byte0[128] |= hi;
    }
  }
}

// Рисует спрайт игрока 7×7 в плоский кадровый буфер колонка-за-байтом.
// Колонки и строки переносятся через края арены как в pixel-версии. Глаз (тёмная
// точка) очищается по направлению взгляда; при Dash весь силуэт белый.
void renderPlayerFrame(uint8_t *frame, const Player &player) {
  const int16_t x = player.x / FIXED_ONE;
  const int16_t y = player.y / FIXED_ONE;
  const uint8_t eyeColumn = static_cast<uint8_t>(3 + getFacingX(player) * 2);
  const uint8_t eyeRow = static_cast<uint8_t>(3 + getFacingY(player) * 2);
  const bool dashing = player.dashFrames > 0;
  for (uint8_t column = 0; column < PLAYER_SIZE; ++column) {
    uint8_t base =
        dashing ? 0x7F : static_cast<uint8_t>(pgm_read_byte(&playerBitmap[column]));
    if (column == eyeColumn) {
      base &= static_cast<uint8_t>(~(1u << eyeRow));
    }
    if (base == 0) {
      continue;
    }
    const int16_t screenX = wrapCoordinate(x + column, ARENA_WIDTH);
    const uint8_t shift = y & 7;
    const uint8_t first = 8 - shift;
    const uint8_t page0 = static_cast<uint8_t>(y >> 3);
    const uint8_t lo = base & static_cast<uint8_t>((1u << first) - 1);
    // Строки r < first остаются в своей странице; остальные либо в следующей,
    // либо (y+7 >= 64) переносятся на верх арены — страница 0 с тем же битом.
    if (y <= static_cast<int16_t>(ARENA_HEIGHT - PLAYER_SIZE)) {
      frame[page0 * 128 + screenX] |= static_cast<uint8_t>(lo << shift);
      const uint8_t hi = static_cast<uint8_t>(base >> first);
      if (hi) {
        frame[(page0 + 1) * 128 + screenX] |= hi;
      }
    } else {
      frame[page0 * 128 + screenX] |= static_cast<uint8_t>(lo << shift);
      const uint8_t hi = static_cast<uint8_t>(base >> first);
      if (hi) {
        frame[screenX] |= hi;
      }
    }
  }
}

// Отрисовка всего игрового экрана.
void renderGame(Arduboy2 &arduboy, const Game &game) {
  arduboy.clear();
  arduboy.setTextWrap(false);
  if (game.state == GameState::Intro) {
    drawMenuScreen(arduboy, 1);
    return;
  }

  if (game.state == GameState::Menu) {
    // screens: play, about, sound, exit -> indexes 1..4 -> decode 2..5.
    drawMenuScreen(arduboy, 2 + game.menu.selectedIndex);
    return;
  }

  if (game.state == GameState::SoundMenu) {
    // screens: on, off, exit -> indexes 5..7 -> decode 6..8.
    drawMenuScreen(arduboy, 6 + game.soundMenu.selectedIndex);
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
    arduboy.setCursor(0, 0);
    arduboy.print(F("SHOP $"));
    arduboy.print(game.combat.playerScore);
    if (game.shop.choosingSlot) {
      arduboy.setCursor(0, 16);
      arduboy.print(F("Equip "));
      printAbility(
          arduboy,
          static_cast<AbilityId>(
              game.shop.activeChoices[game.shop.selectedIndex - 3] + 1));
      arduboy.setCursor(0, 24);
      arduboy.print(F("A: "));
      printAbility(arduboy, game.player.slots[0].ability);
      arduboy.setCursor(0, 32);
      arduboy.print(F("B: "));
      printAbility(arduboy, game.player.slots[1].ability);
      arduboy.setCursor(0, 48);
      arduboy.print(F("A/B REPLACE $"));
      arduboy.print(ACTIVE_PRICE);
      arduboy.setCursor(0, 56);
      arduboy.print(F("LEFT CANCEL"));
      return;
    }
    const bool activePage =
        game.shop.selectedIndex >= 3 && game.shop.selectedIndex < 6;
    arduboy.setCursor(0, 8);
    arduboy.print(activePage ? F("ACTIVE $") : F("PASSIVE $"));
    arduboy.print(activePage ? ACTIVE_PRICE : PASSIVE_PRICE);
    const bool bought =
        activePage ? game.shop.activeBought : game.shop.passiveBought;
    if (bought)
      arduboy.print(F(" BOUGHT"));
    for (uint8_t i = 0; i < 3; ++i) {
      arduboy.setCursor(0, 16 + i * 8);
      if (i + (activePage ? 3 : 0) == game.shop.selectedIndex)
        arduboy.print(F("> "));
      else
        arduboy.print(F("  "));
      if (activePage) {
        printAbility(arduboy,
                     static_cast<AbilityId>(game.shop.activeChoices[i] + 1));
        continue;
      }
      switch (static_cast<PassiveId>(game.shop.passiveChoices[i])) {
      case PassiveId::DamageUp:
        arduboy.print(F("DMG Up"));
        break;
      case PassiveId::MaxHpUp:
        arduboy.print(F("Max HP Up"));
        break;
      case PassiveId::MoveSpeedUp:
        arduboy.print(F("Speed Up"));
        break;
      default:
        arduboy.print(F("?"));
        break;
      }
    }
    arduboy.setCursor(0, 40);
    arduboy.print(game.shop.selectedIndex == 6 ? F("> Next / Skip")
                                               : F("  Next / Skip"));
    arduboy.setCursor(0, 48);
    arduboy.print(F("UP/DOWN: ALL 6 ITEMS"));
    arduboy.setCursor(0, 56);
    arduboy.print(F("A BUY / B NEXT"));
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
    // PAUSED Сначала рисуем арену как в обычном режиме. Фоновая сетка точек
    // нужна только на пустом первом стейдже; на 2 и 3 ориентацию дают стены.
    if (game.combat.currentStage == 0) {
      for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
        for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
          arduboy.drawPixel(x, y + HUD_HEIGHT);
        }
      }
    }
    drawStageMap(arduboy, game.combat.currentStage);
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

  // Фоновая сетка для ориентации только на пустом первом стейдже; на 2 и 3
  // ориентацию дают сами стены.
  if (game.combat.currentStage == 0) {
    for (uint8_t y = 4; y < ARENA_HEIGHT; y += 12) {
      for (uint8_t x = 4; x < ARENA_WIDTH; x += 12) {
        arduboy.drawPixel(x, y + HUD_HEIGHT);
      }
    }
  }
  // Стены стейджа (попиксельные маски из assets), сдвинутые под HUD
  drawStageMap(arduboy, game.combat.currentStage);
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
