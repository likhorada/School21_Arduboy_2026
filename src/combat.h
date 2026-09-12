#pragma once

#include "config.h"

namespace gc {

// Тестовый враг: неподвижный, можно убить выстрелами, возрождается.
// x/y: левый верхний угол в пикселях. HP и вспышка делят один байт вместо двух.
struct Dummy {
    uint8_t x;
    uint8_t y;
    uint8_t hpAndFlash;     // Биты 0-1: HP (0-3), биты 2-4: flash (0-7)
    uint8_t respawnFrames;
};

// Получить HP из упакованного поля
inline uint8_t getHp(const Dummy& d) {
    return d.hpAndFlash & 0x03;
}

// Получить вспышку попадания из упакованного поля
inline uint8_t getHitFlash(const Dummy& d) {
    return (d.hpAndFlash >> 2) & 0x07;
}

// Установить HP, сохранив flash
inline void setHp(Dummy& d, uint8_t hp) {
    d.hpAndFlash = (d.hpAndFlash & 0xFC) | (hp & 0x03);
}

// Установить flash, сохранив HP
inline void setHitFlash(Dummy& d, uint8_t flash) {
    d.hpAndFlash = (d.hpAndFlash & 0xE3) | ((flash & 0x07) << 2);
}

// Установить HP и flash одновременно
inline void setHpAndFlash(Dummy& d, uint8_t hp, uint8_t flash) {
    d.hpAndFlash = (hp & 0x03) | ((flash & 0x07) << 2);
}

// Пуля: летит по прямой. Координаты и скорости в единицах 1/16 пикселя.
// framesLeft == 0 означает пустой слот.
struct Projectile {
    int16_t x;
    int16_t y;
    int8_t velocityX;
    int8_t velocityY;
    uint8_t framesLeft;
};

// Боевая система: враги, пули, кулдаун автострельбы
struct Combat {
    Dummy dummies[DUMMY_COUNT];
    Projectile projectiles[MAX_PROJECTILES];
    uint8_t shotCooldown;
};

// Сброс: враги на стартовые позиции, пули очищены
void resetCombat(Combat& combat);

// Таймеры -> пули -> автострельба. playerX/Y: левый верхний угол в 1/16 пикселя.
// Новая пуля впервые двигается на следующем кадре, независимо от номера слота.
void updateCombat(Combat& combat, int16_t playerX, int16_t playerY);

static_assert(sizeof(Dummy) == 4, "Dummy must fit in four bytes");
#ifdef __AVR__
static_assert(sizeof(Combat) == DUMMY_COUNT * 4 + MAX_PROJECTILES * 7 + 1,
              "Unexpected AVR combat layout");
#endif

} // пространство имён gc
