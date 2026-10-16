// Title screen: the Bec IT Room artwork, mode select, and character easter eggs.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

struct Hotspot {
    const char* who;
    Vector2 pos;
    float radius;
    const char* line;
    Sfx sound;
    Color color;
};

class MenuScene : public Scene {
public:
    Presence presence() const override { return {"Main Menu"}; }
    MusicMode music() const override { return MusicMode::Title; }

    void enter() override { P().save(); }

    void update(float dt) override {
        t_ += dt;
        Vector2 m = E().mouse();
        parallax_ = damp(parallax_, {(m.x - VW / 2.0f) / VW * -18, (m.y - VH / 2.0f) / VH * -10}, 4, dt);

        // purple dust floating up
        spawn_ += dt;
        while (spawn_ > 0.08f) {
            spawn_ -= 0.08f;
            Particle p;
            p.pos = {rnd(0, VW), VH + 10.0f};
            p.vel = {rnd(-15, 15), rnd(-60, -25)};
            p.life = p.maxLife = rnd(5, 9);
            p.size = rnd(1.5f, 3.5f);
            p.endSize = 0.5f;
            p.color = alpha(chance(0.3f) ? col::pink : col::violet, rnd(0.3f, 0.7f));
            fx_.add(p);
        }
        fx_.update(dt);

        if (E().mousePressed()) {
            for (auto& h : hotspots_) {
                Vector2 hp = h.pos + parallax_;
                if (Vector2Distance(m, hp) < h.radius) {
                    E().sfx.play(h.sound);
                    fx_.floatText(h.line, {hp.x, hp.y - h.radius * 0.6f}, h.color, 30);
                    fx_.burst(hp, 16, h.color, 260, 7, 0.7f, h.sound == Sfx::Kiss ? PShape::Heart : PShape::Star);
                    if (h.sound == Sfx::Neigh) E().shake(5);
                    secretClick(h.who);
                }
            }
        }
        if (E().backPressed()) confirmQuit_ = !confirmQuit_;
    }

    void draw() override {
        art::menuBackground(parallax_.x, parallax_.y);
        // soft spotlight behind the menu column
        DrawCircleGradient({VW / 2.0f, VH / 2.0f + 20}, 460, alpha(col::bgDeep, 0.6f), alpha(col::bgDeep, 0));
        fx::beginAdd();
        fx::glow({VW / 2.0f, 95}, 420, alpha(col::violet, 0.35f));
        fx::glow({VW / 2.0f + 230, 90}, 160, alpha(col::gold, 0.35f + 0.1f * std::sin(t_ * 4)));
        fx::endAdd();
        fx::bokeh(t_, 12, col::pink, 7);
        fx_.draw();

        drawTitle();
        if (confirmQuit_) { drawQuitConfirm(); return; }

        const float cx = VW / 2.0f, bw = 236, bh = 96, gap = 18;
        float x0 = cx - bw - gap / 2, x1 = cx + gap / 2, y0 = 222;
        if (ui::bigButton({x0, y0, bw, bh}, "CLASSIC", "dodge. survive. boss.", col::purple)) E().go(makeClassic);
        if (ui::bigButton({x1, y0, bw, bh}, "PIETER ROOM", "feed & talk to Pieter", rgba(0xc2418aff), 30)) E().go(makePieterRoom);
        if (ui::bigButton({x0, y0 + bh + gap, bw, bh}, "HORSES", "JUHUH! bet or ride", rgba(0xa0582aff))) E().go(makeHorses);
        if (ui::bigButton({x1, y0 + bh + gap, bw, bh}, "STROOPWAFEL", "stack the waffles", rgba(0xc98a1fff), 29)) E().go(makeStroopwafel);

        float y2 = y0 + 2 * (bh + gap) + 4;
        if (ui::bigButton({x0, y2, bw, 70}, "PIETER BEATS", "rhythm game, every song", rgba(0x2a5bd7ff), 27)) E().go(makeBeats);
        if (ui::bigButton({x1, y2, bw, 70}, "CASINO", "roulette, blackjack & more", rgba(0x1f8a4cff), 30)) E().go(makeCasino);

        float y3 = y2 + 70 + gap + 4, sw = (bw * 2 + gap - 2 * 12) / 3;
        if (ui::button({x0, y3, sw, 54}, "SHOP", col::panelHi, 24)) E().go(makeShop);
        if (ui::button({x0 + sw + 12, y3, sw, 54}, "INVENTORY", col::panelHi, 24)) E().go(makeInventory);
        if (ui::button({x0 + 2 * (sw + 12), y3, sw, 54}, "SETTINGS", col::panelHi, 24)) E().go(makeSettings);
        if (ui::button({cx - 70, y3 + 66, 140, 40}, "QUIT", rgba(0x5a2035ff), 20)) confirmQuit_ = true;

        ui::coinCounter(VW - 20, 38, P().coins);
        drawNowPlaying();
        ui::text("v3.0  -  F11 fullscreen  -  F9 next song", 16, VH - 18, 15, alpha(col::text, 0.5f));
    }

private:
    // Secret: Pieter, Pieter, Kira, Pieter, Davi opens the admin panel.
    void secretClick(const char* who) {
        if (t_ - lastSecret_ > 4) secret_.clear();
        lastSecret_ = t_;
        secret_.push_back(who);
        if (secret_.size() > 5) secret_.erase(secret_.begin());
        static const std::vector<std::string> code = {"pieter", "pieter", "kira", "pieter", "davi"};
        if (secret_ == code) {
            secret_.clear();
            E().sfx.play(Sfx::Win);
            E().flash(col::green, 0.4f);
            E().toast("ADMIN PANEL UNLOCKED", col::green);
            E().go(makeAdmin);
        }
    }

    void drawTitle() {
        float cx = VW / 2.0f, y = 92 + std::sin(t_ * 2) * 4;
        float rot = std::sin(t_ * 1.3f) * 2;
        fx::gradientText("PIETERGAME", cx - 46, y, 84, WHITE, rgba(0xc9b2ffff), 6);
        float s = 1 + std::sin(t_ * 4) * 0.06f;
        Vector2 p{cx + 230, y - 6};
        ui::textRotated("3", {p.x + 5, p.y + 8}, 130 * s, rot * 4 - 8, col::outline, true);
        ui::textRotated("3", p, 130 * s, rot * 4 - 8, col::gold, true);
        ui::textShadow("the dutch legend returns", cx, y + 62, 22, col::pink, ui::Center, false, 2);
    }

    void drawNowPlaying() {
        std::string np = E().music.nowPlaying();
        if (np.empty()) return;
        float w = std::max(260.0f, ui::measure(np, 18) + 60);
        Rectangle r{VW - w - 16, VH - 54, w, 40};
        DrawRectangleRounded(r, 0.5f, 10, alpha(col::bgDeep, 0.8f));
        // little equalizer
        for (int i = 0; i < 4; i++) {
            float h = 6 + std::abs(std::sin(t_ * (5 + i * 1.7f) + i)) * 14;
            DrawRectangleRec({r.x + 14 + i * 6.0f, r.y + 28 - h, 4, h}, col::cyan);
        }
        ui::text(np, r.x + 48, r.y + 16, 18, col::text);
        DrawRectangleRec({r.x + 48, r.y + 30, (r.width - 64), 3}, alpha(col::muted, 0.3f));
        DrawRectangleRec({r.x + 48, r.y + 30, (r.width - 64) * E().music.progress(), 3}, col::cyan);
    }

    void drawQuitConfirm() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.5f));
        Rectangle r{VW / 2.0f - 220, VH / 2.0f - 90, 440, 200};
        ui::panel(r);
        ui::text("Leave Pieter alone?", r.x + r.width / 2, r.y + 50, 32, col::text, ui::Center, true);
        ui::text("He will miss you. Probably.", r.x + r.width / 2, r.y + 88, 20, col::muted, ui::Center);
        if (ui::button({r.x + 30, r.y + 125, 180, 52}, "STAY", col::purple)) confirmQuit_ = false;
        if (ui::button({r.x + 230, r.y + 125, 180, 52}, "QUIT", col::red)) E().quit();
    }

    float t_ = 0, spawn_ = 0, lastSecret_ = -10;
    std::vector<std::string> secret_;
    Vector2 parallax_{};
    bool confirmQuit_ = false;
    Particles fx_;
    std::vector<Hotspot> hotspots_ = {
        {"pieter", {172, 287}, 75, "Ik ben Pieter!", Sfx::Power, col::gold},
        {"horse", {1001, 125}, 95, "JUHUH!", Sfx::Neigh, col::orange},
        {"davi", {980, 380}, 70, "PRO since 2010 (claims)", Sfx::Error, col::cyan},
        {"kira", {1192, 423}, 70, "hihi <3", Sfx::Kiss, col::pink},
    };
};

}  // namespace

std::unique_ptr<Scene> makeMenu() { return std::make_unique<MenuScene>(); }

}  // namespace pg
