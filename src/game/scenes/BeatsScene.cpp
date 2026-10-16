// PIETER BEATS: a 4-lane rhythm game on the Pieter songs. Charts are generated
// from the audio (game/BeatChart), cached on disk, and played on a perspective
// highway with a hype crew, JUHUH MODE and a grade at the end.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/BeatChart.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace pg {
namespace {

using beats::Analysis;
using beats::Difficulty;
using beats::Note;

const Color kLaneCol[4] = {rgba(0xff6fb5ff), rgba(0x3cd6ffff), rgba(0xffc531ff), rgba(0x3ddc84ff)};
const char* kKeyLabel[4] = {"D", "F", "J", "K"};
const int kKeys[4][2] = {{KEY_D, KEY_LEFT}, {KEY_F, KEY_DOWN}, {KEY_J, KEY_UP}, {KEY_K, KEY_RIGHT}};
const char* kDiffName[3] = {"EASY", "NORMAL", "HARD"};
const Color kDiffCol[3] = {rgba(0x3ddc84ff), rgba(0xffc531ff), rgba(0xff4a5aff)};

// timing windows (seconds either side)
constexpr float kPerfect = 0.045f, kGreat = 0.090f, kGood = 0.135f;
constexpr float kLeadIn = 2.6f;

// highway geometry: u = 1 at the horizon, 0 at the hit line
constexpr float kHitY = 610, kVY = -68.4f, kLaneW = 132, kCX = 640;

enum class Phase { Select, Loading, Play, Paused, Results };
enum class Judge { Perfect, Great, Good, Miss };

struct LiveNote {
    Note n;
    bool hit = false, missed = false, holding = false, broken = false, done = false;
    float tickT = 0;
};

// ------------------------------------------------------------------ analysis cache

std::string stemOf(const std::string& path) { return fs::path(path).stem().string(); }

fs::path cacheFile(const std::string& path) {
    std::string name = stemOf(path);
    for (auto& c : name)
        if (!std::isalnum((unsigned char)c)) c = '_';
    return fs::path(P().path()).parent_path() / "beats" / (name + ".pgb");
}

constexpr uint32_t kCacheVersion = 2;

bool loadCache(const std::string& path, Analysis& a) {
    std::ifstream f(cacheFile(path), std::ios::binary);
    if (!f) return false;
    uint32_t ver = 0, frames = 0;
    f.read((char*)&ver, 4);
    if (ver != kCacheVersion) return false;
    f.read((char*)&a.duration, 4);
    f.read((char*)&a.bpm, 4);
    f.read((char*)&a.firstBeat, 4);
    f.read((char*)&a.hop, 4);
    f.read((char*)&frames, 4);
    if (!f || frames == 0 || frames > 2000000) return false;
    for (auto* env : {&a.onset, &a.centroid, &a.energy, &a.bass}) {
        std::vector<uint8_t> q(frames);
        f.read((char*)q.data(), frames);
        env->resize(frames);
        for (uint32_t i = 0; i < frames; i++) (*env)[i] = q[i] / 255.0f * 1.5f;
    }
    a.ok = (bool)f;
    return a.ok;
}

void saveCache(const std::string& path, const Analysis& a) {
    std::error_code ec;
    fs::create_directories(cacheFile(path).parent_path(), ec);
    std::ofstream f(cacheFile(path), std::ios::binary);
    if (!f) return;
    uint32_t frames = (uint32_t)a.onset.size();
    f.write((const char*)&kCacheVersion, 4);
    f.write((const char*)&a.duration, 4);
    f.write((const char*)&a.bpm, 4);
    f.write((const char*)&a.firstBeat, 4);
    f.write((const char*)&a.hop, 4);
    f.write((const char*)&frames, 4);
    for (auto* env : {&a.onset, &a.centroid, &a.energy, &a.bass}) {
        std::vector<uint8_t> q(frames);
        for (uint32_t i = 0; i < frames; i++) q[i] = (uint8_t)std::clamp((*env)[i] / 1.5f * 255.0f + 0.5f, 0.0f, 255.0f);
        f.write((const char*)q.data(), frames);
    }
}

// Songs are analyzed once per install; this keeps them in memory for the session
// and runs the work on one background thread (the selected song jumps the queue).
class Analyzer {
public:
    static Analyzer& get() {
        static Analyzer* a = new Analyzer;  // never destroyed: the detached worker may still be running at exit
        return *a;
    }
    std::shared_ptr<const Analysis> find(const std::string& path) {
        std::lock_guard<std::mutex> l(m_);
        auto it = done_.find(path);
        if (it != done_.end()) return it->second;
        Analysis a;
        if (loadCache(path, a)) return done_[path] = std::make_shared<Analysis>(std::move(a));
        return nullptr;
    }
    // queue a song; front = do it next
    void request(const std::string& path, bool front) {
        std::lock_guard<std::mutex> l(m_);
        if (done_.count(path) || path == busy_) return;
        std::erase(queue_, path);
        if (front) queue_.insert(queue_.begin(), path);
        else queue_.push_back(path);
        if (!running_) {
            running_ = true;
            std::thread([this] { work(); }).detach();
        }
    }
    float progress(const std::string& path) {
        std::lock_guard<std::mutex> l(m_);
        return path == busy_ ? progress_.load() : 0.0f;
    }

private:
    void work() {
        for (;;) {
            std::string path;
            {
                std::lock_guard<std::mutex> l(m_);
                if (queue_.empty()) { running_ = false; busy_.clear(); return; }
                path = queue_.front();
                queue_.erase(queue_.begin());
                busy_ = path;
                progress_ = 0;
            }
            Analysis a;
            if (!loadCache(path, a)) {
                a = beats::analyze(path, &progress_);
                if (a.ok) saveCache(path, a);
            }
            std::lock_guard<std::mutex> l(m_);
            done_[path] = std::make_shared<Analysis>(std::move(a));
            busy_.clear();
        }
    }
    std::mutex m_;
    std::map<std::string, std::shared_ptr<const Analysis>> done_;
    std::vector<std::string> queue_;
    std::string busy_;
    std::atomic<float> progress_{0};
    bool running_ = false;
};

std::string bestKey(const std::string& path, int diff) { return stemOf(path) + "|" + kDiffName[diff]; }

const char* gradeOf(float acc, bool failed) {
    if (failed) return "F";
    if (acc >= 0.95f) return "S";
    if (acc >= 0.90f) return "A";
    if (acc >= 0.80f) return "B";
    if (acc >= 0.70f) return "C";
    if (acc >= 0.60f) return "D";
    return "F";
}
Color gradeColor(const char* g) {
    switch (g[0]) {
        case 'S': return col::gold;
        case 'A': return col::green;
        case 'B': return col::cyan;
        case 'C': return col::violet;
        case 'D': return col::orange;
        default: return col::red;
    }
}

Color hueCol(float deg, float s = 0.75f, float v = 1) { return ColorFromHSV(std::fmod(deg + 3600, 360.0f), s, v); }

void tri2(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    rlBegin(RL_TRIANGLES);
    for (int w = 0; w < 2; w++) {
        rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex2f(a.x, a.y);
        Vector2 p = w ? c : b, q = w ? b : c;
        Color cp = w ? cc : cb, cq = w ? cb : cc;
        rlColor4ub(cp.r, cp.g, cp.b, cp.a); rlVertex2f(p.x, p.y);
        rlColor4ub(cq.r, cq.g, cq.b, cq.a); rlVertex2f(q.x, q.y);
    }
    rlEnd();
}
void quad4(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color top, Color bottom) {
    tri2(tl, tr, br, top, top, bottom);
    tri2(tl, br, bl, top, bottom, bottom);
}

// spinning record with a portrait label
void vinyl(Vector2 c, float r, float rot, Color accent, const std::string& face) {
    fx::softShadow({c.x + r * 0.06f, c.y + r * 0.12f}, r * 1.05f, r * 1.05f, 0.5f);
    DrawCircleV(c, r, rgba(0x0e0b14ff));
    for (float k = 0.42f; k < 0.97f; k += 0.06f) DrawRing(c, r * k, r * k + 1, 0, 360, 64, alpha(WHITE, 0.05f));
    // light catching the grooves
    DrawRing(c, r * 0.4f, r * 0.97f, rot * RAD2DEG + 20, rot * RAD2DEG + 50, 24, alpha(WHITE, 0.06f));
    DrawRing(c, r * 0.4f, r * 0.97f, rot * RAD2DEG + 200, rot * RAD2DEG + 230, 24, alpha(WHITE, 0.06f));
    DrawCircleV(c, r * 0.38f, accent);
    art::portrait(face, c, r * 0.33f, darken(accent, 0.4f), rot * RAD2DEG);
    DrawCircleV(c, r * 0.04f, rgba(0x0e0b14ff));
    DrawRing(c, r - 2, r, 0, 360, 64, alpha(WHITE, 0.15f));
}

// ====================================================================== scene

class BeatsScene : public Scene {
public:
    ~BeatsScene() override {
        unloadSong();
        unloadPreview();
    }

    MusicMode music() const override { return MusicMode::None; }  // we play the songs ourselves

    Presence presence() const override {
        if (phase_ == Phase::Select || songIdx_ < 0) return {"Pieter Beats", "Picking a song"};
        std::string song = E().music.trackName(songIdx_);
        if (phase_ == Phase::Results) return {"Pieter Beats: " + std::string(failed_ ? "failed" : gradeOf(accuracy(), false)) + " rank", song};
        return {"Pieter Beats (" + std::string(kDiffName[diff_]) + ")", song + " - " + std::to_string(combo_) + " combo"};
    }

    void enter() override {
        for (int i = 0; i < E().music.trackCount(); i++) songs_.push_back(i);
        // pre-analyze everything in the background so most songs are instant later
        for (int i : songs_) Analyzer::get().request(E().music.trackPath(i), false);
        diff_ = 1;
        // dev hook: PG_BEATS=<song index>[:<difficulty 0-2>], PG_BEATS_AUTO=1 plays itself
        if (const char* b = std::getenv("PG_BEATS")) {
            int s = 0, d = 1;
            std::sscanf(b, "%d:%d", &s, &d);
            sel_ = std::clamp(s, 0, (int)songs_.size() - 1);
            diff_ = std::clamp(d, 0, 2);
            autoplay_ = std::getenv("PG_BEATS_AUTO") != nullptr;
            startSong();
        }
    }

    void exit() override { P().save(); }

    // ============================================================ song lifecycle
    void unloadSong() {
        if (songLoaded_) {
            StopMusicStream(song_);
            UnloadMusicStream(song_);
            songLoaded_ = false;
        }
    }
    void unloadPreview() {
        if (previewLoaded_) {
            StopMusicStream(preview_);
            UnloadMusicStream(preview_);
            previewLoaded_ = false;
        }
        previewIdx_ = -1;
    }

    const std::string& pathOf(int i) const { return E().music.trackPath(songs_[i]); }

    void startSong() {
        unloadPreview();
        songIdx_ = songs_[sel_];
        Analyzer::get().request(pathOf(sel_), true);
        phase_ = Phase::Loading;
        loadT_ = 0;
    }

    void beginPlay(std::shared_ptr<const Analysis> a) {
        an_ = std::move(a);
        notes_.clear();
        for (auto& n : beats::buildChart(*an_, (Difficulty)diff_)) notes_.push_back({n});
        unloadSong();
        song_ = LoadMusicStream(pathOf(sel_).c_str());
        songLoaded_ = song_.stream.buffer != nullptr;
        if (songLoaded_) song_.looping = false;
        songT_ = -kLeadIn;
        started_ = false;
        score_ = 0;
        shownScore_ = 0;
        combo_ = maxCombo_ = 0;
        for (auto& c : counts_) c = 0;
        health_ = 1;
        juhuh_ = 0;
        juhuhT_ = 0;
        failed_ = false;
        failT_ = 0;
        endT_ = 0;
        judgeT_ = 0;
        resumeT_ = 0;
        fx_.clear();
        newBest_ = false;
        coinsWon_ = 0;
        phase_ = Phase::Play;
        E().sfx.play(Sfx::Whoosh, 0.6f);
    }

    float accuracy() const {
        int judged = counts_[0] + counts_[1] + counts_[2] + counts_[3];
        if (judged == 0) return 1;
        return (counts_[0] + counts_[1] * 0.7f + counts_[2] * 0.4f) / judged;
    }
    int multiplier() const { return std::min(4, 1 + combo_ / 10); }

    void finish() {
        phase_ = Phase::Results;
        resultsT_ = 0;
        unloadSong();
        float acc = accuracy();
        float diffMult = diff_ == 0 ? 0.7f : diff_ == 1 ? 1.0f : 1.45f;
        bool fc = !failed_ && counts_[3] == 0 && !notes_.empty();
        if (failed_) coinsWon_ = (counts_[0] + counts_[1]) / 8;
        else coinsWon_ = (long long)(an_->duration / 60 * 45 * acc * acc * diffMult) + (fc ? 30 : 0);
        if (autoplay_) coinsWon_ = 0;
        if (coinsWon_ > 0) P().earn(coinsWon_);
        auto& best = P().beats[bestKey(pathOf(sel_), diff_)];
        if (!failed_ && !autoplay_) {
            newBest_ = score_ > best.score;
            best.score = std::max(best.score, (int)score_);
            best.acc = std::max(best.acc, acc);
            best.fc = best.fc || fc;
        }
        P().save();
        const char* g = gradeOf(acc, failed_);
        if (g[0] == 'S' || g[0] == 'A') {
            E().sfx.play(Sfx::Win);
            fx_.confetti({VW / 2.0f, 200}, 160);
        } else {
            E().sfx.play(failed_ ? Sfx::Lose : Sfx::Perfect);
        }
    }

    // ============================================================ input + judging
    void judge(Judge j, int lane) {
        Color c = j == Judge::Perfect ? col::gold : j == Judge::Great ? col::green : j == Judge::Good ? col::cyan : col::red;
        const char* txt = j == Judge::Perfect ? "PERFECT" : j == Judge::Great ? "GREAT" : j == Judge::Good ? "GOOD" : "MISS";
        judgeText_ = txt;
        judgeCol_ = c;
        judgeT_ = 0.6f;
        counts_[(int)j]++;
        float dmg = diff_ == 0 ? 0.04f : diff_ == 1 ? 0.07f : 0.09f;
        if (j == Judge::Miss) {
            if (combo_ >= 20) {
                E().sfx.play(Sfx::Drop, 0.5f);
                say(pickOf({"NOOO my combo", "lag!!", "who unplugged me", "Davi did that"}));
            }
            combo_ = 0;
            health_ -= dmg;
            missFlash_ = 1;
            pieterHurt_ = 0.5f;
            if (lane >= 0) laneMiss_[lane] = 1;
        } else {
            combo_++;
            maxCombo_ = std::max(maxCombo_, combo_);
            int base = j == Judge::Perfect ? 300 : j == Judge::Great ? 200 : 100;
            score_ += base * multiplier() * (juhuhT_ > 0 ? 2 : 1);
            health_ = std::min(1.0f, health_ + (j == Judge::Perfect ? 0.02f : j == Judge::Great ? 0.012f : 0.004f));
            if (juhuhT_ <= 0) juhuh_ = std::min(1.0f, juhuh_ + (j == Judge::Perfect ? 0.035f : j == Judge::Great ? 0.02f : 0));
            if (combo_ % 50 == 0) {
                say(pickOf({"JUHUH!!!", "PRO SINCE NOW", "LEKKER BEZIG", "EZ CLAP", "Pieter is proud"}));
                E().sfx.play(Sfx::Gem, 0.6f);
                comboPop_ = 1;
            }
        }
    }

    std::string pickOf(std::initializer_list<const char*> l) { return *(l.begin() + rndi(0, (int)l.size() - 1)); }
    void say(const std::string& s) {
        bubble_ = s;
        bubbleT_ = 2.2f;
    }

    void hitEffects(int lane, Judge j) {
        Vector2 p = proj(lane - 1.5f, 0);
        Color c = kLaneCol[lane];
        fx_.burst(p, j == Judge::Perfect ? 18 : 10, c, 380, 5, 0.45f, PShape::Spark);
        if (j == Judge::Perfect) fx_.burst(p, 6, WHITE, 250, 6, 0.4f, PShape::Star);
        shocks_.add(p, 20, j == Judge::Perfect ? 90 : 65, c, 0.35f, 8);
        laneHit_[lane] = 1;
        if (P().beatsHitSounds) E().sfx.play(Sfx::Key, 0.35f, 1.0f + lane * 0.08f, (lane - 1.5f) * 0.3f);
    }

    float judgeTime() const { return songT_ - P().beatsOffsetMs / 1000.0f; }

    void pressLane(int lane) {
        float t = judgeTime();
        LiveNote* best = nullptr;
        for (auto& n : notes_) {
            if (n.n.lane != lane || n.hit || n.missed) continue;
            if (n.n.t - t > kGood) break;
            if (std::abs(n.n.t - t) <= kGood) { best = &n; break; }
        }
        padPress_[lane] = 1;
        if (!best) return;
        float d = std::abs(best->n.t - t);
        Judge j = d <= kPerfect ? Judge::Perfect : d <= kGreat ? Judge::Great : Judge::Good;
        best->hit = true;
        if (best->n.len > 0) best->holding = true;
        else best->done = true;
        judge(j, lane);
        hitEffects(lane, j);
        lateness_ = best->n.t - t;
    }

    bool laneDown(int lane) const {
        if (autoplay_) return autoDown_[lane] > 0;
        return IsKeyDown(kKeys[lane][0]) || IsKeyDown(kKeys[lane][1]);
    }
    bool lanePressed(int lane) const { return IsKeyPressed(kKeys[lane][0]) || IsKeyPressed(kKeys[lane][1]); }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        shocks_.update(dt);
        switch (phase_) {
            case Phase::Select: updateSelect(dt); break;
            case Phase::Loading: updateLoading(dt); break;
            case Phase::Play: updatePlay(dt); break;
            case Phase::Paused:
                if (E().backPressed()) resume();
                break;
            case Phase::Results:
                resultsT_ += dt;
                shownCoins_ = (long long)(coinsWon_ * clamp01((resultsT_ - 1.0f) / 1.2f));
                if (E().backPressed()) toSelect();
                break;
        }
    }

    void toSelect() {
        unloadSong();
        phase_ = Phase::Select;
        previewIdx_ = -1;
    }

    // back from pause: a 2 second count-in before the music continues
    void resume() {
        phase_ = Phase::Play;
        resumeT_ = 2.0f;
        lastResumeTick_ = 3;
    }

    void updateSelect(float dt) {
        int n = (int)songs_.size();
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { sel_ = (sel_ + 1) % n; E().sfx.play(Sfx::Tick, 0.5f); ensureVisible(); }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { sel_ = (sel_ + n - 1) % n; E().sfx.play(Sfx::Tick, 0.5f); ensureVisible(); }
        if (IsKeyPressed(KEY_LEFT)) diff_ = std::max(0, diff_ - 1);
        if (IsKeyPressed(KEY_RIGHT)) diff_ = std::min(2, diff_ + 1);
        if (IsKeyPressed(KEY_ENTER)) { E().sfx.play(Sfx::Power, 0.6f); startSong(); return; }
        float wheel = GetMouseWheelMove();
        if (E().mouse().x < 640) scrollTarget_ = std::clamp(scrollTarget_ - wheel * 70, 0.0f, std::max(0.0f, n * kCardH - kListH));
        scroll_ = damp(scroll_, scrollTarget_, 14, dt);
        Analyzer::get().request(pathOf(sel_), true);

        // song preview: starts a moment after the selection settles, fades in from the chorus-ish part
        if (previewIdx_ != sel_) {
            previewWait_ += dt;
            if (previewLoaded_) SetMusicVolume(preview_, std::max(0.0f, previewVol_ -= dt * 3) * E().music.volume());
            if (previewWait_ > 0.35f) {
                unloadPreview();
                preview_ = LoadMusicStream(pathOf(sel_).c_str());
                previewLoaded_ = preview_.stream.buffer != nullptr;
                previewIdx_ = sel_;
                previewVol_ = 0;
                if (previewLoaded_) {
                    preview_.looping = true;
                    PlayMusicStream(preview_);
                    SeekMusicStream(preview_, GetMusicTimeLength(preview_) * 0.33f);
                    SetMusicVolume(preview_, 0);
                }
            }
        } else {
            previewWait_ = 0;
        }
        if (previewLoaded_) {
            if (previewIdx_ == sel_) previewVol_ = std::min(1.0f, previewVol_ + dt * 0.8f);
            SetMusicVolume(preview_, previewVol_ * E().music.volume() * 0.8f);
            UpdateMusicStream(preview_);
        }
        vinylRot_ += dt * (previewLoaded_ ? 3.5f : 0.6f);
        if (E().backPressed()) E().go(makeMenu);
    }

    void ensureVisible() {
        float y = sel_ * kCardH;
        if (y < scrollTarget_) scrollTarget_ = y;
        if (y + kCardH > scrollTarget_ + kListH) scrollTarget_ = y + kCardH - kListH;
    }

    void updateLoading(float dt) {
        loadT_ += dt;
        vinylRot_ += dt * 6;
        if (auto a = Analyzer::get().find(pathOf(sel_))) {
            if (!a->ok) {
                E().toast("Couldn't read that song", col::red);
                toSelect();
                return;
            }
            if (loadT_ > 0.6f) beginPlay(a);
        }
        if (E().backPressed()) toSelect();
    }

    void updatePlay(float dt) {
        if (resumeT_ > 0) {
            int tick = (int)std::ceil(resumeT_);
            if (tick != lastResumeTick_) {
                lastResumeTick_ = tick;
                E().sfx.play(Sfx::Tick, 0.8f, 1.3f);
            }
            resumeT_ -= dt;
            if (resumeT_ <= 0) {
                resumeT_ = 0;
                E().sfx.play(Sfx::Bell, 0.4f);
                if (songLoaded_ && started_) ResumeMusicStream(song_);
            }
            if (E().backPressed()) { phase_ = Phase::Paused; resumeT_ = 0; }
            return;
        }
        // song clock: run on frame time, gently pulled towards the audio stream's position
        if (failed_) {
            failT_ += dt;
            if (songLoaded_) {
                SetMusicPitch(song_, std::max(0.05f, 1 - failT_ / 1.4f));
                UpdateMusicStream(song_);
            }
            if (failT_ > 1.6f) finish();
            return;
        }
        songT_ += dt;
        if (!started_ && songT_ >= 0) {
            started_ = true;
            if (songLoaded_) {
                SetMusicVolume(song_, E().music.volume());
                SetMusicPitch(song_, 1);
                PlayMusicStream(song_);
            }
            songT_ = 0;
        }
        if (started_ && songLoaded_) {
            UpdateMusicStream(song_);
            float played = GetMusicTimePlayed(song_);
            if (std::abs(songT_ - played) > 0.08f) songT_ = played;
            else songT_ += (played - songT_) * std::min(1.0f, dt * 1.5f);
        }

        // count-in ticks
        int cd = (int)std::ceil(-songT_ / (kLeadIn / 3));
        if (songT_ < 0 && cd != lastCount_ && cd >= 1 && cd <= 3) {
            lastCount_ = cd;
            E().sfx.play(Sfx::Tick, 0.8f, 1.3f);
        }
        if (songT_ >= 0 && lastCount_ != 0) {
            lastCount_ = 0;
            E().sfx.play(Sfx::Bell, 0.5f);
        }

        float t = judgeTime();
        // autoplay bot (dev hook)
        if (autoplay_) {
            for (auto& d : autoDown_) d -= dt;
            for (auto& n : notes_)
                if (!n.hit && !n.missed && std::abs(n.n.t - t) < 0.012f) {
                    autoDown_[n.n.lane] = n.n.len > 0 ? n.n.len + 0.05f : 0.08f;
                    pressLane(n.n.lane);
                }
        } else {
            for (int l = 0; l < 4; l++)
                if (lanePressed(l)) pressLane(l);
        }
        // misses + holds
        for (auto& n : notes_) {
            if (!n.hit && !n.missed && t - n.n.t > kGood) {
                n.missed = true;
                judge(Judge::Miss, n.n.lane);
            }
            if (n.holding) {
                float end = n.n.t + n.n.len;
                if (laneDown(n.n.lane)) {
                    n.tickT += dt;
                    while (n.tickT >= 0.1f) {
                        n.tickT -= 0.1f;
                        score_ += 15 * multiplier() * (juhuhT_ > 0 ? 2 : 1);
                    }
                    laneHit_[n.n.lane] = std::max(laneHit_[n.n.lane], 0.6f);
                    if (rndi(0, 2) == 0) {
                        Particle p;
                        p.pos = proj(n.n.lane - 1.5f, 0) + Vector2{rnd(-14, 14), 0};
                        p.vel = {rnd(-60, 60), rnd(-380, -200)};
                        p.life = p.maxLife = 0.35f;
                        p.size = 4;
                        p.color = kLaneCol[n.n.lane];
                        p.shape = PShape::Spark;
                        fx_.add(p);
                    }
                    if (t >= end) {
                        n.holding = false;
                        n.done = true;
                        score_ += 100 * multiplier();
                        shocks_.add(proj(n.n.lane - 1.5f, 0), 20, 80, WHITE, 0.3f, 6);
                    }
                } else if (t < end - 0.1f) {
                    n.holding = false;
                    n.broken = true;
                } else {
                    n.holding = false;
                    n.done = true;
                }
            }
        }

        // JUHUH MODE
        if (juhuhT_ > 0) {
            juhuhT_ -= dt;
            juhuh_ = std::max(0.0f, juhuhT_ / 8);
            health_ = std::min(1.0f, health_ + dt * 0.02f);
        } else if (juhuh_ >= 1 && (IsKeyPressed(KEY_SPACE) || autoplay_)) {
            juhuhT_ = 8;
            E().sfx.play(Sfx::Neigh);
            E().shake(8);
            say("JUHUH MODE!!!");
            shocks_.add({kCX, 400}, 40, 700, col::gold, 0.8f, 20);
        }

        // decays
        judgeT_ = std::max(0.0f, judgeT_ - dt);
        bubbleT_ = std::max(0.0f, bubbleT_ - dt);
        missFlash_ = std::max(0.0f, missFlash_ - dt * 3);
        pieterHurt_ = std::max(0.0f, pieterHurt_ - dt);
        comboPop_ = std::max(0.0f, comboPop_ - dt * 2);
        for (int l = 0; l < 4; l++) {
            laneHit_[l] = std::max(0.0f, laneHit_[l] - dt * 4);
            laneMiss_[l] = std::max(0.0f, laneMiss_[l] - dt * 3);
            padPress_[l] = std::max(0.0f, padPress_[l] - dt * 6);
            if (laneDown(l)) padPress_[l] = 1;
        }
        shownScore_ += (score_ - shownScore_) * std::min(1.0f, dt * 10);

        if (health_ <= 0 && !autoplay_) {
            health_ = 0;
            failed_ = true;
            failT_ = 0;
            E().sfx.play(Sfx::Lose);
            say("I need a break...");
            return;
        }
        bool allDone = std::all_of(notes_.begin(), notes_.end(), [](auto& n) { return n.missed || n.done || n.broken; });
        bool ended = started_ && (!songLoaded_ || !IsMusicStreamPlaying(song_) || songT_ > an_->duration);
        if ((ended || (allDone && songT_ > an_->duration - 0.5f)) && (endT_ += dt) > 1.2f) finish();

        if (E().backPressed()) {
            phase_ = Phase::Paused;
            if (songLoaded_) PauseMusicStream(song_);
        }
    }

    // ============================================================ highway projection
    float lookahead() const { return 1.55f / P().beatsSpeed; }
    static float depth(float u) { return 1 / (1 + u * 2.6f); }
    static Vector2 proj(float lane, float u) {
        float s = depth(u);
        return {kCX + lane * kLaneW * s, kVY + (kHitY - kVY) * s};
    }
    float beatPhase() const {
        if (!an_) return std::fmod(t_ * 2, 1.0f);
        float b = beats::beatLength(*an_);
        return std::fmod(std::fmod(songT_ - an_->firstBeat, b) / b + 1, 1.0f);
    }
    float pulse() const { return std::pow(1 - beatPhase(), 3.0f); }
    float bass() const { return an_ && songT_ > 0 ? beats::sample(*an_, an_->bass, songT_) : 0; }
    float energy() const { return an_ && songT_ > 0 ? beats::sample(*an_, an_->energy, songT_) : 0.3f; }

    // ============================================================ draw: stage
    void drawStage() {
        bool jm = juhuhT_ > 0;
        fx::blurredBackdrop("menu_bg", 0.62f, t_);
        float e = energy(), p = pulse();
        fx::beginAdd();
        // stage lights sweeping with the music
        for (int i = 0; i < 4; i++) {
            float a = std::sin(t_ * (0.5f + i * 0.13f) + i * 1.7f) * 0.5f;
            Vector2 src{i < 2 ? 80.0f + i * 160 : VW - 80.0f - (i - 2) * 160, -20};
            Vector2 dir = fromAngle(PI / 2 + a + (i < 2 ? -0.35f : 0.35f));
            Color c = jm ? hueCol(t_ * 120 + i * 90) : (i % 2 ? col::pink : col::violet);
            Vector2 end = src + dir * 900, n{-dir.y, dir.x};
            quad4(src - n * 8, src + n * 8, end + n * 140, end - n * 140, alpha(c, 0.10f + 0.12f * e), alpha(c, 0));
        }
        // beat pulse behind the highway
        fx::glow({kCX, 470}, 420 + p * 60, alpha(jm ? hueCol(t_ * 90) : col::purple, 0.22f + 0.25f * bass() * p));
        fx::endAdd();
        fx::bokeh(t_, 14, jm ? col::gold : col::pink, 3);
        if (missFlash_ > 0) DrawRectangle(0, 0, VW, VH, alpha(col::red, 0.12f * missFlash_));
    }

    void drawHighway() {
        bool jm = juhuhT_ > 0;
        float look = lookahead();
        Vector2 tl = proj(-2, 1), tr = proj(2, 1), br = proj(2, -0.08f), bl = proj(-2, -0.08f);
        // floor
        quad4(tl, tr, br, bl, alpha(rgba(0x0c0818ff), 0.25f), alpha(rgba(0x0c0818ff), 0.88f));
        // lane tint + key flashes
        fx::beginAdd();
        for (int l = 0; l < 4; l++) {
            float x0 = l - 2.0f, x1 = l - 1.0f;
            Color c = kLaneCol[l];
            quad4(proj(x0, 1), proj(x1, 1), proj(x1, 0), proj(x0, 0), alpha(c, 0), alpha(c, 0.06f));
            float f = std::max(padPress_[l] * 0.5f, laneHit_[l]);
            if (f > 0) quad4(proj(x0, 0.55f), proj(x1, 0.55f), proj(x1, 0), proj(x0, 0), alpha(c, 0), alpha(c, 0.45f * f));
            if (laneMiss_[l] > 0) quad4(proj(x0, 0.4f), proj(x1, 0.4f), proj(x1, 0), proj(x0, 0), alpha(col::red, 0), alpha(col::red, 0.35f * laneMiss_[l]));
        }
        fx::endAdd();
        // beat lines scroll towards you
        if (an_) {
            float b = beats::beatLength(*an_);
            float first = std::ceil((songT_ - an_->firstBeat) / b);
            for (int k = 0; k < 16; k++) {
                float bt = an_->firstBeat + (first + k) * b;
                float u = (bt - songT_) / look;
                if (u > 1) break;
                if (u < 0) continue;
                bool bar = (int)std::lround((bt - an_->firstBeat) / b) % 4 == 0;
                DrawLineEx(proj(-2, u), proj(2, u), bar ? 3 : 1.5f, alpha(WHITE, (bar ? 0.28f : 0.12f) * (1 - u)));
            }
        }
        // separators + glowing rails
        for (int l = -1; l <= 1; l++) DrawLineEx(proj((float)l, 1), proj((float)l, -0.08f), 1.5f, alpha(WHITE, 0.12f));
        Color railL = jm ? hueCol(t_ * 200) : col::violet, railR = jm ? hueCol(t_ * 200 + 180) : col::pink;
        fx::beginAdd();
        fx::glowLine(tl, bl, 10, alpha(railL, 0.6f + 0.3f * pulse()));
        fx::glowLine(tr, br, 10, alpha(railR, 0.6f + 0.3f * pulse()));
        fx::endAdd();
        DrawLineEx(tl, bl, 3, railL);
        DrawLineEx(tr, br, 3, railR);
        // glowing horizon the notes come out of
        fx::beginAdd();
        fx::glow({kCX, tl.y}, 150 + 30 * pulse(), alpha(jm ? hueCol(t_ * 120) : col::violet, 0.45f));
        fx::glowLine(tl, tr, 6, alpha(WHITE, 0.35f + 0.3f * pulse()));
        fx::endAdd();
    }

    void drawNotes() {
        float t = songT_ - P().beatsOffsetMs / 1000.0f;
        float look = lookahead();
        // hold tails first
        for (auto& n : notes_) {
            if (n.n.len <= 0 || n.done || n.missed) continue;
            float u0 = (n.n.t - t) / look, u1 = (n.n.t + n.n.len - t) / look;
            if (u0 > 1) break;
            if (u1 < 0) continue;
            if (n.holding) u0 = 0;
            u0 = std::max(u0, -0.05f);
            u1 = std::min(u1, 1.0f);
            float lx = n.n.lane - 1.5f, w = 0.17f;
            Color c = n.broken ? rgba(0x5a5470ff) : kLaneCol[n.n.lane];
            fx::beginAdd();
            if (!n.broken) quad4(proj(lx - w * 1.8f, u1), proj(lx + w * 1.8f, u1), proj(lx + w * 1.8f, u0), proj(lx - w * 1.8f, u0), alpha(c, 0.05f), alpha(c, n.holding ? 0.45f : 0.2f));
            fx::endAdd();
            quad4(proj(lx - w, u1), proj(lx + w, u1), proj(lx + w, u0), proj(lx - w, u0), alpha(c, 0.5f), alpha(c, 0.85f));
            DrawCircleV(proj(lx, u1), kLaneW * 0.17f * depth(u1), alpha(c, 0.85f));
        }
        // heads, far to near
        for (int i = (int)notes_.size() - 1; i >= 0; i--) {
            auto& n = notes_[i];
            if (n.hit || n.missed) continue;
            float u = (n.n.t - t) / look;
            if (u > 1.02f || u < -0.12f) continue;
            float s = depth(u);
            Vector2 p = proj(n.n.lane - 1.5f, u);
            float r = kLaneW * 0.32f * s;
            float fade = clamp01((1.02f - u) / 0.12f);
            Color c = juhuhT_ > 0 ? hueCol(t_ * 200 + n.n.lane * 60) : kLaneCol[n.n.lane];
            fx::beginAdd();
            fx::glow(p, r * 2.4f, alpha(c, 0.45f * fade));
            fx::endAdd();
            DrawEllipse((int)p.x, (int)(p.y + r * 0.35f), r * 1.05f, r * 0.6f, alpha(BLACK, 0.4f * fade));
            DrawEllipse((int)p.x, (int)p.y, r + 3, r * 0.72f + 3, alpha(col::outline, fade));
            DrawEllipse((int)p.x, (int)p.y, r, r * 0.72f, alpha(darken(c, 0.25f), fade));
            DrawEllipse((int)p.x, (int)(p.y - r * 0.12f), r * 0.82f, r * 0.5f, alpha(c, fade));
            DrawEllipse((int)(p.x - r * 0.25f), (int)(p.y - r * 0.28f), r * 0.32f, r * 0.15f, alpha(WHITE, 0.75f * fade));
            if (n.n.len > 0) DrawRing({p.x, p.y - r * 0.12f}, r * 0.3f, r * 0.42f, 0, 360, 24, alpha(WHITE, 0.8f * fade));
        }
    }

    void drawReceptors() {
        for (int l = 0; l < 4; l++) {
            Vector2 p = proj(l - 1.5f, 0);
            Color c = kLaneCol[l];
            float pr = padPress_[l];
            float r = kLaneW * 0.36f * (1 - pr * 0.08f);
            fx::beginAdd();
            fx::glow(p, r * 2.2f, alpha(c, 0.15f + 0.4f * laneHit_[l] + 0.1f * pulse()));
            fx::endAdd();
            DrawEllipse((int)p.x, (int)p.y, r + 4, r * 0.72f + 4, col::outline);
            DrawEllipse((int)p.x, (int)p.y, r, r * 0.72f, mix(rgba(0x1a1230ff), darken(c, 0.3f), pr));
            DrawEllipse((int)p.x, (int)p.y, r * 0.78f, r * 0.55f, mix(rgba(0x0c0818ff), c, pr * 0.7f));
            // ring
            for (int k = 0; k < 2; k++) DrawEllipseLines((int)p.x, (int)p.y, r - k, r * 0.72f - k, alpha(c, 0.9f));
            ui::textShadow(kKeyLabel[l], p.x, p.y + 1, 22, pr > 0.5f ? WHITE : alpha(c, 0.9f), ui::Center, true, 2);
        }
    }

    void drawJudgement() {
        if (judgeT_ > 0) {
            float k = 1 - judgeT_ / 0.6f;
            float sc = easeOutBack(clamp01(k / 0.25f)) * (1 + 0.1f * (1 - k));
            float a = clamp01(judgeT_ / 0.2f);
            rlPushMatrix();
            rlTranslatef(kCX, 330 - k * 14, 0);
            rlScalef(sc, sc, 1);
            fx::gradientText(judgeText_, 0, 0, 46, alpha(lighten(judgeCol_, 0.4f), a), alpha(judgeCol_, a), 4);
            rlPopMatrix();
            if (judgeText_ != "MISS" && std::abs(lateness_) > kPerfect * 0.6f)
                ui::text(lateness_ > 0 ? "early" : "late", kCX, 362 - k * 14, 16, alpha(col::muted, a), ui::Center, true);
        }
        if (combo_ >= 5) {
            float sc = 1 + comboPop_ * 0.35f + pulse() * 0.04f;
            rlPushMatrix();
            rlTranslatef(kCX, 230, 0);
            rlScalef(sc, sc, 1);
            ui::textShadow(std::to_string(combo_), 0, 0, 60, alpha(WHITE, 0.85f), ui::Center, true, 4);
            rlPopMatrix();
            ui::text("COMBO", kCX, 268, 16, alpha(col::muted, 0.9f), ui::Center, true);
        }
    }

    void drawCrew() {
        // Pieter hypes, Davi and Kira dance on the off-beat
        float p = pulse(), bp = beatPhase();
        float hurt = pieterHurt_;
        Vector2 pc{170, 360 - p * 14};
        fx::beginAdd();
        fx::glow(pc, 170, alpha(juhuhT_ > 0 ? col::gold : col::pink, 0.22f + 0.2f * p));
        fx::endAdd();
        art::pieter({pc.x + std::sin(t_ * 40) * 6 * hurt, pc.y}, 92, P().hat, std::sin(t_ * 2.6f) * 8 + hurt * 15,
                    hurt > 0 ? mix(WHITE, col::red, hurt * 0.6f) : WHITE);
        // headphones
        DrawRing({pc.x, pc.y - 10}, 96, 108, 200, 340, 24, col::outline);
        for (int s : {-1, 1}) {
            Vector2 cup{pc.x + s * 96, pc.y + 6};
            DrawEllipse((int)cup.x, (int)cup.y, 22, 32, col::outline);
            DrawEllipse((int)cup.x, (int)cup.y, 18, 28, juhuhT_ > 0 ? hueCol(t_ * 120) : col::purple);
        }
        float off = std::pow(std::abs(std::sin((bp + 0.5f) * PI)), 4.0f);
        art::portrait("davi", {90, 560 - off * 16}, 52, col::outline, -std::sin(t_ * 3) * 10);
        art::portrait("kira", {250, 560 - (1 - off) * 16}, 52, col::outline, std::sin(t_ * 3) * 10);
        ui::text("HYPE CREW", 170, 632, 14, col::muted, ui::Center, true);
        if (bubbleT_ > 0) {
            float a = clamp01(bubbleT_ / 0.3f), pop = easeOutBack(clamp01((2.2f - bubbleT_) / 0.25f));
            float w = ui::measure(bubble_, 20, true) + 32;
            Rectangle b{170 - w / 2, 218 - 40 * pop, w, 40};
            DrawRectangleRounded(shrink(b, -3), 0.6f, 10, alpha(col::outline, a));
            DrawRectangleRounded(b, 0.6f, 10, alpha(WHITE, a));
            DrawTriangle({b.x + w / 2 - 10, b.y + 38}, {b.x + w / 2, b.y + 52}, {b.x + w / 2 + 10, b.y + 38}, alpha(WHITE, a));
            ui::text(bubble_, b.x + w / 2, b.y + 20, 20, alpha(col::outline, a), ui::Center, true);
        }
    }

    void drawHud() {
        // song + progress
        std::string song = E().music.trackName(songIdx_);
        float prog = an_ ? clamp01(songT_ / an_->duration) : 0;
        ui::text(song, 24, 26, 22, col::text, ui::Left, true);
        Rectangle dp{24 + ui::measure(song, 22, true) + 12, 15, ui::measure(kDiffName[diff_], 13, true) + 16, 22};
        DrawRectangleRounded(dp, 1, 8, kDiffCol[diff_]);
        ui::text(kDiffName[diff_], dp.x + dp.width / 2, dp.y + 11, 13, col::outline, ui::Center, true);
        DrawRectangleRounded({24, 48, 300, 6}, 1, 6, alpha(WHITE, 0.15f));
        DrawRectangleRounded({24, 48, 300 * prog, 6}, 1, 6, col::pink);

        // score panel
        Rectangle r{VW - 300, 84, 276, 300};
        ui::panel(r, alpha(rgba(0x120c22ff), 0.85f), 0.08f, alpha(col::violet, 0.5f));
        ui::text("SCORE", r.x + 20, r.y + 26, 15, col::muted, ui::Left, true);
        ui::textShadow(fmtNum((long long)std::lround(shownScore_)), r.x + 20, r.y + 60, 40, col::text, ui::Left, true, 3);
        ui::text(TextFormat("%.2f%%", accuracy() * 100), r.x + r.width - 20, r.y + 26, 17, col::gold, ui::Right, true);
        // multiplier ring
        Vector2 mc{r.x + 60, r.y + 140};
        int m = multiplier();
        float toNext = m >= 4 ? 1 : (combo_ % 10) / 10.0f;
        DrawRing(mc, 30, 38, 0, 360, 48, col::bgDeep);
        DrawRing(mc, 30, 38, -90, -90 + 360 * toNext, 48, m >= 4 ? col::gold : col::violet);
        ui::textShadow("x" + std::to_string(m * (juhuhT_ > 0 ? 2 : 1)), mc.x, mc.y, 26, juhuhT_ > 0 ? col::gold : col::text, ui::Center, true, 2);
        ui::text("MAX COMBO", r.x + 120, r.y + 124, 13, col::muted, ui::Left, true);
        ui::text(std::to_string(maxCombo_), r.x + 120, r.y + 150, 26, col::text, ui::Left, true);
        // health
        ui::text("VIBE", r.x + 20, r.y + 200, 14, col::muted, ui::Left, true);
        Rectangle hb{r.x + 70, r.y + 192, r.width - 90, 16};
        DrawRectangleRounded(hb, 1, 8, col::bgDeep);
        Color hc = health_ > 0.5f ? col::green : health_ > 0.25f ? col::gold : col::red;
        if (health_ > 0.01f) DrawRectangleRounded({hb.x, hb.y, hb.width * health_, hb.height}, 1, 8, hc);
        // JUHUH meter
        ui::text("JUHUH", r.x + 20, r.y + 244, 14, col::muted, ui::Left, true);
        Rectangle jb{r.x + 70, r.y + 236, r.width - 90, 16};
        DrawRectangleRounded(jb, 1, 8, col::bgDeep);
        if (juhuh_ > 0.01f) {
            Rectangle f{jb.x, jb.y, jb.width * juhuh_, jb.height};
            DrawRectangleRounded(f, 1, 8, juhuhT_ > 0 ? hueCol(t_ * 240) : col::gold);
        }
        if (juhuh_ >= 1 && juhuhT_ <= 0) {
            float a = 0.6f + 0.4f * std::sin(t_ * 8);
            ui::text("press SPACE!", r.x + r.width / 2, r.y + 276, 18, alpha(col::gold, a), ui::Center, true);
            fx::beginAdd();
            fx::glow(center(jb), 120, alpha(col::gold, 0.25f * a));
            fx::endAdd();
        } else if (juhuhT_ > 0) {
            ui::text("JUHUH MODE  x2", r.x + r.width / 2, r.y + 276, 18, hueCol(t_ * 240), ui::Center, true);
        }
        // judgement tally
        const char* names[] = {"PERFECT", "GREAT", "GOOD", "MISS"};
        Color cs[] = {col::gold, col::green, col::cyan, col::red};
        for (int i = 0; i < 4; i++) {
            float y = r.y + r.height + 26 + i * 26;
            ui::text(names[i], r.x + 20, y, 15, cs[i], ui::Left, true);
            ui::text(std::to_string(counts_[i]), r.x + r.width - 20, y, 15, col::text, ui::Right, true);
        }

        // count-in
        if (songT_ < 0 && !failed_) {
            int cd = (int)std::ceil(-songT_ / (kLeadIn / 3));
            float k = std::fmod(-songT_, kLeadIn / 3) / (kLeadIn / 3);
            float sc = 0.8f + k * 0.6f;
            rlPushMatrix();
            rlTranslatef(kCX, 330, 0);
            rlScalef(sc, sc, 1);
            fx::gradientText(std::to_string(std::clamp(cd, 1, 3)), 0, 0, 120, alpha(WHITE, k), alpha(col::pink, k), 6);
            rlPopMatrix();
            ui::text("D  F  J  K   or arrow keys", kCX, 420, 18, alpha(col::text, 0.8f), ui::Center, true);
        } else if (songT_ < 0.6f && !failed_) {
            float k = songT_ / 0.6f;
            rlPushMatrix();
            rlTranslatef(kCX, 330, 0);
            rlScalef(1 + k * 0.5f, 1 + k * 0.5f, 1);
            fx::gradientText("GO!", 0, 0, 110, alpha(col::gold, 1 - k), alpha(col::orange, 1 - k), 6);
            rlPopMatrix();
        }
        if (resumeT_ > 0) {
            float k = resumeT_ - std::floor(resumeT_);
            float sc = 0.8f + k * 0.6f;
            DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.35f * clamp01(resumeT_)));
            rlPushMatrix();
            rlTranslatef(kCX, 330, 0);
            rlScalef(sc, sc, 1);
            fx::gradientText(std::to_string((int)std::ceil(resumeT_)), 0, 0, 120, alpha(WHITE, 0.4f + k * 0.6f), alpha(col::cyan, 0.4f + k * 0.6f), 6);
            rlPopMatrix();
            ui::text("get ready...", kCX, 420, 18, alpha(col::text, 0.8f), ui::Center, true);
        }
        if (autoplay_) ui::text("AUTOPLAY", kCX, 80, 18, col::red, ui::Center, true);
        if (failed_) {
            float k = clamp01(failT_ / 0.6f);
            DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.4f * k));
            fx::gradientText("FAILED", kCX, 330, 90, alpha(col::red, k), alpha(rgba(0x7a0f1aff), k), 6);
        }
    }

    void drawPlay() {
        drawStage();
        drawCrew();
        drawHighway();
        drawNotes();
        drawReceptors();
        shocks_.draw();
        fx_.draw();
        drawJudgement();
        drawHud();
        art::vignette(0.5f);
    }

    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.7f));
        Rectangle r{VW / 2.0f - 220, 200, 440, 320};
        ui::panel(r, col::panel, 0.1f, col::violet);
        fx::gradientText("PAUSED", VW / 2.0f, r.y + 56, 54, WHITE, col::violet, 4);
        if (ui::button({r.x + 70, r.y + 110, 300, 56}, "RESUME", col::green, 24)) resume();
        if (ui::button({r.x + 70, r.y + 176, 300, 50}, "RESTART", col::purple, 22)) beginPlay(an_);
        if (ui::button({r.x + 70, r.y + 236, 300, 50}, "SONG SELECT", rgba(0x5a2035ff), 22)) toSelect();
    }

    // ============================================================ draw: select
    static constexpr float kCardH = 66, kListH = 560;

    void drawSelect() {
        fx::blurredBackdrop("menu_bg", 0.6f, t_);
        fx::bokeh(t_, 12, col::pink, 5);
        ui::titleBar("PIETER BEATS", col::pink);
        if (ui::backButton()) { E().go(makeMenu); return; }
        ui::coinCounter(VW - 20, 38, P().coins);

        // song list
        Rectangle list{40, 100, 560, kListH};
        ui::panel(shrink(list, -10), alpha(rgba(0x120c22ff), 0.8f), 0.05f, alpha(col::violet, 0.4f));
        E().beginClip(list);
        for (int i = 0; i < (int)songs_.size(); i++) {
            Rectangle c{list.x, list.y + i * kCardH - scroll_, list.width, kCardH - 8};
            if (c.y + c.height < list.y || c.y > list.y + list.height) continue;
            bool sel = i == sel_;
            bool hot = ui::hovered(c) && ui::hovered(list);
            Color hue = hueCol(i * 47.0f, 0.6f, 0.9f);
            DrawRectangleRounded(c, 0.3f, 10, sel ? alpha(hue, 0.35f) : (hot ? alpha(WHITE, 0.08f) : alpha(WHITE, 0.03f)));
            if (sel) DrawRectangleRoundedLinesEx(c, 0.3f, 10, 2.5f, hue);
            // mini record
            Vector2 vc{c.x + 34, c.y + c.height / 2};
            DrawCircleV(vc, 23, rgba(0x0e0b14ff));
            DrawRing(vc, 14, 15, 0, 360, 24, alpha(WHITE, 0.1f));
            DrawCircleV(vc, 9, hue);
            DrawCircleV(vc, 2, rgba(0x0e0b14ff));
            ui::text(E().music.trackName(songs_[i]), c.x + 70, c.y + 21, 20, sel ? WHITE : col::text, ui::Left, true);
            auto a = Analyzer::get().find(pathOf(i));
            std::string sub = a && a->ok ? TextFormat("%d BPM  -  %d:%02d", (int)std::lround(a->bpm), (int)a->duration / 60, (int)a->duration % 60)
                                         : "listening...";
            ui::text(sub, c.x + 70, c.y + 42, 14, col::muted);
            // best grade badge for this difficulty
            auto it = P().beats.find(bestKey(pathOf(i), diff_));
            Vector2 bc{c.x + c.width - 34, c.y + c.height / 2};
            if (it != P().beats.end() && it->second.score > 0) {
                const char* g = gradeOf(it->second.acc, false);
                DrawCircleV(bc, 20, col::outline);
                DrawCircleV(bc, 17, gradeColor(g));
                ui::text(g, bc.x, bc.y, 22, col::outline, ui::Center, true);
                if (it->second.fc) ui::text("FC", bc.x - 34, bc.y, 13, col::gold, ui::Right, true);
            } else {
                ui::text("NEW", bc.x, bc.y, 14, alpha(col::muted, 0.7f), ui::Center, true);
            }
            if (hot && E().mousePressed()) {
                E().consumeClick();
                if (sel_ == i && GetTime() - lastClick_ < 0.4) { startSong(); E().sfx.play(Sfx::Power, 0.6f); }
                else E().sfx.play(Sfx::Tick, 0.6f);
                sel_ = i;
                lastClick_ = GetTime();
            }
        }
        E().endClip();
        // scrollbar
        float total = songs_.size() * kCardH;
        if (total > kListH) {
            float h = kListH * kListH / total, y = list.y + (kListH - h) * (scroll_ / (total - kListH));
            DrawRectangleRounded({list.x + list.width + 2, y, 5, h}, 1, 6, alpha(col::violet, 0.6f));
        }

        // detail panel
        Rectangle d{640, 90, 610, 580};
        ui::panel(d, alpha(rgba(0x120c22ff), 0.85f), 0.06f, alpha(col::pink, 0.5f));
        Color hue = hueCol(sel_ * 47.0f, 0.6f, 0.9f);
        Vector2 vc{d.x + 150, d.y + 165};
        fx::beginAdd();
        fx::glow(vc, 210, alpha(hue, 0.3f));
        fx::endAdd();
        const char* faces[] = {"pieter", "davi", "kira", "horse"};
        vinyl(vc, 118, vinylRot_, hue, faces[sel_ % 4]);
        // tone arm
        Vector2 pivot{vc.x + 140, vc.y - 110};
        float arm = previewLoaded_ ? 0.0f : -0.35f;
        Vector2 tip = pivot + Vector2Rotate({-40, 150}, arm);
        DrawCircleV(pivot, 14, rgba(0x8a8498ff));
        DrawLineEx(pivot, tip, 7, rgba(0xc8c0d8ff));
        DrawRectangleRounded({tip.x - 9, tip.y - 6, 18, 24}, 0.4f, 6, rgba(0x8a8498ff));

        float tx = d.x + 300;
        std::string name = E().music.trackName(songs_[sel_]);
        ui::paragraph(name, {tx, d.y + 52, d.width - 320, 0}, 30, WHITE);
        auto a = Analyzer::get().find(pathOf(sel_));
        if (a && a->ok) {
            ui::text(TextFormat("%d BPM", (int)std::lround(a->bpm)), tx, d.y + 150, 18, col::gold, ui::Left, true);
            ui::text(TextFormat("%d:%02d", (int)a->duration / 60, (int)a->duration % 60), tx + 110, d.y + 150, 18, col::muted, ui::Left, true);
            int notes = (int)beatsCountCache(*a);
            ui::text(TextFormat("%d notes", notes), tx + 190, d.y + 150, 18, col::muted, ui::Left, true);
        } else {
            float pr = Analyzer::get().progress(pathOf(sel_));
            ui::text("Pieter is listening to this one...", tx, d.y + 150, 16, col::muted);
            DrawRectangleRounded({tx, d.y + 168, 220, 6}, 1, 6, alpha(WHITE, 0.12f));
            DrawRectangleRounded({tx, d.y + 168, 220 * pr, 6}, 1, 6, col::pink);
        }
        // previews equalizer
        for (int i = 0; i < 16; i++) {
            float h = previewLoaded_ ? (8 + 22 * std::abs(std::sin(t_ * (3 + i * 0.7f) + i))) * previewVol_ : 3;
            DrawRectangleRounded({tx + i * 13.0f, d.y + 230 - h, 9, h}, 0.5f, 4, alpha(hueCol(i * 20 + t_ * 60), 0.85f));
        }

        // difficulty
        float y = d.y + 268;
        ui::text("DIFFICULTY", d.x + 30, y, 15, col::muted, ui::Left, true);
        for (int i = 0; i < 3; i++) {
            Rectangle b{d.x + 30 + i * 186.0f, y + 16, 176, 52};
            if (ui::button(b, kDiffName[i], diff_ == i ? kDiffCol[i] : col::panelHi, 22)) diff_ = i;
            if (diff_ == i) DrawRectangleRoundedLinesEx(shrink(b, -4), 0.3f, 10, 2, kDiffCol[i]);
        }
        auto it = P().beats.find(bestKey(pathOf(sel_), diff_));
        y += 88;
        if (it != P().beats.end() && it->second.score > 0) {
            const char* g = gradeOf(it->second.acc, false);
            ui::text("BEST", d.x + 30, y, 15, col::muted, ui::Left, true);
            ui::text(fmtNum(it->second.score), d.x + 90, y, 22, col::text, ui::Left, true);
            ui::text(TextFormat("%.2f%%", it->second.acc * 100), d.x + 250, y, 18, col::gold, ui::Left, true);
            ui::text(g, d.x + 360, y, 26, gradeColor(g), ui::Left, true);
            if (it->second.fc) ui::text("FULL COMBO", d.x + 400, y, 15, col::gold, ui::Left, true);
        } else {
            ui::text("No score yet. Go set one!", d.x + 30, y, 17, col::muted, ui::Left, true);
        }

        // settings row
        y += 30;
        auto stepper = [&](float x, const char* label, const std::string& value, auto onMinus, auto onPlus) {
            ui::text(label, x, y + 20, 14, col::muted, ui::Left, true);
            float lx = x + ui::measure(label, 14, true) + 12;
            if (ui::button({lx, y + 2, 36, 36}, "-", col::panelHi, 20)) onMinus();
            ui::text(value, lx + 80, y + 20, 18, col::text, ui::Center, true);
            if (ui::button({lx + 124, y + 2, 36, 36}, "+", col::panelHi, 20)) onPlus();
        };
        stepper(d.x + 30, "SPEED", TextFormat("%.1fx", P().beatsSpeed), [] { P().beatsSpeed = std::max(0.5f, P().beatsSpeed - 0.1f); },
                [] { P().beatsSpeed = std::min(2.5f, P().beatsSpeed + 0.1f); });
        stepper(d.x + 270, "OFFSET", TextFormat("%+dms", P().beatsOffsetMs), [] { P().beatsOffsetMs = std::max(-200, P().beatsOffsetMs - 5); },
                [] { P().beatsOffsetMs = std::min(200, P().beatsOffsetMs + 5); });
        y += 48;
        bool hs = P().beatsHitSounds;
        ui::toggle({d.x + 30, y, 260, 40}, "Hit sounds", P().beatsHitSounds);
        if (hs != P().beatsHitSounds) P().save();
        ui::text("tip: notes late? raise OFFSET", d.x + 310, y + 20, 14, col::muted, ui::Left);

        if (ui::bigButton({d.x + 30, d.y + d.height - 74, d.width - 60, 60}, "PLAY", "D F J K  /  arrows   -   SPACE = JUHUH MODE", col::pink, 36))
            startSong();
    }

    // note count for the detail panel (cached per song + difficulty)
    size_t beatsCountCache(const Analysis& a) {
        std::string key = pathOf(sel_) + "|" + std::to_string(diff_);
        auto it = noteCounts_.find(key);
        if (it != noteCounts_.end()) return it->second;
        return noteCounts_[key] = beats::buildChart(a, (Difficulty)diff_).size();
    }

    void drawLoading() {
        fx::blurredBackdrop("menu_bg", 0.7f, t_);
        Color hue = hueCol(sel_ * 47.0f, 0.6f, 0.9f);
        Vector2 vc{VW / 2.0f, 300};
        fx::beginAdd();
        fx::glow(vc, 260, alpha(hue, 0.35f));
        fx::endAdd();
        const char* faces[] = {"pieter", "davi", "kira", "horse"};
        vinyl(vc, 150, vinylRot_, hue, faces[sel_ % 4]);
        std::string name = E().music.trackName(songs_[sel_]);
        fx::gradientText(name, VW / 2.0f, 500, 40, WHITE, hue, 4);
        float pr = Analyzer::get().find(pathOf(sel_)) ? 1.0f : Analyzer::get().progress(pathOf(sel_));
        ui::text(pr >= 1 ? "Let's go!" : "Pieter is listening to the song and writing the chart...", VW / 2.0f, 548, 18, col::muted, ui::Center, true);
        DrawRectangleRounded({VW / 2.0f - 200, 576, 400, 10}, 1, 8, alpha(WHITE, 0.12f));
        DrawRectangleRounded({VW / 2.0f - 200, 576, 400 * pr, 10}, 1, 8, col::pink);
        ui::text(TextFormat("%s  -  first time only, next time it's instant", kDiffName[diff_]), VW / 2.0f, 610, 14, alpha(col::muted, 0.7f), ui::Center);
    }

    // ============================================================ draw: results
    void drawResults() {
        drawStage();
        art::vignette(0.5f);
        float k = easeOutBack(clamp01(resultsT_ / 0.5f));
        Rectangle r{VW / 2.0f - 360, 90 + (1 - k) * 60, 720, 570};
        ui::panel(r, alpha(rgba(0x120c22ff), 0.94f), 0.06f, alpha(col::pink, 0.6f));
        std::string name = E().music.trackName(songIdx_);
        ui::text(name, r.x + r.width / 2, r.y + 36, 26, WHITE, ui::Center, true);
        Rectangle dp{r.x + r.width / 2 - 50, r.y + 58, 100, 22};
        DrawRectangleRounded(dp, 1, 8, kDiffCol[diff_]);
        ui::text(kDiffName[diff_], dp.x + 50, dp.y + 11, 13, col::outline, ui::Center, true);

        float acc = accuracy();
        const char* g = gradeOf(acc, failed_);
        Color gc = gradeColor(g);
        Vector2 gpos{r.x + 170, r.y + 230};
        float gk = easeOutBack(clamp01((resultsT_ - 0.4f) / 0.5f));
        fx::beginAdd();
        fx::glow(gpos, 200 * gk, alpha(gc, 0.4f));
        fx::endAdd();
        rlPushMatrix();
        rlTranslatef(gpos.x, gpos.y, 0);
        rlScalef(gk, gk, 1);
        rlRotatef(-8 + std::sin(t_ * 2) * 2, 0, 0, 1);
        fx::gradientText(g, 0, 0, 190, lighten(gc, 0.5f), gc, 8);
        rlPopMatrix();
        if (failed_) ui::text("FAILED", gpos.x, gpos.y + 110, 22, col::red, ui::Center, true);
        else if (counts_[3] == 0) ui::text("FULL COMBO!", gpos.x, gpos.y + 110, 22, col::gold, ui::Center, true);

        float x = r.x + 340, y = r.y + 120;
        ui::text("SCORE", x, y, 15, col::muted, ui::Left, true);
        ui::textShadow(fmtNum(score_), x, y + 34, 44, WHITE, ui::Left, true, 3);
        if (newBest_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW BEST!", x + 280, y + 2, 18, col::gold, ui::Left, true);
        y += 84;
        ui::text(TextFormat("ACCURACY  %.2f%%", acc * 100), x, y, 20, col::gold, ui::Left, true);
        ui::text(TextFormat("MAX COMBO  %d", maxCombo_), x, y + 30, 20, col::text, ui::Left, true);
        const char* names[] = {"PERFECT", "GREAT", "GOOD", "MISS"};
        Color cs[] = {col::gold, col::green, col::cyan, col::red};
        int total = std::max(1, (int)notes_.size());
        for (int i = 0; i < 4; i++) {
            float yy = y + 76 + i * 30;
            ui::text(names[i], x, yy, 16, cs[i], ui::Left, true);
            Rectangle bar{x + 100, yy - 8, 200, 16};
            DrawRectangleRounded(bar, 1, 8, col::bgDeep);
            float f = (float)counts_[i] / total * clamp01((resultsT_ - 0.3f - i * 0.1f) / 0.6f);
            if (f > 0.005f) DrawRectangleRounded({bar.x, bar.y, std::max(bar.height, bar.width * f), bar.height}, 1, 8, cs[i]);
            ui::text(std::to_string(counts_[i]), x + 320, yy, 16, col::text, ui::Left, true);
        }
        // coins
        Rectangle cr{r.x + 40, r.y + 400, r.width - 80, 56};
        DrawRectangleRounded(cr, 0.4f, 10, alpha(col::gold, 0.12f));
        ui::coin({cr.x + 36, cr.y + 28}, 16);
        ui::text("+" + fmtNum(shownCoins_) + " Pietercoins", cr.x + 64, cr.y + 28, 24, col::gold, ui::Left, true);
        ui::text(failed_ ? "keep practising!" : g[0] == 'S' ? "Pieter is speechless" : g[0] == 'A' ? "Lekker gespeeld!" : "Not bad. Davi did worse.",
                 cr.x + cr.width - 20, cr.y + 28, 16, col::muted, ui::Right);

        float bw = 200, bx = r.x + r.width / 2 - (bw * 3 + 24) / 2, by = r.y + r.height - 86;
        if (ui::button({bx, by, bw, 60}, "RETRY", col::green, 24)) { songIdx_ = songs_[sel_]; if (an_) beginPlay(an_); }
        if (ui::button({bx + bw + 12, by, bw, 60}, "SONGS", col::purple, 24)) toSelect();
        if (ui::button({bx + 2 * (bw + 12), by, bw, 60}, "MENU", rgba(0x5a2035ff), 24)) E().go(makeMenu);
        shocks_.draw();
        fx_.draw();
    }

    void draw() override {
        switch (phase_) {
            case Phase::Select: drawSelect(); break;
            case Phase::Loading: drawLoading(); break;
            case Phase::Play: drawPlay(); break;
            case Phase::Paused: drawPlay(); drawPause(); break;
            case Phase::Results: drawResults(); break;
        }
    }

private:
    Phase phase_ = Phase::Select;
    std::vector<int> songs_;
    int sel_ = 0, diff_ = 1, songIdx_ = -1;
    float scroll_ = 0, scrollTarget_ = 0, vinylRot_ = 0, loadT_ = 0, t_ = 0;
    double lastClick_ = 0;
    // preview
    Music preview_{};
    bool previewLoaded_ = false;
    int previewIdx_ = -1;
    float previewWait_ = 0, previewVol_ = 0;
    std::map<std::string, size_t> noteCounts_;
    // song
    std::shared_ptr<const Analysis> an_;
    std::vector<LiveNote> notes_;
    Music song_{};
    bool songLoaded_ = false, started_ = false, failed_ = false, autoplay_ = false, newBest_ = false;
    float songT_ = 0, failT_ = 0, endT_ = 0, resultsT_ = 0, resumeT_ = 0;
    int lastResumeTick_ = 0;
    int lastCount_ = -1;
    long long score_ = 0, coinsWon_ = 0, shownCoins_ = 0;
    double shownScore_ = 0;
    int combo_ = 0, maxCombo_ = 0, counts_[4]{};
    float health_ = 1, juhuh_ = 0, juhuhT_ = 0;
    // feedback
    std::string judgeText_, bubble_;
    Color judgeCol_ = WHITE;
    float judgeT_ = 0, lateness_ = 0, bubbleT_ = 0, missFlash_ = 0, pieterHurt_ = 0, comboPop_ = 0;
    float laneHit_[4]{}, laneMiss_[4]{}, padPress_[4]{}, autoDown_[4]{};
    Particles fx_;
    fx::Shocks shocks_;
};

}  // namespace

std::unique_ptr<Scene> makeBeats() { return std::make_unique<BeatsScene>(); }

}  // namespace pg
