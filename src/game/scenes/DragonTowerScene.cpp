// DRAGON TOWER: climb a 9-row tower. Every row hides golden eggs and dragons;
// pick an egg to climb a row and raise the multiplier, pick a dragon and you're
// toast. Cash out whenever you like. 3% house edge, like Mines.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <array>

namespace pg {
namespace {

constexpr int kRows = 9;
constexpr Rectangle kTower{80, 150, 560, 538};
constexpr float kRowH = 46, kRowGap = 7;

struct Diff { const char* name; int tiles, dragons; Color c; };
const Diff kDiffs[5] = {{"EASY", 4, 1, rgba(0x3ddc84ff)},
                        {"MEDIUM", 3, 1, rgba(0x3cd6ffff)},
                        {"HARD", 2, 1, rgba(0xffc531ff)},
                        {"EXPERT", 3, 2, rgba(0xff8a2bff)},
                        {"MASTER", 4, 3, rgba(0xff4a5aff)}};

double multiplierFor(const Diff& d, int rows) {
    if (rows <= 0) return 1;
    return std::pow((double)d.tiles / (d.tiles - d.dragons), rows) * 0.97;
}

// golden egg with speckles and a shine
void egg(Vector2 c, float r, float glow) {
    if (glow > 0) {
        fx::beginAdd();
        fx::glow(c, r * 3, alpha(col::gold, 0.35f * glow));
        fx::endAdd();
    }
    DrawEllipse((int)c.x, (int)(c.y + r * 0.95f), r * 0.8f, r * 0.2f, alpha(BLACK, 0.3f));
    DrawEllipse((int)c.x, (int)c.y, r * 0.78f + 2.5f, r + 2.5f, col::outline);
    DrawEllipse((int)c.x, (int)c.y, r * 0.78f, r, rgba(0xf0b830ff));
    DrawEllipse((int)(c.x - r * 0.08f), (int)(c.y - r * 0.08f), r * 0.62f, r * 0.84f, rgba(0xffd860ff));
    for (int i = 0; i < 6; i++) {
        float a = i * 1.9f, d = r * (0.25f + 0.08f * (i % 3));
        DrawCircleV({c.x + std::cos(a) * d * 0.8f, c.y + std::sin(a) * d}, r * 0.07f, alpha(rgba(0xb07810ff), 0.7f));
    }
    DrawEllipse((int)(c.x - r * 0.28f), (int)(c.y - r * 0.4f), r * 0.16f, r * 0.26f, alpha(WHITE, 0.75f));
}

// green dragon head, facing the viewer
void dragonHead(Vector2 c, float r, float t, float fire) {
    Color scale = rgba(0x2f9a4aff), dark = rgba(0x1c6a30ff), belly = rgba(0xb8e070ff);
    // horns + frills
    for (int s : {-1, 1}) {
        Vector2 a{c.x + s * r * 0.45f, c.y - r * 0.55f}, b{c.x + s * r * 0.95f, c.y - r * 1.25f}, d{c.x + s * r * 0.75f, c.y - r * 0.35f};
        DrawTriangle(a, s < 0 ? b : d, s < 0 ? d : b, col::outline);
        Vector2 a2 = Vector2Lerp(a, d, 0.15f), b2 = Vector2Lerp(b, c, 0.06f);
        DrawTriangle(a2, s < 0 ? b2 : d, s < 0 ? d : b2, rgba(0xe8dcc0ff));
        DrawTriangle({c.x + s * r * 0.9f, c.y - r * 0.1f}, {c.x + s * r * 1.3f, c.y - r * 0.05f}, {c.x + s * r * 0.85f, c.y + r * 0.25f}, dark);
        DrawTriangle({c.x + s * r * 0.9f, c.y - r * 0.1f}, {c.x + s * r * 0.85f, c.y + r * 0.25f}, {c.x + s * r * 1.3f, c.y - r * 0.05f}, dark);
    }
    // head + snout
    DrawCircleV(c, r + 2.5f, col::outline);
    DrawCircleGradient(c, r, lighten(scale, 0.15f), dark);
    DrawEllipse((int)c.x, (int)(c.y + r * 0.45f), r * 0.66f + 2.5f, r * 0.48f + 2.5f, col::outline);
    DrawEllipse((int)c.x, (int)(c.y + r * 0.45f), r * 0.66f, r * 0.48f, belly);
    // brow ridges + glowing eyes with slit pupils
    for (int s : {-1, 1}) {
        Vector2 e{c.x + s * r * 0.4f, c.y - r * 0.12f};
        DrawEllipse((int)e.x, (int)e.y, r * 0.2f, r * 0.15f, col::outline);
        DrawEllipse((int)e.x, (int)e.y, r * 0.17f, r * 0.12f, rgba(0xffd030ff));
        DrawEllipse((int)e.x, (int)e.y, r * 0.04f, r * 0.11f, col::outline);
        DrawLineEx({e.x - s * r * 0.25f, e.y - r * 0.22f}, {e.x + s * r * 0.2f, e.y - r * 0.12f}, std::max(2.0f, r * 0.08f), dark);
    }
    // nostrils + teeth
    DrawCircleV({c.x - r * 0.2f, c.y + r * 0.32f}, r * 0.06f, dark);
    DrawCircleV({c.x + r * 0.2f, c.y + r * 0.32f}, r * 0.06f, dark);
    DrawLineEx({c.x - r * 0.45f, c.y + r * 0.62f}, {c.x + r * 0.45f, c.y + r * 0.62f}, std::max(2.0f, r * 0.05f), col::outline);
    for (int i = -2; i <= 2; i++)
        DrawTriangle({c.x + i * r * 0.17f - r * 0.06f, c.y + r * 0.62f}, {c.x + i * r * 0.17f, c.y + r * 0.74f}, {c.x + i * r * 0.17f + r * 0.06f, c.y + r * 0.62f}, WHITE);
    // smoke / fire from the nostrils
    if (fire > 0) {
        fx::beginAdd();
        for (int i = 0; i < 6; i++) {
            float k = std::fmod(t * 2 + i / 6.0f, 1.0f);
            Vector2 p{c.x + std::sin(i * 2.1f) * r * 0.3f * k, c.y + r * 0.4f + k * r * 1.2f};
            fx::glow(p, r * (0.3f + k * 0.5f), alpha(mix(col::gold, col::red, k), fire * (1 - k) * 0.7f));
        }
        fx::endAdd();
    }
}

class DragonTowerScene : public Scene {
public:
    Presence presence() const override { return {"Casino - Dragon Tower", "", "casino", "Pieter Casino", "logo"}; }

    void enter() override {
        bet_ = std::clamp<long long>(20, 1, std::max<long long>(1, P().coins));
        picks_.fill(-1);
        if (std::getenv("PG_TOWER_DEMO")) {  // dev hook: a climb in progress
            start();
            for (int r = 0; r < 4; r++)
                for (int i = 0; i < diff().tiles; i++)
                    if (!dragons_[r][i]) { pick(i); break; }
        }
    }

    const Diff& diff() const { return kDiffs[diff_]; }

    void start() {
        if (!P().spend(bet_)) { E().sfx.play(Sfx::Error); return; }
        P().wager(bet_);
        stake_ = bet_;
        const Diff& d = diff();
        for (auto& row : dragons_) {
            row = {};
            std::array<int, 4> idx{0, 1, 2, 3};
            std::shuffle(idx.begin(), idx.begin() + d.tiles, rng());
            for (int i = 0; i < d.dragons; i++) row[idx[i]] = true;
        }
        picks_.fill(-1);
        row_ = 0;
        playing_ = true;
        busted_ = false;
        revealAll_ = false;
        lastWin_ = 0;
        bustRow_ = -1;
        E().sfx.play(Sfx::Card);
    }

    void cashOut() {
        if (!playing_ || row_ == 0) return;
        long long win = (long long)std::floor(stake_ * multiplierFor(diff(), row_));
        P().earn(win, true);
        lastWin_ = win;
        playing_ = false;
        revealAll_ = true;
        E().sfx.play(Sfx::Win);
        fx_.confetti({kTower.x + kTower.width / 2, rowY(row_ - 1)}, 90);
        P().save();
    }

    void pick(int tile) {
        if (!playing_) return;
        picks_[row_] = tile;
        Vector2 c = tileCenter(row_, tile);
        if (dragons_[row_][tile]) {
            busted_ = true;
            playing_ = false;
            revealAll_ = true;
            bustRow_ = row_;
            fireT_ = 1.6f;
            E().sfx.play(Sfx::Boom);
            E().sfx.play(Sfx::Hurt, 0.8f, 0.5f);
            E().shake(16);
            E().flash(col::orange, 0.35f);
            fx_.burst(c, 60, col::orange, 520, 10, 0.9f);
            fx_.burst(c, 30, col::gold, 380, 7, 0.7f, PShape::Spark);
            P().save();
        } else {
            row_++;
            E().sfx.play(Sfx::Gem, 0.8f, 1 + row_ * 0.06f);
            fx_.burst(c, 18, col::gold, 260, 6, 0.6f, PShape::Star);
            climbPop_ = 1;
            if (row_ == kRows) cashOut();
        }
    }

    // row 0 is the bottom of the tower
    float rowY(int r) const { return kTower.y + kTower.height - 20 - (r + 0.5f) * (kRowH + kRowGap); }
    Rectangle tileRect(int r, int i) const {
        const Diff& d = diff();
        float x0 = kTower.x + 34, w = 400, gap = 10, tw = (w - (d.tiles - 1) * gap) / d.tiles;
        return {x0 + i * (tw + gap), rowY(r) - kRowH / 2, tw, kRowH};
    }
    Vector2 tileCenter(int r, int i) const { return center(tileRect(r, i)); }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        fireT_ = std::max(0.0f, fireT_ - dt);
        climbPop_ = std::max(0.0f, climbPop_ - dt * 3);
        if (E().backPressed() && !playing_) E().go(makeCasino);
    }

    void drawTower() {
        Rectangle t = kTower;
        // stone tower with battlements
        fx::softShadow({t.x + t.width / 2, t.y + t.height / 2 + 20}, t.width * 0.6f, t.height * 0.55f, 0.6f);
        fx::gradRoundRect(t, 0.03f, rgba(0x4a4258ff), rgba(0x24202eff));
        for (int r = 0; r < 18; r++)
            for (int k = 0; k < 9; k++) {
                float bx = t.x + 6 + k * 64 + (r % 2) * 32 - 32, by = t.y + 6 + r * 30;
                Rectangle b{std::max(bx, t.x + 6), by, 58, 26};
                if (b.x + b.width > t.x + t.width - 6) b.width = t.x + t.width - 6 - b.x;
                if (b.width > 4) DrawRectangleRounded(b, 0.2f, 4, alpha(WHITE, 0.035f + 0.02f * ((r * 7 + k * 3) % 3)));
            }
        for (int k = 0; k < 7; k++) {
            Rectangle m{t.x + 8 + k * 80, t.y - 30, 52, 34};
            fx::gradRoundRect(m, 0.12f, rgba(0x5a5268ff), rgba(0x3a3448ff));
            DrawRectangleRoundedLinesEx(m, 0.12f, 6, 2, col::outline);
        }
        DrawRectangleRoundedLinesEx(t, 0.03f, 8, 3, col::outline);
        // torches either side
        for (int s : {-1, 1}) {
            Vector2 p{s < 0 ? t.x - 18 : t.x + t.width + 18, t.y + 120};
            DrawRectangleRec({p.x - 4, p.y, 8, 30}, rgba(0x3a2418ff));
            fx::beginAdd();
            fx::glow({p.x, p.y - 6}, 70 + std::sin(t_ * 13 + s) * 6, alpha(col::orange, 0.45f));
            fx::endAdd();
            DrawCircleV({p.x, p.y - 6}, 7, col::gold);
        }

        const Diff& d = diff();
        for (int r = 0; r < kRows; r++) {
            bool active = playing_ && r == row_;
            // multiplier label for the row
            double m = multiplierFor(d, r + 1);
            Color lc = active ? col::gold : r < row_ ? col::green : alpha(col::muted, 0.8f);
            ui::text(m >= 1000 ? TextFormat("%.0fx", m) : TextFormat("%.2fx", m), t.x + t.width - 24, rowY(r), 17, lc, ui::Right, true);
            if (active) {
                fx::beginAdd();
                fx::glow({t.x + 234, rowY(r)}, 260, alpha(d.c, 0.18f + 0.06f * std::sin(t_ * 5)));
                fx::endAdd();
            }
            for (int i = 0; i < d.tiles; i++) {
                Rectangle tr = tileRect(r, i);
                bool hot = active && ui::hovered(tr);
                bool picked = picks_[r] == i;
                bool show = picked || revealAll_;
                Rectangle dr{tr.x, tr.y - (hot ? 3.0f : 0.0f), tr.width, tr.height};
                if (!show) {
                    Color base = active ? mix(darken(d.c, 0.3f), d.c, hot ? 0.6f : 0.25f) : rgba(0x3a3050ff);
                    DrawRectangleRounded({dr.x, dr.y + 4, dr.width, dr.height}, 0.3f, 8, darken(base, 0.55f));
                    fx::gradRoundRect(dr, 0.3f, lighten(base, 0.15f), darken(base, 0.15f));
                    DrawLineEx({dr.x + 12, dr.y + 2.5f}, {dr.x + dr.width - 12, dr.y + 2.5f}, 1.6f, alpha(WHITE, active ? 0.35f : 0.12f));
                    DrawRectangleRoundedLinesEx(dr, 0.3f, 8, 2, active ? lighten(d.c, 0.2f) : col::outline);
                    if (active) DrawCircleV(center(dr), 4, alpha(WHITE, 0.5f));
                } else {
                    bool dragon = dragons_[r][i];
                    float dim = picked ? 1.0f : 0.45f;
                    fx::gradRoundRect(dr, 0.3f, alpha(dragon ? rgba(0x3a0c10ff) : rgba(0x2a2010ff), 0.9f), alpha(rgba(0x120a08ff), 0.9f));
                    DrawRectangleRoundedLinesEx(dr, 0.3f, 8, 2, alpha(dragon ? col::red : col::gold, picked ? 0.9f : 0.35f));
                    Vector2 c = center(dr);
                    if (dragon) {
                        dragonHead(c, 18 * (picked ? 1.1f : 0.9f), t_, picked ? 1.0f : 0.0f);
                        if (!picked) DrawRectangleRounded(dr, 0.3f, 8, alpha(BLACK, 1 - dim));
                    } else {
                        egg(c, 17, picked ? 1.0f : 0.0f);
                        if (!picked) DrawRectangleRounded(dr, 0.3f, 8, alpha(BLACK, 1 - dim));
                    }
                }
                if (hot && E().mousePressed()) {
                    E().consumeClick();
                    pick(i);
                }
            }
        }
        // the dragon guarding the top
        float breathe = std::sin(t_ * 1.8f) * 4;
        dragonHead({t.x + t.width / 2, t.y - 46 + breathe}, 48 + climbPop_ * 4, t_, busted_ ? std::max(0.3f, fireT_) : 0.25f);
    }

    void draw() override {
        casino::felt(t_);
        drawTower();

        Rectangle side{720, 110, 520, 578};
        ui::panel(side);
        ui::text("DRAGON TOWER", side.x + 24, side.y + 36, 32, col::text, ui::Left, true);
        ui::betSelector({side.x + 20, side.y + 66, side.width - 40, 100}, bet_, P().coins, !playing_);

        ui::text("DIFFICULTY", side.x + 24, side.y + 192, 16, col::muted, ui::Left, true);
        for (int i = 0; i < 5; i++) {
            Rectangle b{side.x + 20 + i * 97.0f, side.y + 208, 90, 42};
            if (ui::button(b, kDiffs[i].name, diff_ == i ? kDiffs[i].c : col::panelHi, 15, !playing_)) diff_ = i;
        }
        const Diff& d = diff();
        ui::text(TextFormat("%d tiles per row, %d dragon%s", d.tiles, d.dragons, d.dragons > 1 ? "s" : ""), side.x + 24, side.y + 272, 17, col::muted);
        ui::text(TextFormat("top of the tower: %.2fx", multiplierFor(d, kRows)), side.x + side.width - 24, side.y + 272, 17, d.c, ui::Right, true);

        double cur = multiplierFor(d, row_), next = multiplierFor(d, row_ + 1);
        ui::text(TextFormat("current  %.2fx", playing_ ? cur : 1.0), side.x + 24, side.y + 318, 24, col::gold, ui::Left, true);
        ui::text(TextFormat("next row  %.2fx", playing_ ? next : multiplierFor(d, 1)), side.x + 24, side.y + 352, 20, col::muted);
        if (playing_) ui::text(TextFormat("row %d / %d", row_ + 1, kRows), side.x + side.width - 24, side.y + 352, 18, col::muted, ui::Right);

        if (!playing_) {
            if (ui::button({side.x + 24, side.y + 388, side.width - 48, 70}, "START CLIMBING", col::green, 30, P().coins >= bet_ && P().coins > 0)) start();
        } else {
            long long cash = (long long)std::floor(stake_ * cur);
            if (ui::button({side.x + 24, side.y + 388, side.width - 48, 70}, row_ ? "CASH OUT  " + fmtNum(cash) : "PICK A TILE", col::gold, 30, row_ > 0))
                cashOut();
            if (ui::button({side.x + 24, side.y + 466, side.width - 48, 44}, "RANDOM PICK", col::panelHi, 18)) pick(rndi(0, d.tiles - 1));
        }
        if (busted_) ui::textOutline("TOASTED. -" + fmtNum(stake_), side.x + side.width / 2, side.y + 540, 36, col::red, col::outline, 3);
        else if (lastWin_ > 0) ui::textOutline("+" + fmtNum(lastWin_), side.x + side.width / 2, side.y + 540, 42, col::gold, col::outline, 3);
        else if (!playing_) ui::text("pick eggs, dodge dragons, climb to the top", side.x + side.width / 2, side.y + 540, 17, col::muted, ui::Center);

        fx_.draw();
        if (casino::header("DRAGON TOWER") && !playing_) E().go(makeCasino);
    }

private:
    std::array<std::array<bool, 4>, kRows> dragons_{};
    std::array<int, kRows> picks_{};
    int diff_ = 1, row_ = 0, bustRow_ = -1;
    bool playing_ = false, busted_ = false, revealAll_ = false;
    long long bet_ = 20, stake_ = 0, lastWin_ = 0;
    float t_ = 0, fireT_ = 0, climbPop_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeDragonTower() { return std::make_unique<DragonTowerScene>(); }

}  // namespace pg
