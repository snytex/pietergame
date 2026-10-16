#include "engine/Audio.hpp"

#include "engine/Synth.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace pg {

// ====================================================================== Jukebox

static std::string prettyName(std::string s) {
    auto cut = [&](const std::string& what) {
        auto p = s.find(what);
        if (p != std::string::npos) s.erase(p, what.size());
    };
    cut(".mp3");
    cut("-audio");
    cut(" (1)");
    std::replace(s.begin(), s.end(), '_', ' ');
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

void Jukebox::init(const std::string& musicDir, const std::string& titleTrack) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(musicDir, ec)) {
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".mp3" && ext != ".ogg" && ext != ".wav") continue;
        tracks_.push_back({e.path().string(), prettyName(e.path().filename().string())});
    }
    std::sort(tracks_.begin(), tracks_.end(), [](auto& a, auto& b) { return a.name < b.name; });
    for (int i = 0; i < (int)tracks_.size(); i++)
        if (fs::path(tracks_[i].path).stem().string() == titleTrack) title_ = i;
    TraceLog(LOG_INFO, "JUKEBOX: %d tracks, title track index %d", (int)tracks_.size(), title_);
}

void Jukebox::shutdown() {
    if (loaded_) { StopMusicStream(music_); UnloadMusicStream(music_); loaded_ = false; }
}

void Jukebox::start(int index, bool loop) {
    if (index < 0 || index >= (int)tracks_.size()) return;
    shutdown();
    music_ = LoadMusicStream(tracks_[index].path.c_str());
    if (music_.stream.buffer == nullptr) { TraceLog(LOG_WARNING, "JUKEBOX: failed %s", tracks_[index].path.c_str()); return; }
    music_.looping = loop;
    loaded_ = true;
    current_ = index;
    fade_ = 0.0f;
    SetMusicVolume(music_, 0);
    PlayMusicStream(music_);
}

int Jukebox::pickShuffle() {
    if (tracks_.empty()) return -1;
    if (bag_.empty()) {
        for (int i = 0; i < (int)tracks_.size(); i++)
            if (i != title_ || tracks_.size() == 1) bag_.push_back(i);
        std::shuffle(bag_.begin(), bag_.end(), rng());
        // never play the same song twice in a row across bag refills
        if (bag_.size() > 1 && bag_.back() == current_) std::swap(bag_.front(), bag_.back());
    }
    int i = bag_.back();
    bag_.pop_back();
    return i;
}

void Jukebox::setMode(MusicMode m) {
    // Silence (a video playing): pause, and resume afterwards where we were.
    if (m == MusicMode::None) {
        paused_ = true;
        if (loaded_) PauseMusicStream(music_);
        return;
    }
    if (paused_) {
        paused_ = false;
        if (loaded_) ResumeMusicStream(music_);
    }
    // Once the player picked music themselves, scenes stop overriding it:
    // their choice keeps playing everywhere and the shuffle continues after it.
    if (userChoice_ && modeSet_) return;
    if (modeSet_ && m == mode_) return;
    modeSet_ = true;
    mode_ = m;
    if (m == MusicMode::Title) { pending_ = title_ >= 0 ? title_ : pickShuffle(); pendingLoop_ = true; }
    else { pending_ = pickShuffle(); pendingLoop_ = false; }
    fadeOut_ = loaded_ ? 0.6f : 0.0f;
}

void Jukebox::next() {
    userChoice_ = true;
    pending_ = pickShuffle();
    pendingLoop_ = false;
    mode_ = MusicMode::Shuffle;
    fadeOut_ = loaded_ ? 0.4f : 0.0f;
}

void Jukebox::playTrack(int index) {
    userChoice_ = true;
    pending_ = index;
    pendingLoop_ = false;
    mode_ = MusicMode::Shuffle;
    fadeOut_ = loaded_ ? 0.3f : 0.0f;
}

void Jukebox::update(float dt) {
    if (paused_) return;
    if (pending_ >= 0) {
        if (fadeOut_ > 0) {
            fadeOut_ -= dt;
            if (loaded_) SetMusicVolume(music_, volume_ * std::max(0.0f, fadeOut_ / 0.6f) * fade_);
            if (loaded_) UpdateMusicStream(music_);
            if (fadeOut_ > 0) return;
        }
        int p = pending_;
        pending_ = -1;
        start(p, pendingLoop_);
    }
    if (!loaded_) return;
    UpdateMusicStream(music_);
    fade_ = std::min(1.0f, fade_ + dt * 1.5f);
    SetMusicVolume(music_, volume_ * fade_);
    // Shuffle mode: when a song ends, roll the next one.
    if (!music_.looping && !IsMusicStreamPlaying(music_)) start(pickShuffle(), false);
}

std::string Jukebox::nowPlaying() const {
    if (current_ < 0) return "";
    return tracks_[current_].name;
}

float Jukebox::progress() const {
    if (!loaded_) return 0;
    float len = GetMusicTimeLength(music_);
    return len > 0 ? GetMusicTimePlayed(music_) / len : 0;
}

// ====================================================================== SoundBank

void SoundBank::init() {
    // Rendering ~100 takes with reverb takes a moment, so do it off the main
    // thread; sounds start playing as soon as the batch is uploaded.
    worker_ = std::thread([this] {
        const int jobs = (int)Sfx::Count * synth::kVariants;
        std::atomic<int> next{0};
        auto work = [&] {
            for (int j; (j = next++) < jobs;) pcm_[j / synth::kVariants][j % synth::kVariants] = synth::render((Sfx)(j / synth::kVariants), j % synth::kVariants);
        };
        std::vector<std::thread> pool;
        int n = std::clamp((int)std::thread::hardware_concurrency() - 1, 1, 8);
        for (int i = 1; i < n; i++) pool.emplace_back(work);
        work();
        for (auto& t : pool) t.join();
        rendered_ = true;
    });
}

void SoundBank::poll() {
    if (ready_ || !rendered_) return;
    worker_.join();
    for (int i = 0; i < (int)Sfx::Count; i++) {
        auto& slot = slots_[i];
        for (int v = 0; v < synth::kVariants; v++) {
            auto& pcm = pcm_[i][v];
            std::vector<short> s16(pcm.data.size());
            for (size_t k = 0; k < s16.size(); k++) s16[k] = (short)(std::clamp(pcm.data[k], -1.0f, 1.0f) * 32000);
            Wave w{};
            w.frameCount = (unsigned)pcm.frames();
            w.sampleRate = synth::kRate;
            w.sampleSize = 16;
            w.channels = 2;
            w.data = s16.data();
            slot.base[v] = LoadSoundFromWave(w);  // copies the samples
            for (auto& a : slot.alias[v]) a = LoadSoundAlias(slot.base[v]);
            pcm = {};
        }
    }
    ready_ = true;
}

void SoundBank::shutdown() {
    if (worker_.joinable()) worker_.join();
    if (!ready_) return;
    for (auto& slot : slots_)
        for (int v = 0; v < synth::kVariants; v++) {
            for (auto& a : slot.alias[v]) UnloadSoundAlias(a);
            UnloadSound(slot.base[v]);
        }
    ready_ = false;
}

void SoundBank::play(Sfx s, float volume, float pitch, float pan) {
    poll();
    if (!ready_ || volume_ <= 0) return;
    auto& slot = slots_[(int)s];
    // a different take than last time, a hair of pitch drift, and ducking when
    // the same sound is retriggered within a few frames (no phasing pile-ups)
    int v = rndi(0, synth::kVariants - 1);
    if (v == slot.lastVariant) v = (v + 1) % synth::kVariants;
    slot.lastVariant = v;
    double now = GetTime();
    if (now - slot.lastPlay < 0.035) volume *= 0.55f;
    slot.lastPlay = now;
    Sound& snd = slot.alias[v][slot.next];
    slot.next = (slot.next + 1) % kVoices;
    SetSoundVolume(snd, volume * volume_);
    SetSoundPitch(snd, pitch * rnd(0.975f, 1.025f));
    SetSoundPan(snd, std::clamp(pan, -1.0f, 1.0f));
    PlaySound(snd);
}

void SoundBank::playAt(Sfx s, float x, float volume, float pitch) { play(s, volume, pitch, (x / VW * 2 - 1) * 0.6f); }

}  // namespace pg
