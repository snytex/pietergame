#include "engine/Fx.hpp"

#include "engine/Engine.hpp"
#include "engine/Ui.hpp"

#include <algorithm>

#include <rlgl.h>
#include <unordered_map>

namespace pg::fx {

namespace {
Texture2D g_glow{};
std::unordered_map<std::string, Texture2D> g_blurred;
}  // namespace

void init() {
    // radial falloff, brighter core: looks like bloom when drawn additively
    const int n = 128;
    Image img = GenImageColor(n, n, BLANK);
    auto* px = (Color*)img.data;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = clamp01(1 - d);
            a = a * a * (0.55f + 0.45f * a * a);
            px[y * n + x] = {255, 255, 255, (unsigned char)(a * 255)};
        }
    g_glow = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&g_glow);
    SetTextureFilter(g_glow, TEXTURE_FILTER_TRILINEAR);
}

void shutdown() {
    UnloadTexture(g_glow);
    for (auto& [k, t] : g_blurred) UnloadTexture(t);
    g_blurred.clear();
}

void beginAdd() { BeginBlendMode(BLEND_ADDITIVE); }
void endAdd() { EndBlendMode(); }

void glow(Vector2 c, float radius, Color color) {
    DrawTexturePro(g_glow, {0, 0, (float)g_glow.width, (float)g_glow.height}, {c.x, c.y, radius * 2, radius * 2},
                   {radius, radius}, 0, color);
}

void glowLine(Vector2 a, Vector2 b, float width, Color color) {
    Vector2 d = b - a;
    float len = Vector2Length(d);
    if (len < 1) return glow(a, width, color);
    // stretched glow sprite along the segment
    DrawTexturePro(g_glow, {0, 0, (float)g_glow.width, (float)g_glow.height},
                   {(a.x + b.x) / 2, (a.y + b.y) / 2, len + width * 2, width * 2}, {(len + width * 2) / 2, width},
                   angleOf(d) * RAD2DEG, color);
}

void softShadow(Vector2 c, float rx, float ry, float strength) {
    DrawTexturePro(g_glow, {0, 0, (float)g_glow.width, (float)g_glow.height}, {c.x, c.y, rx * 2, ry * 2}, {rx, ry}, 0,
                   alpha(BLACK, strength));
}

void shadedBall(Vector2 c, float r, Color base, float outline) {
    if (outline > 0) DrawCircleV(c, r + outline, col::outline);
    DrawCircleV(c, r, darken(base, 0.35f));
    // lit body, offset toward the light
    DrawCircleV({c.x - r * 0.08f, c.y - r * 0.1f}, r * 0.88f, base);
    DrawCircleGradient({c.x - r * 0.3f, c.y - r * 0.35f}, r * 0.75f, lighten(base, 0.35f), alpha(base, 0));
    // specular + rim
    DrawCircleV({c.x - r * 0.38f, c.y - r * 0.42f}, r * 0.16f, alpha(WHITE, 0.8f));
    DrawRing(c, r * 0.86f, r, 25, 110, 16, alpha(lighten(base, 0.5f), 0.35f));
}

void shadedRoundRect(Rectangle r, float round, Color base, float outline) {
    if (outline > 0) DrawRectangleRounded(shrink(r, -outline), round, 10, col::outline);
    DrawRectangleRounded(r, round, 10, darken(base, 0.3f));
    DrawRectangleRounded({r.x, r.y, r.width, r.height * 0.88f}, round, 10, base);
    DrawRectangleRounded({r.x + r.width * 0.08f, r.y + r.height * 0.06f, r.width * 0.84f, r.height * 0.32f}, round, 10,
                         alpha(lighten(base, 0.6f), 0.35f));
}

void tiled(Texture2D& t, Rectangle dest, float scale, Vector2 scroll, Color tint) {
    if (t.id == 0 || scale <= 0) return;
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    DrawTexturePro(t, {scroll.x, scroll.y, dest.width / scale, dest.height / scale}, dest, {0, 0}, 0, tint);
}

void texQuad(Texture2D& t, const Vector2 p[4], const Vector2 uv[4], Color tint) {
    if (t.id == 0) return;
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    // raylib quads: tl, bl, br, tr
    const int order[4] = {0, 3, 2, 1};
    for (int k : order) {
        rlTexCoord2f(uv[k].x / t.width, uv[k].y / t.height);
        rlVertex2f(p[k].x, p[k].y);
    }
    // and the other winding, in case this one is culled
    const int order2[4] = {0, 1, 2, 3};
    for (int k : order2) {
        rlTexCoord2f(uv[k].x / t.width, uv[k].y / t.height);
        rlVertex2f(p[k].x, p[k].y);
    }
    rlEnd();
    rlSetTexture(0);
}

void sprite(Texture2D& t, Vector2 c, float scale, float rotDeg, Color tint, bool flipX) {
    if (t.id == 0) return;
    float w = t.width * scale, h = t.height * scale;
    DrawTexturePro(t, {0, 0, flipX ? -(float)t.width : (float)t.width, (float)t.height}, {c.x, c.y, w, h}, {w / 2, h / 2}, rotDeg, tint);
}

void gradRoundRect(Rectangle r, float roundness, Color top, Color bottom) {
    if (r.width <= 0 || r.height <= 0) return;
    float rad = std::min(roundness * std::min(r.width, r.height) / 2, std::min(r.width, r.height) / 2);
    const int seg = std::clamp((int)(rad / 2), 3, 12);
    // perimeter, corner by corner (clockwise on screen)
    Vector2 pts[4 * 13];
    int n = 0;
    const Vector2 cs[4] = {{r.x + r.width - rad, r.y + rad}, {r.x + r.width - rad, r.y + r.height - rad}, {r.x + rad, r.y + r.height - rad}, {r.x + rad, r.y + rad}};
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= seg; i++) {
            float a = (-90 + c * 90 + 90.0f * i / seg) * DEG2RAD;
            pts[n++] = {cs[c].x + std::cos(a) * rad, cs[c].y + std::sin(a) * rad};
        }
    auto colAt = [&](float y) { return mix(top, bottom, clamp01((y - r.y) / r.height)); };
    Vector2 mid{r.x + r.width / 2, r.y + r.height / 2};
    Color cm = colAt(mid.y);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < n; i++) {
        Vector2 a = pts[i], b = pts[(i + 1) % n];
        Color ca = colAt(a.y), cb = colAt(b.y);
        for (int w = 0; w < 2; w++) {  // both windings: whichever survives culling draws
            rlColor4ub(cm.r, cm.g, cm.b, cm.a); rlVertex2f(mid.x, mid.y);
            Vector2 p = w ? b : a, q = w ? a : b;
            Color cp = w ? cb : ca, cq = w ? ca : cb;
            rlColor4ub(cp.r, cp.g, cp.b, cp.a); rlVertex2f(p.x, p.y);
            rlColor4ub(cq.r, cq.g, cq.b, cq.a); rlVertex2f(q.x, q.y);
        }
    }
    rlEnd();
}

void dashedRing(Vector2 c, float r, float thick, int dashes, float rot, Color color) {
    float step = 360.0f / dashes;
    for (int i = 0; i < dashes; i++) {
        float a = rot + i * step;
        DrawRing(c, r - thick / 2, r + thick / 2, a, a + step * 0.55f, 6, color);
    }
}

void gradientText(const std::string& s, float x, float y, float size, Color top, Color bottom, float outline) {
    // outline + drop shadow
    for (int i = 0; i < 16; i++) {
        Vector2 o = fromAngle(i * PI / 8, outline);
        ui::text(s, x + o.x, y + o.y + outline * 0.8f, size, col::outline, ui::Center, true);
    }
    ui::text(s, x, y, size, bottom, ui::Center, true);
    // The gradient is faked by redrawing the top half through a scissor rect.
    // Scissor works in canvas pixels, so map the rect through the current rlgl
    // transform; rotated text just keeps the solid colour.
    Matrix m = rlGetMatrixTransform();
    if (std::abs(m.m1) > 1e-4f || std::abs(m.m4) > 1e-4f) return;
    float w = ui::measure(s, size, true);
    auto clip = [&](float top0, float h) {
        Vector3 a = Vector3Transform({x - w / 2 - 4, top0, 0}, m), b = Vector3Transform({x + w / 2 + 4, top0 + h, 0}, m);
        BeginScissorMode((int)std::floor(std::min(a.x, b.x)), (int)std::floor(std::min(a.y, b.y)), (int)std::ceil(std::abs(b.x - a.x)),
                         (int)std::ceil(std::abs(b.y - a.y)));
    };
    clip(y - size * 0.62f, size * 0.6f);
    ui::text(s, x, y, size, top, ui::Center, true);
    EndScissorMode();
    clip(y - size * 0.62f, size * 0.14f);
    ui::text(s, x, y, size, lighten(top, 0.6f), ui::Center, true);
    EndScissorMode();
}

Texture2D& blurred(const std::string& name) {
    auto it = g_blurred.find(name);
    if (it != g_blurred.end()) return it->second;
    Image img = LoadImage(E().assetPath("images/" + name + ".png").c_str());
    if (img.data) {
        ImageResize(&img, img.width / 4, img.height / 4);
        ImageBlurGaussian(&img, 6);
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return g_blurred[name] = t;
}

void blurredBackdrop(const std::string& name, float dim, float t) {
    Texture2D& tex = blurred(name);
    float sc = std::max((float)VW / tex.width, (float)VH / tex.height) * 1.12f;
    float w = tex.width * sc, h = tex.height * sc;
    Vector2 drift{std::sin(t * 0.07f) * 30, std::cos(t * 0.05f) * 18};
    DrawTexturePro(tex, {0, 0, (float)tex.width, (float)tex.height}, {(VW - w) / 2 + drift.x, (VH - h) / 2 + drift.y, w, h},
                   {0, 0}, 0, WHITE);
    DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, dim));
}

void bokeh(float t, int count, Color c, float seed) {
    beginAdd();
    for (int i = 0; i < count; i++) {
        float k = i * 12.9898f + seed * 7.13f;
        float x = std::fmod(std::abs(std::sin(k) * 43758.5f), 1.0f) * VW;
        float y0 = std::fmod(std::abs(std::sin(k * 1.7f) * 24634.6f), 1.0f) * VH;
        float speed = 6 + std::fmod(std::abs(std::sin(k * 3.1f) * 9341.3f), 1.0f) * 14;
        float y = VH + 80 - std::fmod(VH + 160 - y0 + t * speed, (float)VH + 160);
        float r = 18 + std::fmod(std::abs(std::sin(k * 5.3f) * 1234.5f), 1.0f) * 50;
        float tw = 0.5f + 0.5f * std::sin(t * 0.8f + i);
        glow({x + std::sin(t * 0.3f + i) * 20, y}, r, alpha(c, 0.10f + 0.12f * tw));
    }
    endAdd();
}

void Shocks::update(float dt) {
    for (auto& s : list_) s.t += dt;
    std::erase_if(list_, [](auto& s) { return s.t >= s.life; });
}

void Shocks::draw() const {
    for (auto& s : list_) {
        float k = easeOutCubic(s.t / s.life);
        float r = lerpf(s.r0, s.r1, k);
        float th = s.thick * (1 - k) + 1;
        DrawRing(s.pos, std::max(0.0f, r - th), r, 0, 360, 48, alpha(s.color, (1 - k) * 0.9f));
    }
    beginAdd();
    for (auto& s : list_) {
        float k = easeOutCubic(s.t / s.life);
        float r = lerpf(s.r0, s.r1, k);
        DrawRing(s.pos, std::max(0.0f, r - s.thick * 2.5f * (1 - k)), r + 4, 0, 360, 48, alpha(s.color, (1 - k) * 0.35f));
    }
    endAdd();
}

}  // namespace pg::fx
