// Illustrations for the mode tiles in the main menu and the casino lobby.
#pragma once

#include "engine/Core.hpp"

#include <string>

namespace pg::art {

// Draws the picture for a mode ("classic", "room", "beats", "horses", "stroop",
// "job", "fiets", "casino", "fnap", "roulette", "blackjack", "mines", "slots",
// "tower", "crash", "plinko") centred in `r`. `hover` 0..1 livens it up.
void modeArt(const std::string& id, Rectangle r, float t, float hover);

// Small helpers shared with the games themselves.
void rocket(Vector2 c, float s, float rotDeg, float flame, float t);  // Crash
void bike(Vector2 wheelBack, float s, float pedal, Color frame);       // Fietsen (omafiets, facing right)

}  // namespace pg::art
