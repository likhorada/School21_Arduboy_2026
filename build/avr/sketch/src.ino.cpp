#include <Arduino.h>
#line 1 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/src.ino"
#include <Arduboy2.h>

#include "game.h"
#include "render.h"

Arduboy2 arduboy;
gc::Game game = {};

// Сохраняем штатную инициализацию, USB и режим восстановления Arduboy2.
#line 10 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/src.ino"
void setup();
#line 17 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/src.ino"
void loop();
#line 10 "/home/Hermip/Documents/study/School 21/School21_Arduboy_2026/src/src.ino"
void setup() {
    arduboy.begin();
    arduboy.setFrameDuration(gc::FRAME_DURATION_MS);
    arduboy.setTextWrap(false);
}

// Опрашиваем кнопки раз за кадр: движение по удержанию, способности по новому нажатию.
void loop() {
    if (!arduboy.nextFrame()) {
        return;
    }
    arduboy.pollButtons();
    // true = 1, false = 0: разность даёт -1/0/+1, противоположные кнопки гасят друг друга.
    const gc::InputFrame input = {
        static_cast<int8_t>(arduboy.pressed(RIGHT_BUTTON) - arduboy.pressed(LEFT_BUTTON)),
        static_cast<int8_t>(arduboy.pressed(DOWN_BUTTON) - arduboy.pressed(UP_BUTTON)),
        arduboy.justPressed(A_BUTTON),
        arduboy.justPressed(B_BUTTON),
    };
    gc::updateGame(game, input);
    gc::renderGame(arduboy, game);
    arduboy.display();
}

