// FIVE NIGHTS AT PIETER'S - Halloween special.
// You are Pieter, alone in the house from 12 AM to 6 AM. No cameras: run to the
// doors, the closet and the bed, listen, and use the flashlight.
//   Davi  - left hallway. Light scares him off while he's in the hall. At the
//           door you only HEAR him breathing: hold the door, never shine.
//   Kira  - right hallway, same rules, but she giggles instead of breathing,
//           sometimes hides from the light, and hacks the PC: a music box
//           starts playing and everyone moves faster until you switch it off.
//   Het Nachtpaard - grows in the closet while you're not looking. Hold the
//           closet shut to push him back. If the doors swing open, run.
//   Paardjes - plush horses that gather on the bed. Light chases them off;
//           three at once and they call the horse.
//   Monkey Man - (night 2+) at the bedroom window. Don't stare at him.
//   Lures  - throw a stroopwafel (Davi) or a boba (Kira) down the hall.
//   The other one - a far too tall Pieter with holes for eyes. He never
//           attacks. He only stands there, a little closer every night, and he
//           does not want to be looked at. Put the torch on him four times
//           in one night and he comes to you.
// On top of the monsters the house itself is wrong, more so every night (and
// every time Pieter dies): silences that lead nowhere, breathing right behind
// you when you stand still too long, someone under the blanket, a reflection
// in the dark monitor, a hand on the door that isn't yours, a 6 AM that isn't
// real, and a phone that knows who is playing and what time it is.
// The flashlight drains its battery and charges in its dock on the desk, and
// every noise you make carries through the house. How the monsters think lives
// in game/fnap/Brain; how they look in game/fnap/Art.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/RoomArt.hpp"
#include "game/Scenes.hpp"
#include "game/fnap/Art.hpp"
#include "game/fnap/Brain.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace pg {
namespace {

using fnap::Brain;
using fnap::Ev;
using fnap::kHourLen;
using fnap::View;
using fnap::Who;
namespace gfx = fnap::gfx;

enum class Phase { Title, Cinematic, Note, Play, Scare, GameOver, Dawn, FakeDawn };
enum class Src { LeftHall, RightHall, Closet, Bed, Window, Desk };

// Texts that come in on Pieter's phone during the night: story, and hints.
struct Text { float at; const char* from; const char* msg; };
const std::vector<Text> kTexts[fnap::kNights] = {
    {{8, "Mama", "Papa and I are at Oma's in Zwolle until Saturday. Stroopwafels are in the kast. Lock the doors! x"},
     {50, "Davi", "yo pieter u awake? coming over to borrow ur charger"},
     {95, "Davi", "nvm im already inside"},
     {150, "???", "your flashlight charges in the dock on your desk. don't let it die."},
     {215, "Mama", "Slaap lekker lieverd!"}},
    {{8, "Kira", "hiii pieter :) i fixed ur pc for u. it's gonna play u a song"},
     {70, "Mama", "Did you water the tulips?"},
     {130, "???", "the longer you ignore a door, the braver they get."},
     {200, "Kira", "we can hear u running around pieter. hihihi"}},
    {{8, "???", "something big lives in your closet now. when the doors bulge, hold them."},
     {75, "Davi", "why do u keep shining that thing at me. it makes me ANGRY"},
     {140, "???", "she hides in the bathroom when you shine at her too much."},
     {205, "Mama", "Oma says hoi! Are you eating vegetables?"}},
    {{8, "???", "they don't always leave when they walk away. listen before you look."},
     {60, "Kira", "i like ur plushies. i brought more"},
     {125, "???", "when the house goes quiet, get ready."},
     {210, "Davi", "almost morning pieter. almost."}},
    {{8, "Mama", "Coming home early tomorrow!! 6 AM train. Hou vol lieverd."},
     {80, "???", "the clock stops when they're about to come."},
     {150, "Kira", "last night pieter. lets make it count"},
     {220, "Davi", "JUHUH"}},
    {{8, "???", "JUHUH"}, {100, "???", "JUHUH JUHUH"}, {200, "Pieter", "(this is from me. I'm scared.)"}},
};

constexpr float kCinematicLen = 7.0f;

// The whole night is drawn into a texture first, then through this: colour drains
// out of the room, the dark gets crushed, the edges close in and crawl, and the
// picture breathes very slightly, as if the room were.
const char* kGradeFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float uTime, uDesat, uCrush, uVig, uWarp, uSplit, uGrain;
uniform vec3 uTint;
uniform vec2 uRes;
out vec4 finalColor;
float h(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec2 uv = fragTexCoord;
    vec2 c = uv - 0.5;
    float r = length(c * vec2(uRes.x / uRes.y, 1.0));
    uv += c * uWarp * (0.5 + 0.5 * sin(uTime * 0.85)) * r * r * 0.1;
    uv.x += sin(uv.y * 7.0 + uTime * 1.1) * 0.0009 * uWarp;
    float s = uSplit * (0.3 + r * 1.8);
    vec3 col = vec3(texture(texture0, uv + c * s).r, texture(texture0, uv).g, texture(texture0, uv - c * s).b);
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(l) * uTint, uDesat);
    col = pow(max(col, vec3(0.0)), vec3(uCrush));
    float v = 1.0 - smoothstep(0.3 - uVig * 0.25, 0.98 - uVig * 0.5, r);
    float crawl = h(floor(uv * uRes / 4.0) + floor(uTime * 10.0));
    v *= mix(1.0, 0.8 + 0.2 * crawl, smoothstep(0.35, 0.95, r));
    col *= v;
    col += (h(uv * uRes + fract(uTime) * 97.0) - 0.5) * uGrain;
    finalColor = vec4(col, 1.0);
}
)";

// Who is playing, and what time it really is. The phone knows both.
std::string userName() {
    const char* u = std::getenv("USER");
    if (!u || !*u) u = std::getenv("USERNAME");
    std::string n = u && *u ? u : "you";
    if (n.size() > 16) n = n.substr(0, 16);
    return n;
}
std::string realClock() {
    std::time_t now = std::time(nullptr);
    char b[8] = "00:00";
    if (std::tm* lt = std::localtime(&now)) std::strftime(b, sizeof b, "%H:%M", lt);
    return b;
}
std::string fill(std::string s) {
    for (auto [key, val] : {std::pair<const char*, std::string>{"{USER}", userName()}, {"{TIME}", realClock()}})
        for (size_t at; (at = s.find(key)) != std::string::npos;) s.replace(at, std::string(key).size(), val);
    return s;
}

// Things that flash up for two frames.
const char* kSubText[] = {"BEHIND YOU", "IT IS NOT MORNING", "SOMEONE IS IN THE BED", "DON'T LOOK AT HIM", "HE IS WEARING YOUR FACE", "{USER}", "WHY ARE YOU STILL HERE"};

class FnapScene : public Scene {
public:
    ~FnapScene() override {
        if (noise_.id) UnloadTexture(noise_);
        if (lm_.id) UnloadRenderTexture(lm_);
        if (rt_.id) UnloadRenderTexture(rt_);
        if (viewRt_.id) UnloadRenderTexture(viewRt_);
        if (grade_.id) UnloadShader(grade_);
    }

    MusicMode music() const override { return MusicMode::None; }

    Presence presence() const override {
        if (phase_ == Phase::Title) return {"Five Nights at Pieter's", "Too scared to start"};
        return {"Five Nights at Pieter's", "Night " + std::to_string(night_ + 1) + " - " + clockText()};
    }

    void enter() override {
        gfx::initLight();
        Image ni = GenImageWhiteNoise(256, 256, 0.5f);
        noise_ = LoadTextureFromImage(ni);
        UnloadImage(ni);
        SetTextureWrap(noise_, TEXTURE_WRAP_REPEAT);
        lm_ = LoadRenderTexture(VW / 2, VH / 2);
        SetTextureFilter(lm_.texture, TEXTURE_FILTER_BILINEAR);
        Vector2 cs = E().canvasSize();
        rt_ = LoadRenderTexture((int)cs.x, (int)cs.y);
        SetTextureFilter(rt_.texture, TEXTURE_FILTER_BILINEAR);
        BeginTextureMode(rt_);
        ClearBackground(BLACK);
        EndTextureMode();
        viewRt_ = LoadRenderTexture((int)cs.x, (int)cs.y);
        SetTextureFilter(viewRt_.texture, TEXTURE_FILTER_BILINEAR);
        BeginTextureMode(viewRt_);
        ClearBackground(BLACK);
        EndTextureMode();
        grade_ = LoadShaderFromMemory(nullptr, kGradeFs);
        const char* names[] = {"uTime", "uDesat", "uCrush", "uVig", "uWarp", "uSplit", "uGrain", "uTint", "uRes"};
        for (int i = 0; i < 9; i++) gradeLoc_[i] = GetShaderLocation(grade_, names[i]);
        env_.horror = true;
        env_.lights = 0;
        env_.outside = [this] { outsideWindow(); };
        env_.hour = 1;
        devHooks();
    }

    void exit() override {
        for (Sfx x : {Sfx::Exhale, Sfx::Lullaby, Sfx::Drip, Sfx::Barefoot, Sfx::Sub}) E().sfx.stop(x);
        for (Sfx x : {Sfx::MusicBox, Sfx::Drone, Sfx::Breath, Sfx::Giggle, Sfx::Heart, Sfx::Static, Sfx::Scream, Sfx::Creak, Sfx::Step, Sfx::Rain,
                      Sfx::Wind, Sfx::Thunder, Sfx::Carillon, Sfx::Whisper, Sfx::Riser, Sfx::Sting, Sfx::Tinnitus, Sfx::Monkey, Sfx::DreadLow,
                      Sfx::DreadHigh, Sfx::Hum, Sfx::Choir, Sfx::Birds, Sfx::Alarm, Sfx::Impact, Sfx::Swell, Sfx::Roar, Sfx::Shriek,
                      Sfx::HorseScream, Sfx::Flee, Sfx::Snort})
            E().sfx.stop(x);
        gfx::shutdownLight();
        P().save();
    }

    // ============================================================ dev hooks
    // PG_FNAP=<night 1-6> starts playing at once, PG_FNAP_VIEW=center|left|right|closet|bed,
    // PG_FNAP_STAGE=davi,kira,horse,plush,pc (davi/kira 0-4, horse 0-4, plush 0-3), PG_FNAP_LIGHT=1,
    // PG_FNAP_HOLD=1, PG_FNAP_SCARE=0..3 (+PG_FNAP_SCARE_T), PG_FNAP_INTRO=1 (cinematic, +PG_FNAP_CINE_T),
    // PG_FNAP_DAWN=1 (+PG_FNAP_DAWN_T), PG_FNAP_OVER=0..3, PG_FNAP_HIDE=1, PG_FNAP_MONKEY=1, PG_FNAP_BOLT=1,
    // PG_FNAP_T=seconds, PG_FNAP_BATT=0..1, PG_FNAP_TEXT=1, PG_FNAP_TITLE_BOLT=1
    void devHooks() {
        if (std::getenv("PG_FNAP_TITLE_BOLT")) devBolt_ = true;
        const char* nStr = std::getenv("PG_FNAP");
        if (!nStr) return;
        startNight(std::clamp(std::atoi(nStr), 1, 6) - 1);
        phase_ = Phase::Play;
        if (const char* v = std::getenv("PG_FNAP_VIEW")) {
            std::string s = v;
            view_ = s == "left" ? View::Left : s == "right" ? View::Right : s == "closet" ? View::Closet : s == "bed" ? View::Bed : View::Center;
        }
        if (const char* st = std::getenv("PG_FNAP_STAGE")) {
            int d = 0, k = 0, h = 0, b = 0, pc = 0;
            std::sscanf(st, "%d,%d,%d,%d,%d", &d, &k, &h, &b, &pc);
            b_.hall[0].pos = d;
            b_.hall[1].pos = k;
            b_.horse = h;
            b_.plush = b;
            b_.pc = pc != 0;
            for (auto& a : b_.plushAge) a = 20;
            b_.frozen = true;
            prevPos_[0] = d;
            prevPos_[1] = k;
        }
        devLight_ = std::getenv("PG_FNAP_LIGHT") != nullptr;
        devHold_ = std::getenv("PG_FNAP_HOLD") != nullptr;
        if (std::getenv("PG_FNAP_HIDE")) b_.hall[1].hidden = true;
        if (const char* tt = std::getenv("PG_FNAP_T")) b_.t = (float)std::atof(tt);
        if (const char* bt = std::getenv("PG_FNAP_BATT")) b_.battery = (float)std::atof(bt);
        if (std::getenv("PG_FNAP_MONKEY")) b_.monkey = true, monkeyRise_ = 1;
        if (std::getenv("PG_FNAP_BOLT")) devBolt_ = true;
        if (std::getenv("PG_FNAP_TEXT")) showText(kTexts[night_][0]), textT_ = 1;
        if (std::getenv("PG_FNAP_INTRO")) {
            phase_ = Phase::Cinematic;
            if (const char* c = std::getenv("PG_FNAP_CINE_T")) phaseT_ = (float)std::atof(c);
        }
        if (const char* sc = std::getenv("PG_FNAP_SCARE")) {
            jumpscare((Who)std::clamp(std::atoi(sc), 0, 4));
            if (const char* c = std::getenv("PG_FNAP_SCARE_T")) phaseT_ = (float)std::atof(c);
        }
        if (const char* o = std::getenv("PG_FNAP_OVER")) {
            b_.killer = (Who)std::clamp(std::atoi(o), 0, 4);
            scareWho_ = b_.killer;
            b_.cause = "lit at door";
            phase_ = Phase::GameOver;
            phaseT_ = 2;
        }
        // PG_FNAP_OTHER=<spot 0-3> puts him in the starting view; PG_FNAP_DREAD=bed|reflect|hand|behind|fake|ceiling|turned
        if (const char* o = std::getenv("PG_FNAP_OTHER")) {
            dr_.spot = std::clamp(std::atoi(o), 0, 3);
            dr_.at = view_;
            dr_.fade = 1;
            dr_.lastView = view_;
        }
        if (const char* d = std::getenv("PG_FNAP_DREAD")) {
            std::string k = d;
            dr_.lastView = view_;
            if (k == "bed") dr_.bedShape = true;
            if (k == "reflect") dr_.reflectT = 2;
            if (k == "hand") dr_.handT = 1.0f, dr_.handDone = true;
            if (k == "behind") dr_.behindT = 7.5f, dr_.stillT = 30;
            if (k == "ceiling") dr_.ceiling[1] = true;
            if (k == "turned") dr_.turned[0] = true;
            if (k == "fake") phase_ = Phase::FakeDawn, phaseT_ = std::getenv("PG_FNAP_DAWN_T") ? (float)std::atof(std::getenv("PG_FNAP_DAWN_T")) : 3.5f;
        }
        if (std::getenv("PG_FNAP_DAWN")) {
            winNight();
            if (const char* c = std::getenv("PG_FNAP_DAWN_T")) phaseT_ = (float)std::atof(c);
        }
    }

    // ============================================================ night flow
    void startNight(int n) {
        night_ = n;
        b_ = Brain((uint32_t)(GetTime() * 1000) + n * 77);
        b_.start(n);
        view_ = View::Center;
        moveT_ = 0;
        fx_.clear();
        phase_ = Phase::Cinematic;
        phaseT_ = 0;
        introNote_ = n == 0 && !(P().fnapBeaten & 1);
        prevPos_[0] = prevPos_[1] = 0;
        textIdx_ = 0;
        textT_ = -1;
        monkeyRise_ = monkeyBang_ = 0;
        clashAnim_ = 0;
        hushT_ = -1;
        doorAnim_ = 0;
        heartT_ = 0;
        clockT_ = 0;
        plushFall_ = -1;
        thunderT_ = 1.2f;  // the cinematic's lightning
        boltT_ = -1;
        E().sfx.play(Sfx::Swell, 0.35f);
        setupDread();
    }

    // ============================================================ the dread
    // How wrong the house is tonight: grows every night, and a little with every death.
    float creep() const {
        static const float k[fnap::kNights] = {0.15f, 0.35f, 0.55f, 0.75f, 1.0f, 0.85f};
        return k[night_] * (1 + 0.15f * std::min(P().fnapDeaths, 6) / 6.0f);
    }

    void setupDread() {
        dr_ = Dread{};
        float c = std::max(0.3f, creep());
        dr_.nextOther = night_ == 0 ? rnd(120, 170) : rnd(35, 70) / c;
        dr_.nextSilence = rnd(40, 80) / c;
        dr_.nextSub = night_ == 0 ? 1e9f : rnd(50, 100) / c;
        dr_.nextLull = night_ >= 2 ? rnd(60, 120) : 1e9f;
        if (night_ >= 3) dr_.fakeAt = 5 * kHourLen + rnd(8, 22);
        if (night_ == 4 || night_ == 5) dr_.freezeAt = rnd(110, 200);
        // texts nobody should be able to send
        late_.clear();
        auto add = [&](float at, const char* from, const char* msg, bool other) { late_.push_back({at, from, msg, other}); };
        if (P().fnapLeft) {
            add(12, "Pieter", "why did you leave me alone in here", true);
            P().fnapLeft = false;
            P().save();
        }
        switch (night_) {
            case 0: add(232, "Pieter", "are you still awake?", true); break;
            case 1:
                add(58, "Pieter", "if you see me at the end of the hall, don't shine the light on me.", true);
                add(185, "Pieter", "you were standing very still just now.", true);
                break;
            case 2:
                add(112, "Pieter", "{USER}.", true);
                add(228, "Mama", "Pieter? I tried calling you. Who picked up?", false);
                break;
            case 3:
                add(48, "Pieter", "it's {TIME} where you are. you should be asleep too.", true);
                add(150, "Mama", "Coming home early. Don't open the door for anyone. Not even if it sounds like us.", false);
                break;
            case 4:
                add(28, "Pieter", "i'm in the room with you.", true);
                add(138, "Mama", "We're at the station. All the lights in the house are on. Do you have friends over?", false);
                add(248, "Pieter", "don't turn around", true);
                break;
            default: add(160, "Pieter", "they're not the scariest thing in this house, {USER}.", true); break;
        }
        lateIdx_ = 0;
    }

    void showMsg(const std::string& from, const std::string& msg, bool other) {
        shown_ = {from, fill(msg), other};
        textT_ = 0;
        E().sfx.play(Sfx::PhoneBuzz, 0.45f);
    }

    void beginPlay() {
        phase_ = Phase::Play;
        phaseT_ = 0;
        E().sfx.play(Sfx::Static, 0.5f);
        staticT_ = 0.4f;
        thunderT_ = rnd(14, 30);
    }

    void winNight() {
        phase_ = Phase::Dawn;
        phaseT_ = 0;
        bells_ = 0;
        reward_ = 60LL * (night_ + 1) + (night_ == 5 ? 500 : 0);
        bool first = !(P().fnapBeaten & (1 << night_));
        if (!first) reward_ /= 2;
        P().fnapBeaten |= 1 << night_;
        P().fnapNight = std::max(P().fnapNight, std::min(6, night_ + 2));
        P().earn(reward_);
        P().save();
        for (Sfx x : {Sfx::MusicBox, Sfx::Breath, Sfx::Giggle, Sfx::Riser, Sfx::Whisper}) E().sfx.stop(x);
    }

    void jumpscare(Who w) {
        phase_ = Phase::Scare;
        phaseT_ = 0;
        scareWho_ = w;
        b_.killer = w;
        for (Sfx x : {Sfx::Riser, Sfx::MusicBox, Sfx::Whisper, Sfx::Breath, Sfx::Giggle}) E().sfx.stop(x);
        Sfx voice = w == Who::Davi ? Sfx::Roar : w == Who::Kira || w == Who::Other ? Sfx::Shriek : Sfx::HorseScream;
        E().sfx.play(voice, 1.0f, w == Who::Other ? 0.55f : 1.0f);
        if (w == Who::Other) E().sfx.play(Sfx::Exhale, 1.0f, 0.7f);
        if (!b_.frozen && !std::getenv("PG_FNAP")) {  // dev runs don't count
            P().fnapDeaths++;
            P().save();
        }
        dr_.freezeT = -1;
        dr_.silenceT = -1;
        E().sfx.play(Sfx::Scream, 0.45f, w == Who::Kira ? 1.15f : 0.85f);
        E().sfx.play(Sfx::Impact, 0.9f);
        E().sfx.play(Sfx::Glitch, 0.6f);
        if (w == Who::Paardjes) E().sfx.play(Sfx::Neigh, 0.9f, 0.6f);
        E().shake(34);
        E().aberrate(0.018f);
        E().flash(WHITE, 0.8f);
    }

    int hour() const { return b_.hour(); }
    std::string clockText() const { return hour() == 0 ? "12 AM" : std::to_string(hour()) + " AM"; }

    // ============================================================ audio
    // Where a sound comes from, heard from where Pieter is standing.
    void play3d(Sfx s, Src src, float vol, float pitch = 1) {
        static const float pan[5][6] = {
            // L hall  R hall  closet  bed    window desk
            {-0.75f, 0.75f, 0.0f, -0.45f, 0.45f, 0.55f},   // centre
            {0.0f, 0.35f, 0.65f, 0.5f, 0.75f, 0.8f},       // at the left door
            {-0.35f, 0.0f, -0.65f, -0.75f, -0.4f, -0.5f},  // at the right door
            {-0.8f, 0.8f, 0.0f, -0.3f, 0.6f, 0.6f},        // at the closet
            {-0.7f, 0.7f, 0.0f, 0.0f, 0.6f, 0.65f},        // at the bed
        };
        static const float gain[5][6] = {
            {0.7f, 0.7f, 0.75f, 0.8f, 0.8f, 0.9f},
            {1.0f, 0.3f, 0.45f, 0.45f, 0.4f, 0.4f},
            {0.3f, 1.0f, 0.45f, 0.45f, 0.45f, 0.45f},
            {0.5f, 0.5f, 1.1f, 0.5f, 0.45f, 0.45f},
            {0.5f, 0.5f, 0.5f, 1.0f, 0.55f, 0.5f},
        };
        int v = (int)view_, k = (int)src;
        E().sfx.play(s, vol * gain[v][k], pitch, pan[v][k]);
    }
    static Src hallSrc(int side) { return side == 0 ? Src::LeftHall : Src::RightHall; }

    void handle(const fnap::Event& e) {
        int s = e.a;
        Src hs = hallSrc(s);
        switch (e.ev) {
            case Ev::Roam: play3d(Sfx::StepFar, hs, 0.35f, rnd(0.9f, 1.1f)); break;
            case Ev::Advance: {
                prevPos_[s] = (int)e.x - 1;
                // later nights: sometimes Kira comes along the ceiling, and Davi waits at the far end with his back to you
                dr_.ceiling[s] = s == 1 && e.x == 2 && night_ >= 3 && chance(0.4f);
                dr_.turned[s] = s == 0 && e.x == 1 && night_ >= 1 && chance(0.5f);
                float near = e.x / 3.0f;
                if (s == 0) {
                    play3d(Sfx::Klomp, hs, 0.25f + 0.45f * near, rnd(0.9f, 1.05f));
                    klompEcho_ = 0.45f;
                } else if (e.x < 3) {
                    play3d(Sfx::FietsBel, hs, 0.18f + 0.3f * near, rnd(0.78f, 0.86f));
                } else {
                    play3d(Sfx::Giggle, hs, 0.35f, 0.85f);
                }
                if (e.x >= 3) play3d(Sfx::Creak, hs, 0.4f, 0.75f);  // the floorboard right outside the door
                break;
            }
            case Ev::Back: prevPos_[s] = b_.hall[s].pos + 1; play3d(Sfx::StepFar, hs, 0.3f); break;
            case Ev::Seen:
                E().sfx.play(Sfx::Sting, 0.25f + 0.2f * e.x, rnd(0.95f, 1.05f), s == 0 ? -0.3f : 0.3f);
                if (e.x >= 2) E().aberrate(0.006f * e.x);
                break;
            case Ev::Flee:
                play3d(Sfx::Flee, hs, 0.7f);
                E().sfx.play(Sfx::Hurt, 0.45f, s == 0 ? 0.45f : 0.75f, s == 0 ? -0.4f : 0.4f);
                flashScare_ = 1;
                break;
            case Ev::Hide: play3d(Sfx::Creak, hs, 0.35f, 1.35f); play3d(Sfx::Giggle, hs, 0.18f, 0.8f); break;
            case Ev::Unhide: play3d(Sfx::Creak, hs, 0.25f, 1.2f); break;
            case Ev::AtDoor: prevPos_[s] = 3; break;  // silence. That's the scary part.
            case Ev::Cue:
                if (s == 0) play3d(Sfx::Breath, hs, e.x > 0 ? (holding() ? 0.4f : 0.85f) : 0.35f);
                else play3d(Sfx::Giggle, hs, e.x > 0 ? (holding() ? 0.35f : 0.65f) : 0.28f, rnd(0.95f, 1.05f));
                break;
            case Ev::Push:
                play3d(Sfx::Thud, hs, 0.8f, rnd(0.9f, 1.1f));
                if (chance(0.5f)) play3d(Sfx::Rattle, hs, 0.5f);
                E().shake(5);
                doorKick_ = 1;
                break;
            case Ev::Leave:
            case Ev::Feint: play3d(Sfx::Flee, hs, 0.55f, 0.9f); break;  // exactly the same. Listen for what comes after.
            case Ev::Lure:
                P().take(s == 0 ? "stroopwafel" : "boba");
                E().sfx.play(Sfx::Whoosh, 0.6f);
                play3d(Sfx::Flee, hs, 0.35f, 1.2f);
                break;
            case Ev::LureFail: E().sfx.play(Sfx::Error, 0.4f); break;
            case Ev::HorseGrow:
                play3d(Sfx::Scratch, Src::Closet, 0.2f + 0.15f * e.x);
                if (e.x >= 3) play3d(Sfx::Creak, Src::Closet, 0.5f, 0.7f);
                break;
            case Ev::HorseOpen:
                play3d(Sfx::Creak, Src::Closet, 1.0f, 0.55f);
                play3d(Sfx::Neigh, Src::Closet, 0.8f, 0.55f);
                E().sfx.play(Sfx::Sting, 0.7f, 0.85f);
                E().shake(8);
                break;
            case Ev::HorseBack: play3d(Sfx::Slam, Src::Closet, 0.6f, 1.2f); play3d(Sfx::Snort, Src::Closet, 0.4f, 1.1f); break;
            case Ev::HorseStrain: play3d(Sfx::Creak, Src::Closet, 0.35f, rnd(1.1f, 1.3f)); break;
            case Ev::Snort: play3d(Sfx::Snort, Src::Closet, 0.25f + 0.1f * b_.horse, rnd(0.9f, 1.1f)); break;
            case Ev::PlushIn: play3d(Sfx::Squeak, Src::Bed, 0.3f, rnd(0.7f, 0.9f)); break;
            case Ev::PlushOut: {
                play3d(Sfx::Squeak, Src::Bed, 0.6f, rnd(1.3f, 1.5f));
                play3d(Sfx::Pop, Src::Bed, 0.5f, 0.8f);
                plushFall_ = 0;
                plushFallIdx_ = (int)e.x;
                Vector2 p = bedSpot((int)e.x);
                fx_.burst(p + Vector2{0, -60}, 18, rgba(0x8a5a3aff), 300, 6, 0.5f, PShape::Circle, 600);
                fx_.burst(p + Vector2{0, -60}, 8, WHITE, 200, 4, 0.4f, PShape::Star);
                break;
            }
            case Ev::PlushCall:
                for (int i = 0; i < 3; i++) play3d(Sfx::Neigh, Src::Bed, 0.4f, 0.5f + i * 0.25f);
                E().sfx.play(Sfx::Whisper, 0.5f);
                break;
            case Ev::PcHack:
                play3d(Sfx::Static, Src::Desk, 0.6f);
                clash();
                break;
            case Ev::PcSong: E().sfx.play(Sfx::MusicBox, view_ == View::Center ? 0.6f : 0.35f, 0.95f, 0.3f); break;
            case Ev::PcOff:
                E().sfx.stop(Sfx::MusicBox);
                E().sfx.play(Sfx::Click);
                E().sfx.play(Sfx::Static, 0.3f, 1.4f);
                break;
            case Ev::MonkeyTap: play3d(Sfx::Knock, Src::Window, 0.18f, 1.9f); break;
            case Ev::MonkeyStare: E().sfx.play(Sfx::Riser, 0.5f); break;
            case Ev::MonkeyCalm: E().sfx.stop(Sfx::Riser); break;
            case Ev::MonkeyBang:
                E().sfx.stop(Sfx::Riser);
                play3d(Sfx::Knock, Src::Window, 1.0f, 0.7f);
                play3d(Sfx::Monkey, Src::Window, 0.9f, rnd(0.9f, 1.05f));
                E().sfx.play(Sfx::Boom, 0.4f, 1.4f);
                clash();
                monkeyBang_ = 1;
                E().shake(14);
                E().aberrate(0.012f);
                break;
            case Ev::Cymbal: clash(); break;
            case Ev::Hour:
                if (e.a > 0 && e.a < 6) {
                    E().sfx.play(Sfx::Carillon, 0.4f, 0.98f);
                    hourPulse_ = 1;
                }
                break;
            case Ev::BatteryLow: E().sfx.play(Sfx::LowBatt, 0.5f); break;
            case Ev::BatteryDead: break;
            case Ev::Knock: E().sfx.play(Sfx::Knock, 0.45f, rnd(0.9f, 1.05f), e.x * 0.8f); break;
            case Ev::Whisper: E().sfx.play(Sfx::Whisper, rnd(0.3f, 0.5f), rnd(0.9f, 1.1f)); break;
            case Ev::Hush:
                hushT_ = 0;
                E().sfx.play(Sfx::Tinnitus, 0.25f);
                for (Sfx x : {Sfx::Rain, Sfx::Wind, Sfx::Drone}) E().sfx.stop(x);
                break;
            case Ev::Rush:
                hushT_ = -1;
                E().sfx.play(Sfx::Impact, 0.8f);
                E().sfx.play(Sfx::Sting, 0.8f, 0.9f);
                play3d(Sfx::Klomp, Src::LeftHall, 0.9f);
                play3d(Sfx::Giggle, Src::RightHall, 0.7f, 0.85f);
                prevPos_[0] = prevPos_[1] = 1;
                E().shake(10);
                E().aberrate(0.015f);
                break;
            case Ev::Jumpscare: jumpscare((Who)e.a); break;
            case Ev::Win: winNight(); break;
        }
    }

    void clash() {
        clashAnim_ = 1;
        play3d(Sfx::Cymbal, Src::Desk, 0.45f, rnd(0.95f, 1.05f));
    }

    // Beds of sound under everything, mixed by how bad things are.
    void ambience(float dt, bool playing) {
        bool hush = hushT_ >= 0 || (playing && dr_.silenceT >= 0);
        float c = playing ? creep() : 0.2f;
        // a pressure under everything that you feel more than hear
        E().sfx.loop(Sfx::Sub, hush ? 0.25f : playing ? 0.12f + 0.3f * c + 0.3f * (dr_.spot >= 0 && dr_.at == view_ ? 1 : 0) : 0.08f);
        float dg = playing ? b_.danger() : 0;
        float title = phase_ == Phase::Title ? 1.0f : 0.0f;
        E().sfx.loop(Sfx::DreadLow, hush ? 0 : 0.32f + 0.25f * dg);
        E().sfx.loop(Sfx::DreadHigh, hush ? 0 : std::max(0.0f, dg - 0.45f) * 1.1f);
        E().sfx.loop(Sfx::Hum, playing && !hush ? (view_ == View::Center ? 0.16f : 0.04f) : 0);
        E().sfx.loop(Sfx::Choir, title * 0.22f);
        if (hush) return;
        // the house, settling. Or something walking around upstairs.
        if (playing && (dr_.creakT -= dt) <= 0) {
            dr_.creakT = rnd(9, 22) / std::max(0.4f, c);
            E().sfx.play(chance(0.5f) ? Sfx::Creak : Sfx::StepFar, rnd(0.08f, 0.18f), rnd(0.5f, 0.75f), rnd(-0.9f, 0.9f));
        }
        if (playing && (dr_.dripT -= dt) <= 0) {
            dr_.dripT = rnd(4, 11);
            E().sfx.play(Sfx::Drip, rnd(0.08f, 0.16f), rnd(0.9f, 1.1f), rnd(-0.8f, 0.8f));
        }
        rainT_ -= dt;
        if (rainT_ <= 0) {  // it's always raining in Holland
            rainT_ = 3.1f;
            bool inside = playing && view_ != View::Center;
            E().sfx.play(Sfx::Rain, inside ? 0.22f : 0.4f, rnd(0.95f, 1.05f), 0.25f);
        }
        if ((windT_ -= dt) <= 0) {
            windT_ = rnd(7, 16);
            E().sfx.play(Sfx::Wind, rnd(0.25f, 0.45f), rnd(0.85f, 1.15f));
        }
        // the staande klok in the left hall: tick... tock
        if (playing && (clockT_ -= dt) <= 0) {
            clockT_ = 1;
            tock_ = !tock_;
            play3d(Sfx::ClockTick, Src::LeftHall, 0.3f, tock_ ? 0.84f : 1.0f);
        }
        // lightning, and the thunder a moment later
        if ((thunderT_ -= dt) <= 0) {
            thunderT_ = phase_ == Phase::Title ? rnd(9, 18) : rnd(20, 42);
            boltT_ = 0;
            thunderDelay_ = rnd(0.15f, 1.1f);
        }
        if (devBolt_) {
            boltT_ = 0.2f;
        } else if (boltT_ >= 0) {
            boltT_ += dt;
            if (thunderDelay_ > 0 && boltT_ >= thunderDelay_) {
                thunderDelay_ = -1;
                E().sfx.play(Sfx::Thunder, rnd(0.6f, 0.9f), rnd(0.85f, 1.1f), rnd(-0.4f, 0.4f));
                E().shake(3);
            }
            if (boltT_ > 3) boltT_ = -1;
        }
    }

    float lightning() const {
        if (boltT_ < 0 || hushT_ >= 0) return 0;
        float t = boltT_;
        if (t < 0.06f) return 1;
        if (t < 0.11f) return 0.15f;
        if (t < 0.17f) return 0.85f;
        return 0.85f * std::exp(-(t - 0.17f) * 7);
    }

    // ============================================================ update
    void update(float dt) override {
        // the freeze: the picture stops, the sound stops. Only the cursor still moves.
        if (dr_.freezeT >= 0) {
            dr_.freezeT += dt;
            for (Sfx x : {Sfx::DreadLow, Sfx::DreadHigh, Sfx::Hum, Sfx::Sub}) E().sfx.loop(x, 0);
            if (dr_.freezeT > 2.9f) {
                dr_.freezeT = -1;
                E().sfx.play(Sfx::Exhale, 0.9f, 0.9f);
                E().sfx.play(Sfx::Glitch, 0.5f, 0.7f);
                subFace_ = 3;
                subT_ = 0.09f;
                staticT_ = 0.3f;
            }
            return;
        }
        t_ += dt;
        phaseT_ += dt;
        fx_.update(dt);
        staticT_ = std::max(0.0f, staticT_ - dt);
        // offscreen passes happen here: render targets can't nest inside the engine's canvas
        if (phase_ == Phase::Play) renderLightMap();
        if (phase_ == Phase::Scare) renderScare();
        switch (phase_) {
            case Phase::Title: updateTitle(dt); break;
            case Phase::Cinematic: updateCinematic(dt); break;
            case Phase::Note:
                ambience(dt, false);
                if (phaseT_ > 1.2f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || E().mousePressed())) {
                    E().consumeClick();
                    beginPlay();
                }
                if (E().backPressed()) phase_ = Phase::Title;
                break;
            case Phase::Play: updatePlay(dt); break;
            case Phase::Scare:
                if (phaseT_ < 1.3f) E().shake(7), E().aberrate(0.008f * (1 - phaseT_ / 1.3f));
                if (phaseT_ > 1.7f) {
                    phase_ = Phase::GameOver;
                    phaseT_ = 0;
                    E().sfx.play(Sfx::Static, 0.7f);
                    E().sfx.play(Sfx::Tinnitus, 0.6f);
                }
                break;
            case Phase::GameOver:
                ambience(dt, false);
                if (E().backPressed()) phase_ = Phase::Title;
                break;
            case Phase::Dawn: updateDawn(dt); break;
            case Phase::FakeDawn: updateFakeDawn(dt); break;
        }
        if (phase_ == Phase::Play) renderView();
    }

    void updateTitle(float dt) {
        ambience(dt, false);
        if ((musicT_ -= dt) <= 0) {
            musicT_ = 7.2f;
            E().sfx.play(Sfx::MusicBox, 0.45f, 0.97f);
        }
        if (std::fmod(t_, 9.0f) < dt) E().sfx.play(Sfx::Giggle, 0.12f, 0.9f, rnd(-0.8f, 0.8f));
        if (E().backPressed()) {
            if (howTo_) howTo_ = false;
            else E().go(makeMenu);
        }
    }

    void updateCinematic(float dt) {
        ambience(dt, false);
        bool skip = phaseT_ > 0.5f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || E().mousePressed());
        if (skip) E().consumeClick();
        if (phaseT_ >= kCinematicLen || skip) {
            if (introNote_) phase_ = Phase::Note, phaseT_ = 0;
            else beginPlay();
        }
        if (E().backPressed()) phase_ = Phase::Title;
    }

    void updateDawn(float dt) {
        if (bells_ < 1 && phaseT_ > 1.5f) {
            bells_ = 1;
            E().sfx.play(Sfx::Alarm, 0.6f);
            E().sfx.play(Sfx::Win, 0.5f);
            E().sfx.play(Sfx::Birds, 0.7f);
        }
        if (bells_ == 1 && phaseT_ > 6) {
            bells_ = 2;
            E().sfx.play(Sfx::Birds, 0.5f);
        }
        E().sfx.loop(Sfx::DreadLow, 0);
        E().sfx.loop(Sfx::DreadHigh, 0);
        E().sfx.loop(Sfx::Hum, 0);
        (void)dt;
        if (E().backPressed()) phase_ = Phase::Title;
    }

    bool lightOn() const {
        if (devLight_) return true;
        if (moveT_ > 0 || phase_ != Phase::Play || view_ == View::Center) return false;
        return IsKeyDown(KEY_F) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    }
    bool holding() const {
        if (devHold_) return true;
        if (moveT_ > 0 || phase_ != Phase::Play) return false;
        if (view_ != View::Left && view_ != View::Right && view_ != View::Closet) return false;
        return IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    }
    // the beam actually shining: switch held, battery alive, a flicker now and then
    float beamPower() const {
        if (!lightOn()) return 0;
        if (b_.battery <= 0) return std::fmod(t_ * 7, 1.0f) < 0.12f ? 0.15f : 0;  // dead: it sputters
        float p = 1;
        if (b_.battery < 0.2f) p *= 0.55f + 0.45f * (b_.battery / 0.2f) * (std::fmod(t_ * 13, 1.0f) < 0.9f ? 1 : 0.3f);
        if (flicker_ > 0) p *= 0.2f;
        return p;
    }

    void go(View v) {
        if (moveT_ > 0 || v == view_) return;
        pendingView_ = v;
        moveT_ = 0.3f;
        E().sfx.play(Sfx::Step, 0.5f, rnd(0.9f, 1.1f), v == View::Left ? -0.5f : v == View::Right ? 0.5f : 0);
    }

    void updatePlay(float dt) {
        ambience(dt, true);
        // ---- moving between spots
        if (moveT_ > 0) {
            float before = moveT_;
            moveT_ -= dt;
            if (before > 0.15f && moveT_ <= 0.15f) view_ = pendingView_;
        } else {
            bool l = IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT), r = IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT);
            bool u = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP), d = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN);
            switch (view_) {
                case View::Center:
                    if (l) go(View::Left);
                    if (r) go(View::Right);
                    if (u) go(View::Closet);
                    if (d) go(View::Bed);
                    break;
                case View::Left: if (r || d) go(View::Center); break;
                case View::Right: if (l || d) go(View::Center); break;
                case View::Closet: if (d || l || r) go(View::Center); break;
                case View::Bed: if (u || l || r) go(View::Center); break;
            }
        }
        bool light = lightOn(), hold = holding();
        if (light != wasLight_) E().sfx.play(Sfx::Flash, 0.6f, light ? 1.1f : 0.9f);
        if (hold && !wasHold_) E().sfx.play(Sfx::Slam, view_ == View::Closet ? 0.45f : 0.7f, view_ == View::Closet ? 1.3f : 1);
        wasLight_ = light;
        wasHold_ = hold;
        doorAnim_ = damp(doorAnim_, hold ? 1.0f : 0.0f, 22, dt);
        doorKick_ = std::max(0.0f, doorKick_ - dt * 4);
        flicker_ = b_.danger() > 0.6f && rndi(0, 50) == 0 ? 0.08f : std::max(0.0f, flicker_ - dt);

        fnap::Input in;
        in.view = view_;
        in.moving = moveT_ > 0;
        in.light = light;
        in.hold = hold;
        in.lure = IsKeyPressed(KEY_Q);
        in.pcOff = IsKeyPressed(KEY_E) || pcClick_;
        in.hasLure[0] = P().count("stroopwafel") > 0;
        in.hasLure[1] = P().count("boba") > 0;
        pcClick_ = false;
        ev_.clear();
        b_.update(dt, in, ev_);
        for (auto& e : ev_) {
            handle(e);
            if (phase_ != Phase::Play) return;
        }

        // ---- the window, texts, heartbeat
        monkeyRise_ = damp(monkeyRise_, b_.monkey ? 1.0f : 0.0f, b_.monkey ? 3 : 6, dt);
        monkeyBang_ = std::max(0.0f, monkeyBang_ - dt * 2);
        clashAnim_ = std::max(0.0f, clashAnim_ - dt * 5);
        hourPulse_ = std::max(0.0f, hourPulse_ - dt * 0.5f);
        if (hushT_ >= 0) hushT_ += dt;
        if (klompEcho_ > 0 && (klompEcho_ -= dt) <= 0) play3d(Sfx::Klomp, Src::LeftHall, 0.15f, rnd(0.88f, 0.95f));
        if (plushFall_ >= 0 && (plushFall_ += dt) > 0.6f) plushFall_ = -1;
        if (textIdx_ < (int)kTexts[night_].size() && b_.t >= kTexts[night_][textIdx_].at) showText(kTexts[night_][textIdx_++]);
        if (lateIdx_ < (int)late_.size() && b_.t >= late_[lateIdx_].at && textT_ < 0) {
            const Late& l = late_[lateIdx_++];
            showMsg(l.from, l.msg, l.other);
        }
        if (textT_ >= 0 && (textT_ += dt) > 6.5f) textT_ = -1;
        float dg = b_.danger();
        if (dg > 0.2f && hushT_ < 0) {
            if ((heartT_ -= dt) <= 0) {
                heartT_ = lerpf(1.1f, 0.42f, dg);
                E().sfx.play(Sfx::Heart, 0.3f + 0.5f * dg);
                heartPulse_ = 1;
            }
        }
        heartPulse_ = std::max(0.0f, heartPulse_ - dt * 3);
        flashScare_ = std::max(0.0f, flashScare_ - dt * 2);
        // when things get bad you start seeing faces
        subT_ = std::max(0.0f, subT_ - dt);
        if (dg > 0.5f && subT_ <= 0 && chance(dt * 0.04f * (1 + night_ * 0.3f))) {
            subT_ = 0.07f;
            subFace_ = rndi(0, night_ >= 1 ? 3 : 2);
            E().sfx.play(Sfx::Static, 0.25f, 1.3f);
        }
        updateDread(dt);
        if (phase_ != Phase::Play) return;
        if (E().backPressed()) {
            if (night_ >= 1 && b_.t > 30 && !std::getenv("PG_FNAP")) {  // it notices when you leave
                P().fnapLeft = true;
                P().save();
            }
            phase_ = Phase::Title;
        }
    }

    // ---- where he stands. Halls: (u, depth, scale factor) by how close he's got.
    struct Spot { float u, d, k; };
    static Spot hallSpot(int side, int spot) {
        float m = side == 0 ? 1.0f : -1.0f;
        switch (spot) {
            case 0: return {0.02f * m, 0.97f, 1.25f};   // at the far end, in front of the window
            case 1: return {-0.8f * m, 0.34f, 1.25f};   // half in a doorway along the hall
            case 2: return {-0.4f * m, 0.14f, 0.92f};   // nearly at your door
            default: return {0.0f, 0.0f, 0.97f};        // right in front of you
        }
    }
    // Centre view: feet and scale, in room coordinates.
    static void roomSpot(int spot, Vector2& feet, float& s) {
        if (spot <= 1) feet = {812, 540}, s = 0.6f;        // in the dark gap between the closet and the window
        else if (spot == 2) feet = {300, 566}, s = 0.62f;  // at the end of the bed
        else feet = {640, 760}, s = 0.98f;                 // in the middle of the room
    }

    void onArrive() {
        dr_.spot = -1;
        dr_.lit = dr_.hover = 0;
        dr_.reflectT = -1;
        float c = creep();
        if (dr_.hunting && dr_.finalT < 0) {  // he's coming, wherever you go
            dr_.spot = 3;
            dr_.at = view_;
            dr_.finalT = 0;
            dr_.fade = 1;
            dr_.silenceT = 0;
            dr_.silenceLen = 99;
            for (Sfx x : {Sfx::Rain, Sfx::Wind, Sfx::MusicBox}) E().sfx.stop(x);
            return;
        }
        bool hall = view_ == View::Left || view_ == View::Right;
        if ((hall || view_ == View::Center) && b_.t > dr_.nextOther && chance(0.35f + c * 0.4f)) {
            dr_.at = view_;
            dr_.spot = std::min(dr_.stage, 2);
            if (view_ == View::Center && dr_.stage == 0) dr_.spot = 0;
            dr_.fade = 0;
            dr_.nextOther = b_.t + rnd(30, 60) * (1.3f - c * 0.6f);
        }
        if (view_ == View::Center && !b_.pc && night_ >= 1 && dr_.spot < 0 && chance(0.25f * c)) dr_.reflectT = 0;
        dr_.bedShape = view_ == View::Bed && night_ >= 2 && b_.plush < 3 && chance(0.3f * c);
        dr_.bedLit = 0;
    }

    void otherSeen() {
        dr_.stage++;
        dr_.spot = -1;
        staticT_ = 0.35f;
        subFace_ = 3;
        subT_ = 0.06f;
        E().sfx.play(Sfx::Glitch, 0.7f, 0.8f);
        E().sfx.play(Sfx::Sting, 0.6f, 0.7f);
        E().sfx.play(Sfx::Exhale, 0.5f, 0.8f);
        E().aberrate(0.015f);
        E().shake(4);
        const char* lines[] = {"don't look at me", "please. don't look at me.", "if you look one more time i'm coming over there"};
        if (dr_.stage <= 3) showMsg("Pieter", lines[dr_.stage - 1], true);
        if (dr_.stage >= 4) {
            dr_.hunting = true;
            showMsg("Pieter", "ok", true);
        }
    }

    void updateDread(float dt) {
        float c = creep();
        if (moveT_ <= 0 && view_ != dr_.lastView) {
            dr_.lastView = view_;
            onArrive();
        }
        // ---- him
        if (dr_.spot >= 0 && dr_.at == view_) {
            dr_.fade = std::min(1.0f, dr_.fade + dt * 1.5f);
            if (dr_.finalT >= 0) {
                dr_.finalT += dt;
                if (dr_.finalT > 2.2f) {
                    jumpscare(Who::Other);
                    return;
                }
            } else if (view_ != View::Center) {
                bool inBeam = beamPower() > 0 && CheckCollisionPointRec(aim(), dr_.rect);
                dr_.lit = inBeam ? dr_.lit + dt : std::max(0.0f, dr_.lit - dt * 0.5f);
                if (dr_.lit > 0.3f) otherSeen();
            } else {
                // in the room you can't light him. Look at him and his head follows you; keep looking and he steps out of sight.
                Vector2 m = roomMouse();
                bool over = CheckCollisionPointRec(m, dr_.rect);
                dr_.hover = over ? dr_.hover + dt : std::max(0.0f, dr_.hover - dt);
                if (dr_.hover > 3.2f) {
                    dr_.spot = -1;
                    staticT_ = 0.15f;
                    E().sfx.play(Sfx::Creak, 0.3f, 0.6f);
                }
            }
            Vector2 head{dr_.rect.x + dr_.rect.width / 2, dr_.rect.y};
            float want = clamp01((aim().x - head.x) / 500 + 0.5f) * 2 - 1;
            dr_.look = damp(dr_.look, want, 0.8f, dt);  // slowly. Too slowly.
        } else if (dr_.hunting && dr_.finalT < 0 && moveT_ <= 0 && (dr_.huntT += dt) > 6) {
            dr_.huntT = 0;
            onArrive();  // staying put doesn't help
        }
        // ---- silences that lead nowhere
        if (dr_.silenceT < 0 && b_.t > dr_.nextSilence && hushT_ < 0 && b_.danger() < 0.45f && !dr_.hunting) {
            dr_.silenceT = 0;
            dr_.silenceLen = rnd(7, 12);
            for (Sfx x : {Sfx::Rain, Sfx::Wind}) E().sfx.stop(x);
        }
        if (dr_.silenceT >= 0 && dr_.finalT < 0) {
            dr_.silenceT += dt;
            if (dr_.silenceT > dr_.silenceLen) {
                dr_.silenceT = -1;
                dr_.nextSilence = b_.t + rnd(55, 95) / std::max(0.3f, c);
                switch (rndi(0, 4)) {  // and then, maybe, one sound
                    case 1: E().sfx.play(Sfx::Slam, 0.12f, 0.6f, rnd(-0.6f, 0.6f)); break;
                    case 2: if (night_ >= 2) E().sfx.play(Sfx::Lullaby, 0.12f, 1, rnd(-0.7f, 0.7f)); break;
                    case 3: E().sfx.play(Sfx::Whisper, 0.3f, 0.85f, 0); break;
                    case 4: if (night_ >= 1) E().sfx.play(Sfx::Barefoot, 0.5f, 1, 0); break;
                    default: break;
                }
            }
        }
        // ---- breathing behind you, if you stand in the room too long
        if (view_ == View::Center && moveT_ <= 0 && dr_.finalT < 0) dr_.stillT += dt;
        else dr_.stillT = std::min(dr_.stillT, 0.0f), dr_.behindT = -1;
        if (view_ != View::Center) dr_.stillT = 0;
        if (c >= 0.3f && dr_.behindT < 0 && dr_.stillT > 15 - c * 6) dr_.behindT = 0, dr_.breathT = 0;
        if (dr_.behindT >= 0) {
            dr_.behindT += dt;
            if ((dr_.breathT -= dt) <= 0) {
                dr_.breathT = 3.3f;
                E().sfx.play(Sfx::Exhale, std::min(1.0f, 0.35f + dr_.behindT * 0.08f), rnd(0.92f, 1.0f), 0);
            }
            if (dr_.behindT > 11) {
                dr_.behindT = -1;
                dr_.stillT = -30;  // it goes away. For a while.
                E().sfx.play(Sfx::Barefoot, 0.35f, 1, 0);
            }
        }
        // ---- a hand on the door that isn't yours
        if (holding() && doorAnim_ > 0.9f && view_ != View::Closet) {
            dr_.holdT += dt;
            if (!dr_.handDone && night_ >= 2 && dr_.holdT > 1.2f && chance(dt * 0.5f * c)) {
                dr_.handDone = true;
                dr_.handT = 0;
                E().sfx.play(Sfx::Exhale, 0.7f, 0.85f, 0);
            }
        } else {
            dr_.holdT = 0;
            dr_.handDone = false;
        }
        if (dr_.handT >= 0 && (dr_.handT += dt) > 2.6f) dr_.handT = -1;
        if (dr_.handT >= 0 && !holding()) dr_.handT = -1;
        // ---- two frames of something written
        dr_.subT = std::max(0.0f, dr_.subT - dt);
        if (b_.t > dr_.nextSub) {
            dr_.nextSub = b_.t + rnd(45, 90) / std::max(0.3f, c);
            dr_.subT = 0.05f;
            dr_.subText = fill(kSubText[rndi(0, (int)(sizeof(kSubText) / sizeof(kSubText[0])) - 1)]);
        }
        if (dr_.reflectT >= 0 && (dr_.reflectT += dt) > 6) dr_.reflectT = -1;
        // ---- the bed: shine on the shape and it's gone
        if (dr_.bedShape && view_ == View::Bed && beamPower() > 0 && CheckCollisionPointRec(aim(), {300, 200, 520, 220})) {
            if ((dr_.bedLit += dt) > 0.25f) {
                dr_.bedShape = false;
                E().sfx.play(Sfx::Exhale, 0.8f, 1.15f, 0);
                E().sfx.play(Sfx::Creak, 0.4f, 0.8f);
                staticT_ = 0.12f;
            }
        }
        // ---- a lullaby somewhere in the house
        if (b_.t > dr_.nextLull && hushT_ < 0 && dr_.silenceT < 0) {
            dr_.nextLull = b_.t + rnd(90, 150);
            E().sfx.play(Sfx::Lullaby, 0.1f + 0.08f * c, 1, rnd(-0.8f, 0.8f));
        }
        // ---- the fake morning, the freeze
        if (dr_.fakeAt > 0 && b_.t >= dr_.fakeAt && b_.danger() < 0.7f && !dr_.hunting) {
            dr_.fakeAt = -1;
            phase_ = Phase::FakeDawn;
            phaseT_ = 0;
            bells_ = 0;
            for (Sfx x : {Sfx::MusicBox, Sfx::Breath, Sfx::Giggle, Sfx::Riser, Sfx::Whisper, Sfx::Exhale}) E().sfx.stop(x);
            return;
        }
        if (dr_.freezeAt > 0 && b_.t >= dr_.freezeAt && view_ == View::Center && moveT_ <= 0 && hushT_ < 0) {
            dr_.freezeAt = -1;
            dr_.freezeT = 0;
            dr_.frozenT = t_;
            for (Sfx x : {Sfx::Rain, Sfx::Wind, Sfx::Thunder, Sfx::MusicBox, Sfx::ClockTick, Sfx::Heart}) E().sfx.stop(x);
        }
        redPulse_ = std::max(0.0f, redPulse_ - dt * 0.15f);
    }

    // the mouse in room coordinates (the centre view is panned and zoomed a little)
    Vector2 roomMouse() const {
        Vector2 m = E().mouse(), cam = camShift();
        return {(m.x - VW / 2.0f - cam.x) / 1.06f + VW / 2.0f, (m.y - VH / 2.0f - cam.y) / 1.06f + VH / 2.0f};
    }

    // It's 6 AM. The alarm, the birds, the sunrise. And then it isn't.
    void updateFakeDawn(float dt) {
        (void)dt;
        for (Sfx x : {Sfx::DreadLow, Sfx::DreadHigh, Sfx::Hum, Sfx::Sub}) E().sfx.loop(x, 0);
        if (bells_ < 1 && phaseT_ > 1.5f) {
            bells_ = 1;
            E().sfx.play(Sfx::Alarm, 0.6f);
            E().sfx.play(Sfx::Win, 0.5f);
            E().sfx.play(Sfx::Birds, 0.7f);
        }
        if (bells_ == 1 && phaseT_ > 4.6f) {
            bells_ = 2;
            for (Sfx x : {Sfx::Alarm, Sfx::Win, Sfx::Birds}) E().sfx.stop(x);
        }
        if (bells_ == 2 && phaseT_ > 6.4f) {
            bells_ = 3;
            E().sfx.play(Sfx::Impact, 0.9f, 0.8f);
            E().sfx.play(Sfx::Glitch, 0.7f);
            E().sfx.play(Sfx::Static, 0.6f);
            E().shake(16);
            E().aberrate(0.02f);
        }
        if (phaseT_ > 6.75f) {
            phase_ = Phase::Play;
            phaseT_ = 0;
            staticT_ = 0.6f;
            redPulse_ = 1;
            while (textIdx_ < (int)kTexts[night_].size() && kTexts[night_][textIdx_].at <= b_.t + 10) textIdx_++;  // nothing else gets through
            showMsg("Pieter", "it's not morning.", true);
        }
    }

    void showText(const Text& t) { showMsg(t.from, t.msg, false); }

    Vector2 bedSpot(int i) const {
        const Vector2 spots[3] = {{390, 560}, {650, 470}, {920, 570}};
        return spots[std::clamp(i, 0, 2)];
    }

    // ============================================================ light map (centre view)
    void renderLightMap() {
        if (!lm_.id) return;
        env_.t = t_;
        BeginTextureMode(lm_);
        ClearBackground(mix(rgba(0x2e2a54ff), rgba(0x1c1a38ff), creep()));  // less and less light gets in
        rlPushMatrix();
        rlScalef(0.5f, 0.5f, 1);
        fx::beginAdd();
        room::addLights(env_);
        fx::glow({70, 330}, 230, alpha(rgba(0x6878d8ff), 0.45f));  // moonlight from both halls
        fx::glow({VW - 70.0f, 330}, 230, alpha(rgba(0x6878d8ff), 0.45f));
        if (b_.pc) fx::glow({1030, 350}, 420, alpha(std::fmod(t_, 0.5f) < 0.25f ? col::pink : col::red, 0.8f));
        fx::glow({1158, 440}, 90, alpha(b_.battery < 1 ? rgba(0xffa030ff) : rgba(0x40ff80ff), 0.5f));  // the charger's LED
        if (float L = lightning(); L > 0) {
            fx::glow({975, 190}, 900, alpha(rgba(0xc8d4ffff), 0.9f * L));
            fx::glow({975, 600}, 500, alpha(rgba(0xc8d4ffff), 0.5f * L));
        }
        fx::endAdd();
        rlPopMatrix();
        EndTextureMode();
    }

    // ============================================================ draw: shared
    void grain(float amount) {
        float ox = gfx::hash(std::floor(t_ * 24)) * 256, oy = gfx::hash(std::floor(t_ * 24) + 7) * 256;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(noise_, {ox, oy, VW / 2.0f, VH / 2.0f}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, alpha(WHITE, amount));
        EndBlendMode();
        for (int y = 0; y < VH; y += 4) DrawRectangle(0, y, VW, 1, alpha(BLACK, 0.1f));
    }

    void staticScreen(float a) {
        float ox = gfx::hash(std::floor(t_ * 30)) * 256, oy = gfx::hash(std::floor(t_ * 30) + 3) * 256;
        DrawTexturePro(noise_, {ox, oy, VW / 3.0f, VH / 3.0f}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, alpha(rgba(0xb0b0c0ff), a));
        for (int i = 0; i < 6; i++) {
            float y = std::fmod(t_ * 300 + i * 137, (float)VH);
            DrawRectangle(0, (int)y, VW, 3 + i % 3, alpha(WHITE, 0.08f * a));
        }
    }

    Vector2 aim() const { return devLight_ ? Vector2{VW / 2.0f + 40, VH / 2.0f - 40} : E().mouse(); }

    // the flashlight over a view: darkness, the beam, the haze, your hand
    void lightPass(float dark, Vector2 windowPos, bool hands = true) {
        gfx::Light l;
        l.pos = aim();
        l.power = beamPower();
        l.radius = 300 * (0.75f + 0.25f * std::min(1.0f, b_.battery * 2));
        l.dark = dark * (1 - 0.5f * lightning());
        l.tint = b_.battery < 0.2f ? rgba(0xffc890ff) : rgba(0xfff0d8ff);
        Vector2 hand{1000 + (l.pos.x - 640) * 0.16f, 676 + (l.pos.y - 360) * 0.08f};
        l.hand = hand + Vector2Normalize(l.pos - hand) * 110;
        if (windowPos.x > 0) {
            l.add(windowPos, 240, 0.4f);  // moonlight through the window at the end of the hall
            if (float L = lightning(); L > 0) l.add(windowPos, 320, 0.8f * L);
        }
        gfx::darkness(l, t_);
        gfx::haze(l, t_);
        if (!hands) return;
        float bob = std::sin(t_ * 1.7f) * 4 + (moveT_ > 0 ? 20 : 0);
        if (doorAnim_ > 0.05f) {
            gfx::holdingHands(doorAnim_ + doorKick_ * 0.4f, t_, view_ == View::Closet);
            if (dr_.handT >= 0) gfx::thirdHand(dr_.handT / 2.6f, t_);
        }
        else if (lightOn()) gfx::flashlightHand(l.pos, t_, l.power, bob);
        DrawRectangleGradientV(0, VH - 220, VW, 220, alpha(BLACK, 0), alpha(BLACK, 0.55f));
    }

    void drawHud() {
        // clock
        float jitter = hushT_ >= 0 && std::fmod(t_, 0.7f) < 0.1f ? 3.0f : 0.0f;
        bool near = dr_.spot >= 0 && dr_.at == view_;
        std::string clk = clockText();
        if (near && std::fmod(t_, 1.3f) < 0.18f) clk = std::fmod(t_, 2.6f) < 1.3f ? "?? AM" : "13 AM";
        Color cc = mix(mix(rgba(0xf4ecffff), col::gold, hourPulse_), col::red, redPulse_);
        ui::textShadow(clk, VW - 30 + jitter, 44, 44, cc, ui::Right, true, 3);
        ui::text("NIGHT " + std::to_string(night_ + 1), VW - 30, 82, 18, col::muted, ui::Right, true);
        // lures
        for (int s = 0; s < 2; s++) {
            Vector2 p{30.0f + s * 54, 42};
            const char* item = s == 0 ? "stroopwafel" : "boba";
            DrawCircleV(p, 22, alpha(BLACK, 0.5f));
            art::itemIcon(item, p, 34);
            if (b_.hall[s].lureUsed || P().count(item) == 0) {
                DrawCircleV(p, 22, alpha(BLACK, 0.6f));
                DrawLineEx({p.x - 14, p.y - 14}, {p.x + 14, p.y + 14}, 3, col::red);
            }
            ui::text(std::to_string(P().count(item)), p.x + 16, p.y + 18, 13, col::text, ui::Center, true);
        }
        ui::text("Q lure", 57, 78, 13, col::muted, ui::Center, true);
        // battery: a torch icon with five cells
        {
            float x = VW - 186, y = VH - 52;
            bool charging = view_ == View::Center && b_.battery < 1;
            Color c = b_.battery < 0.2f ? col::red : b_.battery < 0.45f ? col::orange : rgba(0xd8f0c8ff);
            if (b_.battery < 0.2f && std::fmod(t_, 0.8f) < 0.4f) c = alpha(c, 0.4f);
            DrawRectangleRounded({x - 6, y - 6, 166, 34}, 0.3f, 6, alpha(BLACK, 0.45f));
            DrawRectangleRoundedLinesEx({x, y, 140, 22}, 0.3f, 6, 2, c);
            DrawRectangleRec({x + 140, y + 6, 6, 10}, c);
            for (int i = 0; i < 5; i++) {
                float fill = clamp01(b_.battery * 5 - i);
                if (fill > 0) DrawRectangleRec({x + 4 + i * 27.0f, y + 4, 24 * fill, 14}, c);
            }
            ui::text(charging ? "CHARGING" : "TORCH", x + 70, y - 14, 13, charging ? col::green : col::muted, ui::Center, true);
        }
        // noise: how much of you the house can hear
        {
            float x = VW - 186, y = VH - 104;
            ui::text("NOISE", x - 2, y + 9, 13, col::muted, ui::Left, true);
            for (int i = 0; i < 10; i++) {
                float h = 6 + i * 2.2f, on = b_.noise * 10 - i;
                Color c = i < 5 ? rgba(0x8a9ac8ff) : i < 8 ? col::orange : col::red;
                DrawRectangleRec({x + 50 + i * 10.0f, y + 18 - h, 6, h}, on > 0 ? alpha(c, 0.9f) : alpha(WHITE, 0.12f));
            }
        }
        // controls
        if (night_ == 0 || b_.t < 25) {
            float a = night_ == 0 ? 0.6f : clamp01((25 - b_.t) / 3) * 0.6f;
            const char* hint = view_ == View::Center ? "A/D doors   W closet   S bed   E turn off PC   (the torch charges here)"
                                                     : "F / left mouse = flashlight     SPACE / right mouse = hold shut     back: opposite key";
            ui::text(hint, 24, VH - 20, 15, alpha(col::text, a), ui::Left, true);
        }
        drawPhone();
        // heartbeat vignette
        float dg = b_.danger();
        if (dg > 0.2f) {
            float a = (0.1f + 0.2f * heartPulse_) * dg;
            DrawRectangleGradientV(0, 0, VW, 140, alpha(rgba(0x600010ff), a), alpha(BLACK, 0));
            DrawRectangleGradientV(0, VH - 140, VW, 140, alpha(BLACK, 0), alpha(rgba(0x600010ff), a));
            DrawRectangleGradientH(0, 0, 160, VH, alpha(rgba(0x600010ff), a), alpha(BLACK, 0));
            DrawRectangleGradientH(VW - 160, 0, 160, VH, alpha(BLACK, 0), alpha(rgba(0x600010ff), a));
        }
    }

    // a text slides in on Pieter's phone
    void drawPhone() {
        if (textT_ < 0) return;
        float in = easeOutBack(clamp01(textT_ / 0.4f)), out = clamp01((textT_ - 6.0f) / 0.5f);
        float x = 24 - (1 - in) * 420 - out * 420;
        Rectangle r{x, VH - 190, 380, 92};
        DrawRectangleRounded({r.x + 4, r.y + 6, r.width, r.height}, 0.25f, 8, alpha(BLACK, 0.5f));
        DrawRectangleRounded(r, 0.25f, 8, rgba(0x15121ef0));
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, rgba(0x3a3450ff));
        std::string from = shown_.from;
        Vector2 av{r.x + 40, r.y + 46};
        if (shown_.other) {
            DrawCircleV(av, 25, BLACK);
            E().beginClip({av.x - 24, av.y - 24, 48, 48});
            gfx::otherFace(av + Vector2{0, 6}, 26, t_, 0, 0);
            E().endClip();
        } else if (from == "Davi") art::portrait("davi", av, 24);
        else if (from == "Kira") art::portrait("kira", av, 24);
        else if (from == "Pieter") art::portrait("pieter", av, 24);
        else {
            DrawCircleV(av, 24, from == "Mama" ? rgba(0xd04a7aff) : rgba(0x2a2a34ff));
            ui::text(from == "Mama" ? "M" : "?", av.x, av.y, 24, col::text, ui::Center, true);
        }
        ui::text(from, r.x + 76, r.y + 20, 15, from == "???" ? col::red : col::text, ui::Left, true);
        ui::text(shown_.other ? "" : "now", r.x + r.width - 16, r.y + 20, 12, col::muted, ui::Right, true);
        ui::paragraph(shown_.msg, {r.x + 76, r.y + 34, r.width - 92, 0}, 14, shown_.other ? rgba(0xe8e0e0ff) : rgba(0xd8d0e8ff));
    }

    // ============================================================ draw: centre (the room)
    Vector2 camShift() const {
        Vector2 m = E().mouse();
        return {-(m.x - VW / 2.0f) * 0.035f, -(m.y - VH / 2.0f) * 0.02f};
    }

    void sideDoor(bool left) {
        auto wallY = [](float x, float k) {
            float top = -108 + 108 * (x / 170), floor = 531 - 61 * (x / 170);
            return lerpf(top, floor, k);
        };
        float x0 = 22, x1 = 142;
        auto P2 = [&](float x, float k) { return left ? Vector2{x, wallY(x, k)} : Vector2{VW - x, wallY(x, k)}; };
        Vector2 tl = P2(x0, 0.22f), tr = P2(x1, 0.22f), br = P2(x1, 1), bl = P2(x0, 1);
        Vector2 ftl = P2(x0 - 10, 0.19f), ftr = P2(x1 + 8, 0.19f), fbr = P2(x1 + 8, 1), fbl = P2(x0 - 10, 1);
        gfx::quad(ftl, ftr, fbr, fbl, rgba(0x6a4a8aff));
        gfx::quad(tl, tr, br, bl, rgba(0x04030aff), rgba(0x0c0818ff));
        gfx::quad(Vector2Lerp(bl, br, 0.2f), Vector2Lerp(bl, br, 0.6f), Vector2Lerp(bl, br, 0.6f) + Vector2{0, -6}, Vector2Lerp(bl, br, 0.2f) + Vector2{0, -6},
                  alpha(rgba(0x6878d8ff), 0.5f));
        int s = left ? 0 : 1;
        doorQuad_[s][0] = ftl, doorQuad_[s][1] = ftr, doorQuad_[s][2] = fbr, doorQuad_[s][3] = fbl;
        doorEyePos_[s] = Vector2Lerp(Vector2Lerp(tl, tr, 0.5f), Vector2Lerp(bl, br, 0.5f), 0.32f);
        // someone standing in the doorway blocks the moonlight on the floor
        if (b_.hall[s].pos == 4 && !b_.hall[s].feint)
            gfx::quad(Vector2Lerp(bl, br, 0.15f), Vector2Lerp(bl, br, 0.7f), Vector2Lerp(bl, br, 0.7f) + Vector2{0, -10}, Vector2Lerp(bl, br, 0.15f) + Vector2{0, -10},
                      alpha(BLACK, 0.85f));
    }

    void centerEyes() {
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < 4; i++) DrawLineEx(doorQuad_[s][i], doorQuad_[s][(i + 1) % 4], 3, alpha(rgba(0x8f9cffff), 0.35f));
        for (int s = 0; s < 2; s++) {
            const auto& h = b_.hall[s];
            if (h.pos >= 2 && h.pos <= 3 && !h.hidden) gfx::eyes(s, doorEyePos_[s], h.pos == 3 ? 110 : 75, h.pos == 3 ? 0.8f : 0.55f, t_);
        }
        if (b_.horse >= 2) gfx::eyes(2, {655 - 26, 296}, 60, 0.25f + 0.2f * b_.horse, t_);
        // plushies on the bed, button eyes catching the light
        fx::beginAdd();
        for (int i = 0; i < b_.plush; i++) {
            Vector2 e{236 + i * 64.0f + (i % 2 ? -13.0f : 13.0f), 463 + (i % 2) * 6.0f};
            fx::glow(e, 12, alpha(rgba(0xff9020ff), 0.6f));
            fx::glow(e, 3, alpha(WHITE, 0.8f));
        }
        fx::endAdd();
        if (float L = lightning(); L > 0.2f)
            for (int s = 0; s < 2; s++) {
                gfx::quad(doorQuad_[s][0], doorQuad_[s][1], doorQuad_[s][2], doorQuad_[s][3], alpha(rgba(0x9aa8e0ff), 0.55f * L), alpha(rgba(0x404870ff), 0.4f * L));
                if (b_.hall[s].pos >= 3) {
                    Vector2 c = doorEyePos_[s];
                    Color k = alpha(BLACK, 0.9f * L);
                    DrawCircleV({c.x, c.y}, 26, k);
                    DrawRectangleRounded({c.x - 34, c.y + 22, 68, 150}, 0.4f, 6, k);
                    for (int a : {-1, 1}) DrawLineEx({c.x + a * 30, c.y + 40}, {c.x + a * 44, c.y + 170}, 12, k);
                }
            }
        if (monkeyRise_ > 0.3f) {
            fx::beginAdd();
            for (int k : {-1, 1}) {
                Vector2 e = monkeyHead() + Vector2{k * kMonkeyR * 0.28f, -kMonkeyR * 0.14f};
                fx::glow(e, 18, alpha(rgba(0xffb020ff), 0.6f * monkeyRise_));
                fx::glow(e, 5, alpha(WHITE, 0.8f * monkeyRise_));
            }
            fx::endAdd();
        }
    }

    static constexpr float kMonkeyR = 44;
    Vector2 monkeyHead() const {
        Rectangle g = room::kWindowGlass;
        float bang = monkeyBang_ > 0 ? std::sin(monkeyBang_ * 40) * 4 : 0;
        return {g.x + 70 + std::sin(t_ * 0.7f) * 6 + bang, g.y + g.height + 70 - lerpf(0, 150, easeOutCubic(clamp01(monkeyRise_)))};
    }

    void outsideWindow() {
        Rectangle g = room::kWindowGlass;
        float L = lightning();
        if (L > 0) DrawRectangleRec(g, alpha(rgba(0xd8e0ffff), 0.75f * L));
        if (L > 0.5f && std::fmod(t_ * 7, 1) < 0.5f) {
            Vector2 p{g.x + 150, g.y};
            for (int i = 0; i < 6; i++) {
                Vector2 q = p + Vector2{(gfx::hash(i + std::floor(thunderT_)) - 0.5f) * 30, g.height / 6};
                DrawLineEx(p, q, 2, alpha(WHITE, L));
                p = q;
            }
        }
        gfx::windmill({g.x + 170, g.y + g.height + 2}, 0.9f, t_, rgba(0x0a0618ff));
        if (monkeyRise_ > 0.02f || monkeyBang_ > 0) {
            Vector2 h = monkeyHead();
            float angry = std::max(monkeyBang_, b_.monkeyRiser ? clamp01((b_.monkeyStare - 1.6f) / 3) * 0.5f : 0);
            for (int k : {-1, 1}) {
                Vector2 hand{h.x + k * 66, h.y + 30 - (monkeyBang_ > 0 ? std::abs(std::sin(monkeyBang_ * 20)) * 16 : 0)};
                DrawEllipse((int)hand.x, (int)hand.y, 14, 17, rgba(0x4a3428ff));
                for (int f = 0; f < 4; f++) DrawLineEx(hand, hand + Vector2{(f - 1.5f) * 7.0f, -26}, 6, rgba(0x4a3428ff));
            }
            gfx::monkeyMan(h, kMonkeyR, t_, angry);
        }
        for (int i = 0; i < 26; i++) {
            float x = g.x + gfx::hash(i + 0.3f) * g.width, sp = 120 + gfx::hash(i + 5.0f) * 120;
            float y = g.y - 20 + std::fmod(t_ * sp + gfx::hash(i + 2.0f) * 400, g.height + 40);
            DrawLineEx({x, y}, {x - 2, y + 12}, 1.2f, alpha(rgba(0xc0d0ffff), 0.25f));
        }
    }

    void closetCenter() {
        Rectangle c{548, 176, 214, 296};
        fx::softShadow({c.x + c.width / 2, c.y + c.height}, 130, 12, 0.6f);
        DrawRectangleRounded(shrink(c, -4), 0.04f, 6, col::outline);
        DrawRectangleRec(c, rgba(0x3a2418ff));
        DrawRectangleRec({c.x - 8, c.y - 14, c.width + 16, 18}, rgba(0x4a2e1eff));
        float gap = b_.horse >= 4 ? 120 : 6 + b_.horse * 6.0f;
        DrawRectangleRec({c.x + c.width / 2 - gap / 2, c.y + 10, gap, c.height - 24}, rgba(0x020104ff));
        for (int k : {-1, 1}) {
            float x = c.x + c.width / 2 + k * (gap / 2 + 4);
            DrawRectangleLinesEx({k < 0 ? c.x + 10 : x, c.y + 14, c.width / 2 - gap / 2 - 14, 120}, 2, alpha(BLACK, 0.4f));
            DrawRectangleLinesEx({k < 0 ? c.x + 10 : x, c.y + 146, c.width / 2 - gap / 2 - 14, 120}, 2, alpha(BLACK, 0.4f));
            DrawCircleV({c.x + c.width / 2 + k * (gap / 2 + 10), c.y + 150}, 4, col::goldDark);
        }
    }

    // the flashlight in its charging dock on the desk
    void chargerDock() {
        Vector2 d{1158, 452};
        DrawRectangleRounded({d.x - 34, d.y - 8, 68, 16}, 0.4f, 6, rgba(0x1a1a22ff));
        if (view_ == View::Center) {
            DrawRectangleRounded({d.x - 30, d.y - 26, 62, 18}, 0.4f, 6, rgba(0x2a2a34ff));
            DrawRectangleRounded({d.x + 26, d.y - 30, 12, 26}, 0.3f, 4, rgba(0x3a3a46ff));
        }
        Color led = b_.battery >= 1 ? col::green : std::fmod(t_, 1.0f) < 0.5f ? col::orange : darken(col::orange, 0.6f);
        DrawCircleV({d.x - 22, d.y}, 3, led);
        fx::beginAdd();
        fx::glow({d.x - 22, d.y}, 14, alpha(led, 0.6f));
        fx::endAdd();
    }

    void drawCenter() {
        Vector2 cam = camShift();
        rlPushMatrix();
        rlTranslatef(VW / 2.0f + cam.x, VH / 2.0f + cam.y, 0);
        rlScalef(1.06f, 1.06f, 1);
        rlTranslatef(-VW / 2.0f, -VH / 2.0f, 0);
        room::drawWalls(env_);
        room::drawWindow(env_);
        room::drawWallDecor(env_);
        room::drawFloorLight(env_);
        sideDoor(true);
        sideDoor(false);
        closetCenter();
        room::drawBed(env_);
        for (int i = 0; i < b_.plush; i++) {  // little lumps on the bed
            Vector2 p{236 + i * 64.0f, 480 + (i % 2) * 6.0f};
            DrawEllipse((int)p.x, (int)p.y, 17, 12, rgba(0x3a2820ff));
            DrawCircleV({p.x + (i % 2 ? -11.0f : 11.0f), p.y - 15}, 10, rgba(0x3a2820ff));
        }
        room::drawDesk(env_);
        chargerDock();
        gfx::toyMonkey({922, 450}, 1.1f, clashAnim_);
        Rectangle mon{926, 286, 210, 120};
        if (b_.pc) {
            DrawRectangleRec(mon, std::fmod(t_, 0.5f) < 0.25f ? rgba(0x300818ff) : rgba(0x18040cff));
            E().beginClip(mon);
            gfx::nightmare(1, {mon.x + mon.width / 2, mon.y + mon.height / 2 + 6}, 48 + std::sin(t_ * 9) * 3, std::sin(t_ * 5) * 10, 1, t_);
            float ox = gfx::hash(std::floor(t_ * 30)) * 256;
            DrawTexturePro(noise_, {ox, 0, 100, 60}, mon, {0, 0}, 0, alpha(WHITE, 0.25f));
            E().endClip();
            ui::text("HIHIHI", mon.x + mon.width / 2, mon.y + 14, 14, col::pink, ui::Center, true);
        }
        room::drawRug(env_);
        if (dr_.spot >= 0 && dr_.at == View::Center) {
            Vector2 feet;
            float sc;
            roomSpot(dr_.spot, feet, sc);
            float a = dr_.fade * (dr_.hover > 2.4f ? 1 - clamp01((dr_.hover - 2.4f) / 0.8f) : 1);
            gfx::other(feet, sc, t_, dr_.look, a);
            Vector2 head = gfx::otherHead(feet, sc, dr_.look);
            dr_.rect = {feet.x - 70 * sc, head.y - 70 * sc, 140 * sc, feet.y - head.y + 70 * sc};
        }
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawTexturePro(lm_.texture, {0, 0, (float)lm_.texture.width, -(float)lm_.texture.height}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, WHITE);
        EndBlendMode();
        room::drawBloom(env_);
        if (float L = lightning(); L > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangle(-100, -100, VW + 200, VH + 200, alpha(rgba(0x8090c0ff), 0.18f * L));
            EndBlendMode();
        }
        centerEyes();
        // his face, pale in the room's light, where you'd least want it
        if (dr_.spot >= 0 && dr_.at == View::Center && dr_.fade > 0.5f) {
            Vector2 head{dr_.rect.x + dr_.rect.width / 2, dr_.rect.y + 70 * dr_.rect.width / 140};
            // the face is the one thing that catches what little light there is
            Vector2 feet;
            float sc;
            roomSpot(dr_.spot, feet, sc);
            gfx::otherFace(gfx::otherHead(feet, sc, dr_.look), 58 * sc, t_, 0, dr_.look * 22 + 4, 0.55f * dr_.fade * (dr_.hover > 2.4f ? 1 - clamp01((dr_.hover - 2.4f) / 0.8f) : 1));
            (void)head;
        }
        // in the black monitor: Pieter's reflection, and someone standing behind him
        if (dr_.reflectT >= 0 && !b_.pc) {
            float a = clamp01(dr_.reflectT / 1.2f) * clamp01((6 - dr_.reflectT) / 1.2f);
            Rectangle mon{926, 286, 210, 120};
            E().beginClip(mon);
            DrawCircleV({mon.x + 96, mon.y + 92}, 30, alpha(rgba(0x0c0a10ff), 0.9f * a));
            DrawEllipse((int)(mon.x + 96), (int)(mon.y + 150), 70, 40, alpha(rgba(0x0c0a10ff), 0.9f * a));
            if (a > 0.05f) {
                BeginBlendMode(BLEND_ADDITIVE);
                gfx::otherFace({mon.x + 150, mon.y + 44}, 30, t_, 0, 6);
                EndBlendMode();
                DrawRectangleRec(mon, alpha(rgba(0x06060aff), 1 - a * 0.7f));
            }
            E().endClip();
        }
        if (moveT_ <= 0 && phase_ == Phase::Play) hotspots();
        rlPopMatrix();
        art::vignette(0.45f);
        if (hushT_ >= 0) DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.25f * clamp01(hushT_)));
    }

    void hotspots() {
        struct H { Rectangle r; View v; const char* label; };
        H hs[] = {{{0, 40, 170, 520}, View::Left, "LEFT DOOR  [A]"},
                  {{VW - 170.0f, 40, 170, 520}, View::Right, "RIGHT DOOR  [D]"},
                  {{548, 170, 214, 300}, View::Closet, "CLOSET  [W]"},
                  {{166, 360, 296, 200}, View::Bed, "BED  [S]"}};
        Vector2 m = E().mouse();
        Vector2 cam = camShift();
        Vector2 rm{(m.x - VW / 2.0f - cam.x) / 1.06f + VW / 2.0f, (m.y - VH / 2.0f - cam.y) / 1.06f + VH / 2.0f};
        for (auto& h : hs) {
            if (!CheckCollisionPointRec(rm, h.r)) continue;
            DrawRectangleRoundedLinesEx(shrink(h.r, 6), 0.08f, 8, 2, alpha(col::text, 0.25f + 0.15f * std::sin(t_ * 5)));
            ui::textShadow(h.label, h.r.x + h.r.width / 2, h.r.y + h.r.height / 2, 18, alpha(col::text, 0.8f), ui::Center, true, 2);
            if (E().mousePressed()) {
                E().consumeClick();
                go(h.v);
            }
            return;
        }
        Rectangle mon{926, 286, 210, 120};
        if (b_.pc && CheckCollisionPointRec(rm, mon)) {
            DrawRectangleRoundedLinesEx(shrink(mon, -6), 0.08f, 8, 3, col::pink);
            ui::textShadow("TURN OFF  [E]", mon.x + mon.width / 2, mon.y + mon.height + 24, 18, col::pink, ui::Center, true, 2);
            if (E().mousePressed()) {
                E().consumeClick();
                pcClick_ = true;
            }
        }
    }

    // ============================================================ draw: hallways
    static constexpr Rectangle kOpening{292, 34, 696, 720};
    static constexpr float kVPx = 640, kVPy = 330, kFar = 0.18f;
    static float hallScale(float d) { return 1 / (1 + d * (1 / kFar - 1)); }
    static Vector2 hallPt(float u, float v, float d) {
        float s = hallScale(d);
        return {kVPx + u * 348 * s, kVPy + (v < 0 ? v * 296 : v * 390) * s};
    }
    // further down the hall it gets darker and mistier
    static Color fog(Color c, float d) { return mix(c, rgba(0x0a0814ff), clamp01(d * 0.85f)); }
    static float posDepth(int pos) { return pos == 1 ? 0.9f : pos == 2 ? 0.52f : pos == 3 ? 0.14f : 1.2f; }
    static float posU(int side, int pos) { return (side == 0 ? 1 : -1) * (pos == 1 ? 0.25f : pos == 2 ? 0.22f : 0.06f); }

    void hallWall(int k, float d0, float d1, float v0, float v1, Color c0, Color c1) {
        gfx::quad(hallPt((float)k, v0, d0), hallPt((float)k, v0, d1), hallPt((float)k, v1, d1), hallPt((float)k, v1, d0), fog(c0, d0), fog(c1, d0));
    }

    void grandfatherClock(float d) {
        float s = hallScale(d);
        Vector2 base = hallPt(-1, 1, d) + Vector2{8 * s, 0};
        float w = 70 * s, h = 380 * s;
        Rectangle body{base.x, base.y - h, w, h};
        Color wood = fog(rgba(0x3a2214ff), d);
        DrawRectangleRec(body, wood);
        DrawRectangleRec({body.x - 6 * s, body.y - 16 * s, w + 12 * s, 20 * s}, fog(rgba(0x4a2c1aff), d));
        DrawCircleV({body.x + w / 2, body.y + 40 * s}, 26 * s, fog(rgba(0xd8ccb0ff), d));  // face
        float hr = hour() / 12.0f * 2 * PI, mn = std::fmod(b_.t / kHourLen, 1.0f) * 2 * PI;
        Vector2 fc{body.x + w / 2, body.y + 40 * s};
        DrawLineEx(fc, fc + fromAngle(hr - PI / 2, 14 * s), 2.5f * s, BLACK);
        DrawLineEx(fc, fc + fromAngle(mn - PI / 2, 20 * s), 1.5f * s, BLACK);
        // the pendulum swings... unless something's about to happen
        Rectangle glass{body.x + 12 * s, body.y + 90 * s, w - 24 * s, 230 * s};
        DrawRectangleRec(glass, fog(rgba(0x120a08ff), d));
        float swing = hushT_ >= 0 ? 0.0f : std::sin(t_ * PI) * 0.22f;
        Vector2 piv{glass.x + glass.width / 2, glass.y + 6 * s}, bob = piv + fromAngle(PI / 2 + swing, 190 * s);
        DrawLineEx(piv, bob, 2 * s, fog(rgba(0xc8a040ff), d));
        DrawCircleV(bob, 13 * s, fog(rgba(0xd8b048ff), d));
    }

    void drawHall(int side) {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        const auto& h = b_.hall[side];
        rlPushMatrix();
        rlTranslatef(pan.x, pan.y, 0);
        // bedroom wall around the door
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x2a1a50ff));
        for (int x = -60; x < VW + 60; x += 48) DrawRectangle(x, -20, 22, VH + 40, alpha(WHITE, 0.02f));
        float sx = side == 0 ? 1080 : 236;
        DrawRectangleRounded({sx - 18, 330, 36, 56}, 0.2f, 6, rgba(0xd8d0e0ff));
        DrawRectangleRounded({sx - 6, 344, 12, 26}, 0.3f, 6, rgba(0xa098b0ff));
        if (side == 0) {
            Rectangle tile{1050, 120, 170, 170};
            DrawRectangleRec({tile.x + 6, tile.y + 8, tile.width, tile.height}, alpha(BLACK, 0.4f));
            DrawRectangleRec(tile, rgba(0xe8eef8ff));
            DrawRectangleLinesEx(shrink(tile, 8), 3, rgba(0x2a4aa8ff));
            for (Vector2 c : {Vector2{tile.x + 20, tile.y + 20}, {tile.x + tile.width - 20, tile.y + 20}, {tile.x + 20, tile.y + tile.height - 20},
                              {tile.x + tile.width - 20, tile.y + tile.height - 20}})
                DrawPoly(c, 4, 7, 45, rgba(0x2a4aa8ff));
            bool late = night_ >= 3;
            Color ink = late ? rgba(0x8a1020ff) : rgba(0x1e3a90ff);
            ui::text(late ? "NOBODY" : "HOME", tile.x + tile.width / 2, tile.y + 62, 22, ink, ui::Center, true);
            ui::text(late ? "COMES" : "SWEET", tile.x + tile.width / 2, tile.y + 92, 22, ink, ui::Center, true);
            ui::text("HOME", tile.x + tile.width / 2, tile.y + 122, 22, ink, ui::Center, true);
        } else {
            Rectangle poster{24, 110, 180, 230};
            DrawRectangleRec({poster.x + 6, poster.y + 8, poster.width, poster.height}, alpha(BLACK, 0.4f));
            DrawRectangleRec(poster, rgba(0xd8c070ff));
            DrawRectangleRec(shrink(poster, 10), rgba(0x1a1210ff));
            gfx::monkeyMan({poster.x + poster.width / 2, poster.y + 92}, 42, t_, 0.6f);
            ui::text("MONKEY MEN", poster.x + poster.width / 2, poster.y + 166, 22, rgba(0xffd84aff), ui::Center, true);
            ui::text("live @ Paradiso", poster.x + poster.width / 2, poster.y + 190, 15, rgba(0xe8e0d0ff), ui::Center, true);
            ui::text("31 OKT  -  UITVERKOCHT", poster.x + poster.width / 2, poster.y + 210, 11, rgba(0xff6a4aff), ui::Center, true);
        }

        E().beginClip({kOpening.x + pan.x, kOpening.y, kOpening.width, kOpening.height});
        // far wall with the window, the moon, the rain
        Vector2 ftl = hallPt(-1, -1, 1), fbr = hallPt(1, 1, 1);
        DrawRectangleV(ftl, fbr - ftl, rgba(0x14101eff));
        Rectangle win{kVPx - 26, kVPy - 44, 52, 62};
        float L = lightning();
        DrawRectangleRec(win, mix(rgba(0x1a2050ff), rgba(0xd8e0ffff), L));
        DrawCircleV({win.x + 34, win.y + 18}, 9, rgba(0xfff6d8ff));
        gfx::windmill({win.x + 16, win.y + win.height}, 0.4f, t_, rgba(0x0a0618ff));
        for (int i = 0; i < 6; i++) {
            float x = win.x + gfx::hash(i + 1.7f) * win.width, y = win.y + std::fmod(t_ * 60 + gfx::hash(i) * 80, win.height);
            DrawLineEx({x, y}, {x - 1, y + 5}, 1, alpha(rgba(0xc0d0ffff), 0.3f));
        }
        DrawLineEx({win.x + win.width / 2, win.y}, {win.x + win.width / 2, win.y + win.height}, 3, rgba(0x0c0814ff));
        DrawLineEx({win.x, win.y + win.height / 2}, {win.x + win.width, win.y + win.height / 2}, 3, rgba(0x0c0814ff));
        DrawRectangleLinesEx(win, 3, rgba(0x0c0814ff));
        for (int k : {-1, 1}) {  // curtains stirring in the draught
            float sway = std::sin(t_ * 1.3f + k) * 3;
            gfx::quad({win.x + (k < 0 ? -10 : win.width - 4), win.y - 6}, {win.x + (k < 0 ? 4 : win.width + 10), win.y - 6},
                      {win.x + (k < 0 ? 8 : win.width + 12) + sway, win.y + win.height + 8}, {win.x + (k < 0 ? -12 : win.width - 8) + sway, win.y + win.height + 8},
                      rgba(0x3a1a2aff), rgba(0x24101aff));
        }
        // ceiling, walls (wallpaper above, wooden panelling below)
        const int N = 10;
        for (int i = 0; i < N; i++) {
            float d0 = (float)i / N, d1 = (float)(i + 1) / N;
            gfx::quad(hallPt(-1, -1, d0), hallPt(1, -1, d0), hallPt(1, -1, d1), hallPt(-1, -1, d1), fog(rgba(0x1a1428ff), d0), fog(rgba(0x1a1428ff), d1));
            for (int k : {-1, 1}) {
                hallWall(k, d0, d1, -1, 0.35f, rgba(0x3a2852ff), rgba(0x2c1e40ff));
                hallWall(k, d0, d1, 0.35f, 1, rgba(0x2a1a14ff), rgba(0x1e120cff));
            }
        }
        for (int i = 0; i < 18; i++) {  // wallpaper stripes and damask diamonds
            float d = i / 18.0f, sc = hallScale(d);
            for (int k : {-1, 1}) {
                DrawLineEx(hallPt((float)k, -1, d), hallPt((float)k, 0.35f, d), 1.5f * sc, alpha(BLACK, 0.22f));
                for (int j = 0; j < 4; j++) DrawPoly(hallPt((float)k, -0.75f + j * 0.3f, d + 0.028f), 4, 6 * sc, 0, alpha(fog(rgba(0x5a4278ff), d), 0.6f));
            }
        }
        for (int k : {-1, 1}) {
            DrawLineEx(hallPt((float)k, 0.35f, 0), hallPt((float)k, 0.35f, 1), 5, rgba(0x4a3020ff));
            for (int i = 0; i < 8; i++) {  // panel mouldings
                float d0 = i / 8.0f + 0.02f, d1 = (i + 1) / 8.0f - 0.02f;
                Vector2 a = hallPt((float)k * 0.995f, 0.45f, d0), b = hallPt((float)k * 0.995f, 0.45f, d1), c = hallPt((float)k * 0.995f, 0.9f, d1), e = hallPt((float)k * 0.995f, 0.9f, d0);
                Color line = alpha(fog(rgba(0x0a0604ff), d0), 0.6f);
                DrawLineEx(a, b, 2, line);
                DrawLineEx(b, c, 2, line);
                DrawLineEx(c, e, 2, line);
                DrawLineEx(e, a, 2, line);
            }
        }
        // floorboards and a long Persian runner
        for (int i = 0; i < N; i++) {
            float d0 = (float)i / N, d1 = (float)(i + 1) / N;
            gfx::quad(hallPt(-1, 1, d0), hallPt(1, 1, d0), hallPt(1, 1, d1), hallPt(-1, 1, d1), fog(rgba(0x3a2416ff), d0), fog(rgba(0x3a2416ff), d1));
            gfx::quad(hallPt(-0.5f, 1, d0), hallPt(0.5f, 1, d0), hallPt(0.5f, 1, d1), hallPt(-0.5f, 1, d1), fog(rgba(0x6a1820ff), d0), fog(rgba(0x6a1820ff), d1));
            for (float u : {-0.42f, 0.42f}) DrawLineEx(hallPt(u, 1, d0), hallPt(u, 1, d1), 4 * hallScale(d0), fog(rgba(0xc89a40ff), d0));
            DrawPoly(hallPt(0, 1, (d0 + d1) / 2), 4, 26 * hallScale(d0), 0, fog(rgba(0x2a3a6aff), d0));
        }
        for (int j = -6; j <= 6; j++)
            if (std::abs(j) > 3) DrawLineEx(hallPt(j / 6.5f, 1, 0), hallPt(j / 6.5f, 1, 1), 1.5f, alpha(BLACK, 0.4f));
        // doors along the hall, the clock / the bathroom
        {
            float d = 0.3f, s = hallScale(d);
            Vector2 a = hallPt(side == 0 ? 1 : -1, -0.45f, d), b = hallPt(side == 0 ? 1 : -1, 1, d + 0.12f);
            Color door = fog(rgba(0x3a2418ff), d);
            gfx::quad(a, {b.x, hallPt(side == 0 ? 1 : -1, -0.45f, d + 0.12f).y}, b, hallPt(side == 0 ? 1 : -1, 1, d), door);
            DrawCircleV(Vector2Lerp(a, b, 0.55f), 4 * s, fog(rgba(0xc89a40ff), d));
        }
        if (side == 0) {
            grandfatherClock(0.47f);
            for (int i = 0; i < 2; i++) {  // Delft blue plates
                float d = 0.16f + i * 0.5f, s = hallScale(d);
                Vector2 p = hallPt(-1, -0.4f, d);
                DrawEllipse((int)(p.x + 6 * s), (int)p.y, 9 * s, 26 * s, fog(rgba(0xe8eef8ff), d));
                DrawEllipse((int)(p.x + 6 * s), (int)p.y, 6 * s, 18 * s, fog(rgba(0x2a4aa8ff), d));
            }
            if (h.pos == 0) {  // Davi's klompen by the wall. When they're gone, he's wearing them.
                float kd = 0.2f, ks = hallScale(kd);
                Vector2 k = hallPt(0.74f, 1, kd);
                for (int i = 0; i < 2; i++) {
                    Vector2 c{k.x + i * 46 * ks, k.y - 14 * ks - i * 4 * ks};
                    DrawEllipse((int)c.x, (int)(c.y + 12 * ks), 30 * ks, 8 * ks, alpha(BLACK, 0.4f));
                    DrawEllipse((int)c.x, (int)c.y, 30 * ks, 13 * ks, rgba(0xe8c040ff));
                    DrawEllipse((int)(c.x + 14 * ks), (int)(c.y - 9 * ks), 13 * ks, 6 * ks, rgba(0x2a1a10ff));
                    DrawEllipse((int)(c.x - 10 * ks), (int)(c.y - 2 * ks), 7 * ks, 4 * ks, rgba(0xc83020ff));
                }
            }
        } else {
            // the bathroom door, ajar. Kira hides in there.
            float d0 = 0.44f, d1 = 0.6f;
            Vector2 a = hallPt(-1, -0.45f, d0), b = hallPt(-1, -0.45f, d1), c = hallPt(-1, 1, d1), e = hallPt(-1, 1, d0);
            gfx::quad(a, b, c, e, rgba(0x020104ff));
            Vector2 ea = hallPt(-0.78f, -0.45f, d0 - 0.05f), ee = hallPt(-0.78f, 1, d0 - 0.05f);
            gfx::quad(a, ea, ee, e, fog(rgba(0x4a3424ff), d0), fog(rgba(0x3a2418ff), d0));  // the door, swung into the hall
            if (h.hidden) {  // fingers curled round the edge of the door, an eye in the gap
                float s = hallScale(d0);
                for (int f = 0; f < 4; f++) {
                    Vector2 p = Vector2Lerp(ea, ee, 0.36f + f * 0.035f);
                    gfx::limb(p, p + Vector2{-12 * s, 2 * s}, 7 * s, rgba(0xb8a098ff));
                }
                gfx::eyes(1, Vector2Lerp(a, e, 0.22f) + Vector2{-6 * s, 0}, 40 * s, 0.7f, t_);
            }
            // a Dutch bike against the wall
            float d = 0.24f, s = hallScale(d);
            Vector2 bk = hallPt(0.8f, 1, d);
            Color bike = rgba(0x101018ff);
            DrawRing({bk.x - 60 * s, bk.y - 46 * s}, 40 * s, 46 * s, 0, 360, 32, bike);
            DrawRing({bk.x + 60 * s, bk.y - 46 * s}, 40 * s, 46 * s, 0, 360, 32, bike);
            DrawLineEx({bk.x - 60 * s, bk.y - 46 * s}, {bk.x - 10 * s, bk.y - 120 * s}, 7 * s, rgba(0x8a1a2aff));
            DrawLineEx({bk.x - 10 * s, bk.y - 120 * s}, {bk.x + 60 * s, bk.y - 46 * s}, 7 * s, rgba(0x8a1a2aff));
            DrawLineEx({bk.x + 20 * s, bk.y - 46 * s}, {bk.x - 10 * s, bk.y - 120 * s}, 6 * s, rgba(0x8a1a2aff));
            DrawLineEx({bk.x - 40 * s, bk.y - 130 * s}, {bk.x - 10 * s, bk.y - 120 * s}, 6 * s, bike);
        }
        // tulips in a vase, wilting a little more every night
        {
            float d = 0.36f, s = hallScale(d);
            Vector2 v = hallPt(side == 0 ? 0.8f : -0.8f, 1, d);
            DrawRectangleRounded({v.x - 14 * s, v.y - 50 * s, 28 * s, 50 * s}, 0.4f, 6, rgba(0x2a4aa8ff));
            DrawRectangleRounded({v.x - 9 * s, v.y - 38 * s, 18 * s, 16 * s}, 0.4f, 6, rgba(0xe8eef8ff));
            float droop = 0.15f + night_ * 0.22f;
            for (int i = 0; i < 3; i++) {
                float a = -PI / 2 + (i - 1) * 0.35f + droop * (i - 1 >= 0 ? 1 : -1) * (0.6f + i * 0.3f);
                Vector2 bb{v.x + (i - 1) * 5 * s, v.y - 50 * s};
                Vector2 m = bb + fromAngle(-PI / 2 + (i - 1) * 0.2f, 34 * s), tip = m + fromAngle(a + droop, 26 * s);
                DrawLineEx(bb, m, 2.5f * s, rgba(0x2a4a2aff));
                DrawLineEx(m, tip, 2.5f * s, rgba(0x2a4a2aff));
                DrawEllipse((int)tip.x, (int)tip.y, 7 * s, 9 * s, mix(rgba(0xc81a30ff), rgba(0x3a1a20ff), clamp01(night_ / 5.0f)));
            }
        }
        // a lamp hanging from the ceiling, swaying
        {
            Vector2 lamp = hallPt(0, -1, 0.4f);
            float ls = hallScale(0.4f), sw = std::sin(t_ * 0.8f) * 0.08f;
            Vector2 end = lamp + fromAngle(PI / 2 + sw, 60 * ls);
            DrawLineEx(lamp, end, 2, rgba(0x0c0814ff));
            DrawCircleSector(end + Vector2{0, 10 * ls}, 24 * ls, 180, 360, 16, rgba(0x2a2238ff));
        }
        // moonlight down the middle of the hall
        fx::beginAdd();
        gfx::quad(hallPt(-0.2f, 1, 1), hallPt(0.2f, 1, 1), hallPt(0.5f, 1, 0.4f), hallPt(-0.5f, 1, 0.4f), alpha(rgba(0x6878d8ff), 0.12f), alpha(rgba(0x6878d8ff), 0));
        fx::endAdd();

        // ---- him, if he's standing in this hall. Far away he's behind whoever is coming; close up, in front.
        bool otherHere = dr_.spot >= 0 && dr_.at == (side ? View::Right : View::Left);
        auto drawOther = [&] {
            Spot sp = hallSpot(side, dr_.spot);
            Vector2 feet = hallPt(sp.u, 1, sp.d);
            float sc = hallScale(sp.d) * sp.k;
            gfx::other(feet, sc, t_, dr_.look, dr_.fade);
            Vector2 head = gfx::otherHead(feet, sc, dr_.look);
            dr_.rect = {feet.x - 70 * sc + pan.x, head.y - 80 * sc, 140 * sc, feet.y - head.y + 80 * sc};
        };
        if (otherHere && dr_.spot <= 1) drawOther();

        // ---- the stalker
        bool visible = (h.pos >= 1 && h.pos <= 3 && !h.hidden) || h.fleeAnim >= 0;
        Vector2 headPos{-1, -1};
        float scale = 0;
        if (visible) {
            float d, u, k = 1;
            if (h.fleeAnim >= 0) {  // a flinch, then gone down the hall
                float f = h.fleeAnim;
                float d0 = posDepth(h.fleeFrom);
                d = lerpf(d0, 1.3f, easeInCubic(clamp01((f - 0.12f) / 0.6f)));
                u = posU(side, std::max(1, h.fleeFrom));
                k = f < 0.12f ? 0.96f : 1;
            } else {
                float to = posDepth(h.pos), from = posDepth(std::clamp(prevPos_[side], 1, 3));
                float m = clamp01(h.moveAnim / 0.5f);
                d = prevPos_[side] >= 1 && prevPos_[side] <= 3 ? lerpf(from, to, easeInOutSine(m)) : to;
                u = posU(side, h.pos);
            }
            if (d < 1.15f) {
                Vector2 feet = hallPt(u, 1, d);
                scale = hallScale(d) * 1.3f * k;
                float lit = beamPower() > 0 && view_ == (side ? View::Right : View::Left) && doorAnim_ < 0.5f ? 1.0f : 0.0f;
                if (lit > 0) gfx::stalkerShadow(side, feet, scale, {aim().x - pan.x, VH + 300.0f}, 0.8f);
                float flinch = h.fleeAnim >= 0 ? 1 - clamp01(h.fleeAnim / 0.5f) : clamp01(h.lightT * 3);
                if (dr_.ceiling[side] && h.pos == 2 && h.fleeAnim < 0) {
                    // on the ceiling, upside down, hair hanging towards the floor
                    Vector2 top = hallPt(u, -1, d);
                    rlDrawRenderBatchActive();
                    rlDisableBackfaceCulling();
                    rlPushMatrix();
                    rlTranslatef(top.x, top.y, 0);
                    rlScalef(1, -1, 1);
                    rlTranslatef(-top.x, -top.y, 0);
                    gfx::stalker(side, top, scale * 0.9f, t_, flinch, 0.15f);
                    rlPopMatrix();
                    rlDrawRenderBatchActive();
                    rlEnableBackfaceCulling();
                    headPos = Vector2{top.x, top.y + 548 * scale * 0.9f} + Vector2{pan.x, 0};
                } else {
                    gfx::stalker(side, feet, scale, t_, flinch, h.pos == 3 && h.fleeAnim < 0 ? 0.35f : 0);
                    headPos = gfx::stalkerHead(feet, scale) + Vector2{pan.x, 0};
                    if (dr_.turned[side] && h.pos == 1 && h.fleeAnim < 0) {
                        // facing the wall at the far end. You can only see the back of his head.
                        Vector2 hd = gfx::stalkerHead(feet, scale);
                        DrawEllipse((int)hd.x, (int)(hd.y + 4 * scale), 76 * scale, 86 * scale, rgba(0x1a140eff));
                        DrawEllipse((int)(hd.x - 10 * scale), (int)(hd.y - 20 * scale), 40 * scale, 30 * scale, alpha(rgba(0x3a2e22ff), 0.6f));
                        headPos = {-1, -1};
                    }
                }
            }
        }
        if (otherHere && dr_.spot >= 2) drawOther();
        if (h.lureAnim >= 0) {
            float k = clamp01(h.lureAnim / 0.9f);
            float d = k * 0.85f;
            Vector2 p = hallPt(0, 0.3f + k * 0.7f, d);
            p.y -= std::sin(k * PI) * 160 * hallScale(d);
            art::itemIcon(side == 0 ? "stroopwafel" : "boba", p, 80 * hallScale(d));
        }
        E().endClip();
        // ---- the door, held shut
        if (doorAnim_ > 0.01f) {
            float w = kOpening.width * doorAnim_;
            float kick = doorKick_ * 8 * (side == 0 ? 1 : -1);
            Rectangle d{(side == 0 ? kOpening.x : kOpening.x + kOpening.width - w) + kick, kOpening.y, w, kOpening.height};
            fx::tiled(E().tex("tex/wood"), d, 1.4f, {0, 0}, rgba(0x8a6a58ff));
            DrawRectangleGradientH((int)d.x, (int)d.y, (int)d.width, (int)d.height, alpha(BLACK, side == 0 ? 0.1f : 0.5f), alpha(BLACK, side == 0 ? 0.5f : 0.1f));
            if (doorAnim_ > 0.6f) {
                for (Rectangle p : {Rectangle{d.x + 60, d.y + 40, d.width - 120, 260}, Rectangle{d.x + 60, d.y + 340, d.width - 120, 300}}) {
                    DrawRectangleLinesEx(p, 5, alpha(BLACK, 0.35f));
                    DrawRectangleLinesEx(shrink(p, 5), 2, alpha(WHITE, 0.06f));
                }
                Rectangle sign{d.x + d.width / 2 - 170, d.y + 120, 340, 110};
                DrawRectangleRec(sign, rgba(0xe8e0d0ff));
                DrawRectangleLinesEx(sign, 4, col::red);
                ui::text("KEEP", sign.x + sign.width / 2, sign.y + 36, 38, col::red, ui::Center, true);
                ui::text("OUT", sign.x + sign.width / 2, sign.y + 78, 30, col::outline, ui::Center, true);
                Vector2 knob{side == 0 ? d.x + d.width - 50 : d.x + 50, d.y + 420};
                float rattle = h.pos == 4 ? std::sin(t_ * 50) * 3 * doorKick_ : 0;
                DrawCircleV(knob + Vector2{rattle, 0}, 16, col::goldDark);
                DrawCircleV(knob + Vector2{rattle - 4, -4}, 6, col::gold);
            }
            if (h.pos == 4) E().shake(1.2f);
        }
        Color wood = rgba(0x4a2e1eff);
        DrawRectangleRec({kOpening.x - 30, kOpening.y - 26, 30, kOpening.height + 30}, wood);
        DrawRectangleRec({kOpening.x + kOpening.width, kOpening.y - 26, 30, kOpening.height + 30}, wood);
        DrawRectangleRec({kOpening.x - 30, kOpening.y - 30, kOpening.width + 60, 30}, wood);
        DrawRectangleLinesEx({kOpening.x - 30, kOpening.y - 30, kOpening.width + 60, kOpening.height + 60}, 3, col::outline);
        rlPopMatrix();

        lightPass(0.95f, Vector2{kVPx + pan.x, kVPy - 14});
        // fingers curled round the door frame, pale in the moonlight: someone is right beside the door
        if (h.pos == 4 && !h.feint && doorAnim_ < 0.5f) {
            float fx0 = (side == 0 ? kOpening.x + kOpening.width + 4 : kOpening.x - 4) + pan.x;
            Color c = side == 0 ? rgba(0x4a3a34ff) : rgba(0x5a5058ff);
            float dir = side == 0 ? -1.0f : 1.0f;
            for (int f = 0; f < 4; f++) {
                float len = f == 1 || f == 2 ? 1.0f : 0.8f;
                Vector2 p{fx0, 296.0f + f * 22 + std::sin(t_ * 1.5f + f) * 1.5f};
                Vector2 k1 = p + Vector2{dir * 24 * len, 2}, k2 = k1 + Vector2{dir * 14 * len, 9}, tip = k2 + Vector2{dir * 3, 9};
                gfx::limb(p, k1, 11, c);
                gfx::limb(k1, k2, 9, c);
                DrawCircleV(k1, 5.5f, darken(c, 0.25f));
                DrawLineEx(k2, tip, 4, rgba(0x120a08ff));  // long dirty nails
            }
        }
        // eyes in the dark
        if (headPos.x > 0 && doorAnim_ < 0.5f && h.fleeAnim < 0) gfx::eyes(side, headPos, 72 * scale, beamPower() > 0 ? 0.3f : 0.6f, t_);
    }

    // ============================================================ draw: closet
    void drawCloset() {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        int st = b_.horse;
        rlPushMatrix();
        rlTranslatef(pan.x, 0, 0);
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x2a1a50ff));
        Rectangle c{250, 20, 780, 760};
        DrawRectangleRec({c.x - 20, c.y - 20, c.width + 40, 40}, rgba(0x4a2e1eff));
        DrawRectangleRec(c, rgba(0x05030aff));
        float gapTarget = st >= 4 ? 620.0f : st == 3 ? 150 : st == 2 ? 70 : st == 1 ? 26 : 4;
        float gap = lerpf(gapTarget, 0, doorAnim_);
        bool strain = holding() && st >= 2;
        float shake = strain ? std::sin(t_ * 60) * 4 : st == 3 ? std::sin(t_ * 23) * 2 : 0;
        Rectangle inside{c.x + c.width / 2 - gap / 2, c.y, gap, c.height};
        E().beginClip({inside.x + pan.x, inside.y, inside.width, inside.height});
        DrawRectangleGradientV((int)c.x, (int)c.y, (int)c.width, (int)c.height, rgba(0x120a18ff), rgba(0x05030aff));
        DrawLineEx({c.x, c.y + 70}, {c.x + c.width, c.y + 70}, 6, rgba(0x8a8498ff));
        Color hoodies[] = {rgba(0x6a5a48ff), rgba(0x7a3a5aff), rgba(0x1f1c26ff), rgba(0x3a6a8aff), rgba(0x5b2fb0ff)};
        for (int i = 0; i < 5; i++) {
            float x = c.x + 120 + i * 130 + std::sin(t_ + i) * 3;
            DrawLineEx({x, c.y + 70}, {x, c.y + 90}, 3, rgba(0x8a8498ff));
            DrawRectangleRounded({x - 50, c.y + 90, 100, 240}, 0.3f, 8, darken(hoodies[i], 0.4f));
        }
        Vector2 hc{c.x + c.width / 2, c.y + 330};
        if (st == 3) {  // a muzzle pushing through, breath steaming
            gfx::nightmare(2, {hc.x + 10, hc.y + 40}, 190, -12 + std::sin(t_ * 2) * 3, 1, t_);
        }
        if (st >= 4) {
            gfx::nightmare(2, {hc.x, hc.y + 10}, 300, std::sin(t_ * 3) * 4, 1, t_, 0.4f + 0.3f * std::sin(t_ * 4));
            for (int i = 0; i < 9; i++) {  // mane spilling out
                float x = hc.x - 260 + i * 30;
                gfx::limb({x, c.y + 60}, {x + std::sin(t_ + i) * 20, c.y + 200 + 60 * gfx::hash(i * 3.0f)}, 18, rgba(0x120806ff));
            }
        }
        E().endClip();
        // the doors
        float dw = c.width / 2 - gap / 2;
        for (int k : {-1, 1}) {
            Rectangle d = k < 0 ? Rectangle{c.x + shake, c.y, dw, c.height} : Rectangle{c.x + c.width - dw - shake, c.y, dw, c.height};
            if (d.width < 2) continue;
            fx::tiled(E().tex("tex/wood"), d, 1.2f, {k * 40.0f, 0}, rgba(0x7a5a48ff));
            DrawRectangleGradientH((int)d.x, (int)d.y, (int)d.width, (int)d.height, alpha(BLACK, k < 0 ? 0.0f : 0.4f), alpha(BLACK, k < 0 ? 0.4f : 0.0f));
            DrawRectangleLinesEx(shrink({d.x, d.y + 30, d.width, 300}, 24), 5, alpha(BLACK, 0.35f));
            DrawRectangleLinesEx(shrink({d.x, d.y + 350, d.width, 340}, 24), 5, alpha(BLACK, 0.35f));
            DrawCircleV({k < 0 ? d.x + d.width - 30 : d.x + 30, d.y + 340}, 12, darken(col::goldDark, 0.45f));
        }
        if (st >= 2 && st < 4 && gap > 10) {  // a hoof curled round the edge of the door
            Vector2 hf{c.x + c.width / 2 - gap / 2 + shake + 4, c.y + 250};
            gfx::limb(hf + Vector2{30, -40}, hf, 34, rgba(0x241410ff));  // the leg, coming out of the dark
            DrawEllipse((int)(hf.x - 14), (int)(hf.y + 10), 30, 26, rgba(0x2a2420ff));
            DrawEllipse((int)(hf.x - 20), (int)(hf.y + 22), 26, 12, rgba(0x14100eff));
        }
        if (dw > 120) {
            Rectangle sticker{c.x + 60, c.y + 120, 150, 60};
            DrawRectangleRounded(sticker, 0.3f, 8, rgba(0xe8e0d0ff));
            ui::text("DO NOT OPEN", sticker.x + sticker.width / 2, sticker.y + 30, 20, col::red, ui::Center, true);
        }
        if (st == 3 && gap > 40) {  // breath steaming out of the gap
            fx::beginAdd();
            for (int i = 0; i < 6; i++) {
                float k = std::fmod(t_ * 0.6f + i / 6.0f, 1.0f);
                fx::glow({hc.x + 30 + k * 60, hc.y + 120 - k * 80}, 30 + k * 40, alpha(rgba(0x8088a0ff), 0.12f * (1 - k)));
            }
            fx::endAdd();
        }
        rlPopMatrix();
        lightPass(0.96f, {-1, -1});
        closeOther();
        if (doorAnim_ < 0.5f) {
            if (st == 1) gfx::eyes(2, {640 + pan.x - 10, 300}, 60, 0.5f, t_);
            if (st == 2) gfx::eyes(2, {640 + pan.x - 30, 300}, 120, 0.9f, t_);
            if (st == 3) gfx::eyes(2, {640 + pan.x + 10, 370}, 190, 0.5f, t_);
        }
    }

    // he came for you: in the closet and at the bed there's nowhere for him to stand but right in front of you
    void closeOther() {
        if (dr_.spot != 3 || dr_.at != view_) return;
        float k = clamp01(dr_.finalT / 2.2f);
        gfx::other({640, VH + 200 - k * 120}, 1.05f + k * 0.3f, t_, 0, 1);
    }

    // someone lying under the blanket, breathing
    void bedShape() {
        if (!dr_.bedShape) return;
        float br = std::sin(t_ * 1.9f) * 0.5f + 0.5f;
        Color b0 = rgba(0x2f68b8ff), b1 = rgba(0x23508cff);
        DrawEllipse(560, 286, 120, 70 + br * 6, alpha(BLACK, 0.25f));
        DrawEllipse(470, 252, 70, 52 + br * 3, b1);            // head and shoulders under the cover
        DrawEllipse(470, 246, 64, 46 + br * 3, b0);
        DrawEllipse(650, 280, 210, 60 + br * 8, b1);           // the body
        DrawEllipse(650, 272, 200, 52 + br * 8, b0);
        DrawEllipse(840, 300, 70, 30, b1);                     // feet
        for (int i = 0; i < 6; i++) DrawLineEx({420.0f + i * 70, 240.0f + (i % 2) * 20}, {470.0f + i * 70, 300}, 4, alpha(WHITE, 0.08f));
        DrawEllipse(450, 210, 90, 30, rgba(0xd8d0e8ff));      // the pillow, pressed down
        DrawEllipse(450, 212, 60, 18, rgba(0xb0a8c0ff));
    }

    // ============================================================ draw: bed
    void drawBed() {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        rlPushMatrix();
        rlTranslatef(pan.x, 0, 0);
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x1c1230ff));
        // a poster above the bed and the headboard
        Rectangle poster{560, -10, 160, 60};
        DrawRectangleRec(poster, rgba(0x2a1a3aff));
        DrawRectangleRounded({160, 40, 960, 170}, 0.15f, 10, rgba(0x5b2fb0ff));
        for (int i = 0; i < 9; i++) DrawCircleV({220 + i * 105.0f, 110}, 6, darken(rgba(0x5b2fb0ff), 0.4f));
        DrawRectangleRounded({300, 170, 300, 120}, 0.6f, 10, rgba(0xf4f0ffff));
        DrawRectangleRounded({660, 175, 300, 115}, 0.6f, 10, col::pink);
        gfx::quad({120, 260}, {1160, 260}, {1280, 760}, {0, 760}, rgba(0x3a7bd5ff), rgba(0x2a5aa5ff));
        for (int i = 0; i < 8; i++) {
            float u = (i + 0.5f) / 8;
            DrawLineEx({lerpf(120, 1160, u), 260}, {lerpf(0, 1280, u), 760}, 10, alpha(WHITE, 0.15f));
        }
        DrawRectangleRounded({200, 520, 880, 60}, 0.6f, 10, alpha(BLACK, 0.12f));
        bedShape();
        for (int i = 0; i < b_.plush; i++) {
            Vector2 p = bedSpot(i);
            float creep = clamp01(b_.plushAge[i] / 25);
            float scared = beamPower() > 0 && i == b_.plush - 1 ? clamp01(b_.plushLit * 3) : 0;
            fx::softShadow({p.x, p.y + 4}, 80, 14, 0.5f);
            gfx::plush(p, 1.25f, t_, i, creep, scared);
        }
        if (plushFall_ >= 0) {  // the one you lit up tumbles off the bed
            Vector2 p = bedSpot(plushFallIdx_) + Vector2{plushFall_ * 300, plushFall_ * plushFall_ * 900};
            rlPushMatrix();
            rlTranslatef(p.x, p.y, 0);
            rlRotatef(plushFall_ * 400, 0, 0, 1);
            gfx::plush({0, 0}, 1.25f, t_, plushFallIdx_, 0, 1);
            rlPopMatrix();
        }
        rlPopMatrix();
        lightPass(0.96f, {-1, -1});
        closeOther();
        for (int i = 0; i < b_.plush; i++) {
            Vector2 e = gfx::plushEye(bedSpot(i), 1.25f, i, clamp01(b_.plushAge[i] / 25)) + Vector2{pan.x, 0};
            fx::beginAdd();
            fx::glow(e, 18, alpha(rgba(0xff9020ff), 0.8f));
            fx::glow(e, 5, alpha(WHITE, 0.9f));
            fx::endAdd();
        }
    }

    // ============================================================ draw: phases
    // the whole view, drawn into viewRt_ during update() and graded in draw()
    void renderView() {
        if (!viewRt_.id) return;
        BeginTextureMode(viewRt_);
        ClearBackground(BLACK);
        rlPushMatrix();
        rlScalef(viewRt_.texture.width / (float)VW, viewRt_.texture.height / (float)VH, 1);
        drawPlayWorld();
        rlPopMatrix();
        EndTextureMode();
    }

    void drawGraded() {
        if (!grade_.id || !viewRt_.id) {
            drawPlayWorld();
            return;
        }
        float c = creep(), dg = b_.danger();
        bool near = dr_.spot >= 0 && dr_.at == view_;
        float behind = dr_.behindT >= 0 ? clamp01(dr_.behindT / 6) : 0;
        float time = dr_.freezeT >= 0 ? dr_.frozenT : t_;
        float desat = 0.42f + 0.38f * c + (near ? 0.15f : 0), crush = 1.12f + 0.22f * c;
        float vig = 0.15f + 0.35f * dg + 0.55f * behind + (near ? 0.3f : 0) + (dr_.finalT >= 0 ? 0.4f : 0);
        float warp = 0.25f + 0.6f * c + 1.2f * behind + (dr_.finalT >= 0 ? 2.5f : 0);
        float split = 0.0012f + 0.004f * dg + (near ? 0.004f : 0) + (dr_.finalT >= 0 ? 0.01f : 0);
        float grain = 0.045f + 0.03f * c;
        float tint[3] = {0.84f, 0.97f, 1.06f};
        Vector2 res = E().canvasSize();
        float vals[7] = {time, desat, crush, vig, warp, split, grain};
        for (int i = 0; i < 7; i++) SetShaderValue(grade_, gradeLoc_[i], &vals[i], SHADER_UNIFORM_FLOAT);
        SetShaderValue(grade_, gradeLoc_[7], tint, SHADER_UNIFORM_VEC3);
        SetShaderValue(grade_, gradeLoc_[8], &res, SHADER_UNIFORM_VEC2);
        Texture2D& tex = viewRt_.texture;
        BeginShaderMode(grade_);
        DrawTexturePro(tex, {0, 0, (float)tex.width, -(float)tex.height}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, WHITE);
        EndShaderMode();
    }

    void drawPlayView() {
        drawGraded();
        // a hand on your shoulder, from behind. Drawn over everything: the dark doesn't hide it.
        if (dr_.behindT > 5) {
            float k = dr_.behindT < 9 ? clamp01((dr_.behindT - 5) / 2.5f) : 1 - clamp01((dr_.behindT - 9) / 1.5f);
            gfx::shoulderHand(k, t_);
            DrawRectangleGradientV(0, VH - 260, VW, 260, alpha(BLACK, 0), alpha(BLACK, 0.35f));
        }
        if (dr_.subT > 0) {  // two frames of white letters on black
            DrawRectangle(0, 0, VW, VH, BLACK);
            ui::text(dr_.subText, VW / 2.0f + rnd(-6, 6), VH / 2.0f, 64, WHITE, ui::Center, true);
        }
        if (dr_.freezeT < 0) drawHud();
    }

    void drawPlayWorld() {
        switch (view_) {
            case View::Center: drawCenter(); break;
            case View::Left: drawHall(0); break;
            case View::Right: drawHall(1); break;
            case View::Closet: drawCloset(); break;
            case View::Bed: drawBed(); break;
        }
        if (flashScare_ > 0) DrawRectangle(0, 0, VW, VH, alpha(WHITE, 0.12f * flashScare_));
        if (subT_ > 0) {
            DrawRectangle(0, 0, VW, VH, alpha(BLACK, subFace_ == 3 ? 0.9f : 0.6f));
            if (subFace_ == 3) gfx::otherFace({VW / 2.0f + rnd(-20, 20), VH / 2.0f - 20}, 250, t_, 0.3f, rnd(-6, 6));
            else gfx::nightmare(subFace_, {VW / 2.0f + rnd(-40, 40), VH / 2.0f + 20}, 330, rnd(-10, 10), 1, t_);
            staticScreen(0.3f);
        }
        fx_.draw();
        if (moveT_ > 0) DrawRectangle(0, 0, VW, VH, alpha(BLACK, 1 - std::abs(moveT_ - 0.15f) / 0.15f));
        grain(0.035f);
        if (staticT_ > 0) staticScreen(staticT_ * 2);
    }

    void drawStreet(float zoom, float dawn, bool figures) {
        gfx::Street s;
        s.t = t_;
        s.zoom = zoom;
        s.focus = center(gfx::kPieterWindow);
        s.lightning = lightning();
        s.dawn = dawn;
        s.rain = 1 - dawn;
        s.night = night_;
        s.figures = figures;
        gfx::street(s);
    }

    void drawTitle() {
        float drift = std::sin(t_ * 0.05f) * 0.02f;
        drawStreet(1.08f + drift, 0, true);
        DrawRectangleGradientH(0, 0, 720, VH, alpha(BLACK, 0.92f), alpha(BLACK, 0));
        float flick = std::fmod(t_, 4.3f) < 0.1f ? 0.4f : 1.0f;
        int beaten = 0;
        for (int i = 0; i < 6; i++) beaten += (P().fnapBeaten >> i) & 1;
        bool wrong = (P().fnapDeaths >= 3 || beaten >= 2) && std::fmod(t_, 11.0f) < 0.09f;
        if (wrong) {
            fx::gradientText("HE IS STILL", 330, 110, 74, rgba(0xe8e8e8ff), rgba(0x8a8a8aff), 5);
            fx::gradientText("IN THE HOUSE", 330, 184, 74, rgba(0xe8e8e8ff), rgba(0x6a0a0aff), 5);
        } else {
            fx::gradientText("FIVE NIGHTS", 330, 110, 74, alpha(rgba(0xffd08aff), flick), alpha(rgba(0xff5a1aff), flick), 5);
            fx::gradientText("AT PIETER'S", 330, 184, 74, alpha(rgba(0xffd08aff), flick), alpha(rgba(0xc81a1aff), flick), 5);
        }
        ui::text("HALLOWEEN SPECIAL", 330, 238, 20, col::orange, ui::Center, true);
        for (int i = 0; i < 5; i++) {  // a pumpkin per night survived
            bool beat = P().fnapBeaten & (1 << i);
            Vector2 p{230.0f + i * 50, 276};
            if (beat) gfx::jackOLantern(p, 15, t_ + i, 1);
            else DrawCircleV(p, 14, alpha(rgba(0x3a2a20ff), 0.8f));
        }
        if (howTo_) { drawHowTo(); return; }
        float y = 330;
        if (ui::button({180, y, 300, 58}, "NEW GAME", rgba(0x8a2a10ff), 26)) startNight(0);
        y += 70;
        int cont = std::min(P().fnapNight, 5);
        if (cont > 1 && ui::button({180, y, 300, 58}, "CONTINUE  - NIGHT " + std::to_string(cont), rgba(0x6a2a40ff), 22)) startNight(cont - 1);
        if (cont > 1) y += 70;
        if (P().fnapNight >= 6 && ui::button({180, y, 300, 58}, "NIGHT 6: JUHUH", col::red, 22)) startNight(5);
        if (P().fnapNight >= 6) y += 70;
        if (ui::button({180, y, 300, 50}, "HOW TO SURVIVE", col::panelHi, 20)) howTo_ = true;
        y += 62;
        if (ui::button({180, y, 300, 50}, "BACK", rgba(0x2a2238ff), 20)) E().go(makeMenu);
        grain(0.04f);
        art::vignette(0.6f);
    }

    void drawHowTo() {
        Rectangle r{60, 290, 760, 420};
        ui::panel(r, alpha(rgba(0x0c0814ff), 0.95f), 0.06f, col::orange);
        ui::text("HOW TO SURVIVE UNTIL 6 AM", r.x + 24, r.y + 30, 22, col::orange, ui::Left, true);
        struct Line { int face; const char* who; const char* how; } lines[] = {
            {0, "DAVI  (left door)", "Light the hall to chase him off. BREATHING at the door? Hold SPACE until he walks away. Never shine the light. Gets angrier every time."},
            {1, "KIRA  (right door)", "Same, but she GIGGLES, hides in the bathroom if you shine too much, and hacks the PC (go to the room, press E)."},
            {2, "THE NIGHT HORSE  (closet)", "Grows while you're away. Hold the closet shut to push him back. Doors swing open? Get there and hold them NOW."},
            {-1, "PLUSH HORSES  (bed)", "They gather on the bed. Shine on them. Three at once and they call the horse."},
            {-2, "THE MONKEY MAN  (window)", "Tik tik on the glass? Don't stare at him, or he wakes the whole house."},
        };
        for (int i = 0; i < 5; i++) {
            float y = r.y + 74 + i * 58;
            if (lines[i].face >= 0) gfx::nightmare(lines[i].face, {r.x + 50, y + 8}, 26, 0, 0.8f, t_);
            else if (lines[i].face == -2) gfx::monkeyMan({r.x + 50, y + 10}, 22, t_, 0);
            else gfx::plush({r.x + 46, y + 34}, 0.32f, t_, 0, 0, 0);
            ui::text(lines[i].who, r.x + 92, y - 8, 17, col::text, ui::Left, true);
            ui::paragraph(lines[i].how, {r.x + 92, y + 6, r.width - 120, 0}, 14, col::muted);
        }
        ui::paragraph("The torch drains its battery and charges on the desk. Running and slamming doors make NOISE, and they come for noise. "
                      "They don't always leave when they walk away: wait a moment before you look. Q at a door throws a lure.",
                      {r.x + 24, r.y + r.height - 58, r.width - 48, 0}, 13, col::gold);
        if (ui::button({r.x + r.width - 120, r.y + 12, 100, 36}, "OK", col::panelHi, 18)) howTo_ = false;
        grain(0.04f);
    }

    // the night opens on the house: a push in towards Pieter's window
    void drawCinematic() {
        float k = phaseT_;
        float push = easeInOutSine(clamp01(k / 5.4f));
        drawStreet(lerpf(1.0f, 3.4f, push * push), 0, true);
        // title cards
        float a = clamp01((k - 1.4f) / 0.6f) * (1 - clamp01((k - 4.4f) / 0.5f));
        if (a > 0) {
            DrawRectangleGradientV(0, 230, VW, 260, alpha(BLACK, 0), alpha(BLACK, 0.6f * a));
            fx::gradientText("NIGHT " + std::to_string(night_ + 1), VW / 2.0f, 330, 84, alpha(rgba(0xffd08aff), a), alpha(rgba(0xc81a1aff), a), 6);
            ui::text(fnap::kNight[night_].tag, VW / 2.0f, 396, 24, alpha(col::orange, a), ui::Center, true);
        }
        float black = clamp01((k - 5.0f) / 0.5f);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, black));
        if (k > 5.5f) {
            float c = clamp01((k - 5.5f) / 0.4f) * (1 - clamp01((k - 6.6f) / 0.4f));
            ui::textShadow("12:00 AM", VW / 2.0f, 330, 64, alpha(WHITE, c), ui::Center, true, 3);
        }
        if (k > 0.8f && k < 5) ui::text("click to skip", VW - 24, VH - 20, 14, alpha(col::muted, 0.5f), ui::Right, true);
        grain(0.05f);
        art::vignette(0.55f);
    }

    void drawNote() {
        DrawRectangle(0, 0, VW, VH, BLACK);
        float a = clamp01(phaseT_ / 0.8f);
        Rectangle n{VW / 2.0f - 330, 70, 660, 560};
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, 350, 0);
        rlRotatef(-1.5f, 0, 0, 1);
        rlTranslatef(-VW / 2.0f, -350, 0);
        DrawRectangleRec({n.x + 8, n.y + 10, n.width, n.height}, alpha(BLACK, 0.5f * a));
        DrawRectangleRec(n, alpha(rgba(0xfff3a0ff), a));
        DrawRectangleRec({n.x + n.width / 2 - 60, n.y - 14, 120, 28}, alpha(rgba(0xd8d0c0ff), 0.8f * a));
        Color ink = alpha(rgba(0x2a2040ff), a);
        ui::text("Note to self (Pieter)", n.x + 30, n.y + 40, 24, ink, ui::Left, true);
        const char* rules[] = {"Davi + Kira come down the halls at night. A/D = run to a door.",
                               "F / left mouse = torch. Light them in the hall = they run.",
                               "BREATHING or GIGGLING at the door? HOLD SPACE. NO LIGHT!!",
                               "They walk away... wait a sec. Sometimes they're still there.",
                               "W = closet. If the doors bulge, hold them shut.",
                               "S = bed. Light the plush horses away before there are 3.",
                               "Torch battery runs out! It charges on my desk.",
                               "PC playing music? Go to the room, press E.",
                               "Monkey Man at the window?? DON'T LOOK AT HIM.",
                               "Don't run around like crazy. They can HEAR me.",
                               "Survive till 6 AM. I'm not scared. (I'm scared)"};
        for (int i = 0; i < 11; i++) ui::paragraph(rules[i], {n.x + 30, n.y + 78 + i * 41.0f, n.width - 60, 0}, 18, ink);
        rlPopMatrix();
        if (phaseT_ > 1.2f) ui::text("click or SPACE to begin", VW / 2.0f, 680, 18, alpha(col::text, 0.5f + 0.5f * std::sin(t_ * 4)), ui::Center, true);
        grain(0.05f);
    }

    // The jumpscare: a cut to them lunging out of the dark, torn up by glitches.
    void drawScareFrame(float k) {
        DrawRectangle(0, 0, VW, VH, std::fmod(k, 0.12f) < 0.06f ? rgba(0x200004ff) : BLACK);
        Vector2 off{rnd(-30, 30), rnd(-30, 30)};
        float grow = easeOutCubic(clamp01(k / 0.22f));
        float lunge = easeOutCubic(clamp01((k - 0.75f) / 0.15f));  // and a second lunge, right into your face
        fx::beginAdd();
        fx::glow({VW / 2.0f, VH / 2.0f + 80}, 760, alpha(col::red, 0.45f));
        fx::endAdd();
        float jaw = 0.5f + 0.5f * std::sin(k * 38);
        if (scareWho_ == Who::Davi || scareWho_ == Who::Kira) {
            int who = (int)scareWho_;
            // motion trail
            for (int g = 2; g >= 0; g--) {
                float gk = std::max(0.0f, grow - g * 0.12f);
                float s = lerpf(0.5f, 1.6f, gk) + lunge * 0.9f;
                Vector2 feet{VW / 2.0f + off.x + (who == 0 ? -1 : 1) * (1 - gk) * 400, VH / 2.0f + 60 + 548 * s + off.y};
                if (g == 0) gfx::stalker(who, feet, s, t_, 0, 1);
                else DrawCircleV(gfx::stalkerHead(feet, s), 80 * s, alpha(BLACK, 0.3f));
            }
            float s = lerpf(0.5f, 1.6f, grow) + lunge * 0.9f;
            Vector2 head = gfx::stalkerHead({VW / 2.0f + off.x, VH / 2.0f + 60 + 548 * s + off.y}, s);
            gfx::nightmare(who, head, 72 * s, std::sin(k * 40) * 10, 1, t_, jaw);
        } else if (scareWho_ == Who::Other) {
            // no lunge. He's just suddenly there, filling everything, and the smile opens.
            float r = lerpf(260, 470, easeOutCubic(clamp01(k / 0.1f))) + lunge * 200 + std::sin(k * 70) * 6;
            DrawRectangle(0, 0, VW, VH, BLACK);
            gfx::otherFace(Vector2{VW / 2.0f, VH / 2.0f - 30} + off * 0.4f, r, t_, clamp01(k * 2.5f), std::sin(k * 30) * 4);
        } else if (scareWho_ == Who::Paardjes) {
            for (int i = 0; i < 3; i++)
                gfx::plush({VW / 2.0f + (i - 1) * 420 * grow + off.x, VH + 60 + off.y}, 1.6f + grow * 1.4f, t_, i, 1, 1);
            gfx::nightmare(2, Vector2{VW / 2.0f, VH / 2.0f - 40} + off, lerpf(120, 420, grow) + lunge * 200, std::sin(k * 40) * 8, 1, t_, jaw);
        } else {
            float r = lerpf(140, 560, grow) + lunge * 260 + std::sin(k * 50) * 10;
            for (int i = 0; i < 12; i++) {
                float x = VW / 2.0f - r + i * r / 6;
                gfx::limb({x, 0}, {x + std::sin(t_ * 3 + i) * 30, VH * (0.5f + 0.5f * gfx::hash(i * 3.0f))}, 26, rgba(0x0a0404ff));
            }
            gfx::nightmare(2, Vector2{VW / 2.0f, VH / 2.0f + 40} + off, r, std::sin(k * 40) * 10, 1, t_, jaw);
        }
        fx::beginAdd();
        fx::glow({VW / 2.0f, VH + 100.0f}, 700, alpha(rgba(0xff2020ff), 0.25f));
        fx::endAdd();
        if (k < 0.05f || (k > 0.75f && k < 0.79f)) DrawRectangle(0, 0, VW, VH, alpha(WHITE, 0.7f));
    }

    void renderScare() {
        if (!rt_.id) return;
        BeginTextureMode(rt_);
        ClearBackground(BLACK);
        rlPushMatrix();
        rlScalef(rt_.texture.width / (float)VW, rt_.texture.height / (float)VH, 1);
        drawScareFrame(phaseT_);
        rlPopMatrix();
        EndTextureMode();
    }

    // the frame from renderScare(), torn into glitching strips
    void drawScare() {
        float k = phaseT_;
        Texture2D& tex = rt_.texture;
        float tw = (float)tex.width, th = (float)tex.height;
        int strips = 24;
        for (int i = 0; i < strips; i++) {
            float y0 = (float)i / strips, y1 = (float)(i + 1) / strips;
            float shift = gfx::hash(std::floor(k * 24) + i * 1.7f) > 0.8f ? (gfx::hash(i + std::floor(k * 24)) - 0.5f) * 120 : 0;
            Color tint = shift != 0 && i % 3 == 0 ? rgba(0xff8080ff) : WHITE;
            DrawTexturePro(tex, {0, th - y1 * th, tw, -(y1 - y0) * th}, {shift, y0 * VH, (float)VW, (y1 - y0) * VH + 1}, {0, 0}, 0, tint);
        }
        staticScreen(0.05f + (k > 1.3f ? (k - 1.3f) * 2 : 0));
        grain(0.05f);
    }

    void drawGameOver() {
        DrawRectangle(0, 0, VW, VH, BLACK);
        int f = scareWho_ == Who::Davi ? 0 : scareWho_ == Who::Kira ? 1 : 2;
        if (scareWho_ == Who::Other) gfx::otherFace({VW / 2.0f, 230}, 170, t_, 0.2f, 0);
        else gfx::nightmare(f, {VW / 2.0f, 250}, 190, 0, 0.6f, t_);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f));
        DrawRectangleGradientV(0, 400, VW, 100, alpha(BLACK, 0), alpha(BLACK, 0.85f));
        DrawRectangle(0, 500, VW, VH - 500, alpha(BLACK, 0.85f));
        staticScreen(std::max(0.12f, 1 - phaseT_ * 1.5f));
        float a = clamp01((phaseT_ - 0.5f) / 0.5f);
        float jitter = std::fmod(t_, 2.1f) < 0.06f ? 8.0f : 0.0f;
        fx::gradientText("GAME OVER", VW / 2.0f + jitter, 110, 90, alpha(rgba(0xff6a6aff), a), alpha(rgba(0x8a0a1aff), a), 6);
        std::string cause = b_.cause;
        const char* who = scareWho_ == Who::Other   ? "You kept looking at him."
                          : scareWho_ == Who::Davi  ? "Davi got you."
                          : scareWho_ == Who::Kira  ? "Kira got you. HIHIHI."
                          : scareWho_ == Who::Horse ? "The Night Horse came out of the closet."
                                                    : "The plush horses called the big horse.";
        const char* tip = scareWho_ == Who::Other      ? "Tip: the tall one only wants one thing. Don't put the torch on him. He told you. Four times."
                          : cause == "lit at door"     ? "Tip: you shone the torch at the door while someone was right behind it. Hear breathing or giggling? HOLD, never shine."
                          : cause == "door not held"   ? "Tip: you were at the door, but didn't hold it shut. SPACE / right mouse."
                          : cause == "door unattended" ? "Tip: someone waited at a door and nobody held it. Listen from the room: breathing = run there."
                          : cause == "stared"          ? "Tip: when the closet swings open, hold it shut straight away. Don't just look."
                          : cause == "too late"        ? "Tip: the closet doors creaked open and you didn't get there in time. That creak means RUN."
                          : scareWho_ == Who::Paardjes ? "Tip: check the bed and light up every plush horse before there are three."
                                                       : "Tip: listen. Everything in the house makes a sound before it comes.";
        ui::text(who, VW / 2.0f, 466, 26, alpha(col::text, a), ui::Center, true);
        if (P().fnapDeaths >= 3 && P().fnapDeaths % 3 == 0)  // every third time, it says something to you
            ui::text(fill("That's " + std::to_string(P().fnapDeaths) + " times now, {USER}. You keep letting him die."), VW / 2.0f, 34, 16,
                     alpha(rgba(0xc04040ff), a * 0.8f), ui::Center, true);
        ui::paragraph(tip, {VW / 2.0f - 380, 490, 760, 0}, 16, alpha(col::muted, a));
        ui::text(std::string("Night ") + std::to_string(night_ + 1) + "  -  " + clockText(), VW / 2.0f, 548, 16, alpha(col::orange, a), ui::Center, true);
        if (phaseT_ > 1.0f) {
            if (ui::button({VW / 2.0f - 260, 590, 250, 60}, "TRY AGAIN", rgba(0x8a2a10ff), 24)) startNight(night_);
            if (ui::button({VW / 2.0f + 10, 590, 250, 60}, "GIVE UP", rgba(0x2a2238ff), 24)) phase_ = Phase::Title;
        }
        grain(0.05f);
    }

    // the fake one: the same morning, cut off in the middle
    void drawFakeDawn() {
        float k = phaseT_;
        if (k < 4.6f) {
            drawDawn(true);
            return;
        }
        DrawRectangle(0, 0, VW, VH, BLACK);  // silence. Nothing. For far too long.
        if (k > 6.4f) {
            gfx::otherFace({VW / 2.0f + rnd(-30, 30), VH / 2.0f - 10}, 300, t_, 0.6f + 0.4f * std::sin(k * 40), rnd(-8, 8));
            staticScreen(0.4f);
        }
        grain(0.06f);
    }

    // 6 AM: the clock rolls over, the alarm goes, and the street is pink with sunrise
    void drawDawn(bool fake = false) {
        float k = phaseT_;
        if (k < 1.6f) {
            DrawRectangle(0, 0, VW, VH, BLACK);
            float roll = easeInOutSine(clamp01((k - 0.2f) / 1.0f));
            E().beginClip({VW / 2.0f - 200, 200, 400, 140});
            ui::textShadow("5", VW / 2.0f - 60, 270 - roll * 140, 120, WHITE, ui::Center, true, 4);
            ui::textShadow("6", VW / 2.0f - 60, 410 - roll * 140, 120, WHITE, ui::Center, true, 4);
            E().endClip();
            ui::textShadow("AM", VW / 2.0f + 70, 270, 90, WHITE, ui::Center, true, 4);
            grain(0.06f);
            return;
        }
        float in = clamp01((k - 1.6f) / 1.2f);
        drawStreet(lerpf(2.6f, 1.05f, easeOutCubic(clamp01((k - 1.6f) / 3.5f))), clamp01((k - 1.4f) / 2.5f), false);
        for (int i = 0; i < 5; i++) {  // birds crossing the sunrise
            float x = std::fmod(k * 70 + i * 140, (float)VW + 200) - 100, y = 160 + i * 18 + std::sin(k * 2 + i) * 10;
            float w = std::sin(k * 9 + i) * 6;
            DrawLineEx({x - 9, y - w}, {x, y}, 2, rgba(0x2a1a2aff));
            DrawLineEx({x, y}, {x + 9, y - w}, 2, rgba(0x2a1a2aff));
        }
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 1 - in));
        float a = clamp01((k - 3.2f) / 0.6f);
        if (a > 0) {
            DrawRectangleGradientV(0, 250, VW, 330, alpha(BLACK, 0), alpha(BLACK, 0.55f * a));
            fx::gradientText("6:00 AM", VW / 2.0f, 300, 54, alpha(WHITE, a), alpha(rgba(0xffd8a0ff), a), 4);
            fx::gradientText("GOOD MORNING!", VW / 2.0f, 370, 64, alpha(rgba(0xfff3a0ff), a), alpha(col::orange, a), 5);
            ui::text("Night " + std::to_string(night_ + 1) + " survived", VW / 2.0f, 422, 22, alpha(col::text, a), ui::Center, true);
            if (!fake) {
                ui::coin({VW / 2.0f - 90, 460}, 14);
                ui::text("+" + fmtNum(reward_) + " Pietercoins", VW / 2.0f - 68, 460, 22, alpha(col::gold, a), ui::Left, true);
            }
        }
        if (fake) {
            grain(0.04f);
            art::vignette(0.4f);
            return;
        }
        float endA = clamp01((k - 4.4f) / 0.8f);
        if (night_ == 4 && endA > 0) {
            const char* lines[] = {"Mama and Papa came home on the 6 AM train, with fresh stroopwafels.", "Pieter told them he slept great.",
                                   "The closet door was open. On the floor inside lay one plush horse.", "It was still warm."};
            for (int i = 0; i < 4; i++) {
                float la = clamp01((k - 4.4f - i * 0.9f) / 0.6f);
                ui::text(lines[i], VW / 2.0f, 500 + i * 24.0f, 17, alpha(i == 3 ? col::red : col::text, la), ui::Center, true);
            }
        }
        if (night_ == 5 && endA > 0) ui::text("You beat JUHUH NIGHT. Pieter is officially not scared. (He is.)", VW / 2.0f, 506, 18, alpha(col::gold, endA), ui::Center, true);
        float btnAt = night_ == 4 ? 8.4f : 4.6f;
        if (k > btnAt) {
            if (night_ == 4) ui::text("NIGHT 6 UNLOCKED: JUHUH NIGHT", VW / 2.0f, 610, 18, col::red, ui::Center, true);
            bool more = night_ < 4 || (night_ == 4 && P().fnapNight >= 6);
            float y = night_ == 4 ? 634 : 600;
            if (more && ui::button({VW / 2.0f - 260, y, 250, 60}, "NEXT NIGHT", rgba(0x8a2a10ff), 24)) startNight(std::min(5, night_ + 1));
            if (ui::button({more ? VW / 2.0f + 10 : VW / 2.0f - 125, y, 250, 60}, "TITLE", rgba(0x2a2238ff), 24)) phase_ = Phase::Title;
        }
        fx_.draw();
        grain(0.04f);
        art::vignette(0.4f);
    }

    void draw() override {
        switch (phase_) {
            case Phase::Title: drawTitle(); break;
            case Phase::Cinematic: drawCinematic(); break;
            case Phase::Note: drawNote(); break;
            case Phase::Play: drawPlayView(); break;
            case Phase::Scare: drawScare(); break;
            case Phase::GameOver: drawGameOver(); break;
            case Phase::Dawn: drawDawn(); break;
            case Phase::FakeDawn: drawFakeDawn(); break;
        }
    }

private:
    Brain b_;
    std::vector<fnap::Event> ev_;
    Phase phase_ = Phase::Title;
    View view_ = View::Center, pendingView_ = View::Center;
    int night_ = 0, prevPos_[2]{}, bells_ = 0, subFace_ = 0, textIdx_ = 0, plushFallIdx_ = 0;
    float t_ = 0, phaseT_ = 0, moveT_ = 0, staticT_ = 0, musicT_ = 0;
    bool introNote_ = false, howTo_ = false, wasLight_ = false, wasHold_ = false, pcClick_ = false, tock_ = false;
    float doorAnim_ = 0, doorKick_ = 0, flicker_ = 0, heartT_ = 0, heartPulse_ = 0, flashScare_ = 0, subT_ = 0, hourPulse_ = 0, klompEcho_ = 0;
    float rainT_ = 0, windT_ = 4, thunderT_ = 6, boltT_ = -1, thunderDelay_ = -1, clockT_ = 0, hushT_ = -1, plushFall_ = -1;
    float monkeyRise_ = 0, monkeyBang_ = 0, clashAnim_ = 0, textT_ = -1;
    bool devLight_ = false, devHold_ = false, devBolt_ = false;
    struct Shown { std::string from, msg; bool other = false; } shown_;
    struct Late { float at; const char* from; const char* msg; bool other; };
    std::vector<Late> late_;
    int lateIdx_ = 0;
    // everything that makes the house wrong
    struct Dread {
        // the other one
        int stage = 0;           // times you've put the torch on him tonight
        int spot = -1;           // where he stands in the view you're in (-1: not here)
        View at = View::Center;
        bool hunting = false;    // stage 4: he comes to you
        float lit = 0, hover = 0, look = 0, nextOther = 60, finalT = -1, fade = 0;
        Rectangle rect{};        // where he was drawn this frame, on screen
        // silences that lead nowhere
        float silenceT = -1, silenceLen = 0, nextSilence = 60;
        // breathing right behind you when you stand still
        float stillT = 0, behindT = -1, breathT = 0;
        // a hand on the door that isn't yours
        float holdT = 0, handT = -1, huntT = 0;
        bool handDone = false;
        // two-frame flashes of text
        float subT = 0, nextSub = 80;
        std::string subText;
        // the monitor, the bed, the lullaby
        float reflectT = -1, nextLull = 1e9f, bedLit = 0;
        bool bedShape = false;
        // the fake morning, the freeze
        float fakeAt = -1, freezeAt = -1, freezeT = -1, frozenT = 0;
        float creakT = 9, dripT = 5;
        bool ceiling[2]{}, turned[2]{};
        View lastView = View::Center;
    } dr_;
    Who scareWho_ = Who::Davi;
    long long reward_ = 0;
    Texture2D noise_{};
    Vector2 doorEyePos_[2]{}, doorQuad_[2][4]{};
    RenderTexture2D lm_{}, rt_{}, viewRt_{};
    Shader grade_{};
    int gradeLoc_[9]{};
    float redPulse_ = 0;
    room::Env env_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeFnap() { return std::make_unique<FnapScene>(); }

}  // namespace pg
