// Shared bits for the casino tables.
#pragma once

#include "engine/Engine.hpp"
#include "engine/Ui.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg::casino {

inline void felt(float t) {
    DrawRectangleGradientV(0, 0, VW, VH, rgba(0x0f5a34ff), rgba(0x06301cff));
    DrawCircleGradient({VW / 2.0f, VH / 2.0f}, 700, alpha(rgba(0x1f8a4cff), 0.35f), alpha(rgba(0x1f8a4cff), 0));
    // subtle diamond pattern
    for (int y = 0; y < VH; y += 40)
        for (int x = (y / 40) % 2 * 20; x < VW; x += 40) DrawCircleV({(float)x, (float)y}, 1.5f, alpha(BLACK, 0.12f));
    (void)t;
}

// Title + back-to-casino + wallet. Returns true when the player wants to leave.
inline bool header(const char* title) {
    ui::titleBar(title, col::green);
    ui::coinCounter(VW - 20, 38, P().coins);
    return ui::backButton();
}

}  // namespace pg::casino
