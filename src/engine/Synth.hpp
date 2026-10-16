// Offline sound-effect synthesizer. Every SFX in the game is designed here as
// layers of oscillators, filters, envelopes and effects (reverb, echo), and
// rendered once at startup into stereo PCM. No SFX files ship with the game.
#pragma once

#include <cstdint>
#include <vector>

namespace pg {

enum class Sfx;

namespace synth {

constexpr int kRate = 44100;
constexpr int kVariants = 3;  // slightly different takes of each sound, so repeats don't sound robotic

// Interleaved stereo float samples.
struct Pcm {
    std::vector<float> data;
    int frames() const { return (int)data.size() / 2; }
};

Pcm render(Sfx s, int variant);
const char* name(Sfx s);

}  // namespace synth
}  // namespace pg
