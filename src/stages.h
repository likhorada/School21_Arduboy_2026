#pragma once

#include "config.h"
#include "enemies.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void *const *)(addr))
#endif
#endif

namespace gc {

// Описание одного врага в волне
struct WaveEnemy {
  EnemyType type; // Тип врага
  uint8_t hp; // HP (для Basic/Fast игнорируется, генерируется рандомно; для
              // Splitter - начальный HP)
  uint8_t x;  // Координата X в пикселях
  uint8_t y;  // Координата Y в пикселях
};

// Описание волны врагов
struct Wave {
  uint8_t enemyCount;       // Количество врагов в волне
  const WaveEnemy *enemies; // Указатель на массив врагов в PROGMEM
};

// Описание стейджа
struct Stage {
  uint8_t waveCount; // Количество волн в стейдже
  const Wave *waves; // Указатель на массив волн в PROGMEM
};

// ====== СТЕЙДЖ 1 ======

// Волна 1: 3 базовых врага
const WaveEnemy PROGMEM stage1_wave1[] = {{EnemyType::Basic, 3, 30, 20},
                                          {EnemyType::Basic, 3, 90, 20},
                                          {EnemyType::Basic, 3, 60, 40}};

// Волна 2: 2 базовых + 1 быстрый
const WaveEnemy PROGMEM stage1_wave2[] = {{EnemyType::Basic, 3, 20, 15},
                                          {EnemyType::Basic, 3, 100, 15},
                                          {EnemyType::Fast, 15, 60, 35}};

// Волна 3: 1 Splitter
const WaveEnemy PROGMEM stage1_wave3[] = {{EnemyType::Splitter, 16, 64, 28}};

// Массив волн для стейджа 1
const Wave PROGMEM stage1_waves[] = {
    {3, stage1_wave1}, {3, stage1_wave2}, {1, stage1_wave3}};

// Определение стейджа 1
const Stage PROGMEM stage1 = {3, // 3 волны
                              stage1_waves};

// ====== СТЕЙДЖ 2 ======

// Волна 1: три быстрых, разный разброс
const WaveEnemy PROGMEM stage2_wave1[] = {{EnemyType::Fast, 15, 20, 12},
                                          {EnemyType::Fast, 15, 108, 12},
                                          {EnemyType::Splitter, 8, 64, 42}};

// Волна 2: два крупных сплиттера
const WaveEnemy PROGMEM stage2_wave2[] = {{EnemyType::Splitter, 16, 40, 28},
                                          {EnemyType::Splitter, 16, 96, 28}};

// Волна 3: финал стейджа — быстрые и сплиттеры
const WaveEnemy PROGMEM stage2_wave3[] = {{EnemyType::Fast, 15, 24, 16},
                                          {EnemyType::Fast, 15, 104, 16},
                                          {EnemyType::Splitter, 16, 64, 40},
                                          {EnemyType::Basic, 3, 64, 50}};

// Массив волн для стейджа 2
const Wave PROGMEM stage2_waves[] = {
    {3, stage2_wave1}, {2, stage2_wave2}, {4, stage2_wave3}};

// Определение стейджа 2
const Stage PROGMEM stage2 = {3, // 3 волны
                              stage2_waves};

// ====== СТЕЙДЖ 3 ======
// Босс-стейдж: волн из данных нет. Раунд ведёт босс-«квадрат» LOV / I E / YOU
// (см. Boss в enemies.h): он появляется после предупреждения, ходит по кругу и
// раз в ~10 секунд испускает волну слабых прислужников. Убить его можно только
// автострельбой; контакт ранит игрока. Закрытие стейджа — смерть босса.
const Stage PROGMEM stage3 = {0, // Волн нет
                              nullptr};

// Массив всех стейджей
const Stage *const PROGMEM allStages[] = {
    &stage1, &stage2, &stage3};

constexpr uint8_t TOTAL_STAGES = 3;

// Функции для работы со стейджами
uint8_t getStageWaveCount(uint8_t stageIndex);
uint8_t getWaveEnemyCount(uint8_t stageIndex, uint8_t waveIndex);
WaveEnemy readWaveEnemy(uint8_t stageIndex, uint8_t waveIndex,
                        uint8_t enemyIndex);

} // namespace gc
