// GET A JOB SIMULATOR: "Pieter get a job" - so he tries every job, five seconds
// each. A WarioWare-style run of microgames that speeds up; 4 coffee-cup lives.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/ModeArt.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace pg {
namespace {

// ====================================================================== microgames

class Micro {
public:
    virtual ~Micro() = default;
    virtual const char* verb() const = 0;  // the one-word instruction
    virtual const char* job() const = 0;   // the job title
    virtual Color color() const = 0;
    virtual bool winOnTimeout() const { return false; }  // "survive" games
    virtual void start(int level) { (void)level; }
    virtual void update(float dt) = 0;
    virtual void draw(float t) = 0;
    bool won = false, lost = false;
    bool decided() const { return won || lost; }
    void win() { if (!decided()) { won = true; E().sfx.play(Sfx::Perfect, 0.7f); } }
    void lose() { if (!decided()) { lost = true; E().sfx.play(Sfx::Error, 0.7f); } }
};

void stageBg(Color top, Color bottom) { DrawRectangleGradientV(0, 0, VW, VH, top, bottom); }

void desk(float y, Color c) {
    DrawRectangleGradientV(0, (int)y, VW, VH - (int)y, c, darken(c, 0.35f));
    DrawRectangle(0, (int)y, VW, 6, lighten(c, 0.25f));
}

void bigPrompt(const std::string& s, float y, float size, Color c) { ui::textOutline(s, VW / 2.0f, y, size, c, col::outline, 4); }

// ---- 1. type your CV
class TypeCV : public Micro {
public:
    const char* verb() const override { return "TYPE!"; }
    const char* job() const override { return "Secretary"; }
    Color color() const override { return rgba(0x2a7ab8ff); }
    void start(int level) override {
        static const char* easy[] = {"BOBA", "JUHUH", "PIETER", "DAVI", "KIRA", "WAFEL"};
        static const char* hard[] = {"STROOPWAFEL", "FRIKANDEL", "HAGELSLAG", "GAMEFUEL", "KROKET"};
        word_ = level < 6 ? easy[rndi(0, 5)] : hard[rndi(0, 4)];
    }
    void update(float) override {
        int c;
        while ((c = GetCharPressed()) != 0) {
            if (decided()) continue;
            char ch = (char)std::toupper(c);
            if (ch == word_[typed_]) {
                typed_++;
                E().sfx.play(Sfx::Key, 0.6f);
                if (typed_ == (int)word_.size()) win();
            } else if (ch != ' ') {
                shake_ = 1;
                E().sfx.play(Sfx::Key, 0.6f, 0.6f);
            }
        }
        shake_ = std::max(0.0f, shake_ - GetFrameTime() * 4);
    }
    void draw(float t) override {
        stageBg(rgba(0x1a3a5aff), rgba(0x0c1a2aff));
        desk(560, rgba(0x6a4a32ff));
        // monitor with a CV document
        Rectangle mon{290, 90, 700, 420};
        DrawRectangleRounded(shrink(mon, -14), 0.05f, 8, rgba(0x15121cff));
        DrawRectangleRec(mon, rgba(0xf4f6fbff));
        DrawRectangleRec({mon.x, mon.y, mon.width, 34}, rgba(0x2a5aa8ff));
        ui::text("cv_pieter_FINAL_v7.docx", mon.x + 16, mon.y + 17, 15, WHITE, ui::Left, true);
        art::portrait("pieter", {mon.x + 80, mon.y + 110}, 50, col::outline);
        ui::text("CURRICULUM VITAE", mon.x + 160, mon.y + 80, 26, rgba(0x1a1a24ff), ui::Left, true);
        ui::text("Skills:", mon.x + 160, mon.y + 120, 18, rgba(0x5a5a6aff), ui::Left, true);
        for (int i = 0; i < 4; i++) DrawRectangleRounded({mon.x + 40, mon.y + 190 + i * 22.0f, 400 - i * 60.0f, 10}, 1, 4, rgba(0xd8dce8ff));
        // the word to type
        float sx = shake_ * std::sin(t * 80) * 10;
        float fs = 64, w = ui::measure(word_, fs, true);
        float x = VW / 2.0f - w / 2 + sx;
        for (size_t i = 0; i < word_.size(); i++) {
            std::string ch(1, word_[i]);
            Color c = (int)i < typed_ ? rgba(0x1f9a5aff) : rgba(0xb8bccaff);
            ui::text(ch, x, mon.y + 330, fs, c, ui::Left, true);
            x += ui::measure(ch, fs, true) + fs * 0.02f;
        }
        if (std::fmod(t, 0.8f) < 0.4f) DrawRectangle((int)(VW / 2.0f - w / 2 + sx + ui::measure(word_.substr(0, typed_), fs, true)), (int)(mon.y + 300), 4, 60, rgba(0x2a5aa8ff));
        // keyboard
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 12 - r; k++) {
                Rectangle key{380 + r * 22 + k * 44.0f, 590 + r * 34.0f, 40, 30};
                char want = typed_ < (int)word_.size() ? word_[typed_] : 0;
                static const char* rows[] = {"QWERTYUIOP[]", "ASDFGHJKL;'", "ZXCVBNM,./"};
                bool hint = std::strlen(rows[r]) > (size_t)k && rows[r][k] == want;
                DrawRectangleRounded({key.x, key.y + 3, key.width, key.height}, 0.25f, 4, rgba(0x15121cff));
                DrawRectangleRounded(key, 0.25f, 4, hint ? col::gold : rgba(0x3a3448ff));
                if (std::strlen(rows[r]) > (size_t)k) ui::text(std::string(1, rows[r][k]), key.x + 20, key.y + 15, 14, hint ? col::outline : col::muted, ui::Center, true);
            }
    }

private:
    std::string word_;
    int typed_ = 0;
    float shake_ = 0;
};

// ---- 2. handshake timing
class Handshake : public Micro {
public:
    const char* verb() const override { return "SHAKE!"; }
    const char* job() const override { return "Intern"; }
    Color color() const override { return rgba(0x8a5ad8ff); }
    void start(int level) override { speed_ = 2.2f + level * 0.15f; }
    void update(float dt) override {
        if (!decided()) ph_ += dt * speed_;
        x_ = 640 + std::sin(ph_) * 330;
        if (!decided() && (E().mousePressed() || IsKeyPressed(KEY_SPACE))) {
            if (std::abs(x_ - 760) < 40) win();
            else lose();
            stop_ = x_;
        }
    }
    void draw(float t) override {
        stageBg(rgba(0x5a3a8aff), rgba(0x2a1a4aff));
        desk(520, rgba(0x8a6a4aff));
        art::portrait("davi", {960, 230}, 110, col::outline, std::sin(t * 2) * 3);
        ui::text("the boss", 960, 360, 18, col::muted, ui::Center, true);
        art::portrait("pieter", {300, 230}, 110, col::outline);
        // target zone
        float zx = 760;
        fx::beginAdd();
        fx::glow({zx, 450}, 90, alpha(col::green, 0.35f + 0.15f * std::sin(t * 8)));
        fx::endAdd();
        DrawRectangleRounded({zx - 40, 410, 80, 80}, 0.3f, 8, alpha(col::green, 0.25f));
        DrawRectangleRoundedLinesEx({zx - 40, 410, 80, 80}, 0.3f, 8, 3, col::green);
        // boss hand from the right, Pieter's hand swinging
        DrawLineEx({1280, 450}, {zx + 40, 450}, 40, rgba(0x3a3a4aff));
        DrawCircleV({zx + 30, 450}, 26, rgba(0xe8b48aff));
        float hx = decided() ? stop_ : x_;
        DrawLineEx({0, 450}, {hx - 30, 450}, 40, rgba(0x1f1c26ff));
        DrawCircleV({hx - 20, 450}, 28, col::outline);
        DrawCircleV({hx - 20, 450}, 25, rgba(0xeb975fff));
        ui::text("click when the hands meet", VW / 2.0f, 640, 22, alpha(WHITE, 0.8f), ui::Center, true);
    }

private:
    float ph_ = 0, speed_ = 2.5f, x_ = 0, stop_ = 0;
};

// ---- 3. stay awake in the meeting (first person: your eyelids close)
class StayAwake : public Micro {
public:
    const char* verb() const override { return "STAY AWAKE!"; }
    const char* job() const override { return "Meeting"; }
    Color color() const override { return rgba(0x3a5a8aff); }
    bool winOnTimeout() const override { return true; }
    void start(int level) override { drain_ = 0.55f + level * 0.04f; }
    void update(float dt) override {
        if (decided()) return;
        awake_ -= drain_ * dt;
        if (IsKeyPressed(KEY_SPACE) || E().mousePressed()) {
            awake_ = std::min(1.0f, awake_ + 0.12f);
            E().sfx.play(Sfx::Tick, 0.4f, 1.4f);
        }
        if (awake_ <= 0) lose();
    }
    void draw(float t) override {
        stageBg(rgba(0x2a3a5aff), rgba(0x141c2eff));
        // presentation screen
        Rectangle scr{260, 70, 760, 380};
        DrawRectangleRec(shrink(scr, -10), rgba(0x0c0c14ff));
        DrawRectangleRec(scr, rgba(0xf4f6fbff));
        ui::text("Q3 SYNERGY ALIGNMENT", scr.x + scr.width / 2, scr.y + 50, 34, rgba(0x1a1a24ff), ui::Center, true);
        for (int i = 0; i < 5; i++) {
            float h = 40 + i * 40 + std::sin(t + i) * 8;
            DrawRectangleRec({scr.x + 120 + i * 110.0f, scr.y + scr.height - 40 - h, 70, h}, i == 4 ? col::green : rgba(0x6a8ad8ff));
        }
        art::portrait("davi", {1120, 480}, 80, col::outline, std::sin(t * 6) * 8);
        ui::text("blah blah blah", 1120, 580, 18, col::muted, ui::Center, true);
        DrawRectangleGradientV(0, 600, VW, 120, rgba(0x6a4a32ff), rgba(0x3a2410ff));
        // eyelids
        float lid = (1 - awake_) * (VH / 2.0f) * 1.02f + std::sin(t * 3) * 6 * (1 - awake_);
        DrawRectangle(0, 0, VW, (int)lid, BLACK);
        DrawRectangle(0, VH - (int)lid, VW, (int)lid + 2, BLACK);
        DrawRectangleGradientV(0, (int)lid, VW, 40, alpha(BLACK, 0.8f), alpha(BLACK, 0));
        DrawRectangleGradientV(0, VH - (int)lid - 40, VW, 40, alpha(BLACK, 0), alpha(BLACK, 0.8f));
        ui::bar({440, 676, 400, 18}, awake_, awake_ < 0.3f ? col::red : col::cyan);
        ui::text("MASH SPACE / CLICK", VW / 2.0f, 660, 16, WHITE, ui::Center, true);
    }

private:
    float awake_ = 0.75f, drain_ = 0.6f;
};

// ---- 4. pour coffee to the line
class Coffee : public Micro {
public:
    const char* verb() const override { return "POUR!"; }
    const char* job() const override { return "Barista"; }
    Color color() const override { return rgba(0x8a5a32ff); }
    void start(int level) override {
        lo_ = rnd(0.55f, 0.72f);
        hi_ = lo_ + std::max(0.08f, 0.16f - level * 0.008f);
        rate_ = 0.42f + level * 0.03f;
    }
    void update(float dt) override {
        if (decided()) return;
        bool hold = E().mouseDown() || IsKeyDown(KEY_SPACE);
        if (hold) {
            fill_ += rate_ * dt;
            started_ = true;
        } else if (started_) {
            if (fill_ >= lo_ && fill_ <= hi_) win();
            else lose();
        }
        if (fill_ > 1) lose();
    }
    void draw(float t) override {
        stageBg(rgba(0x6a4428ff), rgba(0x2a1608ff));
        // espresso machine
        DrawRectangleRounded({460, 60, 360, 220}, 0.12f, 8, rgba(0xc8c8d8ff));
        DrawRectangleRounded({480, 80, 320, 60}, 0.2f, 8, rgba(0x2a2534ff));
        ui::text("PIETER ESPRESSO", 640, 110, 22, col::gold, ui::Center, true);
        DrawRectangleRec({610, 280, 60, 40}, rgba(0x6a6a7aff));
        bool hold = (E().mouseDown() || IsKeyDown(KEY_SPACE)) && !decided();
        if (hold) DrawRectangleRec({634, 320, 12, 140 + std::sin(t * 40) * 4}, rgba(0x4a2410ff));
        desk(600, rgba(0x3a2418ff));
        // the cup with fill and target band
        Rectangle cup{560, 380, 160, 220};
        DrawRing({cup.x + cup.width + 10, cup.y + 90}, 26, 40, -90, 90, 20, col::outline);
        DrawRing({cup.x + cup.width + 10, cup.y + 90}, 30, 36, -90, 90, 20, WHITE);
        DrawRectangleRounded(shrink(cup, -5), 0.15f, 8, col::outline);
        DrawRectangleRounded(cup, 0.15f, 8, WHITE);
        float fh = (cup.height - 20) * std::min(1.0f, fill_);
        DrawRectangleRec({cup.x + 10, cup.y + cup.height - 10 - fh, cup.width - 20, fh}, rgba(0x5a2c10ff));
        DrawRectangleRec({cup.x + 10, cup.y + cup.height - 10 - fh, cup.width - 20, std::min(8.0f, fh)}, rgba(0xc8905aff));
        float ylo = cup.y + cup.height - 10 - (cup.height - 20) * lo_, yhi = cup.y + cup.height - 10 - (cup.height - 20) * hi_;
        DrawRectangleRec({cup.x - 30, yhi, cup.width + 60, ylo - yhi}, alpha(col::green, 0.25f));
        DrawLineEx({cup.x - 30, ylo}, {cup.x + cup.width + 30, ylo}, 3, col::green);
        DrawLineEx({cup.x - 30, yhi}, {cup.x + cup.width + 30, yhi}, 3, col::green);
        ui::text("HOLD, release in the green", VW / 2.0f, 680, 22, WHITE, ui::Center, true);
        art::portrait("kira", {1080, 400}, 80, col::outline);
        ui::text("\"one boba latte pls\"", 1080, 500, 18, col::pink, ui::Center, true);
    }

private:
    float fill_ = 0, lo_ = 0.6f, hi_ = 0.75f, rate_ = 0.45f;
    bool started_ = false;
};

// ---- 5. catch the paycheck, dodge the bills
class Payday : public Micro {
public:
    const char* verb() const override { return "CATCH!"; }
    const char* job() const override { return "Payday"; }
    Color color() const override { return rgba(0x2a8a4aff); }
    void start(int level) override {
        for (int i = 0; i < 2 + level / 3; i++) items_.push_back({{rnd(100, 1180), rnd(-500, -40)}, false});
        items_.push_back({{rnd(200, 1080), -60}, true});
        speed_ = 260 + level * 15;
    }
    void update(float dt) override {
        float target = E().mouse().x;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) kx_ -= 700 * dt, useKeys_ = true;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) kx_ += 700 * dt, useKeys_ = true;
        if (!useKeys_) kx_ = damp(kx_, target, 14, dt);
        kx_ = std::clamp(kx_, 60.0f, 1220.0f);
        for (auto& it : items_) {
            it.p.y += speed_ * dt;
            it.p.x += std::sin(it.p.y * 0.02f) * 40 * dt;
            if (!decided() && it.p.y > 560 && it.p.y < 620 && std::abs(it.p.x - kx_) < 70) {
                if (it.good) win();
                else lose();
                it.p.y = 2000;
            }
        }
    }
    void draw(float t) override {
        stageBg(rgba(0x2a6a4aff), rgba(0x0c2a1aff));
        for (auto& it : items_) {
            rlPushMatrix();
            rlTranslatef(it.p.x, it.p.y, 0);
            rlRotatef(std::sin(t * 4 + it.p.x) * 15, 0, 0, 1);
            if (it.good) {
                DrawRectangleRounded({-60, -30, 120, 60}, 0.1f, 6, rgba(0xd8f0d0ff));
                DrawRectangleLinesEx({-60, -30, 120, 60}, 3, col::green);
                ui::text("PAYCHECK", 0, -10, 15, rgba(0x1a5a2aff), ui::Center, true);
                ui::text("$$$", 0, 12, 18, rgba(0x1a5a2aff), ui::Center, true);
            } else {
                DrawRectangleRounded({-46, -32, 92, 64}, 0.1f, 6, WHITE);
                DrawRectangleLinesEx({-46, -32, 92, 64}, 3, col::red);
                ui::text("RENT", 0, -10, 18, col::red, ui::Center, true);
                ui::text("due!!", 0, 12, 14, col::outline, ui::Center, true);
            }
            rlPopMatrix();
        }
        art::portrait("pieter", {kx_, 620}, 48, col::outline);
        DrawRectangleRounded({kx_ - 70, 580, 140, 18}, 1, 8, rgba(0x8a5a32ff));  // tray
        ui::text("catch the paycheck - not the bills", VW / 2.0f, 690, 20, WHITE, ui::Center, true);
    }

private:
    struct It { Vector2 p; bool good; };
    std::vector<It> items_;
    float kx_ = 640, speed_ = 280;
    bool useKeys_ = false;
};

// ---- 6. hide when the boss turns around
class HideBoss : public Micro {
public:
    const char* verb() const override { return "HIDE!"; }
    const char* job() const override { return "Office"; }
    Color color() const override { return rgba(0x5a5a7aff); }
    bool winOnTimeout() const override { return true; }
    void start(int level) override { turnAt_ = rnd(0.8f, 1.6f); lookLen_ = 1.0f + level * 0.05f; }
    void update(float dt) override {
        t_ += dt;
        hiding_ = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || E().mouseDown();
        looking_ = t_ > turnAt_ && t_ < turnAt_ + lookLen_;
        warn_ = t_ > turnAt_ - 0.5f && t_ < turnAt_;
        if (t_ > turnAt_ + lookLen_ + 0.6f) turnAt_ = t_ + rnd(0.5f, 1.0f);
        if (looking_ && !hiding_ && !decided()) lose();
        if (!looking_ && hiding_ && !decided() && !warn_) slack_ += dt;
    }
    void draw(float t) override {
        stageBg(rgba(0x8a8aa8ff), rgba(0x4a4a6aff));
        // boss office window at the back
        Rectangle win{460, 60, 360, 260};
        DrawRectangleRec(shrink(win, -10), rgba(0x2a2a3aff));
        DrawRectangleRec(win, rgba(0xc8d8f0ff));
        if (looking_ || warn_) {
            art::portrait("davi", {640, 200}, 90, col::outline, warn_ ? std::sin(t * 30) * 8 : 0);
        } else {
            // back of his head, headphones on, busy gaming
            DrawCircleV({640, 200}, 92, col::outline);
            DrawCircleV({640, 200}, 88, rgba(0x5a3418ff));
            for (int i = 0; i < 9; i++) DrawCircleV(Vector2{640, 200} + fromAngle(-PI + i * PI / 8, 70), 22, rgba(0x6a4020ff));
            DrawRing({640, 210}, 84, 96, 180, 360, 24, rgba(0x2a2534ff));
            for (int s2 : {-1, 1}) DrawCircleV({640 + s2 * 90.0f, 220}, 22, rgba(0x2a2534ff));
        }
        if (looking_) {
            fx::beginAdd();
            fx::glow({610, 190}, 40, alpha(col::red, 0.7f));
            fx::glow({670, 190}, 40, alpha(col::red, 0.7f));
            fx::endAdd();
        }
        if (warn_) ui::textOutline("!", 640, 50, 80, col::red, col::outline, 4);
        // Pieter at his desk, monitor in front
        float py = hiding_ ? 560 : 430;
        art::portrait("pieter", {640, py}, 80, col::outline);
        desk(520, rgba(0x8a6a4aff));
        DrawRectangleRounded({500, 380, 280, 160}, 0.06f, 8, rgba(0x15121cff));
        DrawRectangleRec({515, 395, 250, 120}, rgba(0x3a7ad8ff));
        ui::text(hiding_ ? "working hard" : "PieterGame 3", 640, 455, 20, WHITE, ui::Center, true);
        ui::text("HOLD SPACE to hide when he turns", VW / 2.0f, 680, 22, WHITE, ui::Center, true);
    }

private:
    float t_ = 0, turnAt_ = 1, lookLen_ = 1, slack_ = 0;
    bool hiding_ = false, looking_ = false, warn_ = false;
};

// ---- 7. sort papers into folders
class SortPapers : public Micro {
public:
    const char* verb() const override { return "SORT!"; }
    const char* job() const override { return "Admin"; }
    Color color() const override { return rgba(0xb8783aff); }
    void start(int level) override {
        need_ = 3 + level / 4;
        for (int i = 0; i < need_; i++) papers_.push_back(rndi(0, 1));
    }
    void update(float dt) override {
        slide_ = std::max(0.0f, slide_ - dt * 6);
        if (decided() || idx_ >= (int)papers_.size()) return;
        int pick = -1;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) pick = 0;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) pick = 1;
        if (E().mousePressed()) pick = E().mouse().x < VW / 2.0f ? 0 : 1;
        if (pick >= 0) {
            if (pick == papers_[idx_]) {
                idx_++;
                slide_ = 1;
                lastDir_ = pick;
                E().sfx.play(Sfx::Card, 0.7f);
                if (idx_ == (int)papers_.size()) win();
            } else {
                lose();
            }
        }
    }
    void draw(float t) override {
        (void)t;
        stageBg(rgba(0x8a6a4aff), rgba(0x4a3420ff));
        Color fc[2] = {col::red, rgba(0x2a7ad8ff)};
        const char* fl[2] = {"BILLS", "MEMES"};
        for (int i = 0; i < 2; i++) {
            Rectangle f{i == 0 ? 120.0f : 860.0f, 360, 300, 230};
            DrawRectangleRounded({f.x, f.y - 30, 120, 40}, 0.3f, 6, darken(fc[i], 0.2f));
            DrawRectangleRounded(f, 0.06f, 6, fc[i]);
            ui::textShadow(fl[i], f.x + f.width / 2, f.y + f.height / 2, 40, WHITE, ui::Center, true, 3);
            ui::text(i == 0 ? "< LEFT" : "RIGHT >", f.x + f.width / 2, f.y + f.height + 30, 20, WHITE, ui::Center, true);
        }
        if (idx_ < (int)papers_.size()) {
            int k = papers_[idx_];
            Rectangle p{540, 200, 200, 260};
            DrawRectangleRec({p.x + 6, p.y + 8, p.width, p.height}, alpha(BLACK, 0.3f));
            DrawRectangleRec(p, WHITE);
            DrawRectangleRec({p.x, p.y, p.width, 40}, fc[k]);
            ui::text(k == 0 ? "INVOICE" : "funny.png", p.x + p.width / 2, p.y + 20, 18, WHITE, ui::Center, true);
            if (k == 0) {
                for (int i = 0; i < 5; i++) DrawRectangle((int)p.x + 20, (int)p.y + 70 + i * 26, 160 - i * 14, 8, rgba(0xc8ccd8ff));
                ui::text("TOTAL: 499,-", p.x + p.width / 2, p.y + 220, 18, col::red, ui::Center, true);
            } else {
                art::portrait("horse", {p.x + p.width / 2, p.y + 140}, 60, col::outline);
                ui::text("JUHUH", p.x + p.width / 2, p.y + 230, 20, col::outline, ui::Center, true);
            }
        }
        if (slide_ > 0) {
            float x = lastDir_ == 0 ? lerpf(270, 640, slide_) : lerpf(1010, 640, slide_);
            DrawRectangleRec({x - 60, 330 + (1 - slide_) * 80, 120, 150}, alpha(WHITE, slide_));
        }
        ui::text(TextFormat("%d / %d", idx_, (int)papers_.size()), VW / 2.0f, 520, 24, WHITE, ui::Center, true);
    }

private:
    std::vector<int> papers_;
    int idx_ = 0, need_ = 3, lastDir_ = 0;
    float slide_ = 0;
};

// ---- 8. flip the frikandellen when they're done
class Flip : public Micro {
public:
    const char* verb() const override { return "FLIP!"; }
    const char* job() const override { return "Snackbar"; }
    Color color() const override { return rgba(0xd8582aff); }
    void start(int level) override {
        for (int i = 0; i < 3; i++) items_[i] = {rnd(0.0f, 0.35f), false, 0.32f + level * 0.025f + rnd(-0.04f, 0.06f)};
    }
    void update(float dt) override {
        for (int i = 0; i < 3; i++) {
            auto& it = items_[i];
            if (!it.flipped) it.cook += it.rate * dt;
            it.flipAnim = std::max(0.0f, it.flipAnim - dt * 3);
            if (!decided() && !it.flipped && it.cook > 1.0f) lose();  // burnt
            Rectangle r{220 + i * 300.0f, 330, 240, 140};
            if (!decided() && !it.flipped && E().mousePressed() && CheckCollisionPointRec(E().mouse(), r)) {
                if (it.cook >= 0.62f) {
                    it.flipped = true;
                    it.flipAnim = 1;
                    E().sfx.play(Sfx::Jump, 0.6f);
                } else {
                    lose();
                }
            }
        }
        if (!decided() && items_[0].flipped && items_[1].flipped && items_[2].flipped) win();
    }
    void draw(float t) override {
        stageBg(rgba(0x5a2a1aff), rgba(0x1a0a08ff));
        // grill
        DrawRectangleRounded({150, 290, 980, 230}, 0.06f, 8, rgba(0x2a2a30ff));
        for (int i = 0; i < 14; i++) DrawLineEx({170, 300 + i * 16.0f}, {1110, 300 + i * 16.0f}, 3, rgba(0x4a4a52ff));
        for (int i = 0; i < 3; i++) {
            auto& it = items_[i];
            Vector2 c{340 + i * 300.0f, 400 - std::sin(it.flipAnim * PI) * 90};
            Color raw = rgba(0xd89a8aff), done = rgba(0x9a5a2aff), burnt = rgba(0x2a1a10ff);
            float k = std::min(it.cook, 1.0f);
            Color cc = k < 0.62f ? mix(raw, done, k / 0.62f) : mix(done, burnt, (k - 0.62f) / 0.38f);
            if (it.flipped) cc = done;
            rlPushMatrix();
            rlTranslatef(c.x, c.y, 0);
            rlRotatef(it.flipAnim * 360, 1, 0, 0);
            DrawRectangleRounded({-100, -22, 200, 44}, 1, 12, col::outline);
            DrawRectangleRounded({-97, -19, 194, 38}, 1, 12, cc);
            DrawLineEx({-80, -10}, {80, -10}, 2, alpha(WHITE, 0.2f));
            rlPopMatrix();
            if (!it.flipped && k > 0.4f) {
                for (int s = 0; s < 3; s++) {
                    float y = std::fmod(t * 40 + s * 15 + i * 7, 45.0f);
                    DrawCircleV({c.x - 40 + s * 40.0f, c.y - 30 - y}, 6 + y * 0.2f, alpha(WHITE, 0.25f * (1 - y / 45) * (k > 0.85f ? 2.5f : 1)));
                }
            }
            if (it.flipped) ui::text("OK!", c.x, c.y - 50, 24, col::green, ui::Center, true);
            // doneness meter
            ui::bar({c.x - 80, 540, 160, 14}, std::min(1.0f, it.cook), k < 0.62f ? col::gold : k < 0.9f ? col::green : col::red);
            DrawLineEx({c.x - 80 + 160 * 0.62f, 536}, {c.x - 80 + 160 * 0.62f, 558}, 2, WHITE);
        }
        ui::text("click each one when it's golden - not raw, not burnt", VW / 2.0f, 640, 20, WHITE, ui::Center, true);
    }

private:
    struct It { float cook; bool flipped; float rate; float flipAnim = 0; };
    It items_[3];
};

// ---- 9. snooze the jumping alarm clock
class Snooze : public Micro {
public:
    const char* verb() const override { return "SNOOZE!"; }
    const char* job() const override { return "Monday"; }
    Color color() const override { return rgba(0x2a2a5aff); }
    void start(int level) override {
        need_ = 3 + level / 3;
        jump();
    }
    void jump() { pos_ = {rnd(160, 1120), rnd(160, 560)}; }
    void update(float dt) override {
        pop_ = std::max(0.0f, pop_ - dt * 4);
        if (decided()) return;
        if (E().mousePressed() && Vector2Distance(E().mouse(), pos_) < 70) {
            hits_++;
            pop_ = 1;
            E().sfx.play(Sfx::Bell, 0.3f, 1.6f);
            if (hits_ >= need_) win();
            else jump();
        }
    }
    void draw(float t) override {
        stageBg(rgba(0x1a1a3aff), rgba(0x0a0a1aff));
        DrawRectangleGradientV(0, 480, VW, 240, rgba(0x3a7bd5ff), rgba(0x1a3a7aff));  // blanket
        art::portrait("pieter", {640, 470}, 70, col::outline, 15);
        ui::text("zzz", 720, 380, 28, rgba(0xb8c4ffff), ui::Center, true);
        // ringing clock
        Vector2 c = pos_ + Vector2{decided() ? 0 : std::sin(t * 60) * 5, 0};
        float r = 54 * (1 + pop_ * 0.2f);
        for (int s : {-1, 1}) DrawCircleV({c.x + s * r * 0.6f, c.y - r * 0.9f}, r * 0.28f, col::red);
        DrawCircleV(c, r + 4, col::outline);
        DrawCircleV(c, r, col::red);
        DrawCircleV(c, r * 0.8f, WHITE);
        DrawLineEx(c, c + fromAngle(t * 4, r * 0.55f), 4, col::outline);
        DrawLineEx(c, c + fromAngle(-PI / 2, r * 0.4f), 5, col::outline);
        ui::text("RING", c.x - r - 30, c.y - r, 22, col::gold, ui::Center, true);
        ui::text("RING", c.x + r + 30, c.y - r + 10, 22, col::gold, ui::Center, true);
        ui::text(TextFormat("%d / %d", hits_, need_), VW / 2.0f, 680, 24, WHITE, ui::Center, true);
    }

private:
    Vector2 pos_{640, 300};
    int hits_ = 0, need_ = 3;
    float pop_ = 0;
};

// ---- 10. deliver the boba on the bike
class Deliver : public Micro {
public:
    const char* verb() const override { return "DELIVER!"; }
    const char* job() const override { return "Delivery"; }
    Color color() const override { return rgba(0x2f7ad8ff); }
    bool winOnTimeout() const override { return true; }
    void start(int level) override {
        speed_ = 420 + level * 25;
        for (int i = 0; i < 5 + level / 2; i++) cones_.push_back({900.0f + i * rnd(260, 360), (float)rndi(0, 2)});
    }
    void update(float dt) override {
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) lane_ = std::max(0, lane_ - 1);
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) lane_ = std::min(2, lane_ + 1);
        if (E().mousePressed()) lane_ = std::clamp((int)((E().mouse().y - 360) / 110), 0, 2);
        y_ = damp(y_, laneY(lane_), 16, dt);
        ph_ += dt * 12;
        for (auto& c : cones_) {
            c.x -= speed_ * dt;
            if (!decided() && std::abs(c.x - 300) < 50 && std::abs(laneY((int)c.y) - y_) < 40) lose();
        }
    }
    static float laneY(int l) { return 430 + l * 110.0f; }
    void draw(float t) override {
        (void)t;
        stageBg(rgba(0x8fc8ffff), rgba(0xd8ecffff));
        for (int i = 0; i < 10; i++) {
            float x = std::fmod(i * 160 - ph_ * 8, 1600.0f) - 100;
            float h = 120 + (i * 37 % 80);
            DrawRectangleRec({x, 360 - h, 110, h}, i % 2 ? rgba(0x8a4a3aff) : rgba(0x6a3a2aff));
            for (int s = 1; s <= 3; s++) DrawRectangleRec({x + s * 13.0f, 360 - h - s * 8, 110 - s * 26.0f, 8}, i % 2 ? rgba(0x8a4a3aff) : rgba(0x6a3a2aff));
        }
        DrawRectangle(0, 360, VW, 360, rgba(0x6a6a72ff));
        for (int l = 0; l < 3; l++)
            for (int k = 0; k < 14; k++) {
                float x = std::fmod(k * 110 - ph_ * 35, 1540.0f) - 100;
                DrawRectangleRec({x, laneY(l) + 50, 60, 6}, alpha(WHITE, 0.6f));
            }
        for (auto& c : cones_) {
            float y = laneY((int)c.y) + 30;
            DrawTriangle({c.x, y - 60}, {c.x - 26, y}, {c.x + 26, y}, col::orange);
            DrawTriangle({c.x, y - 60}, {c.x + 26, y}, {c.x - 26, y}, col::orange);
            DrawRectangleRec({c.x - 14, y - 34, 28, 8}, WHITE);
            DrawRectangleRec({c.x - 32, y - 4, 64, 8}, darken(col::orange, 0.3f));
        }
        art::bike({240, y_ + 30}, 120, ph_, rgba(0x1a1a24ff));
        art::pieter({300, y_ - 50}, 30, P().hat);
        art::boba({380, y_ - 50}, 34);
        ui::text("UP / DOWN to dodge the cones", VW / 2.0f, 690, 20, col::outline, ui::Center, true);
    }

private:
    struct Cone { float x, y; };
    std::vector<Cone> cones_;
    int lane_ = 1;
    float y_ = 540, speed_ = 450, ph_ = 0;
};

std::unique_ptr<Micro> makeMicro(int kind) {
    switch (kind) {
        case 0: return std::make_unique<TypeCV>();
        case 1: return std::make_unique<Handshake>();
        case 2: return std::make_unique<StayAwake>();
        case 3: return std::make_unique<Coffee>();
        case 4: return std::make_unique<Payday>();
        case 5: return std::make_unique<HideBoss>();
        case 6: return std::make_unique<SortPapers>();
        case 7: return std::make_unique<Flip>();
        case 8: return std::make_unique<Snooze>();
        default: return std::make_unique<Deliver>();
    }
}
constexpr int kMicroCount = 10;

// ====================================================================== scene

enum class Phase { Title, Brief, Play, Result, Over };

class JobScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Title) return {"Get A Job Simulator", "Reading the job ads"};
        return {"Get A Job Simulator", std::to_string(score_) + " jobs survived"};
    }

    void enter() override {
        if (const char* m = std::getenv("PG_JOB")) {  // dev hook: play one microgame
            devKind_ = std::atoi(m);
            startRun();
        }
    }

    void startRun() {
        score_ = 0;
        lives_ = 4;
        speed_ = 1;
        bag_.clear();
        nextJob();
    }

    void nextJob() {
        if (bag_.empty()) {
            for (int i = 0; i < kMicroCount; i++) bag_.push_back(i);
            std::shuffle(bag_.begin(), bag_.end(), rng());
        }
        int kind = devKind_ >= 0 ? devKind_ : bag_.back();
        bag_.pop_back();
        micro_ = makeMicro(kind);
        micro_->start(score_);
        phase_ = Phase::Brief;
        phaseT_ = 0;
        speedUp_ = score_ > 0 && score_ % 5 == 0;
        if (speedUp_) {
            speed_ = std::min(1.9f, speed_ + 0.15f);
            E().sfx.play(Sfx::Power, 0.6f);
        } else {
            E().sfx.play(Sfx::Whoosh, 0.5f);
        }
    }

    float briefLen() const { return (speedUp_ ? 2.0f : 1.3f) / speed_; }
    float playLen() const { return 5.0f / speed_; }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        phaseT_ += dt;
        switch (phase_) {
            case Phase::Title:
                if (E().backPressed()) E().go(makeMenu);
                break;
            case Phase::Brief:
                if (phaseT_ >= briefLen()) {
                    phase_ = Phase::Play;
                    phaseT_ = 0;
                    GetCharPressed();
                }
                break;
            case Phase::Play: {
                micro_->update(dt * speed_);
                bool timeUp = phaseT_ >= playLen();
                // finish a little early once decided, so it feels snappy
                if ((micro_->decided() && phaseT_ > 0.35f) || timeUp) {
                    bool won = micro_->won || (!micro_->lost && micro_->winOnTimeout());
                    lastWon_ = won;
                    if (won) {
                        score_++;
                        fx_.confetti({VW / 2.0f, 300}, 50);
                        E().sfx.play(Sfx::Coin, 0.6f);
                    } else {
                        lives_--;
                        E().shake(8);
                        E().sfx.play(Sfx::Lose, 0.5f);
                    }
                    phase_ = Phase::Result;
                    phaseT_ = 0;
                }
                break;
            }
            case Phase::Result:
                if (phaseT_ >= 1.1f / std::min(speed_, 1.4f)) {
                    if (lives_ <= 0) gameOver();
                    else nextJob();
                }
                break;
            case Phase::Over:
                if (E().backPressed()) phase_ = Phase::Title;
                break;
        }
        if (phase_ == Phase::Play && E().backPressed()) phase_ = Phase::Title;
    }

    void gameOver() {
        phase_ = Phase::Over;
        phaseT_ = 0;
        coins_ = score_ * 8 + (score_ >= 10 ? 40 : 0);
        best_ = score_ > P().stats.jobBest;
        P().stats.jobBest = std::max(P().stats.jobBest, score_);
        if (coins_ > 0) P().earn(coins_);
        P().save();
        E().sfx.play(best_ ? Sfx::Win : Sfx::Lose);
    }

    // ------------------------------------------------------------ draw
    void cups(Vector2 at, float s) {
        for (int i = 0; i < 4; i++) {
            Vector2 p{at.x + i * 46 * s, at.y};
            bool full = i < lives_;
            DrawRectangleRounded({p.x - 14 * s, p.y - 16 * s, 28 * s, 32 * s}, 0.2f, 6, full ? WHITE : alpha(WHITE, 0.2f));
            DrawRing({p.x + 15 * s, p.y - 2 * s}, 6 * s, 9 * s, -90, 90, 10, full ? WHITE : alpha(WHITE, 0.2f));
            if (full) DrawRectangleRec({p.x - 11 * s, p.y - 12 * s, 22 * s, 7 * s}, rgba(0x5a2c10ff));
        }
    }

    void drawBrief() {
        Color c = micro_->color();
        DrawRectangleGradientV(0, 0, VW, VH, lighten(c, 0.1f), darken(c, 0.6f));
        // sunburst
        fx::beginAdd();
        for (int i = 0; i < 16; i++) {
            float a = t_ * 0.3f + i * PI / 8;
            Vector2 d = fromAngle(a, 1000), d2 = fromAngle(a + 0.12f, 1000);
            DrawTriangle({VW / 2.0f, 380}, Vector2{VW / 2.0f, 380} + d, Vector2{VW / 2.0f, 380} + d2, alpha(WHITE, 0.05f));
            DrawTriangle({VW / 2.0f, 380}, Vector2{VW / 2.0f, 380} + d2, Vector2{VW / 2.0f, 380} + d, alpha(WHITE, 0.05f));
        }
        fx::endAdd();
        float k = easeOutBack(clamp01(phaseT_ * speed_ / 0.35f));
        ui::text(TextFormat("JOB #%d", score_ + 1), VW / 2.0f, 120, 28, alpha(WHITE, 0.85f), ui::Center, true);
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, 200, 0);
        rlScalef(k, k, 1);
        fx::gradientText(micro_->job(), 0, 0, 74, WHITE, lighten(c, 0.6f), 5);
        rlPopMatrix();
        float k2 = easeOutBack(clamp01((phaseT_ * speed_ - 0.25f) / 0.3f));
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, 380, 0);
        rlScalef(k2, k2, 1);
        rlRotatef(-4 + std::sin(t_ * 6) * 2, 0, 0, 1);
        ui::textOutline(micro_->verb(), 0, 0, 120, col::gold, col::outline, 7);
        rlPopMatrix();
        art::pieter({VW / 2.0f, 580 + std::sin(t_ * 8) * 6}, 70, "hat_cap");
        DrawTriangle({VW / 2.0f - 14, 640}, {VW / 2.0f, 690}, {VW / 2.0f + 14, 640}, rgba(0xc8283aff));  // tie
        cups({60, 60}, 1);
        if (speedUp_) ui::textOutline("SPEED UP!", VW / 2.0f, 470, 50, col::red, col::outline, 4);
    }

    void drawHud() {
        // fuse-style timer along the bottom
        float left = 1 - clamp01(phaseT_ / playLen());
        DrawRectangle(0, VH - 14, VW, 14, alpha(BLACK, 0.5f));
        Color fc = left > 0.3f ? col::gold : col::red;
        DrawRectangle(0, VH - 14, (int)(VW * left), 14, fc);
        fx::beginAdd();
        fx::glow({VW * left, VH - 7.0f}, 30, alpha(col::orange, 0.8f));
        fx::endAdd();
        // job + verb chip
        Rectangle chip{20, 16, ui::measure(micro_->verb(), 22, true) + 40, 40};
        DrawRectangleRounded(chip, 1, 10, alpha(BLACK, 0.55f));
        ui::text(micro_->verb(), chip.x + chip.width / 2, chip.y + 20, 22, col::gold, ui::Center, true);
        cups({VW - 170.0f, 36}, 0.8f);
    }

    void drawResult() {
        micro_->draw(t_);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.45f));
        float k = easeOutBack(clamp01(phaseT_ / 0.25f));
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, VH / 2.0f - 40, 0);
        rlScalef(1.6f - k * 0.6f, 1.6f - k * 0.6f, 1);
        rlRotatef(lastWon_ ? -6 : 8, 0, 0, 1);
        Color c = lastWon_ ? col::green : col::red;
        Rectangle st{-260, -70, 520, 140};
        DrawRectangleRoundedLinesEx(st, 0.2f, 8, 10, alpha(c, k));
        ui::text(lastWon_ ? "HIRED!" : "FIRED!", 0, 0, 100, alpha(c, k), ui::Center, true);
        rlPopMatrix();
        art::portrait("pieter", {VW / 2.0f, VH / 2.0f + 140}, 50, c, lastWon_ ? std::sin(t_ * 12) * 10 : 15);
        cups({VW / 2.0f - 70, VH / 2.0f + 230}, 1);
    }

    void drawTitle() {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x1f9a8aff), rgba(0x0c3a3aff));
        for (int i = 0; i < 12; i++) {
            float x = std::fmod(i * 130 + t_ * 30, 1500.0f) - 100;
            ui::textRotated("GET A JOB", {x, 80.0f + (i % 4) * 170}, 40, -12, alpha(WHITE, 0.05f), true);
        }
        fx::gradientText("GET A JOB", VW / 2.0f, 130, 96, WHITE, rgba(0xb8f0e0ff), 6);
        ui::text("SIMULATOR", VW / 2.0f, 200, 32, col::gold, ui::Center, true);
        art::modeArt("job", {VW / 2.0f - 160, 230, 320, 230}, t_, 1);
        ui::paragraph("Pieter's mom says get a job. So he tries ALL of them, five seconds each. Read the word, do the thing. Four coffees = four "
                      "chances. It gets faster.",
                      {VW / 2.0f - 300, 480, 600, 0}, 18, alpha(WHITE, 0.85f));
        ui::text("Best: " + std::to_string(P().stats.jobBest) + " jobs", VW / 2.0f, 575, 20, col::gold, ui::Center, true);
        if (ui::bigButton({VW / 2.0f - 170, 600, 340, 80}, "APPLY", "", rgba(0xd8582aff), 40)) startRun();
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawOver() {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x3a1a2aff), rgba(0x0c0812ff));
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, 150, 0);
        rlScalef(k, k, 1);
        rlRotatef(-5, 0, 0, 1);
        ui::textOutline("UNEMPLOYED", 0, 0, 90, col::red, col::outline, 6);
        rlPopMatrix();
        art::portrait("pieter", {VW / 2.0f, 300}, 70, col::outline, 12);
        ui::text("(again)", VW / 2.0f, 390, 20, col::muted, ui::Center, true);
        ui::textShadow(std::to_string(score_) + " jobs survived", VW / 2.0f, 450, 40, WHITE, ui::Center, true, 3);
        if (best_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW RECORD!", VW / 2.0f, 494, 22, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 80, 534}, 14);
        ui::text("+" + fmtNum(coins_) + " Pietercoins", VW / 2.0f - 58, 534, 22, col::gold, ui::Left, true);
        if (phaseT_ > 0.8f) {
            if (ui::button({VW / 2.0f - 260, 590, 250, 64}, "APPLY AGAIN", rgba(0xd8582aff), 24)) startRun();
            if (ui::button({VW / 2.0f + 10, 590, 250, 64}, "GIVE UP", col::panelHi, 24)) phase_ = Phase::Title;
        }
    }

    void draw() override {
        switch (phase_) {
            case Phase::Title: drawTitle(); break;
            case Phase::Brief: drawBrief(); break;
            case Phase::Play:
                micro_->draw(t_);
                drawHud();
                break;
            case Phase::Result: drawResult(); break;
            case Phase::Over: drawOver(); break;
        }
        fx_.draw();

    }

private:
    Phase phase_ = Phase::Title;
    std::unique_ptr<Micro> micro_;
    std::vector<int> bag_;
    int score_ = 0, lives_ = 4, devKind_ = -1;
    long long coins_ = 0;
    float t_ = 0, phaseT_ = 0, speed_ = 1;
    bool speedUp_ = false, lastWon_ = false, best_ = false;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeJob() { return std::make_unique<JobScene>(); }

}  // namespace pg
