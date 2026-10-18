#include "engine/Synth.hpp"

#include "engine/Audio.hpp"

#include <algorithm>
#include <cmath>

namespace pg::synth {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 2 * kPi;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9e3779b9u) {
        if (!s) s = 1;
    }
    uint32_t u() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return (u() & 0xffffff) / 16777216.0f; }  // 0..1
    float bi() { return f() * 2 - 1; }                      // -1..1
    float range(float a, float b) { return a + (b - a) * f(); }
    float jit(float x, float amt) { return x * (1 + bi() * amt); }
};

float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }

// ------------------------------------------------------------------ patch model

enum class Osc { Sine, Tri, Saw, Square, Noise, Pink, Crush };  // Crush = sample-and-hold noise at f
enum class Filt { None, LP, HP, BP };

struct Layer {
    float at = 0, dur = 0.1f;
    Osc osc = Osc::Sine;
    float f0 = 440, f1 = -1;               // exponential pitch glide (f1 < 0: none)
    float glideCurve = 1;                  // <1 glides early, >1 late
    float dropAmt = 0, dropTime = 0.02f;   // transient pitch drop (kicks, zaps)
    float gain = 0.5f;
    float atk = 0.002f, rel = 0.02f;
    float decay = 0, sus = 0;              // >0: exponential decay towards sus; 0: shaped fade over the note
    float shape = 1.6f;
    float pan0 = 0, pan1 = 0;
    Filt filt = Filt::None;
    float cut0 = 2000, cut1 = -1, q = 0.707f;
    float fmRatio = 0, fmIdx0 = 0, fmIdx1 = -1;
    int voices = 1;
    float detune = 0;                      // unison spread in cents
    float vibDepth = 0, vibRate = 0;       // semitones, Hz
    float tremDepth = 0, tremRate = 0;
    float duty = 0.5f;
    float driveAmt = 0;

    Layer& vol(float g) { gain = g; return *this; }
    Layer& env(float a, float r) { atk = a; rel = r; return *this; }
    Layer& dec(float d, float s = 0) { decay = d; sus = s; return *this; }
    Layer& curve(float c) { shape = c; return *this; }
    Layer& glide(float c) { glideCurve = c; return *this; }
    Layer& drop(float amt, float t) { dropAmt = amt; dropTime = t; return *this; }
    Layer& pan(float a, float b = 9) { pan0 = a; pan1 = b > 1.5f ? a : b; return *this; }
    Layer& lp(float c0, float c1 = -1, float qq = 0.707f) { filt = Filt::LP; cut0 = c0; cut1 = c1; q = qq; return *this; }
    Layer& hp(float c0, float c1 = -1, float qq = 0.707f) { filt = Filt::HP; cut0 = c0; cut1 = c1; q = qq; return *this; }
    Layer& bp(float c0, float c1 = -1, float qq = 1.5f) { filt = Filt::BP; cut0 = c0; cut1 = c1; q = qq; return *this; }
    Layer& fm(float ratio, float i0, float i1 = -1) { fmRatio = ratio; fmIdx0 = i0; fmIdx1 = i1; return *this; }
    Layer& uni(int n, float cents) { voices = n; detune = cents; return *this; }
    Layer& vib(float d, float r) { vibDepth = d; vibRate = r; return *this; }
    Layer& trem(float d, float r) { tremDepth = d; tremRate = r; return *this; }
    Layer& pw(float d) { duty = d; return *this; }
    Layer& drive(float d) { driveAmt = d; return *this; }
};

Layer L(float at, float dur, Osc o, float f0 = 440, float f1 = -1) {
    Layer l;
    l.at = at;
    l.dur = dur;
    l.osc = o;
    l.f0 = f0;
    l.f1 = f1;
    return l;
}

struct Patch {
    std::vector<Layer> layers;
    float level = 0.7f;                                // output peak
    float verbWet = 0, room = 0.5f, damp = 0.4f;      // reverb
    float echoWet = 0, echoTime = 0.12f, echoFb = 0.35f;  // ping-pong delay

    Patch& add(const Layer& l) { layers.push_back(l); return *this; }
    Patch& out(float lvl) { level = lvl; return *this; }
    Patch& verb(float wet, float size = 0.5f, float dmp = 0.4f) { verbWet = wet; room = size; damp = dmp; return *this; }
    Patch& echo(float wet, float t = 0.12f, float fb = 0.35f) { echoWet = wet; echoTime = t; echoFb = fb; return *this; }
    bool bed = false;  // ambient loop: keep the nominal length, wrap the effect tail onto the start
    Patch& asBed() { bed = true; return *this; }
};

// ------------------------------------------------------------------ DSP

// PolyBLEP residual: removes most aliasing from saw/square edges.
float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

// Zero-delay-feedback state variable filter.
struct Svf {
    float ic1 = 0, ic2 = 0;
    float run(float v0, Filt type, float fc, float q) {
        fc = std::clamp(fc, 20.0f, kRate * 0.45f);
        float g = std::tan(kPi * fc / kRate), k = 1 / q;
        float a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        switch (type) {
            case Filt::LP: return v2;
            case Filt::HP: return v0 - k * v1 - v2;
            case Filt::BP: return k * v1;  // unity gain at the centre
            case Filt::None: break;
        }
        return v0;
    }
};

void renderLayer(const Layer& l, std::vector<float>& out, Rng& rng) {
    const int frames = (int)out.size() / 2;
    const int s0 = (int)(l.at * kRate), n = (int)(l.dur * kRate);
    const int V = std::max(1, l.voices);
    std::vector<double> ph(V), mph(V, 0.0);
    std::vector<float> det(V), vpan(V);
    for (int v = 0; v < V; v++) {
        float spread = V > 1 ? v / (V - 1.0f) * 2 - 1 : 0;
        ph[v] = V > 1 ? rng.f() : 0;
        det[v] = std::pow(2.0f, spread * l.detune / 1200);
        vpan[v] = spread * 0.5f;
    }
    Svf fl, fr;
    float pink[7]{};
    float hold = 0;
    double holdPh = 1;
    const float norm = 1 / std::sqrt((float)V);

    for (int i = 0; i < n && s0 + i < frames; i++) {
        float t = (float)i / n, ts = (float)i / kRate;
        float f = l.f0;
        if (l.f1 > 0) f = l.f0 * std::pow(l.f1 / l.f0, std::pow(t, l.glideCurve));
        if (l.dropAmt > 0) f *= 1 + l.dropAmt * std::exp(-ts / l.dropTime);
        if (l.vibDepth > 0) f *= std::pow(2.0f, l.vibDepth * std::sin(kTau * l.vibRate * ts) / 12);
        float idx = l.fmIdx1 >= 0 ? l.fmIdx0 + (l.fmIdx1 - l.fmIdx0) * t : l.fmIdx0;
        float pan = l.pan0 + (l.pan1 - l.pan0) * t;

        float sl = 0, sr = 0;
        for (int v = 0; v < V; v++) {
            float fv = f * det[v], dt = fv / kRate;
            float p = (float)ph[v], s = 0;
            switch (l.osc) {
                case Osc::Sine:
                    if (l.fmRatio > 0) {
                        mph[v] += fv * l.fmRatio / kRate;
                        mph[v] -= std::floor(mph[v]);
                        s = std::sin(kTau * p + idx * std::sin(kTau * (float)mph[v]));
                    } else {
                        s = std::sin(kTau * p);
                    }
                    break;
                case Osc::Tri: s = p < 0.5f ? 4 * p - 1 : 3 - 4 * p; break;
                case Osc::Saw: s = (2 * p - 1 - blep(p, dt)) * 0.8f; break;
                case Osc::Square: {
                    float p2 = p + 1 - l.duty;
                    p2 -= std::floor(p2);
                    s = ((p < l.duty ? 1.0f : -1.0f) + blep(p, dt) - blep(p2, dt)) * 0.7f;
                    break;
                }
                case Osc::Noise: s = rng.bi(); break;
                case Osc::Pink: {  // Paul Kellet's filter
                    float w = rng.bi();
                    pink[0] = 0.99886f * pink[0] + w * 0.0555179f;
                    pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
                    pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
                    pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
                    pink[4] = 0.55000f * pink[4] + w * 0.5329522f;
                    pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
                    s = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362f) * 0.2f;
                    pink[6] = w * 0.115926f;
                    break;
                }
                case Osc::Crush:
                    holdPh += dt;
                    if (holdPh >= 1) {
                        holdPh -= std::floor(holdPh);
                        hold = rng.bi();
                    }
                    s = hold;
                    break;
            }
            ph[v] += dt;
            ph[v] -= std::floor(ph[v]);
            float a = (std::clamp(pan + vpan[v], -1.0f, 1.0f) + 1) * kPi / 4;  // equal-power pan
            sl += s * std::cos(a) * 1.41421f;
            sr += s * std::sin(a) * 1.41421f;
        }
        sl *= norm;
        sr *= norm;

        if (l.filt != Filt::None) {
            float fc = l.cut1 > 0 ? l.cut0 * std::pow(l.cut1 / l.cut0, t) : l.cut0;
            sl = fl.run(sl, l.filt, fc, l.q);
            sr = fr.run(sr, l.filt, fc, l.q);
        }
        if (l.driveAmt > 0) {
            float d = 1 + l.driveAmt, k = 1 / std::tanh(d);
            sl = std::tanh(sl * d) * k;
            sr = std::tanh(sr * d) * k;
        }

        float e = ts < l.atk ? ts / l.atk : 1.0f;
        if (l.decay > 0) e *= l.sus + (1 - l.sus) * std::exp(-std::max(0.0f, ts - l.atk) / l.decay);
        else e *= std::pow(1 - t, l.shape);
        float remain = l.dur - ts;
        if (remain < l.rel) e *= remain / l.rel;
        if (l.tremDepth > 0) e *= 1 - l.tremDepth * (0.5f + 0.5f * std::sin(kTau * l.tremRate * ts));
        e *= l.gain;

        out[(s0 + i) * 2] += sl * e;
        out[(s0 + i) * 2 + 1] += sr * e;
    }
}

// Freeverb-style stereo reverb (8 damped combs + 4 allpasses per channel).
void reverb(std::vector<float>& buf, float wet, float room, float damp) {
    static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int aps[4] = {556, 441, 341, 225};
    const int frames = (int)buf.size() / 2;
    const float fb = 0.7f + 0.28f * room;
    for (int ch = 0; ch < 2; ch++) {
        int spread = ch * 23;
        std::vector<std::vector<float>> cb(8), ab(4);
        std::vector<int> ci(8, 0), ai(4, 0);
        std::vector<float> store(8, 0);
        for (int c = 0; c < 8; c++) cb[c].assign(combs[c] + spread, 0);
        for (int a = 0; a < 4; a++) ab[a].assign(aps[a] + spread, 0);
        std::vector<float> wetOut(frames);
        for (int i = 0; i < frames; i++) {
            float in = (buf[i * 2] + buf[i * 2 + 1]) * 0.015f;
            float o = 0;
            for (int c = 0; c < 8; c++) {
                float y = cb[c][ci[c]];
                store[c] = y * (1 - damp) + store[c] * damp;
                cb[c][ci[c]] = in + store[c] * fb;
                if (++ci[c] >= (int)cb[c].size()) ci[c] = 0;
                o += y;
            }
            for (int a = 0; a < 4; a++) {
                float y = ab[a][ai[a]];
                float res = -o + y;
                ab[a][ai[a]] = o + y * 0.5f;
                if (++ai[a] >= (int)ab[a].size()) ai[a] = 0;
                o = res;
            }
            wetOut[i] = o;
        }
        for (int i = 0; i < frames; i++) buf[i * 2 + ch] += wetOut[i] * wet * 3;
    }
}

void pingPong(std::vector<float>& buf, float wet, float time, float fb) {
    const int frames = (int)buf.size() / 2, len = std::max(1, (int)(time * kRate));
    std::vector<float> dl(len, 0), dr(len, 0);
    int idx = 0;
    for (int i = 0; i < frames; i++) {
        float yl = dl[idx], yr = dr[idx];
        dl[idx] = (buf[i * 2] + buf[i * 2 + 1]) * 0.5f + yr * fb;
        dr[idx] = yl * fb;
        buf[i * 2] += yl * wet;
        buf[i * 2 + 1] += yr * wet;
        if (++idx >= len) idx = 0;
    }
}

Pcm bounce(const Patch& p, Rng& rng) {
    float end = 0;
    for (auto& l : p.layers) end = std::max(end, l.at + l.dur);
    float tail = 0;
    if (p.verbWet > 0) tail += 0.35f + p.room * 1.6f;
    if (p.echoWet > 0) tail += p.echoTime * std::log(0.003f) / std::log(std::max(0.05f, p.echoFb)) + p.echoTime;
    int frames = (int)((end + tail) * kRate) + 64;
    std::vector<float> buf(frames * 2, 0.0f);
    for (auto& l : p.layers) renderLayer(l, buf, rng);
    if (p.echoWet > 0) pingPong(buf, p.echoWet, p.echoTime, p.echoFb);
    if (p.verbWet > 0) reverb(buf, p.verbWet, p.room, p.damp);
    if (p.bed) {
        int len = (int)(end * kRate);
        for (int i = len; i < frames; i++) {
            buf[(i - len) % len * 2] += buf[i * 2];
            buf[(i - len) % len * 2 + 1] += buf[i * 2 + 1];
        }
        frames = len;
        buf.resize(len * 2);
    }

    // DC blocker, then normalize the peak to the patch level
    float xl = 0, yl = 0, xr = 0, yr = 0, peak = 1e-6f;
    for (int i = 0; i < frames; i++) {
        float l = buf[i * 2], r = buf[i * 2 + 1];
        yl = l - xl + 0.9985f * yl; xl = l;
        yr = r - xr + 0.9985f * yr; xr = r;
        buf[i * 2] = yl;
        buf[i * 2 + 1] = yr;
        peak = std::max({peak, std::abs(yl), std::abs(yr)});
    }
    float g = p.level / peak;
    int last = 0;
    for (int i = 0; i < frames; i++) {
        buf[i * 2] *= g;
        buf[i * 2 + 1] *= g;
        if (std::abs(buf[i * 2]) > 6e-4f || std::abs(buf[i * 2 + 1]) > 6e-4f) last = i;
    }
    // trim the silent tail with a short fade so nothing clicks
    int keep = p.bed ? frames : std::min(frames, last + 256);
    int fade = p.bed ? 0 : std::min(keep, 441);
    for (int i = 0; i < fade; i++) {
        float k = (float)i / fade;
        buf[(keep - 1 - i) * 2] *= k;
        buf[(keep - 1 - i) * 2 + 1] *= k;
    }
    buf.resize(keep * 2);
    return {std::move(buf)};
}

// ------------------------------------------------------------------ the sounds

using O = Osc;

Patch design(Sfx s, Rng& r) {
    Patch p;
    switch (s) {
        case Sfx::Click:  // soft woody tock
            p.add(L(0, 0.05f, O::Sine, r.jit(1900, 0.04f), 850).glide(0.4f).curve(3).vol(0.55f))
             .add(L(0, 0.012f, O::Noise).hp(3200).curve(2).vol(0.25f))
             .add(L(0, 0.06f, O::Tri, 330, 210).curve(3).vol(0.25f))
             .out(0.5f).verb(0.08f, 0.2f);
            break;
        case Sfx::Hover:
            p.add(L(0, 0.03f, O::Sine, r.jit(2700, 0.05f), 2300).curve(3).vol(0.4f))
             .add(L(0, 0.008f, O::Noise).hp(6500).vol(0.12f))
             .out(0.2f).verb(0.1f, 0.2f);
            break;
        case Sfx::Coin:  // the classic two-note coin, but with a bell shimmer and echo
            p.add(L(0, 0.075f, O::Square, hz(83)).pw(0.25f).lp(6500).dec(0.06f, 0.5f).vol(0.33f))
             .add(L(0.065f, 0.45f, O::Square, hz(88)).pw(0.25f).lp(7500, 2500).dec(0.13f).vol(0.33f))
             .add(L(0.065f, 0.55f, O::Sine, hz(95)).fm(2, 0.8f, 0).dec(0.14f).vol(0.16f).pan(0.25f))
             .add(L(0, 0.05f, O::Sine, hz(95)).dec(0.02f).vol(0.12f).pan(-0.25f))
             .out(0.5f).echo(0.2f, 0.085f, 0.3f).verb(0.12f, 0.35f);
            break;
        case Sfx::Hurt:  // body thump + growl + crack
            p.add(L(0, 0.26f, O::Sine, 150, 45).drop(1.6f, 0.015f).dec(0.09f).vol(0.9f))
             .add(L(0, 0.32f, O::Saw, r.jit(330, 0.05f), 70).uni(3, 28).lp(2600, 300, 1.3f).drive(2).dec(0.12f).vol(0.5f))
             .add(L(0, 0.08f, O::Noise).bp(2200, 900, 0.8f).dec(0.025f).vol(0.35f))
             .add(L(0.015f, 0.2f, O::Crush, 1800, 280).lp(3000).dec(0.05f).vol(0.22f))
             .out(0.85f).verb(0.14f, 0.35f);
            break;
        case Sfx::Power: {  // supersaw arpeggio with bells and a riser
            float notes[] = {hz(72), hz(76), hz(79), hz(84)};
            for (int i = 0; i < 4; i++) {
                float at = i * 0.065f, len = i == 3 ? 0.55f : 0.2f;
                Layer saw = L(at, len, O::Saw, notes[i]).uni(5, 14).lp(1100, 6500, 1.1f).env(0.004f, 0.08f).dec(0.13f, 0.3f).vol(0.22f);
                if (i == 3) saw.vib(0.25f, 6.5f);
                p.add(saw).add(L(at, len + 0.15f, O::Sine, notes[i] * 2).fm(2, 1.0f, 0).dec(0.18f).vol(0.12f).pan(i % 2 ? 0.3f : -0.3f));
            }
            p.add(L(0, 0.32f, O::Noise).bp(700, 7000, 2).env(0.22f, 0.04f).vol(0.16f))
             .add(L(0.195f, 0.5f, O::Sine, hz(48)).dec(0.25f).vol(0.25f))
             .out(0.62f).echo(0.18f, 0.11f, 0.3f).verb(0.22f, 0.5f);
            break;
        }
        case Sfx::Win: {  // brassy fanfare into a big major chord
            float seq[] = {hz(72), hz(76), hz(79)};
            for (int i = 0; i < 3; i++)
                p.add(L(i * 0.11f, 0.16f, O::Saw, seq[i]).uni(4, 10).lp(900, 3500, 1.2f).env(0.01f, 0.05f).vol(0.24f))
                 .add(L(i * 0.11f, 0.3f, O::Sine, seq[i] * 2).fm(2, 1.2f, 0).dec(0.12f).vol(0.12f));
            float chord[] = {hz(84), hz(79), hz(76), hz(72)};
            for (int i = 0; i < 4; i++)
                p.add(L(0.33f, 1.0f, O::Saw, chord[i]).uni(4, 12).lp(1000, 3800, 1.1f).env(0.015f, 0.35f).dec(0.4f, 0.45f)
                          .vib(i == 0 ? 0.3f : 0.12f, 5.5f).vol(i == 0 ? 0.24f : 0.13f).pan((i - 1.5f) * 0.3f));
            p.add(L(0.33f, 1.2f, O::Sine, hz(96)).fm(2, 1.2f, 0).dec(0.3f).vol(0.1f))
             .add(L(0.33f, 0.9f, O::Sine, hz(48)).dec(0.45f).vol(0.3f))
             .add(L(0.33f, 0.35f, O::Noise).hp(7000).dec(0.1f).vol(0.07f))
             .out(0.72f).echo(0.1f, 0.14f, 0.3f).verb(0.3f, 0.6f);
            break;
        }
        case Sfx::Lose: {  // wah-wah sad trombone
            float seq[] = {hz(67), hz(66), hz(65)};
            for (int i = 0; i < 3; i++)
                p.add(L(i * 0.23f, 0.24f, O::Saw, seq[i]).uni(3, 12).lp(500, 2200, 3).env(0.02f, 0.06f).vol(0.32f));
            p.add(L(0.69f, 0.95f, O::Saw, hz(64), hz(63.2f)).uni(3, 12).lp(2400, 300, 3).vib(0.45f, 5).env(0.02f, 0.2f).vol(0.34f))
             .add(L(0.69f, 0.95f, O::Sine, hz(40)).dec(0.5f).vol(0.25f))
             .out(0.62f).verb(0.25f, 0.55f);
            break;
        }
        case Sfx::Dash:  // airy whoosh panning across
            p.add(L(0, 0.24f, O::Noise).bp(650, 5200, 2.5f).env(0.03f, 0.09f).vol(0.6f).pan(-0.5f, 0.5f))
             .add(L(0, 0.15f, O::Sine, 300, 1100).curve(2).vol(0.15f))
             .out(0.5f).verb(0.1f, 0.3f);
            break;
        case Sfx::Boom:  // sub kick, blast, crackle and a rumbling tail
            p.add(L(0, 0.75f, O::Sine, 105, 32).drop(2, 0.02f).dec(0.26f).vol(1.0f))
             .add(L(0, 1.1f, O::Noise).lp(5000, 140, 1).drive(1.5f).dec(0.32f).vol(0.8f))
             .add(L(0.01f, 0.55f, O::Crush, 950, 60).dec(0.14f).vol(0.35f))
             .add(L(0.05f, 1.3f, O::Pink).lp(900, 180).dec(0.5f).vol(0.45f))
             .out(0.92f).verb(0.25f, 0.7f, 0.6f);
            break;
        case Sfx::Card:  // snap + paper slide
            p.add(L(0, 0.05f, O::Noise).bp(r.jit(3600, 0.12f), 1800, 1.2f).dec(0.014f).vol(0.65f))
             .add(L(0.012f, 0.09f, O::Noise).bp(5200, 2500, 0.8f).env(0.01f, 0.03f).vol(0.25f))
             .add(L(0, 0.022f, O::Sine, 950, 300).vol(0.2f))
             .out(0.45f).verb(0.08f, 0.25f);
            break;
        case Sfx::Tick:
            p.add(L(0, 0.018f, O::Square, r.jit(2600, 0.06f), 1800).pw(0.3f).lp(6000).curve(3).vol(0.4f))
             .add(L(0, 0.006f, O::Noise).hp(4000).vol(0.3f))
             .out(0.3f).verb(0.05f, 0.2f);
            break;
        case Sfx::Gem:  // crystalline FM chime
            p.add(L(0, 0.5f, O::Sine, hz(91)).fm(2, 1.3f, 0).dec(0.15f).vol(0.4f))
             .add(L(0.07f, 0.85f, O::Sine, hz(96)).fm(3, 1.1f, 0).dec(0.25f).vol(0.35f))
             .add(L(0.07f, 0.85f, O::Sine, hz(96) * 1.004f).dec(0.3f).vol(0.15f).pan(0.45f))
             .add(L(0.07f, 0.85f, O::Sine, hz(84)).dec(0.3f).vol(0.12f).pan(-0.45f))
             .out(0.5f).echo(0.25f, 0.1f, 0.4f).verb(0.3f, 0.6f);
            break;
        case Sfx::Neigh: {  // whinny: trilling formant voice, a squeal up front, breath and a snort
            float k = r.jit(1, 0.06f);
            p.add(L(0, 0.2f, O::Saw, 620 * k, 1180 * k).vib(0.9f, 20).bp(1900, -1, 2).env(0.02f, 0.04f).vol(0.35f))
             .add(L(0.12f, 0.85f, O::Saw, 1100 * k, 420 * k).glide(0.7f).vib(1.7f, 13.5f).uni(2, 14).bp(1450, 850, 1.8f).env(0.03f, 0.18f).vol(0.55f))
             .add(L(0.12f, 0.85f, O::Saw, 1100 * k, 420 * k).glide(0.7f).vib(1.7f, 13.5f).bp(2700, 1800, 2.5f).env(0.03f, 0.18f).vol(0.25f))
             .add(L(0, 1.0f, O::Noise).bp(2100, 1200, 1).env(0.05f, 0.2f).vol(0.12f))
             .add(L(0.98f, 0.2f, O::Noise).lp(900).trem(0.85f, 32).env(0.01f, 0.06f).vol(0.45f))
             .out(0.7f).verb(0.2f, 0.5f);
            break;
        }
        case Sfx::Chew:  // three crunchy munches
            for (int i = 0; i < 3; i++) {
                float at = i * 0.14f + r.range(0, 0.02f);
                p.add(L(at, 0.08f, O::Noise).bp(r.range(900, 1500), 400, 1.2f).dec(0.03f).vol(0.5f))
                 .add(L(at, 0.06f, O::Crush, r.range(2000, 3200), 600).dec(0.02f).vol(0.25f))
                 .add(L(at, 0.07f, O::Sine, 190, 90).dec(0.03f).vol(0.35f));
            }
            p.out(0.6f).verb(0.06f, 0.2f);
            break;
        case Sfx::Bubble:  // water blip
            p.add(L(0, 0.11f, O::Sine, r.jit(380, 0.18f), 1500).glide(2.2f).env(0.003f, 0.02f).curve(1.2f).vol(0.6f))
             .add(L(0.09f, 0.02f, O::Sine, 1600, 1850).vol(0.2f))
             .out(0.45f).verb(0.15f, 0.3f);
            break;
        case Sfx::Whoosh:
            p.add(L(0, 0.38f, O::Noise).bp(400, 3600, 2).env(0.15f, 0.15f).vol(0.6f).pan(-0.6f, 0.6f))
             .add(L(0, 0.38f, O::Pink).lp(800, 2600).env(0.12f, 0.12f).vol(0.35f))
             .out(0.5f).verb(0.12f, 0.35f);
            break;
        case Sfx::Drop:  // thud
            p.add(L(0, 0.18f, O::Sine, 200, 70).drop(0.8f, 0.01f).dec(0.06f).vol(0.8f))
             .add(L(0, 0.06f, O::Noise).lp(3000, 500).dec(0.02f).vol(0.35f))
             .add(L(0, 0.12f, O::Tri, 420, 180).dec(0.04f).vol(0.2f))
             .out(0.6f).verb(0.1f, 0.3f);
            break;
        case Sfx::Perfect: {  // chime arpeggio
            float seq[] = {hz(84), hz(88), hz(91), hz(96)};
            for (int i = 0; i < 4; i++)
                p.add(L(i * 0.065f, 0.45f, O::Sine, seq[i]).fm(2, 1.0f, 0).dec(0.15f).vol(0.3f).pan((i - 1.5f) * 0.25f))
                 .add(L(i * 0.065f, 0.3f, O::Tri, seq[i] / 2).dec(0.1f).vol(0.12f));
            p.out(0.55f).echo(0.2f, 0.1f, 0.3f).verb(0.25f, 0.5f);
            break;
        }
        case Sfx::Jump:  // springy boing
            p.add(L(0, 0.22f, O::Square, r.jit(260, 0.05f), 700).pw(0.35f).lp(2600).glide(0.45f).dec(0.12f).vol(0.35f))
             .add(L(0, 0.22f, O::Sine, 520, 1400).glide(0.45f).dec(0.1f).vol(0.2f))
             .out(0.45f).verb(0.08f, 0.3f);
            break;
        case Sfx::Bell: {  // struck bell: inharmonic partials with their own decays
            float f = r.jit(990, 0.015f);
            float ratio[] = {0.5f, 1, 2.0f, 2.76f, 5.4f, 8.93f};
            float gain[] = {0.25f, 0.5f, 0.3f, 0.18f, 0.06f, 0.025f};
            float dec[] = {1.4f, 1.1f, 0.8f, 0.55f, 0.3f, 0.18f};
            for (int i = 0; i < 6; i++)
                p.add(L(0, dec[i] * 1.8f, O::Sine, f * ratio[i]).dec(dec[i] * 0.45f).env(0.001f, 0.1f).vol(gain[i]).pan((i % 2 ? 1 : -1) * 0.2f));
            p.add(L(0, 0.02f, O::Noise).hp(5000).vol(0.25f))
             .out(0.55f).verb(0.3f, 0.6f);
            break;
        }
        case Sfx::Spawn:  // magic fwip
            p.add(L(0, 0.25f, O::Sine, 300, 950).glide(0.7f).env(0.05f, 0.08f).vol(0.3f))
             .add(L(0, 0.25f, O::Noise).bp(1500, 5000, 3).env(0.08f, 0.05f).vol(0.2f))
             .out(0.35f).verb(0.2f, 0.4f);
            break;
        case Sfx::Shoot:  // pew
            p.add(L(0, 0.12f, O::Square, r.jit(1700, 0.06f), 260).glide(0.35f).pw(0.3f).lp(5000, 1200).curve(1.5f).vol(0.35f))
             .add(L(0, 0.03f, O::Noise).hp(2500).dec(0.01f).vol(0.2f))
             .out(0.38f).verb(0.08f, 0.25f);
            break;
        case Sfx::Error:  // soft low buzzer, not harsh
            p.add(L(0, 0.11f, O::Square, 196).pw(0.4f).lp(1200).uni(2, 10).env(0.005f, 0.03f).vol(0.4f))
             .add(L(0.13f, 0.2f, O::Square, 165).pw(0.4f).lp(1000, 500).uni(2, 10).env(0.005f, 0.06f).vol(0.4f))
             .out(0.42f).verb(0.08f, 0.25f);
            break;
        case Sfx::Kiss:  // mwah
            p.add(L(0, 0.03f, O::Noise).bp(2600, -1, 1).dec(0.008f).vol(0.5f))
             .add(L(0.01f, 0.16f, O::Sine, r.jit(700, 0.08f), 1700).glide(0.6f).env(0.01f, 0.04f).vol(0.4f))
             .add(L(0.01f, 0.16f, O::Tri, 1400, 2600).vol(0.08f))
             .out(0.45f).verb(0.12f, 0.3f);
            break;
        case Sfx::Pop:
            p.add(L(0, 0.05f, O::Sine, r.jit(900, 0.15f), 300).glide(0.3f).curve(2).vol(0.7f))
             .add(L(0, 0.01f, O::Noise).hp(2000).vol(0.3f))
             .out(0.4f).verb(0.1f, 0.25f);
            break;
        case Sfx::Snore:  // rattly inhale, whistly exhale
            p.add(L(0, 0.95f, O::Pink).lp(r.jit(420, 0.1f), 750, 4).trem(0.6f, 27).env(0.35f, 0.2f).vol(0.6f))
             .add(L(0, 0.95f, O::Saw, 70, 82).lp(300).env(0.3f, 0.2f).vol(0.25f))
             .add(L(1.1f, 0.8f, O::Noise).bp(1300, 700, 1).env(0.1f, 0.4f).vol(0.25f))
             .add(L(1.1f, 0.7f, O::Sine, 950, 720).env(0.1f, 0.3f).vol(0.035f))
             .out(0.4f).verb(0.12f, 0.35f);
            break;
        case Sfx::Water:  // shower burst: overlapping copies make a continuous stream
            p.add(L(0, 1.6f, O::Noise).bp(2900, -1, 0.6f).env(0.35f, 0.35f).vol(0.5f).pan(-0.5f))
             .add(L(0, 1.6f, O::Noise).bp(2400, -1, 0.6f).env(0.35f, 0.35f).vol(0.5f).pan(0.5f))
             .add(L(0, 1.6f, O::Pink).lp(1800).env(0.35f, 0.35f).vol(0.4f));
            for (int i = 0; i < 12; i++) {
                float f = r.range(1100, 2600);
                p.add(L(r.range(0.1f, 1.4f), 0.04f, O::Sine, f, f * 1.5f).vol(0.1f).pan(r.bi() * 0.7f));
            }
            p.out(0.35f).verb(0.15f, 0.45f);
            break;
        case Sfx::Yawn:
            p.add(L(0, 0.45f, O::Saw, 240, 330).bp(900, 1100, 1.5f).vib(0.2f, 5).env(0.08f, 0.05f).vol(0.45f))
             .add(L(0.4f, 0.75f, O::Saw, 330, 170).bp(1100, 650, 1.5f).vib(0.25f, 5).env(0.05f, 0.3f).vol(0.45f))
             .add(L(0, 1.15f, O::Noise).bp(1400, 800, 1).env(0.1f, 0.4f).vol(0.12f))
             .out(0.45f).verb(0.2f, 0.4f);
            break;
        case Sfx::Boop:  // squeaky-toy boop for petting
            p.add(L(0, 0.14f, O::Sine, r.jit(700, 0.1f), 1250).glide(0.5f).vib(0.5f, 30).env(0.01f, 0.04f).vol(0.5f))
             .add(L(0.12f, 0.09f, O::Sine, 1250, 1000).env(0.005f, 0.04f).vol(0.3f))
             .out(0.4f).verb(0.12f, 0.3f);
            break;
        case Sfx::Gulp:
            p.add(L(0, 0.12f, O::Sine, 500, 200).glide(0.6f).vol(0.6f))
             .add(L(0.06f, 0.1f, O::Sine, 260, 640).glide(1.5f).vol(0.4f))
             .out(0.45f).verb(0.1f, 0.25f);
            break;
        case Sfx::Buzz:  // a fly doing laps
            p.add(L(0, 0.55f, O::Saw, r.jit(220, 0.12f)).vib(0.6f, 7).uni(2, 30).bp(1200, 900, 1.2f).trem(0.5f, 9)
                      .env(0.08f, 0.15f).vol(0.4f).pan(r.bi() * 0.5f, r.bi() * 0.5f))
             .out(0.18f).verb(0.05f, 0.2f);
            break;
        case Sfx::Msg:  // incoming chat: two marimba notes
            p.add(L(0, 0.3f, O::Sine, hz(81)).dec(0.08f).vol(0.5f))
             .add(L(0, 0.05f, O::Sine, hz(81) * 4).dec(0.01f).vol(0.2f))
             .add(L(0.09f, 0.45f, O::Sine, hz(88)).dec(0.12f).vol(0.5f))
             .add(L(0.09f, 0.05f, O::Sine, hz(88) * 4).dec(0.01f).vol(0.2f))
             .out(0.4f).echo(0.1f, 0.1f, 0.25f).verb(0.15f, 0.35f);
            break;
        case Sfx::Send:
            p.add(L(0, 0.12f, O::Sine, 600, 1400).glide(0.5f).curve(2).vol(0.4f))
             .add(L(0, 0.12f, O::Noise).bp(2000, 6000, 2).vol(0.15f))
             .out(0.35f).verb(0.1f, 0.3f);
            break;
        case Sfx::Key:  // keyboard clack
            p.add(L(0, 0.025f, O::Noise).bp(r.range(2500, 4500), -1, 1.5f).dec(0.006f).vol(0.5f))
             .add(L(0, 0.03f, O::Sine, r.range(300, 500), 150).dec(0.008f).vol(0.3f))
             .out(0.25f).verb(0.04f, 0.15f);
            break;
        // ---- Five Nights at Pieter's
        case Sfx::Breath:  // inhale through the teeth, then a long wet growl out
            p.add(L(0, 1.1f, O::Noise).bp(700, 1600, 1.4f).env(0.5f, 0.35f).vol(0.45f))
             .add(L(0, 1.1f, O::Noise).bp(2600, 3400, 3).env(0.5f, 0.3f).vol(0.12f))
             .add(L(1.15f, 1.5f, O::Noise).bp(1400, 480, 1.1f).env(0.08f, 0.8f).vol(0.6f))
             .add(L(1.15f, 1.5f, O::Saw, r.jit(78, 0.08f), 56).uni(3, 30).lp(380, 200).drive(2).trem(0.5f, r.range(26, 36)).env(0.1f, 0.7f).vol(0.3f))
             .add(L(1.15f, 1.2f, O::Crush, r.range(38, 48)).lp(900).env(0.1f, 0.6f).vol(0.12f))
             .out(0.62f).verb(0.18f, 0.3f, 0.6f);
            break;
        case Sfx::Giggle: {  // "hihihi": a little girl's voice (formants on a buzzy source) with a ghost an octave down
            float at = 0;
            for (int i = 0; i < 5; i++) {
                float f = r.jit(640, 0.03f) * std::pow(0.95f, (float)i);
                p.add(L(at, 0.04f, O::Noise).bp(2800, -1, 1.5f).env(0.003f, 0.02f).vol(0.16f))
                 .add(L(at + 0.025f, 0.085f, O::Saw, f, f * 0.92f).vib(0.8f, 22).bp(420, -1, 4).env(0.006f, 0.04f).vol(0.5f))
                 .add(L(at + 0.025f, 0.085f, O::Saw, f, f * 0.92f).vib(0.8f, 22).bp(2300, -1, 6).env(0.006f, 0.04f).vol(0.36f))
                 .add(L(at + 0.025f, 0.085f, O::Saw, f, f * 0.92f).vib(0.8f, 22).bp(3100, -1, 8).env(0.006f, 0.04f).vol(0.18f))
                 .add(L(at + 0.06f, 0.1f, O::Saw, f * 0.5f, f * 0.44f).bp(900, -1, 3).env(0.01f, 0.05f).vol(0.13f).pan(r.bi() * 0.6f));
                at += r.jit(0.12f, 0.15f);
            }
            p.out(0.5f).echo(0.32f, 0.19f, 0.45f).verb(0.42f, 0.78f);
            break;
        }
        case Sfx::Heart:  // lub-dub
            p.add(L(0, 0.13f, O::Sine, 72, 45).drop(1, 0.02f).dec(0.04f).vol(0.9f))
             .add(L(0.19f, 0.11f, O::Sine, 62, 40).drop(1, 0.02f).dec(0.035f).vol(0.7f))
             .out(0.8f).verb(0.05f, 0.2f);
            break;
        case Sfx::Step:  // footstep down a long hallway
            p.add(L(0, 0.1f, O::Noise).lp(r.jit(650, 0.15f), 200).dec(0.03f).vol(0.6f))
             .add(L(0, 0.09f, O::Sine, 115, 60).dec(0.03f).vol(0.6f))
             .add(L(0.02f, 0.25f, O::Saw, r.jit(160, 0.2f), 130).bp(800, -1, 5).trem(0.6f, 40).env(0.02f, 0.1f).vol(0.12f))
             .out(0.5f).verb(0.4f, 0.65f, 0.5f);
            break;
        case Sfx::Slam:  // door pulled shut
            p.add(L(0, 0.4f, O::Sine, 92, 40).drop(1.2f, 0.02f).dec(0.12f).vol(1))
             .add(L(0, 0.25f, O::Noise).lp(2600, 300).dec(0.06f).vol(0.6f))
             .add(L(0.02f, 0.15f, O::Crush, 700, 200).dec(0.04f).vol(0.3f))
             .out(0.7f).verb(0.25f, 0.5f);
            break;
        case Sfx::Flash:  // flashlight switch
            p.add(L(0, 0.012f, O::Noise).hp(3000).vol(0.6f))
             .add(L(0, 0.02f, O::Square, 2200, 1500).pw(0.3f).curve(3).vol(0.3f))
             .out(0.3f).verb(0.05f, 0.15f);
            break;
        case Sfx::Scream:  // jumpscare: a screaming "AAA" formant voice, a metal screech, a crash and a sub drop
            for (float fm : {800.0f, 1250.0f, 2600.0f})
                p.add(L(0, 1.5f, O::Saw, r.jit(560, 0.05f), 380).glide(0.6f).uni(5, 70).vib(1.5f, 19).bp(fm, fm * 0.85f, 3).drive(3).env(0.004f, 0.35f)
                          .vol(fm < 1000 ? 0.7f : fm < 2000 ? 0.55f : 0.35f));
            p.add(L(0, 1.3f, O::Sine, r.jit(1900, 0.04f), 1150).fm(1.414f, 9, 3).vib(2.5f, 23).drive(2).hp(700).env(0.004f, 0.3f).vol(0.35f))
             .add(L(0, 0.9f, O::Noise).hp(3500).dec(0.25f).vol(0.45f))
             .add(L(0, 1.5f, O::Noise).bp(2400, 1500, 0.7f).env(0.004f, 0.3f).vol(0.4f))
             .add(L(0, 1.4f, O::Saw, 55, 40).uni(4, 40).drive(4).lp(700, 250).env(0.004f, 0.4f).vol(0.5f))
             .add(L(0, 0.9f, O::Sine, 90, 28).drop(1.5f, 0.03f).dec(0.3f).vol(0.9f))
             .out(0.97f).verb(0.22f, 0.55f);
            break;
        case Sfx::Static:
            p.add(L(0, 1.2f, O::Noise).hp(800).trem(0.3f, 13).env(0.02f, 0.2f).vol(0.5f))
             .add(L(0, 1.2f, O::Crush, 4000).trem(0.5f, 7).env(0.02f, 0.2f).vol(0.2f))
             .out(0.4f);
            break;
        case Sfx::MusicBox: {  // "Lang zal hij leven", on a broken music box
            const int mel[] = {72, 72, 72, 69, 70, 72, 70, 70, 70, 67, 69, 70, 69, 69, 69, 65, 67, 69, 67, 65, 64, 62, 60};
            float at = 0;
            for (int i = 0; i < 23; i++) {
                float f = hz(mel[i] - 1.5f) * r.jit(1, 0.006f);
                float len = (i % 6 == 5 || i == 22) ? 0.56f : 0.28f;
                p.add(L(at, 0.9f, O::Sine, f).fm(3.5f, 1.4f, 0.2f).vib(0.15f, 4).dec(0.25f).vol(0.35f))
                 .add(L(at, 0.4f, O::Sine, f * 2.01f).dec(0.08f).vol(0.1f));
                at += len * (1 + 0.04f * std::sin(i * 1.3f));  // the spring is running down
            }
            p.out(0.45f).echo(0.2f, 0.21f, 0.35f).verb(0.35f, 0.7f);
            break;
        }
        case Sfx::Drone:  // room tone: a beating minor second, sub, a breathing wind band and a glassy cluster up high
            p.add(L(0, 4, O::Sine, 55).env(1.2f, 1.2f).vol(0.4f))
             .add(L(0, 4, O::Sine, 58.3f).env(1.2f, 1.2f).vol(0.3f))
             .add(L(0, 4, O::Saw, 41.2f).uni(3, 12).lp(160).env(1.2f, 1.2f).vol(0.35f))
             .add(L(0, 4, O::Pink).bp(r.range(250, 400), r.range(600, 900), 2).trem(0.5f, 0.3f).env(1.2f, 1.2f).vol(0.35f))
             .add(L(0, 4, O::Sine, r.jit(1567, 0.01f), 1480).vib(0.3f, 0.5f).env(1.5f, 1.5f).vol(0.03f))
             .add(L(0, 4, O::Sine, r.jit(1661, 0.01f), 1700).vib(0.25f, 0.37f).env(1.5f, 1.5f).vol(0.025f))
             .out(0.38f).verb(0.45f, 0.85f);
            break;
        case Sfx::Creak:  // wooden creak
            p.add(L(0, 0.6f, O::Saw, r.jit(140, 0.1f), 110).bp(900, 600, 4).trem(0.6f, 35).env(0.05f, 0.2f).vol(0.6f))
             .add(L(0, 0.6f, O::Square, 280, 240).pw(0.15f).bp(1400, -1, 5).trem(0.7f, 47).vol(0.15f))
             .out(0.45f).verb(0.3f, 0.5f);
            break;
        case Sfx::Klomp:  // a wooden clog on the hallway tiles: hollow heel, then toe, long hall reverb
            for (int k = 0; k < 2; k++) {
                float at = k * r.range(0.07f, 0.1f), g = k ? 0.6f : 1;
                p.add(L(at, 0.06f, O::Noise).bp(r.jit(1150, 0.1f), -1, 6).dec(0.012f).vol(0.7f * g))
                 .add(L(at, 0.07f, O::Sine, r.jit(720, 0.06f), 480).dec(0.018f).vol(0.45f * g))
                 .add(L(at, 0.12f, O::Sine, 190, 120).dec(0.03f).vol(0.35f * g));
            }
            p.out(0.5f).verb(0.5f, 0.75f, 0.45f);
            break;
        case Sfx::FietsBel: {  // "tring tring": the classic Dutch bike bell, rattling clapper
            float f = r.jit(2100, 0.015f);
            const float ratio[] = {1, 2.32f, 4.1f, 6.0f}, gain[] = {0.5f, 0.3f, 0.15f, 0.08f}, dcy[] = {0.6f, 0.35f, 0.2f, 0.12f};
            for (int k = 0; k < 2; k++)
                for (int i = 0; i < 4; i++)
                    p.add(L(k * 0.3f, 1.1f, O::Sine, f * ratio[i]).dec(dcy[i]).trem(0.45f, 30 + i * 3).vol(gain[i]));
            p.out(0.45f).echo(0.12f, 0.23f, 0.3f).verb(0.4f, 0.8f);
            break;
        }
        case Sfx::Carillon: {  // the church tower's beiaard plays the Wilhelmus, slightly out of tune
            const int mel[] = {62, 67, 67, 69, 71, 72, 74, 76, 72, 74};
            const float len[] = {0.5f, 0.5f, 0.38f, 0.38f, 0.38f, 0.38f, 0.5f, 0.38f, 0.38f, 0.9f};
            float at = 0;
            for (int i = 0; i < 10; i++) {
                float f = hz(mel[i] - 0.4f) * r.jit(1, 0.004f);
                // bell partials: hum, prime, the carillon's minor third, fifth, nominal
                const float rt[] = {0.5f, 1, 1.2f, 1.5f, 2, 2.97f}, gn[] = {0.3f, 0.45f, 0.3f, 0.12f, 0.25f, 0.08f}, dc[] = {2.2f, 1.4f, 1.0f, 0.7f, 0.6f, 0.3f};
                for (int k = 0; k < 6; k++) p.add(L(at, 3, O::Sine, f * rt[k]).dec(dc[k]).env(0.002f, 0.3f).vol(gn[k]));
                at += len[i];
            }
            p.out(0.5f).verb(0.5f, 0.9f, 0.3f);
            break;
        }
        case Sfx::Rain:  // a few seconds of rain on the window; retriggered so copies overlap
            p.add(L(0, 4, O::Pink).lp(3200).env(1.0f, 1.0f).vol(0.5f))
             .add(L(0, 4, O::Noise).hp(6000).trem(0.3f, 0.7f).env(1.0f, 1.0f).vol(0.12f));
            for (int i = 0; i < 70; i++) {
                float at = r.range(0.3f, 3.6f), f = r.range(1600, 4200);
                p.add(L(at, 0.03f, O::Sine, f, f * 0.6f).dec(0.006f).pan(r.bi() * 0.9f).vol(r.range(0.05f, 0.18f)));
            }
            for (int i = 0; i < 4; i++)  // gutter drips
                p.add(L(r.range(0.5f, 3.4f), 0.08f, O::Sine, r.range(700, 1000), 1400).glide(0.5f).dec(0.02f).pan(0.6f).vol(0.25f));
            p.out(0.4f).verb(0.2f, 0.4f);
            break;
        case Sfx::Wind:  // a gust howling past the window
            p.add(L(0, 5, O::Pink).bp(r.range(250, 350), r.range(700, 900), 2.5f).trem(0.4f, 0.4f).env(1.8f, 2.0f).pan(-0.6f, 0.6f).vol(0.6f))
             .add(L(0.5f, 4.5f, O::Noise).bp(r.range(900, 1100), 500, 6).env(1.5f, 1.8f).pan(0.5f, -0.5f).vol(0.3f))
             .add(L(0.8f, 4, O::Sine, r.range(560, 640), r.range(700, 800)).vib(0.6f, 0.8f).lp(1200).env(1.5f, 1.5f).vol(0.05f))
             .out(0.45f).verb(0.3f, 0.7f);
            break;
        case Sfx::Thunder: {  // close crack, then rolling rumbles
            p.add(L(0, 0.35f, O::Noise).hp(1200).drive(3).dec(0.07f).vol(0.8f))
             .add(L(0, 0.5f, O::Crush, 900, 200).dec(0.1f).vol(0.4f))
             .add(L(0, 4.5f, O::Pink).lp(420, 110).dec(1.4f).vol(0.9f))
             .add(L(0, 3.5f, O::Sine, 42, 30).trem(0.6f, 3).dec(1.0f).vol(0.6f));
            for (int i = 0; i < 4; i++)
                p.add(L(r.range(0.3f, 2.5f), 1.5f, O::Pink).lp(r.range(200, 400)).dec(0.4f).pan(r.bi() * 0.7f).vol(r.range(0.4f, 0.7f)));
            p.out(0.9f).verb(0.35f, 0.9f, 0.6f);
            break;
        }
        case Sfx::Whisper: {  // breathy syllables ("pie-ter...") drifting past one ear
            float at = 0, side = r.bi() > 0 ? 1.0f : -1.0f;
            int n = 2 + (int)(r.f() * 3);
            for (int i = 0; i < n; i++) {
                float d = r.range(0.16f, 0.3f), f1 = r.range(300, 750), f2 = r.range(1200, 2600);
                p.add(L(at, d * 0.35f, O::Noise).bp(r.range(3500, 5500), -1, 2).env(0.01f, 0.03f).pan(side * 0.8f).vol(0.25f))
                 .add(L(at + d * 0.2f, d, O::Noise).bp(f1, f1 * 0.9f, 7).env(0.03f, 0.08f).pan(side * 0.8f, side * 0.4f).vol(0.7f))
                 .add(L(at + d * 0.2f, d, O::Noise).bp(f2, f2 * 0.85f, 9).env(0.03f, 0.08f).pan(side * 0.8f, side * 0.4f).vol(0.55f));
                at += d + r.range(0.05f, 0.18f);
            }
            p.out(0.4f).echo(0.2f, 0.27f, 0.4f).verb(0.5f, 0.85f);
            break;
        }
        case Sfx::Knock:  // three knocks on a wooden door
            for (int k = 0; k < 3; k++) {
                float at = k * r.range(0.2f, 0.26f);
                p.add(L(at, 0.12f, O::Sine, r.jit(170, 0.05f), 105).drop(0.8f, 0.01f).dec(0.03f).vol(0.9f))
                 .add(L(at, 0.06f, O::Noise).bp(900, -1, 2).dec(0.012f).vol(0.6f))
                 .add(L(at, 0.2f, O::Sine, 95).dec(0.06f).vol(0.4f));
            }
            p.out(0.75f).verb(0.3f, 0.55f);
            break;
        case Sfx::Monkey: {  // "OOH OOH AAH AAH" turning into a shriek
            float at = 0;
            for (int i = 0; i < 5; i++) {
                bool ah = i >= 2;
                float f = r.jit(ah ? 820 : 460, 0.05f) * (1 + i * 0.08f), d = ah ? 0.16f : 0.13f;
                float fa = ah ? 1100 : 550, fb = ah ? 1700 : 950;
                p.add(L(at, d, O::Saw, f * 0.85f, f * 1.25f).glide(0.6f).vib(0.6f, 18).bp(fa, -1, 4).drive(1.5f).env(0.01f, 0.05f).vol(0.6f))
                 .add(L(at, d, O::Saw, f * 0.85f, f * 1.25f).glide(0.6f).vib(0.6f, 18).bp(fb, -1, 6).env(0.01f, 0.05f).vol(0.4f))
                 .add(L(at, d * 0.5f, O::Noise).bp(2500, -1, 2).env(0.005f, 0.03f).vol(0.12f));
                at += d + 0.05f;
            }
            p.add(L(at, 0.55f, O::Saw, r.jit(1500, 0.04f), 1050).vib(1.8f, 21).bp(1800, 1300, 3).drive(2.5f).env(0.01f, 0.25f).vol(0.6f))
             .add(L(at, 0.55f, O::Saw, r.jit(1500, 0.04f), 1050).vib(1.8f, 21).bp(3000, -1, 5).env(0.01f, 0.25f).vol(0.25f))
             .out(0.7f).verb(0.3f, 0.6f);
            break;
        }
        case Sfx::Cymbal:  // a wind-up toy monkey clashing its cymbals
            p.add(L(0, 0.012f, O::Noise).hp(2000).vol(0.6f))
             .add(L(0, 0.7f, O::Noise).hp(3500).bp(7000, -1, 0.8f).dec(0.18f).vol(0.5f))
             .add(L(0, 0.7f, O::Sine, r.jit(820, 0.03f)).fm(1.414f, 7, 2).dec(0.2f).vol(0.35f))
             .add(L(0, 0.7f, O::Sine, r.jit(1330, 0.03f)).fm(1.73f, 5, 1).dec(0.15f).vol(0.25f))
             .add(L(0.04f, 0.2f, O::Square, 40).pw(0.3f).lp(800).trem(0.8f, 35).vol(0.1f))  // the clockwork
             .out(0.5f).verb(0.25f, 0.5f);
            break;
        case Sfx::Tinnitus:  // ears ringing after a scare
            p.add(L(0, 4, O::Sine, r.jit(6200, 0.02f)).env(0.05f, 1.5f).dec(1.8f, 0.2f).vol(0.25f))
             .add(L(0, 4, O::Sine, r.jit(4100, 0.02f)).vib(0.05f, 3).env(0.3f, 1.5f).dec(1.4f).vol(0.1f))
             .add(L(0, 1.5f, O::Pink).lp(300).dec(0.4f).vol(0.4f))
             .out(0.3f);
            break;
        case Sfx::Scratch:  // claws on the inside of a wooden door
            for (int k = 0; k < 4; k++) {
                float at = k * r.range(0.18f, 0.3f), d = r.range(0.12f, 0.22f);
                p.add(L(at, d, O::Noise).bp(r.range(2500, 3800), r.range(1500, 2200), 4).trem(0.7f, r.range(60, 110)).env(0.01f, 0.05f).vol(0.6f))
                 .add(L(at, d, O::Saw, r.range(200, 300), r.range(160, 220)).bp(1100, -1, 5).trem(0.6f, 70).env(0.01f, 0.05f).vol(0.15f));
            }
            p.out(0.45f).verb(0.25f, 0.4f);
            break;
        case Sfx::Riser:  // tension rising, cut off dead
            p.add(L(0, 3, O::Saw, 110, 233).glide(2).uni(7, 40).lp(300, 4000).env(1.8f, 0.01f).curve(0).vol(0.45f))
             .add(L(0, 3, O::Noise).hp(400, 6000).env(2.2f, 0.01f).curve(0).vol(0.25f))
             .add(L(0, 3, O::Sine, 55, 110).glide(2).env(1.5f, 0.01f).curve(0).vol(0.35f))
             .add(L(0, 3, O::Sine, 1760, 1661).trem(0.8f, 9).env(2.0f, 0.01f).curve(0).vol(0.04f))
             .out(0.55f);
            break;
        case Sfx::Sting: {  // dissonant cluster hit when you see something you shouldn't
            for (float m : {48.0f, 49.0f, 54.0f, 55.0f, 61.0f})
                p.add(L(0, 2.2f, O::Saw, hz(m)).uni(3, 18).lp(3000, 600).env(0.004f, 0.6f).dec(0.5f, 0.25f).vol(0.3f));
            p.add(L(0, 2.0f, O::Saw, hz(91), hz(90)).vib(0.5f, 7).hp(1500).env(0.01f, 0.6f).dec(0.6f, 0.2f).vol(0.12f))
             .add(L(0, 0.8f, O::Sine, 70, 32).drop(1, 0.03f).dec(0.25f).vol(0.9f))
             .add(L(0, 0.4f, O::Noise).lp(5000, 400).dec(0.08f).vol(0.4f))
             .out(0.75f).verb(0.45f, 0.8f);
            break;
        }
        case Sfx::Putt:  // putter on a golf ball: a crisp tick and a hollow tock
            p.add(L(0, 0.01f, O::Noise).hp(4000).vol(0.5f))
             .add(L(0, 0.06f, O::Sine, r.jit(1250, 0.03f), 900).dec(0.018f).vol(0.7f))
             .add(L(0, 0.08f, O::Sine, 320, 260).dec(0.025f).vol(0.35f))
             .add(L(0, 0.05f, O::Noise).bp(2600, -1, 4).dec(0.012f).vol(0.3f))
             .out(0.55f).verb(0.08f, 0.2f);
            break;
        case Sfx::Cup: {  // the ball rattles round the cup and drops in
            const float at[] = {0, 0.07f, 0.12f, 0.155f};
            for (int i = 0; i < 4; i++)
                p.add(L(at[i], 0.05f, O::Sine, r.jit(2300 - i * 120, 0.03f)).fm(1.41f, 2).dec(0.012f).vol(0.35f - i * 0.05f));
            p.add(L(0.19f, 0.2f, O::Sine, 420, 230).drop(0.5f, 0.01f).dec(0.05f).vol(0.9f))
             .add(L(0.19f, 0.1f, O::Noise).lp(1200).dec(0.03f).vol(0.4f))
             .out(0.65f).verb(0.15f, 0.3f);
            break;
        }
        case Sfx::Splash:  // plop into a canal
            p.add(L(0, 0.18f, O::Sine, r.jit(700, 0.06f), 140).glide(0.5f).dec(0.06f).vol(0.8f))
             .add(L(0.01f, 0.5f, O::Noise).lp(3000, 600).dec(0.14f).vol(0.6f))
             .add(L(0.01f, 0.3f, O::Noise).bp(1200, 500, 2).dec(0.08f).vol(0.3f));
            for (int i = 0; i < 5; i++) {
                float f = r.range(600, 1400);
                p.add(L(r.range(0.12f, 0.5f), 0.05f, O::Sine, f, f * 1.8f).dec(0.015f).vol(0.15f));
            }
            p.out(0.6f).verb(0.2f, 0.4f);
            break;
        case Sfx::Swap:  // two tiles sliding past each other
            p.add(L(0, 0.12f, O::Noise).bp(1500, 3500, 2).env(0.02f, 0.04f).vol(0.35f))
             .add(L(0, 0.09f, O::Sine, 520, 780).glide(0.6f).dec(0.04f).vol(0.35f))
             .out(0.35f).verb(0.06f, 0.2f);
            break;
        case Sfx::Match:  // sparkly chime, pitched up for combos
            p.add(L(0, 0.6f, O::Sine, r.jit(1046, 0.004f)).fm(3.5f, 1.8f, 0.2f).dec(0.22f).vol(0.45f))
             .add(L(0, 0.4f, O::Sine, 2093).dec(0.1f).vol(0.2f))
             .add(L(0.04f, 0.5f, O::Sine, 1568).fm(2, 1, 0).dec(0.18f).vol(0.25f))
             .add(L(0, 0.2f, O::Noise).hp(7000).dec(0.05f).vol(0.12f))
             .out(0.45f).echo(0.12f, 0.09f, 0.3f).verb(0.15f, 0.4f);
            break;
        case Sfx::Gull:  // seagull: "kyaa kyaa"
            for (int k = 0; k < 2; k++) {
                float at = k * r.range(0.22f, 0.3f), f = r.jit(1350, 0.06f) * (k ? 0.92f : 1.0f);
                p.add(L(at, 0.2f, O::Saw, f, f * 0.66f).glide(0.7f).vib(0.8f, 30).bp(1900, 1300, 3).drive(1.5f).env(0.01f, 0.06f).vol(0.6f))
                 .add(L(at, 0.2f, O::Saw, f, f * 0.66f).glide(0.7f).vib(0.8f, 30).bp(3200, -1, 5).env(0.01f, 0.06f).vol(0.25f))
                 .add(L(at, 0.1f, O::Noise).bp(2500, -1, 2).env(0.005f, 0.04f).vol(0.1f));
            }
            p.out(0.45f).verb(0.25f, 0.6f);
            break;
        case Sfx::Hammer:  // building a tower: two hammer hits on wood and metal
            for (int k = 0; k < 2; k++) {
                float at = k * 0.13f;
                p.add(L(at, 0.05f, O::Noise).bp(r.range(2600, 3400), -1, 3).dec(0.012f).vol(0.6f))
                 .add(L(at, 0.25f, O::Sine, r.jit(880, 0.04f)).fm(2.76f, 3, 0.5f).dec(0.07f).vol(0.35f))
                 .add(L(at, 0.08f, O::Sine, 200, 120).dec(0.02f).vol(0.5f));
            }
            p.out(0.5f).verb(0.12f, 0.3f);
            break;
        case Sfx::Cannon:  // cheese cannon: a fat thump with a puff
            p.add(L(0, 0.35f, O::Sine, r.jit(110, 0.05f), 40).drop(1.5f, 0.015f).dec(0.09f).vol(1))
             .add(L(0, 0.3f, O::Noise).lp(1800, 300).dec(0.07f).vol(0.6f))
             .add(L(0, 0.03f, O::Noise).hp(3000).vol(0.4f))
             .out(0.6f).verb(0.15f, 0.35f);
            break;
        case Sfx::Zap:  // electric zap
            p.add(L(0, 0.16f, O::Square, r.jit(1800, 0.05f), 260).pw(0.3f).glide(0.4f).dec(0.05f).vol(0.45f))
             .add(L(0, 0.16f, O::Crush, 3000, 600).dec(0.05f).vol(0.35f))
             .add(L(0, 0.05f, O::Noise).hp(5000).dec(0.01f).vol(0.3f))
             .out(0.4f).verb(0.08f, 0.2f);
            break;
        case Sfx::Laser:  // player shot: short, light, never tiring
            p.add(L(0, 0.07f, O::Square, r.jit(1700, 0.04f), 650).pw(0.25f).glide(0.5f).curve(2).lp(5000).vol(0.4f))
             .add(L(0, 0.05f, O::Sine, 2400, 1200).curve(2).vol(0.2f))
             .out(0.25f).verb(0.05f, 0.15f);
            break;
        case Sfx::Horn:  // a ship's horn: next wave incoming
            for (float f : {98.0f, 123.5f, 146.8f})
                p.add(L(0, 1.6f, O::Saw, f).uni(3, 10).lp(900, 700).env(0.15f, 0.5f).dec(2.0f, 0.6f).vol(0.35f));
            p.add(L(0, 1.6f, O::Sine, 49).env(0.15f, 0.5f).vol(0.4f))
             .out(0.55f).verb(0.35f, 0.8f);
            break;
        case Sfx::Shutter:  // camera: mirror slap, shutter clack, film advance
            p.add(L(0, 0.02f, O::Noise).hp(2500).vol(0.7f))
             .add(L(0, 0.05f, O::Sine, r.jit(1900, 0.03f), 900).dec(0.012f).vol(0.5f))
             .add(L(0.065f, 0.03f, O::Noise).bp(3200, -1, 2).dec(0.008f).vol(0.6f))
             .add(L(0.065f, 0.06f, O::Sine, 1300, 700).dec(0.015f).vol(0.4f))
             .add(L(0.12f, 0.22f, O::Square, 55, 70).pw(0.2f).lp(1400).trem(0.7f, 38).env(0.01f, 0.05f).vol(0.12f))
             .out(0.55f).verb(0.06f, 0.2f);
            break;
        case Sfx::Whistle:  // "fwiet-fwiew"
            p.add(L(0, 0.22f, O::Sine, r.jit(1700, 0.03f), 2500).glide(0.7f).vib(0.3f, 9).env(0.02f, 0.05f).vol(0.5f))
             .add(L(0, 0.22f, O::Noise).bp(2200, 2800, 6).env(0.02f, 0.05f).vol(0.08f))
             .add(L(0.28f, 0.35f, O::Sine, r.jit(2500, 0.03f), 1500).glide(0.8f).vib(0.4f, 8).env(0.02f, 0.1f).vol(0.5f))
             .add(L(0.28f, 0.35f, O::Noise).bp(2600, 1700, 6).env(0.02f, 0.1f).vol(0.08f))
             .out(0.5f).verb(0.25f, 0.6f);
            break;
        // ---- Five Nights at Pieter's: beds (loop() overlaps takes by 1.2 s, so they fade at both ends)
        case Sfx::DreadLow:  // the house at night: sub, a groaning low cluster, air moving through the pipes
            p.add(L(0, 8, O::Sine, 36.7f).trem(0.35f, 0.21f).env(1.2f, 1.2f).curve(0).vol(0.55f))
             .add(L(0, 8, O::Saw, 73.4f).uni(3, 14).lp(190, 260).env(1.2f, 1.2f).curve(0).vol(0.3f))
             .add(L(0, 8, O::Saw, 77.8f).uni(3, 18).lp(210, 170).trem(0.5f, 0.13f).env(1.2f, 1.2f).curve(0).vol(0.22f))
             .add(L(0, 8, O::Pink).bp(r.range(160, 220), r.range(300, 420), 2.5f).trem(0.6f, r.range(0.15f, 0.3f)).env(1.2f, 1.2f).curve(0).vol(0.4f))
             .add(L(r.range(1, 3), 4, O::Sine, r.jit(293, 0.02f), 277).fm(2.76f, 1.2f, 0.3f).env(1.5f, 1.5f).vol(0.05f))
             .out(0.45f).verb(0.35f, 0.9f, 0.6f).asBed();
            break;
        case Sfx::DreadHigh:  // something is close: tremolo strings in a cluster, a whistling harmonic, a pulse under it all
            for (float m : {72.0f, 73.0f, 78.0f, 79.0f})
                p.add(L(0, 8, O::Saw, hz(m) * r.jit(1, 0.002f)).uni(3, 12).bp(hz(m) * 2, -1, 1.2f).trem(0.7f, r.range(10, 13)).vib(0.08f, 5)
                          .env(1.2f, 1.2f).curve(0).vol(0.18f));
            p.add(L(0, 8, O::Sine, hz(91), hz(90.4f)).vib(0.2f, 0.4f).env(1.5f, 1.2f).curve(0).vol(0.04f))
             .add(L(0, 8, O::Saw, 49).uni(4, 25).lp(140).trem(0.8f, 1.6f).env(1.2f, 1.2f).curve(0).vol(0.45f))
             .add(L(0, 8, O::Noise).bp(5200, 4800, 3).trem(0.6f, 0.5f).env(1.2f, 1.2f).curve(0).vol(0.05f))
             .out(0.42f).verb(0.4f, 0.8f).asBed();
            break;
        case Sfx::Hum:  // 50 Hz mains hum from the charger and the PC
            p.add(L(0, 6, O::Sine, 50).env(1.2f, 1.2f).curve(0).vol(0.5f))
             .add(L(0, 6, O::Sine, 100).env(1.2f, 1.2f).curve(0).vol(0.35f))
             .add(L(0, 6, O::Saw, 150).lp(600).env(1.2f, 1.2f).curve(0).vol(0.12f))
             .add(L(0, 6, O::Noise).bp(9000, -1, 4).trem(0.5f, 0.7f).env(1.2f, 1.2f).curve(0).vol(0.03f))
             .out(0.3f).asBed();
            break;
        case Sfx::Choir:  // a choir of nobody, singing "aah" in d minor
            for (float m : {50.0f, 57.0f, 62.0f, 65.0f, 69.0f})
                for (float fm : {700.0f, 1150.0f})
                    p.add(L(0, 8, O::Saw, hz(m)).uni(4, 16).vib(0.12f, r.range(4.5f, 5.5f)).bp(fm, fm * r.range(0.9f, 1.1f), 5).env(1.2f, 1.2f).curve(0)
                              .vol(fm < 1000 ? 0.22f : 0.12f));
            p.out(0.38f).verb(0.6f, 0.95f, 0.5f).asBed();
            break;
        // ---- Five Nights at Pieter's: one-shots
        case Sfx::ClockTick:  // the staande klok in the hall: a dry wooden escapement tick
            p.add(L(0, 0.03f, O::Noise).bp(r.jit(3200, 0.05f), -1, 3).dec(0.006f).vol(0.6f))
             .add(L(0, 0.06f, O::Sine, r.jit(1750, 0.02f)).fm(2.3f, 1.5f).dec(0.012f).vol(0.35f))
             .add(L(0, 0.2f, O::Sine, 420).dec(0.05f).vol(0.12f))
             .out(0.4f).verb(0.45f, 0.75f, 0.5f);
            break;
        case Sfx::Rattle:  // someone trying the doorknob
            for (int i = 0; i < 9; i++) {
                float at = i * r.range(0.045f, 0.075f) + (i > 4 ? 0.18f : 0);
                p.add(L(at, 0.05f, O::Sine, r.range(2200, 3000)).fm(1.41f, 3).dec(0.01f).vol(0.35f))
                 .add(L(at, 0.03f, O::Noise).bp(r.range(3500, 5000), -1, 3).dec(0.006f).vol(0.4f))
                 .add(L(at, 0.06f, O::Sine, r.range(500, 700)).dec(0.015f).vol(0.2f));
            }
            p.out(0.5f).verb(0.2f, 0.4f);
            break;
        case Sfx::Thud:  // a shoulder against the door
            p.add(L(0, 0.45f, O::Sine, r.jit(72, 0.08f), 38).drop(1, 0.02f).dec(0.12f).vol(1))
             .add(L(0, 0.3f, O::Noise).lp(900, 200).dec(0.06f).vol(0.6f))
             .add(L(0, 0.35f, O::Noise).bp(r.jit(190, 0.1f), -1, 6).dec(0.09f).vol(0.5f))
             .add(L(0.01f, 0.25f, O::Crush, 400, 120).dec(0.04f).vol(0.15f))
             .out(0.8f).verb(0.2f, 0.45f, 0.6f);
            break;
        case Sfx::StepFar:  // footsteps somewhere else in the house, through the walls
            for (int k = 0; k < 2; k++) {
                float at = k * r.range(0.45f, 0.6f);
                p.add(L(at, 0.14f, O::Noise).lp(r.jit(380, 0.15f), 150).dec(0.04f).vol(0.6f))
                 .add(L(at, 0.16f, O::Sine, 80, 50).dec(0.05f).vol(0.7f));
            }
            p.out(0.45f).verb(0.55f, 0.85f, 0.7f);
            break;
        case Sfx::Flee: {  // running away down the hall, fading into the house
            float at = 0;
            for (int i = 0; i < 7; i++) {
                float g = 1 - i / 8.0f;
                p.add(L(at, 0.08f, O::Noise).lp(lerpf(1400, 350, i / 7.0f)).dec(0.025f).vol(0.6f * g))
                 .add(L(at, 0.09f, O::Sine, 110, 55).dec(0.025f).vol(0.6f * g));
                at += r.range(0.15f, 0.19f);
            }
            p.out(0.55f).verb(0.45f, 0.8f, 0.5f);
            break;
        }
        case Sfx::Snort:  // a big horse blowing through its nostrils, inside a wardrobe
            p.add(L(0, 0.7f, O::Noise).bp(700, 260, 2).trem(0.6f, r.range(26, 34)).env(0.03f, 0.25f).vol(0.8f))
             .add(L(0, 0.7f, O::Saw, r.jit(64, 0.1f), 48).uni(2, 30).lp(300).drive(1.5f).trem(0.4f, 30).env(0.03f, 0.25f).vol(0.35f))
             .add(L(0.75f, 0.5f, O::Noise).bp(400, 900, 1.5f).env(0.2f, 0.2f).vol(0.3f))
             .out(0.55f).verb(0.15f, 0.25f, 0.7f);
            break;
        case Sfx::Squeak:  // a plush toy being squeezed, slightly wrong
            p.add(L(0, 0.22f, O::Sine, r.jit(1250, 0.08f), r.jit(1700, 0.08f)).glide(0.4f).vib(0.8f, 23).bp(1600, -1, 2).env(0.01f, 0.06f).vol(0.6f))
             .add(L(0, 0.22f, O::Noise).bp(2400, -1, 4).env(0.01f, 0.06f).vol(0.15f))
             .add(L(0.22f, 0.15f, O::Sine, r.jit(1500, 0.05f), 900).bp(1200, -1, 2).env(0.01f, 0.06f).vol(0.35f))
             .out(0.4f).verb(0.25f, 0.4f);
            break;
        case Sfx::PhoneBuzz:  // a phone vibrating on the nightstand
            for (int k = 0; k < 2; k++)
                p.add(L(k * 0.5f, 0.32f, O::Square, 175).pw(0.4f).lp(450).env(0.01f, 0.03f).curve(0).vol(0.5f))
                 .add(L(k * 0.5f, 0.32f, O::Noise).bp(2200, -1, 2).trem(0.9f, 175).env(0.01f, 0.03f).curve(0).vol(0.12f));
            p.out(0.45f).verb(0.08f, 0.2f);
            break;
        case Sfx::Birds: {  // dawn chorus: a blackbird and a few sparrows
            float at = 0.2f;
            for (int i = 0; i < 9; i++) {  // the merel's fluting phrase
                float f = r.range(1600, 2800);
                p.add(L(at, r.range(0.08f, 0.2f), O::Sine, f, f * r.range(0.8f, 1.3f)).vib(0.5f, r.range(20, 40)).env(0.01f, 0.04f).pan(-0.5f).vol(0.35f));
                at += r.range(0.1f, 0.25f) + (i == 4 ? 0.8f : 0);
            }
            for (int i = 0; i < 16; i++) {  // sparrow chirps all around
                float t0 = r.range(0, 5), f = r.range(3800, 5200);
                p.add(L(t0, 0.05f, O::Sine, f, f * 0.7f).env(0.004f, 0.02f).pan(r.bi() * 0.8f).vol(r.range(0.1f, 0.25f)));
            }
            p.add(L(0, 6, O::Pink).hp(2000).env(1.2f, 1.2f).curve(0).vol(0.05f))
             .out(0.4f).verb(0.3f, 0.7f);
            break;
        }
        case Sfx::Alarm:  // the 6 AM alarm clock: beep-beep-beep-beep
            for (int k = 0; k < 3; k++)
                for (int i = 0; i < 4; i++)
                    p.add(L(k * 0.9f + i * 0.12f, 0.07f, O::Square, 2050).pw(0.5f).lp(5000).env(0.002f, 0.01f).curve(0).vol(0.45f));
            p.out(0.4f).verb(0.12f, 0.3f);
            break;
        case Sfx::Roar:  // Davi's jumpscare: a chest roar torn into distortion
            for (float fm : {420.0f, 900.0f, 2300.0f})
                p.add(L(0, 1.6f, O::Saw, r.jit(140, 0.05f), 85).glide(0.5f).uni(5, 60).vib(1.2f, 14).bp(fm, fm * 0.8f, 2.5f).drive(4).env(0.004f, 0.4f)
                          .vol(fm < 600 ? 0.8f : fm < 1000 ? 0.6f : 0.3f));
            p.add(L(0, 1.5f, O::Saw, 46, 30).uni(4, 40).drive(5).lp(500, 180).env(0.004f, 0.4f).vol(0.7f))
             .add(L(0, 1.2f, O::Noise).bp(1500, 700, 0.8f).drive(2).env(0.004f, 0.4f).vol(0.5f))
             .add(L(0, 0.9f, O::Sine, 100, 26).drop(2, 0.03f).dec(0.35f).vol(1))
             .add(L(0, 0.5f, O::Noise).hp(4000).dec(0.12f).vol(0.4f))
             .out(0.98f).verb(0.2f, 0.5f);
            break;
        case Sfx::Shriek:  // Kira's jumpscare: a little girl's scream sliding up into static
            for (float fm : {1100.0f, 2700.0f, 3600.0f})
                p.add(L(0, 1.5f, O::Saw, r.jit(880, 0.04f), 1320).glide(0.4f).uni(4, 50).vib(2, 17).bp(fm, fm * 1.1f, 4).drive(2.5f).env(0.004f, 0.35f)
                          .vol(fm < 2000 ? 0.7f : 0.45f));
            p.add(L(0, 1.4f, O::Crush, 3000, 900).env(0.004f, 0.4f).vol(0.25f))
             .add(L(0, 0.8f, O::Sine, 90, 30).drop(1.5f, 0.03f).dec(0.3f).vol(0.9f))
             .add(L(0.05f, 1.2f, O::Noise).bp(3000, 1800, 1).env(0.004f, 0.4f).vol(0.35f))
             .out(0.97f).echo(0.12f, 0.09f, 0.3f).verb(0.25f, 0.6f);
            break;
        case Sfx::HorseScream:  // Het Nachtpaard: a whinny pitched down into a scream
            for (float fm : {650.0f, 1500.0f, 2900.0f})
                p.add(L(0, 1.7f, O::Saw, r.jit(520, 0.04f), 260).glide(1.4f).uni(5, 50).vib(3.5f, 11).bp(fm, fm * 0.7f, 3).drive(3.5f).env(0.004f, 0.45f)
                          .vol(fm < 1000 ? 0.75f : fm < 2000 ? 0.5f : 0.3f));
            p.add(L(0, 1.6f, O::Saw, 41, 27).uni(4, 30).drive(5).lp(450, 160).env(0.004f, 0.5f).vol(0.7f))
             .add(L(0, 1.0f, O::Sine, 85, 24).drop(2, 0.03f).dec(0.4f).vol(1))
             .add(L(0, 0.35f, O::Noise).lp(3000, 400).dec(0.1f).vol(0.6f))  // hooves through the door
             .out(0.98f).verb(0.3f, 0.7f);
            break;
        case Sfx::LowBatt:  // flashlight battery warning
            p.add(L(0, 0.12f, O::Sine, 1480).env(0.004f, 0.03f).curve(0).vol(0.4f))
             .add(L(0.16f, 0.2f, O::Sine, 1040).env(0.004f, 0.06f).curve(0).vol(0.4f))
             .out(0.3f).verb(0.1f, 0.2f);
            break;
        case Sfx::Glitch:  // digital tearing
            for (int i = 0; i < 8; i++) {
                float at = r.range(0, 0.3f);
                p.add(L(at, r.range(0.02f, 0.06f), O::Crush, r.range(300, 6000)).env(0.001f, 0.004f).curve(0).pan(r.bi() * 0.8f).vol(r.range(0.3f, 0.6f)))
                 .add(L(at, 0.04f, O::Square, r.range(80, 900)).pw(r.range(0.1f, 0.5f)).env(0.001f, 0.004f).curve(0).vol(0.2f));
            }
            p.out(0.5f);
            break;
        case Sfx::Impact:  // the cinematic hit
            for (float f : {32.7f, 65.4f, 98.0f, 130.8f})
                p.add(L(0, 3.5f, O::Saw, f * r.jit(1, 0.004f)).uni(4, 18).drive(2).lp(1600, 120).env(0.01f, 1.0f).dec(1.2f, 0.1f).vol(f < 50 ? 0.5f : 0.3f));
            p.add(L(0, 1.6f, O::Sine, 70, 28).drop(1.5f, 0.04f).dec(0.5f).vol(1))
             .add(L(0, 0.6f, O::Noise).lp(4000, 200).dec(0.15f).vol(0.6f))
             .out(0.9f).verb(0.4f, 0.9f, 0.5f);
            break;
        case Sfx::Swell:  // a reversed cymbal and a rising cluster that cuts dead
            p.add(L(0, 2.2f, O::Noise).hp(1500, 5000).env(2.15f, 0.01f).curve(0).vol(0.4f))
             .add(L(0, 2.2f, O::Saw, 98, 147).glide(2).uni(6, 35).lp(300, 3000).env(2.1f, 0.01f).curve(0).vol(0.35f))
             .add(L(0, 2.2f, O::Sine, 49, 55).env(2.0f, 0.01f).curve(0).vol(0.4f))
             .out(0.6f);
            break;
        // ---- Meeuwen!
        case Sfx::Flap:  // two quick wingbeats
            for (int k = 0; k < 2; k++)
                p.add(L(k * 0.1f, 0.09f, O::Noise).bp(r.jit(720, 0.1f) * (k ? 0.85f : 1), 320, 1.3f).env(0.012f, 0.05f).vol(k ? 0.35f : 0.5f))
                 .add(L(k * 0.1f, 0.06f, O::Sine, 160, 90).env(0.005f, 0.03f).vol(0.15f));
            p.out(0.35f).verb(0.05f, 0.2f);
            break;
        case Sfx::Splat:  // a wet little present landing
            p.add(L(0, 0.22f, O::Noise).lp(r.jit(2600, 0.1f), 350).dec(0.05f).vol(0.8f))
             .add(L(0, 0.12f, O::Sine, r.jit(320, 0.08f), 90).glide(0.5f).dec(0.03f).vol(0.6f))
             .add(L(0.03f, 0.08f, O::Noise).bp(900, 400, 3).dec(0.02f).vol(0.3f));
            for (int i = 0; i < 3; i++) {
                float f = r.range(500, 1100);
                p.add(L(r.range(0.05f, 0.18f), 0.04f, O::Sine, f, f * 0.6f).dec(0.012f).vol(0.12f));
            }
            p.out(0.5f).verb(0.08f, 0.25f);
            break;
        case Sfx::Surf:  // bed: waves rolling in and out, far-off gulls
            p.add(L(0, 8, O::Pink).lp(700, 1300).trem(0.65f, 0.125f).env(1.2f, 1.2f).curve(0).vol(0.7f))
             .add(L(0, 8, O::Noise).hp(2500).trem(0.8f, 0.125f).env(1.2f, 1.2f).curve(0).vol(0.1f))
             .add(L(0, 8, O::Pink).lp(220).env(1.2f, 1.2f).curve(0).vol(0.35f));
            for (int k = 0; k < 2; k++) {
                float at = r.range(1.5f, 6.0f), f = r.jit(1500, 0.08f);
                p.add(L(at, 0.18f, O::Saw, f, f * 0.66f).glide(0.7f).vib(0.8f, 30).bp(1900, 1300, 3).env(0.01f, 0.06f).pan(r.bi() * 0.7f).vol(0.05f));
            }
            p.out(0.4f).verb(0.25f, 0.7f).asBed();
            break;
        // ---- Dakkenloper
        case Sfx::Steam:  // a chimney vent puffing hot steam
            p.add(L(0, 0.9f, O::Noise).hp(r.jit(2200, 0.1f), 4000).env(0.03f, 0.4f).dec(0.35f, 0.2f).vol(0.6f))
             .add(L(0, 0.9f, O::Noise).bp(900, 600, 1.2f).env(0.05f, 0.4f).dec(0.4f).vol(0.3f))
             .add(L(0, 0.08f, O::Sine, 160, 80).dec(0.03f).vol(0.3f))
             .out(0.4f).verb(0.12f, 0.4f);
            break;
        case Sfx::Boing:  // a striped awning used as a trampoline
            p.add(L(0, 0.45f, O::Sine, r.jit(180, 0.05f), 520).glide(0.5f).vib(1.2f, 14).dec(0.18f).vol(0.7f))
             .add(L(0, 0.3f, O::Tri, 360, 1040).glide(0.5f).dec(0.1f).vol(0.25f))
             .add(L(0, 0.05f, O::Noise).lp(1200).dec(0.015f).vol(0.3f))
             .out(0.45f).verb(0.1f, 0.3f);
            break;
        case Sfx::Coo:  // a pigeon: "roo-coo"
            for (int k = 0; k < 2; k++) {
                float at = k * 0.24f, f = r.jit(420, 0.05f) * (k ? 1.12f : 1.0f);
                p.add(L(at, 0.22f, O::Sine, f, f * 0.82f).vib(0.6f, 22).env(0.03f, 0.08f).vol(0.5f))
                 .add(L(at, 0.22f, O::Pink).bp(f * 1.5f, -1, 4).env(0.03f, 0.08f).vol(0.15f));
            }
            p.out(0.35f).verb(0.15f, 0.4f);
            break;
        // ---- Pannenkoekenhuis
        case Sfx::Sizzle:  // bed: butter and batter on a hot pan
            p.add(L(0, 6, O::Noise).hp(2500).bp(5200, -1, 0.8f).trem(0.25f, 7).env(1.0f, 1.0f).curve(0).vol(0.45f))
             .add(L(0, 6, O::Pink).bp(1400, -1, 1.2f).trem(0.4f, 3.3f).env(1.0f, 1.0f).curve(0).vol(0.2f));
            for (int i = 0; i < 160; i++) {  // fat popping
                float at = r.range(0.2f, 5.6f);
                p.add(L(at, 0.012f, O::Noise).hp(r.range(2500, 7000)).curve(2).pan(r.bi() * 0.6f).vol(r.range(0.1f, 0.45f)));
            }
            p.out(0.4f).verb(0.05f, 0.2f).asBed();
            break;
        case Sfx::Pour:  // a ladle of batter hitting the pan: a wet slap, then a hiss
            p.add(L(0, 0.12f, O::Noise).lp(1500, 400).dec(0.04f).vol(0.7f))
             .add(L(0, 0.1f, O::Sine, r.jit(220, 0.06f), 110).dec(0.03f).vol(0.4f))
             .add(L(0.05f, 0.7f, O::Noise).hp(3000).env(0.05f, 0.3f).dec(0.3f).vol(0.4f))
             .out(0.45f).verb(0.06f, 0.2f);
            break;
        case Sfx::Register:  // ka-ching
            p.add(L(0, 0.05f, O::Noise).bp(2500, -1, 2).dec(0.015f).vol(0.6f))
             .add(L(0.04f, 0.06f, O::Square, 180, 120).pw(0.3f).dec(0.02f).vol(0.25f));
            for (float f : {2093.0f, 2637.0f, 3136.0f})
                p.add(L(0.09f, 0.8f, O::Sine, f * r.jit(1, 0.005f)).fm(3.5f, 1.2f, 0.1f).dec(0.25f).vol(0.25f));
            p.out(0.45f).verb(0.15f, 0.4f);
            break;
        case Sfx::Count: break;
    }
    if (p.layers.empty()) p.add(L(0, 0.01f, O::Sine).vol(0));
    return p;
}

}  // namespace

const char* name(Sfx s) {
    static const char* names[] = {"click", "hover", "coin", "hurt", "power", "win", "lose", "dash", "boom", "card", "tick", "gem",
                                  "neigh", "chew", "bubble", "whoosh", "drop", "perfect", "jump", "bell", "spawn", "shoot", "error", "kiss",
                                  "pop", "snore", "water", "yawn", "boop", "gulp", "buzz", "msg", "send", "key",
                                  "breath", "giggle", "heart", "step", "slam", "flash", "scream", "static", "musicbox", "drone", "creak",
                                  "klomp", "fietsbel", "carillon", "rain", "wind", "thunder", "whisper", "knock", "monkey", "cymbal", "tinnitus",
                                  "scratch", "riser", "sting",
                                  "putt", "cup", "splash", "swap", "match", "gull", "hammer", "cannon", "zap", "laser", "horn", "shutter", "whistle",
                                  "dreadlow", "dreadhigh", "hum", "clocktick", "rattle", "thud", "stepfar", "flee", "snort", "squeak", "phonebuzz",
                                  "birds", "alarm", "roar", "shriek", "horsescream", "lowbatt", "glitch", "impact", "swell", "choir",
                                  "flap", "splat", "surf", "steam", "boing", "coo", "sizzle", "pour", "register"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)Sfx::Count);
    return names[(int)s];
}

Pcm render(Sfx s, int variant) {
    Rng rng((uint32_t)s * 977u + (uint32_t)variant * 131u + 7u);
    Patch p = design(s, rng);
    return bounce(p, rng);
}

}  // namespace pg::synth
