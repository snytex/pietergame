// Title screen: the Bec IT Room artwork behind a modern, scrolling mode browser.
// Tabs filter the carousel; the crew bar holds the character easter eggs (and
// the secret admin code: Pieter, Pieter, Kira, Pieter, Davi).
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Carousel.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

struct Crew {
    const char* who;
    const char* name;
    const char* line;
    Sfx sound;
    Color color;
};
const Crew kCrew[4] = {{"pieter", "Pieter", "Ik ben Pieter!", Sfx::Power, col::gold},
                       {"davi", "Davi", "PRO since 2010 (claims)", Sfx::Error, col::cyan},
                       {"kira", "Kira", "hihi <3", Sfx::Kiss, col::pink},
                       {"horse", "Horse", "JUHUH!", Sfx::Neigh, col::orange}};

const char* kTabs[4] = {"ALL", "ARCADE", "CASINO", "SPECIAL"};

// remembered between visits to the menu
int g_tab = 0;
int g_focus[4] = {0, 0, 0, 0};

std::vector<Tile> tilesFor(int tab) {
    Tile classic{"classic", "CLASSIC", "Dodge, survive, beat the bosses.", "", rgba(0x6b3fd1ff), makeClassic};
    Tile room{"room", "PIETER ROOM", "Feed, pet and talk to Pieter.", "", rgba(0xc2418aff), makePieterRoom};
    Tile beats{"beats", "PIETER BEATS", "Rhythm game on every song.", "", rgba(0x2a5bd7ff), makeBeats};
    Tile horses{"horses", "HORSES", "JUHUH! Bet on a horse or ride one.", "", rgba(0xa0582aff), makeHorses};
    Tile stroop{"stroop", "STROOPWAFEL", "Stack the waffles sky high.", "", rgba(0xc98a1fff), makeStroopwafel};
    Tile job{"job", "GET A JOB", "Pieter tries every job. 5 seconds each.", "NEW", rgba(0x1f9a8aff), makeJob};
    Tile fiets{"fiets", "FIETSEN!", "Bike along the canal. Don't fall in.", "NEW", rgba(0x2f7ad8ff), makeFiets};
    Tile kart{"kart", "PIETER KART", "Race Davi, Kira and the Horse.", "NEW", rgba(0xd8582aff), makeKart};
    Tile dungeon{"dungeon", "DAVI'S DUNGEON", "Roguelite. Smash useless elektronik.", "NEW", rgba(0x4a3a8aff), makeDungeon};
    Tile casino{"casino", "CASINO", "Seven tables. Pity fund included.", "", rgba(0x1f8a4cff), makeCasino};
    Tile fnap{"fnap", "FIVE NIGHTS", "...at Pieter's. Don't open the closet.", "HALLOWEEN", rgba(0x5a1a2aff), makeFnap};
    switch (tab) {
        case 1: return {classic, kart, dungeon, job, fiets, beats, horses, stroop};
        case 2:
            return {{"roulette", "ROULETTE", "European wheel. All in on 17?", "", rgba(0xb8283aff), makeRoulette},
                    {"blackjack", "BLACKJACK", "Beat Davi the dealer to 21.", "", rgba(0x2a7ab8ff), makeBlackjack},
                    {"crash", "CRASH", "Cash out before the rocket blows.", "NEW", rgba(0x2a9a6aff), makeCrash},
                    {"plinko", "PLINKO", "Drop Pieters, watch them bounce.", "NEW", rgba(0xd84a8aff), makePlinko},
                    {"mines", "MINES", "Find stroopwafels, avoid bombs.", "", rgba(0x6a3ad8ff), makeMines},
                    {"tower", "DRAGON TOWER", "Climb for eggs. Don't wake it.", "", rgba(0x2a8a4aff), makeDragonTower},
                    {"slots", "SLOTS", "Three Pieters pays 500x.", "", rgba(0xc8901aff), makeSlots}};
        case 3: return {room, fnap};
        default: return {classic, kart, dungeon, job, fiets, room, beats, horses, stroop, casino, fnap};
    }
}

class MenuScene : public Scene {
public:
    Presence presence() const override { return {"Main Menu"}; }
    MusicMode music() const override { return MusicMode::Title; }

    void enter() override {
        P().save();
        carousel_.setTiles(tilesFor(g_tab));
        carousel_.setFocus(g_focus[g_tab]);
    }

    void exit() override { g_focus[g_tab] = carousel_.focus(); }

    void setTab(int tab) {
        g_focus[g_tab] = carousel_.focus();
        g_tab = tab;
        carousel_.setTiles(tilesFor(tab));
        carousel_.setFocus(g_focus[tab]);
        tabT_ = 0;
    }

    void update(float dt) override {
        t_ += dt;
        tabT_ += dt;
        Vector2 m = E().mouse();
        parallax_ = damp(parallax_, {(m.x - VW / 2.0f) / VW * -18, (m.y - VH / 2.0f) / VH * -10}, 4, dt);
        spawn_ += dt;
        while (spawn_ > 0.1f) {
            spawn_ -= 0.1f;
            Particle p;
            p.pos = {rnd(0, VW), VH + 10.0f};
            p.vel = {rnd(-15, 15), rnd(-60, -25)};
            p.life = p.maxLife = rnd(5, 9);
            p.size = rnd(1.5f, 3.5f);
            p.endSize = 0.5f;
            p.color = alpha(chance(0.3f) ? col::pink : col::violet, rnd(0.3f, 0.6f));
            fx_.add(p);
        }
        fx_.update(dt);
        if (!confirmQuit_) {
            if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_E)) setTab((g_tab + 1) % 4);
            if (IsKeyPressed(KEY_Q)) setTab((g_tab + 3) % 4);
            for (int i = 0; i < 4; i++)
                if (IsKeyPressed(KEY_ONE + i)) setTab(i);
        }
        if (E().backPressed()) confirmQuit_ = !confirmQuit_;
    }

    Color tabColor() const {
        const Color c[4] = {col::violet, rgba(0x2f7ad8ff), col::green, rgba(0xc2418aff)};
        return c[g_tab];
    }

    // ------------------------------------------------------------ pieces
    void drawBackdrop() {
        // the menu artwork, nudged up so the faces sit above the UI, fading into dark glass
        art::menuBackground(parallax_.x, parallax_.y - 120);
        DrawRectangleGradientV(0, 0, VW, 120, alpha(col::bgDeep, 0.65f), alpha(col::bgDeep, 0));
        DrawRectangleGradientV(0, 200, VW, 160, alpha(col::bgDeep, 0), alpha(col::bgDeep, 0.93f));
        DrawRectangle(0, 360, VW, VH - 360, alpha(col::bgDeep, 0.93f));
        fx::beginAdd();
        fx::glow({VW / 2.0f, 470}, 640, alpha(tabColor(), 0.16f));
        fx::endAdd();
        fx::bokeh(t_, 10, col::pink, 7);
        fx_.draw();
    }

    void drawTitle() {
        float cx = VW / 2.0f, y = 118 + std::sin(t_ * 2) * 3;
        fx::gradientText("PIETERGAME", cx - 38, y, 72, WHITE, rgba(0xc9b2ffff), 5);
        float s = 1 + std::sin(t_ * 4) * 0.06f;
        Vector2 p{cx + 196, y - 4};
        float rot = std::sin(t_ * 1.3f) * 8 - 8;
        ui::textRotated("3", {p.x + 4, p.y + 7}, 108 * s, rot, col::outline, true);
        ui::textRotated("3", p, 108 * s, rot, col::gold, true);
        ui::textShadow("the dutch legend returns", cx, y + 54, 20, col::pink, ui::Center, false, 2);
    }

    void drawCrew() {
        Rectangle bar{18, 14, 236, 60};
        DrawRectangleRounded(bar, 1, 12, alpha(col::bgDeep, 0.7f));
        DrawRectangleRoundedLinesEx(bar, 1, 12, 1.5f, alpha(WHITE, 0.12f));
        Vector2 m = E().mouse();
        for (int i = 0; i < 4; i++) {
            Vector2 c{bar.x + 34 + i * 56.0f, bar.y + bar.height / 2};
            bool over = Vector2Distance(m, c) < 24 && !confirmQuit_;
            float& h = crewHover_[i];
            h = damp(h, over ? 1.0f : 0.0f, 14, GetFrameTime());
            float r = 22 + h * 3;
            if (h > 0.01f) {
                fx::beginAdd();
                fx::glow(c, 60, alpha(kCrew[i].color, 0.4f * h));
                fx::endAdd();
            }
            art::portrait(kCrew[i].who, {c.x, c.y - h * 3}, r, over ? kCrew[i].color : col::outline);
            DrawCircleV({c.x + 15, c.y + 15 - h * 3}, 6, col::outline);
            DrawCircleV({c.x + 15, c.y + 15 - h * 3}, 4.5f, col::green);
            if (over) {
                float w = ui::measure(kCrew[i].name, 15, true) + 20;
                Rectangle tip{c.x - w / 2, bar.y + bar.height + 8, w, 26};
                DrawRectangleRounded(tip, 1, 8, alpha(col::bgDeep, 0.9f));
                ui::text(kCrew[i].name, c.x, tip.y + 13, 15, kCrew[i].color, ui::Center, true);
                if (E().mousePressed()) {
                    E().consumeClick();
                    E().sfx.play(kCrew[i].sound);
                    fx_.floatText(kCrew[i].line, {c.x + 40, c.y + 50}, kCrew[i].color, 24);
                    fx_.burst(c, 14, kCrew[i].color, 220, 6, 0.6f, kCrew[i].sound == Sfx::Kiss ? PShape::Heart : PShape::Star);
                    if (kCrew[i].sound == Sfx::Neigh) E().shake(4);
                    secretClick(kCrew[i].who);
                }
            }
        }
    }

    void drawTabs() {
        float pw = 132, ph = 40, gap = 6, total = 4 * pw + 3 * gap + 12;
        Rectangle bar{VW / 2.0f - total / 2, 262, total, ph + 12};
        DrawRectangleRounded(bar, 1, 12, alpha(BLACK, 0.4f));
        DrawRectangleRoundedLinesEx(bar, 1, 12, 1.5f, alpha(WHITE, 0.1f));
        // the selected pill slides between tabs
        float target = bar.x + 6 + g_tab * (pw + gap);
        tabX_ = tabX_ == 0 ? target : damp(tabX_, target, 16, GetFrameTime());
        Rectangle sel{tabX_, bar.y + 6, pw, ph};
        fx::gradRoundRect(sel, 1, lighten(tabColor(), 0.25f), tabColor());
        DrawLineEx({sel.x + 18, sel.y + 2}, {sel.x + sel.width - 18, sel.y + 2}, 1.5f, alpha(WHITE, 0.5f));
        for (int i = 0; i < 4; i++) {
            Rectangle r{bar.x + 6 + i * (pw + gap), bar.y + 6, pw, ph};
            bool over = ui::hovered(r);
            ui::text(kTabs[i], r.x + r.width / 2, r.y + r.height / 2, 17, g_tab == i ? WHITE : (over ? col::text : col::muted), ui::Center, true);
            if (over && E().mousePressed() && g_tab != i) {
                E().consumeClick();
                E().sfx.play(Sfx::Click);
                setTab(i);
            }
        }
        ui::text("Q / E", bar.x - 16, bar.y + bar.height / 2, 13, alpha(col::muted, 0.6f), ui::Right, true);
    }

    // round icon + label, Rec Room style
    bool dockButton(Rectangle r, const char* label, Color c, int icon) {
        bool hot = ui::hovered(r);
        float& h = dockHover_[icon];
        h = damp(h, hot ? 1.0f : 0.0f, 14, GetFrameTime());
        Rectangle d{r.x, r.y - h * 3, r.width, r.height};
        fx::gradRoundRect(d, 1, alpha(lighten(col::panelHi, 0.1f + h * 0.1f), 0.95f), alpha(col::panel, 0.95f));
        DrawRectangleRoundedLinesEx(d, 1, 12, 1.5f, hot ? alpha(c, 0.9f) : alpha(WHITE, 0.12f));
        Vector2 ic{d.x + 24, d.y + d.height / 2};
        DrawCircleV(ic, 17, darken(c, 0.2f));
        DrawCircleV({ic.x, ic.y - 1.5f}, 16, c);
        Color k = WHITE;
        switch (icon) {
            case 0:  // shopping bag
                DrawRectangleRounded({ic.x - 8, ic.y - 3, 16, 12}, 0.25f, 4, k);
                DrawRing({ic.x, ic.y - 3}, 3.5f, 5.5f, 180, 360, 10, k);
                break;
            case 1:  // backpack
                DrawRectangleRounded({ic.x - 7, ic.y - 7, 14, 16}, 0.4f, 6, k);
                DrawRectangleRec({ic.x - 4, ic.y + 1, 8, 4}, c);
                DrawRing({ic.x, ic.y - 7}, 2.5f, 4.5f, 180, 360, 8, k);
                break;
            case 2:  // gear
                DrawCircleV(ic, 7, k);
                for (int i = 0; i < 8; i++) DrawCircleV(ic + fromAngle(i * PI / 4 + t_ * h, 9), 2.6f, k);
                DrawCircleV(ic, 3, c);
                break;
            default:  // power
                DrawRing(ic, 5.5f, 7.8f, -50, 230, 16, k);
                DrawLineEx({ic.x, ic.y - 9}, {ic.x, ic.y - 1}, 3, k);
                break;
        }
        ui::text(label, d.x + 48, d.y + d.height / 2, 16, col::text, ui::Left, true);
        if (hot && E().mousePressed()) {
            E().consumeClick();
            E().sfx.play(Sfx::Click);
            return true;
        }
        return false;
    }

    void drawDock() {
        const float bw[4] = {112, 152, 144, 100};
        float gap = 8, total = bw[0] + bw[1] + bw[2] + bw[3] + 3 * gap + 16;
        Rectangle bar{VW / 2.0f - total / 2, VH - 70, total, 56};
        DrawRectangleRounded(bar, 1, 12, alpha(BLACK, 0.45f));
        DrawRectangleRoundedLinesEx(bar, 1, 12, 1.5f, alpha(WHITE, 0.08f));
        float x = bar.x + 8;
        const char* labels[4] = {"SHOP", "INVENTORY", "SETTINGS", "QUIT"};
        const Color cs[4] = {col::pink, col::orange, col::violet, rgba(0xd84a5aff)};
        for (int i = 0; i < 4; i++) {
            Rectangle r{x, bar.y + 7, bw[i], 42};
            if (dockButton(r, labels[i], cs[i], i)) {
                if (i == 0) E().go(makeShop);
                if (i == 1) E().go(makeInventory);
                if (i == 2) E().go(makeSettings);
                if (i == 3) confirmQuit_ = true;
            }
            x += bw[i] + gap;
        }
    }

    void drawNowPlaying() {
        std::string np = E().music.nowPlaying();
        if (np.empty()) return;
        if (np.size() > 22) np = np.substr(0, 20) + "..";
        Rectangle r{VW - 282, VH - 70, 266, 56};
        DrawRectangleRounded(r, 0.5f, 12, alpha(col::bgDeep, 0.85f));
        DrawRectangleRoundedLinesEx(r, 0.5f, 12, 1.5f, alpha(WHITE, 0.1f));
        Vector2 vc{r.x + 30, r.y + 28};
        DrawCircleV(vc, 19, rgba(0x0e0b14ff));
        DrawRing(vc, 11, 12, 0, 360, 24, alpha(WHITE, 0.12f));
        DrawCircleV(vc, 7, col::cyan);
        DrawCircleSector(vc, 18, t_ * 120, t_ * 120 + 25, 6, alpha(WHITE, 0.12f));
        ui::text("NOW PLAYING", r.x + 58, r.y + 15, 11, col::cyan, ui::Left, true);
        ui::text(np, r.x + 58, r.y + 32, 15, col::text, ui::Left, true);
        DrawRectangleRounded({r.x + 58, r.y + 44, 150, 4}, 1, 4, alpha(WHITE, 0.15f));
        DrawRectangleRounded({r.x + 58, r.y + 44, 150 * E().music.progress(), 4}, 1, 4, col::cyan);
        Vector2 sk{r.x + r.width - 28, r.y + 28};
        bool over = Vector2Distance(E().mouse(), sk) < 18;
        DrawCircleV(sk, 16, over ? col::cyan : alpha(WHITE, 0.12f));
        Color k = over ? col::bgDeep : col::text;
        DrawTriangle({sk.x - 7, sk.y - 7}, {sk.x - 7, sk.y + 7}, {sk.x + 3, sk.y}, k);
        DrawRectangleRec({sk.x + 3, sk.y - 7, 3, 14}, k);
        if (over && E().mousePressed()) {
            E().consumeClick();
            E().music.next();
        }
    }

    void draw() override {
        drawBackdrop();
        drawTitle();
        drawCrew();
        ui::coinCounter(VW - 20, 42, P().coins);
        if (confirmQuit_) { drawQuitConfirm(); return; }
        drawTabs();
        float k = easeOutCubic(clamp01(tabT_ / 0.35f));
        Rectangle area{0, 340 + (1 - k) * 30, (float)VW, 250};
        if (const Tile* tl = carousel_.run(area, t_, true)) E().go(tl->go);
        drawDock();
        drawNowPlaying();
        ui::text("v3.1  -  F11 fullscreen  -  F9 next song", 18, VH - 22, 13, alpha(col::text, 0.4f));
    }

private:
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

    void drawQuitConfirm() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.5f));
        Rectangle r{VW / 2.0f - 220, VH / 2.0f - 90, 440, 200};
        ui::panel(r);
        ui::text("Leave Pieter alone?", r.x + r.width / 2, r.y + 50, 32, col::text, ui::Center, true);
        ui::text("He will miss you. Probably.", r.x + r.width / 2, r.y + 88, 20, col::muted, ui::Center);
        if (ui::button({r.x + 30, r.y + 125, 180, 52}, "STAY", col::purple)) confirmQuit_ = false;
        if (ui::button({r.x + 230, r.y + 125, 180, 52}, "QUIT", col::red)) E().quit();
    }

    float t_ = 0, spawn_ = 0, lastSecret_ = -10, tabT_ = 1, tabX_ = 0;
    float crewHover_[4]{}, dockHover_[4]{};
    std::vector<std::string> secret_;
    Vector2 parallax_{};
    bool confirmQuit_ = false;
    Carousel carousel_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeMenu() { return std::make_unique<MenuScene>(); }

}  // namespace pg
