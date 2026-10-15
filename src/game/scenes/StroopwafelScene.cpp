// STROOPWAFEL STACKER: stack warm stroopwafels on a coffee cup. Drop the
// sliding waffle with SPACE / click; anything hanging over gets sliced off.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>

namespace pg {
namespace {

constexpr float kLayerH = 26;
constexpr float kStartW = 300;
constexpr float kCupTop = 500;  // world y of the first waffle's bottom
constexpr float kMinX = 170, kMaxX = 1110;

struct Layer {
    float x, w;
    float squash = 0;
};

struct Debris {
    Vector2 pos, vel;
    float w, rot, rotVel;
};

enum class State { Ready, Playing, Over };

class StroopwafelScene : public Scene {
public:
    Presence presence() const override {
        Presence p{"Stroopwafel Stacker"};
        int layers = (int)stack_.size() - 1;
        p.state = state_ == State::Ready ? "Warming up the waffles"
                  : std::to_string(layers) + " layers (best " + std::to_string(std::max(layers, P().stats.stroopBest)) + ")";
        p.largeImage = "stroopwafel";
        p.largeText = "Stroopwafel Stacker";
        p.smallImage = "logo";
        return p;
    }

    void enter() override { reset(); }

    void reset() {
        stack_.clear();
        stack_.push_back({VW / 2.0f, kStartW});
        debris_.clear();
        movingW_ = kStartW;
        movingX_ = kMinX + kStartW / 2;
        dir_ = 1;
        combo_ = 0;
        perfects_ = 0;
        earned_ = 0;
        camY_ = 0;
        state_ = State::Ready;
        reaction_ = "Stack 'em high!";
    }

    // world-space center y of waffle i (index 0 is the cup itself)
    float waffleY(int i) const { return kCupTop - (i - 0.5f) * kLayerH; }
    float speed() const { return std::min(240.0f + (stack_.size() - 1) * 14.0f, 720.0f); }

    void drop() {
        const Layer& prev = stack_.back();
        float dx = movingX_ - prev.x;
        int idx = (int)stack_.size();
        if (std::abs(dx) <= 6) {
            // perfect: snap and maybe grow
            combo_++;
            perfects_++;
            float w = movingW_;
            if (combo_ >= 3) w = std::min(kStartW, w + 12);
            stack_.push_back({prev.x, w, 1});
            movingW_ = w;
            E().sfx.play(Sfx::Perfect, 0.8f, 1 + std::min(combo_, 8) * 0.06f);
            fx_.floatText(combo_ >= 3 ? "PERFECT x" + std::to_string(combo_) : "PERFECT!", {prev.x, waffleY(idx) - camY_ - 40}, col::gold, 30);
            fx_.burst({prev.x, waffleY(idx) - camY_}, 20, col::gold, 300, 5, 0.6f, PShape::Star);
            reward(2 + 5);
            reaction_ = combo_ >= 3 ? "LEKKER!!" : "Perfect!";
        } else {
            float left = std::max(movingX_ - movingW_ / 2, prev.x - prev.w / 2);
            float right = std::min(movingX_ + movingW_ / 2, prev.x + prev.w / 2);
            float overlap = right - left;
            if (overlap <= 0) {
                debris_.push_back({{movingX_, waffleY(idx)}, {dir_ * 120.0f, 0}, movingW_, 0, dir_ * 3.0f});
                gameOver();
                return;
            }
            combo_ = 0;
            // cut-off piece
            float cutW = movingW_ - overlap;
            float cutX = dx > 0 ? right + cutW / 2 : left - cutW / 2;
            debris_.push_back({{cutX, waffleY(idx)}, {(dx > 0 ? 1 : -1) * 90.0f, -60}, cutW, 0, (dx > 0 ? 1 : -1) * rnd(2, 5)});
            stack_.push_back({(left + right) / 2, overlap, 1});
            movingW_ = overlap;
            E().sfx.play(Sfx::Drop, 0.8f);
            reward(2);
            reaction_ = overlap < 60 ? "careful..." : (cutW > 40 ? "oof, crumbs" : "nice");
            for (int i = 0; i < 6; i++) {
                Particle p;
                p.pos = {cutX + rnd(-cutW / 2, cutW / 2), waffleY(idx) - camY_ + kLayerH / 2};
                p.vel = {rnd(-40, 40), rnd(0, 60)};
                p.gravity = 500;
                p.life = p.maxLife = 1.0f;
                p.size = 3;
                p.color = col::caramel;
                fx_.add(p);
            }
        }
        int layers = (int)stack_.size() - 1;
        if (layers % 10 == 0) {
            P().give("stroopwafel");
            E().toast("+1 Stroopwafel for Pieter! (" + std::to_string(layers) + " layers)", col::wafel);
            E().sfx.play(Sfx::Coin);
        }
        // next waffle starts from a random side
        dir_ = chance(0.5f) ? 1 : -1;
        movingX_ = dir_ > 0 ? kMinX + movingW_ / 2 : kMaxX - movingW_ / 2;
        E().shake(2);
    }

    void reward(int coins) {
        P().earn(coins);
        earned_ += coins;
    }

    void gameOver() {
        state_ = State::Over;
        overT_ = 0;
        int layers = (int)stack_.size() - 1;
        bool best = layers > P().stats.stroopBest;
        P().stats.stroopBest = std::max(P().stats.stroopBest, layers);
        P().save();
        E().sfx.play(best && layers > 0 ? Sfx::Win : Sfx::Lose);
        if (best && layers > 0) fx_.confetti({VW / 2.0f, 200}, 100);
        newBest_ = best && layers > 0;
        reaction_ = layers > 20 ? "That's a lot of waffles" : "NOOO my stroopwafel";
    }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        for (auto& l : stack_) l.squash = std::max(0.0f, l.squash - dt * 5);
        for (auto& d : debris_) {
            d.vel.y += 1400 * dt;
            d.pos += d.vel * dt;
            d.rot += d.rotVel * dt * 60;
        }
        std::erase_if(debris_, [&](auto& d) { return d.pos.y - camY_ > VH + 100; });

        // camera follows the top of the stack
        float target = std::min(0.0f, waffleY((int)stack_.size()) - 320);
        camY_ = damp(camY_, target, 4, dt);

        bool press = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || (E().mousePressed() && E().mouse().y > 80);
        switch (state_) {
            case State::Ready:
                if (press) { state_ = State::Playing; E().sfx.play(Sfx::Click); }
                break;
            case State::Playing:
                movingX_ += dir_ * speed() * dt;
                if (movingX_ + movingW_ / 2 > kMaxX) { movingX_ = kMaxX - movingW_ / 2; dir_ = -1; }
                if (movingX_ - movingW_ / 2 < kMinX) { movingX_ = kMinX + movingW_ / 2; dir_ = 1; }
                if (press) drop();
                break;
            case State::Over:
                overT_ += dt;
                break;
        }
        // steam
        if (chance(dt * 6)) {
            Particle p;
            p.pos = {VW / 2.0f + rnd(-120, 120), kCupTop - camY_ - 10};
            if (stack_.size() > 1) p.pos.y = waffleY((int)stack_.size() - 1) - camY_ - kLayerH / 2;
            p.vel = {rnd(-10, 10), rnd(-50, -30)};
            p.life = p.maxLife = 2.0f;
            p.size = rnd(8, 16);
            p.endSize = 26;
            p.color = alpha(WHITE, 0.12f);
            fx_.add(p);
        }
        if (E().backPressed()) E().go(makeMenu);
    }

    // Delft-blue tiled kitchen wall with little windmills, fading into the sky
    // and then space as the tower grows.
    void drawKitchen(float h) {
        const float tile = 72;
        float wallFade = clamp01(1 - h * 3);
        if (wallFade <= 0) return;
        float oy = std::fmod(-camY_, tile);
        for (float y = -tile + oy; y < VH; y += tile)
            for (float x = 0; x < VW; x += tile) {
                int ix = (int)(x / tile), iy = (int)std::floor((y - oy - camY_) / tile);
                Rectangle t{x + 2, y + 2, tile - 4, tile - 4};
                DrawRectangleRec(t, alpha(rgba(0xe9eef8ff), wallFade));
                Color blue = alpha(rgba(0x2f5aa8ff), 0.55f * wallFade);
                Vector2 c = center(t);
                // corner ornaments
                for (int k = 0; k < 4; k++)
                    DrawCircleSector({k % 2 ? t.x + t.width : t.x, k / 2 ? t.y + t.height : t.y}, 9, 0, 360, 8, alpha(blue, 0.5f * wallFade));
                if ((ix + iy) % 2 == 0) {
                    // tiny windmill
                    DrawTriangle({c.x - 7, c.y + 16}, {c.x + 7, c.y + 16}, {c.x, c.y - 4}, blue);
                    float rot = t_ * 0.6f + ix;
                    for (int b = 0; b < 4; b++) DrawLineEx(c + Vector2{0, -4}, c + Vector2{0, -4} + fromAngle(rot + b * PI / 2, 14), 3, blue);
                } else {
                    DrawPoly(c, 4, 10, 45, alpha(blue, 0.8f));
                    DrawPoly(c, 4, 5, 45, alpha(rgba(0xe9eef8ff), wallFade));
                }
            }
        // mood: purple wash over the tiles so the game still feels like PieterGame
        DrawRectangleGradientV(0, 0, VW, VH, alpha(rgba(0x241245ff), 0.82f * wallFade), alpha(rgba(0x3b2470ff), 0.62f * wallFade));
        // wooden counter under the cup
        float ct = kCupTop + 168 - camY_;
        if (ct < VH) {
            DrawRectangleGradientV(0, (int)ct, VW, VH - (int)ct, rgba(0x8a5a32ff), rgba(0x4a2c16ff));
            DrawRectangle(0, (int)ct, VW, 6, rgba(0xb07a48ff));
            for (int k = 0; k < 6; k++) DrawLineEx({0, ct + 22 + k * 18}, {(float)VW, ct + 26 + k * 18}, 1.5f, alpha(rgba(0x3a2010ff), 0.35f));
        }
    }

    void drawCup() {
        float top = kCupTop - camY_;
        float cx = VW / 2.0f;
        fx::softShadow({cx + 20, top + 190}, 320, 34, 0.6f);
        // saucer
        DrawEllipse((int)cx, (int)(top + 175), 262, 30, col::outline);
        DrawEllipse((int)cx, (int)(top + 172), 252, 25, rgba(0xdcd4ecff));
        DrawEllipse((int)cx, (int)(top + 168), 230, 18, rgba(0xf6f2fcff));
        // handle (behind the body)
        DrawRing({cx + 185, top + 75}, 30, 48, -90, 90, 24, col::outline);
        DrawRing({cx + 185, top + 75}, 34, 44, -90, 90, 24, rgba(0xe6e0f2ff));
        // cup body with a soft cylindrical shading
        Rectangle body{cx - 175, top, 350, 170};
        DrawRectangleRounded(shrink(body, -4), 0.35f, 12, col::outline);
        DrawRectangleRounded(body, 0.35f, 12, rgba(0xfdfbffff));
        DrawRectangleRec({body.x, body.y, body.width, 40}, rgba(0xfdfbffff));
        DrawRectangleGradientH((int)(body.x + body.width * 0.55f), (int)body.y, (int)(body.width * 0.45f), (int)body.height - 30,
                               alpha(rgba(0xb8a8d8ff), 0), alpha(rgba(0xb8a8d8ff), 0.55f));
        DrawRectangleRounded({body.x + 30, body.y + 18, 26, body.height - 50}, 1, 8, alpha(WHITE, 0.9f));
        // label band with the logo
        DrawRectangleRec({body.x, body.y + 66, body.width, 38}, col::purple);
        DrawRectangleRec({body.x, body.y + 66, body.width, 6}, lighten(col::purple, 0.25f));
        ui::text("PIETER", cx, body.y + 86, 26, WHITE, ui::Center, true);
        // coffee surface with a crema ring
        DrawEllipse((int)cx, (int)top + 4, 174, 15, rgba(0x2a160aff));
        DrawEllipse((int)cx, (int)top + 4, 160, 11, rgba(0x6a3a1aff));
        DrawEllipse((int)cx - 30, (int)top + 2, 60, 4, alpha(rgba(0xd9a066ff), 0.6f));
    }

    void draw() override {
        // background goes from cozy kitchen purple to space as you stack higher
        float h = clamp01(-camY_ / 3000);
        Color top = mix(rgba(0x3b2470ff), rgba(0x05031aff), h), bot = mix(rgba(0x8f5cffff), rgba(0x1a0f2eff), h);
        DrawRectangleGradientV(0, 0, VW, VH, top, bot);
        drawKitchen(h);
        fx::bokeh(t_, 18, mix(col::gold, col::violet, h), 5);
        fx::beginAdd();
        fx::glow({VW / 2.0f, waffleY((int)stack_.size() - 1) - camY_}, 420, alpha(col::gold, 0.22f));
        fx::glow({VW / 2.0f, -100}, 700, alpha(rgba(0xffd59aff), 0.15f * (1 - h)));
        fx::endAdd();
        if (h > 0.2f)
            for (int i = 0; i < 60; i++)
                DrawCircleV({(float)(i * 211 % VW), (float)((i * 97 + (int)(-camY_ * 0.2f)) % VH)}, 1.5f, alpha(WHITE, (h - 0.2f) * (0.5f + 0.5f * std::sin(t_ * 2 + i))));
        // height markers
        for (int m = 10; m < (int)stack_.size() + 20; m += 10) {
            float y = waffleY(m) - camY_;
            if (y < -20 || y > VH + 20) continue;
            DrawLineEx({60, y}, {140, y}, 2, alpha(WHITE, 0.25f));
            ui::text(std::to_string(m), 40, y, 18, alpha(WHITE, 0.4f), ui::Right, true);
        }

        drawCup();
        for (size_t i = 1; i < stack_.size(); i++) {
            const Layer& l = stack_[i];
            float y = waffleY((int)i) - camY_;
            float sq = l.squash * 0.15f;
            art::stroopwafel({l.x, y}, l.w * (1 + sq), kLayerH * (1 - sq));
        }
        for (auto& d : debris_) art::stroopwafel({d.pos.x, d.pos.y - camY_}, d.w, kLayerH, d.rot);
        if (state_ != State::Over) {
            float y = waffleY((int)stack_.size()) - camY_;
            // drop guide
            DrawRectangle((int)(movingX_ - movingW_ / 2), (int)y, (int)movingW_, (int)(VH - y), alpha(WHITE, 0.04f));
            art::stroopwafel({movingX_, y - 4 + std::sin(t_ * 8) * 2}, movingW_, kLayerH);
        }
        fx_.draw();

        // HUD
        ui::titleBar("STROOPWAFEL STACKER", col::gold);
        if (ui::backButton()) E().go(makeMenu);
        ui::coinCounter(VW - 20, 38, P().coins);
        int layers = (int)stack_.size() - 1;
        ui::textOutline(std::to_string(layers), VW / 2.0f, 130, 80, WHITE, col::outline, 4);
        ui::text("best " + std::to_string(P().stats.stroopBest), VW / 2.0f, 180, 20, col::muted, ui::Center);
        if (combo_ >= 2) ui::text("combo x" + std::to_string(combo_), VW / 2.0f, 206, 22, col::gold, ui::Center, true);
        ui::text("+" + fmtNum(earned_), VW - 26, 78, 18, col::gold, ui::Right);

        // Pieter watching from the corner
        art::pieter({1170, VH - 100}, 62, P().hat, std::sin(t_ * 2) * 5);
        float w = ui::measure(reaction_, 20) + 28;
        Rectangle b{1170 - w + 40, VH - 210, w, 36};
        DrawRectangleRounded(b, 0.5f, 10, WHITE);
        ui::text(reaction_, b.x + b.width / 2, b.y + 18, 20, col::outline, ui::Center);

        if (state_ == State::Ready) {
            DrawRectangleRounded({VW / 2.0f - 470, 290, 940, 140}, 0.3f, 10, alpha(col::bgDeep, 0.75f));
            ui::textOutline("PRESS SPACE", VW / 2.0f, 330, 54, col::gold, col::outline, 4);
            ui::text("drop the waffle when it lines up. perfect drops build a combo and grow it back.", VW / 2.0f, 380, 20, WHITE, ui::Center);
            ui::text("every 10 layers = a free stroopwafel for Pieter", VW / 2.0f, 408, 18, col::wafel, ui::Center);
        }
        if (state_ == State::Over && overT_ > 0.6f) {
            DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.45f));
            Rectangle r{VW / 2.0f - 260, VH / 2.0f - 160, 520, 320};
            ui::panel(r);
            ui::textOutline(newBest_ ? "NEW BEST!" : "TOWER FELL", r.x + r.width / 2, r.y + 60, 50, newBest_ ? col::gold : col::orange, col::outline, 4);
            ui::text(std::to_string(layers) + " layers  -  " + std::to_string(perfects_) + " perfect", r.x + r.width / 2, r.y + 120, 24, col::text, ui::Center);
            ui::coin({r.x + r.width / 2 - 70, r.y + 170}, 16);
            ui::text("+" + fmtNum(earned_), r.x + r.width / 2 - 45, r.y + 170, 32, col::gold);
            if (ui::button({r.x + 30, r.y + 230, 220, 60}, "AGAIN", col::purple) || IsKeyPressed(KEY_R)) reset();
            if (ui::button({r.x + 270, r.y + 230, 220, 60}, "MENU", col::panelHi)) E().go(makeMenu);
        }
    }

private:
    State state_ = State::Ready;
    std::vector<Layer> stack_;
    std::vector<Debris> debris_;
    float movingX_ = 0, movingW_ = kStartW, camY_ = 0, t_ = 0, overT_ = 0;
    int dir_ = 1, combo_ = 0, perfects_ = 0;
    long long earned_ = 0;
    bool newBest_ = false;
    std::string reaction_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeStroopwafel() { return std::make_unique<StroopwafelScene>(); }

}  // namespace pg
