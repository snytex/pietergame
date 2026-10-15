#include "engine/Ui.hpp"

#include "engine/Engine.hpp"

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

bool button(Rectangle r, const std::string& label, Color base, float fontSize, bool enabled) {
    bool hot = enabled && hovered(r);
    float h = hoverAnim(r, hot);
    bool down = hot && E().mouseDown();
    float grow = h * 3 - (down ? 2 : 0);
    Rectangle d{r.x - grow, r.y - grow, r.width + grow * 2, r.height + grow * 2};
    Color c = enabled ? mix(base, lighten(base, 0.3f), h) : mix(base, GRAY, 0.7f);
    float depth = down ? 2 : 5;
    DrawRectangleRounded({d.x, d.y + depth, d.width, d.height}, 0.3f, 10, darken(c, 0.55f));
    DrawRectangleRounded(d, 0.3f, 10, c);
    DrawRectangleRounded({d.x + 4, d.y + 3, d.width - 8, d.height * 0.4f}, 0.5f, 10, alpha(WHITE, 0.10f + h * 0.06f));
    DrawRectangleRoundedLinesEx(d, 0.3f, 10, 2.5f, col::outline);
    textShadow(label, d.x + d.width / 2, d.y + d.height / 2, fontSize, enabled ? col::text : col::muted, Center, false, 2);
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
    float grow = h * 5;
    Rectangle d{r.x - grow, r.y - grow, r.width + grow * 2, r.height + grow * 2};
    Color c = mix(base, lighten(base, 0.25f), h);
    DrawRectangleRounded({d.x, d.y + 7, d.width, d.height}, 0.25f, 12, darken(c, 0.6f));
    DrawRectangleRounded(d, 0.25f, 12, c);
    DrawRectangleRounded({d.x + 6, d.y + 4, d.width - 12, d.height * 0.45f}, 0.4f, 12, alpha(WHITE, 0.12f));
    DrawRectangleRoundedLinesEx(d, 0.25f, 12, 3, col::outline);
    float cy = d.y + d.height / 2 - (sub.empty() ? 0 : 10);
    textShadow(label, d.x + d.width / 2, cy, fontSize, col::text, Center, true, 3);
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
    if (button({20, 16, 120, 44}, "< BACK", col::panelHi, 22) || esc) {
        if (esc) E().sfx.play(Sfx::Click);
        return true;
    }
    return false;
}

bool slider(Rectangle r, float& value, Color c) {
    static const void* dragging = nullptr;
    bool hot = hovered({r.x - 10, r.y - 12, r.width + 20, r.height + 24});
    if (hot && E().mousePressed()) { dragging = &value; E().consumeClick(); }
    if (!E().mouseDown()) dragging = nullptr;
    float old = value;
    if (dragging == &value) value = clamp01((E().mouse().x - r.x) / r.width);
    DrawRectangleRounded(r, 1, 8, col::bgDeep);
    DrawRectangleRounded({r.x, r.y, r.width * value, r.height}, 1, 8, c);
    DrawCircleV({r.x + r.width * value, r.y + r.height / 2}, r.height * 1.1f, WHITE);
    DrawCircleLinesV({r.x + r.width * value, r.y + r.height / 2}, r.height * 1.1f, col::outline);
    return old != value;
}

bool toggle(Rectangle r, const std::string& label, bool& value) {
    bool hot = hovered(r);
    hoverAnim(r, hot);
    text(label, r.x, r.y + r.height / 2, 24, col::text);
    Rectangle sw{r.x + r.width - 70, r.y + r.height / 2 - 16, 70, 32};
    DrawRectangleRounded(sw, 1, 12, value ? col::green : col::bgDeep);
    DrawRectangleRoundedLinesEx(sw, 1, 12, 2, col::outline);
    DrawCircleV({value ? sw.x + sw.width - 16 : sw.x + 16, sw.y + 16}, 12, WHITE);
    if (hot && E().mousePressed()) {
        E().consumeClick();
        E().sfx.play(Sfx::Click);
        value = !value;
        return true;
    }
    return false;
}

void panel(Rectangle r, Color c, float round, Color border) {
    DrawRectangleRounded({r.x + 4, r.y + 6, r.width, r.height}, round, 12, alpha(BLACK, 0.35f));
    DrawRectangleRounded(r, round, 12, c);
    DrawRectangleRoundedLinesEx(r, round, 12, 3, border);
}

void bar(Rectangle r, float frac, Color fill, Color back) {
    DrawRectangleRounded(r, 1, 10, back);
    if (frac > 0.001f) DrawRectangleRounded({r.x, r.y, std::max(r.height, r.width * clamp01(frac)), r.height}, 1, 10, fill);
    DrawRectangleRounded({r.x + 3, r.y + 2, std::max(0.0f, r.width * clamp01(frac) - 6), r.height * 0.35f}, 1, 10, alpha(WHITE, 0.25f));
    DrawRectangleRoundedLinesEx(r, 1, 10, 2, col::outline);
}

void coin(Vector2 c, float radius) {
    DrawCircleV({c.x, c.y + radius * 0.15f}, radius, col::goldDark);
    DrawCircleV(c, radius, col::gold);
    DrawCircleV(c, radius * 0.72f, lighten(col::gold, 0.15f));
    DrawCircleLinesV(c, radius, col::outline);
    text("P", c.x, c.y, radius * 1.2f, col::goldDark, Center, true);
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
    DrawRectangleGradientV(0, 0, VW, 76, alpha(col::bgDeep, 0.95f), alpha(col::bgDeep, 0.0f));
    textShadow(title, VW / 2.0f, 38, 44, col::text, Center, true, 3, darken(accent, 0.5f));
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
