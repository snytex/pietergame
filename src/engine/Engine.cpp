#include "engine/Engine.hpp"

#include "engine/Ui.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>

#include <rlgl.h>

namespace fs = std::filesystem;

namespace pg {

static Engine g_engine;
Engine& E() { return g_engine; }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }
float rnd(float lo, float hi) { return lo + (hi - lo) * rnd(); }
int rndi(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng()); }
bool chance(float p) { return rnd() < p; }
Vector2 rndDir() { return fromAngle(rnd(0, 2 * PI)); }

std::string fmtNum(long long n) {
    bool neg = n < 0;
    std::string s = std::to_string(neg ? -n : n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
    return neg ? "-" + s : s;
}

static std::string findAssets() {
    std::vector<std::string> candidates = {"assets", std::string(GetApplicationDirectory()) + "assets",
                                           std::string(GetApplicationDirectory()) + "../assets"};
#ifdef PG_SOURCE_ASSETS
    candidates.push_back(PG_SOURCE_ASSETS);
#endif
    for (auto& c : candidates)
        if (fs::exists(c + "/images/menu_bg.png")) return c;
    return "assets";
}

bool Engine::init(const char* title, bool fullscreen, int resW, int resH, const std::string& monitorName) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_HIDDEN);
    InitWindow(VW, VH, title);
    if (!IsWindowReady()) return false;
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);  // Esc is "back" in this game
    InitAudioDevice();

    // Pick the monitor explicitly. The window is created wherever the OS puts it
    // (on multi-monitor X11 that is often the left-most screen, not the primary),
    // so never trust the window's initial position. Monitor 0 is the primary.
    monitor_ = 0;
    for (int i = 0; i < GetMonitorCount(); i++)
        if (!monitorName.empty() && this->monitorName(i) == monitorName) monitor_ = i;
    int rate = GetMonitorRefreshRate(monitor_);
    SetTargetFPS(rate > 0 ? rate : 144);

    if (resW <= 0 || resH <= 0) {
        // first launch: the biggest preset that leaves some room on the monitor
        int mw = GetMonitorWidth(monitor_), mh = GetMonitorHeight(monitor_);
        resW = VW, resH = VH;
        for (auto [w, h] : resolutions())
            if (w <= mw * 0.85f && h <= mh * 0.85f) resW = w, resH = h;
    }
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    setDisplay(fullscreen, resW, resH);

    assetDir_ = findAssets();
    TraceLog(LOG_INFO, "PIETER: assets at %s", assetDir_.c_str());

    resizeCanvas();

    // big atlases so text stays sharp at 1440p / 4K
    fontSmall_ = LoadFontEx(assetPath("fonts/NotoSans-Bold.ttf").c_str(), 72, nullptr, 0);
    fontBig_ = LoadFontEx(assetPath("fonts/NotoSans-Black.ttf").c_str(), 160, nullptr, 0);
    for (Font* f : {&fontSmall_, &fontBig_}) {
        GenTextureMipmaps(&f->texture);
        SetTextureFilter(f->texture, TEXTURE_FILTER_TRILINEAR);
    }

    Image icon = LoadImage(assetPath("images/pieter.png").c_str());
    if (icon.data) { ImageResize(&icon, 64, 64); SetWindowIcon(icon); UnloadImage(icon); }

    sfx.init();
    music.init(assetPath("music"), "PieterGame_OST");
    return true;
}

void Engine::shutdown() {
    if (scene_) scene_->exit();
    scene_.reset();
    discord.stop();
    music.shutdown();
    sfx.shutdown();
    for (auto& [k, t] : textures_) UnloadTexture(t);
    textures_.clear();
    UnloadFont(fontSmall_);
    UnloadFont(fontBig_);
    UnloadRenderTexture(canvas_);
    CloseAudioDevice();
    CloseWindow();
}

Font& Engine::font(bool big) { return big ? fontBig_ : fontSmall_; }

Texture2D& Engine::tex(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second;
    Texture2D t = LoadTexture(assetPath("images/" + name + ".png").c_str());
    if (t.id) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    }
    return textures_[name] = t;
}

void Engine::go(SceneFactory next) {
    if (fadeDir_ == -1) return;  // already leaving
    pending_ = std::move(next);
    fadeDir_ = -1;
}

void Engine::shake(float amount) {
    if (screenShake) shake_ = std::max(shake_, amount);
}

void Engine::toast(const std::string& text, Color color) {
    toasts_.push_back({text, color});
    if (toasts_.size() > 4) toasts_.erase(toasts_.begin());
}

bool Engine::fullscreen() const { return IsWindowFullscreen(); }

int Engine::monitor() const { return std::clamp(monitor_, 0, std::max(0, GetMonitorCount() - 1)); }

void Engine::trackMonitor(float dt) {
    // Windowed: if the player drags the window to another screen, the game now
    // lives there. Skipped right after we placed the window ourselves, because
    // window managers apply moves asynchronously and the old position would
    // otherwise be read back.
    placeCooldown_ -= dt;
    monitorPoll_ -= dt;
    if (placeCooldown_ > 0 || monitorPoll_ > 0 || fullscreen()) return;
    monitorPoll_ = 0.5f;
    int cur = GetCurrentMonitor();
    if (cur >= 0 && cur < GetMonitorCount()) monitor_ = cur;
}

std::string Engine::monitorName(int index) const {
    const char* n = (index >= 0 && index < GetMonitorCount()) ? GetMonitorName(index) : nullptr;
    return n ? n : "";
}

void Engine::setMonitor(int index) {
    if (index < 0 || index >= GetMonitorCount()) return;
    monitor_ = index;
    setDisplay(fullscreen(), resW_, resH_);
    int rate = GetMonitorRefreshRate(index);
    SetTargetFPS(rate > 0 ? rate : 144);
}

std::vector<std::pair<int, int>> Engine::resolutions() const {
    static const std::pair<int, int> presets[] = {{1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080},
                                                  {2560, 1440}, {3200, 1800}, {3840, 2160}};
    int mon = monitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    std::vector<std::pair<int, int>> out;
    for (auto p : presets)
        if (p.first <= mw && p.second <= mh) out.push_back(p);
    if (out.empty()) out.push_back(presets[0]);
    if (mw > 0 && mh > 0 && out.back() != std::pair{mw, mh}) out.push_back({mw, mh});  // native
    return out;
}

void Engine::setDisplay(bool fs, int w, int h) {
    resW_ = w;
    resH_ = h;
    // Fullscreen is a real fullscreen request (EWMH / xdg fullscreen state), so
    // compositors put the game above taskbars and panels. raylib 6 keeps the
    // monitor's current video mode, so no mode switch happens; resolution only
    // changes the size of the canvas we render into.
    placeCooldown_ = 1.5f;
    if (fs && fullscreen()) {
        SetWindowMonitor(monitor_);  // already fullscreen: just make sure it's on the right screen
        return;
    }
    if (fullscreen()) ToggleFullscreen();  // back to a normal window first
    int mw = GetMonitorWidth(monitor_), mh = GetMonitorHeight(monitor_);
    Vector2 mp = GetMonitorPosition(monitor_);
    int ww = std::min(w, mw), wh = std::min(h, mh);
    SetWindowSize(ww, wh);
    SetWindowPosition((int)mp.x + (mw - ww) / 2, (int)mp.y + (mh - wh) / 2);
    if (fs) {
        // raylib picks the monitor from the window position, which the window
        // manager may not have applied yet; SetWindowMonitor moves it to ours.
        ToggleFullscreen();
        SetWindowMonitor(monitor_);
    }
}

void Engine::resizeCanvas() {
    // render at the real output size (or the chosen fullscreen resolution), 16:9
    int bw = GetRenderWidth(), bh = GetRenderHeight();
    if (fullscreen() && resW_ > 0) bw = resW_, bh = resH_;
    int cw = std::min(bw, bh * VW / VH), ch = cw * VH / VW;
    cw = std::max(cw, 320);
    ch = std::max(ch, 180);
    if (canvas_.id && canvas_.texture.width == cw && canvas_.texture.height == ch) return;
    if (canvas_.id) UnloadRenderTexture(canvas_);
    canvas_ = LoadRenderTexture(cw, ch);
    SetTextureFilter(canvas_.texture, TEXTURE_FILTER_BILINEAR);
    canvasScale_ = (float)cw / VW;
}

void Engine::beginClip(Rectangle r) {
    BeginScissorMode((int)(r.x * canvasScale_), (int)(r.y * canvasScale_), (int)std::ceil(r.width * canvasScale_),
                     (int)std::ceil(r.height * canvasScale_));
}

void Engine::updateCanvasTransform() {
    resizeCanvas();
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    scale_ = std::min(sw / VW, sh / VH);
    offset_ = {(sw - VW * scale_) / 2, (sh - VH * scale_) / 2};
    Vector2 m = GetMousePosition();
    mouse_ = {(m.x - offset_.x) / scale_, (m.y - offset_.y) / scale_};
}

void Engine::drawToasts() {
    // stacked under the wallet in the top-right corner, sliding in from the side
    float y = 96;
    for (auto& t : toasts_) {
        float in = easeOutCubic(t.t / 0.35f);
        float out = clamp01((t.life - t.t) / 0.4f);
        float a = std::min(1.0f, in) * out;
        float w = ui::measure(t.text, 20) + 36;
        Rectangle r{VW - 20 - w + (1 - in) * (w + 40), y, w, 38};
        DrawRectangleRounded(r, 0.5f, 12, alpha(col::bgDeep, 0.92f * a));
        DrawRectangleRoundedLinesEx(r, 0.5f, 12, 2, alpha(t.color, a));
        ui::text(t.text, r.x + w / 2, r.y + 19, 20, alpha(t.color, a), ui::Center);
        y += 46 * out;
    }
}

void Engine::run(std::unique_ptr<Scene> first) {
    scene_ = std::move(first);
    sessionStart_ = (long long)std::time(nullptr);
    music.setMode(scene_->music());
    scene_->enter();
    fade_ = 1;
    fadeDir_ = 1;
    int lastTrack = -1;

    while (!WindowShouldClose() && !quit_) {
        float dt = std::min(GetFrameTime(), 1.0f / 20.0f);
        time_ += dt;
        updateCanvasTransform();
        mousePressed_ = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        clickConsumed_ = fadeDir_ != 0;  // no clicking through transitions

        // global hotkeys
        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)))
            setFullscreen(!fullscreen());
        if (IsKeyPressed(KEY_F9)) music.next();

        music.update(dt);
        trackMonitor(dt);
        presenceT_ -= dt;
        if (discord.running() && presenceT_ <= 0 && fadeDir_ == 0) {
            presenceT_ = 2;
            Presence p = scene_->presence();
            if (presenceHook) presenceHook(p);
            p.startTimestamp = sessionStart_;
            discord.setPresence(p);
        }
        if (music.currentTrack() != lastTrack) {
            lastTrack = music.currentTrack();
            if (lastTrack >= 0) toast("Now playing: " + music.nowPlaying(), col::cyan);
        }

        // transitions
        if (fadeDir_ == -1) {
            fade_ += dt * 4.5f;
            if (fade_ >= 1) {
                fade_ = 1;
                scene_->exit();
                scene_ = pending_();
                pending_ = nullptr;
                ui::resetHover();
                music.setMode(scene_->music());
                scene_->enter();
                fadeDir_ = 1;
            }
        } else if (fadeDir_ == 1) {
            fade_ -= dt * 4.0f;
            if (fade_ <= 0) { fade_ = 0; fadeDir_ = 0; }
        }

        for (auto& t : toasts_) t.t += dt;
        std::erase_if(toasts_, [](auto& t) { return t.t > t.life; });
        shake_ = std::max(0.0f, shake_ - dt * 40);
        flash_ = std::max(0.0f, flash_ - dt * 2.5f);

        scene_->update(dt);

        BeginTextureMode(canvas_);
        ClearBackground(col::bg);
        rlPushMatrix();
        rlScalef(canvasScale_, canvasScale_, 1);  // scenes draw in 1280x720 virtual units
        scene_->draw();
        if (flash_ > 0) DrawRectangle(0, 0, VW, VH, alpha(flashColor_, flash_));
        drawToasts();
        if (fade_ > 0) DrawRectangle(0, 0, VW, VH, alpha(BLACK, easeInOutSine(fade_)));
        rlPopMatrix();
        EndTextureMode();

        if (!shotPath_.empty() && --shotFrames_ <= 0) {
            Image img = LoadImageFromTexture(canvas_.texture);
            ImageFlipVertical(&img);
            ExportImage(img, shotPath_.c_str());
            UnloadImage(img);
            quit_ = true;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        Vector2 sh{shake_ > 0 ? rnd(-shake_, shake_) * scale_ : 0, shake_ > 0 ? rnd(-shake_, shake_) * scale_ : 0};
        DrawTexturePro(canvas_.texture, {0, 0, (float)canvas_.texture.width, -(float)canvas_.texture.height},
                       {offset_.x + sh.x, offset_.y + sh.y, VW * scale_, VH * scale_}, {0, 0}, 0, WHITE);
        EndDrawing();
    }
}

}  // namespace pg
