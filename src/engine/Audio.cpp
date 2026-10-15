#include "engine/Audio.hpp"

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
    if (modeSet_ && m == mode_) return;
    modeSet_ = true;
    mode_ = m;
    if (m == MusicMode::Title) { pending_ = title_ >= 0 ? title_ : pickShuffle(); pendingLoop_ = true; }
    else { pending_ = pickShuffle(); pendingLoop_ = false; }
    fadeOut_ = loaded_ ? 0.6f : 0.0f;
}

void Jukebox::next() {
    pending_ = pickShuffle();
    pendingLoop_ = false;
    mode_ = MusicMode::Shuffle;
    fadeOut_ = loaded_ ? 0.4f : 0.0f;
}

void Jukebox::playTrack(int index) {
    pending_ = index;
    pendingLoop_ = false;
    mode_ = MusicMode::Shuffle;
    fadeOut_ = loaded_ ? 0.3f : 0.0f;
}

void Jukebox::update(float dt) {
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

// ====================================================================== Synth

namespace {

enum class Osc { Sine, Square, Saw, Triangle, Noise };

struct Note {
    float start = 0;      // seconds into the sound
    float dur = 0.1f;
    float f0 = 440, f1 = 440;  // frequency sweep (exponential)
    Osc wave = Osc::Square;
    float vol = 0.5f;
    float attack = 0.005f;
    float vibDepth = 0, vibRate = 0;  // vibrato in semitones / Hz
    float duty = 0.5f;
};

constexpr int kRate = 44100;

Sound render(std::initializer_list<Note> notes) {
    float total = 0;
    for (auto& n : notes) total = std::max(total, n.start + n.dur);
    int frames = (int)(total * kRate) + 64;
    std::vector<float> buf(frames, 0.0f);

    uint32_t noiseState = 0x12345u;
    for (auto& n : notes) {
        int s0 = (int)(n.start * kRate);
        int len = (int)(n.dur * kRate);
        double phase = 0;
        float noiseVal = 0;
        double noisePhase = 0;
        for (int i = 0; i < len && s0 + i < frames; i++) {
            float t = (float)i / len;
            float f = n.f0 * std::pow(n.f1 / n.f0, t);
            if (n.vibDepth > 0) f *= std::pow(2.0f, n.vibDepth * std::sin(2 * PI * n.vibRate * i / kRate) / 12.0f);
            phase += f / kRate;
            phase -= std::floor(phase);
            float p = (float)phase, v = 0;
            switch (n.wave) {
                case Osc::Sine: v = std::sin(2 * PI * p); break;
                case Osc::Square: v = p < n.duty ? 0.6f : -0.6f; break;
                case Osc::Saw: v = (2 * p - 1) * 0.6f; break;
                case Osc::Triangle: v = (p < 0.5f ? 4 * p - 1 : 3 - 4 * p); break;
                case Osc::Noise:
                    noisePhase += f / kRate;
                    if (noisePhase >= 1) {
                        noisePhase -= std::floor(noisePhase);
                        noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                        noiseVal = (noiseState & 0xffff) / 32768.0f - 1.0f;
                    }
                    v = noiseVal * 0.7f;
                    break;
            }
            float tsec = (float)i / kRate;
            float env = tsec < n.attack ? tsec / n.attack : 1.0f;
            env *= std::pow(1.0f - t, 1.6f);
            buf[s0 + i] += v * env * n.vol;
        }
    }
    std::vector<short> pcm(frames);
    for (int i = 0; i < frames; i++) pcm[i] = (short)(std::clamp(buf[i], -1.0f, 1.0f) * 32000);

    Wave w{};
    w.frameCount = (unsigned)frames;
    w.sampleRate = kRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = pcm.data();
    return LoadSoundFromWave(w);  // copies the samples
}


Sound makeSfx(Sfx s) {
    using O = Osc;
    switch (s) {
        case Sfx::Click: return render({{0, 0.06f, 900, 600, O::Square, 0.35f, 0.001f, 0, 0, 0.25f}});
        case Sfx::Hover: return render({{0, 0.035f, 1400, 1500, O::Triangle, 0.18f}});
        case Sfx::Coin: return render({{0, 0.06f, 988, 988, O::Square, 0.3f, 0.001f, 0, 0, 0.25f},
                                       {0.055f, 0.22f, 1319, 1319, O::Square, 0.3f, 0.001f, 0, 0, 0.25f}});
        case Sfx::Hurt: return render({{0, 0.28f, 420, 70, O::Saw, 0.5f}, {0, 0.18f, 3000, 400, O::Noise, 0.35f}});
        case Sfx::Power: return render({{0, 0.08f, 523, 523, O::Square, 0.3f}, {0.07f, 0.08f, 659, 659, O::Square, 0.3f},
                                        {0.14f, 0.08f, 784, 784, O::Square, 0.3f}, {0.21f, 0.25f, 1047, 1047, O::Square, 0.3f, 0.005f, 0.3f, 12}});
        case Sfx::Win: return render({{0, 0.12f, 523, 523, O::Square, 0.3f}, {0.12f, 0.12f, 659, 659, O::Square, 0.3f},
                                      {0.24f, 0.12f, 784, 784, O::Square, 0.3f}, {0.36f, 0.5f, 1047, 1047, O::Square, 0.3f, 0.005f, 0.4f, 7},
                                      {0.36f, 0.5f, 523, 523, O::Triangle, 0.3f}});
        case Sfx::Lose: return render({{0, 0.2f, 392, 392, O::Square, 0.3f}, {0.2f, 0.2f, 370, 370, O::Square, 0.3f},
                                       {0.4f, 0.2f, 349, 349, O::Square, 0.3f}, {0.6f, 0.7f, 330, 300, O::Square, 0.3f, 0.005f, 0.5f, 6}});
        case Sfx::Dash: return render({{0, 0.16f, 2500, 300, O::Noise, 0.35f}, {0, 0.12f, 300, 900, O::Sine, 0.25f}});
        case Sfx::Boom: return render({{0, 0.6f, 900, 40, O::Noise, 0.7f}, {0, 0.4f, 120, 30, O::Sine, 0.6f}});
        case Sfx::Card: return render({{0, 0.07f, 5000, 1500, O::Noise, 0.3f}});
        case Sfx::Tick: return render({{0, 0.025f, 2400, 2000, O::Square, 0.2f, 0.001f, 0, 0, 0.2f}});
        case Sfx::Gem: return render({{0, 0.1f, 1568, 1568, O::Sine, 0.35f}, {0.06f, 0.25f, 2093, 2093, O::Sine, 0.3f},
                                      {0.06f, 0.25f, 2637, 2637, O::Triangle, 0.12f}});
        case Sfx::Neigh: return render({{0, 0.9f, 700, 380, O::Saw, 0.32f, 0.03f, 1.8f, 16}, {0.02f, 0.85f, 1400, 760, O::Square, 0.12f, 0.03f, 1.8f, 16, 0.3f}});
        case Sfx::Chew: return render({{0, 0.07f, 600, 300, O::Noise, 0.3f}, {0.13f, 0.07f, 600, 300, O::Noise, 0.3f},
                                       {0.26f, 0.08f, 600, 250, O::Noise, 0.3f}});
        case Sfx::Bubble: return render({{0, 0.09f, 300, 1200, O::Sine, 0.35f}});
        case Sfx::Whoosh: return render({{0, 0.35f, 400, 3000, O::Noise, 0.25f, 0.15f}});
        case Sfx::Drop: return render({{0, 0.12f, 220, 90, O::Triangle, 0.5f}, {0, 0.06f, 1800, 600, O::Noise, 0.25f}});
        case Sfx::Perfect: return render({{0, 0.09f, 1047, 1047, O::Triangle, 0.35f}, {0.08f, 0.09f, 1319, 1319, O::Triangle, 0.35f},
                                          {0.16f, 0.25f, 1568, 1568, O::Triangle, 0.35f}});
        case Sfx::Jump: return render({{0, 0.18f, 300, 800, O::Square, 0.28f, 0.002f, 0, 0, 0.3f}});
        case Sfx::Bell: return render({{0, 0.8f, 1760, 1760, O::Sine, 0.3f}, {0, 0.6f, 2640, 2640, O::Sine, 0.12f},
                                       {0, 0.9f, 880, 880, O::Sine, 0.2f}});
        case Sfx::Spawn: return render({{0, 0.15f, 200, 500, O::Triangle, 0.2f}});
        case Sfx::Shoot: return render({{0, 0.09f, 1200, 300, O::Square, 0.18f, 0.001f, 0, 0, 0.3f}});
        case Sfx::Error: return render({{0, 0.12f, 180, 160, O::Square, 0.3f}, {0.13f, 0.18f, 150, 140, O::Square, 0.3f}});
        case Sfx::Kiss: return render({{0, 0.12f, 900, 2200, O::Sine, 0.35f}, {0.1f, 0.05f, 4000, 1000, O::Noise, 0.15f}});
        case Sfx::Count: break;
    }
    return render({{0, 0.01f}});
}

}  // namespace

void SoundBank::init() {
    for (int i = 0; i < (int)Sfx::Count; i++) {
        auto& slot = slots_[i];
        slot.base = makeSfx((Sfx)i);
        for (auto& a : slot.alias) a = LoadSoundAlias(slot.base);
    }
    ready_ = true;
}

void SoundBank::shutdown() {
    if (!ready_) return;
    for (auto& slot : slots_) {
        for (auto& a : slot.alias) UnloadSoundAlias(a);
        UnloadSound(slot.base);
    }
    ready_ = false;
}

void SoundBank::play(Sfx s, float volume, float pitch) {
    if (!ready_ || volume_ <= 0) return;
    auto& slot = slots_[(int)s];
    Sound& snd = slot.alias[slot.next];
    slot.next = (slot.next + 1) % kVoices;
    SetSoundVolume(snd, volume * volume_);
    SetSoundPitch(snd, pitch);
    PlaySound(snd);
}

}  // namespace pg
