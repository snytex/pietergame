// ROULETTE: European single-zero wheel. Click the table to place chips,
// right-click to remove. The outcome is rolled first, then animated.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <map>

namespace pg {
namespace {

const int kWheel[37] = {0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
                        5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26};

bool isRed(int n) {
    static const int reds[] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
    return std::find(std::begin(reds), std::end(reds), n) != std::end(reds);
}
Color numColor(int n) { return n == 0 ? rgba(0x1f9a54ff) : isRed(n) ? rgba(0xd22a3aff) : rgba(0x1d1a24ff); }

constexpr Vector2 kWheelC{290, 400};
constexpr float kWheelR = 215;
constexpr float kSpinTime = 6.0f;

struct Spot {
    std::string id;
    Rectangle r;
    std::string label;
    Color color;
};

class RouletteScene : public Scene {
public:
    Presence presence() const override { return {"Casino - Roulette", "", "casino", "Pieter Casino", "logo"}; }

    RouletteScene() { buildTable(); }

    void buildTable() {
        const float x0 = 608, y0 = 150, cw = 48, ch = 56;
        spots_.push_back({"n:0", {x0 - 48, y0, 46, ch * 3}, "0", numColor(0)});
        for (int c = 0; c < 12; c++)
            for (int r = 0; r < 3; r++) {
                int n = c * 3 + (3 - r);
                spots_.push_back({"n:" + std::to_string(n), {x0 + c * cw, y0 + r * ch, cw, ch}, std::to_string(n), numColor(n)});
            }
        for (int r = 0; r < 3; r++) spots_.push_back({"col:" + std::to_string(2 - r), {x0 + 12 * cw + 2, y0 + r * ch, 46, ch}, "2:1", col::panelHi});
        const char* dz[] = {"1st 12", "2nd 12", "3rd 12"};
        for (int d = 0; d < 3; d++) spots_.push_back({"dz:" + std::to_string(d), {x0 + d * 4 * cw, y0 + 3 * ch + 2, 4 * cw, 48}, dz[d], col::panelHi});
        struct O { const char* id; const char* label; Color c; } outs[] = {
            {"low", "1-18", col::panelHi}, {"even", "EVEN", col::panelHi}, {"red", "RED", numColor(1)},
            {"black", "BLACK", numColor(2)}, {"odd", "ODD", col::panelHi}, {"high", "19-36", col::panelHi}};
        for (int i = 0; i < 6; i++) spots_.push_back({outs[i].id, {x0 + i * 2 * cw, y0 + 3 * ch + 52, 2 * cw, 48}, outs[i].label, outs[i].c});
    }

    long long totalBet() const {
        long long s = 0;
        for (auto& [k, v] : bets_) s += v;
        return s;
    }

    // payout multiplier (profit) if `id` wins with result n, or -1 if it loses
    static int payout(const std::string& id, int n) {
        if (id.rfind("n:", 0) == 0) return std::stoi(id.substr(2)) == n ? 35 : -1;
        if (n == 0) return -1;
        if (id.rfind("dz:", 0) == 0) return (n - 1) / 12 == std::stoi(id.substr(3)) ? 2 : -1;
        if (id.rfind("col:", 0) == 0) return (n - 1) % 3 == std::stoi(id.substr(4)) ? 2 : -1;
        if (id == "red") return isRed(n) ? 1 : -1;
        if (id == "black") return !isRed(n) ? 1 : -1;
        if (id == "odd") return n % 2 == 1 ? 1 : -1;
        if (id == "even") return n % 2 == 0 ? 1 : -1;
        if (id == "low") return n <= 18 ? 1 : -1;
        if (id == "high") return n >= 19 ? 1 : -1;
        return -1;
    }

    void spin() {
        long long total = totalBet();
        if (total <= 0 || spinning_) return;
        if (!P().spend(total)) { E().sfx.play(Sfx::Error); E().toast("Not enough coins for those bets", col::red); return; }
        P().wager(total);
        lastBets_ = bets_;
        spinning_ = true;
        spinT_ = 0;
        resultIdx_ = rndi(0, 36);
        wheelStart_ = wheelRot_;
        wheelEnd_ = wheelRot_ + 360 * 3 + rnd(0, 360);
        tickAcc_ = 0;
        E().sfx.play(Sfx::Whoosh);
    }

    void resolve() {
        int n = kWheel[resultIdx_];
        long long won = 0;
        for (auto& [id, amt] : bets_) {
            int m = payout(id, n);
            if (m >= 0) won += amt * (m + 1);
        }
        lastWin_ = won;
        history_.insert(history_.begin(), n);
        if (history_.size() > 12) history_.pop_back();
        if (won > 0) {
            P().earn(won, true);
            E().sfx.play(Sfx::Win);
            fx_.confetti({880, 300}, 80);
        } else {
            E().sfx.play(Sfx::Lose, 0.6f);
        }
        P().save();
        bets_.clear();
        showResultT_ = 3.5f;
    }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        showResultT_ -= dt;
        if (spinning_) {
            spinT_ += dt;
            float k = easeOutCubic(spinT_ / kSpinTime);
            float prev = wheelRot_;
            wheelRot_ = lerpf(wheelStart_, wheelEnd_, k);
            tickAcc_ += std::abs(wheelRot_ - prev) + (1 - k) * 15;
            if (tickAcc_ > 360.0f / 37 * 2) { tickAcc_ = 0; E().sfx.play(Sfx::Tick, 0.4f); }
            if (spinT_ >= kSpinTime) { spinning_ = false; resolve(); }
        } else {
            wheelRot_ += dt * 6;  // idle drift
        }
        if (E().backPressed() && !spinning_) E().go(makeCasino);
    }

    float pocketAngle(int idx) const { return wheelRot_ + (idx + 0.5f) * 360.0f / 37; }

    void drawWheel() {
        Vector2 c = kWheelC;
        DrawCircleV({c.x + 6, c.y + 10}, kWheelR + 26, alpha(BLACK, 0.4f));
        DrawCircleV(c, kWheelR + 26, rgba(0x4a2410ff));
        DrawCircleV(c, kWheelR + 16, rgba(0x6b3a1aff));
        DrawRing(c, kWheelR - 4, kWheelR + 4, 0, 360, 72, col::gold);
        float step = 360.0f / 37;
        for (int i = 0; i < 37; i++) {
            float a0 = wheelRot_ + i * step;
            DrawCircleSector(c, kWheelR - 4, a0, a0 + step, 4, numColor(kWheel[i]));
            DrawLineEx(c + fromAngle(a0 * DEG2RAD, kWheelR * 0.62f), c + fromAngle(a0 * DEG2RAD, kWheelR - 4), 1.5f, col::gold);
            float mid = (a0 + step / 2) * DEG2RAD;
            ui::textRotated(std::to_string(kWheel[i]), c + fromAngle(mid, kWheelR * 0.86f), 17, a0 + step / 2 + 90, WHITE, true);
        }
        DrawCircleV(c, kWheelR * 0.62f, rgba(0x6b3a1aff));
        DrawRing(c, kWheelR * 0.6f, kWheelR * 0.64f, 0, 360, 60, col::gold);
        DrawCircleV(c, kWheelR * 0.45f, rgba(0x8a4d22ff));
        // spokes + Pieter in the middle
        for (int i = 0; i < 4; i++) {
            float a = (wheelRot_ + i * 90) * DEG2RAD;
            DrawLineEx(c + fromAngle(a, 30), c + fromAngle(a, kWheelR * 0.5f), 8, col::gold);
        }
        art::portrait("pieter", c, 46, col::gold, wheelRot_);

        // ball
        float ballA, ballR;
        if (spinning_) {
            float k = clamp01(spinT_ / kSpinTime);
            float finalA = wheelEnd_ + (resultIdx_ + 0.5f) * 360.0f / 37;
            ballA = finalA - (1 - easeOutCubic(k)) * 360 * 6;
            ballR = lerpf(kWheelR + 6, kWheelR * 0.74f, easeInCubic(clamp01((k - 0.55f) / 0.4f)));
            if (k > 0.55f && k < 0.95f) ballR += std::abs(std::sin(k * 60)) * 8 * (0.95f - k);
        } else if (!history_.empty()) {
            ballA = pocketAngle(resultIdx_);
            ballR = kWheelR * 0.74f;
        } else {
            return;
        }
        Vector2 bp = c + fromAngle(ballA * DEG2RAD, ballR);
        DrawCircleV(bp + Vector2{2, 3}, 9, alpha(BLACK, 0.4f));
        DrawCircleV(bp, 9, WHITE);
        DrawCircleV(bp - Vector2{3, 3}, 3, alpha(col::cyan, 0.5f));
    }

    void drawChip(Vector2 p, long long amount, float r = 17) {
        int v = (int)amount;
        Color c = v >= 500 ? col::violet : v >= 100 ? rgba(0x1d1a24ff) : v >= 25 ? col::green : v >= 5 ? col::red : rgba(0xe8e8f0ff);
        DrawCircleV({p.x + 1, p.y + 3}, r, alpha(BLACK, 0.4f));
        DrawCircleV(p, r, c);
        for (int i = 0; i < 6; i++) DrawCircleSector(p, r, i * 60.0f, i * 60.0f + 20, 3, WHITE);
        DrawCircleV(p, r * 0.7f, c);
        ui::circleOutline(p, r, col::outline);
        std::string s = amount >= 1000 ? std::to_string(amount / 1000) + "k" : std::to_string(amount);
        ui::text(s, p.x, p.y, r * 0.75f, v >= 5 && v < 25 ? WHITE : (v < 5 ? col::outline : WHITE), ui::Center, true);
    }

    void drawTable() {
        Rectangle t{540, 130, 700, 330};
        DrawRectangleRounded(t, 0.06f, 8, rgba(0x0b4a2aff));
        DrawRectangleRoundedLinesEx(t, 0.06f, 8, 4, col::goldDark);
        int winN = (!spinning_ && showResultT_ > 0 && !history_.empty()) ? history_[0] : -1;
        for (auto& s : spots_) {
            bool hot = !spinning_ && ui::hovered(s.r);
            bool winning = winN >= 0 && payout(s.id, winN) >= 0;
            Color c = s.color;
            if (winning && std::fmod(t_, 0.4f) < 0.2f) c = col::gold;
            DrawRectangleRec(s.r, hot ? lighten(c, 0.25f) : c);
            DrawRectangleLinesEx(s.r, 1.5f, alpha(WHITE, 0.6f));
            ui::text(s.label, s.r.x + s.r.width / 2, s.r.y + s.r.height / 2, s.label.size() > 3 ? 17 : 20, WHITE, ui::Center, true);
            if (bets_.count(s.id)) drawChip({s.r.x + s.r.width / 2 + 8, s.r.y + s.r.height / 2 + 8}, bets_[s.id], 15);
            if (hot) {
                if (E().mousePressed()) {
                    E().consumeClick();
                    if (totalBet() + chip_ <= P().coins) { bets_[s.id] += chip_; E().sfx.play(Sfx::Card, 0.8f, 1.4f); }
                    else { E().sfx.play(Sfx::Error); E().toast("Not enough coins", col::red); }
                }
                if (E().rightPressed() && bets_.count(s.id)) {
                    bets_[s.id] -= std::min(bets_[s.id], chip_);
                    if (bets_[s.id] <= 0) bets_.erase(s.id);
                    E().sfx.play(Sfx::Card, 0.8f, 0.8f);
                }
            }
        }
    }

    void draw() override {
        casino::felt(t_);
        drawWheel();
        drawTable();

        // chip selector
        const int chips[] = {1, 5, 25, 100, 500};
        ui::text("CHIP", 560, 500, 18, col::muted, ui::Left, true);
        for (int i = 0; i < 5; i++) {
            Vector2 p{640 + i * 62.0f, 500};
            if (chip_ == chips[i]) DrawCircleV(p, 27, col::gold);
            drawChip(p, chips[i], 23);
            if (CheckCollisionPointCircle(E().mouse(), p, 23) && E().mousePressed()) {
                E().consumeClick();
                chip_ = chips[i];
                E().sfx.play(Sfx::Click);
            }
        }
        ui::text("Total bet: " + fmtNum(totalBet()), 1230, 500, 24, col::gold, ui::Right, true);

        bool idle = !spinning_;
        if (ui::button({560, 560, 160, 60}, "CLEAR", col::panelHi, 22, idle && !bets_.empty())) bets_.clear();
        if (ui::button({735, 560, 160, 60}, "REBET", col::panelHi, 22, idle && !lastBets_.empty())) {
            long long need = 0;
            for (auto& [k, v] : lastBets_) need += v;
            if (need <= P().coins) bets_ = lastBets_;
            else E().toast("Not enough coins to rebet", col::red);
        }
        if (ui::button({910, 560, 330, 60}, spinning_ ? "SPINNING..." : "SPIN", col::red, 30, idle && !bets_.empty())) spin();
        ui::text("left click: place chip   right click: remove", 560, 645, 16, alpha(WHITE, 0.5f));

        // history
        ui::text("LAST", 560, 690, 16, col::muted, ui::Left, true);
        for (size_t i = 0; i < history_.size(); i++) {
            Rectangle r{610 + i * 52.0f, 672, 46, 36};
            DrawRectangleRounded(r, 0.3f, 6, numColor(history_[i]));
            ui::text(std::to_string(history_[i]), r.x + 23, r.y + 18, 20, WHITE, ui::Center, true);
        }

        if (!spinning_ && showResultT_ > 0 && !history_.empty()) {
            int n = history_[0];
            float k = easeOutBack(clamp01((3.5f - showResultT_) / 0.3f));
            Rectangle r{kWheelC.x - 110, 112, 220, 70};
            DrawRectangleRounded(r, 0.4f, 10, numColor(n));
            DrawRectangleRoundedLinesEx(r, 0.4f, 10, 3, col::gold);
            ui::text(std::to_string(n) + (n == 0 ? " GREEN" : isRed(n) ? " RED" : " BLACK"), r.x + r.width / 2, r.y + 35, 34 * k, WHITE, ui::Center, true);
            if (lastWin_ > 0) ui::textOutline("+" + fmtNum(lastWin_), kWheelC.x, 660, 44, col::gold, col::outline, 3);
        }
        fx_.draw();
        if (casino::header("ROULETTE") && !spinning_) E().go(makeCasino);
    }

private:
    std::vector<Spot> spots_;
    std::map<std::string, long long> bets_, lastBets_;
    std::vector<int> history_;
    long long chip_ = 5, lastWin_ = 0;
    bool spinning_ = false;
    int resultIdx_ = 0;
    float t_ = 0, spinT_ = 0, wheelRot_ = 0, wheelStart_ = 0, wheelEnd_ = 0, tickAcc_ = 0, showResultT_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeRoulette() { return std::make_unique<RouletteScene>(); }

}  // namespace pg
