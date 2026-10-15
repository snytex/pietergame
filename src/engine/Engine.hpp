// The PieterEngine: owns the window, the virtual-resolution canvas, the scene
// stack, assets, audio and global effects (fades, toasts, screen shake).
#pragma once

#include "engine/Audio.hpp"
#include "engine/Core.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pg {

class Scene {
public:
    virtual ~Scene() = default;
    virtual void enter() {}
    virtual void exit() {}
    virtual void update(float dt) = 0;
    virtual void draw() = 0;
    virtual MusicMode music() const { return MusicMode::Shuffle; }
};

using SceneFactory = std::function<std::unique_ptr<Scene>()>;

struct Toast {
    std::string text;
    Color color;
    float t = 0;
    float life = 2.6f;
};

class Engine {
public:
    bool init(const char* title);
    void run(std::unique_ptr<Scene> first);
    void shutdown();

    // Scene management (switch happens behind a fade).
    void go(SceneFactory next);
    void quit() { quit_ = true; }

    // Input in virtual (1280x720) coordinates. `clickConsumed` lets stacked
    // UI layers stop a click from leaking to whatever is underneath.
    Vector2 mouse() const { return mouse_; }
    bool mousePressed() const { return mousePressed_ && !clickConsumed_; }
    bool mouseReleased() const { return IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }
    bool mouseDown() const { return IsMouseButtonDown(MOUSE_BUTTON_LEFT); }
    bool rightPressed() const { return IsMouseButtonPressed(MOUSE_BUTTON_RIGHT); }
    void consumeClick() { clickConsumed_ = true; }
    bool backPressed() const { return IsKeyPressed(KEY_ESCAPE) && !transitioning(); }
    bool transitioning() const { return fadeDir_ != 0; }

    float time() const { return time_; }

    // Effects
    void shake(float amount);
    void toast(const std::string& text, Color color = col::text);
    void flash(Color c, float strength = 0.6f) { flashColor_ = c; flash_ = strength; }

    // Assets
    Font& font(bool big);
    Texture2D& tex(const std::string& name);  // assets/images/<name>.png
    std::string assetPath(const std::string& rel) const { return assetDir_ + "/" + rel; }

    Jukebox music;
    SoundBank sfx;
    bool screenShake = true;

    void setFullscreen(bool on);
    bool fullscreen() const;

    // Dev helper: save the canvas to `path` after `frames` frames, then quit.
    void autoScreenshot(const std::string& path, int frames) { shotPath_ = path; shotFrames_ = frames; }

private:
    void updateCanvasTransform();
    void drawToasts();

    RenderTexture2D canvas_{};
    std::unique_ptr<Scene> scene_;
    SceneFactory pending_;
    int fadeDir_ = 0;           // -1 fading out, +1 fading in
    float fade_ = 0;            // 0 = clear, 1 = black
    bool quit_ = false;

    float scale_ = 1;
    Vector2 offset_{};
    Vector2 mouse_{};
    bool mousePressed_ = false;
    bool clickConsumed_ = false;
    float time_ = 0;

    float shake_ = 0;
    float flash_ = 0;
    Color flashColor_ = WHITE;
    std::vector<Toast> toasts_;

    std::string shotPath_;
    int shotFrames_ = 0;

    std::string assetDir_;
    Font fontSmall_{}, fontBig_{};
    std::unordered_map<std::string, Texture2D> textures_;
};

Engine& E();

}  // namespace pg
