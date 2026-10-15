// Music jukebox (streams the Pieter songs) and a tiny synthesizer that
// generates every sound effect at startup, so no SFX files are needed.
#pragma once

#include "engine/Core.hpp"

#include <array>
#include <string>
#include <vector>

namespace pg {

enum class MusicMode { Title, Shuffle };

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
    float volume_ = 0.6f;
    float fade_ = 1.0f;               // fade-in multiplier
    int pending_ = -1;                // track waiting for the fade-out to finish
    bool pendingLoop_ = false;
    float fadeOut_ = 0.0f;
};

enum class Sfx {
    Click, Hover, Coin, Hurt, Power, Win, Lose, Dash, Boom, Card, Tick, Gem,
    Neigh, Chew, Bubble, Whoosh, Drop, Perfect, Jump, Bell, Spawn, Shoot, Error, Kiss,
    Count
};

class SoundBank {
public:
    void init();
    void shutdown();
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
    void setVolume(float v) { volume_ = v; }
    float volume() const { return volume_; }

private:
    static constexpr int kVoices = 4;
    struct Slot { Sound base{}; std::array<Sound, kVoices> alias{}; int next = 0; };
    std::array<Slot, (size_t)Sfx::Count> slots_{};
    float volume_ = 0.7f;
    bool ready_ = false;
};

}  // namespace pg
