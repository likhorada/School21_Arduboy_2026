#include <Arduboy2.h>

#include "game.h"
#include "render.h"

Arduboy2 arduboy;
gc::Game game = {};

// Сохраняем штатную инициализацию, USB и режим восстановления Arduboy2.
void setup() {
    arduboy.begin();
    arduboy.setFrameDuration(gc::FRAME_DURATION_MS);
    arduboy.setTextWrap(false);
    game.soundEnabled = arduboy.audio.enabled();
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
        arduboy.pressed(A_BUTTON),
        arduboy.pressed(B_BUTTON),
    };
    const uint8_t previousSoundEnabled = game.soundEnabled;
    gc::updateGame(game, input);
    if (game.soundEnabled != previousSoundEnabled) {
        if (game.soundEnabled) arduboy.audio.on();
        else arduboy.audio.off();
        arduboy.audio.saveOnOff();
    }
    gc::renderGame(arduboy, game);
    arduboy.display();
}
