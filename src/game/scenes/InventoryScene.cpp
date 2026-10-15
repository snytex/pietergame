// INVENTORY: Classic loadout (3 powerup slots), cosmetics, food stash, stats.
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

class InventoryScene : public Scene {
public:
    MusicMode music() const override { return MusicMode::Title; }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        // trail preview orbiting the portrait
        if (!P().trail.empty()) {
            trailT_ -= dt;
            Vector2 p = previewPos();
            if (trailT_ <= 0) {
                trailT_ = 0.02f;
                Particle pt;
                pt.pos = p + rndDir() * 6;
                pt.life = pt.maxLife = 0.6f;
                pt.size = rnd(5, 8);
                if (P().trail == "trail_rainbow") pt.color = ColorFromHSV(std::fmod(t_ * 360, 360), 0.8f, 1);
                else if (P().trail == "trail_fire") { pt.color = mix(col::gold, col::red, rnd()); pt.vel = {0, -60}; }
                else pt.color = rgba(0x3a2418ff);
                fx_.add(pt);
            }
        }
        if (E().backPressed()) E().go(makeMenu);
    }

    Vector2 previewPos() const { return {1060 + std::cos(t_ * 1.6f) * 110, 250 + std::sin(t_ * 3.2f) * 40}; }

    void draw() override {
        art::purpleBackdrop(t_);
        ui::titleBar("INVENTORY", col::violet);
        if (ui::backButton()) E().go(makeMenu);
        ui::coinCounter(VW - 20, 38, P().coins);

        // ---------------- loadout
        Rectangle lo{30, 90, 560, 300};
        ui::panel(lo);
        ui::text("CLASSIC LOADOUT", lo.x + 20, lo.y + 28, 24, col::text, ui::Left, true);
        ui::text("pick a slot, then a powerup. keys 1 / 2 / 3 in game", lo.x + 20, lo.y + 56, 16, col::muted);
        for (int i = 0; i < 3; i++) {
            Rectangle s{lo.x + 20 + i * 112.0f, lo.y + 80, 100, 100};
            bool sel = slot_ == i;
            DrawRectangleRounded(s, 0.2f, 8, sel ? col::panelHi : col::bgDeep);
            DrawRectangleRoundedLinesEx(s, 0.2f, 8, 3, sel ? col::gold : col::outline);
            const std::string& id = P().loadout[i];
            if (!id.empty()) art::itemIcon(id, {s.x + 50, s.y + 48}, 60);
            ui::text(std::to_string(i + 1), s.x + 10, s.y + 14, 18, col::gold, ui::Left, true);
            if (!id.empty()) ui::text("x" + std::to_string(P().count(id)), s.x + s.width - 8, s.y + s.height - 12, 16, col::text, ui::Right);
            if (ui::hovered(s) && E().mousePressed()) { E().consumeClick(); slot_ = i; E().sfx.play(Sfx::Click); }
        }
        int k = 0;
        for (auto& it : itemDb()) {
            if (it.kind != ItemKind::Powerup) continue;
            Rectangle s{lo.x + 20 + k * 87.0f, lo.y + 200, 78, 84};
            int cnt = P().count(it.id);
            bool hot = ui::hovered(s);
            DrawRectangleRounded(s, 0.2f, 8, hot ? col::panelHi : alpha(col::bgDeep, 0.7f));
            art::itemIcon(it.id, {s.x + s.width / 2, s.y + 36}, 46);
            ui::text("x" + std::to_string(cnt), s.x + s.width / 2, s.y + s.height - 12, 15, cnt ? col::text : col::muted, ui::Center);
            if (hot) ui::text(it.name, lo.x + lo.width - 20, lo.y + 28, 18, it.color, ui::Right, true);
            if (hot && E().mousePressed()) {
                E().consumeClick();
                // a powerup can only sit in one slot
                for (auto& l : P().loadout)
                    if (l == it.id) l.clear();
                P().loadout[slot_] = it.id;
                slot_ = (slot_ + 1) % 3;
                P().save();
                E().sfx.play(Sfx::Power, 0.5f);
            }
            k++;
        }

        // ---------------- cosmetics + preview
        Rectangle co{610, 90, 640, 300};
        ui::panel(co);
        ui::text("WARDROBE", co.x + 20, co.y + 28, 24, col::text, ui::Left, true);
        Vector2 pp = previewPos();
        fx_.draw();
        art::pieter(pp, 54, P().hat, std::sin(t_ * 3) * 8);
        int ci = 0;
        bool any = false;
        for (auto& it : itemDb()) {
            if (it.kind != ItemKind::Hat && it.kind != ItemKind::Trail) continue;
            if (!P().owns(it.id)) continue;
            any = true;
            Rectangle s{co.x + 20 + (ci % 4) * 80.0f, co.y + 60 + (ci / 4) * 92.0f, 72, 84};
            bool eq = P().hat == it.id || P().trail == it.id;
            DrawRectangleRounded(s, 0.2f, 8, eq ? col::panelHi : alpha(col::bgDeep, 0.7f));
            DrawRectangleRoundedLinesEx(s, 0.2f, 8, 2, eq ? col::green : col::outline);
            art::itemIcon(it.id, {s.x + s.width / 2, s.y + 36}, 46);
            ui::text(eq ? "ON" : "off", s.x + s.width / 2, s.y + s.height - 12, 15, eq ? col::green : col::muted, ui::Center, true);
            if (ui::hovered(s) && E().mousePressed()) {
                E().consumeClick();
                std::string& slot = it.kind == ItemKind::Hat ? P().hat : P().trail;
                slot = eq ? "" : it.id;
                P().save();
                E().sfx.play(Sfx::Click);
            }
            ci++;
        }
        if (!any) ui::paragraph("No cosmetics yet. Kira's shop has hats and trails!", {co.x + 20, co.y + 70, 300, 80}, 19, col::muted);

        // ---------------- food
        Rectangle fo{30, 410, 560, 280};
        ui::panel(fo);
        ui::text("FOOD STASH", fo.x + 20, fo.y + 28, 24, col::text, ui::Left, true);
        ui::text("feed it to Pieter in his room", fo.x + 20, fo.y + 56, 16, col::muted);
        int fi = 0;
        for (auto& it : itemDb()) {
            if (it.kind != ItemKind::Food) continue;
            Rectangle s{fo.x + 20 + (fi % 3) * 176.0f, fo.y + 80 + (fi / 3) * 96.0f, 166, 86};
            DrawRectangleRounded(s, 0.2f, 8, alpha(col::bgDeep, 0.7f));
            art::itemIcon(it.id, {s.x + 40, s.y + 43}, 54);
            ui::text(it.name.substr(0, 12), s.x + 76, s.y + 30, 15, col::text);
            ui::text("x" + std::to_string(P().count(it.id)), s.x + 76, s.y + 58, 24, P().count(it.id) ? col::gold : col::muted, ui::Left, true);
            fi++;
        }

        // ---------------- stats
        Rectangle st{610, 410, 640, 280};
        ui::panel(st);
        ui::text("STATS", st.x + 20, st.y + 28, 24, col::text, ui::Left, true);
        auto& s = P().stats;
        std::pair<std::string, std::string> rows[] = {
            {"Best Classic wave", std::to_string(s.bestWave)},
            {"Classic wins / runs", std::to_string(s.classicWins) + " / " + std::to_string(s.classicRuns)},
            {"Stroopwafel record", std::to_string(s.stroopBest) + " layers"},
            {"Horse bets won / jockey wins", std::to_string(s.horseWins) + " / " + std::to_string(s.rideWins)},
            {"Pieter level", std::to_string(P().pieter.level)},
            {"Total coins earned", fmtNum(s.totalEarned)},
            {"Total gambled / biggest win", fmtNum(s.totalWagered) + " / " + fmtNum(s.biggestWin)},
        };
        for (int i = 0; i < 7; i++) {
            float y = st.y + 66 + i * 30;
            ui::text(rows[i].first, st.x + 24, y, 18, col::muted);
            ui::text(rows[i].second, st.x + st.width - 24, y, 18, col::text, ui::Right, true);
        }
    }

private:
    int slot_ = 0;
    float t_ = 0, trailT_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeInventory() { return std::make_unique<InventoryScene>(); }

}  // namespace pg
