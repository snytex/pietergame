// FIVE NIGHTS AT PIETER'S - Halloween special.
// You are Pieter, alone in the Bec IT Room from 12 AM to 6 AM. No cameras: run
// to the doors, the closet and the bed, listen, and use the flashlight.
//   Davi  - left hallway. Light scares him off while he's in the hall. At the
//           door you only HEAR him breathing: hold the door, never shine.
//   Kira  - right hallway, same rules, but she giggles instead of breathing,
//           sometimes hides from the light, and hacks the PC: a music box
//           starts playing and everyone moves twice as fast until you switch it off.
//   Het Nachtpaard - grows in the closet while you're not looking. Hold the
//           closet shut to push him back.
//   Paardjes - little plush horses that gather on the bed. Light chases them off;
//           three at once is game over.
//   Monkey Man - (night 2+) presses his face against the bedroom window. Don't
//           stare: look at him too long and he bangs on the glass, which wakes the
//           whole house and pulls Davi and Kira closer. The toy monkey on the desk
//           clashes its cymbals while he's out there.
//   Lures  - throw a stroopwafel (Davi) or a boba (Kira) down the hall to send
//           them back and keep them busy. Uses your inventory, once per door per night.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/RoomArt.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>

namespace pg {
namespace {

constexpr float kHourLen = 45;  // seconds per in-game hour (a night is 4.5 minutes)

enum class View { Center, Left, Right, Closet, Bed };
enum class Phase { Title, Intro, Play, Scare, GameOver, Win };
enum class Who { Davi, Kira, Horse, Paardjes };

struct NightAI { int davi, kira, horse, paardjes, pc; };
const NightAI kAI[6] = {{3, 2, 0, 2, 0}, {5, 5, 3, 4, 3}, {8, 7, 6, 6, 5}, {11, 11, 9, 8, 7}, {14, 14, 12, 10, 9}, {18, 18, 16, 14, 11}};
const char* kNightTag[6] = {"Welkom thuis", "Kira is awake", "Het paard ontwaakt", "Niemand slaapt", "De laatste nacht", "JUHUH NIGHT"};

// Nightmare faces: where the eyes and mouth are on each portrait (portrait radius = 1)
struct Face { const char* tex; Vector2 eye[2]; int eyes; Vector2 mouth; float mouthW; Color glow; };
const Face kFaces[3] = {
    {"davi", {{-0.42f, 0.125f}, {0.113f, 0.152f}}, 2, {-0.14f, 0.6f}, 0.55f, rgba(0xff3030ff)},
    {"kira", {{-0.63f, -0.08f}, {0.066f, -0.277f}}, 2, {-0.19f, 0.36f}, 0.5f, rgba(0xff40d0ff)},
    {"horse", {{0.33f, -0.2f}, {0, 0}}, 1, {-0.38f, 0.66f}, 0.5f, rgba(0xff9020ff)},
};

float hash(float n) { return std::fmod(std::abs(std::sin(n * 12.9898f) * 43758.5453f), 1.0f); }

void tri2(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    rlBegin(RL_TRIANGLES);
    for (int w = 0; w < 2; w++) {
        Vector2 p = w ? c : b, q = w ? b : c;
        Color cp = w ? cc : cb, cq = w ? cb : cc;
        rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex2f(a.x, a.y);
        rlColor4ub(cp.r, cp.g, cp.b, cp.a); rlVertex2f(p.x, p.y);
        rlColor4ub(cq.r, cq.g, cq.b, cq.a); rlVertex2f(q.x, q.y);
    }
    rlEnd();
}
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color top, Color bottom) {
    tri2(tl, tr, br, top, top, bottom);
    tri2(tl, br, bl, top, bottom, bottom);
}
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c) { quad(tl, tr, br, bl, c, c); }

// A portrait turned into a nightmare: darkened, hollow glowing eyes, a mouth full of teeth.
void nightmare(int f, Vector2 c, float r, float rot, float eyeGlow, float t) {
    const Face& fc = kFaces[f];
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(rot, 0, 0, 1);
    art::portrait(fc.tex, {0, 0}, r, BLANK, 0, false, rgba(0x8a6a78ff));
    DrawCircleGradient({0, 0}, r, alpha(BLACK, 0.05f), alpha(BLACK, 0.8f));
    // cracks
    for (int i = 0; i < 4; i++) {
        Vector2 p{(hash(i + f * 7.0f) - 0.5f) * r * 1.2f, (hash(i * 3.1f + f) - 0.5f) * r * 1.2f};
        for (int k = 0; k < 4; k++) {
            Vector2 q = p + Vector2{(hash(i * 9.0f + k) - 0.5f) * r * 0.3f, (hash(i * 5.0f + k + 1) - 0.2f) * r * 0.25f};
            DrawLineEx(p, q, std::max(1.5f, r * 0.012f), alpha(BLACK, 0.7f));
            p = q;
        }
    }
    // mouth: black hole with jagged teeth
    Vector2 m = fc.mouth * r;
    float mw = fc.mouthW * r, mh = r * 0.16f;
    DrawEllipse((int)m.x, (int)m.y, mw, mh, rgba(0x120406ff));
    DrawEllipse((int)m.x, (int)(m.y + mh * 0.3f), mw * 0.7f, mh * 0.5f, rgba(0x5a0a14ff));
    int teeth = 9;
    for (int i = 0; i < teeth; i++) {
        float u0 = (float)i / teeth, u1 = (float)(i + 1) / teeth;
        float x0 = m.x - mw * 0.9f + u0 * mw * 1.8f, x1 = m.x - mw * 0.9f + u1 * mw * 1.8f;
        float edge = std::sqrt(std::max(0.0f, 1 - std::pow(((x0 + x1) / 2 - m.x) / mw, 2.0f)));
        float ty = m.y - mh * edge, by = m.y + mh * edge;
        Color tooth = rgba(0xe8dcc0ff);
        tri2({x0, ty}, {x1, ty}, {(x0 + x1) / 2, ty + mh * 0.75f * edge}, tooth, tooth, tooth);
        tri2({x0, by}, {x1, by}, {(x0 + x1) / 2, by - mh * 0.6f * edge}, tooth, tooth, tooth);
    }
    // eyes: hollow sockets with burning pupils
    for (int i = 0; i < fc.eyes; i++) {
        Vector2 e = fc.eye[i] * r;
        DrawCircleV(e, r * 0.13f, rgba(0x060204ff));
        DrawCircleGradient(e, r * 0.2f, alpha(BLACK, 0.8f), alpha(BLACK, 0));
    }
    fx::beginAdd();
    for (int i = 0; i < fc.eyes; i++) {
        Vector2 e = fc.eye[i] * r + Vector2{std::sin(t * 23 + i) * r * 0.01f, 0};
        fx::glow(e, r * 0.35f, alpha(fc.glow, 0.7f * eyeGlow));
        fx::glow(e, r * 0.1f, alpha(WHITE, 0.9f * eyeGlow));
    }
    fx::endAdd();
    rlPopMatrix();
}

// just the eyes, for when everything else is dark
void eyesOnly(int f, Vector2 c, float r, float strength, float t) {
    const Face& fc = kFaces[f];
    fx::beginAdd();
    for (int i = 0; i < fc.eyes; i++) {
        Vector2 e = c + fc.eye[i] * r;
        float blink = std::fmod(t * 0.37f + f, 1.0f) < 0.03f ? 0.0f : 1.0f;
        fx::glow(e, r * 0.3f, alpha(fc.glow, 0.5f * strength * blink));
        fx::glow(e, r * 0.07f, alpha(WHITE, 0.8f * strength * blink));
    }
    fx::endAdd();
}

// Davi / Kira standing in the hallway: long arms, torn hoodie, nightmare head
void stalker(int f, Vector2 feet, float s, float t) {
    Color hood = f == 0 ? rgba(0x6a5a48ff) : rgba(0x7a3a5aff);
    Color dark = darken(hood, 0.5f);
    float sway = std::sin(t * 1.3f + f) * 4 * s;
    // legs
    for (int k : {-1, 1}) {
        DrawLineEx({feet.x + k * 22 * s, feet.y - 150 * s}, {feet.x + k * 30 * s, feet.y}, 30 * s, rgba(0x1a1620ff));
        DrawEllipse((int)(feet.x + k * 34 * s), (int)feet.y, 26 * s, 9 * s, rgba(0x0c0a10ff));
    }
    // torso with a ragged hem
    Rectangle torso{feet.x - 75 * s + sway, feet.y - 340 * s, 150 * s, 200 * s};
    DrawRectangleRounded(torso, 0.35f, 8, hood);
    for (int i = 0; i < 6; i++) {
        float x = torso.x + i * torso.width / 6;
        DrawTriangle({x, torso.y + torso.height - 4}, {x + torso.width / 12, torso.y + torso.height + 22 * s * (0.5f + hash(i + f))},
                     {x + torso.width / 6, torso.y + torso.height - 4}, hood);
    }
    DrawRectangleGradientH((int)torso.x, (int)torso.y, (int)(torso.width / 2), (int)torso.height, alpha(BLACK, 0.4f), alpha(BLACK, 0));
    // too-long arms with claws
    for (int k : {-1, 1}) {
        Vector2 sh{feet.x + k * 70 * s + sway, feet.y - 320 * s};
        Vector2 hand{feet.x + k * 105 * s + std::sin(t * 2 + k) * 6 * s, feet.y - 60 * s};
        DrawLineEx(sh, hand, 30 * s, dark);
        DrawCircleV(hand, 16 * s, rgba(0x8a6a60ff));
        for (int c = -1; c <= 1; c++) DrawLineEx(hand, hand + Vector2{c * 9 * s, 30 * s}, 4 * s, rgba(0xd8d0c0ff));
    }
    nightmare(f, {feet.x + sway, feet.y - 400 * s}, 78 * s, std::sin(t * 0.9f + f) * 8, 1, t);
}

void jackOLantern(Vector2 c, float r, float t, float flicker) {
    DrawEllipse((int)c.x, (int)(c.y + r * 0.85f), r * 0.9f, r * 0.18f, alpha(BLACK, 0.4f));
    Color o = rgba(0xff7a1aff);
    for (int i = -1; i <= 1; i++) DrawEllipse((int)(c.x + i * r * 0.42f), (int)c.y, r * 0.55f, r * 0.82f, darken(o, 0.15f + std::abs(i) * 0.15f));
    DrawEllipse((int)c.x, (int)c.y, r * 0.4f, r * 0.82f, o);
    DrawRectangleRec({c.x - r * 0.08f, c.y - r * 1.05f, r * 0.16f, r * 0.3f}, rgba(0x3a6a2aff));
    float g = 0.75f + 0.25f * std::sin(t * 13 + c.x) * flicker;
    Color fire = rgba(0xffd84aff);
    auto carveTri = [&](Vector2 a, Vector2 b, Vector2 cc) { tri2(a, b, cc, alpha(fire, g), alpha(fire, g), alpha(fire, g)); };
    carveTri({c.x - r * 0.45f, c.y - r * 0.1f}, {c.x - r * 0.15f, c.y - r * 0.1f}, {c.x - r * 0.3f, c.y - r * 0.4f});
    carveTri({c.x + r * 0.15f, c.y - r * 0.1f}, {c.x + r * 0.45f, c.y - r * 0.1f}, {c.x + r * 0.3f, c.y - r * 0.4f});
    for (int i = 0; i < 5; i++) {
        float x = c.x - r * 0.5f + i * r * 0.2f;
        carveTri({x, c.y + r * 0.2f}, {x + r * 0.2f, c.y + r * 0.2f}, {x + r * 0.1f, c.y + r * (i % 2 ? 0.48f : 0.32f)});
    }
    fx::beginAdd();
    fx::glow(c, r * 2.2f, alpha(rgba(0xff8a2aff), 0.3f * g));
    fx::endAdd();
}

// The Monkey Man, face pressed against the glass. angry: 0 staring .. 1 screaming
void monkeyMan(Vector2 c, float r, float t, float angry) {
    Color fur = rgba(0x24160eff), skin = rgba(0x6e5040ff), dark = rgba(0x0a0604ff);
    float jaw = angry * (0.6f + 0.4f * std::sin(t * 40));
    // tufts, ears, head
    for (int i = 0; i < 7; i++) {
        float a = -PI / 2 + (i - 3) * 0.32f;
        DrawTriangle(c + fromAngle(a - 0.2f, r * 0.85f), c + fromAngle(a, r * 1.25f), c + fromAngle(a + 0.2f, r * 0.85f), fur);
        DrawTriangle(c + fromAngle(a + 0.2f, r * 0.85f), c + fromAngle(a, r * 1.25f), c + fromAngle(a - 0.2f, r * 0.85f), fur);
    }
    for (int k : {-1, 1}) {
        DrawCircleV({c.x + k * r * 0.98f, c.y - r * 0.05f}, r * 0.33f, fur);
        DrawCircleV({c.x + k * r * 0.98f, c.y - r * 0.05f}, r * 0.2f, skin);
    }
    DrawCircleV(c, r, fur);
    // face mask: two eye lobes + muzzle
    for (int k : {-1, 1}) DrawCircleV({c.x + k * r * 0.3f, c.y - r * 0.12f}, r * 0.36f, skin);
    DrawEllipse((int)c.x, (int)(c.y + r * 0.34f + jaw * r * 0.08f), r * 0.52f, r * (0.36f + jaw * 0.1f), skin);
    DrawEllipse((int)c.x, (int)(c.y - r * 0.4f), r * 0.66f, r * 0.13f, fur);  // heavy brow
    // eyes: deep sockets
    for (int k : {-1, 1}) {
        DrawCircleV({c.x + k * r * 0.28f, c.y - r * 0.14f}, r * 0.13f, dark);
        DrawCircleV({c.x + k * r * 0.28f, c.y - r * 0.14f}, r * 0.05f, rgba(0xffe080ff));
    }
    for (int k : {-1, 1}) DrawEllipse((int)(c.x + k * r * 0.09f), (int)(c.y + r * 0.18f), r * 0.05f, r * 0.035f, dark);
    // mouth: a line, or a screaming hole with fangs
    Vector2 m{c.x, c.y + r * 0.46f};
    if (jaw < 0.05f) {
        DrawLineEx({m.x - r * 0.25f, m.y}, {m.x + r * 0.25f, m.y}, std::max(1.5f, r * 0.04f), dark);
    } else {
        DrawEllipse((int)m.x, (int)(m.y + r * 0.05f), r * 0.3f, r * (0.06f + 0.18f * jaw), dark);
        Color fang = rgba(0xe8dcc0ff);
        for (int k : {-1, 1}) {
            Vector2 a{m.x + k * r * 0.2f, m.y - r * 0.02f}, b{m.x + k * r * 0.12f, m.y - r * 0.02f}, tip{m.x + k * r * 0.16f, m.y + r * 0.14f * jaw};
            DrawTriangle(a, b, tip, fang);
            DrawTriangle(b, a, tip, fang);
        }
    }
}

// A windmill on the polder, black against the sky
void windmill(Vector2 base, float s, float t, Color c) {
    DrawTriangle({base.x - 13 * s, base.y}, {base.x + 13 * s, base.y}, {base.x + 7 * s, base.y - 62 * s}, c);
    DrawTriangle({base.x - 13 * s, base.y}, {base.x + 7 * s, base.y - 62 * s}, {base.x - 7 * s, base.y - 62 * s}, c);
    DrawCircleSector({base.x, base.y - 62 * s}, 9 * s, 180, 360, 10, c);
    Vector2 hub{base.x, base.y - 60 * s};
    for (int i = 0; i < 4; i++) {
        float a = t * 0.5f + i * PI / 2;
        Vector2 d = fromAngle(a, 1), n{-d.y, d.x};
        Vector2 p0 = hub + d * (6 * s), p1 = hub + d * (42 * s);
        DrawLineEx(hub, p1, 2.2f * s, c);
        // lattice sail on one side of the stock
        Vector2 q0 = p0 + n * (7 * s), q1 = p1 + n * (7 * s);
        DrawLineEx(q0, q1, 1.2f * s, c);
        for (int k = 0; k <= 5; k++) DrawLineEx(Vector2Lerp(p0, p1, k / 5.0f), Vector2Lerp(q0, q1, k / 5.0f), 1.0f * s, c);
    }
    DrawCircleV(hub, 2.5f * s, c);
}

// The wind-up toy monkey on the desk. clash: 0..1, cymbals closing
void toyMonkey(Vector2 feet, float s, float clash) {
    Color fur = rgba(0x6a4428ff), face = rgba(0xd8b088ff), gold = rgba(0xe0b040ff);
    DrawEllipse((int)feet.x, (int)(feet.y - 14 * s), 11 * s, 14 * s, fur);
    DrawRectangleRec({feet.x - 10 * s, feet.y - 20 * s, 20 * s, 9 * s}, rgba(0xb02030ff));  // little red vest
    DrawCircleV({feet.x, feet.y - 34 * s}, 10 * s, fur);
    for (int k : {-1, 1}) DrawCircleV({feet.x + k * 10 * s, feet.y - 36 * s}, 4 * s, face);
    DrawEllipse((int)feet.x, (int)(feet.y - 31 * s), 6 * s, 5 * s, face);
    DrawRectangleRec({feet.x - 5 * s, feet.y - 49 * s, 10 * s, 6 * s}, rgba(0xb02030ff));  // fez
    for (int k : {-1, 1}) {
        DrawCircleV({feet.x + k * 3 * s, feet.y - 36 * s}, 1.6f * s, BLACK);
        float x = feet.x + k * lerpf(20, 5, clash) * s;
        DrawLineEx({feet.x + k * 8 * s, feet.y - 20 * s}, {x, feet.y - 22 * s}, 3 * s, fur);
        DrawEllipse((int)x, (int)(feet.y - 22 * s), 2.5f * s, 9 * s, gold);
        DrawEllipse((int)x, (int)(feet.y - 22 * s), 1.2f * s, 4 * s, lighten(gold, 0.4f));
    }
}

// ====================================================================== scene

class FnapScene : public Scene {
public:
    ~FnapScene() override {
        if (mask_.id) UnloadTexture(mask_);
        if (noise_.id) UnloadTexture(noise_);
        if (lm_.id) UnloadRenderTexture(lm_);
    }

    MusicMode music() const override { return MusicMode::None; }

    Presence presence() const override {
        if (phase_ == Phase::Title) return {"Five Nights at Pieter's", "Too scared to start"};
        return {"Five Nights at Pieter's", "Night " + std::to_string(night_ + 1) + " - " + clockText()};
    }

    void enter() override {
        // flashlight mask: transparent centre, black edge
        const int n = 256;
        Image img = GenImageColor(n, n, BLANK);
        auto* px = (Color*)img.data;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
                float d = std::sqrt(dx * dx + dy * dy);
                float a = std::clamp((d - 0.2f) / 0.8f, 0.0f, 1.0f);
                a = a * a * (3 - 2 * a);
                px[y * n + x] = {0, 0, 0, (unsigned char)(a * 255)};
            }
        mask_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(mask_, TEXTURE_FILTER_BILINEAR);
        Image ni = GenImageWhiteNoise(256, 256, 0.5f);
        noise_ = LoadTextureFromImage(ni);
        UnloadImage(ni);
        SetTextureWrap(noise_, TEXTURE_WRAP_REPEAT);
        lm_ = LoadRenderTexture(VW / 2, VH / 2);
        SetTextureFilter(lm_.texture, TEXTURE_FILTER_BILINEAR);
        env_.horror = true;
        env_.lights = 0;
        env_.outside = [this] { outsideWindow(); };
        env_.hour = 1;

        // dev hooks for screenshots: PG_FNAP=<night 1-6> starts playing at once,
        // PG_FNAP_VIEW=center|left|right|closet|bed, PG_FNAP_STAGE=davi,kira,horse,bed,pc,
        // PG_FNAP_LIGHT=1 / PG_FNAP_HOLD=1, PG_FNAP_SCARE=0..3 shows a jumpscare
        if (const char* nStr = std::getenv("PG_FNAP")) {
            startNight(std::clamp(std::atoi(nStr), 1, 6) - 1);
            phase_ = Phase::Play;
            if (const char* v = std::getenv("PG_FNAP_VIEW")) {
                std::string s = v;
                view_ = s == "left" ? View::Left : s == "right" ? View::Right : s == "closet" ? View::Closet : s == "bed" ? View::Bed : View::Center;
            }
            if (const char* st = std::getenv("PG_FNAP_STAGE")) {
                int d = 0, k = 0, h = 0, b = 0, pc = 0;
                std::sscanf(st, "%d,%d,%d,%d,%d", &d, &k, &h, &b, &pc);
                hall_[0].stage = d;
                hall_[1].stage = k;
                horse_ = h;
                paardjes_ = b;
                pcOn_ = pc != 0;
                hall_[0].attackT = hall_[1].attackT = 999;
            }
            devLight_ = std::getenv("PG_FNAP_LIGHT") != nullptr;
            devHold_ = std::getenv("PG_FNAP_HOLD") != nullptr;
            devFreeze_ = std::getenv("PG_FNAP_STAGE") != nullptr;
            if (const char* tt = std::getenv("PG_FNAP_T")) nightT_ = (float)std::atof(tt);
            if (std::getenv("PG_FNAP_INTRO")) phase_ = Phase::Intro;
            if (const char* sc = std::getenv("PG_FNAP_SCARE")) jumpscare((Who)std::clamp(std::atoi(sc), 0, 3));
            if (std::getenv("PG_FNAP_MONKEY")) { monkeyOn_ = true; monkeyRise_ = 1; }
            if (std::getenv("PG_FNAP_BOLT")) { devBolt_ = true; thunderT_ = 999; }
        }
    }

    void exit() override {
        // the music box, drone and friends are long one-shots: cut them so they
        // don't play over the menu music
        for (Sfx x : {Sfx::MusicBox, Sfx::Drone, Sfx::Breath, Sfx::Giggle, Sfx::Heart, Sfx::Static, Sfx::Scream, Sfx::Creak, Sfx::Step,
                  Sfx::Rain, Sfx::Wind, Sfx::Thunder, Sfx::Carillon, Sfx::Whisper, Sfx::Riser, Sfx::Sting, Sfx::Tinnitus, Sfx::Monkey})
            E().sfx.stop(x);
        P().save();
    }

    // ============================================================ night setup
    struct Hall {
        int stage = 0;        // 0 away, 1 end of the hall, 2 halfway, 3 right at the door
        float moveT = 0, attackT = 0, heldT = 0, needHold = 0, flashT = 0, stunT = 0, cueT = 0;
        bool lureUsed = false, hidden = false, seen = false;  // seen: the stinger already played for this approach
        float lureAnim = -1;  // >= 0 while a lure is flying down the hall
    };

    void startNight(int n) {
        night_ = n;
        nightT_ = 0;
        view_ = View::Center;
        moveT_ = 0;
        for (auto& h : hall_) h = Hall{};
        hall_[0].moveT = 6;
        hall_[1].moveT = 8;
        horse_ = 0;
        horseT_ = 8;
        horseLinger_ = 0;
        horseHold_ = 0;
        paardjes_ = 0;
        paardT_ = 10;
        bedT_ = 0;
        bedFlash_ = 0;
        pcOn_ = false;
        pcT_ = 20;
        pcSongT_ = 0;
        captions_.clear();
        doorAnim_ = 0;
        fx_.clear();
        heartT_ = 0;
        droneT_ = 0;
        phase_ = Phase::Intro;
        introT_ = 0;
        introNote_ = n == 0;
        lastHour_ = 0;
        monkeyOn_ = false;
        monkeyRise_ = 0;
        monkeyNext_ = rnd(35, 60);
        whisperT_ = rnd(12, 25);
        knockT_ = rnd(30, 55);
        subT_ = 0;
        clashAnim_ = 0;
    }

    int level(int base) const { return base + (hour() >= 2) + (hour() >= 4); }
    int hour() const { return std::min(6, (int)(nightT_ / kHourLen)); }
    std::string clockText() const { return hour() == 0 ? "12 AM" : std::to_string(hour()) + " AM"; }

    void caption(const std::string& s, Color c = col::text, float dur = 2.6f) {
        for (auto& cp : captions_)
            if (cp.text == s) { cp.t = dur; return; }
        captions_.push_back({s, c, dur});
        if (captions_.size() > 3) captions_.erase(captions_.begin());
    }

    // ============================================================ events
    void jumpscare(Who w) {
        if (phase_ == Phase::Scare) return;
        phase_ = Phase::Scare;
        scareWho_ = w;
        scareT_ = 0;
        for (Sfx x : {Sfx::Riser, Sfx::MusicBox, Sfx::Whisper}) E().sfx.stop(x);
        E().sfx.play(Sfx::Scream, 1.0f);
        E().sfx.play(Sfx::Sting, 0.8f, 0.9f);
        E().sfx.play(Sfx::Boom, 0.8f);
        if (w == Who::Paardjes) E().sfx.play(Sfx::Neigh, 0.9f, 0.6f);
        E().shake(30);
        E().aberrate(0.03f);
        E().flash(WHITE, 0.7f);
    }

    void winNight() {
        phase_ = Phase::Win;
        winT_ = 0;
        bellsRung_ = 0;
        reward_ = 60LL * (night_ + 1) + (night_ == 5 ? 500 : 0);
        bool first = !(P().fnapBeaten & (1 << night_));
        if (!first) reward_ /= 2;
        P().fnapBeaten |= 1 << night_;
        P().fnapNight = std::max(P().fnapNight, std::min(6, night_ + 2));
        P().earn(reward_);
        P().save();
    }

    void go(View v) {
        if (moveT_ > 0 || v == view_) return;
        pendingView_ = v;
        moveT_ = 0.3f;
        E().sfx.play(Sfx::Step, 0.55f, rnd(0.9f, 1.1f), v == View::Left ? -0.5f : v == View::Right ? 0.5f : 0);
    }

    void arrive(View v) {
        view_ = v;
        // arriving at a door where someone waits: you hear them straight away
        for (int s = 0; s < 2; s++)
            if (v == (s == 0 ? View::Left : View::Right) && hall_[s].stage == 3) {
                hall_[s].cueT = 0;
            }
        if (v == View::Closet && horse_ >= 4) scareDelay_ = 0.45f;
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        for (auto& c : captions_) c.t -= dt;
        std::erase_if(captions_, [](auto& c) { return c.t <= 0; });
        renderLightMap();

        switch (phase_) {
            case Phase::Title: updateTitle(dt); break;
            case Phase::Intro:
                introT_ += dt;
                ambience(dt);
                if (introNote_ ? (introT_ > 1.5f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || E().mousePressed()))
                               : introT_ > 3.6f) {
                    E().consumeClick();
                    phase_ = Phase::Play;
                    E().sfx.play(Sfx::Static, 0.5f);
                    staticT_ = 0.4f;
                }
                if (E().backPressed()) phase_ = Phase::Title;
                break;
            case Phase::Play: updatePlay(dt); break;
            case Phase::Scare:
                scareT_ += dt;
                if (scareT_ < 1.2f) {
                    E().shake(6);
                    E().aberrate(0.02f * (1 - scareT_ / 1.2f));
                }
                if (scareT_ > 1.5f) {
                    phase_ = Phase::GameOver;
                    overT_ = 0;
                    E().sfx.play(Sfx::Static, 0.7f);
                    E().sfx.play(Sfx::Tinnitus, 0.6f);
                }
                break;
            case Phase::GameOver:
                overT_ += dt;
                if (E().backPressed()) phase_ = Phase::Title;
                break;
            case Phase::Win:
                winT_ += dt;
                if (bellsRung_ < 6 && winT_ > 0.6f + bellsRung_ * 0.55f) {
                    E().sfx.play(Sfx::Bell, 0.7f, 0.75f);
                    bellsRung_++;
                    if (bellsRung_ == 6) {
                        E().sfx.play(Sfx::Win);
                        fx_.confetti({VW / 2.0f, 260}, 160);
                    }
                }
                if (E().backPressed()) phase_ = Phase::Title;
                break;
        }
        staticT_ = std::max(0.0f, staticT_ - dt);
    }

    void ambience(float dt) {
        droneT_ -= dt;
        if (droneT_ <= 0) {
            droneT_ = 3.2f;
            E().sfx.play(Sfx::Drone, 0.55f, rnd(0.97f, 1.03f));
        }
        // it's always raining in Holland
        rainT_ -= dt;
        if (rainT_ <= 0) {
            rainT_ = 3.1f;
            bool inside = phase_ == Phase::Play && view_ != View::Center;  // the window is in the bedroom
            E().sfx.play(Sfx::Rain, inside ? 0.22f : 0.4f, rnd(0.95f, 1.05f), 0.25f);
        }
        windT_ -= dt;
        if (windT_ <= 0) {
            windT_ = rnd(7, 16);
            E().sfx.play(Sfx::Wind, rnd(0.25f, 0.45f), rnd(0.85f, 1.15f));
        }
        // lightning, and the thunder a moment later
        thunderT_ -= dt;
        if (thunderT_ <= 0) {
            thunderT_ = rnd(20, 42);
            boltT_ = 0;
            thunderDelay_ = rnd(0.15f, 1.3f);
        }
        if (devBolt_) boltT_ = 0.2f;
        else if (boltT_ >= 0) {
            boltT_ += dt;
            if (thunderDelay_ > 0 && boltT_ >= thunderDelay_) {
                thunderDelay_ = -1;
                E().sfx.play(Sfx::Thunder, rnd(0.6f, 0.9f), rnd(0.85f, 1.1f), rnd(-0.4f, 0.4f));
                E().shake(3);
            }
            if (boltT_ > 3) boltT_ = -1;
        }
    }

    // flicker pattern of a lightning strike: flash, dip, second flash, fade
    float lightning() const {
        if (boltT_ < 0) return 0;
        float t = boltT_;
        if (t < 0.06f) return 1;
        if (t < 0.11f) return 0.15f;
        if (t < 0.17f) return 0.85f;
        return 0.85f * std::exp(-(t - 0.17f) * 7);
    }

    void updateTitle(float dt) {
        ambience(dt);
        musicT_ -= dt;
        if (musicT_ <= 0) {
            musicT_ = 7.2f;
            E().sfx.play(Sfx::MusicBox, 0.5f, 0.97f);
        }
        if (std::fmod(t_, 9.0f) < dt) E().sfx.play(Sfx::Giggle, 0.12f, 0.9f, rnd(-0.8f, 0.8f));
        if (E().backPressed()) {
            if (howTo_) howTo_ = false;
            else E().go(makeMenu);
        }
    }

    bool lightOn() const {
        if (devLight_) return true;
        if (moveT_ > 0 || phase_ != Phase::Play) return false;
        bool mouse = view_ != View::Center && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        return IsKeyDown(KEY_F) || mouse;
    }
    bool holding() const {
        if (devHold_) return true;
        if (moveT_ > 0 || phase_ != Phase::Play) return false;
        if (view_ != View::Left && view_ != View::Right && view_ != View::Closet) return false;
        return IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    }

    void updatePlay(float dt) {
        ambience(dt);
        nightT_ += dt;
        if (nightT_ >= 6 * kHourLen) { winNight(); return; }
        const NightAI& ai = kAI[night_];

        // --- moving between spots
        if (moveT_ > 0) {
            float before = moveT_;
            moveT_ -= dt;
            if (before > 0.15f && moveT_ <= 0.15f) arrive(pendingView_);
        } else {
            bool l = IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT), r = IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT);
            bool u = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP), d = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN);
            switch (view_) {
                case View::Center:
                    if (l) go(View::Left);
                    if (r) go(View::Right);
                    if (u) go(View::Closet);
                    if (d) go(View::Bed);
                    if (IsKeyPressed(KEY_E) && pcOn_) pcOff();
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
        // a little flashlight flicker when things are close
        flicker_ = danger() > 0.6f && rndi(0, 40) == 0 ? 0.08f : std::max(0.0f, flicker_ - dt);

        float speed = pcOn_ ? 2.0f : 1.0f;
        if (devFreeze_) speed = 0;
        // --- Davi (0) and Kira (1)
        for (int s = 0; s < 2; s++) {
            Hall& h = hall_[s];
            View door = s == 0 ? View::Left : View::Right;
            bool here = view_ == door && moveT_ <= 0;
            int lvl = level(s == 0 ? ai.davi : ai.kira);
            if (h.lureAnim >= 0) {
                h.lureAnim += dt;
                if (h.lureAnim > 1.2f) h.lureAnim = -1;
            }
            if (h.stunT > 0) h.stunT -= dt;
            else if (h.stage < 3) {
                h.moveT -= dt * speed;
                if (h.moveT <= 0) {
                    h.moveT = (s == 0 ? 4.6f : 4.0f) * rnd(0.8f, 1.2f);
                    if (rndi(1, 20) <= lvl) {
                        h.stage++;
                        h.seen = false;
                        if (h.stage == 3) {
                            h.attackT = std::max(4.5f, 8.5f - night_ * 0.6f);
                            h.heldT = 0;
                            h.needHold = rnd(2.0f, 3.2f);
                            h.cueT = 0;
                        } else {
                            if (s == 1 && h.stage == 2) h.hidden = rndi(0, 100) < 30;
                            // Davi clomps around in his klompen; Kira rings the bell of the omafiets in the hall
                            float near = here ? 0.6f : h.stage == 2 ? 0.35f : 0.2f;
                            if (s == 0) {
                                E().sfx.play(Sfx::Klomp, near, rnd(0.9f, 1.05f), -0.7f);
                                klompEcho_ = 0.45f;
                            } else {
                                E().sfx.play(Sfx::FietsBel, near * 0.7f, rnd(0.78f, 0.86f), 0.7f);
                            }
                            if (!here) caption(s == 0 ? "[ klompen clacking - left hall ]" : "[ a bike bell rings - right hall ]", col::muted, 2);
                        }
                    }
                }
            }
            // seeing them (flashlight or lightning) for the first time this approach: stinger
            if (here && (h.stage == 1 || h.stage == 2) && !h.hidden && !h.seen && ((light && !hold) || lightning() > 0.5f) && !devFreeze_) {
                h.seen = true;
                E().sfx.play(Sfx::Sting, h.stage == 2 ? 0.65f : 0.35f, rnd(0.95f, 1.05f), s == 0 ? -0.3f : 0.3f);
                if (h.stage == 2) E().aberrate(0.008f);
            }
            // flashlight in the hall
            if (here && light && !hold) {
                h.flashT += dt;
                if (h.stage == 3 && h.flashT > 0.1f && !devFreeze_) { jumpscare(s == 0 ? Who::Davi : Who::Kira); return; }
                if ((h.stage == 1 || h.stage == 2) && !h.hidden && h.flashT > 0.35f && !devFreeze_) {
                    h.stage = 0;
                    h.moveT = 5;
                    E().sfx.play(Sfx::Hurt, 0.5f, 0.45f, s == 0 ? -0.5f : 0.5f);
                    caption(s == 0 ? "[ Davi runs back into the dark ]" : "[ Kira skitters away ]", col::muted);
                    flashScare_ = 1;
                }
            } else {
                h.flashT = 0;
            }
            // at the door
            if (h.stage == 3) {
                if (here && hold) {
                    h.heldT += dt;
                    if (h.heldT >= h.needHold) {
                        h.stage = 0;
                        h.moveT = 6;
                        h.hidden = false;
                        E().sfx.play(Sfx::Step, 0.4f, 0.8f, s == 0 ? -0.6f : 0.6f);
                        caption("[ footsteps fading away ]", col::muted);
                    }
                } else if (!devFreeze_) {
                    h.attackT -= dt;
                    if (h.attackT <= 0) { jumpscare(s == 0 ? Who::Davi : Who::Kira); return; }
                }
                if (here) {
                    h.cueT -= dt;
                    if (h.cueT <= 0) {
                        h.cueT = 2.5f;
                        if (s == 0) E().sfx.play(Sfx::Breath, hold ? 0.35f : 0.8f, 1, -0.4f);
                        else E().sfx.play(Sfx::Giggle, hold ? 0.3f : 0.6f, rnd(0.95f, 1.05f), 0.4f);
                    }
                    caption(s == 0 ? "[ heavy breathing right outside the door ]" : "[ soft giggling right outside the door ]", col::red, 0.3f);
                }
            }
            // lure: Q throws a stroopwafel (Davi) or boba (Kira)
            if (here && IsKeyPressed(KEY_Q)) lure(s);
        }

        // --- Het Nachtpaard in the closet
        bool atCloset = view_ == View::Closet && moveT_ <= 0;
        if (!atCloset && horse_ < 4) {
            horseT_ -= dt * speed;
            if (horseT_ <= 0) {
                horseT_ = 6.0f * rnd(0.8f, 1.2f);
                if (rndi(1, 20) <= level(ai.horse) + (paardjes_ >= 2 ? 3 : 0)) {
                    horse_++;
                    if (horse_ >= 2) E().sfx.play(Sfx::Scratch, horse_ >= 3 ? 0.5f : 0.25f, rnd(0.9f, 1.1f));
                    if (horse_ >= 3) {
                        E().sfx.play(Sfx::Creak, 0.45f, 0.7f);
                        caption("[ something scratching inside the closet ]", col::orange);
                    }
                }
            }
        }
        if (horse_ >= 4 && !devFreeze_) {
            horseLinger_ += dt;
            if (horseLinger_ > 12) { jumpscare(Who::Horse); return; }
        }
        if (scareDelay_ > 0) {
            scareDelay_ -= dt;
            if (scareDelay_ <= 0 && horse_ >= 4 && view_ == View::Closet) { jumpscare(Who::Horse); return; }
        }
        if (atCloset && hold && horse_ > 0 && horse_ < 4) {
            horseHold_ += dt;
            if (std::fmod(horseHold_, 0.5f) < dt) E().sfx.play(Sfx::Creak, 0.35f, 1.2f);
            if (horseHold_ >= 1.6f + horse_ * 0.4f) {
                horse_ = 0;
                horseHold_ = 0;
                horseT_ = 7;
                caption("[ the closet goes quiet ]", col::muted);
            }
        } else if (!hold) {
            horseHold_ = 0;
        }

        // --- Paardjes on the bed
        bool atBed = view_ == View::Bed && moveT_ <= 0;
        if (!atBed && paardjes_ < 3) {
            paardT_ -= dt * speed;
            if (paardT_ <= 0) {
                paardT_ = 7.0f * rnd(0.8f, 1.2f);
                if (rndi(1, 20) <= level(ai.paardjes)) {
                    paardjes_++;
                    E().sfx.play(Sfx::Giggle, 0.22f, 1.7f);
                    if (paardjes_ >= 2) caption("[ tiny giggles from the bed ]", col::pink);
                }
            }
        }
        if (paardjes_ >= 3 && !devFreeze_) {
            bedT_ += dt;
            if (bedT_ > 6) { jumpscare(Who::Paardjes); return; }
        } else {
            bedT_ = 0;
        }
        if (atBed && light && paardjes_ > 0 && !devFreeze_) {
            bedFlash_ += dt;
            if (bedFlash_ > 0.5f) {
                bedFlash_ = 0;
                paardjes_--;
                Vector2 p = bedSpot(paardjes_);
                fx_.burst(p, 18, rgba(0x8a5a3aff), 300, 6, 0.5f, PShape::Circle, 600);
                fx_.burst(p, 8, WHITE, 200, 4, 0.4f, PShape::Star);
                E().sfx.play(Sfx::Pop, 0.6f, 0.8f);
                E().sfx.play(Sfx::Neigh, 0.25f, 2.0f);
            }
        } else {
            bedFlash_ = 0;
        }

        // --- Kira's PC prank: a music box blares from the PC
        if (night_ >= 1 && !pcOn_ && !devFreeze_) {
            pcT_ -= dt;
            if (pcT_ <= 0) {
                pcT_ = 18;
                if (rndi(1, 20) <= level(ai.pc)) {
                    pcOn_ = true;
                    pcSongT_ = 0;
                    E().sfx.play(Sfx::Static, 0.5f);
                    clash();
                }
            }
        }
        if (pcOn_) {
            pcSongT_ -= dt;
            if (pcSongT_ <= 0) {
                pcSongT_ = 7.2f;
                E().sfx.play(Sfx::MusicBox, view_ == View::Center ? 0.6f : 0.35f, 0.95f, 0.3f);
            }
            caption("[ a music box is playing from the PC ]", col::violet, 0.3f);
        }

        updateMonkey(dt);
        if (phase_ != Phase::Play) return;
        events(dt);

        // --- heartbeat speeds up with danger
        float dg = danger();
        if (dg > 0.2f) {
            heartT_ -= dt;
            if (heartT_ <= 0) {
                heartT_ = lerpf(1.1f, 0.42f, dg);
                E().sfx.play(Sfx::Heart, 0.3f + 0.5f * dg);
                heartPulse_ = 1;
            }
        }
        heartPulse_ = std::max(0.0f, heartPulse_ - dt * 3);
        flashScare_ = std::max(0.0f, flashScare_ - dt * 2);

        if (E().backPressed()) phase_ = Phase::Title;
    }

    void clash() {
        clashAnim_ = 1;
        E().sfx.play(Sfx::Cymbal, view_ == View::Center ? 0.45f : 0.22f, rnd(0.95f, 1.05f), 0.5f);
    }

    // The Monkey Man at the window: harmless, unless you keep looking at him.
    void updateMonkey(float dt) {
        clashAnim_ = std::max(0.0f, clashAnim_ - dt * 5);
        bool watching = view_ == View::Center && moveT_ <= 0;
        if (!monkeyOn_) {
            monkeyRise_ = damp(monkeyRise_, 0, 6, dt);
            monkeyBang_ = std::max(0.0f, monkeyBang_ - dt * 2);
            if (night_ < 1 || devFreeze_) return;
            monkeyNext_ -= dt;
            if (monkeyNext_ <= 0) {
                monkeyOn_ = true;
                monkeyT_ = monkeyStare_ = 0;
                riser_ = false;
                E().sfx.play(Sfx::Knock, 0.12f, 1.9f, 0.5f);  // tik tik on the glass
                caption("[ tik... tik... something at the window ]", col::orange, 3);
            }
            return;
        }
        monkeyT_ += dt;
        monkeyRise_ = damp(monkeyRise_, 1, 3, dt);
        if (std::fmod(monkeyT_, 1.3f) < dt) clash();
        if (watching) {
            monkeyStare_ += dt;
            if (monkeyStare_ > 1.6f && !riser_) {
                riser_ = true;
                E().sfx.play(Sfx::Riser, 0.5f);
                caption("[ don't look at him ]", col::red, 2);
            }
        } else if (riser_) {
            riser_ = false;
            monkeyStare_ = std::min(monkeyStare_, 1.0f);
            E().sfx.stop(Sfx::Riser);
        }
        if (monkeyStare_ >= 4.6f) {
            // BANG BANG BANG: the whole house is awake now
            monkeyOn_ = false;
            monkeyBang_ = 1;
            monkeyNext_ = rnd(40, 70) - night_ * 4;
            E().sfx.stop(Sfx::Riser);
            E().sfx.play(Sfx::Knock, 1.0f, 0.7f, 0.5f);
            E().sfx.play(Sfx::Monkey, 0.9f, rnd(0.9f, 1.05f), 0.4f);
            E().sfx.play(Sfx::Boom, 0.4f, 1.4f);
            clash();
            E().shake(14);
            E().aberrate(0.012f);
            for (auto& h : hall_)
                if (h.stage < 2 && h.stunT <= 0) {
                    h.stage++;
                    h.seen = false;
                }
            caption("[ the Monkey Man woke up the whole house ]", col::red, 3.5f);
        } else if (monkeyT_ > 11) {
            monkeyOn_ = false;
            monkeyNext_ = rnd(40, 70) - night_ * 4;
            caption("[ the window is empty again ]", col::muted);
        }
    }

    // Things that go bump in the night: the church bells, whispers, knocking, flashes.
    void events(float dt) {
        if (hour() != lastHour_) {
            lastHour_ = hour();
            if (lastHour_ > 0 && lastHour_ < 6) {
                E().sfx.play(Sfx::Carillon, 0.4f, 0.98f);
                caption("[ the church carillon plays - " + clockText() + " ]", col::gold, 4);
                hourPulse_ = 1;
            }
        }
        hourPulse_ = std::max(0.0f, hourPulse_ - dt * 0.5f);
        if (klompEcho_ > 0 && (klompEcho_ -= dt) <= 0) E().sfx.play(Sfx::Klomp, 0.15f, rnd(0.88f, 0.95f), -0.8f);
        if (night_ >= 1) {
            whisperT_ -= dt;
            if (whisperT_ <= 0) {
                whisperT_ = rnd(14, 32) - night_ * 1.5f;
                E().sfx.play(Sfx::Whisper, rnd(0.3f, 0.5f), rnd(0.9f, 1.1f));
                caption("[ whispering... \"pieter...\" ]", col::muted, 2);
            }
        }
        if (night_ >= 2) {  // knocking at a door - maybe nobody's there
            knockT_ -= dt;
            if (knockT_ <= 0) {
                knockT_ = rnd(28, 55);
                bool left = chance(0.5f);
                E().sfx.play(Sfx::Knock, 0.45f, rnd(0.9f, 1.05f), left ? -0.8f : 0.8f);
                caption(left ? "[ knocking - left door ]" : "[ knocking - right door ]", col::muted, 2);
            }
        }
        // when things get bad you start seeing faces
        subT_ = std::max(0.0f, subT_ - dt);
        if (danger() > 0.5f && subT_ <= 0 && chance(dt * 0.04f * (1 + night_ * 0.3f))) {
            subT_ = 0.07f;
            subFace_ = rndi(0, 2);
            E().sfx.play(Sfx::Static, 0.25f, 1.3f);
        }
    }

    float danger() const {
        float d = 0;
        for (auto& h : hall_) d = std::max(d, h.stage == 3 ? 1.0f : h.stage * 0.25f);
        d = std::max(d, horse_ / 4.0f);
        d = std::max(d, paardjes_ / 3.0f);
        return d;
    }

    void lure(int s) {
        Hall& h = hall_[s];
        const char* item = s == 0 ? "stroopwafel" : "boba";
        if (h.lureUsed) { caption("[ already used a lure on this side tonight ]", col::muted); E().sfx.play(Sfx::Error, 0.4f); return; }
        if (h.stage == 3) { caption("[ too close... it won't work now ]", col::red); E().sfx.play(Sfx::Error, 0.4f); return; }
        if (!P().take(item)) {
            caption(s == 0 ? "[ no stroopwafels left - buy some in the shop ]" : "[ no boba left - buy some in the shop ]", col::orange);
            E().sfx.play(Sfx::Error, 0.4f);
            return;
        }
        h.lureUsed = true;
        h.lureAnim = 0;
        h.stage = 0;
        h.stunT = 15;
        h.hidden = false;
        E().sfx.play(Sfx::Whoosh, 0.6f);
        caption(s == 0 ? "[ Davi goes after the stroopwafel ]" : "[ Kira goes after the boba ]", col::gold);
    }

    void pcOff() {
        pcOn_ = false;
        pcT_ = 18;
        E().sfx.play(Sfx::Click);
        E().sfx.play(Sfx::Static, 0.3f, 1.4f);
    }

    Vector2 bedSpot(int i) const {
        const Vector2 spots[3] = {{380, 520}, {660, 440}, {940, 530}};
        return spots[std::clamp(i, 0, 2)];
    }

    // ============================================================ light map (center view)
    void renderLightMap() {
        if (!lm_.id) return;
        env_.t = t_;
        BeginTextureMode(lm_);
        ClearBackground(rgba(0x3a3666ff));
        rlPushMatrix();
        rlScalef(0.5f, 0.5f, 1);
        fx::beginAdd();
        room::addLights(env_);
        fx::glow({70, 330}, 230, alpha(rgba(0x6878d8ff), 0.45f));  // moonlight spilling in from both halls
        fx::glow({VW - 70.0f, 330}, 230, alpha(rgba(0x6878d8ff), 0.45f));
        if (pcOn_) fx::glow({1030, 350}, 420, alpha(std::fmod(t_, 0.5f) < 0.25f ? col::pink : col::red, 0.8f));
        if (float L = lightning(); L > 0) {
            fx::glow({975, 190}, 900, alpha(rgba(0xc8d4ffff), 0.9f * L));
            fx::glow({975, 600}, 500, alpha(rgba(0xc8d4ffff), 0.5f * L));
        }
        fx::endAdd();
        rlPopMatrix();
        EndTextureMode();
    }

    // ============================================================ draw: shared
    void darkness(float level, bool light) {
        level *= 1 - 0.6f * lightning();
        // pitch black except for the flashlight
        if (!light || flicker_ > 0) {
            DrawRectangle(-50, -50, VW + 100, VH + 100, alpha(BLACK, level));
            return;
        }
        Vector2 m = devLight_ ? Vector2{VW / 2.0f, VH / 2.0f - 30} : E().mouse();
        float R = 330;
        Color tint{255, 255, 255, (unsigned char)(level * 255)};
        DrawTexturePro(mask_, {0, 0, (float)mask_.width, (float)mask_.height}, {m.x - R, m.y - R, R * 2, R * 2}, {0, 0}, 0, tint);
        Color k = alpha(BLACK, level);
        DrawRectangle(-50, -50, VW + 100, (int)(m.y - R) + 50, k);
        DrawRectangle(-50, (int)(m.y + R), VW + 100, VH, k);
        DrawRectangle(-50, (int)(m.y - R), (int)(m.x - R) + 50, (int)(R * 2), k);
        DrawRectangle((int)(m.x + R), (int)(m.y - R), VW, (int)(R * 2), k);
        fx::beginAdd();
        fx::glow(m, R * 0.9f, alpha(rgba(0xfff0c0ff), 0.10f));
        fx::endAdd();
    }

    void grain(float amount) {
        // film grain + scanlines
        float ox = hash(std::floor(t_ * 24)) * 256, oy = hash(std::floor(t_ * 24) + 7) * 256;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(noise_, {ox, oy, VW / 2.0f, VH / 2.0f}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, alpha(WHITE, amount));
        EndBlendMode();
        for (int y = 0; y < VH; y += 4) DrawRectangle(0, y, VW, 1, alpha(BLACK, 0.12f));
    }

    void staticScreen(float a) {
        float ox = hash(std::floor(t_ * 30)) * 256, oy = hash(std::floor(t_ * 30) + 3) * 256;
        DrawTexturePro(noise_, {ox, oy, VW / 3.0f, VH / 3.0f}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, alpha(rgba(0xb0b0c0ff), a));
        for (int i = 0; i < 6; i++) {
            float y = std::fmod(t_ * 300 + i * 137, (float)VH);
            DrawRectangle(0, (int)y, VW, 3 + i % 3, alpha(WHITE, 0.08f * a));
        }
    }

    void drawCaptions() {
        float y = VH - 40;
        for (int i = (int)captions_.size() - 1; i >= 0; i--) {
            auto& c = captions_[i];
            float a = clamp01(c.t / 0.3f);
            float w = ui::measure(c.text, 18, true) + 30;
            DrawRectangleRounded({VW / 2.0f - w / 2, y - 16, w, 32}, 0.5f, 8, alpha(BLACK, 0.6f * a));
            ui::text(c.text, VW / 2.0f, y, 18, alpha(c.color, a), ui::Center, true);
            y -= 38;
        }
    }

    void drawHud() {
        // clock
        ui::textShadow(clockText(), VW - 30, 44, 44, mix(rgba(0xf4ecffff), col::gold, hourPulse_), ui::Right, true, 3);
        ui::text("NIGHT " + std::to_string(night_ + 1), VW - 30, 82, 18, col::muted, ui::Right, true);
        // lures
        for (int s = 0; s < 2; s++) {
            Vector2 p{30.0f + s * 54, 42};
            const char* item = s == 0 ? "stroopwafel" : "boba";
            bool used = hall_[s].lureUsed;
            DrawCircleV(p, 22, alpha(BLACK, 0.5f));
            art::itemIcon(item, p, 34);
            if (used || P().count(item) == 0) {
                DrawCircleV(p, 22, alpha(BLACK, 0.6f));
                DrawLineEx({p.x - 14, p.y - 14}, {p.x + 14, p.y + 14}, 3, col::red);
            }
            ui::text(std::to_string(P().count(item)), p.x + 16, p.y + 18, 13, col::text, ui::Center, true);
        }
        ui::text("Q lure", 57, 78, 13, col::muted, ui::Center, true);
        // controls
        if (night_ == 0 || nightT_ < 25) {
            float a = night_ == 0 ? 0.6f : clamp01((25 - nightT_) / 3) * 0.6f;
            const char* hint = view_ == View::Center ? "A/D doors   W closet   S bed   E turn off PC"
                                                     : "F / left mouse = flashlight     SPACE / right mouse = hold shut     back: opposite key";
            ui::text(hint, 24, VH - 20, 15, alpha(col::text, a), ui::Left, true);
        }
        // heartbeat vignette
        float dg = danger();
        if (dg > 0.2f) {
            float a = (0.1f + 0.2f * heartPulse_) * dg;
            DrawRectangleGradientV(0, 0, VW, 140, alpha(rgba(0x600010ff), a), alpha(BLACK, 0));
            DrawRectangleGradientV(0, VH - 140, VW, 140, alpha(BLACK, 0), alpha(rgba(0x600010ff), a));
            DrawRectangleGradientH(0, 0, 160, VH, alpha(rgba(0x600010ff), a), alpha(BLACK, 0));
            DrawRectangleGradientH(VW - 160, 0, 160, VH, alpha(BLACK, 0), alpha(rgba(0x600010ff), a));
        }
    }

    // ============================================================ draw: center (the room)
    Vector2 camShift() const {
        Vector2 m = E().mouse();
        return {-(m.x - VW / 2.0f) * 0.035f, -(m.y - VH / 2.0f) * 0.02f};
    }

    // door on a side wall, in the room's perspective (wall runs from x=0 to 170)
    void sideDoor(bool left) {
        auto wallY = [](float x, float k) {  // k: 0 = ceiling line, 1 = floor line
            float top = -108 + 108 * (x / 170), floor = 531 - 61 * (x / 170);
            return lerpf(top, floor, k);
        };
        float x0 = 22, x1 = 142;
        auto P2 = [&](float x, float k) { return left ? Vector2{x, wallY(x, k)} : Vector2{VW - x, wallY(x, k)}; };
        Vector2 tl = P2(x0, 0.22f), tr = P2(x1, 0.22f), br = P2(x1, 1), bl = P2(x0, 1);
        Vector2 ftl = P2(x0 - 10, 0.19f), ftr = P2(x1 + 8, 0.19f), fbr = P2(x1 + 8, 1), fbl = P2(x0 - 10, 1);
        quad(ftl, ftr, fbr, fbl, rgba(0x6a4a8aff));
        quad(tl, tr, br, bl, rgba(0x04030aff), rgba(0x0c0818ff));
        // a sliver of moonlight from the hall window on the floor
        quad(Vector2Lerp(bl, br, 0.2f), Vector2Lerp(bl, br, 0.6f), Vector2Lerp(bl, br, 0.6f) + Vector2{0, -6}, Vector2Lerp(bl, br, 0.2f) + Vector2{0, -6}, alpha(rgba(0x6878d8ff), 0.5f));
        ui::text(left ? "<" : ">", (tl.x + tr.x) / 2, (tl.y + bl.y) / 2, 40, alpha(col::muted, 0.15f), ui::Center, true);
        doorQuad_[left ? 0 : 1][0] = ftl;
        doorQuad_[left ? 0 : 1][1] = ftr;
        doorQuad_[left ? 0 : 1][2] = fbr;
        doorQuad_[left ? 0 : 1][3] = fbl;
        doorEyePos_[left ? 0 : 1] = Vector2Lerp(Vector2Lerp(tl, tr, 0.5f), Vector2Lerp(bl, br, 0.5f), 0.32f);
    }

    // drawn after the light map, so the dark doesn't swallow them
    void centerEyes() {
        // moonlit rim around both doorframes so you can find them in the dark
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < 4; i++) DrawLineEx(doorQuad_[s][i], doorQuad_[s][(i + 1) % 4], 3, alpha(rgba(0x8f9cffff), 0.35f));
        for (int s = 0; s < 2; s++)
            if (hall_[s].stage >= 2 && !hall_[s].hidden) eyesOnly(s, doorEyePos_[s], hall_[s].stage == 3 ? 120 : 80, hall_[s].stage == 3 ? 0.9f : 0.6f, t_);
        if (horse_ >= 2) eyesOnly(2, {655 - 26, 296}, 60, 0.3f + 0.2f * horse_, t_);
        // the lightning lights up the halls behind the doors, and shows who's standing in them
        if (float L = lightning(); L > 0.2f)
            for (int s = 0; s < 2; s++) {
                quad(doorQuad_[s][0], doorQuad_[s][1], doorQuad_[s][2], doorQuad_[s][3], alpha(rgba(0x9aa8e0ff), 0.55f * L), alpha(rgba(0x404870ff), 0.4f * L));
                if (hall_[s].stage == 3) {
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

    // drawn behind the glass of the bedroom window
    void outsideWindow() {
        Rectangle g = room::kWindowGlass;
        float L = lightning();
        if (L > 0) DrawRectangleRec(g, alpha(rgba(0xd8e0ffff), 0.75f * L));
        if (L > 0.5f && std::fmod(t_ * 7, 1) < 0.5f) {  // the bolt itself
            Vector2 p{g.x + 150, g.y};
            for (int i = 0; i < 6; i++) {
                Vector2 q = p + Vector2{(hash(i + std::floor(thunderT_)) - 0.5f) * 30, g.height / 6};
                DrawLineEx(p, q, 2, alpha(WHITE, L));
                p = q;
            }
        }
        windmill({g.x + 170, g.y + g.height + 2}, 0.9f, t_, rgba(0x0a0618ff));
        if (monkeyRise_ > 0.02f || monkeyBang_ > 0) {
            Vector2 h = monkeyHead();
            float angry = std::max(monkeyBang_, riser_ ? clamp01((monkeyStare_ - 1.6f) / 3) * 0.5f : 0);
            // hands on the glass
            for (int k : {-1, 1}) {
                Vector2 hand{h.x + k * 66, h.y + 30 - (monkeyBang_ > 0 ? std::abs(std::sin(monkeyBang_ * 20)) * 16 : 0)};
                DrawEllipse((int)hand.x, (int)hand.y, 14, 17, rgba(0x4a3428ff));
                for (int f = 0; f < 4; f++) DrawLineEx(hand, hand + Vector2{(f - 1.5f) * 7.0f, -26}, 6, rgba(0x4a3428ff));
            }
            monkeyMan(h, kMonkeyR, t_, angry);
        }
        // rain running down the glass
        for (int i = 0; i < 26; i++) {
            float x = g.x + hash(i + 0.3f) * g.width, sp = 120 + hash(i + 5.0f) * 120;
            float y = g.y - 20 + std::fmod(t_ * sp + hash(i + 2.0f) * 400, g.height + 40);
            DrawLineEx({x, y}, {x - 2, y + 12}, 1.2f, alpha(rgba(0xc0d0ffff), 0.25f));
        }
    }

    void closetCenter() {
        Rectangle c{548, 176, 214, 296};
        fx::softShadow({c.x + c.width / 2, c.y + c.height}, 130, 12, 0.6f);
        DrawRectangleRounded(shrink(c, -4), 0.04f, 6, col::outline);
        DrawRectangleRec(c, rgba(0x3a2418ff));
        DrawRectangleRec({c.x - 8, c.y - 14, c.width + 16, 18}, rgba(0x4a2e1eff));
        float gap = 6 + horse_ * 6.0f;
        DrawRectangleRec({c.x + c.width / 2 - gap / 2, c.y + 10, gap, c.height - 24}, rgba(0x020104ff));
        for (int k : {-1, 1}) {
            float x = c.x + c.width / 2 + k * (gap / 2 + 4);
            DrawRectangleLinesEx({k < 0 ? c.x + 10 : x, c.y + 14, c.width / 2 - gap / 2 - 14, 120}, 2, alpha(BLACK, 0.4f));
            DrawRectangleLinesEx({k < 0 ? c.x + 10 : x, c.y + 146, c.width / 2 - gap / 2 - 14, 120}, 2, alpha(BLACK, 0.4f));
            DrawCircleV({c.x + c.width / 2 + k * (gap / 2 + 10), c.y + 150}, 4, col::goldDark);
        }
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
        room::drawDesk(env_);
        toyMonkey({922, 450}, 1.1f, clashAnim_);
        // Kira's prank on the monitor
        Rectangle mon{926, 286, 210, 120};
        if (pcOn_) {
            DrawRectangleRec(mon, std::fmod(t_, 0.5f) < 0.25f ? rgba(0x300818ff) : rgba(0x18040cff));
            E().beginClip(mon);
            nightmare(1, {mon.x + mon.width / 2, mon.y + mon.height / 2 + 6}, 48 + std::sin(t_ * 9) * 3, std::sin(t_ * 5) * 10, 1, t_);
            float ox = hash(std::floor(t_ * 30)) * 256;
            DrawTexturePro(noise_, {ox, 0, 100, 60}, mon, {0, 0}, 0, alpha(WHITE, 0.25f));
            E().endClip();
            ui::text("HIHIHI", mon.x + mon.width / 2, mon.y + 14, 14, col::pink, ui::Center, true);
        }
        room::drawRug(env_);
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
        // hotspots
        if (moveT_ <= 0 && phase_ == Phase::Play) hotspots();
        rlPopMatrix();
        art::vignette(0.45f);
    }

    void hotspots() {
        struct H { Rectangle r; View v; const char* label; };
        H hs[] = {{{0, 40, 170, 520}, View::Left, "LEFT DOOR  [A]"},
                  {{VW - 170.0f, 40, 170, 520}, View::Right, "RIGHT DOOR  [D]"},
                  {{548, 170, 214, 300}, View::Closet, "CLOSET  [W]"},
                  {{166, 360, 296, 200}, View::Bed, "BED  [S]"}};
        Vector2 m = E().mouse();
        // the camera is shifted/scaled: map the mouse into room space
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
        if (pcOn_ && CheckCollisionPointRec(rm, mon)) {
            DrawRectangleRoundedLinesEx(shrink(mon, -6), 0.08f, 8, 3, col::pink);
            ui::textShadow("TURN OFF  [E]", mon.x + mon.width / 2, mon.y + mon.height + 24, 18, col::pink, ui::Center, true, 2);
            if (E().mousePressed()) {
                E().consumeClick();
                pcOff();
            }
        }
    }

    // ============================================================ draw: hallways
    static constexpr Rectangle kOpening{292, 34, 696, 720};
    static constexpr float kVPx = 640, kVPy = 330, kFar = 0.2f;

    // depth d (0 at the door, 1 at the far wall) -> perspective scale
    static float hallScale(float d) { return 1 / (1 + d * (1 / kFar - 1)); }
    static Vector2 hallPt(float u, float v, float d) {  // u: -1..1 left/right, v: -1 ceiling .. 1 floor
        float s = hallScale(d);
        return {kVPx + u * 348 * s, kVPy + (v < 0 ? v * 296 : v * 390) * s};
    }

    void drawHall(int side) {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        rlPushMatrix();
        rlTranslatef(pan.x, pan.y, 0);
        // bedroom wall around the door
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x2a1a50ff));
        for (int x = -60; x < VW + 60; x += 48) DrawRectangle(x, -20, 22, VH + 40, alpha(WHITE, 0.02f));
        // light switch + a poster
        float sx = side == 0 ? 1080 : 236;
        DrawRectangleRounded({sx - 18, 330, 36, 56}, 0.2f, 6, rgba(0xd8d0e0ff));
        DrawRectangleRounded({sx - 6, 344, 12, 26}, 0.3f, 6, rgba(0xa098b0ff));
        if (side == 0) {
            // a Delft blue tegeltje with a house saying. It changes as the nights go on.
            Rectangle tile{1050, 120, 170, 170};
            DrawRectangleRec({tile.x + 6, tile.y + 8, tile.width, tile.height}, alpha(BLACK, 0.4f));
            DrawRectangleRec(tile, rgba(0xe8eef8ff));
            DrawRectangleLinesEx(shrink(tile, 8), 3, rgba(0x2a4aa8ff));
            for (Vector2 c : {Vector2{tile.x + 20, tile.y + 20}, {tile.x + tile.width - 20, tile.y + 20}, {tile.x + 20, tile.y + tile.height - 20},
                              {tile.x + tile.width - 20, tile.y + tile.height - 20}})
                DrawPoly(c, 4, 7, 45, rgba(0x2a4aa8ff));
            bool late = night_ >= 3;
            Color ink = rgba(0x1e3a90ff);
            ui::text("Oost west,", tile.x + tile.width / 2, tile.y + 62, 22, ink, ui::Center, true);
            ui::text(late ? "niemand" : "thuis", tile.x + tile.width / 2, tile.y + 92, 22, late ? rgba(0x8a1020ff) : ink, ui::Center, true);
            ui::text(late ? "komt thuis" : "best", tile.x + tile.width / 2, tile.y + 122, 22, late ? rgba(0x8a1020ff) : ink, ui::Center, true);
        } else {
            // gig poster: Monkey Men live in the Paradiso
            Rectangle poster{24, 110, 180, 230};
            DrawRectangleRec({poster.x + 6, poster.y + 8, poster.width, poster.height}, alpha(BLACK, 0.4f));
            DrawRectangleRec(poster, rgba(0xd8c070ff));
            DrawRectangleRec(shrink(poster, 10), rgba(0x1a1210ff));
            monkeyMan({poster.x + poster.width / 2, poster.y + 92}, 42, t_, 0.6f);
            ui::text("MONKEY MEN", poster.x + poster.width / 2, poster.y + 166, 22, rgba(0xffd84aff), ui::Center, true);
            ui::text("live @ Paradiso", poster.x + poster.width / 2, poster.y + 190, 15, rgba(0xe8e0d0ff), ui::Center, true);
            ui::text("31 OKT  -  UITVERKOCHT", poster.x + poster.width / 2, poster.y + 210, 11, rgba(0xff6a4aff), ui::Center, true);
        }

        E().beginClip({kOpening.x + pan.x, kOpening.y, kOpening.width, kOpening.height});
        // far wall + window with the moon
        Vector2 ftl = hallPt(-1, -1, 1), fbr = hallPt(1, 1, 1);
        DrawRectangleV(ftl, fbr - ftl, rgba(0x1c1430ff));
        Rectangle win{kVPx - 26, kVPy - 44, 52, 62};
        float L = lightning();
        DrawRectangleRec(win, mix(rgba(0x1a2050ff), rgba(0xd8e0ffff), L));
        DrawCircleV({win.x + 34, win.y + 18}, 9, rgba(0xfff6d8ff));
        windmill({win.x + 16, win.y + win.height}, 0.4f, t_, rgba(0x0a0618ff));
        for (int i = 0; i < 6; i++) {
            float x = win.x + hash(i + 1.7f) * win.width, y = win.y + std::fmod(t_ * 60 + hash(i) * 80, win.height);
            DrawLineEx({x, y}, {x - 1, y + 5}, 1, alpha(rgba(0xc0d0ffff), 0.3f));
        }
        DrawLineEx({win.x + win.width / 2, win.y}, {win.x + win.width / 2, win.y + win.height}, 3, rgba(0x0c0814ff));
        DrawLineEx({win.x, win.y + win.height / 2}, {win.x + win.width, win.y + win.height / 2}, 3, rgba(0x0c0814ff));
        DrawRectangleLinesEx(win, 3, rgba(0x0c0814ff));
        // ceiling, walls
        quad(hallPt(-1, -1, 0), hallPt(1, -1, 0), hallPt(1, -1, 1), hallPt(-1, -1, 1), rgba(0x120c1eff), rgba(0x1e1630ff));
        quad(hallPt(-1, -1, 0), hallPt(-1, -1, 1), hallPt(-1, 1, 1), hallPt(-1, 1, 0), rgba(0x34224aff), rgba(0x24183aff));
        quad(hallPt(1, -1, 1), hallPt(1, -1, 0), hallPt(1, 1, 0), hallPt(1, 1, 1), rgba(0x34224aff), rgba(0x24183aff));
        // wallpaper stripes + wainscoting line along both walls
        for (int i = 0; i < 14; i++) {
            float d = i / 14.0f;
            for (int k : {-1, 1}) DrawLineEx(hallPt((float)k, -1, d), hallPt((float)k, 0.35f, d), 1.5f * hallScale(d), alpha(BLACK, 0.25f));
        }
        for (int k : {-1, 1}) DrawLineEx(hallPt((float)k, 0.35f, 0), hallPt((float)k, 0.35f, 1), 4, rgba(0x4a3060ff));
        // Dutch checkerboard floor
        const int rows = 12, cols = 6;
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++) {
                float d0 = (float)r / rows, d1 = (float)(r + 1) / rows, u0 = -1 + 2.0f * c / cols, u1 = -1 + 2.0f * (c + 1) / cols;
                Color cc = (r + c) % 2 ? rgba(0xb8b0c4ff) : rgba(0x1a1622ff);
                quad(hallPt(u0, 1, d0), hallPt(u1, 1, d0), hallPt(u1, 1, d1), hallPt(u0, 1, d1), cc);
            }
        // props: Delft blue plates + family photo (left), a Dutch bike + coat rack (right)
        if (side == 0) {
            for (int i = 0; i < 3; i++) {
                float d = 0.18f + i * 0.2f, s = hallScale(d);
                Vector2 p = hallPt(-1, -0.35f, d);
                DrawEllipse((int)p.x + (int)(6 * s), (int)p.y, 9 * s, 26 * s, rgba(0xe8eef8ff));
                DrawEllipse((int)p.x + (int)(6 * s), (int)p.y, 6 * s, 18 * s, rgba(0x2a4aa8ff));
                DrawEllipse((int)p.x + (int)(6 * s), (int)p.y, 3 * s, 9 * s, rgba(0xe8eef8ff));
            }
            // Davi's klompen by the wall. When they're gone, he's wearing them.
            if (hall_[0].stage == 0) {
                float kd = 0.22f, ks = hallScale(kd);
                Vector2 k = hallPt(-0.86f, 1, kd);
                for (int i = 0; i < 2; i++) {
                    Vector2 c{k.x + i * 46 * ks, k.y - 14 * ks - i * 4 * ks};
                    DrawEllipse((int)c.x, (int)(c.y + 12 * ks), 30 * ks, 8 * ks, alpha(BLACK, 0.4f));
                    DrawEllipse((int)c.x, (int)c.y, 30 * ks, 13 * ks, rgba(0xe8c040ff));
                    DrawEllipse((int)(c.x + 14 * ks), (int)(c.y - 9 * ks), 13 * ks, 6 * ks, rgba(0x2a1a10ff));
                    DrawEllipse((int)(c.x - 10 * ks), (int)(c.y - 2 * ks), 7 * ks, 4 * ks, rgba(0xc83020ff));
                }
            }
            float d = 0.72f, s = hallScale(d);
            Vector2 p = hallPt(-1, -0.2f, d);
            DrawRectangleRec({p.x + 2, p.y - 30 * s, 14 * s, 50 * s}, rgba(0x6a4a2aff));
            DrawRectangleRec({p.x + 4, p.y - 26 * s, 10 * s, 42 * s}, rgba(0x2a2a3aff));
        } else {
            float d = 0.35f, s = hallScale(d);
            Vector2 b = hallPt(0.82f, 1, d);
            Color bike = rgba(0x101018ff);
            DrawRing({b.x - 60 * s, b.y - 46 * s}, 40 * s, 46 * s, 0, 360, 32, bike);
            DrawRing({b.x + 60 * s, b.y - 46 * s}, 40 * s, 46 * s, 0, 360, 32, bike);
            DrawLineEx({b.x - 60 * s, b.y - 46 * s}, {b.x - 10 * s, b.y - 120 * s}, 7 * s, rgba(0x8a1a2aff));
            DrawLineEx({b.x - 10 * s, b.y - 120 * s}, {b.x + 60 * s, b.y - 46 * s}, 7 * s, rgba(0x8a1a2aff));
            DrawLineEx({b.x + 20 * s, b.y - 46 * s}, {b.x - 10 * s, b.y - 120 * s}, 6 * s, rgba(0x8a1a2aff));
            DrawLineEx({b.x - 40 * s, b.y - 130 * s}, {b.x - 10 * s, b.y - 120 * s}, 6 * s, bike);
            Vector2 rack = hallPt(0.95f, -0.1f, 0.7f);
            float rs = hallScale(0.7f);
            DrawLineEx(rack, {rack.x, rack.y + 120 * rs}, 4 * rs, rgba(0x5a3a2aff));
            DrawRectangleRec({rack.x - 12 * rs, rack.y + 8 * rs, 20 * rs, 50 * rs}, rgba(0xff7a1aff));  // orange Koningsdag scarf
        }
        // tulips in a vase, wilting a little more every night
        {
            float d = 0.3f, s = hallScale(d);
            Vector2 v = hallPt(side == 0 ? 0.78f : -0.78f, 1, d);
            DrawRectangleRounded({v.x - 14 * s, v.y - 50 * s, 28 * s, 50 * s}, 0.4f, 6, rgba(0x2a4aa8ff));
            DrawRectangleRounded({v.x - 9 * s, v.y - 38 * s, 18 * s, 16 * s}, 0.4f, 6, rgba(0xe8eef8ff));
            float droop = 0.15f + night_ * 0.22f;
            for (int i = 0; i < 3; i++) {
                float a = -PI / 2 + (i - 1) * 0.35f + droop * (i - 1 >= 0 ? 1 : -1) * (0.6f + i * 0.3f);
                Vector2 b{v.x + (i - 1) * 5 * s, v.y - 50 * s};
                Vector2 m = b + fromAngle(-PI / 2 + (i - 1) * 0.2f, 34 * s), tip = m + fromAngle(a + droop, 26 * s);
                DrawLineEx(b, m, 2.5f * s, rgba(0x2a4a2aff));
                DrawLineEx(m, tip, 2.5f * s, rgba(0x2a4a2aff));
                Color petal = mix(rgba(0xc81a30ff), rgba(0x3a1a20ff), clamp01(night_ / 5.0f));
                DrawEllipse((int)tip.x, (int)tip.y, 7 * s, 9 * s, petal);
            }
            if (night_ >= 2) DrawEllipse((int)(v.x + 16 * s), (int)(v.y - 2 * s), 5 * s, 2 * s, rgba(0x5a1a20ff));  // a fallen petal
        }
        // ceiling lamp (off)
        Vector2 lamp = hallPt(0, -1, 0.45f);
        float ls = hallScale(0.45f);
        DrawLineEx(lamp, {lamp.x, lamp.y + 40 * ls}, 2, rgba(0x0c0814ff));
        DrawCircleSector({lamp.x, lamp.y + 52 * ls}, 22 * ls, 180, 360, 16, rgba(0x2a2238ff));
        // moonlight streak on the floor
        fx::beginAdd();
        quad(hallPt(-0.2f, 1, 1), hallPt(0.2f, 1, 1), hallPt(0.5f, 1, 0.4f), hallPt(-0.5f, 1, 0.4f), alpha(rgba(0x6878d8ff), 0.12f), alpha(rgba(0x6878d8ff), 0));
        fx::endAdd();

        // the stalker
        const Hall& h = hall_[side];
        if ((h.stage == 1 || h.stage == 2) && !h.hidden) {
            float d = h.stage == 1 ? 0.88f : 0.42f;
            float u = (side == 0 ? 0.25f : -0.25f) * (h.stage == 1 ? 1 : 0.6f);
            Vector2 feet = hallPt(u, 1, d);
            stalker(side, feet, hallScale(d) * 1.45f, t_);
        }
        // a lure flying down the hall
        if (h.lureAnim >= 0) {
            float k = clamp01(h.lureAnim / 0.9f);
            float d = k * 0.85f;
            Vector2 p = hallPt(0, 0.3f + k * 0.7f, d);
            p.y -= std::sin(k * PI) * 160 * hallScale(d);
            art::itemIcon(side == 0 ? "stroopwafel" : "boba", p, 80 * hallScale(d));
        }
        E().endClip();

        // door panel (held shut)
        if (doorAnim_ > 0.01f) {
            float w = kOpening.width * doorAnim_;
            Rectangle d{side == 0 ? kOpening.x : kOpening.x + kOpening.width - w, kOpening.y, w, kOpening.height};
            DrawRectangleRec(d, rgba(0x5a3a28ff));
            DrawRectangleGradientH((int)d.x, (int)d.y, (int)d.width, (int)d.height, alpha(BLACK, side == 0 ? 0 : 0.4f), alpha(BLACK, side == 0 ? 0.4f : 0));
            if (doorAnim_ > 0.6f) {
                DrawRectangleLinesEx({d.x + 60, d.y + 40, d.width - 120, 260}, 4, alpha(BLACK, 0.3f));
                DrawRectangleLinesEx({d.x + 60, d.y + 340, d.width - 120, 300}, 4, alpha(BLACK, 0.3f));
                Rectangle sign{d.x + d.width / 2 - 170, d.y + 120, 340, 110};
                DrawRectangleRec(sign, rgba(0xe8e0d0ff));
                DrawRectangleLinesEx(sign, 4, col::red);
                ui::text("VERBODEN", sign.x + sign.width / 2, sign.y + 36, 38, col::red, ui::Center, true);
                ui::text("TOEGANG", sign.x + sign.width / 2, sign.y + 78, 30, col::outline, ui::Center, true);
                Vector2 knob{side == 0 ? d.x + d.width - 50 : d.x + 50, d.y + 420};
                DrawCircleV(knob, 16, col::goldDark);
                DrawCircleV({knob.x - 4, knob.y - 4}, 6, col::gold);
            }
            // the door shudders while someone pushes on it
            if (hall_[side].stage == 3) E().shake(1.5f);
        }
        // frame
        Color wood = rgba(0x4a2e1eff);
        DrawRectangleRec({kOpening.x - 30, kOpening.y - 26, 30, kOpening.height + 30}, wood);
        DrawRectangleRec({kOpening.x + kOpening.width, kOpening.y - 26, 30, kOpening.height + 30}, wood);
        DrawRectangleRec({kOpening.x - 30, kOpening.y - 30, kOpening.width + 60, 30}, wood);
        DrawRectangleLinesEx({kOpening.x - 30, kOpening.y - 30, kOpening.width + 60, kOpening.height + 60}, 3, col::outline);
        rlPopMatrix();

        bool light = lightOn();
        darkness(0.94f, light && doorAnim_ < 0.5f);
        // glowing eyes show faintly through the dark
        if ((h.stage == 1 || h.stage == 2) && !h.hidden && doorAnim_ < 0.5f) {
            float d = h.stage == 1 ? 0.88f : 0.42f;
            float u = (side == 0 ? 0.25f : -0.25f) * (h.stage == 1 ? 1 : 0.6f);
            float s = hallScale(d) * 1.45f;
            Vector2 head = hallPt(u, 1, d) + Vector2{pan.x, -400 * s};
            eyesOnly(side, head, 78 * s, light ? 0.3f : 0.55f, t_);
        }
    }

    // ============================================================ draw: closet
    void drawCloset() {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        rlPushMatrix();
        rlTranslatef(pan.x, 0, 0);
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x2a1a50ff));
        Rectangle c{250, 20, 780, 760};
        DrawRectangleRec({c.x - 20, c.y - 20, c.width + 40, 40}, rgba(0x4a2e1eff));
        DrawRectangleRec(c, rgba(0x05030aff));
        // inside: clothes rail with hoodies
        float gapTarget = horse_ >= 4 ? 640.0f : 110 + horse_ * 30.0f;
        float gap = lerpf(gapTarget, 0, doorAnim_);
        float shake = view_ == View::Closet && holding() && horse_ >= 2 ? std::sin(t_ * 60) * 4 : 0;
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
        if (horse_ == 1) art::horse({hc.x - 60, c.y + 700}, 0.9f, 0.3f, rgba(0x4a3020ff), rgba(0x1a1008ff), col::red, "none");
        if (horse_ == 3) nightmare(2, {hc.x + 10, hc.y + 40}, 190, -12 + std::sin(t_ * 2) * 3, 1, t_);
        if (horse_ >= 4) nightmare(2, {hc.x, hc.y + 20}, 300, std::sin(t_ * 3) * 4, 1, t_);
        E().endClip();
        // doors, opened by `gap`
        float dw = c.width / 2 - gap / 2;
        for (int k : {-1, 1}) {
            Rectangle d = k < 0 ? Rectangle{c.x + shake, c.y, dw, c.height} : Rectangle{c.x + c.width - dw - shake, c.y, dw, c.height};
            if (d.width < 2) continue;
            DrawRectangleRec(d, rgba(0x4a2e1eff));
            DrawRectangleGradientH((int)d.x, (int)d.y, (int)d.width, (int)d.height, alpha(BLACK, k < 0 ? 0.0f : 0.35f), alpha(BLACK, k < 0 ? 0.35f : 0.0f));
            DrawRectangleLinesEx(shrink({d.x, d.y + 30, d.width, 300}, 24), 4, alpha(BLACK, 0.35f));
            DrawRectangleLinesEx(shrink({d.x, d.y + 350, d.width, 340}, 24), 4, alpha(BLACK, 0.35f));
            DrawCircleV({k < 0 ? d.x + d.width - 30 : d.x + 30, d.y + 340}, 12, col::goldDark);
        }
        if (dw > 120) {  // stickers on the left door
            Rectangle st{c.x + 60, c.y + 120, 150, 60};
            DrawRectangleRounded(st, 0.3f, 8, rgba(0xe8e0d0ff));
            ui::text("NIET OPENEN", st.x + st.width / 2, st.y + 30, 20, col::red, ui::Center, true);
        }
        rlPopMatrix();
        bool light = lightOn();
        darkness(0.93f, light);
        if (horse_ == 2 && doorAnim_ < 0.5f) eyesOnly(2, {640 + pan.x - 30, 300}, 120, 0.9f, t_);
        if (horse_ >= 3 && doorAnim_ < 0.5f) eyesOnly(2, {640 + pan.x + 10, 370}, 190, 0.5f, t_);
    }

    // ============================================================ draw: bed
    void drawBed() {
        Vector2 pan{-(E().mouse().x - VW / 2.0f) * 0.03f, 0};
        rlPushMatrix();
        rlTranslatef(pan.x, 0, 0);
        DrawRectangle(-60, -20, VW + 120, VH + 40, rgba(0x1c1230ff));
        // headboard + pillow + blanket, seen from standing at the foot of the bed
        DrawRectangleRounded({160, 40, 960, 170}, 0.15f, 10, rgba(0x5b2fb0ff));
        for (int i = 0; i < 9; i++) DrawCircleV({220 + i * 105.0f, 110}, 6, darken(rgba(0x5b2fb0ff), 0.4f));
        DrawRectangleRounded({300, 170, 300, 120}, 0.6f, 10, rgba(0xf4f0ffff));
        DrawRectangleRounded({660, 175, 300, 115}, 0.6f, 10, col::pink);
        quad({120, 260}, {1160, 260}, {1280, 760}, {0, 760}, rgba(0x3a7bd5ff), rgba(0x2a5aa5ff));
        for (int i = 0; i < 8; i++) {
            float u = (i + 0.5f) / 8;
            DrawLineEx({lerpf(120, 1160, u), 260}, {lerpf(0, 1280, u), 760}, 10, alpha(WHITE, 0.15f));
        }
        DrawRectangleRounded({200, 520, 880, 60}, 0.6f, 10, alpha(BLACK, 0.12f));  // fold
        // paardjes
        for (int i = 0; i < paardjes_; i++) {
            Vector2 p = bedSpot(i);
            float twitch = std::sin(t_ * 9 + i * 2) * (bedFlash_ > 0 ? 6 : 1);
            fx::softShadow({p.x + 20, p.y + 6}, 90, 16, 0.5f);
            art::horse({p.x + twitch, p.y}, 1.25f, 0.2f + i, rgba(0x6a4a30ff), rgba(0x1a1008ff), i == 1 ? col::red : col::violet, "none");
        }
        rlPopMatrix();
        bool light = lightOn();
        darkness(0.9f, light);
        for (int i = 0; i < paardjes_; i++) {
            Vector2 p = bedSpot(i) + Vector2{pan.x, 0};
            Vector2 eye{p.x + 82 * 1.25f, p.y - 124 * 1.25f};
            fx::beginAdd();
            fx::glow(eye, 18, alpha(rgba(0xff9020ff), 0.8f));
            fx::glow(eye, 5, alpha(WHITE, 0.9f));
            fx::endAdd();
        }
    }

    // ============================================================ draw: phases
    void drawPlayView() {
        switch (view_) {
            case View::Center: drawCenter(); break;
            case View::Left: drawHall(0); break;
            case View::Right: drawHall(1); break;
            case View::Closet: drawCloset(); break;
            case View::Bed: drawBed(); break;
        }
        if (flashScare_ > 0) DrawRectangle(0, 0, VW, VH, alpha(WHITE, 0.15f * flashScare_));
        if (subT_ > 0) {  // a face, for two frames
            DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f));
            nightmare(subFace_, {VW / 2.0f + rnd(-40, 40), VH / 2.0f + 20}, 330, rnd(-10, 10), 1, t_);
            staticScreen(0.3f);
        }
        fx_.draw();
        // running between spots: fade through black
        if (moveT_ > 0) DrawRectangle(0, 0, VW, VH, alpha(BLACK, 1 - std::abs(moveT_ - 0.15f) / 0.15f));
        grain(0.035f);
        if (staticT_ > 0) staticScreen(staticT_ * 2);
        drawHud();
        drawCaptions();
    }

    void drawTitle() {
        // the room, very dark, with them watching
        drawCenter();
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.55f));
        float a1 = 0.5f + 0.5f * std::sin(t_ * 0.7f), a2 = 0.5f + 0.5f * std::sin(t_ * 0.6f + 2);
        float glitch = std::fmod(t_, 3.7f) < 0.08f ? 1.0f : 0.0f;
        nightmare(0, {1040 + glitch * 20, 330}, 210, -8 + std::sin(t_) * 3, 0.8f * a1, t_);
        DrawRectangle(820, 0, 460, VH, alpha(BLACK, 1 - a1 * 0.75f));
        nightmare(1, {1180, 560}, 150, 12, 0.8f * a2, t_);
        DrawRectangleGradientH(780, 0, 200, VH, alpha(BLACK, 0.95f), alpha(BLACK, 0));
        // title
        float flick = std::fmod(t_, 4.3f) < 0.1f ? 0.4f : 1.0f;
        fx::gradientText("FIVE NIGHTS", 330, 110, 70, alpha(rgba(0xffd08aff), flick), alpha(rgba(0xff5a1aff), flick), 5);
        fx::gradientText("AT PIETER'S", 330, 180, 70, alpha(rgba(0xffd08aff), flick), alpha(rgba(0xc81a1aff), flick), 5);
        ui::text("HALLOWEEN SPECIAL", 330, 232, 20, col::orange, ui::Center, true);
        jackOLantern({90, 640}, 46, t_, 1);
        jackOLantern({190, 668}, 30, t_ + 3, 1);
        // night progress: a pumpkin per night survived
        for (int i = 0; i < 5; i++) {
            bool beat = P().fnapBeaten & (1 << i);
            Vector2 p{230.0f + i * 50, 270};
            if (beat) jackOLantern(p, 15, t_ + i, 1);
            else DrawCircleV(p, 14, alpha(rgba(0x3a2a20ff), 0.8f));
        }
        if (howTo_) { drawHowTo(); return; }
        float y = 330;
        if (ui::button({180, y, 300, 58}, "NEW GAME", rgba(0x8a2a10ff), 26)) { startNight(0); E().sfx.play(Sfx::Static, 0.5f); }
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
        Rectangle r{60, 290, 700, 410};
        ui::panel(r, alpha(rgba(0x0c0814ff), 0.95f), 0.06f, col::orange);
        ui::text("HOW TO SURVIVE UNTIL 6 AM", r.x + 24, r.y + 30, 22, col::orange, ui::Left, true);
        struct L { int face; const char* who; const char* how; } lines[] = {
            {0, "DAVI  (left door)", "Light the hall to chase him off. Hear BREATHING at the door? Hold SPACE until he leaves. Never shine the light."},
            {1, "KIRA  (right door)", "Same, but she GIGGLES, can hide from the light, and hacks the PC: turn it off (E) or everyone gets faster."},
            {2, "HET NACHTPAARD  (closet)", "He grows while you look away. Hold the closet shut (SPACE) to push him back. Don't look when he's out."},
            {-1, "PAARDJES  (bed)", "Little plush horses gather on the bed. Shine your light on them. Three at once is game over."},
            {-2, "THE MONKEY MAN  (window)", "Tik tik on the glass? Don't stare at him. Look away, or he wakes up the whole house."},
        };
        for (int i = 0; i < 5; i++) {
            float y = r.y + 76 + i * 60;
            if (lines[i].face >= 0) nightmare(lines[i].face, {r.x + 50, y + 8}, 26, 0, 0.8f, t_);
            else if (lines[i].face == -2) monkeyMan({r.x + 50, y + 10}, 22, t_, 0);
            else art::horse({r.x + 30, y + 30}, 0.3f, 0, rgba(0x6a4a30ff), rgba(0x1a1008ff), col::violet, "none");
            ui::text(lines[i].who, r.x + 92, y - 8, 17, col::text, ui::Left, true);
            ui::paragraph(lines[i].how, {r.x + 92, y + 6, r.width - 120, 0}, 15, col::muted);
        }
        ui::text("Q at a door throws a stroopwafel (Davi) or boba (Kira) from your inventory: they leave and stay busy for a while.", r.x + 24,
                 r.y + r.height - 24, 13, col::gold, ui::Left, true);
        if (ui::button({r.x + r.width - 120, r.y + 12, 100, 36}, "OK", col::panelHi, 18)) howTo_ = false;
        grain(0.04f);
    }

    void drawIntro() {
        DrawRectangle(0, 0, VW, VH, BLACK);
        float a = clamp01(introT_ / 0.8f) * (introNote_ ? 1 : clamp01((3.6f - introT_) / 0.6f));
        ui::textShadow("12:00 AM", VW / 2.0f, introNote_ ? 120.0f : 300.0f, 64, alpha(WHITE, a), ui::Center, true, 3);
        ui::text("NIGHT " + std::to_string(night_ + 1), VW / 2.0f, introNote_ ? 176.0f : 360.0f, 28, alpha(col::orange, a), ui::Center, true);
        ui::text(kNightTag[night_], VW / 2.0f, introNote_ ? 208.0f : 396.0f, 18, alpha(col::muted, a), ui::Center, true);
        if (introNote_) {
            // Pieter's note, taped to the screen
            Rectangle n{VW / 2.0f - 300, 250, 600, 400};
            rlPushMatrix();
            rlTranslatef(VW / 2.0f, 450, 0);
            rlRotatef(-2, 0, 0, 1);
            rlTranslatef(-VW / 2.0f, -450, 0);
            DrawRectangleRec({n.x + 8, n.y + 10, n.width, n.height}, alpha(BLACK, 0.5f * a));
            DrawRectangleRec(n, alpha(rgba(0xfff3a0ff), a));
            DrawRectangleRec({n.x + n.width / 2 - 60, n.y - 14, 120, 28}, alpha(rgba(0xd8d0c0ff), 0.8f * a));
            Color ink = alpha(rgba(0x2a2040ff), a);
            ui::text("Note to self (Pieter)", n.x + 30, n.y + 40, 24, ink, ui::Left, true);
            const char* rules[] = {"Davi + Kira come down the halls at night. A/D = run to a door.",
                                   "F / left mouse = flashlight. Light them in the hall = they run.",
                                   "BREATHING or GIGGLING at the door? HOLD SPACE. NO LIGHT!!",
                                   "W = closet. If the horse is in there, hold it shut.",
                                   "S = bed. Light the paardjes away before there are 3.",
                                   "If the PC starts playing music: go to the room, press E.",
                                   "Monkey Man at the window?? DON'T LOOK. Mama says doe normaal.",
                                   "Survive till 6 AM. Ik ben niet bang. (ik ben bang)"};
            for (int i = 0; i < 8; i++) ui::paragraph(rules[i], {n.x + 30, n.y + 78 + i * 38.0f, n.width - 60, 0}, 18, ink);
            rlPopMatrix();
            if (introT_ > 1.5f) ui::text("click or SPACE to begin", VW / 2.0f, 690, 18, alpha(col::text, 0.5f + 0.5f * std::sin(t_ * 4)), ui::Center, true);
        }
        grain(0.05f);
    }

    void drawScare() {
        float k = scareT_;
        DrawRectangle(0, 0, VW, VH, std::fmod(k, 0.12f) < 0.06f ? rgba(0x200004ff) : BLACK);
        Vector2 off{rnd(-30, 30), rnd(-30, 30)};
        float grow = easeOutCubic(clamp01(k / 0.2f));
        float lunge = easeOutCubic(clamp01((k - 0.7f) / 0.15f));  // and a second lunge, right into your face
        float r = lerpf(120, 520, grow) + lunge * 260 + std::sin(k * 50) * 10;
        fx::beginAdd();
        fx::glow({VW / 2.0f, VH / 2.0f}, 700, alpha(col::red, 0.4f));
        fx::endAdd();
        if (scareWho_ == Who::Paardjes) {
            for (int i = 0; i < 3; i++)
                art::horse({VW / 2.0f + (i - 1) * 380 * grow + off.x, VH / 2.0f + 300 + off.y}, 1.2f + grow * 1.5f, k * 20 + i, rgba(0x6a4a30ff),
                           rgba(0x1a1008ff), col::red, "none");
            nightmare(2, Vector2{VW / 2.0f, VH / 2.0f - 60} + off, r * 0.8f, std::sin(k * 40) * 8, 1, t_);
        } else {
            int f = scareWho_ == Who::Davi ? 0 : scareWho_ == Who::Kira ? 1 : 2;
            nightmare(f, Vector2{VW / 2.0f, VH / 2.0f + 40} + off, r, std::sin(k * 40) * 10, 1, t_);
        }
        // the picture tears
        for (int i = 0; i < 5; i++) {
            float y = hash(std::floor(k * 20) + i) * VH, h = 6 + hash(i * 3.0f + std::floor(k * 20)) * 30;
            DrawRectangle(0, (int)y, VW, (int)h, alpha(i % 2 ? rgba(0xff2030ff) : BLACK, 0.35f));
        }
        if (k < 0.05f || (k > 0.7f && k < 0.74f)) DrawRectangle(0, 0, VW, VH, alpha(WHITE, 0.7f));
        staticScreen(0.25f);
        grain(0.15f);
    }

    void drawGameOver() {
        DrawRectangle(0, 0, VW, VH, BLACK);
        int f = scareWho_ == Who::Davi ? 0 : scareWho_ == Who::Kira ? 1 : 2;
        nightmare(f, {VW / 2.0f, 250}, 190, 0, 0.6f, t_);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f));
        DrawRectangleGradientV(0, 400, VW, 100, alpha(BLACK, 0), alpha(BLACK, 0.85f));
        DrawRectangle(0, 500, VW, VH - 500, alpha(BLACK, 0.85f));
        staticScreen(std::max(0.12f, 1 - overT_ * 1.5f));
        float a = clamp01((overT_ - 0.5f) / 0.5f);
        float jitter = std::fmod(t_, 2.1f) < 0.06f ? 8.0f : 0.0f;
        fx::gradientText("GAME OVER", VW / 2.0f + jitter, 110, 90, alpha(rgba(0xff6a6aff), a), alpha(rgba(0x8a0a1aff), a), 6);
        const char* who = f == 0 ? (monkeyBang_ > 0 || night_ % 2 ? "Davi got you. The Monkey Men saw everything." : "Davi got you.") : f == 1 ? "Kira got you. HIHIHI." : scareWho_ == Who::Paardjes ? "The paardjes called the big horse." : "Het Nachtpaard came out of the closet.";
        const char* tip = scareWho_ == Who::Davi    ? "Tip: breathing at the door means HOLD SPACE. The light makes it worse."
                          : scareWho_ == Who::Kira  ? "Tip: giggling at the door = hold it shut. And switch off the PC fast."
                          : scareWho_ == Who::Horse ? "Tip: check the closet often and hold it shut until it's quiet."
                                                    : "Tip: look at the bed now and then and light up every paardje.";
        ui::text(who, VW / 2.0f, 470, 26, alpha(col::text, a), ui::Center, true);
        ui::text(tip, VW / 2.0f, 506, 17, alpha(col::muted, a), ui::Center, true);
        ui::text(std::string("Night ") + std::to_string(night_ + 1) + "  -  " + clockText(), VW / 2.0f, 536, 16, alpha(col::orange, a), ui::Center, true);
        if (overT_ > 1.0f) {
            if (ui::button({VW / 2.0f - 260, 580, 250, 60}, "TRY AGAIN", rgba(0x8a2a10ff), 24)) startNight(night_);
            if (ui::button({VW / 2.0f + 10, 580, 250, 60}, "GIVE UP", rgba(0x2a2238ff), 24)) phase_ = Phase::Title;
        }
        grain(0.05f);
    }

    void drawWin() {
        DrawRectangle(0, 0, VW, VH, BLACK);
        // the clock rolls from 5 to 6
        float roll = easeInOutSine(clamp01((winT_ - 0.4f) / 1.2f));
        E().beginClip({VW / 2.0f - 200, 200, 400, 140});
        ui::textShadow("5", VW / 2.0f - 60, 270 - roll * 140, 120, WHITE, ui::Center, true, 4);
        ui::textShadow("6", VW / 2.0f - 60, 410 - roll * 140, 120, WHITE, ui::Center, true, 4);
        E().endClip();
        ui::textShadow("AM", VW / 2.0f + 70, 270, 90, WHITE, ui::Center, true, 4);
        if (winT_ > 3.6f) {
            float a = clamp01((winT_ - 3.6f) / 0.6f);
            fx::beginAdd();
            fx::glow({VW / 2.0f, 440}, 400, alpha(col::gold, 0.25f * a));
            fx::endAdd();
            fx::gradientText("GOEDEMORGEN!", VW / 2.0f, 430, 60, alpha(rgba(0xfff3a0ff), a), alpha(col::orange, a), 5);
            ui::text("Night " + std::to_string(night_ + 1) + " survived", VW / 2.0f, 484, 22, alpha(col::text, a), ui::Center, true);
            ui::coin({VW / 2.0f - 90, 524}, 14);
            ui::text("+" + fmtNum(reward_) + " Pietercoins", VW / 2.0f - 68, 524, 22, alpha(col::gold, a), ui::Left, true);
            if (night_ == 4) ui::text("...but the horse wants one more night. NIGHT 6 unlocked.", VW / 2.0f, 562, 17, alpha(col::red, a), ui::Center, true);
            if (night_ == 5) ui::text("You beat JUHUH NIGHT. Pieter is officially not scared. (he is)", VW / 2.0f, 562, 17, alpha(col::gold, a), ui::Center, true);
            if (winT_ > 4.2f) {
                bool more = night_ < 4 || (night_ == 4 && P().fnapNight >= 6);
                if (more && ui::button({VW / 2.0f - 260, 600, 250, 60}, "NEXT NIGHT", rgba(0x8a2a10ff), 24)) startNight(std::min(5, night_ + 1));
                if (ui::button({more ? VW / 2.0f + 10 : VW / 2.0f - 125, 600, 250, 60}, "TITLE", rgba(0x2a2238ff), 24)) phase_ = Phase::Title;
            }
        }
        fx_.draw();
        grain(0.06f);
    }

    void draw() override {
        switch (phase_) {
            case Phase::Title: drawTitle(); break;
            case Phase::Intro: drawIntro(); break;
            case Phase::Play: drawPlayView(); break;
            case Phase::Scare: drawScare(); break;
            case Phase::GameOver: drawGameOver(); break;
            case Phase::Win: drawWin(); break;
        }
    }

private:
    struct Caption { std::string text; Color color; float t; };
    Phase phase_ = Phase::Title;
    View view_ = View::Center, pendingView_ = View::Center;
    int night_ = 0;
    float t_ = 0, nightT_ = 0, moveT_ = 0, introT_ = 0, scareT_ = 0, overT_ = 0, winT_ = 0, staticT_ = 0, musicT_ = 0, droneT_ = 0;
    bool introNote_ = false, howTo_ = false;
    Hall hall_[2];
    int horse_ = 0, paardjes_ = 0, bellsRung_ = 0;
    float horseT_ = 0, horseLinger_ = 0, horseHold_ = 0, scareDelay_ = 0, paardT_ = 0, bedT_ = 0, bedFlash_ = 0;
    bool pcOn_ = false;
    float pcT_ = 0, pcSongT_ = 0;
    float doorAnim_ = 0, flicker_ = 0, heartT_ = 0, heartPulse_ = 0, flashScare_ = 0;
    bool wasLight_ = false, wasHold_ = false;
    float rainT_ = 0, windT_ = 4, thunderT_ = 12, boltT_ = -1, thunderDelay_ = -1, whisperT_ = 20, knockT_ = 40, subT_ = 0, hourPulse_ = 0, klompEcho_ = 0;
    int lastHour_ = 0, subFace_ = 0;
    bool monkeyOn_ = false, riser_ = false, devBolt_ = false;
    float monkeyNext_ = 50, monkeyT_ = 0, monkeyStare_ = 0, monkeyRise_ = 0, monkeyBang_ = 0, clashAnim_ = 0;
    bool devLight_ = false, devHold_ = false, devFreeze_ = false;
    Who scareWho_ = Who::Davi;
    long long reward_ = 0;
    std::vector<Caption> captions_;
    Texture2D mask_{}, noise_{};
    Vector2 doorEyePos_[2]{}, doorQuad_[2][4]{};
    RenderTexture2D lm_{};
    room::Env env_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeFnap() { return std::make_unique<FnapScene>(); }

}  // namespace pg
