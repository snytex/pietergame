// SETTINGS: audio, display, the jukebox, controls, and save reset.
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/DiscordConfig.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>

namespace pg {
namespace {

class SettingsScene : public Scene {
public:
    Presence presence() const override { return {"In the settings"}; }

    MusicMode music() const override { return MusicMode::Title; }

    void enter() override {
        // preselect the current resolution
        auto res = E().resolutions();
        for (int i = 0; i < (int)res.size(); i++)
            if (res[i].first <= E().resWidth()) pick_ = i;
    }

    void exit() override { P().save(); }

    void changeMonitor(int index) {
        E().setMonitor(index);
        P().monitor = E().monitorName(index);
        P().save();
        enter();  // the new monitor may offer different resolutions
    }

    void applyDisplay(bool fullscreen) {
        auto res = E().resolutions();
        auto [w, h] = res[std::clamp(pick_, 0, (int)res.size() - 1)];
        E().setDisplay(fullscreen, w, h);
        P().fullscreen = fullscreen;
        P().resW = w;
        P().resH = h;
        P().monitor = E().monitorName(E().monitor());
        P().save();
    }

    void update(float dt) override {
        t_ += dt;
        resetArm_ = std::max(0.0f, resetArm_ - dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    void draw() override {
        art::purpleBackdrop(t_);
        ui::titleBar("SETTINGS", col::violet);
        if (ui::backButton()) E().go(makeMenu);

        Rectangle a{40, 90, 560, 440};
        ui::panel(a);
        ui::text("AUDIO & DISPLAY", a.x + 24, a.y + 32, 24, col::text, ui::Left, true);
        ui::text("Music", a.x + 24, a.y + 80, 22, col::text);
        if (ui::slider({a.x + 170, a.y + 72, 320, 16}, P().musicVolume)) E().music.setVolume(P().musicVolume);
        ui::text(std::to_string((int)(P().musicVolume * 100)) + "%", a.x + a.width - 20, a.y + 80, 18, col::muted, ui::Right);
        ui::text("Sound FX", a.x + 24, a.y + 124, 22, col::text);
        if (ui::slider({a.x + 170, a.y + 116, 320, 16}, P().sfxVolume)) {
            E().sfx.setVolume(P().sfxVolume);
            if (std::fmod(t_, 0.15f) < GetFrameTime()) E().sfx.play(Sfx::Coin, 0.6f);
        }
        ui::text(std::to_string((int)(P().sfxVolume * 100)) + "%", a.x + a.width - 20, a.y + 124, 18, col::muted, ui::Right);

        // display mode applies immediately
        bool fs = E().fullscreen();
        ui::text("Display", a.x + 24, a.y + 178, 22, col::text);
        if (ui::button({a.x + 170, a.y + 156, 160, 44}, "WINDOWED", fs ? col::panelHi : col::purple, 18) && fs) applyDisplay(false);
        if (ui::button({a.x + 342, a.y + 156, 194, 44}, "FULLSCREEN", fs ? col::purple : col::panelHi, 18) && !fs) applyDisplay(true);

        // monitor: applies immediately
        int mon = E().monitor(), monCount = E().monitorCount();
        ui::text("Monitor", a.x + 24, a.y + 230, 22, col::text);
        if (ui::button({a.x + 170, a.y + 208, 44, 44}, "<", col::panelHi, 22, monCount > 1)) changeMonitor((mon + monCount - 1) % monCount);
        std::string mname = E().monitorName(mon);
        if (mname.size() > 14) mname = mname.substr(0, 13) + "..";
        ui::text(TextFormat("%d: %s (%dx%d)%s", mon + 1, mname.c_str(), GetMonitorWidth(mon), GetMonitorHeight(mon), mon == 0 ? " *" : ""),
                 a.x + 340, a.y + 230, 17, col::text, ui::Center, true);
        if (ui::button({a.x + 492, a.y + 208, 44, 44}, ">", col::panelHi, 22, monCount > 1)) changeMonitor((mon + 1) % monCount);

        // resolution: pick with arrows, then APPLY
        auto res = E().resolutions();
        pick_ = std::clamp(pick_, 0, (int)res.size() - 1);
        ui::text("Resolution", a.x + 24, a.y + 284, 22, col::text);
        if (ui::button({a.x + 170, a.y + 262, 44, 44}, "<", col::panelHi, 22, pick_ > 0)) pick_--;
        auto [pw, ph] = res[pick_];
        bool native = pw == GetMonitorWidth(mon) && ph == GetMonitorHeight(mon);
        ui::text(TextFormat("%d x %d%s", pw, ph, native ? " (native)" : ""), a.x + 312, a.y + 284, 20, col::gold, ui::Center, true);
        if (ui::button({a.x + 410, a.y + 262, 44, 44}, ">", col::panelHi, 22, pick_ + 1 < (int)res.size())) pick_++;
        bool changed = pw != E().resWidth() || ph != E().resHeight();
        if (ui::button({a.x + 464, a.y + 262, 72, 44}, "APPLY", changed ? col::green : col::panelHi, 16, changed)) applyDisplay(fs);
        Vector2 cs = E().canvasSize();
        ui::text(TextFormat("%s  -  rendering at %d x %d   (* = primary monitor)",
                            fs ? "fullscreen scales to the monitor" : "window size", (int)cs.x, (int)cs.y),
                 a.x + 24, a.y + 324, 15, col::muted);

        if (ui::toggle({a.x + 24, a.y + 344, 236, 50}, "Screen shake", P().screenShake)) E().screenShake = P().screenShake;
        if (ui::toggle({a.x + 290, a.y + 344, 246, 50}, "Discord status", P().discordRpc)) {
            if (P().discordRpc) E().discord.start(discordAppId());
            else E().discord.stop();
            P().save();
        }
        if (P().discordRpc && discordAppId().empty())
            ui::text("no Discord app id set (see DiscordConfig.hpp)", a.x + a.width - 20, a.y + 404, 13, col::muted, ui::Right);
        else if (P().discordRpc)
            ui::text(E().discord.connected() ? "connected to Discord" : "Discord not running", a.x + a.width - 20, a.y + 404, 13,
                     E().discord.connected() ? col::green : col::muted, ui::Right);

        Rectangle c{40, 550, 560, 140};
        ui::panel(c);
        ui::text("CONTROLS", c.x + 24, c.y + 24, 20, col::text, ui::Left, true);
        const char* lines[] = {"Classic: WASD / arrows move, SPACE dash, 1-3 powerups",
                               "Horses (ride): alternate LEFT/RIGHT, SPACE jump",
                               "Stroopwafel: SPACE or click to drop",
                               "Esc: back / pause      F9: next song      F11: fullscreen"};
        for (int i = 0; i < 4; i++) ui::text(lines[i], c.x + 24, c.y + 52 + i * 22, 15, col::muted);

        // jukebox
        Rectangle j{630, 100, 610, 520};
        ui::panel(j);
        ui::text("JUKEBOX", j.x + 24, j.y + 32, 24, col::text, ui::Left, true);
        ui::text("click a song to play it", j.x + j.width - 24, j.y + 32, 16, col::muted, ui::Right);
        int n = E().music.trackCount();
        float rowH = 34;
        for (int i = 0; i < n; i++) {
            int colIdx = i / 11, row = i % 11;
            Rectangle r{j.x + 16 + colIdx * 296.0f, j.y + 60 + row * rowH, 288, rowH - 2};
            bool cur = E().music.currentTrack() == i;
            bool hot = ui::hovered(r);
            if (cur || hot) DrawRectangleRounded(r, 0.4f, 6, cur ? alpha(col::cyan, 0.25f) : alpha(WHITE, 0.06f));
            std::string name = E().music.trackName(i);
            if (name.size() > 28) name = name.substr(0, 27) + "...";
            ui::text(name, r.x + 12, r.y + rowH / 2 - 1, 18, cur ? col::cyan : col::text);
            if (hot && E().mousePressed()) {
                E().consumeClick();
                E().music.playTrack(i);
            }
        }
        if (ui::button({j.x + 24, j.y + j.height - 64, 180, 46}, "SHUFFLE", col::purple, 20)) E().music.next();
        if (ui::button({j.x + 220, j.y + j.height - 64, 180, 46}, "TITLE THEME", col::panelHi, 18)) {
            for (int i = 0; i < n; i++)
                if (E().music.trackName(i) == "PieterGame OST") E().music.playTrack(i);
        }

        // reset save
        Rectangle rb{j.x, 640, j.width, 50};
        if (ui::button(rb, resetArm_ > 0 ? "CLICK AGAIN TO WIPE EVERYTHING" : "RESET SAVE", resetArm_ > 0 ? col::red : rgba(0x5a2035ff), 20)) {
            if (resetArm_ > 0) {
                P().resetAll();
                resetArm_ = 0;
                E().toast("Save wiped. Fresh start, Pieter.", col::red);
            } else {
                resetArm_ = 3;
            }
        }
    }

private:
    float t_ = 0, resetArm_ = 0;
    int pick_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeSettings() { return std::make_unique<SettingsScene>(); }

}  // namespace pg
