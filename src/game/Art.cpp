#include "game/Art.hpp"

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"
#include "engine/Ui.hpp"
#include "game/Profile.hpp"

#include <rlgl.h>

namespace pg::art {

namespace {
// Draw in a local coordinate system centered at `c`, rotated by `deg`.
struct Local {
    Local(Vector2 c, float deg = 0, float sx = 1, float sy = 1) {
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        if (deg != 0) rlRotatef(deg, 0, 0, 1);
        if (sx != 1 || sy != 1) rlScalef(sx, sy, 1);
    }
    ~Local() { rlPopMatrix(); }
};

void ellipseF(Vector2 c, float rx, float ry, Color color) {
    // DrawEllipse snaps to ints; this version keeps sub-pixel precision under transforms
    Local l(c, 0, 1, ry / rx);
    DrawCircleV({0, 0}, rx, color);
}

void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    // counter-clockwise a,b,c,d (screen space, y down -> pass clockwise visually)
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, d, col);
    DrawTriangle(a, c, b, col);  // draw both windings so callers can't get it wrong
    DrawTriangle(a, d, c, col);
}

void limb(Vector2 a, Vector2 b, float w, Color col) {
    DrawLineEx(a, b, w, col);
    DrawCircleV(a, w / 2, col);
    DrawCircleV(b, w / 2, col);
}
}  // namespace

// ------------------------------------------------------------------ portraits

void portrait(const std::string& who, Vector2 c, float radius, Color ring, float rotDeg, bool flip, Color tint) {
    Texture2D& t = E().tex(who);
    Rectangle src{0, 0, flip ? -(float)t.width : (float)t.width, (float)t.height};
    DrawCircleV({c.x + radius * 0.06f, c.y + radius * 0.1f}, radius * 1.04f, alpha(BLACK, 0.35f * tint.a / 255.0f));
    DrawTexturePro(t, src, {c.x, c.y, radius * 2, radius * 2}, {radius, radius}, rotDeg, tint);
    if (ring.a > 0) DrawRing(c, radius - 1, radius + std::max(2.0f, radius * 0.07f), 0, 360, 48, ring);
}

void hat(const std::string& id, Vector2 c, float r, float rotDeg) {
    if (id.empty()) return;
    Local l(c, rotDeg);
    const Color o = col::outline;
    if (id == "hat_party") {
        Vector2 a{-0.48f * r, -0.62f * r}, b{0.42f * r, -0.78f * r}, tip{0.12f * r, -1.85f * r};
        DrawTriangle(tip, a, b, o);
        Vector2 a2{a.x + 4, a.y - 2}, b2{b.x - 4, b.y - 2}, tip2{tip.x, tip.y + 6};
        DrawTriangle(tip2, a2, b2, col::pink);
        for (int i = 1; i <= 3; i++) {
            float t = i / 4.0f;
            Vector2 p1 = Vector2Lerp(a2, tip2, t), p2 = Vector2Lerp(b2, tip2, t);
            DrawLineEx(p1, p2, r * 0.08f, col::gold);
        }
        DrawCircleV(tip, r * 0.16f, o);
        DrawCircleV(tip, r * 0.13f, col::cyan);
    } else if (id == "hat_cap") {
        DrawCircleSector({0, -0.5f * r}, 0.74f * r, 180, 360, 24, o);
        DrawCircleSector({0, -0.5f * r}, 0.68f * r, 180, 360, 24, col::red);
        DrawRectangleRounded({-1.25f * r, -0.58f * r, 0.75f * r, 0.18f * r}, 1, 8, darken(col::red, 0.3f));
        DrawRectangleRoundedLinesEx({-1.25f * r, -0.58f * r, 0.75f * r, 0.18f * r}, 1, 8, 2, o);
        DrawCircleV({0, -1.2f * r}, 0.09f * r, darken(col::red, 0.3f));
        ui::text("P", 0.15f * r, -0.85f * r, r * 0.42f, WHITE, ui::Center, true);
    } else if (id == "hat_cowboy") {
        Color br = rgba(0x8b5a2bff);
        ellipseF({0, -0.66f * r}, 1.3f * r, 0.26f * r, o);
        ellipseF({0, -0.68f * r}, 1.24f * r, 0.2f * r, br);
        DrawRectangleRounded({-0.55f * r, -1.45f * r, 1.1f * r, 0.8f * r}, 0.5f, 10, o);
        DrawRectangleRounded({-0.5f * r, -1.4f * r, 1.0f * r, 0.74f * r}, 0.5f, 10, lighten(br, 0.1f));
        DrawRectangle((int)(-0.5f * r), (int)(-0.86f * r), (int)(1.0f * r), (int)(0.14f * r), darken(br, 0.5f));
    } else if (id == "hat_crown") {
        float w = 0.62f * r, base = -0.68f * r, top = -1.4f * r;
        Vector2 pts[] = {{-w, base}, {-w, top}, {-w * 0.5f, -1.05f * r}, {0, top - 0.12f * r},
                         {w * 0.5f, -1.05f * r}, {w, top}, {w, base}};
        for (int pass = 0; pass < 2; pass++) {
            Color cc = pass == 0 ? o : col::gold;
            float g = pass == 0 ? 0 : 3;
            DrawRectangleV({-w + g, -1.05f * r + g}, {2 * w - 2 * g, base - (-1.05f * r) - g}, cc);
            DrawTriangle({pts[1].x + g, pts[1].y + g * 2}, {pts[0].x + g, -1.0f * r}, {pts[2].x, pts[2].y + g}, cc);
            DrawTriangle({pts[3].x, pts[3].y + g * 2}, {pts[2].x + g, pts[2].y + g}, {pts[4].x - g, pts[4].y + g}, cc);
            DrawTriangle({pts[5].x - g, pts[5].y + g * 2}, {pts[4].x, pts[4].y + g}, {pts[6].x - g, -1.0f * r}, cc);
        }
        DrawCircleV({0, -0.86f * r}, 0.1f * r, col::red);
        DrawCircleV({-0.38f * r, -0.86f * r}, 0.07f * r, col::cyan);
        DrawCircleV({0.38f * r, -0.86f * r}, 0.07f * r, col::green);
    } else if (id == "hat_halo") {
        float bob = std::sin(E().time() * 3) * 0.06f * r;
        Local l2({0, -1.25f * r + bob}, 0, 1, 0.32f);
        DrawRing({0, 0}, 0.55f * r, 0.75f * r, 0, 360, 40, alpha(rgba(0xfff3a0ff), 0.35f));
        DrawRing({0, 0}, 0.6f * r, 0.7f * r, 0, 360, 40, rgba(0xfff3a0ff));
    }
}

void pieter(Vector2 c, float radius, const std::string& hatId, float rotDeg, Color tint) {
    portrait("pieter", c, radius, col::outline, rotDeg, false, tint);
    hat(hatId, c, radius, rotDeg);
}

// ------------------------------------------------------------------ food & props

void boba(Vector2 c, float s, Color tea) {
    // cup: trapezoid
    float tw = s * 0.42f, bw = s * 0.32f, top = -s * 0.3f, bot = s * 0.5f;
    Color cup = alpha(WHITE, 0.55f);
    quad({c.x - tw - 3, c.y + top}, {c.x + tw + 3, c.y + top}, {c.x + bw + 3, c.y + bot + 3}, {c.x - bw - 3, c.y + bot + 3}, col::outline);
    quad({c.x - tw, c.y + top}, {c.x + tw, c.y + top}, {c.x + bw, c.y + bot}, {c.x - bw, c.y + bot}, cup);
    float fill = top + s * 0.12f;
    float fw = lerpf(tw, bw, (fill - top) / (bot - top));
    quad({c.x - fw, c.y + fill}, {c.x + fw, c.y + fill}, {c.x + bw, c.y + bot}, {c.x - bw, c.y + bot}, tea);
    for (int i = 0; i < 6; i++) {
        float px = c.x - bw * 0.7f + (i % 3) * bw * 0.7f + (i / 3) * bw * 0.3f;
        float py = c.y + bot - s * 0.08f - (i / 3) * s * 0.11f;
        DrawCircleV({px, py}, s * 0.055f, rgba(0x2a1810ff));
    }
    // lid + straw
    DrawLineEx({c.x + s * 0.05f, c.y + top + s * 0.05f}, {c.x + s * 0.18f, c.y - s * 0.65f}, s * 0.09f, col::outline);
    DrawLineEx({c.x + s * 0.05f, c.y + top + s * 0.05f}, {c.x + s * 0.18f, c.y - s * 0.63f}, s * 0.06f, col::pink);
    DrawRectangleRounded({c.x - tw - 5, c.y + top - s * 0.06f, tw * 2 + 10, s * 0.1f}, 1, 8, col::outline);
    DrawRectangleRounded({c.x - tw - 3, c.y + top - s * 0.05f, tw * 2 + 6, s * 0.07f}, 1, 8, alpha(WHITE, 0.9f));
}

void stroopwafel(Vector2 c, float w, float h, float rotDeg, Color tint) {
    Local l(c, rotDeg);
    Color wa = mix(col::wafel, tint, tint.r == 255 && tint.g == 255 ? 0 : 0.5f);
    Color dark = darken(wa, 0.25f);
    float wh = h * 0.38f;
    Rectangle bottom{-w / 2, h / 2 - wh, w, wh}, top{-w / 2, -h / 2, w, wh};
    DrawRectangleRounded({-w / 2 - 2, -h / 2 - 2, w + 4, h + 4}, 0.4f, 8, col::outline);
    DrawRectangleRec({-w / 2 + 2, -h / 2 + wh - 2, w - 4, h - 2 * wh + 4}, col::caramel);
    DrawRectangleRounded(bottom, 0.5f, 8, dark);
    DrawRectangleRounded(top, 0.5f, 8, wa);
    for (float x = -w / 2 + 10; x < w / 2 - 4; x += 12) {
        DrawLineEx({x, top.y + 2}, {x, top.y + wh - 2}, 1.5f, alpha(dark, 0.7f));
        DrawLineEx({x, bottom.y + 2}, {x, bottom.y + wh - 2}, 1.5f, alpha(darken(dark, 0.2f), 0.7f));
    }
    DrawRectangleRounded({top.x + 4, top.y + 2, top.width - 8, wh * 0.3f}, 1, 6, alpha(WHITE, 0.25f));
}

void stroopwafelTop(Vector2 c, float r) {
    DrawCircleV(c, r + 2, col::outline);
    DrawCircleV(c, r, col::wafel);
    DrawRing(c, r * 0.86f, r, 0, 360, 32, darken(col::wafel, 0.15f));
    Color line = darken(col::wafel, 0.3f);
    for (float k = -0.7f; k <= 0.71f; k += 0.35f) {
        // diagonal grid clipped to the inner circle
        float d = k * r * 0.86f;
        float half = std::sqrt(std::max(0.0f, (r * 0.86f) * (r * 0.86f) - d * d));
        Vector2 n1 = Vector2Normalize({1, 1}), t1 = {-n1.y, n1.x};
        Vector2 n2 = Vector2Normalize({1, -1}), t2 = {-n2.y, n2.x};
        DrawLineEx(c + n1 * d - t1 * half, c + n1 * d + t1 * half, r * 0.07f, line);
        DrawLineEx(c + n2 * d - t2 * half, c + n2 * d + t2 * half, r * 0.07f, line);
    }
    DrawCircleV({c.x - r * 0.35f, c.y - r * 0.35f}, r * 0.18f, alpha(WHITE, 0.2f));
}

void bomb(Vector2 c, float r, float t) {
    DrawCircleV({c.x + 2, c.y + 3}, r, alpha(BLACK, 0.4f));
    DrawCircleV(c, r, rgba(0x2b2b38ff));
    DrawCircleV({c.x - r * 0.35f, c.y - r * 0.35f}, r * 0.25f, alpha(WHITE, 0.35f));
    DrawRectangleRec({c.x + r * 0.4f, c.y - r * 1.0f, r * 0.4f, r * 0.35f}, rgba(0x55556aff));
    DrawLineEx({c.x + r * 0.6f, c.y - r * 1.0f}, {c.x + r * 0.9f, c.y - r * 1.4f}, 3, col::muted);
    float flick = 0.7f + 0.3f * std::sin(t * 30);
    DrawCircleV({c.x + r * 0.9f, c.y - r * 1.45f}, r * 0.22f * flick, col::orange);
    DrawCircleV({c.x + r * 0.9f, c.y - r * 1.45f}, r * 0.1f * flick, col::gold);
}

void gem(Vector2 c, float r, Color color) {
    Vector2 top{c.x, c.y - r}, left{c.x - r * 0.9f, c.y - r * 0.2f}, right{c.x + r * 0.9f, c.y - r * 0.2f}, bot{c.x, c.y + r};
    DrawTriangle(top, left, right, lighten(color, 0.3f));
    DrawTriangle(left, bot, right, color);
    DrawTriangle({c.x, c.y - r * 0.2f}, bot, right, darken(color, 0.2f));
    DrawLineEx(left, right, 2, alpha(WHITE, 0.6f));
    DrawTriangle(top, {c.x - r * 0.3f, c.y - r * 0.2f}, {c.x + r * 0.2f, c.y - r * 0.2f}, alpha(WHITE, 0.5f));
}

// ------------------------------------------------------------------ horse

void horse(Vector2 feet, float s, float phase, Color body, Color mane, Color silk, const std::string& jockey, float facing) {
    float bob = std::sin(phase * 2) * 3 * s;
    Local l({feet.x, feet.y}, 0, facing, 1);
    Color dark = darken(body, 0.25f), o = col::outline;

    // legs (far side first, darker)
    auto leg = [&](float hipX, float off, bool far) {
        float a = std::sin(phase + off);
        Vector2 hip{hipX * s, -52 * s + bob};
        Vector2 knee = hip + Vector2{a * 16 * s, 26 * s};
        Vector2 hoof = knee + Vector2{(a * 6 - std::max(0.0f, std::cos(phase + off)) * 12) * s, 26 * s - std::max(0.0f, -a) * 6 * s};
        Color lc = far ? darken(body, 0.4f) : dark;
        limb(hip, knee, 10 * s, lc);
        limb(knee, hoof, 7 * s, lc);
        DrawRectangleRec({hoof.x - 5 * s, hoof.y - 2 * s, 10 * s, 5 * s}, o);
    };
    leg(30, 0.0f, true);
    leg(-32, PI, true);

    // tail
    float sw = std::sin(phase * 1.3f) * 8 * s;
    for (int i = 0; i < 3; i++)
        DrawLineEx({-52 * s, -74 * s + bob}, {-80 * s + sw, (-50 + i * 6) * s + bob}, 5 * s, mane);

    // body
    ellipseF({0, -66 * s + bob}, 58 * s, 26 * s, o);
    ellipseF({0, -66 * s + bob}, 55 * s, 23 * s, body);
    ellipseF({-4 * s, -74 * s + bob}, 40 * s, 9 * s, alpha(WHITE, 0.12f));

    // neck + head
    Vector2 nb{38 * s, -72 * s + bob}, nt{66 * s, -112 * s + bob};
    limb(nb, nt, 26 * s, o);
    limb(nb, nt, 21 * s, body);
    {
        Local h({78 * s, -110 * s + bob}, 32);
        ellipseF({0, 0}, 25 * s, 13 * s, o);
        ellipseF({0, 0}, 22 * s, 11 * s, body);
        ellipseF({12 * s, 2 * s}, 11 * s, 9 * s, dark);
        DrawCircleV({14 * s, 1 * s}, 2 * s, o);
        DrawCircleV({-6 * s, -5 * s}, 3 * s, WHITE);
        DrawCircleV({-5 * s, -5 * s}, 1.8f * s, o);
    }
    DrawTriangle({62 * s, -122 * s + bob}, {58 * s, -138 * s + bob}, {70 * s, -126 * s + bob}, dark);
    for (int i = 0; i < 5; i++) {
        Vector2 p = Vector2Lerp(nb, nt, i / 4.0f);
        DrawTriangle({p.x - 4 * s, p.y - 9 * s}, {p.x - 18 * s, p.y + 2 * s}, {p.x + 6 * s, p.y - 4 * s}, mane);
    }

    // near legs
    leg(26, PI * 0.5f, false);
    leg(-36, PI * 1.5f, false);

    // saddle cloth + jockey ("none" = riderless, e.g. a plush toy)
    if (jockey == "none") {
        DrawRectangleRounded({-14 * s, -92 * s + bob, 30 * s, 20 * s}, 0.4f, 6, silk);
        DrawRectangleRoundedLinesEx({-14 * s, -92 * s + bob, 30 * s, 20 * s}, 0.4f, 6, 2, o);
        return;
    }
    DrawRectangleRounded({-14 * s, -92 * s + bob, 30 * s, 20 * s}, 0.4f, 6, silk);
    DrawRectangleRoundedLinesEx({-14 * s, -92 * s + bob, 30 * s, 20 * s}, 0.4f, 6, 2, o);
    Vector2 seat{2 * s, -92 * s + bob};
    Vector2 shoulder{12 * s, -122 * s + bob};
    limb(seat, shoulder, 14 * s, silk);
    limb(shoulder, {38 * s, -104 * s + bob}, 6 * s, silk);  // arm to reins
    limb(seat, {16 * s, -78 * s + bob}, 8 * s, rgba(0xf4f4f4ff));
    if (!jockey.empty()) {
        rlPushMatrix();
        rlScalef(facing, 1, 1);  // undo the flip so the face isn't mirrored
        if (jockey == "pieter") pieter({facing * 16 * s, -140 * s + bob}, 15 * s, P().hat);
        else portrait(jockey, {facing * 16 * s, -140 * s + bob}, 15 * s);
        rlPopMatrix();
    } else {
        DrawCircleV({16 * s, -138 * s + bob}, 11 * s, rgba(0xe8b48aff));
        DrawCircleSector({16 * s, -140 * s + bob}, 12 * s, 180, 360, 16, silk);
        ui::circleOutline({16 * s, -138 * s + bob}, 11 * s, o);
    }
}

// ------------------------------------------------------------------ cards

void suit(int s, Vector2 c, float size) {
    Color red = rgba(0xe0283cff), black = rgba(0x1d1a24ff);
    float r = size * 0.27f;
    switch (s) {
        case 1: ui::heart(c, size, red); break;
        case 2:
            DrawTriangle({c.x, c.y - size * 0.55f}, {c.x - size * 0.4f, c.y}, {c.x + size * 0.4f, c.y}, red);
            DrawTriangle({c.x - size * 0.4f, c.y}, {c.x, c.y + size * 0.55f}, {c.x + size * 0.4f, c.y}, red);
            break;
        case 0: {
            // upside-down heart + stem
            DrawCircleV({c.x - r * 0.95f, c.y + r * 0.25f}, r * 1.05f, black);
            DrawCircleV({c.x + r * 0.95f, c.y + r * 0.25f}, r * 1.05f, black);
            DrawTriangle({c.x + size * 0.58f, c.y + r * 0.1f}, {c.x, c.y - size * 0.55f}, {c.x - size * 0.58f, c.y + r * 0.1f}, black);
            DrawTriangle({c.x, c.y + r * 0.4f}, {c.x - r * 0.7f, c.y + size * 0.55f}, {c.x + r * 0.7f, c.y + size * 0.55f}, black);
            break;
        }
        case 3:
            DrawCircleV({c.x, c.y - r * 0.95f}, r, black);
            DrawCircleV({c.x - r * 1.0f, c.y + r * 0.35f}, r, black);
            DrawCircleV({c.x + r * 1.0f, c.y + r * 0.35f}, r, black);
            DrawTriangle({c.x, c.y}, {c.x - r * 0.7f, c.y + size * 0.55f}, {c.x + r * 0.7f, c.y + size * 0.55f}, black);
            break;
    }
}

void card(Rectangle r, int rank, int s, bool faceUp) {
    DrawRectangleRounded({r.x + 3, r.y + 5, r.width, r.height}, 0.12f, 8, alpha(BLACK, 0.4f));
    DrawRectangleRounded(r, 0.12f, 8, faceUp ? rgba(0xfdfbf7ff) : col::purple);
    DrawRectangleRoundedLinesEx(r, 0.12f, 8, 2, col::outline);
    if (!faceUp) {
        Rectangle in = shrink(r, 7);
        DrawRectangleRoundedLinesEx(in, 0.1f, 8, 2, alpha(WHITE, 0.4f));
        for (float y = in.y + 8; y < in.y + in.height; y += 14)
            for (float x = in.x + 8 + ((int)(y / 14) % 2) * 7; x < in.x + in.width - 4; x += 14)
                DrawCircleV({x, y}, 2, alpha(WHITE, 0.15f));
        ui::text("P", r.x + r.width / 2, r.y + r.height / 2, r.height * 0.4f, col::gold, ui::Center, true);
        return;
    }
    static const char* ranks[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    Color c = (s == 1 || s == 2) ? rgba(0xe0283cff) : rgba(0x1d1a24ff);
    ui::text(ranks[rank], r.x + 8, r.y + 16, r.height * 0.2f, c, ui::Left, true);
    suit(s, {r.x + 16, r.y + 40}, r.height * 0.13f);
    if (rank >= 11) {
        // face cards get Pieter crew portraits
        const char* who = rank == 11 ? "davi" : rank == 12 ? "kira" : "pieter";
        portrait(who, {r.x + r.width / 2 + 6, r.y + r.height * 0.58f}, r.width * 0.3f, c);
    } else {
        suit(s, {r.x + r.width / 2 + 4, r.y + r.height * 0.58f}, r.height * 0.36f);
    }
}

// ------------------------------------------------------------------ item icons

void itemIcon(const std::string& id, Vector2 c, float s) {
    if (id == "shield") {
        auto shape = [&](float g, Color colr) {
            float w = s * 0.42f - g, top = c.y - s * 0.44f + g, mid = c.y + s * 0.05f, bot = c.y + s * 0.5f - g * 1.6f;
            DrawRectangleRounded({c.x - w, top, w * 2, mid - top + 2}, 0.25f, 6, colr);
            DrawTriangle({c.x - w, mid}, {c.x, bot}, {c.x + w, mid}, colr);
        };
        shape(0, col::outline);
        shape(3, col::cyan);
        shape(s * 0.1f, lighten(col::cyan, 0.35f));
        boba({c.x, c.y - s * 0.02f}, s * 0.42f);
    } else if (id == "speed" || id == "gamefuel") {
        Rectangle can{c.x - s * 0.24f, c.y - s * 0.45f, s * 0.48f, s * 0.9f};
        DrawRectangleRounded(shrink(can, -3), 0.3f, 8, col::outline);
        DrawRectangleRounded(can, 0.3f, 8, rgba(0x23202bff));
        DrawRectangleRec({can.x, can.y + 4, can.width, 4}, rgba(0x8a8a9aff));
        Vector2 z[] = {{c.x + s * 0.08f, c.y - s * 0.3f}, {c.x - s * 0.12f, c.y + s * 0.05f}, {c.x + s * 0.02f, c.y + s * 0.05f},
                       {c.x - s * 0.08f, c.y + s * 0.32f}};
        DrawLineEx(z[0], z[1], s * 0.08f, col::gold);
        DrawLineEx(z[1], z[2], s * 0.08f, col::gold);
        DrawLineEx(z[2], z[3], s * 0.08f, col::gold);
    } else if (id == "slowmo") {
        DrawCircleV(c, s * 0.45f, col::outline);
        DrawCircleV(c, s * 0.4f, col::cyan);
        DrawCircleV(c, s * 0.32f, rgba(0xe9fbffff));
        DrawLineEx(c, {c.x, c.y - s * 0.25f}, 3, col::outline);
        DrawLineEx(c, {c.x + s * 0.17f, c.y + s * 0.05f}, 3, col::outline);
        ui::text("z", c.x + s * 0.38f, c.y - s * 0.38f, s * 0.35f, WHITE, ui::Center, true);
    } else if (id == "magnet") {
        DrawRing(c, s * 0.18f, s * 0.42f, 0, 180, 20, col::outline);
        DrawRing(c, s * 0.22f, s * 0.38f, 0, 180, 20, col::red);
        DrawRectangleRec({c.x - s * 0.42f, c.y - s * 0.38f, s * 0.24f, s * 0.38f}, col::outline);
        DrawRectangleRec({c.x + s * 0.18f, c.y - s * 0.38f, s * 0.24f, s * 0.38f}, col::outline);
        DrawRectangleRec({c.x - s * 0.38f, c.y - s * 0.12f, s * 0.16f, s * 0.14f}, col::red);
        DrawRectangleRec({c.x + s * 0.22f, c.y - s * 0.12f, s * 0.16f, s * 0.14f}, col::red);
        DrawRectangleRec({c.x - s * 0.38f, c.y - s * 0.36f, s * 0.16f, s * 0.22f}, rgba(0xdfe3eaff));
        DrawRectangleRec({c.x + s * 0.22f, c.y - s * 0.36f, s * 0.16f, s * 0.22f}, rgba(0xdfe3eaff));
    } else if (id == "heal") {
        ui::heart({c.x, c.y + 2}, s * 0.95f, col::outline);
        ui::heart(c, s * 0.8f, col::pink);
        DrawCircleV({c.x - s * 0.18f, c.y - s * 0.18f}, s * 0.07f, alpha(WHITE, 0.7f));
    } else if (id == "nuke") {
        Rectangle box{c.x - s * 0.42f, c.y - s * 0.12f, s * 0.84f, s * 0.42f};
        DrawLineEx({c.x - s * 0.25f, box.y}, {c.x - s * 0.35f, c.y - s * 0.45f}, 3, col::outline);
        DrawLineEx({c.x + s * 0.25f, box.y}, {c.x + s * 0.35f, c.y - s * 0.45f}, 3, col::outline);
        DrawRectangleRounded(shrink(box, -3), 0.3f, 6, col::outline);
        DrawRectangleRounded(box, 0.3f, 6, rgba(0x55566aff));
        for (int i = 0; i < 4; i++) DrawCircleV({box.x + 10 + i * s * 0.18f, box.y + box.height / 2}, 3, i % 2 ? col::green : col::red);
    } else if (id == "boba") {
        boba(c, s * 0.85f);
    } else if (id == "stroopwafel") {
        stroopwafelTop(c, s * 0.42f);
    } else if (id == "frikandel") {
        Local l(c, -20);
        DrawRectangleRounded({-s * 0.5f, -s * 0.13f, s, s * 0.26f}, 1, 10, col::outline);
        DrawRectangleRounded({-s * 0.47f, -s * 0.1f, s * 0.94f, s * 0.2f}, 1, 10, col::caramel);
        for (float x = -s * 0.3f; x < s * 0.35f; x += s * 0.2f) {
            DrawCircleV({x, -s * 0.06f}, s * 0.06f, WHITE);
            DrawCircleV({x + s * 0.09f, -s * 0.05f}, s * 0.05f, col::red);
            DrawCircleV({x + s * 0.05f, -s * 0.1f}, s * 0.025f, rgba(0xf2f0d0ff));
        }
    } else if (id == "kroket") {
        Local l(c, -15);
        DrawRectangleRounded({-s * 0.42f, -s * 0.18f, s * 0.84f, s * 0.36f}, 1, 12, col::outline);
        DrawRectangleRounded({-s * 0.39f, -s * 0.15f, s * 0.78f, s * 0.3f}, 1, 12, col::orange);
        for (int i = 0; i < 14; i++)
            DrawCircleV({-s * 0.33f + (i * 37 % 66) / 100.0f * s, -s * 0.1f + (i * 53 % 20) / 100.0f * s}, 1.5f,
                        darken(col::orange, 0.3f));
    } else if (id == "hagelslag") {
        DrawRectangleRounded({c.x - s * 0.42f, c.y - s * 0.38f, s * 0.84f, s * 0.78f}, 0.35f, 10, col::outline);
        DrawRectangleRounded({c.x - s * 0.39f, c.y - s * 0.35f, s * 0.78f, s * 0.72f}, 0.35f, 10, rgba(0xd9a35fff));
        DrawRectangleRounded({c.x - s * 0.32f, c.y - s * 0.27f, s * 0.64f, s * 0.58f}, 0.3f, 10, rgba(0xf6e2b8ff));
        for (int i = 0; i < 22; i++) {
            float x = c.x - s * 0.26f + (i * 37 % 52) / 100.0f * s, y = c.y - s * 0.2f + (i * 61 % 46) / 100.0f * s;
            DrawLineEx({x, y}, {x + 4, y + 2}, 2.5f, rgba(0x3b2112ff));
        }
    } else if (id.rfind("hat_", 0) == 0) {
        DrawCircleV({c.x, c.y + s * 0.35f}, s * 0.4f, alpha(col::muted, 0.25f));
        hat(id, {c.x, c.y + s * 0.4f}, s * 0.42f);
    } else if (id.rfind("trail_", 0) == 0) {
        for (int i = 0; i < 5; i++) {
            Vector2 p{c.x + s * 0.35f - i * s * 0.18f, c.y + std::sin(i * 1.2f) * s * 0.1f};
            Color cc = id == "trail_rainbow" ? ColorFromHSV(i * 60.0f, 0.8f, 1) :
                       id == "trail_fire"    ? mix(col::gold, col::red, i / 4.0f) : rgba(0x3a2418ff);
            DrawCircleV(p, s * (0.18f - i * 0.025f), alpha(cc, 1 - i * 0.15f));
        }
    }
}

// ------------------------------------------------------------------ backgrounds

void menuBackground(float px, float py, float dim) {
    Texture2D& t = E().tex("menu_bg");
    float sc = std::max((float)VW / t.width, (float)VH / t.height) * 1.04f;
    float w = t.width * sc, h = t.height * sc;
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {(VW - w) / 2 + px, (VH - h) / 2 + py, w, h}, {0, 0}, 0, WHITE);
    if (dim > 0) DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, dim));
}

void purpleBackdrop(float t, bool grid) {
    // the menu artwork, blurred and dimmed, with drifting bokeh lights
    fx::blurredBackdrop("menu_bg", 0.62f, t);
    DrawRectangleGradientV(0, 0, VW, VH, alpha(rgba(0x2a1650ff), 0.35f), alpha(col::bgDeep, 0.55f));
    fx::bokeh(t, 16, col::violet, 1);
    fx::bokeh(t * 0.8f, 8, col::pink, 2);
    if (grid) {
        float off = std::fmod(t * 12, 48.0f);
        for (float x = -48 + off; x < VW; x += 48) DrawLineEx({x, 0}, {x, (float)VH}, 1, alpha(col::violet, 0.05f));
        for (float y = -48 + off; y < VH; y += 48) DrawLineEx({0, y}, {(float)VW, y}, 1, alpha(col::violet, 0.05f));
    }
}

void vignette(float s) {
    Color a = alpha(BLACK, s), z = alpha(BLACK, 0);
    DrawRectangleGradientV(0, 0, VW, 140, a, z);
    DrawRectangleGradientV(0, VH - 140, VW, 140, z, a);
    DrawRectangleGradientH(0, 0, 160, VH, a, z);
    DrawRectangleGradientH(VW - 160, 0, 160, VH, z, a);
}

}  // namespace pg::art
