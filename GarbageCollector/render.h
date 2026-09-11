#pragma once

#include "game.h"

class Arduboy2;

namespace gc {

// Draw a snapshot without modifying gameplay or owning an additional framebuffer.
void renderGame(Arduboy2& arduboy, const Game& game);

} // namespace gc
