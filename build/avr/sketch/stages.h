#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/stages.h"
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
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void* const*)(addr))
#endif
#endif

namespace gc {

// Описание одного врага в волне
struct WaveEnemy {
    EnemyType type;     // Тип врага
    uint8_t hp;         // HP (для Basic/Fast игнорируется, генерируется рандомно; для Splitter - начальный HP)
    uint8_t x;          // Координата X в пикселях
    uint8_t y;          // Координата Y в пикселях
};

// Описание волны врагов
struct Wave {
    uint8_t enemyCount;         // Количество врагов в волне
    const WaveEnemy* enemies;   // Указатель на массив врагов в PROGMEM
};

// Описание стейджа
struct Stage {
    uint8_t waveCount;          // Количество волн в стейдже
    const Wave* waves;          // Указатель на массив волн в PROGMEM
};

// ====== СТЕЙДЖ 1 ======

// Волна 1: 3 базовых врага
const WaveEnemy PROGMEM stage1_wave1[] = {
    {EnemyType::Basic, 3, 30, 20},
    {EnemyType::Basic, 3, 90, 20},
    {EnemyType::Basic, 3, 60, 40}
};

// Волна 2: 2 базовых + 1 быстрый
const WaveEnemy PROGMEM stage1_wave2[] = {
    {EnemyType::Basic, 3, 20, 15},
    {EnemyType::Basic, 3, 100, 15},
    {EnemyType::Fast, 15, 60, 35}
};

// Волна 3: 1 Splitter
const WaveEnemy PROGMEM stage1_wave3[] = {
    {EnemyType::Splitter, 16, 64, 28}
};

// Волна 4: 2 Splitter + 1 быстрый
const WaveEnemy PROGMEM stage1_wave4[] = {
    {EnemyType::Splitter, 8, 40, 20},
    {EnemyType::Splitter, 8, 88, 20},
    {EnemyType::Fast, 15, 64, 40}
};

// Волна 5: финальная волна - микс из всех типов
const WaveEnemy PROGMEM stage1_wave5[] = {
    {EnemyType::Basic, 3, 25, 15},
    {EnemyType::Basic, 3, 103, 15},
    {EnemyType::Fast, 15, 64, 10},
    {EnemyType::Splitter, 16, 40, 40},
    {EnemyType::Splitter, 16, 88, 40}
};

// Массив волн для стейджа 1
const Wave PROGMEM stage1_waves[] = {
    {3, stage1_wave1},
    {3, stage1_wave2},
    {1, stage1_wave3},
    {3, stage1_wave4},
    {5, stage1_wave5}
};

// Определение стейджа 1
const Stage PROGMEM stage1 = {
    5,              // 5 волн
    stage1_waves
};

// ====== СТЕЙДЖ 2 ======

// Волна 1: три быстрых, разный разброс
const WaveEnemy PROGMEM stage2_wave1[] = {
    {EnemyType::Fast, 15, 20, 12},
    {EnemyType::Fast, 15, 108, 12},
    {EnemyType::Splitter, 8, 64, 42}
};

// Волна 2: два крупных сплиттера
const WaveEnemy PROGMEM stage2_wave2[] = {
    {EnemyType::Splitter, 16, 40, 28},
    {EnemyType::Splitter, 16, 96, 28}
};

// Волна 3: финал стейджа — быстрые и сплиттеры
const WaveEnemy PROGMEM stage2_wave3[] = {
    {EnemyType::Fast, 15, 24, 16},
    {EnemyType::Fast, 15, 104, 16},
    {EnemyType::Splitter, 16, 64, 40},
    {EnemyType::Basic, 3, 64, 50}
};

// Массив волн для стейджа 2
const Wave PROGMEM stage2_waves[] = {
    {3, stage2_wave1},
    {2, stage2_wave2},
    {4, stage2_wave3}
};

// Определение стейджа 2
const Stage PROGMEM stage2 = {
    3,              // 3 волны
    stage2_waves
};

// Массив всех стейджей
const Stage* const PROGMEM allStages[] = {
    &stage1,
    &stage2
    // Здесь можно добавить stage3 и т.д.
};

constexpr uint8_t TOTAL_STAGES = 2;

// Функции для работы со стейджами
uint8_t getStageWaveCount(uint8_t stageIndex);
uint8_t getWaveEnemyCount(uint8_t stageIndex, uint8_t waveIndex);
WaveEnemy readWaveEnemy(uint8_t stageIndex, uint8_t waveIndex, uint8_t enemyIndex);

} // namespace gc
