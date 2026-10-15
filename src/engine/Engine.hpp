// The PieterEngine: owns the window, the virtual-resolution canvas, the scene
// stack, assets, audio and global effects (fades, toasts, screen shake).
#pragma once

#include "engine/Audio.hpp"
#include "engine/Core.hpp"
#include "engine/Discord.hpp"

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
    // What Discord shows while this scene is active.
    virtual Presence presence() const { return {"In the menus"}; }
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
    // `resW/resH` = 0 picks a size that fits the monitor. `monitor` is a monitor
    // name from a previous session; empty or unplugged = the primary monitor.
    bool init(const char* title, bool fullscreen, int resW, int resH, const std::string& monitor);
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
    Discord discord;
    // Lets the game fill in defaults (e.g. the coin balance) before sending.
    std::function<void(Presence&)> presenceHook;
    bool screenShake = true;

    // Display. Windowed: the resolution is the window size and the game renders
    // at whatever size the window is. Fullscreen (borderless): the game renders
    // at the chosen resolution and scales it to the monitor.
    void setDisplay(bool fullscreen, int resW, int resH);
    void setFullscreen(bool on) { setDisplay(on, resW_, resH_); }
    bool fullscreen() const;
    int resWidth() const { return resW_; }
    int resHeight() const { return resH_; }
    Vector2 canvasSize() const { return {(float)canvas_.texture.width, (float)canvas_.texture.height}; }
    std::vector<std::pair<int, int>> resolutions() const;  // presets that fit the game's monitor

    // Multi-monitor: which monitor the game lives on (windowed: the one the
    // window was dragged to; fullscreen: the one it fills).
    int monitor() const;
    void setMonitor(int index);  // moves the game there, keeping mode + resolution
    int monitorCount() const { return GetMonitorCount(); }
    std::string monitorName(int index) const;

    // Clip drawing to a rectangle given in virtual (1280x720) coordinates.
    void beginClip(Rectangle r);
    void endClip() { EndScissorMode(); }

    // Dev helper: save the canvas to `path` after `frames` frames, then quit.
    void autoScreenshot(const std::string& path, int frames) { shotPath_ = path; shotFrames_ = frames; }

private:
    void updateCanvasTransform();
    void drawToasts();

    void resizeCanvas();
    void trackMonitor(float dt);

    RenderTexture2D canvas_{};
    float canvasScale_ = 1;  // canvas pixels per virtual unit
    int resW_ = 0, resH_ = 0;
    int monitor_ = 0;
    bool fullscreen_ = false;
    float placeCooldown_ = 0, monitorPoll_ = 0;
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
    float presenceT_ = 0;
    long long sessionStart_ = 0;

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
