// SETTINGS: audio, display, the jukebox, controls, and save reset.
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

class SettingsScene : public Scene {
public:
    MusicMode music() const override { return MusicMode::Title; }

    void exit() override { P().save(); }

    void update(float dt) override {
        t_ += dt;
        resetArm_ = std::max(0.0f, resetArm_ - dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    void draw() override {
        art::purpleBackdrop(t_);
        ui::titleBar("SETTINGS", col::violet);
        if (ui::backButton()) E().go(makeMenu);

        Rectangle a{40, 100, 560, 330};
        ui::panel(a);
        ui::text("AUDIO & DISPLAY", a.x + 24, a.y + 32, 24, col::text, ui::Left, true);
        ui::text("Music", a.x + 24, a.y + 84, 22, col::text);
        if (ui::slider({a.x + 170, a.y + 76, 340, 16}, P().musicVolume)) E().music.setVolume(P().musicVolume);
        ui::text(std::to_string((int)(P().musicVolume * 100)) + "%", a.x + a.width - 24, a.y + 84, 18, col::muted, ui::Right);
        ui::text("Sound FX", a.x + 24, a.y + 134, 22, col::text);
        if (ui::slider({a.x + 170, a.y + 126, 340, 16}, P().sfxVolume)) {
            E().sfx.setVolume(P().sfxVolume);
            if (std::fmod(t_, 0.15f) < GetFrameTime()) E().sfx.play(Sfx::Coin, 0.6f);
        }
        ui::text(std::to_string((int)(P().sfxVolume * 100)) + "%", a.x + a.width - 24, a.y + 134, 18, col::muted, ui::Right);
        bool fs = E().fullscreen();
        if (ui::toggle({a.x + 24, a.y + 170, a.width - 48, 50}, "Fullscreen  (F11)", fs)) {
            E().setFullscreen(fs);
            P().fullscreen = fs;
        }
        if (ui::toggle({a.x + 24, a.y + 225, a.width - 48, 50}, "Screen shake", P().screenShake)) E().screenShake = P().screenShake;

        Rectangle c{40, 450, 560, 240};
        ui::panel(c);
        ui::text("CONTROLS", c.x + 24, c.y + 32, 24, col::text, ui::Left, true);
        const char* lines[] = {"Classic: WASD / arrows move, SPACE dash, 1-3 powerups",
                               "Horses (ride): alternate LEFT/RIGHT, SPACE jump",
                               "Stroopwafel: SPACE or click to drop",
                               "Esc: back / pause      F9: next song      F11: fullscreen"};
        for (int i = 0; i < 4; i++) ui::text(lines[i], c.x + 24, c.y + 72 + i * 32, 18, col::muted);
        std::string path = P().path();
        if (path.size() > 52) path = "..." + path.substr(path.size() - 49);
        ui::text(path, c.x + 24, c.y + 72 + 4 * 32, 15, alpha(col::muted, 0.7f));

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
};

}  // namespace

std::unique_ptr<Scene> makeSettings() { return std::make_unique<SettingsScene>(); }

}  // namespace pg
