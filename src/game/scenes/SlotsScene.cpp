// SLOTS: three reels, one payline. Reel weights + paytable give ~96.6% RTP
// (verified by exhaustive enumeration: python3 tools/slots_rtp.py).
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <array>

namespace pg {
namespace {

enum Sym { Pieter, Horse, Kira, Davi, Boba, Wafel, Coin, kSymCount };
constexpr int kWeights[kSymCount] = {2, 2, 3, 4, 6, 7, 8};  // 32 stops per reel
constexpr int kPay3[kSymCount] = {500, 100, 50, 30, 20, 15, 10};
constexpr int kPay2Pieter = 10, kPay1Pieter = 1;
constexpr float kSymH = 120;
constexpr Rectangle kWindow{240, 200, 600, 360};

void drawSymbol(int s, Vector2 c, float size) {
    switch (s) {
        case Pieter: art::portrait("pieter", c, size * 0.42f, col::gold); break;
        case Horse: art::portrait("horse", c, size * 0.42f, col::orange); break;
        case Kira: art::portrait("kira", c, size * 0.42f, col::pink); break;
        case Davi: art::portrait("davi", c, size * 0.42f, col::cyan); break;
        case Boba: art::boba(c, size * 0.8f); break;
        case Wafel: art::stroopwafelTop(c, size * 0.38f); break;
        case Coin: ui::coin(c, size * 0.34f); break;
    }
}

struct Reel {
    std::vector<int> strip;
    float pos = 0, start = 0, target = 0, stopAt = 0;
    bool spinning = false;
    float bounce = 0;
};

class SlotsScene : public Scene {
public:
    SlotsScene() {
        for (auto& r : reels_) {
            for (int s = 0; s < kSymCount; s++)
                for (int k = 0; k < kWeights[s]; k++) r.strip.push_back(s);
            std::shuffle(r.strip.begin(), r.strip.end(), rng());
            r.pos = (float)rndi(0, 31);
        }
        bet_ = std::clamp<long long>(10, 1, std::max<long long>(1, P().coins));
    }

    int stripLen() const { return (int)reels_[0].strip.size(); }
    int symbolAt(const Reel& r, int offset) const {
        int n = stripLen();
        return r.strip[(((int)std::floor(r.pos + 0.5f) + offset) % n + n) % n];
    }

    void spin() {
        if (spinning() || !P().spend(bet_)) { if (!spinning()) E().sfx.play(Sfx::Error); return; }
        P().wager(bet_);
        stake_ = bet_;
        lastWin_ = 0;
        winT_ = 0;
        spinT_ = 0;
        lever_ = 1;
        int n = stripLen();
        for (int i = 0; i < 3; i++) {
            Reel& r = reels_[i];
            int stop = rndi(0, n - 1);  // uniform stop => weights define the odds
            r.start = r.pos;
            float base = std::floor(r.pos) + n * (3 + i);
            float delta = (float)(((stop - (int)base) % n + n) % n);
            r.target = base + delta;
            r.stopAt = 1.1f + i * 0.45f;
            r.spinning = true;
        }
        E().sfx.play(Sfx::Whoosh);
    }

    bool spinning() const {
        for (auto& r : reels_)
            if (r.spinning) return true;
        return false;
    }

    void evaluate() {
        int a = symbolAt(reels_[0], 0), b = symbolAt(reels_[1], 0), c = symbolAt(reels_[2], 0);
        int mult = 0;
        if (a == b && b == c) mult = kPay3[a];
        else {
            int p = (a == Pieter) + (b == Pieter) + (c == Pieter);
            mult = p == 2 ? kPay2Pieter : p == 1 ? kPay1Pieter : 0;
        }
        lastWin_ = stake_ * mult;
        winMult_ = mult;
        winT_ = 0;
        if (lastWin_ > 0) {
            P().earn(lastWin_, mult > 1);
            if (mult >= 100) {
                E().sfx.play(Sfx::Win);
                E().flash(col::gold, 0.6f);
                E().shake(10);
                fx_.confetti({kWindow.x + kWindow.width / 2, kWindow.y}, 250, 900);
            } else if (mult > 1) {
                E().sfx.play(Sfx::Win, 0.7f);
                fx_.confetti({kWindow.x + kWindow.width / 2, kWindow.y + 100}, 60);
            } else {
                E().sfx.play(Sfx::Coin);
            }
        }
        P().save();
    }

    void update(float dt) override {
        t_ += dt;
        winT_ += dt;
        fx_.update(dt);
        lever_ = approach(lever_, 0, dt * 2.5f);
        if (spinning()) {
            spinT_ += dt;
            for (int i = 0; i < 3; i++) {
                Reel& r = reels_[i];
                if (!r.spinning) continue;
                float k = clamp01(spinT_ / r.stopAt);
                float before = r.pos;
                r.pos = lerpf(r.start, r.target, easeOutCubic(k));
                if (std::floor(r.pos) != std::floor(before) && k < 0.92f) E().sfx.play(Sfx::Tick, 0.25f, 1 + i * 0.1f);
                if (k >= 1) {
                    r.pos = r.target;
                    r.spinning = false;
                    r.bounce = 1;
                    E().sfx.play(Sfx::Drop, 0.6f);
                    if (!spinning()) evaluate();
                }
            }
        }
        for (auto& r : reels_) r.bounce = approach(r.bounce, 0, dt * 4);
        if (IsKeyPressed(KEY_SPACE) && !spinning() && P().coins >= bet_) spin();
        if (E().backPressed() && !spinning()) E().go(makeCasino);

        // coin fountain on wins
        if (lastWin_ > stake_ && winT_ < 1.5f && chance(dt * 30)) {
            Particle p;
            p.pos = {kWindow.x + kWindow.width / 2 + rnd(-200, 200), kWindow.y + kWindow.height};
            p.vel = {rnd(-150, 150), rnd(-650, -400)};
            p.gravity = 1100;
            p.life = p.maxLife = 1.5f;
            p.size = 9;
            p.endSize = 9;
            p.color = col::gold;
            fx_.add(p);
        }
    }

    void draw() override {
        casino::felt(t_);
        // cabinet
        Rectangle cab{kWindow.x - 50, kWindow.y - 110, kWindow.width + 100, kWindow.height + 230};
        ui::panel(cab, rgba(0x5b1f8aff), 0.08f);
        DrawRectangleRounded(shrink(cab, 12), 0.07f, 8, rgba(0x3b1260ff));
        for (int i = 0; i < 18; i++) {
            bool on = ((int)(t_ * (spinning() ? 14 : 4)) + i) % 2 == 0;
            DrawCircleV({cab.x + 30 + i * (cab.width - 60) / 17, cab.y + 26}, 6, on ? col::gold : alpha(col::gold, 0.25f));
        }
        ui::textOutline("PIETER SLOTS", cab.x + cab.width / 2, cab.y + 70, 48, col::gold, col::outline, 4);

        // reels
        DrawRectangleRec(shrink(kWindow, -6), col::outline);
        float rw = kWindow.width / 3;
        E().beginClip(kWindow);
        for (int i = 0; i < 3; i++) {
            Reel& r = reels_[i];
            Rectangle col{kWindow.x + i * rw, kWindow.y, rw - 4, kWindow.height};
            DrawRectangleGradientV((int)col.x, (int)col.y, (int)col.width, (int)col.height, rgba(0xd8d0e8ff), WHITE);
            float frac = r.pos - std::floor(r.pos);
            float bounce = std::sin(r.bounce * PI * 2) * r.bounce * 14;
            int n = stripLen();
            for (int k = -2; k <= 2; k++) {
                int s = r.strip[(((int)std::floor(r.pos) + k) % n + n) % n];
                float y = kWindow.y + kWindow.height / 2 + (k - frac) * kSymH + bounce;
                drawSymbol(s, {col.x + col.width / 2, y}, kSymH * 0.9f);
                if (r.spinning) DrawRectangle((int)col.x, (int)(y - kSymH / 2), (int)col.width, (int)kSymH, alpha(rgba(0xd8d0e8ff), 0.35f));
            }
            DrawRectangleGradientV((int)col.x, (int)col.y, (int)col.width, 50, alpha(BLACK, 0.45f), alpha(BLACK, 0));
            DrawRectangleGradientV((int)col.x, (int)(col.y + col.height - 50), (int)col.width, 50, alpha(BLACK, 0), alpha(BLACK, 0.45f));
        }
        E().endClip();
        // payline
        float ly = kWindow.y + kWindow.height / 2;
        bool won = lastWin_ > 0 && !spinning();
        Color lc = won && std::fmod(t_, 0.3f) < 0.15f ? col::gold : alpha(col::red, 0.8f);
        DrawLineEx({kWindow.x - 20, ly}, {kWindow.x + kWindow.width + 20, ly}, 4, alpha(lc, 0.7f));
        DrawTriangle({kWindow.x - 30, ly - 14}, {kWindow.x - 30, ly + 14}, {kWindow.x - 8, ly}, lc);
        DrawTriangle({kWindow.x + kWindow.width + 30, ly + 14}, {kWindow.x + kWindow.width + 30, ly - 14}, {kWindow.x + kWindow.width + 8, ly}, lc);

        // lever
        Vector2 lb{cab.x + cab.width + 30, kWindow.y + 120};
        float la = lever_ * 1.2f;
        Vector2 knob = lb + Vector2{std::sin(la) * 20, -std::cos(la) * 120};
        DrawRectangleRounded({lb.x - 16, lb.y - 10, 32, 80}, 0.4f, 6, rgba(0x2a2a35ff));
        DrawLineEx(lb, knob, 10, rgba(0xb0b8c8ff));
        DrawCircleV(knob, 22, col::outline);
        DrawCircleV(knob, 19, col::red);
        if (CheckCollisionPointCircle(E().mouse(), knob, 26) && E().mousePressed() && !spinning() && P().coins >= bet_) {
            E().consumeClick();
            spin();
        }

        // controls
        Rectangle ctl{cab.x + 20, cab.y + cab.height - 108, cab.width - 40, 96};
        ui::betSelector({ctl.x, ctl.y, 430, 96}, bet_, P().coins, !spinning());
        if (ui::button({ctl.x + 445, ctl.y + 8, ctl.width - 445, 80}, spinning() ? "..." : "SPIN", col::red, 34, !spinning() && P().coins >= bet_ && P().coins > 0))
            spin();

        // paytable
        Rectangle pt{960, 110, 290, 560};
        ui::panel(pt, alpha(col::bgDeep, 0.85f));
        ui::text("PAYTABLE", pt.x + pt.width / 2, pt.y + 30, 26, col::gold, ui::Center, true);
        for (int s = 0; s < kSymCount; s++) {
            float y = pt.y + 80 + s * 56;
            for (int k = 0; k < 3; k++) drawSymbol(s, {pt.x + 40 + k * 46.0f, y}, 48);
            ui::text(std::to_string(kPay3[s]) + "x", pt.x + pt.width - 20, y, 26, s == Pieter ? col::gold : col::text, ui::Right, true);
        }
        float y = pt.y + 80 + kSymCount * 56;
        drawSymbol(Pieter, {pt.x + 40, y}, 40);
        drawSymbol(Pieter, {pt.x + 80, y}, 40);
        ui::text("any 2", pt.x + 110, y, 18, col::muted);
        ui::text(std::to_string(kPay2Pieter) + "x", pt.x + pt.width - 20, y, 24, col::text, ui::Right, true);
        drawSymbol(Pieter, {pt.x + 40, y + 46}, 40);
        ui::text("any 1", pt.x + 70, y + 46, 18, col::muted);
        ui::text("1x", pt.x + pt.width - 20, y + 46, 24, col::text, ui::Right, true);

        if (lastWin_ > 0 && !spinning()) {
            float k = easeOutElastic(clamp01(winT_ / 0.8f));
            const char* label = winMult_ >= 500 ? "JACKPOT!!!" : winMult_ >= 50 ? "MEGA WIN!" : winMult_ > 1 ? "WIN!" : "money back";
            ui::textOutline(label, kWindow.x + kWindow.width / 2, kWindow.y - 20 + 0, 30 + 26 * k, col::gold, col::outline, 4);
            ui::textOutline("+" + fmtNum(lastWin_), kWindow.x + kWindow.width / 2, ly + 2, 64 * k, WHITE, col::outline, 5);
        }
        fx_.draw();
        if (casino::header("SLOTS") && !spinning()) E().go(makeCasino);
        ui::text("SPACE to spin", 30, VH - 20, 16, alpha(WHITE, 0.5f));
    }

private:
    std::array<Reel, 3> reels_;
    long long bet_ = 10, stake_ = 0, lastWin_ = 0;
    int winMult_ = 0;
    float t_ = 0, spinT_ = 0, winT_ = 0, lever_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeSlots() { return std::make_unique<SlotsScene>(); }

}  // namespace pg
