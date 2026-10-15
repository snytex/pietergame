// Core helpers shared by the whole engine: virtual resolution, math, RNG, colors.
#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <random>
#include <string>

namespace pg {

// Everything is rendered at this resolution and scaled to the window.
constexpr int VW = 1280;
constexpr int VH = 720;

// ---------------------------------------------------------------- random
std::mt19937& rng();
float rnd();                        // [0, 1)
float rnd(float lo, float hi);      // [lo, hi)
int rndi(int lo, int hi);           // [lo, hi] inclusive
bool chance(float p);
Vector2 rndDir();                   // unit vector, random angle

// ---------------------------------------------------------------- math
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float approach(float cur, float target, float step) {
    if (cur < target) return std::fmin(cur + step, target);
    return std::fmax(cur - step, target);
}
// Frame-rate independent exponential smoothing.
inline float damp(float cur, float target, float speed, float dt) {
    return lerpf(cur, target, 1.0f - std::exp(-speed * dt));
}
inline Vector2 damp(Vector2 cur, Vector2 target, float speed, float dt) {
    float t = 1.0f - std::exp(-speed * dt);
    return {lerpf(cur.x, target.x, t), lerpf(cur.y, target.y, t)};
}
inline float easeOutCubic(float t) { t = clamp01(t); return 1 - std::pow(1 - t, 3.0f); }
inline float easeInCubic(float t) { t = clamp01(t); return t * t * t; }
inline float easeInOutSine(float t) { t = clamp01(t); return -(std::cos(PI * t) - 1) / 2; }
inline float easeOutBack(float t) {
    t = clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
}
inline float easeOutElastic(float t) {
    t = clamp01(t);
    if (t == 0 || t == 1) return t;
    return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * (2 * PI / 3)) + 1;
}
inline Vector2 fromAngle(float rad, float len = 1) { return {std::cos(rad) * len, std::sin(rad) * len}; }
inline float angleOf(Vector2 v) { return std::atan2(v.y, v.x); }
inline Rectangle shrink(Rectangle r, float by) { return {r.x + by, r.y + by, r.width - 2 * by, r.height - 2 * by}; }
inline Vector2 center(Rectangle r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// ---------------------------------------------------------------- colors
inline Color rgba(uint32_t hex) {
    return {(unsigned char)(hex >> 24), (unsigned char)(hex >> 16), (unsigned char)(hex >> 8), (unsigned char)hex};
}
inline Color alpha(Color c, float a) { c.a = (unsigned char)(clamp01(a) * c.a); return c; }
inline Color mix(Color a, Color b, float t) {
    t = clamp01(t);
    return {(unsigned char)lerpf(a.r, b.r, t), (unsigned char)lerpf(a.g, b.g, t),
            (unsigned char)lerpf(a.b, b.b, t), (unsigned char)lerpf(a.a, b.a, t)};
}
inline Color lighten(Color c, float t) { return mix(c, {255, 255, 255, c.a}, t); }
inline Color darken(Color c, float t) { return mix(c, {0, 0, 0, c.a}, t); }

namespace col {
inline const Color bg       = rgba(0x1a0f2eff);
inline const Color bgDeep   = rgba(0x0f0820ff);
inline const Color purple   = rgba(0x6b3fd1ff);
inline const Color violet   = rgba(0x8f5cffff);
inline const Color panel    = rgba(0x24163fee);
inline const Color panelHi  = rgba(0x35225cff);
inline const Color outline  = rgba(0x0a0612ff);
inline const Color text     = rgba(0xf4ecffff);
inline const Color muted    = rgba(0xa99cc4ff);
inline const Color gold     = rgba(0xffc531ff);
inline const Color goldDark = rgba(0xc7861aff);
inline const Color orange   = rgba(0xff8a2bff);
inline const Color red      = rgba(0xff4a5aff);
inline const Color green    = rgba(0x3ddc84ff);
inline const Color cyan     = rgba(0x3cd6ffff);
inline const Color pink     = rgba(0xff6fb5ff);
inline const Color boba     = rgba(0xd9a066ff);
inline const Color caramel  = rgba(0xb8651fff);
inline const Color wafel    = rgba(0xe0a64aff);
}  // namespace col

// "12,345"
std::string fmtNum(long long n);

}  // namespace pg
