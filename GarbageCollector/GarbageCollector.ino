#include <Arduboy2.h>

#include "game.h"
#include "render.h"

Arduboy2 arduboy;
gc::Game game = {};

// Keep standard boot recovery and USB upload support provided by Arduboy2.
void setup() {
    arduboy.begin();
    arduboy.setFrameDuration(gc::FRAME_DURATION_MS);
    arduboy.setTextWrap(false);
}

// Poll once per accepted frame; one-shot actions receive edges, not held states.
void loop() {
    if (!arduboy.nextFrame()) {
        return;
    }
    arduboy.pollButtons();
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
