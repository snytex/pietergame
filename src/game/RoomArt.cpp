#include "game/RoomArt.hpp"

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"

#include <rlgl.h>

namespace pg::room {
namespace {

// ------------------------------------------------------------------ layout
// One-point perspective: the back wall is a rectangle, side walls and floor
// run towards the vanishing point.
constexpr float kWallL = 170, kWallR = 1110, kFloorY = 470;
constexpr Vector2 kVP{640, 300};
constexpr Rectangle kGlass{872, 102, 206, 166};  // window glass
constexpr Vector2 kClock{770, 128};

float hash(float n) { return std::fmod(std::abs(std::sin(n * 12.9898f) * 43758.5453f), 1.0f); }
float smooth(float x) { x = clamp01(x); return x * x * (3 - 2 * x); }

// Triangles with per-vertex colour (both windings, so the order never matters).
void tri(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    rlBegin(RL_TRIANGLES);
    for (int w = 0; w < 2; w++) {
        rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex2f(a.x, a.y);
        if (w == 0) { rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex2f(b.x, b.y); rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex2f(c.x, c.y); }
        else { rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex2f(c.x, c.y); rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex2f(b.x, b.y); }
    }
    rlEnd();
}
// tl, tr, br, bl with a colour per corner
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color ctl, Color ctr, Color cbr, Color cbl) {
    tri(tl, tr, br, ctl, ctr, cbr);
    tri(tl, br, bl, ctl, cbr, cbl);
}
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c) { quad(tl, tr, br, bl, c, c, c, c); }
void quadV(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color top, Color bottom) { quad(tl, tr, br, bl, top, top, bottom, bottom); }

void rrect(Rectangle r, float round, Color c) { DrawRectangleRounded(r, round, 10, c); }
void rrectO(Rectangle r, float round, Color c, float o = 3) {
    DrawRectangleRounded(shrink(r, -o), round, 10, col::outline);
    DrawRectangleRounded(r, round, 10, c);
}

// sky gradient keyed on the local time
struct SkyKey { float h; Color top, bottom; };
const SkyKey kSky[] = {
    {0, rgba(0x080a23ff), rgba(0x281c50ff)},   {5.0f, rgba(0x080a23ff), rgba(0x281c50ff)},
    {6.7f, rgba(0x505aaaff), rgba(0xffa078ff)}, {9, rgba(0x5aaaffff), rgba(0xc8e6ffff)},
    {17, rgba(0x5aaaffff), rgba(0xc8e6ffff)},   {19.2f, rgba(0x3c328cff), rgba(0xff785aff)},
    {21, rgba(0x080a23ff), rgba(0x281c50ff)},  {24, rgba(0x080a23ff), rgba(0x281c50ff)},
};
void skyColors(float h, Color& top, Color& bottom) {
    for (int i = 0; i < 7; i++)
        if (h >= kSky[i].h && h <= kSky[i + 1].h) {
            float k = smooth((h - kSky[i].h) / (kSky[i + 1].h - kSky[i].h));
            top = mix(kSky[i].top, kSky[i + 1].top, k);
            bottom = mix(kSky[i].bottom, kSky[i + 1].bottom, k);
            return;
        }
    top = kSky[0].top;
    bottom = kSky[0].bottom;
}
float golden(float h) {  // warm light around sunrise and sunset
    return std::max(std::exp(-std::pow((h - 7.0f) / 1.0f, 2.0f)), std::exp(-std::pow((h - 19.0f) / 1.1f, 2.0f)));
}
Color sunColor(float h) { return mix(rgba(0xfff0d8ff), rgba(0xffa060ff), golden(h)); }

Color hue(float deg, float s = 0.8f, float v = 1) { return ColorFromHSV(std::fmod(deg + 3600, 360.0f), s, v); }

// ------------------------------------------------------------------ window pieces

void skyline(const Env& e, float night) {
    float base = kGlass.y + kGlass.height;
    Color sil = mix(mix(rgba(0x5a74b0ff), rgba(0x7a4a7aff), golden(e.hour)), rgba(0x120c26ff), night);
    Color far = mix(alpha(lighten(sil, 0.25f), 0.5f), alpha(rgba(0x1c1638ff), 0.9f), night);
    // far row of rooftops
    for (int i = 0; i < 12; i++) {
        float x = kGlass.x - 6 + i * 19.0f, h = 16 + hash(i + 40.0f) * 14;
        DrawRectangleRec({x, base - h, 20, h}, far);
    }
    // church spire
    float cx = kGlass.x + 52;
    DrawRectangleRec({cx - 7, base - 52, 14, 52}, sil);
    DrawTriangle({cx, base - 84}, {cx - 8, base - 52}, {cx + 8, base - 52}, sil);
    // canal houses with stepped and bell gables
    float x = kGlass.x - 4;
    for (int i = 0; x < kGlass.x + kGlass.width + 10; i++) {
        float w = 22 + hash(i * 3.1f) * 14, h = 22 + hash(i * 7.7f) * 18;
        if (std::abs(x + w / 2 - (kGlass.x + 150)) < 24) { x += w + 1; continue; }  // room for the windmill
        DrawRectangleRec({x, base - h, w, h}, sil);
        if (i % 2 == 0)
            for (int s = 1; s <= 3; s++) DrawRectangleRec({x + s * w / 8, base - h - s * 5, w - s * w / 4, 5}, sil);
        else {
            DrawCircleV({x + w / 2, base - h}, w * 0.32f, sil);
            DrawRectangleRec({x + w / 2 - 2, base - h - w * 0.45f, 4, 6}, sil);
        }
        if (night > 0.3f)
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 2; c++) {
                    float lit = hash(i * 13.0f + r * 5 + c);
                    if (lit < 0.45f) continue;
                    float flick = 0.75f + 0.25f * std::sin(e.t * (0.5f + lit) + i);
                    DrawRectangleRec({x + 5 + c * (w - 14), base - h + 8 + r * 11, 4, 5}, alpha(rgba(0xffd27aff), night * flick));
                }
        x += w + 1;
    }
    // windmill (the sails really turn)
    Vector2 hub{kGlass.x + 150, base - 44};
    quad({hub.x - 6, hub.y}, {hub.x + 6, hub.y}, {hub.x + 11, base}, {hub.x - 11, base}, sil);
    DrawCircleV(hub, 6, sil);
    for (int b = 0; b < 4; b++) {
        float a = e.t * 0.6f + b * PI / 2;
        Vector2 d = fromAngle(a), n{-d.y, d.x};
        Vector2 p0 = hub + d * 4, p1 = hub + d * 34;
        DrawLineEx(p0, p1, 2, sil);
        quad(p0 + d * 8, p1, p1 + n * 7, p0 + d * 8 + n * 7, alpha(sil, 0.8f));
    }
}

void curtain(bool left, const Env& e) {
    // tied back halfway down; folds shaded across their width
    float x0 = left ? 812 : 1078, x1 = left ? 878 : 1144;
    float top = 72, tieY = 236, bottom = 336;
    float sway = std::sin(e.t * 0.7f + (left ? 0 : 1.3f)) * 2.5f;
    auto edges = [&](float y, float& a, float& b) {
        float pinch = y < tieY ? smooth((y - top) / (tieY - top)) : 1 - smooth((y - tieY) / (bottom - tieY)) * 0.55f;
        float w = (x1 - x0) * (1 - pinch * 0.55f);
        float s = sway * clamp01((y - top) / 200);
        if (left) { a = x0; b = x0 + w + s; }
        else { b = x1; a = x1 - w + s; }
    };
    Color base = rgba(0x6b2fb0ff);
    const int folds = 5, rows = 24;
    for (int r = 0; r < rows; r++) {
        float ya = top + (bottom - top) * r / rows, yb = top + (bottom - top) * (r + 1) / rows;
        float a0, b0, a1, b1;
        edges(ya, a0, b0);
        edges(yb, a1, b1);
        for (int f = 0; f < folds; f++) {
            float u0 = (float)f / folds, u1 = (float)(f + 1) / folds;
            auto shade = [&](float u) { return mix(darken(base, 0.45f), lighten(base, 0.12f), 0.5f + 0.5f * std::sin(u * folds * 2 * PI)); };
            quad({lerpf(a0, b0, u0), ya}, {lerpf(a0, b0, u1), ya}, {lerpf(a1, b1, u1), yb}, {lerpf(a1, b1, u0), yb}, shade(u0), shade(u1),
                 shade(u1), shade(u0));
        }
    }
    // tie-back band
    float a, b;
    edges(tieY, a, b);
    rrectO({a - 2, tieY - 6, b - a + 4, 12}, 0.5f, col::gold, 2);
}

// ------------------------------------------------------------------ screen

void screen(Rectangle r, const Env& e) {
    E().beginClip(r);
    if (e.lights < 0.5f) {
        // screensaver: a dim bouncing logo
        DrawRectangleRec(r, rgba(0x05040aff));
        float bx = std::abs(std::fmod(e.t * 23, (r.width - 70) * 2) - (r.width - 70));
        float by = std::abs(std::fmod(e.t * 17, (r.height - 20) * 2) - (r.height - 20));
        ui::text("PieterOS", r.x + bx + 35, r.y + by + 10, 14, alpha(hue(e.t * 30), 0.5f), ui::Center, true);
    } else {
        // a little Classic match plays on the PC; PLAY makes it go wild
        float g = e.gaming;
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, mix(rgba(0x140c2aff), hue(e.t * 90, 0.7f, 0.35f), g),
                               rgba(0x0a0616ff));
        for (int i = 1; i < 8; i++) DrawLineEx({r.x + i * r.width / 8, r.y}, {r.x + i * r.width / 8, r.y + r.height}, 1, alpha(col::violet, 0.12f));
        for (int i = 1; i < 5; i++) DrawLineEx({r.x, r.y + i * r.height / 5}, {r.x + r.width, r.y + i * r.height / 5}, 1, alpha(col::violet, 0.12f));
        float sp = 1 + g * 1.8f;
        Vector2 c = center(r);
        Vector2 me = c + Vector2{std::sin(e.t * 1.3f * sp) * r.width * 0.3f, std::cos(e.t * 1.7f * sp) * r.height * 0.28f};
        int n = 6 + (int)(g * 8);
        for (int i = 0; i < n; i++) {
            float a = e.t * (0.6f + hash(i) * 0.8f) * sp + i;
            Vector2 p = c + Vector2{std::sin(a * 1.1f + i) * r.width * 0.45f, std::cos(a * 0.9f + i * 2) * r.height * 0.42f};
            Color ec = i % 3 == 0 ? col::red : (i % 3 == 1 ? col::orange : col::cyan);
            DrawCircleV(p, 4, ec);
            DrawCircleV(p + fromAngle(a * 3, 9), 1.8f, alpha(WHITE, 0.8f));
        }
        DrawCircleV(me, 6, col::pink);
        DrawCircleV(me, 3, WHITE);
        ui::text(g > 0.05f ? "WAVE 15" : "WAVE 7", r.x + 8, r.y + 10, 10, alpha(col::text, 0.8f), ui::Left, true);
        ui::text(TextFormat("%06d", (int)(e.t * 137) % 1000000), r.x + r.width - 8, r.y + 10, 10, col::gold, ui::Right, true);
        if (g > 0.5f && std::fmod(e.t, 0.6f) < 0.3f) ui::text("CLUTCH!", c.x, c.y - 20, 22, hue(e.t * 200), ui::Center, true);
    }
    // glass glare
    quad({r.x + r.width * 0.55f, r.y}, {r.x + r.width * 0.75f, r.y}, {r.x + r.width * 0.45f, r.y + r.height}, {r.x + r.width * 0.25f, r.y + r.height},
         alpha(WHITE, 0.07f));
    E().endClip();
}

void fan(Vector2 c, float r, const Env& e, float phase) {
    DrawCircleV(c, r + 3, rgba(0x0c0a12ff));
    float spin = e.t * (5 + e.gaming * 14) + phase;
    for (int b = 0; b < 7; b++) {
        float a = spin + b * 2 * PI / 7;
        DrawCircleSector(c, r - 2, a * RAD2DEG, a * RAD2DEG + 32, 6, rgba(0x2a2534ff));
    }
    DrawRing(c, r - 2, r + 1.5f, 0, 360, 32, hue(e.t * 60 + phase * 40, 0.85f, e.lights > 0.5f ? 1 : 0.5f));
    DrawCircleV(c, r * 0.3f, rgba(0x15121cff));
}

}  // namespace

// ====================================================================== public

float daylight(float h) {
    if (h < 5.5f || h > 21) return 0;
    if (h < 8) return smooth((h - 5.5f) / 2.5f);
    if (h < 18) return 1;
    return 1 - smooth((h - 18) / 3);
}

Color ambient(const Env& e) {
    float d = daylight(e.hour);
    Color c = mix(rgba(0x6c6aa8ff), WHITE, d);
    c = mix(c, rgba(0xffd6c0ff), golden(e.hour) * 0.6f);
    return mix(rgba(0x1c1c46ff), c, e.lights);
}

void drawWalls(const Env& e) {
    // floor: warm planks converging on the vanishing point
    DrawRectangleGradientV(0, (int)kFloorY, VW, VH - (int)kFloorY, rgba(0x3a2216ff), rgba(0x5e3a24ff));
    float rows[] = {kFloorY, 486, 506, 532, 566, 612, 672, 750};
    float prevX = 0;
    for (int i = -14; i <= 32; i++) {
        float xb = kWallL - 40 + i * 52.0f;
        auto at = [&](float y) { return kVP.x + (xb - kVP.x) * (y - kVP.y) / (kFloorY - kVP.y); };
        // per-plank tint variation
        float v = hash(i * 1.7f);
        quad({at(kFloorY), kFloorY}, {at(kFloorY) + 52, kFloorY}, {at(VH) + 52 * (VH - kVP.y) / (kFloorY - kVP.y), (float)VH}, {at(VH), (float)VH},
             alpha(v > 0.5f ? WHITE : BLACK, 0.03f + 0.04f * std::abs(v - 0.5f)));
        DrawLineEx({at(kFloorY), kFloorY}, {at(VH), (float)VH}, 2, alpha(rgba(0x1e100aff), 0.55f));
        for (int r = 0; r < 7; r++)
            if ((i * 3 + r * 2) % 5 == 0) {
                float y = rows[r + 1] - 1;
                DrawLineEx({at(y), y}, {at(y) + 52 * (y - kVP.y) / (kFloorY - kVP.y), y}, 1.5f, alpha(rgba(0x1e100aff), 0.5f));
            }
        prevX = xb;
    }
    (void)prevX;

    // back wall + wallpaper lattice
    DrawRectangleGradientV((int)kWallL, 0, (int)(kWallR - kWallL), (int)kFloorY, rgba(0x3d2676ff), rgba(0x2b1a56ff));
    for (float y = 24; y < 370; y += 40)
        for (float x = kWallL + 20 + (int(y / 40) % 2) * 24; x < kWallR; x += 48) {
            DrawPoly({x, y}, 4, 5, 0, alpha(WHITE, 0.035f));
        }
    // wainscoting
    DrawRectangle((int)kWallL, 376, (int)(kWallR - kWallL), (int)(kFloorY - 376), rgba(0x24164aff));
    DrawRectangle((int)kWallL, 372, (int)(kWallR - kWallL), 6, rgba(0x4a2f86ff));
    for (float x = kWallL + 14; x < kWallR - 40; x += 78) DrawRectangleLinesEx({x, 390, 64, 62}, 2, alpha(rgba(0x4a2f86ff), 0.5f));
    DrawRectangle((int)kWallL, (int)kFloorY - 10, (int)(kWallR - kWallL), 10, rgba(0x150c2aff));

    // side walls (darker, they face away from the window)
    auto side = [&](bool left) {
        float xe = left ? 0.0f : (float)VW, xw = left ? kWallL : kWallR;
        float topE = kVP.y + (0 - kVP.y) * (xe - kVP.x) / (xw - kVP.x);
        float botE = kVP.y + (kFloorY - kVP.y) * (xe - kVP.x) / (xw - kVP.x);
        Vector2 a{xw, 0}, b{xe, topE}, c{xe, botE}, d{xw, kFloorY};
        Color top = rgba(0x2a1a56ff), bot = rgba(0x1c1140ff);
        if (left) quad(b, a, d, c, top, top, bot, bot);
        else quad(a, b, c, d, top, top, bot, bot);
        // baseboard along the wall
        float k = 10;
        if (left) quad({xw, kFloorY - k}, {xw, kFloorY}, c, {xe, c.y - k * 1.3f}, rgba(0x150c2aff));
        else quad({xw, kFloorY - k}, {xe, c.y - k * 1.3f}, c, {xw, kFloorY}, rgba(0x150c2aff));
    };
    side(true);
    side(false);
    // corner shading
    DrawRectangleGradientH((int)kWallL, 0, 50, (int)kFloorY, alpha(BLACK, 0.3f), alpha(BLACK, 0));
    DrawRectangleGradientH((int)kWallR - 50, 0, 50, (int)kFloorY, alpha(BLACK, 0), alpha(BLACK, 0.3f));
    DrawRectangleGradientV((int)kWallL, (int)kFloorY - 24, (int)(kWallR - kWallL), 24, alpha(BLACK, 0), alpha(BLACK, 0.25f));
    DrawRectangleGradientV(0, (int)kFloorY, VW, 50, alpha(BLACK, 0.3f), alpha(BLACK, 0));

    // crown moulding + LED strip
    DrawRectangle((int)kWallL, 0, (int)(kWallR - kWallL), 12, rgba(0x4a2f86ff));
    for (float x = kWallL; x < kWallR; x += 6) DrawRectangleRec({x, 12, 6, 3}, hue(e.t * 40 + x * 0.35f, 0.75f, e.horror ? 0.06f : 0.4f + 0.6f * e.lights));
}

void drawWindow(const Env& e) {
    float night = 1 - daylight(e.hour);
    Color top, bottom;
    skyColors(e.hour, top, bottom);
    rrect({kGlass.x - 16, kGlass.y - 16, kGlass.width + 32, kGlass.height + 32}, 0.04f, rgba(0x150c2aff));
    E().beginClip(kGlass);
    DrawRectangleGradientV((int)kGlass.x, (int)kGlass.y, (int)kGlass.width, (int)kGlass.height, top, bottom);
    // stars + moon
    if (night > 0.05f) {
        for (int i = 0; i < 40; i++) {
            Vector2 p{kGlass.x + hash(i + 0.5f) * kGlass.width, kGlass.y + hash(i + 9.1f) * kGlass.height * 0.7f};
            float tw = 0.5f + 0.5f * std::sin(e.t * (1 + hash(i) * 3) + i);
            DrawCircleV(p, 0.8f + hash(i + 3.3f) * 1.2f, alpha(WHITE, night * (0.3f + 0.7f * tw)));
        }
        Vector2 m{kGlass.x + 158, kGlass.y + 36};
        fx::beginAdd();
        fx::glow(m, 60, alpha(rgba(0x9fb4ffff), 0.35f * night));
        fx::endAdd();
        DrawCircleV(m, 15, alpha(rgba(0xfff6d8ff), night));
        DrawCircleV({m.x + 7, m.y - 4}, 13, alpha(top, night));
    }
    // sun travels across the window during the day
    float d = daylight(e.hour);
    if (d > 0.02f) {
        float u = clamp01((e.hour - 6) / 14);
        Vector2 s{kGlass.x + 20 + u * 170, kGlass.y + 140 - std::sin(u * PI) * 105};
        fx::beginAdd();
        fx::glow(s, 90, alpha(sunColor(e.hour), 0.6f * d));
        fx::endAdd();
        DrawCircleV(s, 16, alpha(mix(rgba(0xfff6c8ff), rgba(0xffb070ff), golden(e.hour)), d));
    }
    // clouds
    Color cloud = mix(mix(WHITE, rgba(0xffb8c8ff), golden(e.hour)), rgba(0x3a3460ff), night);
    for (int i = 0; i < 4; i++) {
        float x = kGlass.x - 60 + std::fmod(e.t * (4 + i * 1.5f) + i * 83, kGlass.width + 120);
        float y = kGlass.y + 22 + i * 19;
        float a = 0.85f - night * 0.4f;
        DrawEllipse((int)x, (int)y, 26, 8, alpha(cloud, a));
        DrawEllipse((int)x + 14, (int)y - 5, 16, 9, alpha(cloud, a));
        DrawEllipse((int)x - 12, (int)y - 2, 12, 7, alpha(cloud, a));
    }
    skyline(e, night);
    if (e.outside) e.outside();
    // glass reflection
    quad({kGlass.x + 30, kGlass.y}, {kGlass.x + 70, kGlass.y}, {kGlass.x + 10, kGlass.y + kGlass.height}, {kGlass.x - 30, kGlass.y + kGlass.height},
         alpha(WHITE, 0.06f));
    E().endClip();
    // frame + mullions
    Color frame = rgba(0xe9e2f2ff);
    DrawRectangleLinesEx(shrink(kGlass, -8), 8, frame);
    DrawRectangleRec({kGlass.x + kGlass.width / 2 - 4, kGlass.y, 8, kGlass.height}, frame);
    DrawRectangleRec({kGlass.x, kGlass.y + kGlass.height / 2 - 4, kGlass.width, 8}, frame);
    DrawRectangleLinesEx(shrink(kGlass, -10), 2, darken(frame, 0.4f));
    // sill + plants
    rrectO({kGlass.x - 22, kGlass.y + kGlass.height + 8, kGlass.width + 44, 12}, 0.3f, frame, 2);
    Vector2 pot{kGlass.x + 18, kGlass.y + kGlass.height + 6};
    quad({pot.x - 13, pot.y - 20}, {pot.x + 13, pot.y - 20}, {pot.x + 9, pot.y}, {pot.x - 9, pot.y}, rgba(0xc8643cff));
    for (int i = 0; i < 6; i++) {
        float a = -PI / 2 + (i - 2.5f) * 0.42f + std::sin(e.t * 1.2f + i) * 0.04f;
        Vector2 tip = Vector2{pot.x, pot.y - 20} + fromAngle(a, 26 + (i % 2) * 8);
        DrawLineEx({pot.x, pot.y - 20}, tip, 6, rgba(0x3a9a5aff));
        DrawCircleV(tip, 4, rgba(0x4fbf72ff));
    }
    Vector2 cac{kGlass.x + kGlass.width - 18, kGlass.y + kGlass.height + 6};
    rrect({cac.x - 9, cac.y - 14, 18, 14}, 0.3f, rgba(0xe8e2f0ff));
    rrect({cac.x - 6, cac.y - 38, 12, 26}, 1, rgba(0x3a9a5aff));
    rrect({cac.x + 4, cac.y - 30, 9, 6}, 1, rgba(0x3a9a5aff));
    DrawCircleV({cac.x, cac.y - 39}, 3.5f, col::pink);

    // curtain rod + curtains
    DrawRectangleRec({800, 68, 356, 6}, rgba(0x15121cff));
    DrawCircleV({800, 71}, 7, col::gold);
    DrawCircleV({1156, 71}, 7, col::gold);
    curtain(true, e);
    curtain(false, e);
}

void drawWallDecor(const Env& e) {
    // --- neon sign on an acrylic board
    Rectangle board{318, 84, 254, 112};
    rrect({board.x + 6, board.y + 8, board.width, board.height}, 0.1f, alpha(BLACK, 0.3f));
    rrect(board, 0.1f, alpha(rgba(0x0c0814ff), 0.85f));
    DrawRectangleRoundedLinesEx(board, 0.1f, 10, 1.5f, alpha(WHITE, 0.15f));
    for (Vector2 s : {Vector2{board.x + 10, board.y + 10}, {board.x + board.width - 10, board.y + 10}, {board.x + 10, board.y + board.height - 10},
                      {board.x + board.width - 10, board.y + board.height - 10}})
        DrawCircleV(s, 3, rgba(0x8a8498ff));
    bool flick = std::fmod(e.t, 5.3f) < 0.12f && std::fmod(e.t * 23, 1.0f) < 0.5f;
    bool on = e.lights > 0.5f && !flick;
    Color tube = on ? rgba(0xffd0d8ff) : rgba(0x5a2430ff), glowC = rgba(0xff3d6aff);
    const char* lines[] = {"BEC IT", "ROOM"};
    for (int i = 0; i < 2; i++) {
        float y = board.y + 34 + i * 46;
        if (on) {
            fx::beginAdd();
            for (int k = 0; k < 10; k++) {
                Vector2 o = fromAngle(k * PI / 5, 4);
                ui::text(lines[i], board.x + board.width / 2 + o.x, y + o.y, 42, alpha(glowC, 0.22f), ui::Center, true);
            }
            fx::endAdd();
        }
        ui::text(lines[i], board.x + board.width / 2, y, 42, on ? glowC : rgba(0x3a1420ff), ui::Center, true);
        ui::text(lines[i], board.x + board.width / 2, y, 40, tube, ui::Center, true);
    }

    // --- shelf with trophies and a tiny horse
    float sy = 252;
    rrect({322, sy + 10, 240, 10}, 0.3f, alpha(BLACK, 0.25f));
    rrectO({318, sy, 240, 12}, 0.3f, rgba(0x8a5a3aff), 2);
    DrawRectangleRec({340, sy + 12, 6, 14}, rgba(0x15121cff));
    DrawRectangleRec({530, sy + 12, 6, 14}, rgba(0x15121cff));
    // trophy
    Vector2 tr{352, sy};
    rrectO({tr.x - 13, tr.y - 8, 26, 8}, 0.2f, rgba(0x6a4a2aff), 1.5f);
    DrawRectangleRec({tr.x - 3, tr.y - 18, 6, 10}, col::goldDark);
    DrawCircleSector({tr.x, tr.y - 34}, 15, 0, 180, 16, col::outline);
    DrawCircleSector({tr.x, tr.y - 34}, 13, 0, 180, 16, col::gold);
    DrawRectangleRec({tr.x - 15, tr.y - 40, 30, 7}, col::gold);
    DrawRing({tr.x - 15, tr.y - 32}, 4, 6.5f, 90, 270, 10, col::goldDark);
    DrawRing({tr.x + 15, tr.y - 32}, 4, 6.5f, -90, 90, 10, col::goldDark);
    ui::text("#1", tr.x, tr.y - 30, 11, rgba(0x7a4a00ff), ui::Center, true);
    // game boxes
    Color boxes[] = {col::red, col::cyan, col::green, col::pink};
    for (int i = 0; i < 4; i++) {
        float lean = i == 3 ? 6.0f : 0.0f;
        Rectangle b{386 + i * 13.0f, sy - 44 + lean, 11, 44 - lean};
        DrawRectangleRec(b, boxes[i]);
        DrawRectangleRec({b.x, b.y + 6, b.width, 5}, alpha(WHITE, 0.5f));
        DrawRectangleLinesEx(b, 1, col::outline);
    }
    // little horse figurine, bobbing like the gamer horse
    art::horse({488, sy}, 0.36f, 0.25f + std::sin(e.t * 2) * 0.05f, rgba(0xf4ecffff), col::pink, col::violet, "none");
    // mini boba
    art::boba({540, sy - 16}, 26);

    // --- clock with the real time
    std::time_t now = std::time(nullptr);
    std::tm lt = *std::localtime(&now);
    float sec = lt.tm_sec + std::fmod(e.t, 1.0f) * 0;
    float mins = lt.tm_min + sec / 60, hrs = (float)(lt.tm_hour % 12) + mins / 60;
    DrawCircleV({kClock.x + 3, kClock.y + 5}, 36, alpha(BLACK, 0.3f));
    DrawCircleV(kClock, 36, col::outline);
    DrawCircleV(kClock, 32, rgba(0xf4ecffff));
    DrawRing(kClock, 29, 32, 0, 360, 36, col::violet);
    for (int i = 0; i < 12; i++) {
        Vector2 d = fromAngle(i * PI / 6);
        DrawLineEx(kClock + d * 22, kClock + d * (i % 3 == 0 ? 28 : 26), i % 3 == 0 ? 3 : 1.5f, col::outline);
    }
    auto hand = [&](float frac, float len, float w, Color c) {
        Vector2 d = fromAngle(frac * 2 * PI - PI / 2);
        DrawLineEx(kClock - d * 4, kClock + d * len, w, c);
    };
    hand(hrs / 12, 15, 4, col::outline);
    hand(mins / 60, 22, 3, col::outline);
    hand(sec / 60, 24, 1.5f, col::red);
    DrawCircleV(kClock, 3, col::red);
}

void drawFloorLight(const Env& e) {
    // sun (or moon) light falling through the four panes onto the floor
    float d = daylight(e.hour);
    Color c = d > 0.05f ? alpha(sunColor(e.hour), 0.16f * d * (0.4f + 0.6f * e.lights)) : alpha(rgba(0x8fa8ffff), 0.07f);
    auto proj = [](Vector2 p) { return Vector2{p.x - (kFloorY - p.y) * 1.05f, kFloorY + 8 + (kFloorY - p.y) * 0.5f}; };
    fx::beginAdd();
    float hw = kGlass.width / 2 - 4, hh = kGlass.height / 2 - 4;
    for (int i = 0; i < 4; i++) {
        float x = kGlass.x + (i % 2) * (hw + 8), y = kGlass.y + (float)(i / 2) * (hh + 8);
        quad(proj({x, y}), proj({x + hw, y}), proj({x + hw, y + hh}), proj({x, y + hh}), c);
    }
    fx::endAdd();
}

void drawBed(const Env& e) {
    fx::softShadow({318, 548}, 170, 16, 0.55f);
    // headboard
    rrectO({188, 360, 250, 112}, 0.18f, rgba(0x5b2fb0ff), 3);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++) DrawCircleV({218 + c * 48.0f + (r % 2) * 24, 390 + r * 30.0f}, 3, darken(rgba(0x5b2fb0ff), 0.35f));
    // legs
    DrawRectangleRec({180, 528, 12, 22}, rgba(0x2a1a10ff));
    DrawRectangleRec({440, 528, 12, 22}, rgba(0x2a1a10ff));
    // mattress top + front
    quad({196, 438}, {432, 438}, {456, 492}, {172, 492}, rgba(0xe8e2f0ff));
    quadV({172, 492}, {456, 492}, {456, 530}, {172, 530}, rgba(0xd8d0e6ff), rgba(0xb8b0caff));
    // pillows
    rrectO({206, 420, 96, 34}, 0.7f, rgba(0xf4f0ffff), 2.5f);
    rrectO({300, 424, 82, 30}, 0.7f, col::pink, 2.5f);
    // blanket over the front two thirds, hanging over the edge
    Color bl = rgba(0x3a7bd5ff);
    quad({190, 458}, {438, 458}, {460, 494}, {168, 494}, bl);
    for (int i = 0; i < 6; i++) {  // stripes
        float u = (i + 0.5f) / 6;
        Vector2 a{lerpf(190, 438, u), 458}, b{lerpf(168, 460, u), 494};
        DrawLineEx(a, b, 5, alpha(WHITE, 0.25f));
    }
    rrect({166, 490, 296, 46}, 0.2f, bl);
    for (int i = 0; i < 9; i++) DrawCircleV({180 + i * 34.0f, 536}, 12, bl);  // scalloped hem
    DrawRectangleRec({166, 494, 296, 6}, alpha(WHITE, 0.25f));
    // plush horse on the bed
    art::horse({392, 470}, 0.42f, 0.0f, rgba(0x9a6a44ff), rgba(0x3a2418ff), col::red, "none");

    // nightstand + lamp in the corner
    if (e.horror) return;
    fx::softShadow({95, 591}, 70, 10, 0.5f);
    rrectO({40, 483, 112, 104}, 0.08f, rgba(0x4a3048ff), 3);
    rrect({48, 493, 96, 38}, 0.15f, rgba(0x5a3a58ff));
    rrect({48, 539, 96, 38}, 0.15f, rgba(0x5a3a58ff));
    DrawCircleV({96, 512}, 4, col::gold);
    DrawCircleV({96, 558}, 4, col::gold);
    bool lit = e.lights < 0.5f || daylight(e.hour) < 0.6f;
    DrawEllipse(96, 481, 22, 6, rgba(0x15121cff));
    DrawRectangleRec({93, 437, 6, 44}, rgba(0x2a2534ff));
    Color shade = lit ? rgba(0xffd9a0ff) : rgba(0xd8c8b8ff);
    quad({76, 405}, {116, 405}, {132, 443}, {60, 443}, shade);
    DrawLineEx({60, 443}, {132, 443}, 3, darken(shade, 0.3f));
    DrawLineEx({76, 405}, {116, 405}, 2, darken(shade, 0.3f));
    if (lit) {
        fx::beginAdd();
        fx::glow({96, 435}, 70, alpha(rgba(0xffb060ff), 0.6f));
        quad({62, 443}, {130, 443}, {170, 495}, {22, 495}, alpha(rgba(0xffb060ff), 0.18f), alpha(rgba(0xffb060ff), 0.18f), alpha(rgba(0xffb060ff), 0),
             alpha(rgba(0xffb060ff), 0));
        fx::endAdd();
    }
    // phone charging on the nightstand
    rrect({112, 473, 26, 8}, 0.4f, rgba(0x15121cff));
    DrawRectangleRec({116, 475, 18, 4}, alpha(col::cyan, 0.4f + 0.3f * std::sin(e.t * 2)));
}

void drawDesk(const Env& e) {
    fx::softShadow({1045, 588}, 190, 14, 0.5f);
    Color wood = rgba(0x3a2a40ff);
    // legs
    DrawRectangleRec({892, 476, 16, 108}, darken(wood, 0.3f));
    DrawRectangleRec({1186, 476, 16, 108}, darken(wood, 0.3f));
    DrawRectangleRec({892, 520, 310, 8}, darken(wood, 0.4f));
    // top surface + front edge
    quad({902, 434}, {1192, 434}, {1212, 462}, {884, 462}, lighten(wood, 0.12f));
    quadV({884, 462}, {1212, 462}, {1212, 478}, {884, 478}, wood, darken(wood, 0.3f));
    DrawLineEx({884, 462}, {1212, 462}, 2, lighten(wood, 0.3f));
    // desk mat with an RGB edge
    quad({948, 438}, {1146, 438}, {1156, 458}, {938, 458}, rgba(0x15121cff));
    DrawLineEx({938, 458}, {1156, 458}, 2, hue(e.t * 60, 0.8f, 0.5f + 0.5f * e.lights));

    // monitor
    DrawRectangleRec({1030, 404, 20, 34}, rgba(0x15121cff));
    DrawEllipse(1040, 440, 46, 6, rgba(0x15121cff));
    Rectangle mon{926, 286, 210, 120};
    rrect(shrink(mon, -8), 0.06f, rgba(0x15121cff));
    rrect({mon.x - 8, mon.y + mon.height + 2, mon.width + 16, 6}, 0.5f, rgba(0x221d2cff));
    screen(mon, e);
    DrawCircleV({mon.x + mon.width / 2, mon.y + mon.height + 5}, 1.5f, e.lights > 0.5f ? col::green : col::orange);

    // keyboard: RGB wave
    quad({968, 440}, {1100, 440}, {1106, 455}, {962, 455}, rgba(0x1c1924ff));
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 13; k++) {
            float u = k / 13.0f, y = 443 + r * 6.0f;
            float x = lerpf(970, 1098, u) - (y - 440) * 0.4f;
            DrawRectangleRec({x, y, 8, 4}, hue(e.t * 120 - k * 25 - r * 40, 0.85f, 0.35f + 0.65f * e.lights));
        }
    // mouse
    DrawEllipse(1124, 450, 8, 6, rgba(0x1c1924ff));
    DrawEllipse(1124, 448, 3, 2, hue(e.t * 120, 0.9f, 0.4f + 0.6f * e.lights));
    // drinks
    art::itemIcon("gamefuel", {936, 422}, 34);
    art::boba({1175, 418}, 36);

    // PC tower on the floor-right, glass side panel
    if (e.horror) return;
    Rectangle pc{1196, 326, 74, 160};
    fx::softShadow({pc.x + pc.width / 2, pc.y + pc.height + 2}, 50, 8, 0.6f);
    rrectO(pc, 0.06f, rgba(0x15121cff), 2.5f);
    Rectangle glass = shrink(pc, 7);
    rrect(glass, 0.05f, rgba(0x0c0a14ff));
    fan({glass.x + glass.width / 2, glass.y + 26}, 22, e, 0);
    fan({glass.x + glass.width / 2, glass.y + 76}, 22, e, 1.7f);
    DrawRectangleRec({glass.x + 4, glass.y + 108, glass.width - 8, 12}, rgba(0x2a2534ff));
    DrawRectangleRec({glass.x + 4, glass.y + 118, glass.width - 8, 2}, hue(e.t * 60 + 120, 0.8f, 0.4f + 0.6f * e.lights));
    ui::text("PIETER", glass.x + glass.width / 2, glass.y + 114, 8, alpha(col::text, 0.6f), ui::Center, true);
    DrawRectangleRec({glass.x + 6, glass.y + 128, 16, 12}, rgba(0x2a2534ff));
    quad({glass.x, glass.y}, {glass.x + 22, glass.y}, {glass.x + glass.width, glass.y + 60}, {glass.x + glass.width, glass.y + 82},
         alpha(WHITE, 0.05f));
}

void drawRug(const Env& e) {
    (void)e;
    Vector2 c{640, 634};
    DrawEllipse((int)c.x, (int)c.y + 6, 420, 74, alpha(BLACK, 0.3f));
    // tassels on both ends
    for (int s : {-1, 1})
        for (int i = 0; i < 9; i++) {
            float y = c.y - 32 + i * 8.0f;
            float x = c.x + s * 412 * std::sqrt(std::max(0.0f, 1 - std::pow((y - c.y) / 72, 2.0f)));
            DrawLineEx({x, y}, {x + s * 14, y + 2}, 2.5f, rgba(0xf4ecffff));
        }
    DrawEllipse((int)c.x, (int)c.y, 414, 70, rgba(0x6b3fd1ff));
    DrawEllipse((int)c.x, (int)c.y, 396, 63, rgba(0xff6fb5ff));
    DrawEllipse((int)c.x, (int)c.y, 382, 58, rgba(0x4a2a9aff));
    // diamond pattern ring
    for (int i = 0; i < 28; i++) {
        float a = i * 2 * PI / 28;
        Vector2 p{c.x + std::cos(a) * 340, c.y + std::sin(a) * 47};
        DrawPoly(p, 4, 9, 0, i % 2 ? col::gold : col::cyan);
    }
    DrawEllipse((int)c.x, (int)c.y, 300, 38, rgba(0x5b34b8ff));
    DrawEllipse((int)c.x, (int)c.y, 160, 20, rgba(0x6b44c8ff));
}

void drawChairBack(Vector2 s, const Env& e) {
    Color leather = rgba(0x1a1622ff), trim = rgba(0x6b2fb0ff);
    fx::softShadow({s.x, s.y + 92}, 170, 22, 0.55f);
    // star base + gas lift
    DrawRectangleRec({s.x - 10, s.y + 30, 20, 50}, rgba(0x2a2534ff));
    for (int i = 0; i < 5; i++) {
        float a = PI / 2 + (i - 2) * 0.62f;
        Vector2 end = Vector2{s.x, s.y + 80} + Vector2{std::cos(a) * 110 * (i == 2 ? 0.2f : 1), std::sin(a) * 18 + (i == 2 ? 10 : 0)};
        DrawLineEx({s.x, s.y + 80}, end, 9, rgba(0x15121cff));
        DrawCircleV({end.x, end.y + 6}, 8, rgba(0x0c0a12ff));
    }
    // backrest with side bolsters
    Rectangle back{s.x - 125, s.y - 400, 250, 400};
    rrectO(back, 0.22f, leather, 3);
    rrect({back.x + 6, back.y + 40, 30, back.height - 70}, 0.6f, trim);
    rrect({back.x + back.width - 36, back.y + 40, 30, back.height - 70}, 0.6f, trim);
    // stitching
    for (float y = back.y + 50; y < back.y + back.height - 40; y += 14) {
        DrawLineEx({back.x + 42, y}, {back.x + 42, y + 7}, 1.5f, alpha(trim, 0.6f));
        DrawLineEx({back.x + back.width - 42, y}, {back.x + back.width - 42, y + 7}, 1.5f, alpha(trim, 0.6f));
    }
    // headrest logo peeking above Pieter
    DrawCircleV({s.x, back.y + 24}, 14, trim);
    ui::text("P", s.x, back.y + 24, 18, WHITE, ui::Center, true);
    // monitor + window rim light on the right edge
    fx::beginAdd();
    fx::glowLine({back.x + back.width - 4, back.y + 30}, {back.x + back.width - 4, back.y + back.height - 30}, 10,
                 alpha(mix(rgba(0x8f5cffff), hue(e.t * 90), e.gaming), 0.25f * e.lights + 0.05f));
    fx::endAdd();
    // seat
    rrectO({s.x - 140, s.y - 30, 280, 54}, 0.5f, leather, 3);
    rrect({s.x - 130, s.y - 26, 260, 14}, 0.6f, alpha(WHITE, 0.06f));
}

void drawChairFront(Vector2 s, const Env& e) {
    (void)e;
    Color leather = rgba(0x221d2cff), trim = rgba(0x6b2fb0ff);
    for (int side : {-1, 1}) {
        float x = s.x + side * 152;
        DrawRectangleRec({x - 7, s.y - 70, 14, 64}, rgba(0x15121cff));
        rrectO({x - 34, s.y - 92, 68, 24}, 0.6f, leather, 3);
        rrect({x - 30, s.y - 90, 60, 6}, 0.6f, alpha(trim, 0.8f));
    }
}

void addLights(const Env& e) {
    float d = daylight(e.hour), on = e.lights;
    // window: sun by day, moonlight by night
    Color win = d > 0.05f ? sunColor(e.hour) : rgba(0x6878d8ff);
    fx::glow({kGlass.x + kGlass.width / 2, kGlass.y + kGlass.height / 2}, 420, alpha(win, d > 0.05f ? 0.9f * d : 0.5f));
    fx::glow({760, 600}, 330, alpha(win, d > 0.05f ? 0.5f * d : 0.25f));
    // monitor
    Color scr = on > 0.5f ? mix(rgba(0x8f7cffff), hue(e.t * 90), e.gaming) : rgba(0x40306aff);
    fx::glow({1030, 350}, 360, alpha(scr, 0.55f + 0.3f * e.gaming));
    // neon sign
    fx::glow({445, 140}, 320, alpha(rgba(0xff3d6aff), 0.65f * on));
    if (e.horror) {
        fx::glow({640, 380}, 300, alpha(rgba(0x6070c0ff), 0.12f));
        return;
    }
    // bedside lamp: reading light awake, dim night light while asleep
    fx::glow({95, 425}, 420, alpha(rgba(0xffa860ff), on > 0.5f ? 0.8f * (1 - d * 0.7f) : 0.45f));
    // LED strip
    for (float x = kWallL + 40; x < kWallR; x += 120) fx::glow({x, 20}, 160, alpha(hue(e.t * 40 + x * 0.35f), 0.35f * on));
    // PC fans
    fx::glow({1233, 380}, 200, alpha(hue(e.t * 60), 0.45f));
    // soft fill on Pieter so he never drowns in the dark
    fx::glow({640, 380}, 330, alpha(rgba(0xffe6d2ff), 0.3f * on + 0.12f));
}

void drawBloom(const Env& e) {
    float d = daylight(e.hour), on = e.lights;
    fx::beginAdd();
    // god rays from the window
    Color ray = d > 0.05f ? alpha(sunColor(e.hour), 0.07f * d) : alpha(rgba(0x8fa8ffff), 0.035f);
    for (int i = 0; i < 4; i++) {
        float x0 = kGlass.x + 20 + i * 50.0f, x1 = x0 - 260 - i * 30.0f;
        float w = 26 + hash(i) * 20, flick = 0.75f + 0.25f * std::sin(e.t * 0.5f + i * 1.7f);
        quad({x0, kGlass.y + 20}, {x0 + w, kGlass.y + 20}, {x1 + w * 3, 640}, {x1, 640}, alpha(ray, (float)ray.a / 255 * flick), alpha(ray, (float)ray.a / 255 * flick),
             alpha(ray, 0), alpha(ray, 0));
    }
    // dust motes drifting in the light
    for (int i = 0; i < 36; i++) {
        float u = hash(i * 1.3f), v = std::fmod(hash(i * 2.9f) + e.t * (0.01f + hash(i) * 0.02f), 1.0f);
        Vector2 p{lerpf(kGlass.x + 40, 640, v) + u * 160 - 80 + std::sin(e.t * 0.6f + i) * 10, lerpf(kGlass.y + 60, 600, v) + std::cos(e.t * 0.4f + i) * 8};
        float tw = 0.5f + 0.5f * std::sin(e.t * 2 + i * 3);
        fx::glow(p, 3 + hash(i * 7.0f) * 3, alpha(WHITE, (0.25f + 0.35f * tw) * (d > 0.05f ? d : 0.3f)));
    }
    // window glow
    fx::glow({kGlass.x + kGlass.width / 2, kGlass.y + kGlass.height / 2}, 220,
             alpha(d > 0.05f ? sunColor(e.hour) : rgba(0x6878d8ff), d > 0.05f ? 0.1f * d : 0.1f));
    // neon
    if (on > 0.5f) fx::glow({445, 140}, 190, alpha(rgba(0xff3d6aff), 0.3f));
    // monitor
    Color scr = on > 0.5f ? mix(rgba(0x8f7cffff), hue(e.t * 90), e.gaming) : rgba(0x40306aff);
    fx::glow({1031, 346}, 170, alpha(scr, 0.25f + 0.25f * e.gaming));
    // LED strip
    for (float x = kWallL + 30; x < kWallR; x += 60) fx::glow({x, 14}, 46, alpha(hue(e.t * 40 + x * 0.35f), 0.45f * on));
    if (e.horror) {
        fx::endAdd();
        return;
    }
    // PC fans
    fx::glow({1233, 359}, 40, alpha(hue(e.t * 60), 0.5f));
    fx::glow({1233, 409}, 40, alpha(hue(e.t * 60 + 68), 0.5f));
    fx::endAdd();
}

}  // namespace pg::room
