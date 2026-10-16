#include "game/Carousel.hpp"

#include "engine/Fx.hpp"
#include "engine/Ui.hpp"
#include "game/ModeArt.hpp"

#include <rlgl.h>

#include <algorithm>

namespace pg {

void Carousel::setTiles(std::vector<Tile> tiles) {
    tiles_ = std::move(tiles);
    hover_.assign(tiles_.size(), 0);
    focus_ = std::clamp(focus_, 0, std::max(0, (int)tiles_.size() - 1));
    scroll_ = target_ = 0;
}

float Carousel::maxScroll(Rectangle area) const {
    float content = tiles_.size() * (tileW + gap) - gap + 2 * 40;
    return std::max(0.0f, content - area.width);
}

void Carousel::ensureVisible(Rectangle area) {
    float x = 40 + focus_ * (tileW + gap);
    if (x - 30 < target_) target_ = x - 30;
    if (x + tileW + 30 > target_ + area.width) target_ = x + tileW + 30 - area.width;
    target_ = std::clamp(target_, 0.0f, maxScroll(area));
}

const Tile* Carousel::run(Rectangle area, float t, bool keys) {
    const Tile* picked = nullptr;
    const int n = (int)tiles_.size();
    if (n == 0) return nullptr;
    float dt = GetFrameTime();
    Vector2 m = E().mouse();
    bool inArea = CheckCollisionPointRec(m, area);

    // --- input: keys, wheel, drag
    if (keys) {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) { focus_ = std::min(n - 1, focus_ + 1); mouseFocus_ = false; ensureVisible(area); E().sfx.play(Sfx::Tick, 0.5f); }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) { focus_ = std::max(0, focus_ - 1); mouseFocus_ = false; ensureVisible(area); E().sfx.play(Sfx::Tick, 0.5f); }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            E().sfx.play(Sfx::Click);
            picked = &tiles_[focus_];
        }
    }
    Vector2 wheel = GetMouseWheelMoveV();
    if (inArea && (wheel.x != 0 || wheel.y != 0)) target_ -= (wheel.y + wheel.x) * 160;
    if (inArea && E().mousePressed()) {
        pressed_ = true;
        dragging_ = false;
        pressX_ = m.x;
        pressScroll_ = target_;
    }
    if (pressed_ && E().mouseDown()) {
        if (std::abs(m.x - pressX_) > 8) dragging_ = true;
        if (dragging_) target_ = pressScroll_ - (m.x - pressX_);
    }
    target_ = std::clamp(target_, 0.0f, maxScroll(area));
    scroll_ = damp(scroll_, target_, 12, dt);

    // --- tiles
    E().beginClip({area.x, area.y - 30, area.width, area.height + 60});
    const float h = area.height;
    int hot = -1;
    for (int i = 0; i < n; i++) {
        float x = area.x + 40 + i * (tileW + gap) - scroll_;
        if (x > area.x + area.width + 20 || x + tileW < area.x - 20) continue;
        Rectangle r{x, area.y, tileW, h};
        bool over = inArea && !dragging_ && CheckCollisionPointRec(m, r);
        if (over && (Vector2Length(GetMouseDelta()) > 0 || mouseFocus_)) {
            if (focus_ != i) E().sfx.play(Sfx::Hover, 0.5f);
            focus_ = i;
            mouseFocus_ = true;
        }
        if (over) hot = i;
        bool focused = i == focus_;
        hover_[i] = damp(hover_[i], focused ? 1.0f : 0.0f, 12, dt);
        float k = hover_[i];
        const Tile& tl = tiles_[i];

        // scale up around the centre when focused
        Vector2 c = center(r);
        rlPushMatrix();
        rlTranslatef(c.x, c.y - k * 8, 0);
        rlScalef(1 + k * 0.05f, 1 + k * 0.05f, 1);
        rlTranslatef(-c.x, -c.y, 0);
        fx::softShadow({c.x, c.y + h * 0.42f}, tileW * 0.55f, 24, 0.5f + 0.2f * k);
        if (k > 0.01f) {
            fx::beginAdd();
            fx::glow(c, tileW * 0.95f, alpha(tl.color, 0.28f * k));
            fx::endAdd();
        }
        fx::gradRoundRect(r, 0.13f, lighten(tl.color, 0.18f + k * 0.08f), darken(tl.color, 0.38f));
        // soft light blob in the top corner + a lit edge
        DrawCircleGradient({r.x + r.width * 0.25f, r.y + r.height * 0.18f}, r.width * 0.6f, alpha(WHITE, 0.14f), alpha(WHITE, 0));
        DrawLineEx({r.x + 24, r.y + 2}, {r.x + r.width - 24, r.y + 2}, 2, alpha(WHITE, 0.35f));
        // illustration
        Rectangle art{r.x + 14, r.y + 14, r.width - 28, h - 112};
        art::modeArt(tl.id, art, t + i * 0.7f, k);
        // label area
        DrawRectangleRounded({r.x + 10, r.y + h - 92, r.width - 20, 80}, 0.3f, 10, alpha(BLACK, 0.22f));
        float fs = ui::measure(tl.title, 26, true) > tileW - 36 ? 21.0f : 26.0f;
        ui::textShadow(tl.title, r.x + 24, r.y + h - 66, fs, WHITE, ui::Left, true, 2, alpha(darken(tl.color, 0.7f), 0.9f));
        ui::paragraph(tl.sub, {r.x + 24, r.y + h - 46, r.width - 44, 0}, 14, alpha(WHITE, 0.82f));
        if (!tl.tag.empty()) {
            float tw = ui::measure(tl.tag, 13, true) + 20;
            Rectangle pill{r.x + 14, r.y + 14, tw, 24};
            Color pc = tl.tag == "NEW" ? col::green : tl.tag == "HOT" ? col::orange : tl.tag == "HALLOWEEN" ? rgba(0xff7a1aff) : col::gold;
            DrawRectangleRounded({pill.x, pill.y + 2, pill.width, pill.height}, 1, 8, alpha(BLACK, 0.3f));
            DrawRectangleRounded(pill, 1, 8, pc);
            ui::text(tl.tag, pill.x + pill.width / 2, pill.y + 12, 13, col::outline, ui::Center, true);
        }
        DrawRectangleRoundedLinesEx(r, 0.13f, 12, 2 + k * 1.5f, k > 0.5f ? alpha(WHITE, 0.9f * k) : alpha(col::outline, 0.8f));
        rlPopMatrix();
    }
    E().endClip();

    // click (not a drag) opens
    if (pressed_ && !E().mouseDown()) {
        if (!dragging_ && hot >= 0 && hot == focus_) {
            E().consumeClick();
            E().sfx.play(Sfx::Click);
            picked = &tiles_[hot];
        }
        pressed_ = false;
        dragging_ = false;
    }

    // --- edge arrows + page dots
    float ms = maxScroll(area);
    for (int side : {-1, 1}) {
        bool can = side < 0 ? target_ > 1 : target_ < ms - 1;
        if (!can) continue;
        Vector2 p{side < 0 ? area.x + 18 : area.x + area.width - 18, area.y + h / 2};
        Rectangle hit{p.x - 24, p.y - 24, 48, 48};
        bool over = CheckCollisionPointRec(m, hit);
        DrawCircleV({p.x, p.y + 3}, 24, alpha(BLACK, 0.35f));
        DrawCircleV(p, 24, over ? WHITE : alpha(WHITE, 0.85f));
        Color ar = col::bgDeep;
        DrawLineEx({p.x - side * 4, p.y - 9}, {p.x + side * 5, p.y}, 4, ar);
        DrawLineEx({p.x + side * 5, p.y}, {p.x - side * 4, p.y + 9}, 4, ar);
        if (over && E().mousePressed()) {
            E().consumeClick();
            pressed_ = false;
            E().sfx.play(Sfx::Click);
            target_ = std::clamp(target_ + side * (tileW + gap) * 3, 0.0f, ms);
        }
    }
    if (ms > 1) {
        // slim scroll track
        float tw = 240, tx = area.x + area.width / 2 - tw / 2, ty = area.y + h + 22;
        DrawRectangleRounded({tx, ty, tw, 6}, 1, 6, alpha(WHITE, 0.15f));
        float vis = area.width / (area.width + ms);
        float kw = std::max(30.0f, tw * vis), kx = tx + (tw - kw) * (scroll_ / ms);
        DrawRectangleRounded({kx, ty, kw, 6}, 1, 6, alpha(WHITE, 0.8f));
    }
    return picked;
}

}  // namespace pg
