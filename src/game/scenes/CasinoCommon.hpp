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

// A clay casino chip seen from above (edge spots, inner ring), or from the side when `side` is set.
inline void chip(Vector2 c, float r, Color base, bool side = false) {
    if (side) {
        DrawEllipse((int)c.x, (int)(c.y + r * 0.12f), r, r * 0.32f, darken(base, 0.45f));
        DrawRectangle((int)(c.x - r), (int)c.y, (int)(r * 2), (int)(r * 0.12f), darken(base, 0.3f));
        DrawEllipse((int)c.x, (int)c.y, r, r * 0.32f, base);
        for (int i = 0; i < 6; i++) {
            float a = i * PI / 3 + 0.3f;
            DrawEllipse((int)(c.x + std::cos(a) * r * 0.8f), (int)(c.y + std::sin(a) * r * 0.26f), r * 0.12f, r * 0.05f, alpha(WHITE, 0.85f));
        }
        DrawEllipse((int)c.x, (int)c.y, r * 0.55f, r * 0.17f, lighten(base, 0.12f));
        return;
    }
    DrawCircleV({c.x + 1, c.y + 2}, r, alpha(BLACK, 0.35f));
    DrawCircleV(c, r, base);
    for (int i = 0; i < 6; i++) DrawCircleSector(c, r, i * 60.0f + 10, i * 60.0f + 30, 4, alpha(WHITE, 0.9f));
    DrawCircleV(c, r * 0.72f, base);
    DrawRing(c, r * 0.6f, r * 0.66f, 0, 360, 24, alpha(WHITE, 0.6f));
    DrawCircleSector(c, r * 0.72f, 200, 260, 8, alpha(WHITE, 0.15f));
    ui::circleOutline(c, r, alpha(BLACK, 0.6f), 1.5f);
}

// Text bent along a circle (centre `c`, radius `rad`), centred on angle `midDeg`.
inline void arcText(const std::string& s, Vector2 c, float rad, float midDeg, float size, Color col) {
    float total = ui::measure(s, size, true);
    float ang = midDeg * DEG2RAD + total / rad / 2;  // text runs right-to-left in angle on the bottom of the circle
    for (char ch : s) {
        std::string one(1, ch);
        float w = ui::measure(one, size, true);
        float a = ang - w / 2 / rad;
        Vector2 p{c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
        ui::textRotated(one, p, size, a * RAD2DEG - 90, col, true);
        ang -= w / rad;
    }
}

// Title + back-to-casino + wallet. Returns true when the player wants to leave.
inline bool header(const char* title) {
    ui::titleBar(title, col::green);
    ui::coinCounter(VW - 20, 38, P().coins);
    return ui::backButton();
}

}  // namespace pg::casino
