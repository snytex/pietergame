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

// ---- winding-agnostic primitives (safe under mirrored transforms) ----
void triAny(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}
void polyFan(const Vector2* p, int n, Color col) {
    for (int i = 1; i + 1 < n; i++) triAny(p[0], p[i], p[i + 1], col);
}
// ellipse rotated by `rot` radians, grown by `grow` in every direction
void ell(Vector2 c, float rx, float ry, float rot, Color col, float grow = 0) {
    const int n = 28;
    Vector2 pts[n];
    float cr = std::cos(rot), sr = std::sin(rot);
    for (int i = 0; i < n; i++) {
        float a = 2 * PI * i / n;
        float x = std::cos(a) * (rx + grow), y = std::sin(a) * (ry + grow);
        pts[i] = {c.x + x * cr - y * sr, c.y + x * sr + y * cr};
    }
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < n; i++)
        for (int w = 0; w < 2; w++) {
            Vector2 p = pts[w ? (i + 1) % n : i], q = pts[w ? i : (i + 1) % n];
            rlColor4ub(col.r, col.g, col.b, col.a);
            rlVertex2f(c.x, c.y);
            rlVertex2f(p.x, p.y);
            rlVertex2f(q.x, q.y);
        }
    rlEnd();
}
void disc(Vector2 c, float r, Color col) { ell(c, r, r, 0, col); }
// tapered limb with round ends
void taper(Vector2 a, Vector2 b, float wa, float wb, Color col, float grow = 0) {
    Vector2 d = Vector2Normalize(b - a), n{-d.y, d.x};
    wa = wa / 2 + grow;
    wb = wb / 2 + grow;
    triAny(a + n * wa, a - n * wa, b - n * wb, col);
    triAny(a + n * wa, b - n * wb, b + n * wb, col);
    disc(a, wa, col);
    disc(b, wb, col);
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
    // side view: two thin golden wafers with a waffle imprint, glossy caramel between
    Local l(c, rotDeg);
    bool tinted = !(tint.r == 255 && tint.g == 255 && tint.b == 255);
    Color wa = tinted ? mix(col::wafel, tint, 0.5f) : col::wafel;
    Color car = tinted ? mix(col::caramel, tint, 0.5f) : rgba(0x9a4e14ff);
    float r = std::min(1.0f, 0.9f);
    DrawRectangleRounded({-w / 2 - 2.5f, -h / 2 - 2.5f, w + 5, h + 5}, r, 10, col::outline);
    // caramel core
    Rectangle mid{-w / 2 + 2, -h * 0.16f, w - 4, h * 0.34f};
    fx::gradRoundRect(mid, 0.6f, lighten(car, 0.15f), darken(car, 0.2f));
    // bottom wafer (in shadow)
    Rectangle bot{-w / 2, h * 0.12f, w, h * 0.38f};
    fx::gradRoundRect(bot, 0.9f, darken(wa, 0.12f), darken(wa, 0.38f));
    // top wafer (lit)
    Rectangle top{-w / 2, -h / 2, w, h * 0.38f};
    fx::gradRoundRect(top, 0.9f, lighten(wa, 0.22f), darken(wa, 0.08f));
    // waffle imprint on the wafer edges: little cross-hatched cells
    Color imprint = alpha(darken(wa, 0.45f), 0.45f);
    float cell = std::max(6.0f, h * 0.36f);
    for (float x = -w / 2 + cell; x < w / 2 - cell * 0.6f; x += cell) {
        DrawLineEx({x - cell * 0.35f, top.y + 2}, {x + cell * 0.35f, top.y + top.height - 2}, 1.2f, imprint);
        DrawLineEx({x + cell * 0.35f, top.y + 2}, {x - cell * 0.35f, top.y + top.height - 2}, 1.2f, imprint);
        DrawLineEx({x, bot.y + 2}, {x, bot.y + bot.height - 2}, 1.2f, alpha(imprint, 0.3f));
    }
    // caramel shine + a few drips (positions fixed per width so they don't jitter)
    DrawLineEx({-w / 2 + h * 0.6f, mid.y + mid.height * 0.35f}, {w / 2 - h * 0.6f, mid.y + mid.height * 0.35f}, 1.4f, alpha(rgba(0xffc070ff), 0.6f));
    for (int i = 0; i < 4; i++) {
        float u = std::fmod(std::abs(std::sin((i + 1) * 12.9898f + w * 0.071f) * 43758.5f), 1.0f);
        if (w < 40 && i > 0) break;
        float x = -w / 2 + h * 0.6f + u * (w - h * 1.2f);
        float len = h * (0.18f + 0.18f * std::fmod(u * 7, 1.0f));
        DrawRectangleRounded({x - 2.2f, bot.y - 1, 4.4f, len}, 1, 6, car);
        DrawCircleV({x, bot.y - 1 + len}, 2.6f, car);
    }
    // lit rim along the top
    DrawLineEx({-w / 2 + h * 0.45f, -h / 2 + 1.5f}, {w / 2 - h * 0.45f, -h / 2 + 1.5f}, 1.6f, alpha(WHITE, 0.45f));
}

void stroopwafelTop(Vector2 c, float r) {
    // seen from above: golden disc, embossed rim, the waffle grid, caramel peeking at the edge
    DrawCircleV({c.x + r * 0.06f, c.y + r * 0.1f}, r + 2, alpha(BLACK, 0.35f));
    DrawCircleV(c, r + 2.5f, col::outline);
    DrawCircleV(c, r, rgba(0x8a4610ff));  // caramel edge
    DrawCircleGradient(c, r * 0.94f, lighten(col::wafel, 0.2f), darken(col::wafel, 0.1f));
    DrawRing(c, r * 0.8f, r * 0.94f, 0, 360, 40, alpha(darken(col::wafel, 0.25f), 0.6f));
    Color line = alpha(darken(col::wafel, 0.4f), 0.75f);
    float ri = r * 0.78f;
    for (float k = -0.75f; k <= 0.76f; k += 0.25f) {
        float d = k * ri;
        float half = std::sqrt(std::max(0.0f, ri * ri - d * d));
        Vector2 n1 = Vector2Normalize({1, 1}), t1 = {-n1.y, n1.x};
        Vector2 n2 = Vector2Normalize({1, -1}), t2 = {-n2.y, n2.x};
        DrawLineEx(c + n1 * d - t1 * half, c + n1 * d + t1 * half, std::max(1.2f, r * 0.05f), line);
        DrawLineEx(c + n2 * d - t2 * half, c + n2 * d + t2 * half, std::max(1.2f, r * 0.05f), line);
    }
    DrawCircleSector(c, r * 0.94f, 200, 250, 12, alpha(WHITE, 0.18f));  // sheen
}

void bomb(Vector2 c, float r, float t) {
    DrawCircleV({c.x + r * 0.08f, c.y + r * 0.15f}, r * 1.02f, alpha(BLACK, 0.4f));
    fx::shadedBall(c, r, rgba(0x34344aff), 2.5f);
    // fuse cap + burning fuse
    Rectangle cap{c.x + r * 0.35f, c.y - r * 1.05f, r * 0.45f, r * 0.38f};
    DrawRectangleRounded(cap, 0.3f, 6, rgba(0x6a6a82ff));
    DrawRectangleRoundedLinesEx(cap, 0.3f, 6, 2, col::outline);
    Vector2 f0{c.x + r * 0.58f, c.y - r * 1.05f}, f1{c.x + r * 0.95f, c.y - r * 1.5f};
    DrawSplineSegmentBezierQuadratic(f0, {c.x + r * 0.6f, c.y - r * 1.45f}, f1, 3, rgba(0xc8b090ff));
    float flick = 0.7f + 0.3f * std::sin(t * 30);
    fx::beginAdd();
    fx::glow(f1, r * 0.8f * flick, alpha(col::orange, 0.7f));
    fx::endAdd();
    DrawCircleV(f1, r * 0.2f * flick, col::orange);
    DrawCircleV(f1, r * 0.1f * flick, rgba(0xfff3a0ff));
    for (int i = 0; i < 3; i++) {
        Vector2 sp = f1 + fromAngle(t * 9 + i * 2.1f, r * 0.35f);
        DrawLineEx(f1, sp, 1.5f, alpha(col::gold, 0.8f));
    }
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
    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, 0);
    rlScalef(facing, 1, 1);
    const Color o = col::outline;
    const float ow = 2.6f * s;  // outline width
    Color dark = darken(body, 0.28f), farCol = darken(body, 0.45f);
    auto at = [&](float x, float y) { return Vector2{x * s, y * s + bob}; };

    // ---- legs: hinged upper + lower segment, gallop cycle
    struct Leg { Vector2 hip, knee, foot; float a2; };
    auto legPose = [&](float hx, float hy, float off, bool hind) {
        float p = phase + off;
        float swing = std::sin(p), lift = std::max(0.0f, std::cos(p));
        Leg L;
        L.hip = at(hx, hy);
        float a1 = (90 - swing * 30 + (hind ? 10.0f : -4.0f)) * DEG2RAD;
        L.knee = L.hip + fromAngle(a1, (hind ? 29.0f : 27.0f) * s);
        float bend = hind ? -(18 + lift * 45) : (8 + lift * 75);
        L.a2 = a1 + bend * DEG2RAD;
        L.foot = L.knee + fromAngle(L.a2, 27.0f * s);
        return L;
    };
    auto drawLeg = [&](const Leg& L, bool hind, Color c) {
        float wTop = (hind ? 25.0f : 19.0f) * s, wKnee = 11 * s, wFoot = 8.5f * s;
        taper(L.hip, L.knee, wTop, wKnee, o, ow);
        taper(L.knee, L.foot, wKnee, wFoot, o, ow);
        taper(L.hip, L.knee, wTop, wKnee, c);
        taper(L.knee, L.foot, wKnee, wFoot, c);
        // hoof
        Vector2 d = fromAngle(L.a2, 1), n{-d.y, d.x};
        disc(L.foot, 6 * s + ow, o);  // fetlock
        disc(L.foot, 6 * s, c);
        Vector2 h0 = L.foot + d * (2 * s), h1 = L.foot + d * (10 * s);
        Vector2 q[4] = {h0 + n * (6 * s), h0 - n * (6 * s), h1 - n * (7.5f * s), h1 + n * (7.5f * s)};
        Vector2 qo[4] = {h0 + n * (6 * s + ow) - d * ow, h0 - n * (6 * s + ow) - d * ow, h1 - n * (7.5f * s + ow) + d * ow, h1 + n * (7.5f * s + ow) + d * ow};
        polyFan(qo, 4, o);
        polyFan(q, 4, rgba(0x3a302aff));
        DrawLineEx(h0 + n * (5 * s), h0 - n * (5 * s), 1.5f * s, alpha(WHITE, 0.25f));
    };
    Leg fFar = legPose(28, -54, 0.0f, false), hFar = legPose(-40, -58, PI, true);
    Leg fNear = legPose(36, -52, PI * 0.5f, false), hNear = legPose(-32, -56, PI * 1.5f, true);
    drawLeg(fFar, false, farCol);
    drawLeg(hFar, true, farCol);

    // ---- tail: flowing strands
    float sw = std::sin(phase * 1.3f) * 9;
    Vector2 tb = at(-60, -88);
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 5; i++) {
            Vector2 end = at(-92 + sw + i * 2, -40 + i * 6), ctrl = at(-88 + sw * 0.4f, -92 + i * 4);
            DrawSplineSegmentBezierQuadratic(tb, ctrl, end, (k == 0 ? 7.0f : 4.5f) * s, k == 0 ? o : (i % 2 ? mane : lighten(mane, 0.12f)));
        }

    // ---- body group: one outline pass, then the fills, so it reads as one shape
    // neck: withers -> crest -> poll -> throat -> chest, a curved wedge
    Vector2 neck[8] = {at(14, -92), at(30, -112), at(48, -130), at(66, -144), at(84, -132), at(78, -112), at(62, -88), at(50, -64)};
    Vector2 neckC{};
    for (auto& q : neck) neckC = neckC + q / 8.0f;
    for (int pass = 0; pass < 2; pass++) {
        float g = pass == 0 ? ow : 0;
        Color c = pass == 0 ? o : body;
        ell(at(-34, -76), 28 * s, 25 * s, 0, c, g);  // hindquarters
        ell(at(0, -73), 50 * s, 21 * s, 0, c, g);     // barrel
        ell(at(34, -73), 23 * s, 23 * s, 0, c, g);    // chest
        {
            Vector2 q[9];
            q[0] = neckC;
            for (int i = 0; i < 8; i++) q[i + 1] = neck[i] + Vector2Normalize(neck[i] - neckC) * g;
            for (int i = 1; i < 8; i++) triAny(q[0], q[i], q[i + 1], c);
            triAny(q[0], q[8], q[1], c);
        }
        ell(at(80, -134), 17 * s, 14 * s, 0.25f, c, g);      // skull
        taper(at(82, -130), at(104, -110), 26 * s, 18 * s, c, g);  // face narrowing to the nose
        ell(at(104, -110), 11 * s, 10 * s, 0.7f, c, g);      // muzzle
        triAny(at(70, -144), at(74, -164) + Vector2{0, -g}, at(82, -146), c);  // ear
    }
    // shading: lit back, darker belly and jaw
    ell(at(-6, -86), 42 * s, 6 * s, 0, alpha(WHITE, 0.16f));
    ell(at(0, -59), 40 * s, 7 * s, 0, alpha(BLACK, 0.16f));
    ell(at(-36, -66), 18 * s, 10 * s, 0.4f, alpha(BLACK, 0.10f));
    ell(at(106, -108), 8 * s, 7 * s, 0.7f, darken(body, 0.15f));       // soft nose
    ell(at(78, -124), 10 * s, 8 * s, 0.3f, alpha(BLACK, 0.12f));        // cheek
    ell(at(60, -118), 18 * s, 5 * s, -0.9f, alpha(WHITE, 0.12f));      // light on the crest
    triAny(at(73, -146), at(75, -159), at(79, -147), darken(body, 0.35f));  // inner ear
    DrawSplineSegmentBezierQuadratic(at(102, -102), at(98, -100), at(94, -103), 1.4f * s, alpha(BLACK, 0.5f));  // mouth
    // shoulder muscle line
    DrawSplineSegmentBezierQuadratic(at(30, -88), at(22, -72), at(30, -58), 1.6f * s, alpha(BLACK, 0.25f));
    // face: nostril, eye with a highlight, bridle in racing silk
    ell(at(109, -112), 2.6f * s, 1.8f * s, 0.7f, o);  // nostril
    disc(at(85, -138), 4.6f * s, o);
    disc(at(85, -138), 3.4f * s, rgba(0x2a1a10ff));
    disc(at(84, -139.5f), 1.3f * s, WHITE);
    DrawSplineSegmentBezierQuadratic(at(80, -143), at(85, -146), at(90, -142), 1.3f * s, alpha(BLACK, 0.5f));  // brow
    DrawLineEx(at(72, -132), at(100, -108), 2.4f * s, silk);  // bridle
    DrawLineEx(at(90, -140), at(96, -116), 2.4f * s, silk);
    disc(at(98, -112), 2.2f * s, rgba(0xd8d0e0ff));
    // mane along the top of the neck
    // mane: locks lying along the crest, swaying with the gallop
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) {
            Vector2 a0 = neck[3 - i], a1 = neck[3 - i - 1 < 0 ? 0 : 3 - i - 1];
            Vector2 root = Vector2Lerp(a0, a1, 0.2f);
            float wave = std::sin(phase * 2 + i * 1.3f) * 4 * s;
            Vector2 tip = root + Vector2{-16 * s + wave, 10 * s};
            Vector2 ctrl = root + Vector2{-4 * s, -6 * s};
            DrawSplineSegmentBezierQuadratic(root, ctrl, tip, (k == 0 ? 11.0f : 7.0f) * s, k == 0 ? o : (i % 2 ? mane : lighten(mane, 0.15f)));
        }
    DrawSplineSegmentBezierQuadratic(at(70, -146), at(80, -150), at(84, -138), 6 * s, mane);  // forelock

    drawLeg(hNear, true, dark);
    drawLeg(fNear, false, dark);

    // ---- saddle cloth + jockey ("none" = riderless, e.g. a plush toy)
    Vector2 cloth[4] = {at(-18, -94), at(16, -94), at(18, -70), at(-20, -70)};
    Vector2 clothO[4] = {at(-20, -96), at(18, -96), at(20, -68), at(-22, -68)};
    polyFan(clothO, 4, o);
    polyFan(cloth, 4, silk);
    DrawLineEx(at(-18, -74), at(18, -74), 2 * s, lighten(silk, 0.4f));
    ell(at(-2, -94), 16 * s, 4 * s, 0, rgba(0x3a2418ff));  // saddle
    if (jockey == "none") {
        rlPopMatrix();
        return;
    }
    Vector2 seat = at(0, -98), shoulder = at(12, -124), hand = at(48, -108), knee = at(18, -86), boot = at(10, -70);
    taper(seat, knee, 12 * s, 9 * s, o, ow * 0.8f);
    taper(knee, boot, 9 * s, 8 * s, o, ow * 0.8f);
    taper(seat, knee, 12 * s, 9 * s, rgba(0xf4f0ffff));
    taper(knee, boot, 9 * s, 8 * s, rgba(0x2a1a10ff));
    taper(seat, shoulder, 18 * s, 15 * s, o, ow * 0.8f);
    taper(seat, shoulder, 18 * s, 15 * s, silk);
    DrawLineEx(Vector2Lerp(seat, shoulder, 0.3f) + Vector2{-8 * s, 0}, Vector2Lerp(seat, shoulder, 0.3f) + Vector2{8 * s, 0}, 3 * s, lighten(silk, 0.45f));
    taper(shoulder, hand, 7 * s, 6 * s, o, ow * 0.7f);
    taper(shoulder, hand, 7 * s, 6 * s, darken(silk, 0.1f));
    disc(hand, 3.5f * s, rgba(0xf4f0ffff));
    DrawLineEx(hand, at(92, -116), 1.4f * s, rgba(0x2a1a10ff));  // reins
    if (!jockey.empty()) {
        rlPushMatrix();
        rlScalef(facing, 1, 1);  // undo the flip so the face isn't mirrored
        Vector2 hp{facing * 16 * s, -142 * s + bob};
        if (jockey == "pieter") pieter(hp, 15 * s, P().hat);
        else portrait(jockey, hp, 15 * s);
        rlPopMatrix();
    } else {
        disc(at(16, -140), 12.5f * s, o);
        disc(at(16, -140), 11 * s, rgba(0xe8b48aff));
        // racing cap in the silk colour with a peak
        ell(at(16, -146), 12 * s, 8 * s, 0, silk);
        ell(at(26, -143), 7 * s, 2.5f * s, 0.1f, darken(silk, 0.3f));
        disc(at(21, -139), 1.4f * s, o);
    }
    rlPopMatrix();
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
        // frikandel speciaal in a paper tray: mayo, curry and onions
        Vector2 tray[4] = {{c.x - s * 0.46f, c.y - s * 0.02f}, {c.x + s * 0.46f, c.y - s * 0.02f}, {c.x + s * 0.36f, c.y + s * 0.3f}, {c.x - s * 0.36f, c.y + s * 0.3f}};
        Vector2 trayO[4] = {{tray[0].x - 3, tray[0].y - 3}, {tray[1].x + 3, tray[1].y - 3}, {tray[2].x + 3, tray[2].y + 3}, {tray[3].x - 3, tray[3].y + 3}};
        polyFan(trayO, 4, col::outline);
        polyFan(tray, 4, rgba(0xf4f0e8ff));
        DrawLineEx({c.x - s * 0.42f, c.y + s * 0.06f}, {c.x + s * 0.42f, c.y + s * 0.06f}, 2, rgba(0xd8d0c4ff));
        {
            Local l({c.x, c.y - s * 0.06f}, -8);
            DrawRectangleRounded({-s * 0.48f, -s * 0.12f, s * 0.96f, s * 0.24f}, 1, 12, col::outline);
            fx::gradRoundRect({-s * 0.45f, -s * 0.095f, s * 0.9f, s * 0.19f}, 1, rgba(0xb8683aff), rgba(0x7a3a18ff));
            DrawLineEx({-s * 0.38f, -s * 0.06f}, {s * 0.38f, -s * 0.06f}, 1.5f, alpha(WHITE, 0.3f));
            // mayo zigzag, curry, onions
            for (int i = 0; i < 6; i++) {
                float x0 = -s * 0.36f + i * s * 0.13f;
                DrawLineEx({x0, -s * 0.1f}, {x0 + s * 0.065f, -s * 0.02f}, s * 0.05f, rgba(0xfff8e0ff));
                DrawLineEx({x0 + s * 0.065f, -s * 0.02f}, {x0 + s * 0.13f, -s * 0.1f}, s * 0.05f, rgba(0xfff8e0ff));
            }
            for (int i = 0; i < 5; i++) DrawCircleV({-s * 0.3f + i * s * 0.15f, -s * 0.12f}, s * 0.045f, rgba(0xd8282cff));
            for (int i = 0; i < 9; i++) DrawRectangleRec({-s * 0.34f + (i * 37 % 68) / 100.0f * s, -s * 0.16f + (i % 3) * 3.0f, 4, 4}, rgba(0xf4f0d0ff));
        }
    } else if (id == "kroket") {
        // crispy kroket with a dab of mustard
        Local l(c, -15);
        DrawRectangleRounded({-s * 0.43f, -s * 0.19f, s * 0.86f, s * 0.38f}, 1, 12, col::outline);
        fx::gradRoundRect({-s * 0.4f, -s * 0.16f, s * 0.8f, s * 0.32f}, 1, rgba(0xe89a4aff), rgba(0xa85a20ff));
        for (int i = 0; i < 26; i++) {
            float x = -s * 0.34f + (i * 37 % 68) / 100.0f * s, y = -s * 0.11f + (i * 53 % 22) / 100.0f * s;
            DrawCircleV({x, y}, 1.6f, i % 3 ? rgba(0x8a4210ff) : rgba(0xffc070ff));
        }
        DrawLineEx({-s * 0.3f, -s * 0.11f}, {s * 0.3f, -s * 0.11f}, 2, alpha(WHITE, 0.25f));
        DrawEllipse((int)(s * 0.18f), (int)(-s * 0.13f), s * 0.1f, s * 0.05f, rgba(0xe8c020ff));
    } else if (id == "hagelslag") {
        // a slice of bread, buttered, under a mountain of chocolate sprinkles
        Vector2 o{c.x, c.y + s * 0.04f};
        auto slice = [&](float g, Color col) {
            DrawRectangleRounded({o.x - s * 0.38f - g, o.y - s * 0.16f - g, s * 0.76f + 2 * g, s * 0.52f + 2 * g}, 0.2f, 10, col);
            DrawCircleV({o.x - s * 0.2f, o.y - s * 0.2f}, s * 0.2f + g, col);
            DrawCircleV({o.x + s * 0.2f, o.y - s * 0.2f}, s * 0.2f + g, col);
        };
        slice(3, col::outline);
        slice(0, rgba(0xb8783aff));             // crust
        Vector2 o2 = o;
        o = {o2.x, o2.y + 1};
        float k = 0.86f;
        DrawRectangleRounded({o.x - s * 0.38f * k, o.y - s * 0.14f, s * 0.76f * k, s * 0.46f}, 0.2f, 10, rgba(0xf8e8c0ff));
        DrawCircleV({o.x - s * 0.18f, o.y - s * 0.18f}, s * 0.17f, rgba(0xf8e8c0ff));
        DrawCircleV({o.x + s * 0.18f, o.y - s * 0.18f}, s * 0.17f, rgba(0xf8e8c0ff));
        for (int i = 0; i < 70; i++) {
            float u = std::fmod(std::abs(std::sin(i * 12.9898f) * 43758.5f), 1.0f), v = std::fmod(std::abs(std::sin(i * 78.233f) * 12345.6f), 1.0f);
            float x = o.x + (u - 0.5f) * s * 0.6f, y = o.y - s * 0.22f + v * s * 0.5f;
            Vector2 d = fromAngle(u * 9 + v * 4, s * 0.035f);
            DrawLineEx(Vector2{x, y} - d, Vector2{x, y} + d, s * 0.03f, i % 5 ? rgba(0x3b2112ff) : rgba(0x6a3a1aff));
        }
    } else if (id.rfind("hat_", 0) == 0) {
        // shown on a mini Pieter
        pieter({c.x, c.y + s * 0.12f}, s * 0.3f, id);
    } else if (id.rfind("trail_", 0) == 0) {
        // a little Pieter zooming right, the trail streaming behind him
        Vector2 head{c.x + s * 0.24f, c.y};
        if (id == "trail_rainbow") {
            const Color band[6] = {rgba(0xff4a5aff), rgba(0xff8a2bff), rgba(0xffc531ff), rgba(0x3ddc84ff), rgba(0x3cd6ffff), rgba(0x8f5cffff)};
            for (int i = 0; i < 6; i++) {
                float y = head.y - s * 0.15f + i * s * 0.06f;
                Vector2 a{head.x - s * 0.1f, y}, b{c.x - s * 0.46f, y + std::sin(i * 0.8f) * 3};
                DrawSplineSegmentBezierQuadratic(a, {(a.x + b.x) / 2, y - s * 0.08f}, b, s * 0.07f, band[i]);
            }
        } else if (id == "trail_fire") {
            fx::beginAdd();
            fx::glow({c.x - s * 0.1f, c.y}, s * 0.5f, alpha(col::orange, 0.4f));
            fx::endAdd();
            for (int i = 0; i < 4; i++) {
                float x = head.x - s * 0.12f - i * s * 0.17f, r = s * (0.17f - i * 0.03f);
                Vector2 tip{x - r * 2.2f, head.y - r * 0.3f + std::sin(i * 1.7f) * r * 0.4f};
                triAny({x, head.y - r}, {x, head.y + r}, tip, mix(col::gold, col::red, i / 3.0f));
                DrawCircleV({x, head.y}, r, mix(col::gold, col::red, i / 3.0f));
                DrawCircleV({x + r * 0.2f, head.y}, r * 0.5f, rgba(0xfff3a0ff));
            }
        } else {
            for (int i = 0; i < 5; i++) {
                Vector2 p{head.x - s * 0.18f - i * s * 0.14f, head.y + std::sin(i * 1.4f) * s * 0.08f};
                fx::shadedBall(p, s * (0.08f - i * 0.008f), rgba(0x3a2418ff), 2);
            }
        }
        pieter(head, s * 0.2f, "");
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
