// Factories for every scene in the game.
#pragma once

#include "engine/Engine.hpp"

#include <memory>

namespace pg {

std::unique_ptr<Scene> makeMenu();
std::unique_ptr<Scene> makeClassic();
std::unique_ptr<Scene> makePieterRoom();
std::unique_ptr<Scene> makeHorses();
std::unique_ptr<Scene> makeStroopwafel();
std::unique_ptr<Scene> makeBeats();
std::unique_ptr<Scene> makeCasino();
std::unique_ptr<Scene> makeRoulette();
std::unique_ptr<Scene> makeBlackjack();
std::unique_ptr<Scene> makeMines();
std::unique_ptr<Scene> makeSlots();
std::unique_ptr<Scene> makeShop();
std::unique_ptr<Scene> makeInventory();
std::unique_ptr<Scene> makeSettings();
std::unique_ptr<Scene> makeAdmin();  // secret: title screen code

// Intro: the animation itself (rendered offline to a video) and the player that
// shows assets/video/intro.mpg, then continues to the main menu.
std::unique_ptr<Scene> makeIntroAnimation();
float introDuration();
std::unique_ptr<Scene> makeIntroVideo();

}  // namespace pg
