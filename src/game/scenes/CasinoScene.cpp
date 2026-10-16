// CASINO hub: pick a table. Also home of Pieter's pity fund for broke players.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/Carousel.hpp"
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

    void enter() override {
        carousel_.setTiles({{"roulette", "ROULETTE", "European wheel. All in on 17?", "", rgba(0xb8283aff), makeRoulette},
                            {"blackjack", "BLACKJACK", "Beat Davi the dealer to 21.", "", rgba(0x2a7ab8ff), makeBlackjack},
                            {"crash", "CRASH", "Cash out before the rocket blows.", "NEW", rgba(0x2a9a6aff), makeCrash},
                            {"plinko", "PLINKO", "Drop Pieters, watch them bounce.", "NEW", rgba(0xd84a8aff), makePlinko},
                            {"mines", "MINES", "Find stroopwafels, avoid bombs.", "", rgba(0x6a3ad8ff), makeMines},
                            {"tower", "DRAGON TOWER", "Climb for eggs. Don't wake it.", "", rgba(0x2a8a4aff), makeDragonTower},
                            {"slots", "SLOTS", "Three Pieters pays 500x.", "", rgba(0xc8901aff), makeSlots}});
        carousel_.tileW = 250;
    }

    void draw() override {
        casino::felt(t_);
        // marquee lights
        for (int i = 0; i < 32; i++) {
            bool on = ((int)(t_ * 6) + i) % 3 == 0;
            DrawCircleV({20 + i * 40.0f, 94}, 5, on ? col::gold : alpha(col::gold, 0.25f));
        }
        if (casino::header("PIETER CASINO")) E().go(makeMenu);
        if (const Tile* tl = carousel_.run({0, 130, (float)VW, 360}, t_)) E().go(tl->go);

        float x0 = 40;
        Rectangle st{x0, 548, VW - 80.0f, 70};
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
    Carousel carousel_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeCasino() { return std::make_unique<CasinoScene>(); }

}  // namespace pg
