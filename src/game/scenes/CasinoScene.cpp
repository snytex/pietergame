// CASINO hub: pick a table. Also home of Pieter's pity fund for broke players.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

namespace pg {
namespace {

class CasinoScene : public Scene {
public:
    Presence presence() const override { return {"Pieter Casino", "", "casino", "Pieter Casino", "logo"}; }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    void drawTable(Rectangle r, const char* name, const char* desc, Color c, int icon, SceneFactory go) {
        bool hot = ui::hovered(r);
        float lift = hot ? -8 : 0;
        Rectangle d{r.x, r.y + lift, r.width, r.height};
        ui::panel(d, hot ? lighten(col::panel, 0.06f) : col::panel, 0.12f, hot ? c : col::outline);
        Vector2 ic{d.x + d.width / 2, d.y + 120};
        DrawCircleGradient(ic, 110, alpha(c, 0.35f), alpha(c, 0));
        switch (icon) {
            case 0: {  // mini roulette wheel
                for (int i = 0; i < 18; i++)
                    DrawCircleSector(ic, 72, t_ * 40 + i * 20, t_ * 40 + (i + 1) * 20, 4, i == 0 ? col::green : (i % 2 ? col::red : rgba(0x1d1a24ff)));
                DrawCircleV(ic, 48, rgba(0x6b3a1aff));
                DrawCircleV(ic, 16, col::gold);
                DrawCircleV(ic + fromAngle(-t_ * 3, 68), 7, WHITE);
                break;
            }
            case 1:
                art::card({ic.x - 80, ic.y - 60, 80, 116}, 1, 0, true);
                art::card({ic.x - 10, ic.y - 70, 80, 116}, 13, 1, true);
                break;
            case 2:
                for (int y = 0; y < 3; y++)
                    for (int x = 0; x < 3; x++) {
                        Vector2 p{ic.x - 56 + x * 56.0f, ic.y - 56 + y * 56.0f};
                        DrawRectangleRounded({p.x - 24, p.y - 24, 48, 48}, 0.25f, 6, col::panelHi);
                        if ((x + y) % 4 == 0) art::gem(p, 16, col::cyan);
                        if (x == 2 && y == 0) art::bomb(p, 14, t_);
                    }
                break;
            case 4:
                // mini tower: rows of tiles, an egg and a dragon peeking out
                for (int r = 0; r < 4; r++)
                    for (int k = 0; k < 3; k++) {
                        Rectangle tl{ic.x - 75 + k * 52.0f, ic.y + 60 - r * 36.0f, 46, 30};
                        DrawRectangleRounded(tl, 0.3f, 6, r == 1 ? rgba(0x3a8a4aff) : col::panelHi);
                        DrawRectangleRoundedLinesEx(tl, 0.3f, 6, 2, col::outline);
                    }
                DrawEllipse((int)ic.x, (int)ic.y + 39, 9, 12, col::outline);
                DrawEllipse((int)ic.x, (int)ic.y + 39, 7, 10, rgba(0xffd860ff));
                DrawCircleV({ic.x, ic.y - 76}, 28, col::outline);
                DrawCircleV({ic.x, ic.y - 76}, 26, rgba(0x2f9a4aff));
                DrawEllipse((int)ic.x, (int)ic.y - 64, 15, 10, rgba(0xb8e070ff));
                DrawCircleV({ic.x - 9, ic.y - 82}, 5, rgba(0xffd030ff));
                DrawCircleV({ic.x + 9, ic.y - 82}, 5, rgba(0xffd030ff));
                DrawTriangle({ic.x - 18, ic.y - 92}, {ic.x - 26, ic.y - 114}, {ic.x - 8, ic.y - 98}, rgba(0xe8dcc0ff));
                DrawTriangle({ic.x + 18, ic.y - 92}, {ic.x + 8, ic.y - 98}, {ic.x + 26, ic.y - 114}, rgba(0xe8dcc0ff));
                break;
            case 3:
                art::portrait("pieter", {ic.x - 64, ic.y}, 30);
                art::portrait("pieter", {ic.x, ic.y}, 30);
                art::portrait("pieter", {ic.x + 64, ic.y}, 30);
                ui::text("JACKPOT", ic.x, ic.y + 60, 22, col::gold, ui::Center, true);
                break;
        }
        ui::text(name, d.x + d.width / 2, d.y + d.height - 80, 34, col::text, ui::Center, true);
        ui::paragraph(desc, {d.x + 22, d.y + d.height - 56, d.width - 44, 50}, 16, col::muted);
        if (hot && E().mousePressed()) {
            E().consumeClick();
            E().sfx.play(Sfx::Click);
            E().go(go);
        }
    }

    void draw() override {
        casino::felt(t_);
        // marquee lights
        for (int i = 0; i < 32; i++) {
            bool on = ((int)(t_ * 6) + i) % 3 == 0;
            DrawCircleV({20 + i * 40.0f, 94}, 5, on ? col::gold : alpha(col::gold, 0.25f));
        }
        if (casino::header("PIETER CASINO")) E().go(makeMenu);

        float w = 232, gap = 14, x0 = VW / 2.0f - (5 * w + 4 * gap) / 2;
        drawTable({x0, 130, w, 380}, "ROULETTE", "European wheel. Red, black, or all in on 17.", col::red, 0, makeRoulette);
        drawTable({x0 + (w + gap), 130, w, 380}, "BLACKJACK", "Beat Davi the dealer to 21. Pays 3:2.", col::cyan, 1, makeBlackjack);
        drawTable({x0 + 2 * (w + gap), 130, w, 380}, "MINES", "Find stroopwafels, avoid the bombs.", col::violet, 2, makeMines);
        drawTable({x0 + 3 * (w + gap), 130, w, 380}, "SLOTS", "Three Pieters in a row pays 500x.", col::gold, 3, makeSlots);
        drawTable({x0 + 4 * (w + gap), 130, w, 380}, "DRAGON TOWER", "Climb for eggs. Don't wake the dragon.", col::green, 4, makeDragonTower);

        Rectangle st{x0, 540, 5 * w + 4 * gap, 70};
        ui::panel(st, alpha(col::bgDeep, 0.8f));
        auto& s = P().stats;
        ui::text("Total wagered: " + fmtNum(s.totalWagered), st.x + 30, st.y + 35, 22, col::text);
        ui::text("Biggest win: " + fmtNum(s.biggestWin), st.x + st.width / 2, st.y + 35, 22, col::gold, ui::Center);

        // pity fund
        Rectangle pb{st.x + st.width - 330, st.y + 10, 310, 50};
        if (P().canPity()) {
            if (ui::button(pb, "ASK PIETER FOR MONEY", col::pink, 20)) {
                P().takePity();
                E().sfx.play(Sfx::Coin);
                fx_.confetti({pb.x + pb.width / 2, pb.y}, 60);
                E().toast("Pieter: \"fine. here's 150. don't tell Davi\"", col::pink);
            }
        } else if (P().coins < 25) {
            long long s2 = P().pitySecondsLeft();
            ui::text(TextFormat("Pieter's pity fund in %lld:%02lld", s2 / 60, s2 % 60), pb.x + pb.width - 10, pb.y + 25, 20, col::muted, ui::Right);
        } else {
            ui::text("gamble responsibly (Pieter doesn't)", pb.x + pb.width - 10, pb.y + 25, 18, col::muted, ui::Right);
        }
        ui::text("Pietercoins are fake. Real gambling is not a strategy, whatever Pieter says.", VW / 2.0f, VH - 30, 16, alpha(WHITE, 0.45f), ui::Center);
        fx_.draw();
    }

private:
    float t_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeCasino() { return std::make_unique<CasinoScene>(); }

}  // namespace pg
