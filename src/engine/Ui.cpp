#include "engine/Ui.hpp"

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace pg::ui {

namespace {
// Per-widget hover animation, keyed by the widget rectangle.
std::unordered_map<uint64_t, float> g_hover;
std::unordered_map<uint64_t, bool> g_wasHovered;

uint64_t key(Rectangle r) {
    auto q = [](float f) { return (uint64_t)(int64_t)std::lround(f) & 0xffff; };
    return q(r.x) | q(r.y) << 16 | q(r.width) << 32 | q(r.height) << 48;
}

float hoverAnim(Rectangle r, bool hot) {
    auto k = key(r);
    float& h = g_hover[k];
    h = approach(h, hot ? 1.0f : 0.0f, GetFrameTime() * 8);
    bool& was = g_wasHovered[k];
    if (hot && !was) E().sfx.play(Sfx::Hover, 0.5f);
    was = hot;
    return h;
}

Font& fontFor(float size, bool big) { return E().font(big || size > 44); }
}  // namespace

void resetHover() {
    g_hover.clear();
    g_wasHovered.clear();
}

float measure(const std::string& s, float size, bool big) {
    return MeasureTextEx(fontFor(size, big), s.c_str(), size, size * 0.02f).x;
}

void text(const std::string& s, float x, float y, float size, Color c, Align a, bool big) {
    Font& f = fontFor(size, big);
    Vector2 m = MeasureTextEx(f, s.c_str(), size, size * 0.02f);
    float px = a == Left ? x : (a == Center ? x - m.x / 2 : x - m.x);
    DrawTextEx(f, s.c_str(), {std::round(px), std::round(y - size * 0.55f)}, size, size * 0.02f, c);
}

void textShadow(const std::string& s, float x, float y, float size, Color c, Align a, bool big, float off, Color shadow) {
    text(s, x + off, y + off, size, alpha(shadow, c.a / 255.0f), a, big);
    text(s, x, y, size, c, a, big);
}

void textOutline(const std::string& s, float x, float y, float size, Color c, Color outline, float thick, Align a, bool big) {
    for (int i = 0; i < 12; i++) {
        Vector2 o = fromAngle(i * PI / 6, thick);
        text(s, x + o.x, y + o.y, size, outline, a, big);
    }
    text(s, x, y, size, c, a, big);
}

void textRotated(const std::string& s, Vector2 pos, float size, float deg, Color c, bool big) {
    Font& f = fontFor(size, big);
    Vector2 m = MeasureTextEx(f, s.c_str(), size, size * 0.02f);
    DrawTextPro(f, s.c_str(), pos, {m.x / 2, m.y / 2}, deg, size, size * 0.02f, c);
}

float paragraph(const std::string& s, Rectangle box, float size, Color c, bool draw) {
    std::istringstream in(s);
    std::string word, line;
    float y = box.y + size * 0.6f;
    float lineH = size * 1.25f;
    auto flush = [&] {
        if (draw) text(line, box.x, y, size, c);
        y += lineH;
        line.clear();
    };
    while (in >> word) {
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (measure(tryLine, size) > box.width && !line.empty()) {
            flush();
            line = word;
        } else {
            line = tryLine;
        }
    }
    if (!line.empty()) flush();
    return y - box.y - size * 0.6f;
}

bool hovered(Rectangle r) { return CheckCollisionPointRec(E().mouse(), r) && !E().transitioning(); }

// corner radius in px -> raylib roundness for this rectangle
float roundFor(Rectangle r, float radius) { return std::min(1.0f, 2 * radius / std::max(1.0f, std::min(r.width, r.height))); }

// The shared "pressable" look: soft shadow, a darker bevel underneath, a gradient
// face with a lit top edge and a crisp outline.
void drawPressable(Rectangle d, float radius, Color c, float depth, float hover, bool enabled) {
    float round = roundFor(d, radius);
    DrawRectangleRounded({d.x + 1, d.y + depth + 3, d.width, d.height}, round, 12, alpha(BLACK, 0.28f));
    DrawRectangleRounded({d.x, d.y + depth, d.width, d.height}, round, 12, darken(c, 0.5f));
    if (hover > 0.01f && enabled) {
        fx::beginAdd();
        fx::glow({d.x + d.width / 2, d.y + d.height / 2}, std::max(d.width, d.height) * 0.75f, alpha(c, 0.22f * hover));
        fx::endAdd();
    }
    fx::gradRoundRect(d, round, lighten(c, 0.16f + hover * 0.08f), darken(c, 0.12f));
    float rad = round * std::min(d.width, d.height) / 2;
    DrawLineEx({d.x + rad, d.y + 1.6f}, {d.x + d.width - rad, d.y + 1.6f}, 1.6f, alpha(WHITE, enabled ? 0.35f : 0.12f));
    DrawRectangleRoundedLinesEx(d, round, 12, 2, alpha(col::outline, 0.9f));
}

bool button(Rectangle r, const std::string& label, Color base, float fontSize, bool enabled) {
    bool hot = enabled && hovered(r);
    float h = hoverAnim(r, hot);
    bool down = hot && E().mouseDown();
    Color c = enabled ? base : mix(base, rgba(0x2e2840ff), 0.75f);
    float depth = down ? 1.5f : 4;
    Rectangle d{r.x, r.y + (down ? 2.5f : -h * 1.5f), r.width, r.height};
    drawPressable(d, std::min(12.0f, r.height * 0.32f), c, depth, h, enabled);
    textShadow(label, d.x + d.width / 2, d.y + d.height / 2, fontSize, enabled ? col::text : alpha(col::muted, 0.8f), Center, false, 2,
               alpha(darken(c, 0.7f), 0.9f));
    if (hot && E().mousePressed()) {
        E().consumeClick();
        E().sfx.play(Sfx::Click);
        return true;
    }
    return false;
}

bool bigButton(Rectangle r, const std::string& label, const std::string& sub, Color base, float fontSize) {
    bool hot = hovered(r);
    float h = hoverAnim(r, hot);
    bool down = hot && E().mouseDown();
    Rectangle d{r.x, r.y + (down ? 3.0f : -h * 3), r.width, r.height};
    drawPressable(d, std::min(18.0f, r.height * 0.28f), base, down ? 2 : 6, h, true);
    // a slow sheen sliding across while hovered
    if (h > 0.01f) {
        float k = std::fmod((float)GetTime() * 0.6f, 1.6f) - 0.3f;
        float x = d.x + d.width * k;
        float w = std::min(60.0f, d.width * 0.15f);
        float x0 = std::max(d.x + 8, x), x1 = std::min(d.x + d.width - 8, x + w);
        if (x1 > x0) DrawRectangleGradientH((int)x0, (int)d.y + 4, (int)(x1 - x0), (int)d.height - 8, alpha(WHITE, 0), alpha(WHITE, 0.10f * h));
    }
    float cy = d.y + d.height / 2 - (sub.empty() ? 0 : 10);
    textShadow(label, d.x + d.width / 2, cy, fontSize, col::text, Center, true, 3, alpha(darken(base, 0.75f), 0.9f));
    if (!sub.empty()) text(sub, d.x + d.width / 2, cy + fontSize * 0.75f, 17, alpha(col::text, 0.85f), Center);
    if (hot && E().mousePressed()) {
        E().consumeClick();
        E().sfx.play(Sfx::Click);
        return true;
    }
    return false;
}

bool backButton() {
    bool esc = E().backPressed();
    Rectangle r{20, 16, 124, 44};
    bool hot = hovered(r);
    float h = hoverAnim(r, hot);
    Rectangle d{r.x - h * 2, r.y, r.width, r.height};
    drawPressable(d, 22, col::panelHi, 3, h, true);
    Vector2 c{d.x + 26, d.y + d.height / 2};
    DrawLineEx({c.x + 5, c.y - 8}, {c.x - 3, c.y}, 3.5f, col::text);
    DrawLineEx({c.x - 3, c.y}, {c.x + 5, c.y + 8}, 3.5f, col::text);
    DrawCircleV({c.x - 3, c.y}, 1.75f, col::text);
    text("BACK", d.x + 44, d.y + d.height / 2, 20, col::text, Left, true);
    if ((hot && E().mousePressed()) || esc) {
        if (hot) E().consumeClick();
        E().sfx.play(Sfx::Click);
        return true;
    }
    return false;
}

bool slider(Rectangle r, float& value, Color c) {
    static const void* dragging = nullptr;
    bool hot = hovered({r.x - 10, r.y - 12, r.width + 20, r.height + 24});
    float h = hoverAnim({r.x - 10, r.y - 12, r.width + 20, r.height + 24}, hot || dragging == &value);
    if (hot && E().mousePressed()) { dragging = &value; E().consumeClick(); }
    if (!E().mouseDown()) dragging = nullptr;
    float old = value;
    if (dragging == &value) value = clamp01((E().mouse().x - r.x) / r.width);
    // inset track
    fx::gradRoundRect(r, 1, alpha(BLACK, 0.55f), col::bgDeep);
    if (value > 0.001f) {
        Rectangle f{r.x, r.y, std::max(r.height, r.width * value), r.height};
        fx::gradRoundRect(f, 1, lighten(c, 0.25f), darken(c, 0.1f));
        DrawLineEx({f.x + r.height / 2, f.y + 2}, {f.x + f.width - r.height / 2, f.y + 2}, 1.5f, alpha(WHITE, 0.35f));
    }
    DrawRectangleRoundedLinesEx(r, 1, 12, 1.5f, alpha(col::outline, 0.8f));
    Vector2 k{r.x + r.width * value, r.y + r.height / 2};
    float kr = r.height * 1.05f + h * 2;
    DrawCircleV({k.x, k.y + 3}, kr, alpha(BLACK, 0.3f));
    DrawCircleV(k, kr, WHITE);
    DrawCircleV({k.x, k.y + kr * 0.15f}, kr * 0.8f, rgba(0xe8e0f4ff));
    DrawCircleV(k, kr * 0.38f, c);
    ui::circleOutline(k, kr, col::outline, 2);
    return old != value;
}

bool toggle(Rectangle r, const std::string& label, bool& value) {
    bool hot = hovered(r);
    float h = hoverAnim(r, hot);
    float fs = std::min(22.0f, r.height * 0.44f);
    text(label, r.x, r.y + r.height / 2, fs, alpha(col::text, 0.85f + 0.15f * h), Left, true);
    Rectangle sw{r.x + r.width - 62, r.y + r.height / 2 - 15, 62, 30};
    // knob position eases between the ends
    static std::unordered_map<const void*, float> anim;
    float& a = anim[&value];
    a = approach(a, value ? 1.0f : 0.0f, GetFrameTime() * 8);
    Color on = col::green, off = rgba(0x1a1230ff);
    Color tc = mix(off, on, a);
    if (a > 0.01f) {
        fx::beginAdd();
        fx::glow(center(sw), 60, alpha(on, 0.25f * a));
        fx::endAdd();
    }
    fx::gradRoundRect(sw, 1, a > 0.5f ? lighten(tc, 0.1f) : alpha(BLACK, 0.5f), a > 0.5f ? darken(tc, 0.15f) : tc);
    DrawRectangleRoundedLinesEx(sw, 1, 12, 2, alpha(col::outline, 0.9f));
    Vector2 k{lerpf(sw.x + 15, sw.x + sw.width - 15, easeInOutSine(a)), sw.y + 15};
    DrawCircleV({k.x, k.y + 2}, 12, alpha(BLACK, 0.3f));
    DrawCircleV(k, 12, WHITE);
    DrawCircleV({k.x, k.y + 2}, 9.5f, rgba(0xe8e0f4ff));
    if (hot && E().mousePressed()) {
        E().consumeClick();
        E().sfx.play(Sfx::Click);
        value = !value;
        return true;
    }
    return false;
}

void panel(Rectangle r, Color c, float round, Color border) {
    // card: soft drop shadow, gentle top-to-bottom gradient, lit top edge, thin rim
    fx::softShadow({r.x + r.width / 2, r.y + r.height / 2 + 12}, r.width * 0.6f, r.height * 0.6f, 0.4f * c.a / 255.0f);
    fx::gradRoundRect(r, round, lighten(c, 0.06f), darken(c, 0.22f));
    float rad = round * std::min(r.width, r.height) / 2;
    DrawLineEx({r.x + rad, r.y + 1.6f}, {r.x + r.width - rad, r.y + 1.6f}, 1.6f, alpha(WHITE, 0.10f));
    DrawRectangleRoundedLinesEx(shrink(r, 2), round, 12, 1, alpha(WHITE, 0.04f));
    DrawRectangleRoundedLinesEx(r, round, 12, 2, border);
}

void bar(Rectangle r, float frac, Color fill, Color back) {
    fx::gradRoundRect(r, 1, alpha(BLACK, 0.5f), back);
    if (frac > 0.001f) {
        Rectangle f{r.x, r.y, std::max(r.height, r.width * clamp01(frac)), r.height};
        fx::gradRoundRect(f, 1, lighten(fill, 0.22f), darken(fill, 0.12f));
        DrawLineEx({f.x + r.height / 2, f.y + r.height * 0.22f}, {f.x + f.width - r.height / 2, f.y + r.height * 0.22f}, std::max(1.0f, r.height * 0.14f),
                   alpha(WHITE, 0.3f));
    }
    DrawRectangleRoundedLinesEx(r, 1, 10, 1.5f, alpha(col::outline, 0.85f));
}

void coin(Vector2 c, float radius) {
    DrawCircleV({c.x, c.y + radius * 0.15f}, radius, col::goldDark);
    DrawCircleV(c, radius, col::gold);
    DrawCircleV(c, radius * 0.72f, lighten(col::gold, 0.15f));
    ui::circleOutline(c, radius, col::outline);
    text("P", c.x, c.y, radius * 1.2f, col::goldDark, Center, true);
}

void circleOutline(Vector2 c, float radius, Color color, float thick) {
    DrawRing(c, radius - thick / 2, radius + thick / 2, 0, 360, std::max(16, (int)radius), color);
}

void heart(Vector2 c, float s, Color color) {
    float r = s * 0.3f;
    DrawCircleV({c.x - r * 0.95f, c.y - r * 0.3f}, r * 1.05f, color);
    DrawCircleV({c.x + r * 0.95f, c.y - r * 0.3f}, r * 1.05f, color);
    DrawTriangle({c.x - s * 0.6f, c.y - r * 0.05f}, {c.x, c.y + s * 0.55f}, {c.x + s * 0.6f, c.y - r * 0.05f}, color);
}

void coinCounter(float rightX, float y, long long amount) {
    static float shown = -1;
    if (shown < 0 || std::abs(shown - amount) > 1e7) shown = (float)amount;
    shown = damp(shown, (float)amount, 10, GetFrameTime());
    if (std::abs(shown - amount) < 1) shown = (float)amount;
    std::string s = fmtNum((long long)std::llround(shown));
    float w = measure(s, 28) + 70;
    Rectangle r{rightX - w, y - 22, w, 44};
    DrawRectangleRounded(r, 1, 12, alpha(col::bgDeep, 0.85f));
    DrawRectangleRoundedLinesEx(r, 1, 12, 2, col::goldDark);
    coin({r.x + 24, y}, 15);
    text(s, rightX - 18, y, 28, col::gold, Right);
}

void titleBar(const std::string& title, Color accent) {
    DrawRectangleGradientV(0, 0, VW, 90, alpha(col::bgDeep, 0.92f), alpha(col::bgDeep, 0.0f));
    float w = measure(title, 42, true);
    // accent: a glowing bar under the title with fading rules either side
    fx::beginAdd();
    fx::glow({VW / 2.0f, 66}, w * 0.5f, alpha(accent, 0.18f));
    fx::endAdd();
    DrawRectangleRounded({VW / 2.0f - 28, 63, 56, 5}, 1, 8, accent);
    DrawRectangleGradientH((int)(VW / 2.0f - 28 - 120), 65, 112, 1, alpha(accent, 0), alpha(accent, 0.6f));
    DrawRectangleGradientH((int)(VW / 2.0f + 36), 65, 112, 1, alpha(accent, 0.6f), alpha(accent, 0));
    textShadow(title, VW / 2.0f, 34, 42, col::text, Center, true, 3, alpha(darken(accent, 0.6f), 0.95f));
}

bool betSelector(Rectangle r, long long& bet, long long maxBet, bool enabled) {
    long long old = bet;
    panel(r, col::panel, 0.25f);
    text("BET", r.x + 16, r.y + 22, 18, col::muted);
    coin({r.x + 30, r.y + 58}, 13);
    text(fmtNum(bet), r.x + 52, r.y + 58, 30, col::gold);
    float bx = r.x + r.width - 4 * 58 - 10;
    struct Q { const char* l; int op; } qs[] = {{"1/2", 0}, {"x2", 1}, {"+10", 2}, {"MAX", 3}};
    for (int i = 0; i < 4; i++) {
        if (button({bx + i * 58, r.y + 14, 52, 34}, qs[i].l, col::panelHi, 18, enabled)) {
            switch (qs[i].op) {
                case 0: bet /= 2; break;
                case 1: bet *= 2; break;
                case 2: bet += 10; break;
                case 3: bet = maxBet; break;
            }
        }
    }
    if (button({bx, r.y + 54, 112, 30}, "-", col::panelHi, 22, enabled)) bet -= bet >= 100 ? 50 : 5;
    if (button({bx + 116, r.y + 54, 112, 30}, "+", col::panelHi, 22, enabled)) bet += bet >= 100 ? 50 : 5;
    bet = std::clamp<long long>(bet, std::min<long long>(1, maxBet), std::max<long long>(1, maxBet));
    return bet != old;
}

}  // namespace pg::ui
