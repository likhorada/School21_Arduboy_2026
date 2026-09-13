#include <Arduboy2.h>

#include "render.h"

#include "arena.h"

#include "assets/menu_screens.h"
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

// Спрайт игрока 7×7 пикселей для байтовой ветки рендера (renderPlayerFrame),
// хранится во flash (PROGMEM).
const uint8_t playerBitmap[] PROGMEM = {0x3e, 0x7f, 0x55, 0x5d,
                                        0x55, 0x7f, 0x3e};
static_assert(sizeof(playerBitmap) == PLAYER_SIZE && PLAYER_SIZE == 7,
              "Update the player bitmap when changing the hitbox");

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

// Рисуем горизонтальные runs сжатой карты стен.
void drawStageMap(Arduboy2 &arduboy, uint8_t stage) {
  for (uint8_t y = 0; y < ARENA_HEIGHT; ++y) {
    const uint8_t* row = stageWallRow(stage, y);
    if (row == nullptr) return;
    uint8_t runs = pgm_read_byte(row++);
    while (runs--) {
      const uint8_t x = pgm_read_byte(row++);
      const uint8_t length = pgm_read_byte(row++);
      arduboy.drawFastHLine(x, HUD_HEIGHT + y, length, WHITE);
    }
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
  const int16_t projectilePosX = projectileX(projectile);
  const int16_t projectilePosY = projectileY(projectile);
  const int8_t velocityX = projectileVelocityX(projectile);
  const int8_t velocityY = projectileVelocityY(projectile);
  for (uint8_t part = 0; part < 3; ++part) {
    const int16_t x = wrapCoordinate(
        projectilePosX - velocityX * part / 2, ARENA_WIDTH_FIXED);
    const int16_t y = wrapCoordinate(
        projectilePosY - velocityY * part / 2, ARENA_HEIGHT_FIXED);
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

  const int16_t screenX = orb.x;
  const int16_t screenY = HUD_HEIGHT + orb.y;

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
  case AbilityId::TimeWarp:
    arduboy.print(F("Warp"));
    break;
  case AbilityId::RecursiveCall:
    arduboy.print(F("Clone"));
    break;
  case AbilityId::Free:
    arduboy.print(F("free"));
    break;
  case AbilityId::BitShift:
    arduboy.print(F("Shift"));
    break;
  case AbilityId::MarkAndSweep:
    arduboy.print(F("Sweep"));
    break;
  case AbilityId::StackOverflow:
    arduboy.print(F("Stack"));
    break;
  case AbilityId::MemoryDump:
    arduboy.print(F("Dump"));
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

void drawPassiveCard(Arduboy2 &arduboy, const Game& game, PassiveId id) {
  arduboy.setCursor(28, 16);
  switch (id) {
  case PassiveId::CompilerOptimization:
    arduboy.print(F("COMPILER OPT"));
    break;
  case PassiveId::Overclock:
    arduboy.print(F("OVERCLOCK"));
    break;
  case PassiveId::OptimizedBuild:
    arduboy.print(F("OPT BUILD"));
    break;
  case PassiveId::MemoryFragmentation:
    arduboy.print(F("MEM FRAG"));
    break;
  case PassiveId::CollectionRange:
    arduboy.print(F("COL RANGE"));
    break;
  case PassiveId::RamCapacity:
    arduboy.print(F("RAM CAPACITY"));
    break;
  default:
    break;
  }
  const uint8_t level = passiveLevel(game.passives, id);
  arduboy.setCursor(100, 16);
  for (uint8_t i = 0; i < level; ++i) arduboy.print(F("+"));
}

void drawActiveCard(Arduboy2 &arduboy, AbilityId id) {
  arduboy.setCursor(34, 16);
  switch (id) {
  case AbilityId::TimeWarp:
    arduboy.print(F("TIME WARP"));
    break;
  case AbilityId::RecursiveCall:
    arduboy.print(F("RECURSIVE"));
    break;
  case AbilityId::Free:
    arduboy.print(F("free()"));
    break;
  case AbilityId::BitShift:
    arduboy.print(F("BIT SHIFT"));
    break;
  case AbilityId::MarkAndSweep:
    arduboy.print(F("SWEEP"));
    break;
  case AbilityId::StackOverflow:
    arduboy.print(F("STACK O/F"));
    break;
  case AbilityId::MemoryDump:
    arduboy.print(F("MEM DUMP"));
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

void drawCombatEffects(Arduboy2& arduboy, const Game& game) {
  const Combat& combat = game.combat;
  if (combat.puddleTicks) {
    for (int8_t offset = -MEMORY_DUMP_RADIUS; offset <= MEMORY_DUMP_RADIUS;
         offset += 4) {
      drawArenaPixel(arduboy, combat.puddleX + offset,
                     combat.puddleY - MEMORY_DUMP_RADIUS, WHITE);
      drawArenaPixel(arduboy, combat.puddleX + offset,
                     combat.puddleY + MEMORY_DUMP_RADIUS, WHITE);
      drawArenaPixel(arduboy, combat.puddleX - MEMORY_DUMP_RADIUS,
                     combat.puddleY + offset, WHITE);
      drawArenaPixel(arduboy, combat.puddleX + MEMORY_DUMP_RADIUS,
                     combat.puddleY + offset, WHITE);
    }
  }
  if (combat.recursiveTicks) {
    Player clone = game.player;
    clone.dashFrames = 0;
    clone.x = wrapCoordinate(clone.x - 8 * FIXED_ONE, ARENA_WIDTH_FIXED);
    drawPlayer(arduboy, clone);
    clone.x = wrapCoordinate(clone.x + 16 * FIXED_ONE, ARENA_WIDTH_FIXED);
    drawPlayer(arduboy, clone);
  }
  const uint8_t effectFrames = combat.visualEffect & 0x0F;
  if ((combat.visualEffect & 0xF0) == 0x10) {
    for (uint8_t y = effectFrames & 3; y < ARENA_HEIGHT; y += 4)
      for (uint8_t x = (y + effectFrames) & 3; x < ARENA_WIDTH; x += 4)
        drawArenaPixel(arduboy, x, y, WHITE);
  } else if ((combat.visualEffect & 0xF0) == 0x20) {
    const int16_t px = game.player.x / FIXED_ONE + PLAYER_SIZE / 2;
    const int16_t py = game.player.y / FIXED_ONE + PLAYER_SIZE / 2;
    const uint8_t radius = (9 - effectFrames) * 3;
    drawArenaPixel(arduboy, px + radius, py, WHITE);
    drawArenaPixel(arduboy, px - radius, py, WHITE);
    drawArenaPixel(arduboy, px, py + radius, WHITE);
    drawArenaPixel(arduboy, px, py - radius, WHITE);
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
    0x0e, 0x11, 0x11, 0x0e, 0x00, 0x12, 0x1f, 0x10,
    0x19, 0x15, 0x15, 0x12, 0x11, 0x15, 0x15, 0x0a,
    0x07, 0x04, 0x04, 0x1f, 0x17, 0x15, 0x15, 0x09,
    0x0e, 0x15, 0x15, 0x08, 0x01, 0x19, 0x05, 0x03,
    0x0a, 0x15, 0x15, 0x0a, 0x02, 0x15, 0x15, 0x0e,
    // 8  9  A  B  C  D  E  F
    0x1e, 0x05, 0x05, 0x1e, 0x1f, 0x15, 0x15, 0x0a,
    0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x0e,
    0x1f, 0x15, 0x15, 0x11, 0x1f, 0x05, 0x05, 0x01,
    // x (Basic)   d (Splitter)
    0x11, 0x0e, 0x0e, 0x11, 0x1c, 0x07, 0x04, 0x18};

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
    arduboy.setCursor(0, 0);
    arduboy.print(F("YOU ARE THE"));
    arduboy.setCursor(0, 8);
    arduboy.print(F("INDISPENSABLE"));
    arduboy.setCursor(0, 16);
    arduboy.print(F("GARBAGE COLLECTOR"));
    arduboy.setCursor(0, 24);
    arduboy.print(F("REMOVE USELESS DATA"));
    arduboy.setCursor(0, 32);
    arduboy.print(F("MAKE DEV PROUD"));
    arduboy.setCursor(0, 40);
    arduboy.print(F("YOU START WITH DASH"));
    arduboy.setCursor(0, 48);
    arduboy.print(F("MOVE DPAD A/B SKILLS"));
    arduboy.setCursor(0, 56);
    arduboy.print(F("B BACK TO MENU"));
    return;
  }

  if (game.state == GameState::Shop) {
    drawShopHeader(arduboy, game);
    if (game.shop.choosingSlot) {
      arduboy.setCursor(0, 16);
      arduboy.print(F("EQUIP "));
      printAbility(arduboy, static_cast<AbilityId>(
                                shopChoice(game, ShopCategory::Active,
                                           game.shop.selectedIndex)));
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
    const uint8_t choice = shopChoice(game, game.shop.category,
                                      game.shop.selectedIndex);
    if (active)
      drawActiveCard(arduboy, static_cast<AbilityId>(choice));
    else
      drawPassiveCard(arduboy, game, static_cast<PassiveId>(choice));

    const uint16_t price = active
        ? activePrice(static_cast<AbilityId>(choice))
        : passivePrice(game, static_cast<PassiveId>(choice));
    arduboy.setCursor(52, 40);
    arduboy.print(F("$"));
    arduboy.print(price);
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
    drawCombatEffects(arduboy, game);
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
  drawCombatEffects(arduboy, game);
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
