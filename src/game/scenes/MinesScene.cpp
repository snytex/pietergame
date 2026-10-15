// MINES: 5x5 grid hiding stroopwafels and bombs. Every stroopwafel raises the
// multiplier; cash out before you hit a bomb. 3% house edge.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <array>

namespace pg {
namespace {

constexpr int kTiles = 25;
constexpr float kTile = 104, kGap = 10;
constexpr Vector2 kGrid{110, 110};

struct Tile {
    bool mine = false, open = false;
    float reveal = 0;  // flip animation 0..1
};

double multiplierFor(int mines, int picks) {
    double m = 1;
    for (int i = 0; i < picks; i++) m *= (double)(kTiles - i) / (kTiles - mines - i);
    return m * 0.97;
}

class MinesScene : public Scene {
public:
    void enter() override { bet_ = std::clamp<long long>(20, 1, std::max<long long>(1, P().coins)); }

    void start() {
        if (!P().spend(bet_)) { E().sfx.play(Sfx::Error); return; }
        P().wager(bet_);
        stake_ = bet_;
        tiles_ = {};
        std::array<int, kTiles> idx;
        for (int i = 0; i < kTiles; i++) idx[i] = i;
        std::shuffle(idx.begin(), idx.end(), rng());
        for (int i = 0; i < mines_; i++) tiles_[idx[i]].mine = true;
        picks_ = 0;
        playing_ = true;
        busted_ = false;
        lastWin_ = 0;
        E().sfx.play(Sfx::Card);
    }

    void cashOut() {
        if (!playing_ || picks_ == 0) return;
        long long win = (long long)std::floor(stake_ * multiplierFor(mines_, picks_));
        P().earn(win, true);
        lastWin_ = win;
        playing_ = false;
        revealAll();
        E().sfx.play(Sfx::Win);
        fx_.confetti({kGrid.x + 2.5f * (kTile + kGap), kGrid.y + 2.5f * (kTile + kGap)}, 80);
        P().save();
    }

    void revealAll() {
        for (auto& t : tiles_) t.open = true;
    }

    void pick(int i) {
        Tile& t = tiles_[i];
        if (t.open || !playing_) return;
        t.open = true;
        Vector2 c = tileCenter(i);
        if (t.mine) {
            busted_ = true;
            playing_ = false;
            E().sfx.play(Sfx::Boom);
            E().shake(14);
            E().flash(col::red, 0.4f);
            fx_.burst(c, 50, col::orange, 500, 9, 0.8f);
            fx_.burst(c, 30, rgba(0x55556aff), 300, 12, 1.0f);
            revealT_ = 0.4f;
            P().save();
        } else {
            picks_++;
            E().sfx.play(Sfx::Gem, 0.8f, 1 + picks_ * 0.04f);
            fx_.burst(c, 14, col::wafel, 240, 5, 0.5f, PShape::Star);
            if (picks_ == kTiles - mines_) cashOut();
        }
    }

    Vector2 tileCenter(int i) const {
        return {kGrid.x + (i % 5) * (kTile + kGap) + kTile / 2, kGrid.y + (i / 5) * (kTile + kGap) + kTile / 2};
    }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        for (auto& t : tiles_) t.reveal = approach(t.reveal, t.open ? 1.0f : 0.0f, dt * 5);
        if (revealT_ > 0) {
            revealT_ -= dt;
            if (revealT_ <= 0) revealAll();
        }
        if (E().backPressed() && !playing_) E().go(makeCasino);
    }

    void draw() override {
        casino::felt(t_);
        Rectangle board{kGrid.x - 20, kGrid.y - 20, 5 * kTile + 4 * kGap + 40, 5 * kTile + 4 * kGap + 40};
        ui::panel(board, alpha(col::bgDeep, 0.85f), 0.05f);
        for (int i = 0; i < kTiles; i++) {
            Tile& t = tiles_[i];
            Vector2 c = tileCenter(i);
            Rectangle r{c.x - kTile / 2, c.y - kTile / 2, kTile, kTile};
            bool hot = playing_ && !t.open && ui::hovered(r);
            float k = std::abs(t.reveal * 2 - 1);
            bool face = t.reveal > 0.5f;
            Rectangle d{c.x - kTile * k / 2, r.y - (hot ? 4 : 0), kTile * k, kTile};
            if (!face) {
                DrawRectangleRounded({d.x, d.y + 6, d.width, d.height}, 0.2f, 8, darken(col::panelHi, 0.5f));
                DrawRectangleRounded(d, 0.2f, 8, hot ? lighten(col::panelHi, 0.2f) : col::panelHi);
                DrawRectangleRounded({d.x + 6, d.y + 5, d.width - 12, d.height * 0.35f}, 0.4f, 8, alpha(WHITE, 0.08f));
            } else {
                Color bg = t.mine ? rgba(0x5a1a24ff) : rgba(0x2a1f4aff);
                DrawRectangleRounded(d, 0.2f, 8, bg);
                if (k > 0.3f) {
                    if (t.mine) art::bomb(c, 30 * k, t_);
                    else art::stroopwafelTop(c, 34 * k);
                }
            }
            if (hot && E().mousePressed()) {
                E().consumeClick();
                pick(i);
            }
        }

        // side panel
        Rectangle side{720, 110, 520, 560};
        ui::panel(side);
        ui::text("MINES", side.x + 24, side.y + 36, 34, col::text, ui::Left, true);
        ui::betSelector({side.x + 20, side.y + 70, side.width - 40, 100}, bet_, P().coins, !playing_);

        ui::text("BOMBS", side.x + 24, side.y + 205, 18, col::muted, ui::Left, true);
        const int presets[] = {1, 3, 5, 10, 24};
        for (int i = 0; i < 5; i++) {
            Rectangle b{side.x + 110 + i * 78.0f, side.y + 186, 70, 40};
            if (ui::button(b, std::to_string(presets[i]), mines_ == presets[i] ? col::red : col::panelHi, 20, !playing_)) mines_ = presets[i];
        }
        if (ui::button({side.x + 24, side.y + 236, 50, 36}, "-", col::panelHi, 22, !playing_)) mines_ = std::max(1, mines_ - 1);
        ui::text(std::to_string(mines_) + " bombs", side.x + 130, side.y + 254, 24, col::text, ui::Center, true);
        if (ui::button({side.x + 186, side.y + 236, 50, 36}, "+", col::panelHi, 22, !playing_)) mines_ = std::min(24, mines_ + 1);

        double cur = multiplierFor(mines_, picks_), next = multiplierFor(mines_, picks_ + 1);
        ui::text(TextFormat("current  %.2fx", playing_ ? cur : 1.0), side.x + 24, side.y + 310, 24, col::gold, ui::Left, true);
        ui::text(TextFormat("next pick  %.2fx", playing_ ? next : multiplierFor(mines_, 1)), side.x + 24, side.y + 345, 20, col::muted);
        if (playing_)
            ui::text(TextFormat("safe tiles left: %d", kTiles - mines_ - picks_), side.x + side.width - 24, side.y + 345, 18, col::muted, ui::Right);

        if (!playing_) {
            if (ui::button({side.x + 24, side.y + 390, side.width - 48, 72}, "START", col::green, 32, P().coins >= bet_ && P().coins > 0)) start();
        } else {
            long long cash = (long long)std::floor(stake_ * cur);
            if (ui::button({side.x + 24, side.y + 390, side.width - 48, 72}, picks_ ? "CASH OUT  " + fmtNum(cash) : "PICK A TILE", col::gold, 30, picks_ > 0))
                cashOut();
        }
        if (busted_) ui::textOutline("BOOM. -" + fmtNum(stake_), side.x + side.width / 2, side.y + 510, 40, col::red, col::outline, 3);
        else if (lastWin_ > 0) ui::textOutline("+" + fmtNum(lastWin_), side.x + side.width / 2, side.y + 510, 44, col::gold, col::outline, 3);

        fx_.draw();
        if (casino::header("MINES") && !playing_) E().go(makeCasino);
    }

private:
    std::array<Tile, kTiles> tiles_{};
    int mines_ = 3, picks_ = 0;
    bool playing_ = false, busted_ = false;
    long long bet_ = 20, stake_ = 0, lastWin_ = 0;
    float t_ = 0, revealT_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeMines() { return std::make_unique<MinesScene>(); }

}  // namespace pg
