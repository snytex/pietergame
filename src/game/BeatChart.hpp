// Pieter Beats charts: notes are generated from the song itself. The audio is
// decoded, onsets are found with a band-split spectral flux, the tempo and beat
// phase come from the onset autocorrelation, and onsets are snapped to the beat
// grid with a density that depends on the difficulty.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace pg::beats {

enum class Difficulty { Easy, Normal, Hard };
constexpr int kLanes = 4;

struct Note {
    float t = 0;     // seconds into the song
    int lane = 0;
    float len = 0;   // > 0 = hold note
};

// Everything learned from the audio once; charts for each difficulty are cheap to build from it.
struct Analysis {
    bool ok = false;
    float duration = 0;
    float bpm = 120;
    float firstBeat = 0;               // time of a beat (phase of the grid)
    float hop = 0;                     // seconds per envelope frame
    std::vector<float> onset;          // onset strength per frame (0..~1)
    std::vector<float> centroid;       // brightness per frame (0..1), drives lane choice
    std::vector<float> energy;         // loudness per frame (0..1), drives visuals
    std::vector<float> bass;           // low band energy per frame (0..1), drives the beat pulse
};

// Slow (decodes the whole file): run it on a worker thread. `progress` goes 0..1.
Analysis analyze(const std::string& path, std::atomic<float>* progress = nullptr);
std::vector<Note> buildChart(const Analysis& a, Difficulty d);

float beatLength(const Analysis& a);
// Sample an envelope at time t (linear interpolation).
float sample(const Analysis& a, const std::vector<float>& env, float t);

}  // namespace pg::beats
