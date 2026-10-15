// Shared bits for the casino tables.
#pragma once

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"
#include "engine/Ui.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg::casino {

inline void felt(float t) {
    DrawRectangleGradientV(0, 0, VW, VH, rgba(0x0c4a2bff), rgba(0x03180eff));
    // felt texture: a fine diamond pattern
    for (int y = 0; y < VH; y += 24)
        for (int x = (y / 24) % 2 * 12; x < VW; x += 24) DrawRectangleRec({(float)x, (float)y, 2, 2}, alpha(BLACK, 0.12f));
    // overhead lamps
    fx::beginAdd();
    fx::glow({VW * 0.3f, VH * 0.45f}, 620, alpha(rgba(0x3ddc84ff), 0.16f));
    fx::glow({VW * 0.7f, VH * 0.45f}, 620, alpha(rgba(0x3ddc84ff), 0.16f));
    fx::glow({VW * 0.5f, -80}, 700, alpha(rgba(0xffd27aff), 0.18f));
    fx::endAdd();
    fx::bokeh(t, 10, col::gold, 4);
}

// Title + back-to-casino + wallet. Returns true when the player wants to leave.
inline bool header(const char* title) {
    ui::titleBar(title, col::green);
    ui::coinCounter(VW - 20, 38, P().coins);
    return ui::backButton();
}

}  // namespace pg::casino
