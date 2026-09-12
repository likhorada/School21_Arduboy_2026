#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/render.h"
#pragma once

#include "game.h"

class Arduboy2;

namespace gc {

// Рисуем текущее состояние, не меняя игру и не создавая второй экранный буфер.
void renderGame(Arduboy2& arduboy, const Game& game);

} // пространство имён gc
