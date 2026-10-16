// CRASH: a boba-rocket climbs and the multiplier with it. Cash out before it
// blows up. Rounds run back to back; bet during the countdown. 3% house edge:
// P(crash >= x) = 0.97 / x.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/ModeArt.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <deque>

#include <rlgl.h>

namespace pg {
namespace {

constexpr float kGrowth = 0.14f;  // m(t) = e^(k t): 2x after ~5s, 10x after ~16s
constexpr Rectangle kGraph{30, 100, 860, 470};

enum class Phase { Countdown, Flying, Crashed };

struct Bot {
    const char* name;
    const char* face;  // portrait, or nullptr for a coloured initial
    Color color;
    long long bet = 0;
    float target = 0;  // cash-out multiplier they're aiming for
    float cashedAt = 0;
};

float multiplierAt(float t) { return std::exp(kGrowth * t); }
float timeFor(float m) { return std::log(std::max(1.0f, m)) / kGrowth; }

class CrashScene : public Scene {
public:
    Presence presence() const override { return {"Casino - Crash", "", "casino", "Pieter Casino", "logo"}; }

    void enter() override {
        bet_ = std::clamp<long long>(20, 1, std::max<long long>(1, P().coins));
        for (int i = 0; i < 8; i++) history_.push_back(rollCrash());
        newRound();
        phaseT_ = 2.5f;
    }

    static float rollCrash() {
        float u = rnd(0.0f, 1.0f);
        float c = 0.97f / std::max(1e-4f, 1 - u);
        if (c < 1) return 1.0f;
        return std::min(1000.0f, std::floor(c * 100) / 100);
    }

    void newRound() {
        phase_ = Phase::Countdown;
        phaseT_ = 5;
        flyT_ = 0;
        crashAt_ = rollCrash();
        trail_.clear();
        cashedAt_ = 0;
        result_.clear();
        // the crew places their bets
        bots_ = {{"Davi", "davi", col::cyan}, {"Kira", "kira", col::pink}, {"Horse", "horse", col::orange},
                 {"Opa Henk", nullptr, rgba(0x8fb0ffff)}, {"boba_lover", nullptr, col::gold}, {"xX_pro_Xx", nullptr, col::green}};
        for (auto& b : bots_) {
            b.bet = (long long)rnd(5, 400);
            b.target = std::string(b.name) == "Davi" ? rnd(5, 30) : std::string(b.name) == "Kira" ? rnd(1.2f, 2.0f) : rnd(1.1f, 6.0f);
            b.cashedAt = 0;
        }
        if (queued_) placeBet();
    }

    void placeBet() {
        queued_ = false;
        if (playing_) return;
        if (!P().spend(bet_)) { E().sfx.play(Sfx::Error); return; }
        P().wager(bet_);
        stake_ = bet_;
        playing_ = true;
        E().sfx.play(Sfx::Card);
    }

    void cashOut() {
        if (!playing_ || phase_ != Phase::Flying) return;
        float m = multiplierAt(flyT_);
        long long win = (long long)std::floor(stake_ * m);
        P().earn(win, true);
        cashedAt_ = m;
        playing_ = false;
        lastWin_ = win;
        result_ = "+" + fmtNum(win);
        E().sfx.play(Sfx::Coin);
        E().sfx.play(Sfx::Win, 0.5f);
        fx_.confetti(rocketPos(), 70);
        P().save();
    }

    // ------------------------------------------------------------ graph mapping
    float maxT() const { return std::max(8.0f, flyT_ * 1.15f); }
    float maxM() const { return std::max(2.0f, multiplierAt(flyT_) * 1.2f); }
    Vector2 toGraph(float t, float m) const {
        Rectangle g{kGraph.x + 60, kGraph.y + 30, kGraph.width - 90, kGraph.height - 80};
        return {g.x + t / maxT() * g.width, g.y + g.height - (m - 1) / (maxM() - 1) * g.height};
    }
    Vector2 rocketPos() const { return toGraph(flyT_, multiplierAt(flyT_)); }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        phaseT_ -= dt;
        switch (phase_) {
            case Phase::Countdown: {
                int s = (int)std::ceil(phaseT_);
                if (s != lastTick_ && s <= 3 && s > 0) { lastTick_ = s; E().sfx.play(Sfx::Tick, 0.7f, 1.2f); }
                if (phaseT_ <= 0) {
                    phase_ = Phase::Flying;
                    flyT_ = 0;
                    E().sfx.play(Sfx::Whoosh, 0.7f);
                    E().sfx.play(Sfx::Power, 0.5f);
                }
                break;
            }
            case Phase::Flying: {
                flyT_ += dt;
                float m = multiplierAt(flyT_);
                if (std::floor(m) != std::floor(multiplierAt(flyT_ - dt))) E().sfx.play(Sfx::Tick, 0.4f, 1 + std::min(2.0f, m * 0.05f));
                for (auto& b : bots_)
                    if (b.cashedAt == 0 && m >= b.target && b.target < crashAt_) b.cashedAt = b.target;
                if (playing_ && autoOn_ && m >= autoAt_) cashOut();
                // exhaust particles
                Particle p;
                p.pos = rocketPos();
                p.vel = {rnd(-80, -30), rnd(10, 60)};
                p.life = p.maxLife = rnd(0.4f, 0.8f);
                p.size = rnd(4, 8);
                p.endSize = 14;
                p.color = alpha(WHITE, 0.25f);
                fx_.add(p);
                if (m >= crashAt_) {
                    phase_ = Phase::Crashed;
                    phaseT_ = 3;
                    history_.push_front(crashAt_);
                    if (history_.size() > 14) history_.pop_back();
                    Vector2 rp = rocketPos();
                    fx_.burst(rp, 60, col::orange, 520, 10, 0.9f);
                    fx_.burst(rp, 30, col::red, 340, 8, 0.8f, PShape::Spark);
                    E().sfx.play(Sfx::Boom);
                    E().shake(14);
                    if (playing_) {
                        playing_ = false;
                        result_ = "-" + fmtNum(stake_);
                        E().sfx.play(Sfx::Lose, 0.5f);
                        P().save();
                    }
                }
                break;
            }
            case Phase::Crashed:
                if (phaseT_ <= 0) newRound();
                break;
        }
        if (IsKeyPressed(KEY_SPACE)) {
            if (phase_ == Phase::Flying && playing_) cashOut();
            else if (phase_ == Phase::Countdown && !playing_) placeBet();
            else if (phase_ != Phase::Countdown && !playing_) queued_ = !queued_;
        }
        if (E().backPressed() && !playing_) E().go(makeCasino);
    }

    // ------------------------------------------------------------ draw
    void drawGraph() {
        Rectangle g = kGraph;
        ui::panel(g, alpha(rgba(0x0c0818ff), 0.92f), 0.04f, alpha(col::green, 0.35f));
        E().beginClip(shrink(g, 3));
        // starfield, faster as it climbs
        float speed = phase_ == Phase::Flying ? 40 + multiplierAt(flyT_) * 25 : 10;
        starScroll_ += GetFrameTime() * speed;
        for (int i = 0; i < 70; i++) {
            float x = g.x + std::fmod(i * 137.0f - starScroll_ * (0.5f + (i % 3) * 0.4f), g.width + 20);
            if (x < g.x - 10) x += g.width + 20;
            float y = g.y + std::fmod(i * 71.0f + starScroll_ * 0.2f * (i % 2), g.height);
            DrawCircleV({x, y}, 1 + (i % 3) * 0.6f, alpha(WHITE, 0.25f + 0.2f * (i % 3)));
        }
        // nebula glow + a city skyline at the bottom
        fx::beginAdd();
        fx::glow({g.x + g.width * 0.7f, g.y + g.height * 0.3f}, 320, alpha(col::violet, 0.12f));
        fx::glow({g.x + g.width * 0.2f, g.y + g.height * 0.5f}, 260, alpha(col::pink, 0.08f));
        fx::endAdd();
        for (int i = 0; i < 26; i++) {
            float bw = 26 + (i * 37 % 30), bh = 30 + (i * 53 % 70);
            float bx = g.x + i * 34 - std::fmod(starScroll_ * 0.4f, 34.0f);
            DrawRectangleRec({bx, g.y + g.height - bh, bw, bh}, rgba(0x120c22ff));
            for (int w = 0; w < (int)(bh / 12); w++)
                if ((i * 7 + w * 3) % 5 < 2) DrawRectangleRec({bx + 6, g.y + g.height - bh + 6 + w * 12, 4, 5}, alpha(col::gold, 0.6f));
        }
        // grid + axis labels
        Rectangle in{g.x + 60, g.y + 30, g.width - 90, g.height - 80};
        float mm = maxM(), tt = maxT();
        float stepM = mm < 3 ? 0.5f : mm < 6 ? 1 : mm < 15 ? 2 : mm < 40 ? 5 : mm < 120 ? 20 : 100;
        for (float m = 1; m <= mm; m += stepM) {
            float y = toGraph(0, m).y;
            DrawLineEx({in.x, y}, {in.x + in.width, y}, 1, alpha(WHITE, 0.07f));
            ui::text(TextFormat("%.1fx", m), in.x - 10, y, 13, alpha(col::muted, 0.8f), ui::Right, true);
        }
        float stepT = tt < 12 ? 2 : tt < 30 ? 5 : 10;
        for (float s = 0; s <= tt; s += stepT) {
            float x = toGraph(s, 1).x;
            DrawLineEx({x, in.y}, {x, in.y + in.height}, 1, alpha(WHITE, 0.05f));
            ui::text(TextFormat("%ds", (int)s), x, in.y + in.height + 18, 13, alpha(col::muted, 0.7f), ui::Center, true);
        }
        // the curve with a filled area under it
        if (phase_ != Phase::Countdown) {
            bool dead = phase_ == Phase::Crashed;
            Color cc = dead ? col::red : col::green;
            Vector2 base = toGraph(0, 1);
            Vector2 prev = base;
            int steps = 60;
            for (int i = 1; i <= steps; i++) {
                float s = flyT_ * i / steps;
                Vector2 p = toGraph(s, multiplierAt(s));
                Vector2 b0{prev.x, base.y}, b1{p.x, base.y};
                DrawTriangle(prev, b0, b1, alpha(cc, 0.14f));
                DrawTriangle(prev, b1, p, alpha(cc, 0.14f));
                DrawTriangle(prev, b1, b0, alpha(cc, 0.14f));
                DrawTriangle(prev, p, b1, alpha(cc, 0.14f));
                prev = p;
            }
            prev = base;
            for (int i = 1; i <= steps; i++) {
                float s = flyT_ * i / steps;
                Vector2 p = toGraph(s, multiplierAt(s));
                DrawLineEx(prev, p, 12, alpha(cc, 0.25f));
                DrawLineEx(prev, p, 5, cc);
                prev = p;
            }
            fx::beginAdd();
            fx::glow(prev, 60, alpha(cc, 0.4f));
            fx::endAdd();
            // where you cashed out
            if (cashedAt_ > 0) {
                Vector2 cp = toGraph(timeFor(cashedAt_), cashedAt_);
                DrawCircleV(cp, 9, col::outline);
                DrawCircleV(cp, 7, col::gold);
                ui::textShadow(TextFormat("%.2fx", cashedAt_), cp.x, cp.y - 22, 16, col::gold, ui::Center, true, 2);
            }
            if (!dead) {
                Vector2 a = toGraph(std::max(0.0f, flyT_ - 0.3f), multiplierAt(std::max(0.0f, flyT_ - 0.3f)));
                float ang = angleOf(prev - a) * RAD2DEG;
                fx::beginAdd();
                for (int i = 0; i < 4; i++) {
                    float k = std::fmod(t_ * 6 + i * 0.25f, 1.0f);
                    fx::glow(prev - fromAngle(ang * DEG2RAD, 40 + k * 50), 26 - k * 14, alpha(mix(col::gold, col::red, k), 0.8f));
                }
                fx::endAdd();
                fx::sprite(E().tex("spr/rocket"), prev, 0.36f, ang);
            }
        }
        E().endClip();

        // big multiplier in the middle
        Vector2 mc{g.x + g.width / 2, g.y + g.height / 2 - 20};
        if (phase_ == Phase::Countdown) {
            ui::text("NEXT ROUND IN", mc.x, mc.y - 40, 20, col::muted, ui::Center, true);
            ui::textShadow(TextFormat("%.1fs", std::max(0.0f, phaseT_)), mc.x, mc.y + 10, 64, WHITE, ui::Center, true, 4);
            DrawRectangleRounded({mc.x - 120, mc.y + 56, 240, 8}, 1, 6, alpha(WHITE, 0.15f));
            DrawRectangleRounded({mc.x - 120, mc.y + 56, 240 * clamp01(phaseT_ / 5), 8}, 1, 6, col::green);
        } else {
            float m = phase_ == Phase::Crashed ? crashAt_ : multiplierAt(flyT_);
            Color c = phase_ == Phase::Crashed ? col::red : m >= 10 ? col::gold : WHITE;
            float pop = phase_ == Phase::Flying ? 1 + std::min(0.4f, (m - 1) * 0.02f) : 1;
            rlPushMatrix();
            rlTranslatef(mc.x, mc.y, 0);
            rlScalef(pop, pop, 1);
            ui::textShadow(TextFormat("%.2fx", m), 0, 0, 86, c, ui::Center, true, 5);
            rlPopMatrix();
            if (phase_ == Phase::Crashed) ui::text("CRASHED", mc.x, mc.y + 60, 26, col::red, ui::Center, true);
        }
        if (!result_.empty() && phase_ != Phase::Countdown) {
            bool won = result_[0] == '+';
            ui::textOutline(result_, mc.x, g.y + g.height - 70, 40, won ? col::gold : col::red, col::outline, 3);
        }

        // history strip
        float x = g.x + 14;
        ui::text("LAST", x, g.y + g.height + 32, 13, col::muted, ui::Left, true);
        x += 48;
        for (float h : history_) {
            std::string s = TextFormat("%.2fx", h);
            float w = ui::measure(s, 14, true) + 18;
            if (x + w > g.x + g.width) break;
            Color c = h < 2 ? col::red : h < 10 ? col::green : col::gold;
            Rectangle pill{x, g.y + g.height + 18, w, 28};
            DrawRectangleRounded(pill, 1, 8, alpha(c, 0.2f));
            DrawRectangleRoundedLinesEx(pill, 1, 8, 1.5f, c);
            ui::text(s, pill.x + w / 2, pill.y + 14, 14, c, ui::Center, true);
            x += w + 6;
        }
    }

    void drawSide() {
        Rectangle side{910, 100, 340, 600};
        ui::panel(side);
        ui::text("CRASH", side.x + 22, side.y + 32, 30, col::text, ui::Left, true);
        ui::betSelector({side.x + 14, side.y + 58, side.width - 28, 100}, bet_, P().coins, !playing_);
        // auto cash-out
        float y = side.y + 178;
        ui::toggle({side.x + 20, y, 160, 40}, "Auto", autoOn_);
        if (ui::button({side.x + 190, y + 2, 36, 36}, "-", col::panelHi, 20)) autoAt_ = std::max(1.1f, autoAt_ - (autoAt_ > 5 ? 1.0f : 0.1f));
        ui::text(TextFormat("%.2fx", autoAt_), side.x + 262, y + 20, 18, autoOn_ ? col::gold : col::muted, ui::Center, true);
        if (ui::button({side.x + 298, y + 2, 36, 36}, "+", col::panelHi, 20)) autoAt_ = std::min(500.0f, autoAt_ + (autoAt_ >= 5 ? 1.0f : 0.1f));

        Rectangle b{side.x + 20, side.y + 232, side.width - 40, 70};
        if (phase_ == Phase::Flying && playing_) {
            long long cash = (long long)std::floor(stake_ * multiplierAt(flyT_));
            if (ui::button(b, "CASH OUT  " + fmtNum(cash), col::gold, 26)) cashOut();
        } else if (playing_) {
            ui::button(b, "BET PLACED", col::panelHi, 26, false);
        } else if (phase_ == Phase::Countdown) {
            if (ui::button(b, "PLACE BET", col::green, 30, P().coins >= bet_)) placeBet();
        } else {
            if (ui::button(b, queued_ ? "QUEUED (cancel)" : "BET NEXT ROUND", queued_ ? col::panelHi : col::purple, 24)) queued_ = !queued_;
        }
        ui::text("SPACE", side.x + side.width / 2, side.y + 318, 12, alpha(col::muted, 0.7f), ui::Center, true);

        // players
        y = side.y + 342;
        ui::text("PLAYERS", side.x + 22, y, 15, col::muted, ui::Left, true);
        y += 22;
        auto row = [&](const char* name, const char* face, Color c, long long bet, float cashed, bool you) {
            Rectangle r{side.x + 14, y, side.width - 28, 30};
            if (you) DrawRectangleRounded(r, 0.4f, 8, alpha(col::gold, 0.12f));
            if (face) art::portrait(face, {r.x + 16, r.y + 15}, 11, BLANK);
            else {
                DrawCircleV({r.x + 16, r.y + 15}, 11, c);
                ui::text(std::string(1, name[0]), r.x + 16, r.y + 15, 13, col::outline, ui::Center, true);
            }
            ui::text(name, r.x + 36, r.y + 15, 15, you ? col::gold : col::text, ui::Left, true);
            ui::text(fmtNum(bet), r.x + 180, r.y + 15, 14, col::muted, ui::Right);
            bool dead = phase_ == Phase::Crashed && cashed == 0;
            std::string st = cashed > 0 ? TextFormat("%.2fx", cashed) : dead ? "bust" : phase_ == Phase::Flying ? "..." : "";
            ui::text(st, r.x + r.width - 8, r.y + 15, 14, cashed > 0 ? col::green : dead ? col::red : col::muted, ui::Right, true);
            y += 34;
        };
        if (playing_ || cashedAt_ > 0 || (phase_ == Phase::Crashed && result_.size() && result_[0] == '-'))
            row("You", "pieter", col::gold, stake_, cashedAt_, true);
        for (auto& bt : bots_) {
            if (y > side.y + side.height - 30) break;
            row(bt.name, bt.face, bt.color, bt.bet, bt.cashedAt, false);
        }
    }

    void draw() override {
        casino::felt(t_);
        drawGraph();
        drawSide();
        fx_.draw();
        if (casino::header("CRASH") && !playing_) E().go(makeCasino);
    }

private:
    Phase phase_ = Phase::Countdown;
    float t_ = 0, phaseT_ = 0, flyT_ = 0, crashAt_ = 2, cashedAt_ = 0, starScroll_ = 0;
    float autoAt_ = 2.0f;
    bool autoOn_ = false, playing_ = false, queued_ = false;
    int lastTick_ = 0;
    long long bet_ = 20, stake_ = 0, lastWin_ = 0;
    std::string result_;
    std::deque<float> history_;
    std::vector<Vector2> trail_;
    std::vector<Bot> bots_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeCrash() { return std::make_unique<CrashScene>(); }

}  // namespace pg
