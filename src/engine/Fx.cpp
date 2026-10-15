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
