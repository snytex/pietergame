// Plays assets/video/intro.mpg (MPEG-1, decoded with pl_mpeg) in sync with
// assets/video/intro.ogg, then continues to the main menu. Shown on the first
// launch and from Settings -> WATCH INTRO.
#include "engine/Ui.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" {
#include "external/pl_mpeg.h"
}

namespace pg {
namespace {

class IntroVideoScene : public Scene {
public:
    MusicMode music() const override { return MusicMode::None; }
    Presence presence() const override { return {"Watching the intro"}; }

    void enter() override {
        plm_ = plm_create_with_filename(E().assetPath("video/intro.mpg").c_str());
        if (!plm_) {
            finish();
            return;
        }
        plm_set_audio_enabled(plm_, 0);
        plm_set_loop(plm_, 0);
        w_ = plm_get_width(plm_);
        h_ = plm_get_height(plm_);
        frameDur_ = 1.0 / std::max(1.0, plm_get_framerate(plm_));
        pixels_.assign((size_t)w_ * h_ * 4, 255);  // pl_mpeg never writes the alpha byte: keep it opaque
        Image img = GenImageColor(w_, h_, BLACK);
        tex_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(tex_, TEXTURE_FILTER_BILINEAR);

        audio_ = LoadMusicStream(E().assetPath("video/intro.ogg").c_str());
        hasAudio_ = audio_.stream.buffer != nullptr;
        if (hasAudio_) {
            audio_.looping = false;
            SetMusicVolume(audio_, E().music.volume());  // the intro is music: follows the music volume
            PlayMusicStream(audio_);
        }
    }

    void exit() override {
        if (hasAudio_) {
            StopMusicStream(audio_);
            UnloadMusicStream(audio_);
        }
        if (tex_.id) UnloadTexture(tex_);
        if (plm_) plm_destroy(plm_);
        plm_ = nullptr;
    }

    void finish() {
        if (done_) return;
        done_ = true;
        P().introSeen = true;
        P().save();
        E().go(makeMenu);
    }

    void update(float dt) override {
        t_ += dt;
        if (!plm_) return;
        // Wall time drives the video; when the audio is really advancing we
        // snap to it so picture and music stay in sync (and a missing or
        // stalled audio device can't freeze the video).
        clock_ += dt;
        if (hasAudio_) {
            UpdateMusicStream(audio_);
            float at = IsMusicStreamPlaying(audio_) ? GetMusicTimePlayed(audio_) : 0;
            if (at > 0.05f && std::abs(at - clock_) > 0.1f) clock_ = at;
        }
        bool dirty = false;
        while (!ended_ && nextFrame_ <= clock_) {
            plm_frame_t* f = plm_decode_video(plm_);
            if (!f) { ended_ = true; break; }
            plm_frame_to_rgba(f, pixels_.data(), w_ * 4);
            nextFrame_ = f->time + frameDur_;
            dirty = true;
        }
        if (dirty) UpdateTexture(tex_, pixels_.data());
        if (ended_ && (!hasAudio_ || !IsMusicStreamPlaying(audio_) || clock_ > introDuration() + 0.5f)) finish();

        bool skip = t_ > 0.4f && (GetKeyPressed() != 0 || E().mousePressed() || E().rightPressed());
        if (skip) finish();
    }

    void draw() override {
        ClearBackground(BLACK);
        if (tex_.id) DrawTexturePro(tex_, {0, 0, (float)w_, (float)h_}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, WHITE);
        float a = clamp01((t_ - 1.0f) / 0.5f) * (0.55f + 0.2f * std::sin(t_ * 3));
        ui::textShadow("press any key to skip", VW - 24, VH - 24, 18, alpha(WHITE, a), ui::Right, true, 2);
    }

private:
    plm_t* plm_ = nullptr;
    Texture2D tex_{};
    Music audio_{};
    bool hasAudio_ = false, ended_ = false, done_ = false;
    std::vector<uint8_t> pixels_;
    int w_ = 0, h_ = 0;
    double frameDur_ = 1.0 / 30, nextFrame_ = 0, clock_ = 0;
    float t_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeIntroVideo() { return std::make_unique<IntroVideoScene>(); }

}  // namespace pg
