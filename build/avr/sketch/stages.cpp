#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/stages.cpp"
#include "stages.h"

namespace gc {

// Получить количество волн в стейдже
uint8_t getStageWaveCount(uint8_t stageIndex) {
    if (stageIndex >= TOTAL_STAGES) {
        return 0;
    }
    
    const Stage* stage = (const Stage*)pgm_read_ptr(&allStages[stageIndex]);
    return pgm_read_byte(&stage->waveCount);
}

// Получить количество врагов в волне
uint8_t getWaveEnemyCount(uint8_t stageIndex, uint8_t waveIndex) {
    if (stageIndex >= TOTAL_STAGES) {
        return 0;
    }
    
    const Stage* stage = (const Stage*)pgm_read_ptr(&allStages[stageIndex]);
    const Wave* waves = (const Wave*)pgm_read_ptr(&stage->waves);
    
    if (waveIndex >= pgm_read_byte(&stage->waveCount)) {
        return 0;
    }
    
    return pgm_read_byte(&waves[waveIndex].enemyCount);
}

// Прочитать данные врага из волны
WaveEnemy readWaveEnemy(uint8_t stageIndex, uint8_t waveIndex, uint8_t enemyIndex) {
    WaveEnemy result = {EnemyType::None, 0, 0, 0};
    
    if (stageIndex >= TOTAL_STAGES) {
        return result;
    }
    
    const Stage* stage = (const Stage*)pgm_read_ptr(&allStages[stageIndex]);
    const Wave* waves = (const Wave*)pgm_read_ptr(&stage->waves);
    
    if (waveIndex >= pgm_read_byte(&stage->waveCount)) {
        return result;
    }
    
    const Wave* wave = &waves[waveIndex];
    const uint8_t enemyCount = pgm_read_byte(&wave->enemyCount);
    
    if (enemyIndex >= enemyCount) {
        return result;
    }
    
    const WaveEnemy* enemies = (const WaveEnemy*)pgm_read_ptr(&wave->enemies);
    const WaveEnemy* enemy = &enemies[enemyIndex];
    
    result.type = (EnemyType)pgm_read_byte(&enemy->type);
    result.hp = pgm_read_byte(&enemy->hp);
    result.x = pgm_read_byte(&enemy->x);
    result.y = pgm_read_byte(&enemy->y);
    
    return result;
}

} // namespace gc
