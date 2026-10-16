// Secret admin panel (title screen code: Pieter, Pieter, Kira, Pieter, Davi).
// Classic practice setup on the left, cheats on the right.
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Admin.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

class AdminScene : public Scene {
public:
    MusicMode music() const override { return MusicMode::Title; }
    Presence presence() const override { return {"In the menus"}; }  // it's a secret

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    void done(const std::string& msg, Vector2 at) {
        E().sfx.play(Sfx::Power, 0.7f);
        E().toast(msg, col::green);
        fx_.burst(at, 14, col::green, 260, 5, 0.5f, PShape::Star);
        P().save();
    }

    // -------------------------------------------------------------- practice
    void drawPractice() {
        auto& a = Admin();
        Rectangle p{30, 92, 660, 610};
        ui::panel(p, rgba(0x120c22f2), 0.06f, col::green);
        ui::text("CLASSIC PRACTICE", p.x + 24, p.y + 30, 26, col::green, ui::Left, true);
        ui::text("start anywhere, practice the hard parts", p.x + p.width - 24, p.y + 30, 15, col::muted, ui::Right);

        ui::text("START AT WAVE", p.x + 24, p.y + 72, 16, col::muted, ui::Left, true);
        for (int w = 1; w <= 15; w++) {
            int i = w - 1;
            Rectangle b{p.x + 24 + (i % 5) * 124.0f, p.y + 88 + (i / 5) * 66.0f, 114, 56};
            bool sel = a.startWave == w;
            const char* face = w == 5 ? "davi" : w == 8 ? "kira" : w == 10 ? "horse" : (w > 10 && w % 5 == 0) ? "pieter" : nullptr;
            bool boss = face != nullptr;
            Color c = sel ? col::green : (boss ? rgba(0x5a2035ff) : col::panelHi);
            if (ui::button(b, "", c, 20)) a.startWave = w;
            if (face) art::portrait(face, {b.x + 28, b.y + b.height / 2 - 2}, 18, WHITE);
            ui::textShadow(std::to_string(w), b.x + (face ? 76 : b.width / 2), b.y + b.height / 2 - 2, 26, WHITE, ui::Center, true, 2);
        }
        const char* what = a.startWave == 5 ? "DAVI (miniboss)" : a.startWave == 8 ? "KIRA (miniboss)" : a.startWave == 10 ? "THE HORSE (final boss)"
                         : a.startWave > 10 ? (a.startWave % 5 == 0 ? "endless boss wave" : "endless wave") : "regular wave";
        ui::text(TextFormat("wave %d: %s", a.startWave, what), p.x + 24, p.y + 300, 18, col::text, ui::Left, true);

        // hearts
        float y = p.y + 326;
        ui::text("HEARTS", p.x + 24, y + 20, 16, col::muted, ui::Left, true);
        if (ui::button({p.x + 140, y, 44, 40}, "-", col::panelHi, 22, a.hearts > 1)) a.hearts--;
        for (int i = 0; i < a.hearts; i++) ui::heart({p.x + 210 + i * 30.0f, y + 20}, 24, col::red);
        if (ui::button({p.x + 210 + 9 * 30.0f, y, 44, 40}, "+", col::panelHi, 22, a.hearts < 9)) a.hearts++;

        // boss phase
        y += 52;
        ui::text("BOSS PHASE", p.x + 24, y + 20, 16, col::muted, ui::Left, true);
        if (ui::button({p.x + 140, y, 170, 40}, "PHASE 1", !a.enragedBoss ? col::green : col::panelHi, 18)) a.enragedBoss = false;
        if (ui::button({p.x + 320, y, 210, 40}, "ENRAGED (50% HP)", a.enragedBoss ? col::red : col::panelHi, 16)) a.enragedBoss = true;

        // speed
        y += 52;
        ui::text("SPEED", p.x + 24, y + 20, 16, col::muted, ui::Left, true);
        const float speeds[] = {0.5f, 0.75f, 1.0f};
        for (int i = 0; i < 3; i++)
            if (ui::button({p.x + 140 + i * 110.0f, y, 100, 40}, TextFormat("%.2gx", speeds[i]), a.speed == speeds[i] ? col::green : col::panelHi, 18))
                a.speed = speeds[i];

        // toggles (apply to every Classic run this session)
        y += 50;
        ui::toggle({p.x + 24, y, 290, 44}, "God mode", a.god);
        ui::toggle({p.x + 340, y, 290, 44}, "Show hitboxes", a.hitboxes);

        if (ui::bigButton({p.x + 24, p.y + p.height - 86, p.width - 48, 70}, TextFormat("PLAY WAVE %d", a.startWave), "", col::green, 34)) {
            a.practicePending = true;
            E().go(makeClassic);
        }
    }

    // -------------------------------------------------------------- cheats
    void drawCheats() {
        auto& pr = P();
        Rectangle p{710, 92, 540, 610};
        ui::panel(p, rgba(0x120c22f2), 0.06f, col::green);
        ui::text("CHEATS", p.x + 24, p.y + 30, 26, col::green, ui::Left, true);

        float y = p.y + 66;
        ui::coin({p.x + 40, y + 16}, 15);
        ui::text(fmtNum(pr.coins) + " Pietercoins", p.x + 64, y + 16, 22, col::gold, ui::Left, true);
        y += 40;
        const long long amounts[] = {100, 1000, 10000, 100000};
        const char* labels[] = {"+100", "+1K", "+10K", "+100K"};
        for (int i = 0; i < 4; i++) {
            Rectangle b{p.x + 24 + i * 124.0f, y, 114, 44};
            if (ui::button(b, labels[i], col::goldDark, 20)) {
                pr.coins += amounts[i];
                done(std::string(labels[i]) + " Pietercoins", center(b));
            }
        }
        y += 54;
        if (ui::button({p.x + 24, y, 238, 40}, "SET TO 0", rgba(0x5a2035ff), 16)) {
            pr.coins = 0;
            done("Broke. Like Davi.", {p.x + 140, y + 20});
        }
        if (ui::button({p.x + 270, y, 246, 40}, "PITY FUND READY", col::panelHi, 16)) {
            pr.lastPity = 0;
            done("Pity fund cooldown reset", {p.x + 390, y + 20});
        }

        y += 62;
        ui::text("ITEMS", p.x + 24, y, 16, col::muted, ui::Left, true);
        y += 16;
        auto giveKind = [&](ItemKind k, int n) {
            for (auto& it : itemDb())
                if (it.kind == k) pr.give(it.id, n);
        };
        if (ui::button({p.x + 24, y, 238, 44}, "+10 ALL POWERUPS", col::purple, 16)) {
            giveKind(ItemKind::Powerup, 10);
            done("+10 of every powerup", {p.x + 140, y + 22});
        }
        if (ui::button({p.x + 270, y, 246, 44}, "+10 ALL FOOD", col::orange, 16)) {
            giveKind(ItemKind::Food, 10);
            done("+10 of every food", {p.x + 390, y + 22});
        }
        y += 52;
        if (ui::button({p.x + 24, y, 492, 44}, "UNLOCK ALL HATS + TRAILS", col::pink, 16)) {
            for (auto& it : itemDb())
                if ((it.kind == ItemKind::Hat || it.kind == ItemKind::Trail) && !pr.owns(it.id)) pr.give(it.id);
            done("Wardrobe unlocked", {p.x + 270, y + 22});
        }

        y += 66;
        ui::text("PIETER ROOM", p.x + 24, y, 16, col::muted, ui::Left, true);
        y += 16;
        if (ui::button({p.x + 24, y, 238, 44}, "MAX ALL NEEDS", col::cyan, 16)) {
            auto& n = pr.pieter;
            n.hunger = n.fun = n.energy = n.hygiene = 100;
            n.sleeping = false;
            done("Pieter feels amazing", {p.x + 140, y + 22});
        }
        if (ui::button({p.x + 270, y, 246, 44}, "+1 PIETER LEVEL", col::cyan, 16)) {
            pr.pieter.level++;
            pr.pieter.xp = 0;
            done("Pieter is now level " + std::to_string(pr.pieter.level), {p.x + 390, y + 22});
        }

        y += 66;
        ui::text("INTRO", p.x + 24, y, 16, col::muted, ui::Left, true);
        y += 16;
        if (ui::button({p.x + 24, y, 238, 44}, "PLAY INTRO", col::panelHi, 16)) E().go(makeIntroVideo);
        if (ui::button({p.x + 270, y, 246, 44}, "SHOW ON NEXT LAUNCH", col::panelHi, 15)) {
            pr.introSeen = false;
            done("Intro plays on next launch", {p.x + 390, y + 22});
        }
    }

    void draw() override {
        DrawRectangle(0, 0, VW, VH, rgba(0x07040fff));
        // matrix-ish rain, because every admin panel needs one
        for (int i = 0; i < 64; i++) {
            float x = i * 20.0f + 6;
            float y = std::fmod(t_ * (60 + (i * 37) % 90) + i * 53, (float)VH + 200) - 100;
            for (int k = 0; k < 8; k++)
                ui::text(std::string(1, (char)('0' + (i * 7 + k * 3 + (int)(t_ * 4)) % 2)), x, y - k * 18, 14,
                         alpha(col::green, 0.12f * (1 - k / 8.0f)));
        }
        ui::titleBar("PIETER DEV PANEL", col::green);
        if (ui::backButton()) E().go(makeMenu);
        ui::text("shh... don't tell Davi", VW / 2.0f, 84, 15, alpha(col::green, 0.7f), ui::Center);
        drawPractice();
        drawCheats();
        fx_.draw();
    }

private:
    float t_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeAdmin() { return std::make_unique<AdminScene>(); }

}  // namespace pg
