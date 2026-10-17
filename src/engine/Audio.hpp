// Music jukebox (streams the Pieter songs) and a tiny synthesizer that
// generates every sound effect at startup, so no SFX files are needed.
#pragma once

#include "engine/Core.hpp"
#include "engine/Synth.hpp"

#include <atomic>
#include <thread>

#include <array>
#include <string>
#include <vector>

namespace pg {

enum class MusicMode { Title, Shuffle, None };  // None = paused (e.g. a video with its own audio)

class Jukebox {
public:
    void init(const std::string& musicDir, const std::string& titleTrack);
    void shutdown();
    void update(float dt);

    void setMode(MusicMode m);        // switches only if the mode actually changes
    // Player-initiated: after either of these, scenes no longer switch the music.
    void next();                      // skip to another random track
    void playTrack(int index);        // jump to a specific track (settings jukebox)

    void setVolume(float v) { volume_ = v; }
    float volume() const { return volume_; }

    int trackCount() const { return (int)tracks_.size(); }
    const std::string& trackName(int i) const { return tracks_[i].name; }
    const std::string& trackPath(int i) const { return tracks_[i].path; }
    int currentTrack() const { return current_; }
    std::string nowPlaying() const;
    float progress() const;           // 0..1 through the current song
    MusicMode mode() const { return mode_; }

private:
    struct Track { std::string path, name; };
    void start(int index, bool loop);
    int pickShuffle();

    std::vector<Track> tracks_;
    std::vector<int> bag_;            // shuffle bag so every song plays before repeats
    int title_ = -1;
    int current_ = -1;
    Music music_{};
    bool loaded_ = false;
    MusicMode mode_ = MusicMode::Title;
    bool modeSet_ = false;
    bool userChoice_ = false;
    bool paused_ = false;
    float volume_ = 0.6f;
    float fade_ = 1.0f;               // fade-in multiplier
    int pending_ = -1;                // track waiting for the fade-out to finish
    bool pendingLoop_ = false;
    float fadeOut_ = 0.0f;
};

enum class Sfx {
    Click, Hover, Coin, Hurt, Power, Win, Lose, Dash, Boom, Card, Tick, Gem,
    Neigh, Chew, Bubble, Whoosh, Drop, Perfect, Jump, Bell, Spawn, Shoot, Error, Kiss,
    Pop, Snore, Water, Yawn, Boop, Gulp, Buzz, Msg, Send, Key,
    Breath, Giggle, Heart, Step, Slam, Flash, Scream, Static, MusicBox, Drone, Creak,
    Klomp, FietsBel, Carillon, Rain, Wind, Thunder, Whisper, Knock, Monkey, Cymbal, Tinnitus, Scratch, Riser, Sting,
    Count
};

// Every effect is synthesized at startup (see Synth.cpp) in a few takes; each
// play picks a take, so rapid repeats don't sound like a machine gun.
class SoundBank {
public:
    void init();      // starts rendering in the background
    void poll();      // uploads the rendered sounds once they're done
    void shutdown();
    // pan: -1 left .. 1 right
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
    void playAt(Sfx s, float x, float volume = 1.0f, float pitch = 1.0f);  // pans by screen x (virtual units)
    void stop(Sfx s);  // cuts every playing copy of a sound (long ambient loops when leaving a scene)
    void setVolume(float v) { volume_ = v; }
    float volume() const { return volume_; }

private:
    static constexpr int kVoices = 4;  // per take
    struct Slot {
        std::array<Sound, synth::kVariants> base{};
        std::array<std::array<Sound, kVoices>, synth::kVariants> alias{};
        int next = 0, lastVariant = -1;
        double lastPlay = -1;
    };
    std::array<Slot, (size_t)Sfx::Count> slots_{};
    std::array<std::array<synth::Pcm, synth::kVariants>, (size_t)Sfx::Count> pcm_{};
    std::thread worker_;
    std::atomic<bool> rendered_{false};
    float volume_ = 0.7f;
    bool ready_ = false;
};

}  // namespace pg
