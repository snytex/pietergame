#include "game/BeatChart.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace pg::beats {
namespace {

constexpr int kRate = 22050;
constexpr int kN = 1024;   // FFT size
constexpr int kHop = 256;  // ~11.6 ms per frame
constexpr float kPi = 3.14159265358979f;

void fft(std::vector<std::complex<float>>& a) {
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2 * kPi / len;
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int k = 0; k < len / 2; k++) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    size_t k = std::min(v.size() - 1, (size_t)(p * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

void normalize(std::vector<float>& v, float p) {
    float top = std::max(1e-6f, percentile(v, p));
    for (auto& x : v) x = std::min(1.5f, x / top);
}

// tiny deterministic rng so a song always gets the same chart
struct Rng {
    uint32_t s;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xffffff) / 16777216.0f; }
};

}  // namespace

float beatLength(const Analysis& a) { return 60.0f / a.bpm; }

float sample(const Analysis& a, const std::vector<float>& env, float t) {
    if (env.empty() || a.hop <= 0) return 0;
    float f = t / a.hop;
    if (f <= 0) return env.front();
    int i = (int)f;
    if (i >= (int)env.size() - 1) return env.back();
    float k = f - i;
    return env[i] * (1 - k) + env[i + 1] * k;
}

Analysis analyze(const std::string& path, std::atomic<float>* progress) {
    Analysis out;
    auto report = [&](float p) { if (progress) progress->store(p); };
    report(0.02f);
    Wave w = LoadWave(path.c_str());
    if (!w.data || w.frameCount == 0) return out;
    WaveFormat(&w, kRate, 32, 1);
    const float* pcm = (const float*)w.data;
    const int n = (int)w.frameCount;
    report(0.25f);

    const int frames = std::max(0, (n - kN) / kHop);
    out.hop = (float)kHop / kRate;
    out.duration = (float)n / kRate;
    std::vector<float> window(kN);
    for (int i = 0; i < kN; i++) window[i] = 0.5f - 0.5f * std::cos(2 * kPi * i / (kN - 1));

    // band edges in FFT bins (bin = f * N / rate)
    auto bin = [](float f) { return std::clamp((int)(f * kN / kRate), 1, kN / 2); };
    const int lowA = bin(30), lowB = bin(160), midB = bin(2200), highB = bin(9000);

    std::vector<float> prev(kN / 2 + 1, 0), mag(kN / 2 + 1);
    std::vector<std::complex<float>> buf(kN);
    std::vector<float> fluxLow(frames), fluxMid(frames), fluxHigh(frames);
    out.centroid.resize(frames);
    out.energy.resize(frames);
    out.bass.resize(frames);
    for (int f = 0; f < frames; f++) {
        const float* x = pcm + f * kHop;
        float rms = 0;
        for (int i = 0; i < kN; i++) {
            buf[i] = {x[i] * window[i], 0};
            rms += x[i] * x[i];
        }
        fft(buf);
        float cNum = 0, cDen = 0, bassE = 0;
        for (int k = 1; k <= kN / 2; k++) {
            mag[k] = std::log1p(100 * std::abs(buf[k]));
            float d = std::max(0.0f, mag[k] - prev[k]);
            if (k >= lowA && k < lowB) { fluxLow[f] += d; bassE += mag[k]; }
            else if (k >= lowB && k < midB) fluxMid[f] += d;
            else if (k >= midB && k < highB) fluxHigh[f] += d;
            if (k < highB) { cNum += k * mag[k]; cDen += mag[k]; }
        }
        std::swap(prev, mag);
        out.centroid[f] = cDen > 0 ? cNum / cDen / midB : 0;
        out.energy[f] = std::sqrt(rms / kN);
        out.bass[f] = bassE / std::max(1, lowB - lowA);
        if ((f & 255) == 0) report(0.25f + 0.55f * f / std::max(1, frames));
    }
    UnloadWave(w);
    if (frames < 100) return out;

    // onset strength: each band normalized so kicks, snares and hats all count
    normalize(fluxLow, 0.97f);
    normalize(fluxMid, 0.97f);
    normalize(fluxHigh, 0.97f);
    std::vector<float> on(frames);
    for (int f = 0; f < frames; f++) on[f] = fluxLow[f] * 1.2f + fluxMid[f] + fluxHigh[f] * 0.6f;
    // remove the slowly varying part (a loud chorus shouldn't be all notes)
    std::vector<float> ps(frames + 1, 0);
    for (int f = 0; f < frames; f++) ps[f + 1] = ps[f] + on[f];
    out.onset.resize(frames);
    const int W = 20;
    for (int f = 0; f < frames; f++) {
        int a = std::max(0, f - W), b = std::min(frames, f + W + 1);
        out.onset[f] = std::max(0.0f, on[f] - (ps[b] - ps[a]) / (b - a));
    }
    normalize(out.onset, 0.985f);
    normalize(out.energy, 0.95f);
    normalize(out.bass, 0.95f);
    float cLo = percentile(out.centroid, 0.05f), cHi = percentile(out.centroid, 0.95f);
    for (auto& c : out.centroid) c = std::clamp((c - cLo) / std::max(1e-4f, cHi - cLo), 0.0f, 1.0f);
    report(0.85f);

    // tempo: autocorrelation of the onset envelope with a prior around 120 BPM
    float bestScore = -1;
    int bestLag = 0;
    std::vector<float> ac;
    const int lagMin = (int)(60.0f / 185 / out.hop), lagMax = (int)(60.0f / 70 / out.hop);
    for (int lag = lagMin; lag <= lagMax; lag++) {
        double s = 0;
        for (int f = 0; f + lag < frames; f++) s += out.onset[f] * out.onset[f + lag];
        float bpm = 60.0f / (lag * out.hop);
        float prior = std::exp(-0.5f * std::pow(std::log2(bpm / 120.0f) / 0.5f, 2.0f));
        float score = (float)s * prior;
        ac.push_back((float)s);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    // parabolic refinement of the peak
    float lag = (float)bestLag;
    int i = bestLag - lagMin;
    if (i > 0 && i + 1 < (int)ac.size()) {
        float a = ac[i - 1], b = ac[i], c = ac[i + 1];
        float d = a - 2 * b + c;
        if (std::abs(d) > 1e-9f) lag += std::clamp(0.5f * (a - c) / d, -0.5f, 0.5f);
    }
    out.bpm = 60.0f / (lag * out.hop);
    while (out.bpm < 80) out.bpm *= 2;
    while (out.bpm > 175) out.bpm /= 2;

    // beat phase: where a grid at this tempo collects the most onset energy
    float period = 60.0f / out.bpm;
    float bestPhase = 0, best = -1;
    for (int p = 0; p < 64; p++) {
        float ph = period * p / 64;
        float s = 0;
        for (float t = ph; t < out.duration; t += period) s += sample(out, out.onset, t) + 0.5f * sample(out, out.bass, t);
        if (s > best) { best = s; bestPhase = ph; }
    }
    out.firstBeat = bestPhase;
    out.ok = true;
    report(1);
    return out;
}

std::vector<Note> buildChart(const Analysis& a, Difficulty d) {
    std::vector<Note> notes;
    if (!a.ok) return notes;
    const float beat = beatLength(a);
    const int sub = d == Difficulty::Easy ? 1 : d == Difficulty::Normal ? 2 : 4;  // grid divisions per beat
    const float density = d == Difficulty::Easy ? 1.25f : d == Difficulty::Normal ? 2.3f : 3.8f;  // notes per second
    const float step = beat / sub;

    // candidate grid points with the strongest onset near them
    struct Cand { float t, s; int idx; };
    std::vector<Cand> cands;
    int idx = 0;
    for (float t = a.firstBeat; t < a.duration - 1.0f; t += step, idx++) {
        if (t < 1.0f) continue;
        float s = 0;
        for (float dt = -0.045f; dt <= 0.045f; dt += a.hop) s = std::max(s, sample(a, a.onset, t + dt));
        // on-beat points get a little bonus, downbeats a bit more
        if (idx % sub == 0) s *= 1.15f;
        if (idx % (sub * 4) == 0) s *= 1.1f;
        if (s > 0.12f) cands.push_back({t, s, idx});
    }
    // keep the strongest ones until the density target is met
    size_t target = (size_t)(density * a.duration);
    std::sort(cands.begin(), cands.end(), [](auto& x, auto& y) { return x.s > y.s; });
    if (cands.size() > target) cands.resize(target);
    std::sort(cands.begin(), cands.end(), [](auto& x, auto& y) { return x.t < y.t; });

    Rng rng{0x9e3779b9u ^ (uint32_t)(a.duration * 1000) ^ (uint32_t)d};
    int last = -1, prevLast = -1;
    float lastT = -10;
    for (size_t k = 0; k < cands.size(); k++) {
        auto& c = cands[k];
        // brighter sound = further right, with some wander so patterns aren't flat
        float bright = sample(a, a.centroid, c.t);
        int lane = std::clamp((int)std::floor(bright * 3.2f + rng.f() * 1.4f - 0.2f), 0, kLanes - 1);
        float gap = c.t - lastT;
        if (lane == last && gap < beat * 0.75f) lane = (lane + 1 + (int)(rng.f() * 3)) % kLanes;  // no fast jacks
        if (lane == prevLast && lane != last && gap < beat * 0.3f && d != Difficulty::Hard) lane = (last + 2) % kLanes;
        Note nt{c.t, lane, 0};
        // hold notes when the next note is far and the sound keeps going
        float next = k + 1 < cands.size() ? cands[k + 1].t : a.duration;
        if (next - c.t >= beat * 1.5f && sample(a, a.energy, c.t + beat * 0.75f) > 0.45f && rng.f() < 0.7f)
            nt.len = std::min(next - c.t - beat * 0.5f, beat * (d == Difficulty::Easy ? 2.0f : 3.0f));
        notes.push_back(nt);
        // chords on big downbeats (normal + hard)
        if (d != Difficulty::Easy && c.idx % (sub * 4) == 0 && c.s > (d == Difficulty::Hard ? 0.8f : 1.05f) && nt.len == 0)
            notes.push_back({c.t, (lane + 2) % kLanes, 0});
        prevLast = last;
        last = lane;
        lastT = c.t;
    }
    std::sort(notes.begin(), notes.end(), [](auto& x, auto& y) { return x.t < y.t || (x.t == y.t && x.lane < y.lane); });
    return notes;
}

}  // namespace pg::beats
