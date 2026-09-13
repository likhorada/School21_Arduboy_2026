#include <Arduboy2.h>
#include <ArduboyTones.h>
#include <ArduboyTonesPitches.h>

#include "game.h"
#include "render.h"

Arduboy2 arduboy;
gc::Game game = {};
ArduboyTones tones(arduboy.audio.enabled);

const uint16_t menuMusic[] PROGMEM = {
    NOTE_E5, 150, NOTE_B4, 150, NOTE_G4, 150, NOTE_E4, 150,
    NOTE_D5, 150, NOTE_A4, 150, NOTE_F4, 150, NOTE_D4, 150,
    NOTE_C5, 150, NOTE_G4, 150, NOTE_E4, 150, NOTE_C4, 150,
    NOTE_B4, 150, NOTE_D4, 150, NOTE_G4, 150, NOTE_B4, 150,
    TONES_REPEAT
};

const uint16_t coinSound[] PROGMEM = {
    NOTE_C6, 35, NOTE_E6, 55, TONES_END
};
const uint16_t enemyDeathSound[] PROGMEM = {
    NOTE_E4, 45, NOTE_C4, 70, TONES_END
};
const uint16_t playerHurtSound[] PROGMEM = {
    NOTE_E3, 60, NOTE_C3, 100, TONES_END
};

bool musicPlaying = false;

// USB не используется игрой; DOWN при старте оставляет вход в bootloader.
ARDUBOY_NO_USB

void updateAudio(uint8_t previousHp) {
    if (!game.soundEnabled) {
        if (tones.playing()) tones.noTone();
        musicPlaying = false;
        return;
    }
    if (game.player.hp < previousHp) {
        tones.tones(playerHurtSound);
        musicPlaying = false;
        return;
    }
    if (game.combat.audioEvents & gc::AUDIO_EVENT_ENEMY_DEATH) {
        tones.tones(enemyDeathSound);
        musicPlaying = false;
        return;
    }
    if (game.combat.audioEvents & gc::AUDIO_EVENT_COIN) {
        tones.tones(coinSound);
        musicPlaying = false;
        return;
    }

    const bool menu = game.state == gc::GameState::Menu ||
                      game.state == gc::GameState::About ||
                      game.state == gc::GameState::SoundMenu ||
                      game.state == gc::GameState::Shop;
    if (menu && !tones.playing()) {
        tones.tones(menuMusic);
        musicPlaying = true;
    } else if (!menu && musicPlaying) {
        tones.noTone();
        musicPlaying = false;
    }
}

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
    const uint8_t previousHp = game.player.hp;
    gc::updateGame(game, input);
    if (game.soundEnabled != previousSoundEnabled) {
        if (game.soundEnabled) arduboy.audio.on();
        else arduboy.audio.off();
        arduboy.audio.saveOnOff();
    }
    updateAudio(previousHp);
    gc::renderGame(arduboy, game);
    arduboy.display();
}
