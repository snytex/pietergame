#include "game/fnap/Art.hpp"

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"
#include "game/Art.hpp"

#include <rlgl.h>

#include <tuple>

#include <algorithm>
#include <string>

namespace pg::fnap::gfx {

// ====================================================================== primitives

float hash(float n) { return std::fmod(std::abs(std::sin(n * 12.9898f) * 43758.5453f), 1.0f); }

void tri(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
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
    tri(tl, tr, br, top, top, bottom);
    tri(tl, br, bl, top, bottom, bottom);
}
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c) { quad(tl, tr, br, bl, c, c); }
void limb(Vector2 a, Vector2 b, float w, Color c) {
    DrawLineEx(a, b, w, c);
    DrawCircleV(a, w / 2, c);
    DrawCircleV(b, w / 2, c);
}

// a limb with a round, lit-from-the-front look: dark edge, body, a soft highlight
static void slimb(Vector2 a, Vector2 b, float w, Color c) {
    limb(a, b, w, darken(c, 0.45f));
    Vector2 n = Vector2Normalize({-(b.y - a.y), b.x - a.x});
    if (n.x > 0) n = n * -1.0f;
    limb(a + n * (w * 0.08f), b + n * (w * 0.08f), w * 0.72f, c);
    limb(a + n * (w * 0.2f), b + n * (w * 0.2f), w * 0.22f, lighten(c, 0.1f));
}

// rotated filled ellipse
static void ellipse(Vector2 c, float rx, float ry, float rot, Color col) {
    const int n = 28;
    Vector2 prev{};
    for (int i = 0; i <= n; i++) {
        float a = i * 2 * PI / n;
        Vector2 p{std::cos(a) * rx, std::sin(a) * ry};
        p = {c.x + p.x * std::cos(rot) - p.y * std::sin(rot), c.y + p.x * std::sin(rot) + p.y * std::cos(rot)};
        if (i > 0) tri(c, prev, p, col, col, col);
        prev = p;
    }
}

// ====================================================================== lighting

namespace {

const char* kLightFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uLight;
uniform float uRadius;
uniform float uPower;
uniform float uDark;
uniform float uTime;
uniform float uAspect;
uniform vec4 uPts[6];
uniform int uCount;
uniform int uMode;
uniform vec3 uTint;
uniform vec2 uHand;
out vec4 finalColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
// a real flashlight: hot core, the reflector's bright ring, the bulb's shadow in the middle, a dim spill
float beam(vec2 p) {
    vec2 d = p - uLight;
    float r = length(d) / uRadius;
    float core = pow(1.0 - smoothstep(0.0, 1.0, r), 0.75);
    float ring = exp(-pow((r - 0.64) / 0.08, 2.0)) * 0.22;
    float hole = 1.0 - 0.16 * exp(-pow(r / 0.13, 2.0));
    float spill = (1.0 - smoothstep(0.7, 2.1, r)) * 0.06;
    float lens = 0.86 + 0.28 * noise(d / uRadius * 7.0 + 3.1);
    return clamp((core * hole + ring) * lens + spill, 0.0, 1.25) * uPower;
}
void main() {
    vec2 uv = fragTexCoord;
    vec2 p = vec2(uv.x * uAspect, uv.y);  // aspect-correct, height units
    float L = beam(p);
    for (int i = 0; i < 6; i++) {
        if (i >= uCount) break;
        float r = length(p - uPts[i].xy) / uPts[i].z;
        L += (1.0 - smoothstep(0.0, 1.0, r)) * uPts[i].w;
    }
    if (uMode == 0) {
        float a = uDark * (1.0 - clamp(L, 0.0, 1.0));
        a += (hash(uv * 911.0 + fract(uTime)) - 0.5) * 0.012;  // dither: no banding in the dark
        finalColor = vec4(0.004, 0.004, 0.016, clamp(a, 0.0, 1.0));
    } else {
        // the shaft of light from the torch in your hand, with dust drifting through it
        vec2 ax = uLight - uHand;
        float t = clamp(dot(p - uHand, ax) / dot(ax, ax), 0.0, 1.0);
        float dist = length(p - (uHand + ax * t));
        float width = mix(0.015, uRadius * 0.85, t);
        float dust = 0.55 + 0.45 * noise(p * 14.0 + vec2(uTime * 0.11, -uTime * 0.07)) * noise(p * 5.0 - uTime * 0.05);
        float shaft = (1.0 - smoothstep(0.0, width, dist)) * pow(t, 0.6) * 0.07 * dust * uPower;
        float warm = beam(p) * 0.06;
        finalColor = vec4(uTint * (shaft + warm), 1.0);
    }
}
)";

Shader gShader{};
Texture2D gWhite{};
int gLoc[13]{};

void drawFull(const Light& l, float t, int mode) {
    if (!gShader.id) return;
    float aspect = (float)VW / VH;
    Vector2 lp{l.pos.x / VH, l.pos.y / VH}, hp{l.hand.x / VH, l.hand.y / VH};
    float radius = l.radius / VH, power = l.power, dark = l.dark;
    float tint[3] = {l.tint.r / 255.0f, l.tint.g / 255.0f, l.tint.b / 255.0f};
    float pts[24]{};
    for (int i = 0; i < l.count; i++) {
        pts[i * 4] = l.pts[i].pos.x / VH;
        pts[i * 4 + 1] = l.pts[i].pos.y / VH;
        pts[i * 4 + 2] = l.pts[i].radius / VH;
        pts[i * 4 + 3] = l.pts[i].strength;
    }
    SetShaderValue(gShader, gLoc[0], &lp, SHADER_UNIFORM_VEC2);
    SetShaderValue(gShader, gLoc[1], &radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gShader, gLoc[2], &power, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gShader, gLoc[3], &dark, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gShader, gLoc[4], &t, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gShader, gLoc[5], &aspect, SHADER_UNIFORM_FLOAT);
    SetShaderValueV(gShader, gLoc[6], pts, SHADER_UNIFORM_VEC4, 6);
    SetShaderValue(gShader, gLoc[7], &l.count, SHADER_UNIFORM_INT);
    SetShaderValue(gShader, gLoc[8], &mode, SHADER_UNIFORM_INT);
    SetShaderValue(gShader, gLoc[9], tint, SHADER_UNIFORM_VEC3);
    SetShaderValue(gShader, gLoc[10], &hp, SHADER_UNIFORM_VEC2);
    BeginShaderMode(gShader);
    DrawTexturePro(gWhite, {0, 0, 1, 1}, {-2, -2, VW + 4.0f, VH + 4.0f}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

}  // namespace

void initLight() {
    if (gShader.id) return;
    gShader = LoadShaderFromMemory(nullptr, kLightFs);
    const char* names[] = {"uLight", "uRadius", "uPower", "uDark", "uTime", "uAspect", "uPts", "uCount", "uMode", "uTint", "uHand"};
    for (int i = 0; i < 11; i++) gLoc[i] = GetShaderLocation(gShader, names[i]);
    Image img = GenImageColor(1, 1, WHITE);
    gWhite = LoadTextureFromImage(img);
    UnloadImage(img);
}

void shutdownLight() {
    if (gShader.id) UnloadShader(gShader);
    if (gWhite.id) UnloadTexture(gWhite);
    gShader = {};
    gWhite = {};
}

void darkness(const Light& l, float t) { drawFull(l, t, 0); }

void haze(const Light& l, float t) {
    if (l.power <= 0.01f) return;
    BeginBlendMode(BLEND_ADDITIVE);
    drawFull(l, t, 1);
    EndBlendMode();
    // dust motes catching the beam
    fx::beginAdd();
    for (int i = 0; i < 70; i++) {
        float sp = 6 + hash(i * 1.3f) * 14;
        Vector2 p{std::fmod(hash(i + 0.1f) * VW + std::sin(t * 0.3f + i) * 30 + t * sp * (hash(i * 2.0f) - 0.5f), (float)VW),
                  std::fmod(hash(i + 0.7f) * VH + t * sp * 0.6f, (float)VH)};
        float d = Vector2Distance(p, l.pos) / l.radius;
        if (d > 1) continue;
        float a = (1 - d) * (0.25f + 0.25f * std::sin(t * 2 + i)) * l.power;
        fx::glow(p, 2.5f + hash(i * 3.0f) * 2.5f, alpha(l.tint, a));
    }
    fx::endAdd();
}

// ====================================================================== the cast

namespace {
struct Face { const char* tex; Vector2 eye[2]; int eyes; Vector2 mouth; float mouthW; Color glow; };
const Face kFaces[3] = {
    {"davi", {{-0.42f, 0.125f}, {0.113f, 0.152f}}, 2, {-0.14f, 0.6f}, 0.55f, rgba(0xff3030ff)},
    {"kira", {{-0.63f, -0.08f}, {0.066f, -0.277f}}, 2, {-0.19f, 0.36f}, 0.5f, rgba(0xff40d0ff)},
    {"horse", {{0.33f, -0.2f}, {0, 0}}, 1, {-0.38f, 0.66f}, 0.5f, rgba(0xff9020ff)},
};
}  // namespace

void nightmare(int f, Vector2 c, float r, float rot, float eyeGlow, float t, float jaw) {
    const Face& fc = kFaces[f];
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(rot, 0, 0, 1);
    art::portrait(fc.tex, {0, 0}, r, BLANK, 0, false, rgba(0x8a6a78ff));
    DrawCircleGradient({0, 0}, r, alpha(BLACK, 0.05f), alpha(BLACK, 0.85f));
    // veins and cracks
    for (int i = 0; i < 5; i++) {
        Vector2 p{(hash(i + f * 7.0f) - 0.5f) * r * 1.2f, (hash(i * 3.1f + f) - 0.5f) * r * 1.2f};
        for (int k = 0; k < 4; k++) {
            Vector2 q = p + Vector2{(hash(i * 9.0f + k) - 0.5f) * r * 0.3f, (hash(i * 5.0f + k + 1) - 0.2f) * r * 0.25f};
            DrawLineEx(p, q, std::max(1.5f, r * 0.012f), alpha(i % 2 ? rgba(0x3a0008ff) : BLACK, 0.7f));
            p = q;
        }
    }
    // mouth: a black hole, wet gums, jagged teeth. jaw opens it.
    Vector2 m = fc.mouth * r;
    float mw = fc.mouthW * r * (1 + jaw * 0.25f), mh = r * (0.16f + jaw * 0.3f);
    m.y += jaw * r * 0.12f;
    DrawEllipse((int)m.x, (int)m.y, mw * 1.08f, mh * 1.15f, rgba(0x3a0610ff));
    DrawEllipse((int)m.x, (int)m.y, mw, mh, rgba(0x0a0204ff));
    DrawEllipse((int)m.x, (int)(m.y + mh * 0.35f), mw * 0.6f, mh * 0.45f, rgba(0x6a0a18ff));  // tongue
    int teeth = 10;
    for (int i = 0; i < teeth; i++) {
        float u0 = (float)i / teeth, u1 = (float)(i + 1) / teeth;
        float x0 = m.x - mw * 0.92f + u0 * mw * 1.84f, x1 = m.x - mw * 0.92f + u1 * mw * 1.84f;
        float edge = std::sqrt(std::max(0.0f, 1 - std::pow(((x0 + x1) / 2 - m.x) / mw, 2.0f)));
        float ty = m.y - mh * edge, by = m.y + mh * edge;
        float len = (0.55f + 0.4f * hash(i + f * 13.0f)) * std::min(mh, r * 0.2f);
        Color tooth = rgba(0xe8dcc0ff), root = rgba(0x9a8a70ff);
        tri({x0, ty}, {x1, ty}, {(x0 + x1) / 2, ty + len * 1.3f * edge}, root, root, tooth);
        tri({x0, by}, {x1, by}, {(x0 + x1) / 2, by - len * edge}, root, root, tooth);
    }
    // spit strings when the jaw is open
    if (jaw > 0.3f)
        for (int i = 0; i < 3; i++) {
            float x = m.x + (i - 1) * mw * 0.5f;
            DrawLineEx({x, m.y - mh * 0.7f}, {x + std::sin(t * 6 + i) * 4, m.y + mh * 0.7f}, std::max(1.0f, r * 0.008f), alpha(rgba(0xd8c8d0ff), 0.5f));
        }
    // eyes: hollow sockets with burning pupils
    for (int i = 0; i < fc.eyes; i++) {
        Vector2 e = fc.eye[i] * r;
        DrawCircleV(e, r * 0.14f, rgba(0x060204ff));
        DrawCircleGradient(e, r * 0.22f, alpha(BLACK, 0.85f), alpha(BLACK, 0));
    }
    fx::beginAdd();
    for (int i = 0; i < fc.eyes; i++) {
        Vector2 e = fc.eye[i] * r + Vector2{std::sin(t * 23 + i) * r * 0.01f, 0};
        fx::glow(e, r * 0.38f, alpha(fc.glow, 0.7f * eyeGlow));
        fx::glow(e, r * 0.11f, alpha(WHITE, 0.9f * eyeGlow));
    }
    fx::endAdd();
    rlPopMatrix();
}

void eyes(int f, Vector2 c, float r, float strength, float t) {
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

Vector2 stalkerHead(Vector2 feet, float s) { return {feet.x, feet.y - 548 * s}; }

void stalker(int who, Vector2 feet, float s, float t, float flinch, float reach) {
    const bool kira = who == 1;
    auto P = [&](float x, float y) { return Vector2{feet.x + x * s, feet.y + y * s}; };
    float breathe = std::sin(t * (kira ? 2.3f : 1.6f) + who);
    float sway = std::sin(t * 0.9f + who * 2) * 5;
    Color cloth = kira ? rgba(0x5a2244ff) : rgba(0x4e4234ff);
    Color dark = darken(cloth, 0.55f), light = lighten(cloth, 0.1f);
    Color skin = kira ? rgba(0x8e7e7cff) : rgba(0x6e5648ff);  // grey, wrong
    Color pants = kira ? rgba(0x141016ff) : rgba(0x1e2230ff);
    Color nail = rgba(0x1a100cff);

    // legs: knees in, a little bent
    for (int k : {-1, 1}) {
        Vector2 hip = P(k * 30 + sway * 0.3f, -250), knee = P(k * 22, -128), ankle = P(k * 34, -16);
        slimb(hip, knee, 36 * s, pants);
        slimb(knee, ankle, 28 * s, pants);
        if (kira) {  // bare, dirty feet
            DrawEllipse((int)(ankle.x + k * 6 * s), (int)(ankle.y + 8 * s), 20 * s, 9 * s, darken(skin, 0.15f));
            for (int toe = 0; toe < 4; toe++) DrawCircleV({ankle.x + k * (14 + toe * 5) * s, ankle.y + 12 * s}, 3.2f * s, darken(skin, 0.3f));
            DrawEllipse((int)(ankle.x + k * 6 * s), (int)(ankle.y + 12 * s), 18 * s, 4 * s, alpha(rgba(0x1a0a08ff), 0.7f));
        } else {  // his klompen
            Vector2 c{ankle.x + k * 12 * s, ankle.y + 6 * s};
            DrawEllipse((int)c.x, (int)(c.y + 10 * s), 34 * s, 7 * s, alpha(BLACK, 0.5f));
            DrawEllipse((int)c.x, (int)c.y, 34 * s, 15 * s, rgba(0xb89028ff));
            DrawEllipse((int)(c.x + k * 14 * s), (int)(c.y - 4 * s), 13 * s, 7 * s, rgba(0xd0aa40ff));
            DrawEllipse((int)(c.x - k * 8 * s), (int)(c.y - 8 * s), 16 * s, 6 * s, rgba(0x1a1008ff));
            DrawCircleV({c.x + k * 18 * s, c.y}, 4 * s, rgba(0x8a1820ff));  // a painted tulip
        }
    }
    // torso: a long, narrow, hunched body in a filthy hoodie
    float sh = -455 + breathe * 3;
    Vector2 sl = P(-78 + sway, sh), sr = P(78 + sway, sh), wl = P(-52, -250), wr = P(52, -250);
    quad(sl, sr, wr, wl, light, cloth);
    quad(sl, P(-30 + sway, sh), P(-22, -250), wl, alpha(BLACK, 0.55f), alpha(BLACK, 0.45f));
    quad(P(34 + sway, sh), sr, wr, P(26, -250), alpha(BLACK, 0.4f), alpha(BLACK, 0.55f));
    quad(sl, sr, P(60 + sway, sh + 40), P(-60 + sway, sh + 40), alpha(BLACK, 0.35f), alpha(BLACK, 0));  // under the chin
    for (int i = 0; i < 4; i++) {  // folds
        Vector2 a = P(-40 + i * 26 + sway * 0.8f, sh + 50), b = P(-34 + i * 22, -270);
        DrawLineEx(a, b, 3 * s, alpha(dark, 0.6f));
    }
    for (int i = 0; i < 6; i++) {  // grime
        Vector2 g = P(-50 + 100 * hash(i * 2.3f + who) + sway * 0.6f, -430 + 160 * hash(i * 4.1f + who));
        DrawEllipse((int)g.x, (int)g.y, (8 + 10 * hash(i * 1.1f)) * s, (5 + 6 * hash(i * 3.3f)) * s, alpha(rgba(0x140c08ff), 0.35f));
    }
    for (int i = 0; i < 7; i++) {  // ragged hem
        float u0 = i / 7.0f, u1 = (i + 1) / 7.0f;
        Vector2 a = Vector2Lerp(wl, wr, u0), b = Vector2Lerp(wl, wr, u1);
        Vector2 tip = Vector2Lerp(a, b, 0.5f) + Vector2{0, (16 + 22 * hash(i + who * 9.0f)) * s};
        tri(a, b, tip, cloth, cloth, dark);
    }
    if (!kira) {  // kangaroo pocket + drawstrings
        DrawRectangleRounded({feet.x - 40 * s, feet.y - 318 * s, 80 * s, 46 * s}, 0.4f, 6, alpha(dark, 0.8f));
        for (int k : {-1, 1}) limb(P(k * 14 + sway, sh + 20), P(k * 16 + sway * 0.6f, sh + 90), 2.5f * s, rgba(0x8a8070ff));
    } else {  // a little boba-pearl necklace
        for (int i = 0; i < 9; i++) {
            float a = PI * (0.15f + 0.7f * i / 8.0f);
            DrawCircleV(P(sway + std::cos(a) * 34, sh + 6 + std::sin(a) * 26), 4 * s, rgba(0x0a0606ff));
        }
    }
    for (int i = 0; i < 3; i++) {  // rips, with grey skin showing through
        Vector2 a = P(-40 + 40 * i + sway * 0.5f, -400 + 40 * hash(i * 4.0f + who)), b = a + Vector2{14 * s, 26 * s};
        DrawLineEx(a, b, 6 * s, alpha(darken(skin, 0.3f), 0.9f));
        DrawLineEx(a + Vector2{-2 * s, 0}, b + Vector2{-2 * s, 0}, 2 * s, alpha(BLACK, 0.7f));
    }
    // hood / hair behind the head
    Vector2 head = P(sway * 1.3f, -548);
    if (kira) {
        DrawEllipse((int)head.x, (int)(head.y + 20 * s), 92 * s, 104 * s, rgba(0x120a0eff));
        for (int i = 0; i < 12; i++) {
            float x = -80 + i * 14.5f;
            Vector2 a = P(sway * 1.3f + x, -560), b = P(sway + x * 1.05f + std::sin(t * 1.2f + i) * 6, -400 - 30 * hash(i * 2.0f));
            limb(a, b, 9 * s, rgba(0x120a0eff));
        }
    } else {
        DrawEllipse((int)head.x, (int)(sh * s + feet.y - 4 * s), 66 * s, 24 * s, dark);
    }
    // arms: far too long, ending in long-nailed fingers
    for (int k : {-1, 1}) {
        Vector2 shoulder = P(k * 72 + sway, sh + 14);
        Vector2 elbow = P(k * 96 + std::sin(t * 1.3f + k) * 4, -322);
        Vector2 wrist = P(k * 102 + std::sin(t * 1.1f + k) * 7, -170);
        bool shield = k == (kira ? -1 : 1);
        if (shield && flinch > 0) {  // an arm up in front of the face
            elbow = Vector2Lerp(elbow, P(k * 36, -470), flinch);
            wrist = Vector2Lerp(wrist, P(-k * 14, -560), flinch);
        }
        if (reach > 0) {  // both arms coming at you
            elbow = Vector2Lerp(elbow, P(k * 130, -400), reach);
            wrist = Vector2Lerp(wrist, P(k * 175, -450), reach);
        }
        slimb(shoulder, elbow, 30 * s, cloth);
        slimb(elbow, Vector2Lerp(elbow, wrist, 0.5f), 25 * s, cloth);
        slimb(Vector2Lerp(elbow, wrist, 0.45f), wrist, 13 * s, skin);  // torn sleeve, a bony forearm
        DrawCircleV(Vector2Lerp(elbow, wrist, 0.5f), 14 * s, dark);    // the sleeve's torn cuff
        Vector2 dir = Vector2Normalize(wrist - elbow);
        float hs = s * (1 + reach * 0.5f);
        ellipse(wrist + dir * (8 * hs), 13 * hs, 9 * hs, angleOf(dir), darken(skin, 0.1f));
        for (int f = 0; f < 4; f++) {
            float a = angleOf(dir) + (f - 1.5f) * 0.2f * k;
            float curl = 0.3f + 0.25f * std::sin(t * 2.5f + f + k) + reach * 0.3f;
            float len = f == 1 || f == 2 ? 1.0f : 0.82f;
            Vector2 j0 = wrist + dir * (10 * hs);
            Vector2 j1 = j0 + fromAngle(a, 26 * hs * len), j2 = j1 + fromAngle(a + curl * k, 20 * hs * len), j3 = j2 + fromAngle(a + curl * 1.8f * k, 12 * hs);
            slimb(j0, j1, 5.5f * hs, skin);
            slimb(j1, j2, 4.5f * hs, skin);
            DrawLineEx(j2, j3, 3 * hs, nail);  // claws
            DrawCircleV(j1, 3 * hs, darken(skin, 0.25f));
        }
    }
    // neck and the head, which snaps to a new angle every now and then
    slimb(P(sway, sh + 6), P(sway * 1.2f, -500), 15 * s, darken(skin, 0.35f));
    DrawEllipse((int)head.x, (int)(head.y + 66 * s), 40 * s, 14 * s, alpha(BLACK, 0.55f));  // shadow under the jaw
    float snap = hash(std::floor(t * 0.55f + who * 3));
    float tilt = std::sin(t * 0.5f + who) * 6 + (snap > 0.7f ? (snap - 0.7f) * 130 * (who ? -1 : 1) : 0);
    nightmare(who, head, 72 * s, tilt, 1, t);
    if (kira)  // hair falling over her face
        for (int i = 0; i < 6; i++) {
            float x = -60 + i * (i < 3 ? 8.0f : 22.0f) + (i >= 3 ? 50 : 0);
            Vector2 a = head + Vector2{x * s, -66 * s}, b = head + Vector2{(x * 1.15f + std::sin(t + i) * 4) * s, (20 + 50 * hash(i * 5.0f)) * s};
            limb(a, b, 6 * s, rgba(0x120a0eff));
        }
}

void stalkerShadow(int who, Vector2 feet, float s, Vector2 lightFrom, float strength) {
    (void)who;
    Vector2 d = feet - lightFrom;
    float len = Vector2Length(d);
    if (len < 1) return;
    d = d / len;
    // stretched across the floor, away from the torch...
    ellipse(feet + d * (70 * s), 120 * s, 22 * s, angleOf(d), alpha(BLACK, 0.5f * strength));
    // ...and a giant silhouette climbing the wall behind
    Vector2 wall = feet + Vector2{d.x * 40 * s, -330 * s};
    Color c = alpha(BLACK, 0.28f * strength);
    DrawEllipse((int)wall.x, (int)(wall.y - 230 * s), 95 * s, 105 * s, c);
    DrawRectangleRounded({wall.x - 120 * s, wall.y - 140 * s, 240 * s, 330 * s}, 0.5f, 8, c);
}

Vector2 plushEye(Vector2 c, float s, int i, float creep) {
    float flip = i % 2 ? -1.0f : 1.0f;
    s *= 1 + creep * 0.18f;
    return {c.x + (40 + creep * 8) * s * flip, c.y + (-112 + creep * 6) * s};
}

void plush(Vector2 c, float s, float t, int i, float creep, float scared) {
    const Color furs[3] = {rgba(0x8a5a3aff), rgba(0xc8beb4ff), rgba(0x4a3a34ff)};
    const Color manes[3] = {rgba(0x2a1a14ff), rgba(0xff6fb5ff), rgba(0xff8a2bff)};
    Color fur = furs[i % 3], mane = manes[i % 3], belly = lighten(fur, 0.25f), shade = darken(fur, 0.35f);
    float flip = i % 2 ? -1.0f : 1.0f;
    s *= 1 + creep * 0.18f;
    c.x += std::sin(t * 60 + i) * 3 * scared * s;
    auto P = [&](float x, float y) { return Vector2{c.x + x * s * flip, c.y + y * s}; };
    float rock = std::sin(t * 1.3f + i * 2) * 0.03f + creep * 0.12f;
    DrawEllipse((int)c.x, (int)(c.y + 4 * s), 70 * s, 12 * s, alpha(BLACK, 0.45f));
    // legs and body
    for (int k = 0; k < 4; k++) {
        Vector2 l = P(-34 + k * 22, -6);
        DrawEllipse((int)l.x, (int)(l.y - 12 * s), 12 * s, 20 * s, k % 2 ? shade : fur);
        DrawEllipse((int)l.x, (int)(l.y + 4 * s), 12 * s, 6 * s, rgba(0x2a1a14ff));
    }
    ellipse(P(0, -44), 60 * s, 42 * s, rock * flip, fur);
    ellipse(P(4, -32), 40 * s, 22 * s, rock * flip, belly);
    for (int k = 0; k < 8; k++) DrawLineEx(P(-30 + k * 8, -18), P(-26 + k * 8, -18), 2 * s, alpha(shade, 0.9f));  // seam
    // a little tail of yarn
    for (int k = 0; k < 4; k++) limb(P(-56, -54), P(-72 - k * 3, -30 + k * 6), 5 * s, mane);
    // neck + head, leaning in as it creeps closer
    Vector2 neck = P(30, -70), head = P(40 + creep * 8, -110 + creep * 6);
    limb(neck, head, 34 * s, fur);
    ellipse(head, 42 * s, 34 * s, (rock + 0.1f) * flip, fur);
    ellipse(head + Vector2{24 * s * flip, 14 * s}, 26 * s, 20 * s, 0.2f * flip, belly);  // muzzle
    for (int k : {-1, 1}) DrawCircleV(head + Vector2{(36 + k * 6) * s * flip, 18 * s}, 3 * s, shade);
    for (int k : {0, 1}) {  // ears
        Vector2 e = head + Vector2{(-14 + k * 18) * s * flip, -30 * s};
        tri(e + Vector2{-9 * s, 0}, e + Vector2{9 * s, 0}, e + Vector2{2 * s * flip, -24 * s}, fur, fur, shade);
    }
    for (int k = 0; k < 7; k++) {  // yarn mane
        Vector2 m = Vector2Lerp(head + Vector2{-20 * s * flip, -30 * s}, neck + Vector2{-18 * s * flip, 4 * s}, k / 6.0f);
        DrawCircleV(m, 9 * s, mane);
        DrawCircleV(m + Vector2{-3 * s, -3 * s}, 3 * s, lighten(mane, 0.3f));
    }
    // button eye with a thread cross
    Vector2 e = head + Vector2{0, -2 * s};
    DrawCircleV(e, 8 * s, rgba(0x0a0606ff));
    for (int k = 0; k < 4; k++) DrawCircleV(e + Vector2{(k % 2 ? 2.5f : -2.5f) * s, (k / 2 ? 2.5f : -2.5f) * s}, 1.2f * s, rgba(0x3a2a2aff));
    // stitched smile, torn open when it's close
    Vector2 m = head + Vector2{26 * s * flip, 22 * s};
    if (creep < 0.6f) {
        DrawLineEx(m + Vector2{-10 * s, -2 * s}, m + Vector2{10 * s, 2 * s}, 2 * s, shade);
    } else {
        DrawEllipse((int)m.x, (int)m.y, 12 * s, 6 * s, rgba(0x1a0408ff));
        for (int k : {-1, 1}) tri(m + Vector2{k * 6 * s, -5 * s}, m + Vector2{k * 2 * s, -5 * s}, m + Vector2{k * 4 * s, 2 * s}, WHITE, WHITE, WHITE);
    }
    // the tag
    Vector2 tg = P(-40, -30);
    DrawRectangleRec({tg.x - 7 * s, tg.y, 14 * s, 18 * s}, rgba(0xe8e0d0ff));
    DrawCircleV({tg.x, tg.y + 7 * s}, 3 * s, rgba(0xc02040ff));
}

void horseSilhouette(Vector2 feet, float s, Color c, float facing) {
    auto P = [&](float x, float y) { return Vector2{feet.x + x * s * facing, feet.y + y * s}; };
    ellipse(P(0, -58), 46 * s, 20 * s, 0, c);
    for (int k = 0; k < 4; k++) limb(P(-34 + k * 22, -50), P(-36 + k * 22 + (k % 2 ? 4 : -4), 0), 7 * s, c);
    limb(P(36, -64), P(52, -104), 18 * s, c);
    ellipse(P(62, -104), 20 * s, 10 * s, 0.5f * facing, c);
    tri(P(46, -114), P(52, -114), P(48, -128), c, c, c);
    limb(P(-44, -60), P(-58, -30), 6 * s, c);
}

void monkeyMan(Vector2 c, float r, float t, float angry) {
    Color fur = rgba(0x24160eff), skin = rgba(0x6e5040ff), dark = rgba(0x0a0604ff);
    float jaw = angry * (0.6f + 0.4f * std::sin(t * 40));
    for (int i = 0; i < 7; i++) {
        float a = -PI / 2 + (i - 3) * 0.32f;
        tri(c + fromAngle(a - 0.2f, r * 0.85f), c + fromAngle(a, r * 1.25f), c + fromAngle(a + 0.2f, r * 0.85f), fur, fur, fur);
    }
    for (int k : {-1, 1}) {
        DrawCircleV({c.x + k * r * 0.98f, c.y - r * 0.05f}, r * 0.33f, fur);
        DrawCircleV({c.x + k * r * 0.98f, c.y - r * 0.05f}, r * 0.2f, skin);
    }
    DrawCircleV(c, r, fur);
    for (int k : {-1, 1}) DrawCircleV({c.x + k * r * 0.3f, c.y - r * 0.12f}, r * 0.36f, skin);
    DrawEllipse((int)c.x, (int)(c.y + r * 0.34f + jaw * r * 0.08f), r * 0.52f, r * (0.36f + jaw * 0.1f), skin);
    DrawEllipse((int)c.x, (int)(c.y - r * 0.4f), r * 0.66f, r * 0.13f, fur);
    for (int k : {-1, 1}) {
        DrawCircleV({c.x + k * r * 0.28f, c.y - r * 0.14f}, r * 0.13f, dark);
        DrawCircleV({c.x + k * r * 0.28f, c.y - r * 0.14f}, r * 0.05f, rgba(0xffe080ff));
    }
    for (int k : {-1, 1}) DrawEllipse((int)(c.x + k * r * 0.09f), (int)(c.y + r * 0.18f), r * 0.05f, r * 0.035f, dark);
    Vector2 m{c.x, c.y + r * 0.46f};
    if (jaw < 0.05f) {
        DrawLineEx({m.x - r * 0.25f, m.y}, {m.x + r * 0.25f, m.y}, std::max(1.5f, r * 0.04f), dark);
    } else {
        DrawEllipse((int)m.x, (int)(m.y + r * 0.05f), r * 0.3f, r * (0.06f + 0.18f * jaw), dark);
        Color fang = rgba(0xe8dcc0ff);
        for (int k : {-1, 1}) tri({m.x + k * r * 0.2f, m.y - r * 0.02f}, {m.x + k * r * 0.12f, m.y - r * 0.02f}, {m.x + k * r * 0.16f, m.y + r * 0.14f * jaw}, fang, fang, fang);
    }
}

void windmill(Vector2 base, float s, float t, Color c) {
    tri({base.x - 13 * s, base.y}, {base.x + 13 * s, base.y}, {base.x + 7 * s, base.y - 62 * s}, c, c, c);
    tri({base.x - 13 * s, base.y}, {base.x + 7 * s, base.y - 62 * s}, {base.x - 7 * s, base.y - 62 * s}, c, c, c);
    DrawCircleSector({base.x, base.y - 62 * s}, 9 * s, 180, 360, 10, c);
    Vector2 hub{base.x, base.y - 60 * s};
    for (int i = 0; i < 4; i++) {
        float a = t * 0.5f + i * PI / 2;
        Vector2 d = fromAngle(a, 1), n{-d.y, d.x};
        Vector2 p0 = hub + d * (6 * s), p1 = hub + d * (42 * s);
        DrawLineEx(hub, p1, 2.2f * s, c);
        Vector2 q0 = p0 + n * (7 * s), q1 = p1 + n * (7 * s);
        DrawLineEx(q0, q1, 1.2f * s, c);
        for (int k = 0; k <= 5; k++) DrawLineEx(Vector2Lerp(p0, p1, k / 5.0f), Vector2Lerp(q0, q1, k / 5.0f), 1.0f * s, c);
    }
    DrawCircleV(hub, 2.5f * s, c);
}

void toyMonkey(Vector2 feet, float s, float clash) {
    Color fur = rgba(0x6a4428ff), face = rgba(0xd8b088ff), gold = rgba(0xe0b040ff);
    DrawEllipse((int)feet.x, (int)(feet.y - 14 * s), 11 * s, 14 * s, fur);
    DrawRectangleRec({feet.x - 10 * s, feet.y - 20 * s, 20 * s, 9 * s}, rgba(0xb02030ff));
    DrawCircleV({feet.x, feet.y - 34 * s}, 10 * s, fur);
    for (int k : {-1, 1}) DrawCircleV({feet.x + k * 10 * s, feet.y - 36 * s}, 4 * s, face);
    DrawEllipse((int)feet.x, (int)(feet.y - 31 * s), 6 * s, 5 * s, face);
    DrawRectangleRec({feet.x - 5 * s, feet.y - 49 * s, 10 * s, 6 * s}, rgba(0xb02030ff));
    for (int k : {-1, 1}) {
        DrawCircleV({feet.x + k * 3 * s, feet.y - 36 * s}, 1.6f * s, BLACK);
        float x = feet.x + k * lerpf(20, 5, clash) * s;
        DrawLineEx({feet.x + k * 8 * s, feet.y - 20 * s}, {x, feet.y - 22 * s}, 3 * s, fur);
        DrawEllipse((int)x, (int)(feet.y - 22 * s), 2.5f * s, 9 * s, gold);
        DrawEllipse((int)x, (int)(feet.y - 22 * s), 1.2f * s, 4 * s, lighten(gold, 0.4f));
    }
}

void jackOLantern(Vector2 c, float r, float t, float flicker) {
    DrawEllipse((int)c.x, (int)(c.y + r * 0.85f), r * 0.9f, r * 0.18f, alpha(BLACK, 0.4f));
    Color o = rgba(0xff7a1aff);
    for (int i = -1; i <= 1; i++) DrawEllipse((int)(c.x + i * r * 0.42f), (int)c.y, r * 0.55f, r * 0.82f, darken(o, 0.15f + std::abs(i) * 0.15f));
    DrawEllipse((int)c.x, (int)c.y, r * 0.4f, r * 0.82f, o);
    DrawRectangleRec({c.x - r * 0.08f, c.y - r * 1.05f, r * 0.16f, r * 0.3f}, rgba(0x3a6a2aff));
    float g = 0.75f + 0.25f * std::sin(t * 13 + c.x) * flicker;
    Color fire = alpha(rgba(0xffd84aff), g);
    tri({c.x - r * 0.45f, c.y - r * 0.1f}, {c.x - r * 0.15f, c.y - r * 0.1f}, {c.x - r * 0.3f, c.y - r * 0.4f}, fire, fire, fire);
    tri({c.x + r * 0.15f, c.y - r * 0.1f}, {c.x + r * 0.45f, c.y - r * 0.1f}, {c.x + r * 0.3f, c.y - r * 0.4f}, fire, fire, fire);
    for (int i = 0; i < 5; i++) {
        float x = c.x - r * 0.5f + i * r * 0.2f;
        tri({x, c.y + r * 0.2f}, {x + r * 0.2f, c.y + r * 0.2f}, {x + r * 0.1f, c.y + r * (i % 2 ? 0.48f : 0.32f)}, fire, fire, fire);
    }
    fx::beginAdd();
    fx::glow(c, r * 2.2f, alpha(rgba(0xff8a2aff), 0.3f * g));
    fx::endAdd();
}

// ====================================================================== first person

static void sleeve(Vector2 a, Vector2 b, float w) {
    limb(a, b, w, rgba(0x5b2fb0ff));  // Pieter's purple hoodie
    DrawLineEx(Vector2Lerp(a, b, 0.92f), b, w * 0.9f, rgba(0x4a248eff));
}

void flashlightHand(Vector2 aim, float t, float on, float bob) {
    Vector2 grip{1000 + (aim.x - 640) * 0.16f, 676 + (aim.y - 360) * 0.08f + bob};
    Vector2 dir = Vector2Normalize(aim - grip);
    float a = angleOf(dir) * RAD2DEG;
    sleeve({grip.x + 170, VH + 90.0f}, grip + Vector2{40, 40}, 92);
    rlPushMatrix();
    rlTranslatef(grip.x, grip.y, 0);
    rlRotatef(a, 0, 0, 1);
    // the torch: knurled body, flared head, glowing lens
    Rectangle body{-60, -17, 130, 34};
    fx::gradRoundRect(body, 0.4f, rgba(0x3a3a44ff), rgba(0x101016ff));
    for (int i = 0; i < 9; i++) DrawLineEx({-50.0f + i * 8, -15}, {-50.0f + i * 8, 15}, 2, alpha(BLACK, 0.4f));
    quad({70, -18}, {108, -28}, {108, 28}, {70, 18}, rgba(0x4a4a56ff), rgba(0x18181eff));
    DrawRectangleRounded({104, -30, 10, 60}, 0.5f, 6, rgba(0x5a5a66ff));
    DrawEllipse(113, 0, 4, 27, on > 0 ? mix(rgba(0x8a8070ff), rgba(0xfff4d0ff), on) : rgba(0x2a2a30ff));
    DrawRectangleRounded({6, -22, 26, 8}, 0.5f, 4, on > 0 ? rgba(0xe04040ff) : rgba(0x602020ff));  // the switch
    // fingers wrapped around it
    Color skin = rgba(0xd9a37aff), skinD = rgba(0xb07a58ff);
    DrawEllipse(-10, 8, 40, 30, skinD);
    for (int i = 0; i < 4; i++) DrawEllipse(-30 + i * 17, 18 + (i == 0 ? -2 : 0), 10, 14, i % 2 ? skin : lighten(skin, 0.06f));
    DrawEllipse(18, -14, 18, 10, skin);  // thumb on the switch
    rlPopMatrix();
    if (on > 0) {
        Vector2 lens = grip + dir * 113;
        fx::beginAdd();
        fx::glow(lens, 90, alpha(rgba(0xfff0c8ff), 0.35f * on));
        fx::glow(lens, 22, alpha(WHITE, 0.8f * on));
        fx::endAdd();
    }
    (void)t;
}

void holdingHands(float push, float t, bool closet) {
    Color skin = rgba(0x7a5a46ff), skinD = rgba(0x5a4032ff);  // in the dark, lit only by the moon
    for (int k : {-1, 1}) {
        float jitter = std::sin(t * 31 + k) * 3 * push;
        Vector2 palm{VW / 2.0f + k * (closet ? 170 : 230) + jitter, 610 - push * 24 + std::sin(t * 2 + k) * 4};
        limb({palm.x + k * 120, VH + 120.0f}, palm + Vector2{k * 20.0f, 60}, 100, rgba(0x2e1a58ff));
        // the back of the hand, fingers spread flat against the wood
        DrawEllipse((int)palm.x, (int)palm.y, 48, 52, skinD);
        DrawEllipse((int)palm.x, (int)(palm.y - 4), 43, 47, skin);
        for (int f = 0; f < 4; f++) {
            float x = palm.x + (-30 + f * 20) * (k < 0 ? -1 : 1);
            Vector2 a{x, palm.y - 34}, b{x + (f - 1.5f) * 7 * (k < 0 ? -1 : 1), palm.y - 92 + std::abs(f - 1.5f) * 12};
            limb(a, b, 18, skinD);
            limb(a, b, 14, skin);
            DrawCircleV(b, 4, alpha(rgba(0xc8b0a0ff), 0.35f));  // fingernails
        }
        limb({palm.x - k * 40, palm.y + 8}, {palm.x - k * 72, palm.y - 30}, 19, skin);  // thumb
        for (int f = 0; f < 3; f++) DrawLineEx({palm.x - 18 + f * 18.0f, palm.y - 20}, {palm.x - 14 + f * 14.0f, palm.y + 10}, 2, alpha(skinD, 0.6f));  // tendons
    }
}

// ====================================================================== the other one

// Pieter's own face, wrong: drained of colour, stretched, the eyes gone to holes
// that don't catch any light, and a smile that keeps going past where it should stop.
void otherFace(Vector2 c, float r, float t, float jaw, float rot, float a) {
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(rot, 0, 0, 1);
    rlScalef(0.92f, 1.32f, 1);
    art::portrait("pieter", {0, 0}, r, BLANK, 0, false, alpha(rgba(0x9c9aa0ff), a));
    DrawCircleGradient({0, 0}, r, alpha(BLACK, 0.0f), alpha(BLACK, 0.6f * a));
    DrawCircleGradient({0, r * 0.35f}, r * 0.9f, alpha(rgba(0x10141aff), 0.25f * a), alpha(BLACK, 0));
    // the eyes: deep, perfectly black, a little too big
    const Vector2 eye[2] = {{-0.02f, 0.23f}, {0.58f, 0.31f}};
    for (int i = 0; i < 2; i++) {
        Vector2 e = eye[i] * r;
        DrawCircleGradient(e, r * 0.3f, alpha(BLACK, 0.7f * a), alpha(BLACK, 0));
        ellipse(e, r * 0.15f, r * (0.12f + jaw * 0.03f), 0.1f, alpha(BLACK, a));
        // something wet trickling out of one of them
        if (i == 0) DrawLineEx(e + Vector2{r * 0.02f, r * 0.1f}, e + Vector2{r * 0.035f, r * (0.26f + 0.02f * std::sin(t))}, std::max(1.0f, r * 0.02f), alpha(BLACK, 0.85f * a));
    }
    // the smile, cut far too wide; with jaw > 0 it opens onto nothing
    float w = 0.62f + jaw * 0.1f;
    Vector2 prev{};
    for (int i = 0; i <= 24; i++) {
        float u = i / 24.0f;
        float x = lerpf(-w * 0.75f, w, u) + 0.07f;
        float y = 0.7f - std::sin(u * PI) * (0.05f - jaw * 0.12f) - (u > 0.85f ? (u - 0.85f) * 0.6f : 0);
        Vector2 q{x * r, y * r};
        if (i > 0) {
            DrawLineEx(prev, q, std::max(1.5f, r * 0.028f), alpha(rgba(0x060406ff), a));
            if (jaw > 0.05f) tri(prev, q, {q.x, q.y + jaw * r * 0.28f * std::sin(u * PI)}, alpha(BLACK, a), alpha(BLACK, a), alpha(BLACK, a));
        }
        prev = q;
    }
    rlPopMatrix();
}

Vector2 otherHead(Vector2 feet, float s, float look) { return {feet.x + look * 10 * s, feet.y - 738 * s}; }

// Him. Far too tall and far too still, in a washed-out copy of Pieter's hoodie,
// arms hanging down past his knees. s = 1 is about 720 px tall. `look` (-1..1)
// turns the head towards wherever you're looking from.
void other(Vector2 feet, float s, float t, float look, float a) {
    auto P = [&](float x, float y) { return Vector2{feet.x + x * s, feet.y + y * s}; };
    Color hood = alpha(rgba(0x2c2638ff), a), hoodD = alpha(rgba(0x16121eff), a), skin = alpha(rgba(0x8a8486ff), a), pants = alpha(rgba(0x101016ff), a);
    // legs: thin, close together, perfectly straight
    for (int k : {-1, 1}) {
        limb(P(k * 14, -330), P(k * 12, -14), 22 * s, pants);
        DrawEllipse((int)(feet.x + k * 16 * s), (int)(feet.y - 6 * s), 18 * s, 7 * s, alpha(BLACK, a));
    }
    // a long narrow torso
    quad(P(-56, -640), P(56, -640), P(40, -320), P(-40, -320), hood, hoodD);
    quad(P(-56, -640), P(-20, -640), P(-14, -320), P(-40, -320), alpha(BLACK, 0.4f * a), alpha(BLACK, 0.3f * a));
    limb(P(-12, -610), P(-14, -520), 2.5f * s, alpha(rgba(0x6a6478ff), a));  // the drawstrings, hanging dead straight
    limb(P(12, -610), P(14, -520), 2.5f * s, alpha(rgba(0x6a6478ff), a));
    // arms down to the knees, fingers too long, not moving at all
    for (int k : {-1, 1}) {
        Vector2 sh = P(k * 54, -630), el = P(k * 62, -470), wr = P(k * 60, -250);
        limb(sh, el, 24 * s, hood);
        limb(el, wr, 20 * s, hood);
        limb(wr, P(k * 60, -236), 14 * s, skin);
        for (int f = 0; f < 4; f++) {
            Vector2 a0 = P(k * (52 + f * 5), -232), a1 = P(k * (50 + f * 6), -160 - (f == 1 || f == 2 ? 0 : 18));
            limb(a0, a1, 5 * s, skin);
            DrawLineEx(a1, a1 + Vector2{0, 10 * s}, 3 * s, alpha(rgba(0x0a0606ff), a));
        }
    }
    // a neck a little too long, and the head
    limb(P(0, -640), P(look * 6, -690), 18 * s, alpha(darken(rgba(0x8a8486ff), 0.3f), a));
    Vector2 head = P(look * 10, -738);
    DrawEllipse((int)head.x, (int)(head.y + 70 * s), 44 * s, 12 * s, alpha(BLACK, 0.6f * a));
    otherFace(head, 58 * s, t, 0, look * 22 + 4, a);
}

// A hand that isn't anybody's: grey-blue, bony, the knuckles too big, long
// fingers with an extra crook in them and dark broken nails. `dir` is where the
// fingers point (radians), `s` its size.
static void wrongHand(Vector2 wrist, float dir, float s, float t, float spread) {
    Color base = rgba(0x6a666eff), shade = rgba(0x34303aff), lit = rgba(0x8e8a94ff), nail = rgba(0x120c0cff);
    Vector2 fwd = fromAngle(dir), side{-fwd.y, fwd.x};
    Vector2 palm = wrist + fwd * (34 * s);
    ellipse(palm + side * (3 * s), 44 * s, 36 * s, dir, shade);
    ellipse(palm, 40 * s, 32 * s, dir, base);
    ellipse(palm - side * (8 * s) - fwd * (6 * s), 22 * s, 12 * s, dir, alpha(lit, 0.5f));
    for (int i = 0; i < 3; i++) DrawLineEx(wrist + side * ((i - 1) * 9 * s), palm + fwd * (24 * s) + side * ((i - 1) * 12 * s), 2.5f * s, alpha(shade, 0.8f));  // tendons
    for (int f = 0; f < 4; f++) {
        float off = (f - 1.5f) * (17 + spread * 8) * s;
        float len = (f == 1 || f == 2 ? 1.0f : 0.84f) * s;
        float a = dir + (f - 1.5f) * (0.09f + spread * 0.05f);
        float wob = std::sin(t * 1.3f + f) * 0.04f;
        Vector2 j0 = palm + fwd * (30 * s) + side * off;
        Vector2 j1 = j0 + fromAngle(a + wob, 46 * len), j2 = j1 + fromAngle(a + 0.25f + wob, 36 * len), j3 = j2 + fromAngle(a + 0.55f, 26 * len);
        for (auto [p, q, w] : {std::tuple{j0, j1, 15.0f}, std::tuple{j1, j2, 12.5f}, std::tuple{j2, j3, 10.0f}}) {
            limb(p, q, (w + 3) * s, shade);
            limb(p - side * (1.5f * s), q - side * (1.5f * s), w * s, base);
        }
        DrawCircleV(j1, 9 * s, base);  // knuckles too big
        DrawCircleV(j1 - side * (2 * s), 4 * s, alpha(lit, 0.6f));
        DrawCircleV(j2, 7 * s, base);
        limb(j3, j3 + fromAngle(a + 0.7f, 16 * len), 6 * s, nail);
    }
    Vector2 th = palm - side * (36 * s) - fwd * (8 * s);
    limb(th, th + fromAngle(dir - 0.9f, 40 * s), 15 * s, shade);
    limb(th, th + fromAngle(dir - 0.9f, 38 * s), 12 * s, base);
    limb(th + fromAngle(dir - 0.9f, 38 * s), th + fromAngle(dir - 0.9f, 52 * s), 5 * s, nail);
}

// A third hand on the door, pressed flat right between yours, slipping down and away at the end.
void thirdHand(float k, float t) {
    float slide = k < 0.15f ? (1 - k / 0.15f) * 200 : k > 0.8f ? (k - 0.8f) / 0.2f * 320 : 0;
    Vector2 wrist{VW / 2.0f + 6, 700 + slide};
    limb(wrist + Vector2{12, 140}, wrist, 52, rgba(0x14101aff));
    wrongHand(wrist, -PI / 2 + 0.05f, 1.0f, t, 0.3f);
}

// Something resting on Pieter's shoulder, from behind. k: 0..1 how far it has crept in.
void shoulderHand(float k, float t) {
    float e = easeInOutSine(clamp01(k));
    Vector2 wrist{-120 + e * 250, VH + 40 - e * 170};
    limb(wrist + Vector2{-220, 120}, wrist, 70, rgba(0x0e0c12ff));  // the arm, reaching over from behind you
    wrongHand(wrist, -0.32f, 1.35f, t, 0.1f);
}

// ====================================================================== the street

namespace {
constexpr float kHouseX0 = 45, kHouseY = 180, kStreetY = 560;
const int kHouseTex[7] = {2, 0, 4, 3, 1, 5, 0};
Rectangle window(int house, int col, int row) {
    return {kHouseX0 + house * 170 + 21 + col * 46.0f, kHouseY + 169 + row * 62.0f, 31, 45};
}
}  // namespace

void street(const Street& s) {
    const float d = s.dawn, L = s.lightning, t = s.t;
    // ---- sky (no camera: it's far away)
    Color top = mix(rgba(0x04050eff), rgba(0x3a4a8aff), d), mid = mix(rgba(0x12142eff), rgba(0xe88a6aff), d), low = mix(rgba(0x2a1f4aff), rgba(0xffcf9aff), d);
    DrawRectangleGradientV(0, 0, VW, 330, top, mid);
    DrawRectangleGradientV(0, 330, VW, 400, mid, low);
    if (d < 0.9f)
        for (int i = 0; i < 90; i++) {
            Vector2 p{hash(i + 0.5f) * VW, hash(i * 1.7f) * 300};
            float tw = 0.4f + 0.6f * std::sin(t * (1 + hash(i * 3.0f) * 3) + i);
            DrawCircleV(p, 1 + hash(i * 5.0f), alpha(WHITE, (1 - d) * 0.5f * tw * (1 - s.rain * 0.5f)));
        }
    float camK = clamp01((s.zoom - 1) / 0.6f);
    Vector2 camC = Vector2Lerp({VW / 2.0f, VH / 2.0f}, s.focus, camK);
    fx::beginAdd();
    if (d < 1) {
        Vector2 moon{1060 - (camC.x - 640) * 0.05f, 110};
        fx::glow(moon, 260, alpha(rgba(0x8090d0ff), 0.25f * (1 - d)));
        fx::endAdd();
        DrawCircleV(moon, 34, alpha(rgba(0xf4f0dcff), 1 - d));
        for (int i = 0; i < 5; i++) DrawCircleV(moon + Vector2{(hash(i * 2.0f) - 0.5f) * 36, (hash(i * 3.0f) - 0.5f) * 36}, 3 + 4 * hash(i * 7.0f), alpha(rgba(0xd8d2bcff), 0.6f * (1 - d)));
        fx::beginAdd();
    }
    if (d > 0) {
        Vector2 sun{300, 600 - d * 170};
        fx::glow(sun, 700, alpha(rgba(0xffa060ff), 0.5f * d));
        fx::glow(sun, 120, alpha(rgba(0xfff0c0ff), 0.9f * d));
    }
    if (L > 0) fx::glow({VW / 2.0f, 80}, 1100, alpha(rgba(0xc8d4ffff), 0.8f * L));
    fx::endAdd();
    // clouds drifting past the moon, lit from behind
    for (int i = 0; i < 9; i++) {
        float x = std::fmod(hash(i * 1.3f) * 1800 + t * (6 + 6 * hash(i * 2.0f)), 1800.0f) - 260, y = 40 + hash(i * 4.0f) * 200;
        Color c = mix(mix(rgba(0x0e0f22ff), rgba(0xd09080ff), d), rgba(0x9aa4d8ff), L * 0.8f);
        DrawEllipse((int)x, (int)y, 200 + 120 * hash(i * 5.0f), 34 + 20 * hash(i * 6.0f), alpha(c, 0.75f));
        DrawEllipse((int)(x + 60), (int)(y - 18), 120, 30, alpha(c, 0.7f));
    }

    rlPushMatrix();
    rlTranslatef(VW / 2.0f, VH / 2.0f, 0);
    rlScalef(s.zoom, s.zoom, 1);
    rlTranslatef(-camC.x, -camC.y, 0);
    // ---- the skyline far behind: the Westertoren and a windmill
    Color far = mix(mix(rgba(0x0a0a1cff), rgba(0x7a5a6aff), d), rgba(0x30365cff), L);
    {
        float x = 230, b = 330;
        DrawRectangleRec({x - 26, b - 150, 52, 150}, far);
        DrawRectangleRec({x - 18, b - 200, 36, 50}, far);
        DrawRectangleRec({x - 12, b - 238, 24, 38}, far);
        tri({x - 12, b - 238}, {x + 12, b - 238}, {x, b - 300}, far, far, far);
        DrawCircleV({x, b - 300}, 4, far);  // the crown
        DrawRectangleRec({0, b - 40, 1280, 400}, far);
        for (int i = 0; i < 14; i++) {
            float rx = i * 100 + 20 * hash(i * 1.0f);
            tri({rx, b - 40}, {rx + 80, b - 40}, {rx + 40, b - 80 - 20 * hash(i * 2.0f)}, far, far, far);
        }
        windmill({1180, b - 36}, 1.3f, t, far);
    }
    // ---- the houses
    Color tint = mix(mix(rgba(0x3e4070ff), rgba(0xf0d8d0ff), d), rgba(0xa8b0e0ff), L * 0.7f);
    for (int i = 0; i < 7; i++) {
        Texture2D& tex = E().tex("spr/house" + std::to_string(kHouseTex[i]));
        DrawTexture(tex, (int)(kHouseX0 + i * 170), (int)kHouseY, tint);
    }
    // lit windows: neighbours still awake, and Pieter's monitor
    fx::beginAdd();
    for (int h = 0; h < 7; h++)
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++) {
                Rectangle w = window(h, c, r);
                if (h == 3 && c == 2 && r == 0) continue;
                float k = hash(h * 11.0f + c * 3 + r * 7);
                if (k > 0.2f - d * 0.15f) continue;
                Color warm = rgba(0xffb050ff);
                DrawRectangleRec(shrink(w, 2), alpha(warm, 0.55f * (1 - d)));
                fx::glow(center(w), 40, alpha(warm, 0.25f * (1 - d)));
            }
    {
        Rectangle w = kPieterWindow;
        float flick = 0.8f + 0.2f * std::sin(t * 17) * std::sin(t * 5.3f);
        Color mon = rgba(0x8a70ffff);
        DrawRectangleRec(shrink(w, 2), alpha(mon, 0.6f * flick * (1 - d * 0.7f)));
        fx::glow(center(w), 70, alpha(mon, 0.35f * flick));
    }
    fx::endAdd();
    {   // Pieter, sitting at his desk, a silhouette in the glow
        Rectangle w = kPieterWindow;
        Color k = rgba(0x0a0612ff);
        DrawCircleV({w.x + 13, w.y + 26}, 6, k);
        DrawRectangleRounded({w.x + 4, w.y + 31, 18, 14}, 0.5f, 4, k);
        DrawLineEx({w.x + w.width / 2, w.y}, {w.x + w.width / 2, w.y + w.height}, 2, rgba(0x1a1424ff));
    }
    // pumpkins on Pieter's stoop
    float stoopX = kHouseX0 + 3 * 170 + 30;
    jackOLantern({stoopX, kStreetY - 14}, 12, t, 1);
    jackOLantern({stoopX + 98, kStreetY - 12}, 9, t + 2, 1);
    // ---- things you only see when the lightning shows them
    if (s.figures && L > 0.35f) {
        Color k = alpha(rgba(0x020206ff), clamp01((L - 0.35f) * 3));
        if (s.night >= 2) horseSilhouette({kHouseX0 + 4 * 170 + 70, kHouseY + 52}, 0.75f, k, -1);  // on the roof next door
        if (s.night >= 1) {  // Kira in a neighbour's window
            Rectangle w = window(1, 1, 1);
            DrawCircleV({w.x + 15, w.y + 18}, 8, k);
            DrawRectangleRec({w.x + 7, w.y + 22, 16, 23}, k);
        }
        // Davi under the street lamp
        Vector2 f{906, kStreetY + 34};
        DrawCircleV({f.x, f.y - 70}, 10, k);
        DrawRectangleRounded({f.x - 13, f.y - 60, 26, 42}, 0.4f, 6, k);
        limb({f.x - 6, f.y - 20}, {f.x - 8, f.y}, 7, k);
        limb({f.x + 6, f.y - 20}, {f.x + 8, f.y}, 7, k);
        for (int a : {-1, 1}) limb({f.x + a * 12, f.y - 56}, {f.x + a * 16, f.y - 14}, 6, k);
    }
    // ---- the quay: cobbles, bollards, bikes, the street lamp
    Color cobble = mix(rgba(0x22243aff), rgba(0x8a7a7aff), d);
    fx::tiled(E().tex("tex/cobble"), {-200, kStreetY, 1680, 44}, 0.5f, {0, 0}, mix(rgba(0x40425aff), rgba(0xc0b0a8ff), d));
    DrawRectangleGradientV(-200, (int)kStreetY, 1680, 44, alpha(cobble, 0.3f), alpha(BLACK, 0.5f));
    DrawRectangleRec({-200, kStreetY + 40, 1680, 6}, mix(rgba(0x2a2a3aff), rgba(0x9a8a80ff), d));
    for (int i = 0; i < 14; i++) {  // amsterdammertjes
        float x = -20 + i * 100;
        DrawRectangleRounded({x - 5, kStreetY + 12, 10, 30}, 0.5f, 4, rgba(0x3a1414ff));
        DrawCircleV({x, kStreetY + 12}, 5, rgba(0x4a1a1aff));
    }
    for (float bx : {150.0f, 210.0f, 1100.0f}) {  // bikes against the railing
        Color b = rgba(0x08080eff);
        DrawRing({bx - 22, kStreetY + 30}, 14, 16, 0, 360, 20, b);
        DrawRing({bx + 22, kStreetY + 30}, 14, 16, 0, 360, 20, b);
        DrawLineEx({bx - 22, kStreetY + 30}, {bx, kStreetY + 8}, 2.5f, b);
        DrawLineEx({bx, kStreetY + 8}, {bx + 22, kStreetY + 30}, 2.5f, b);
        DrawLineEx({bx - 4, kStreetY + 4}, {bx + 6, kStreetY + 4}, 3, b);
    }
    Vector2 lamp{900, kStreetY - 140};
    DrawLineEx({lamp.x, kStreetY + 40}, lamp, 5, rgba(0x0c0c14ff));
    DrawLineEx(lamp, lamp + Vector2{20, -6}, 4, rgba(0x0c0c14ff));
    DrawRectangleRounded({lamp.x + 10, lamp.y - 14, 26, 14}, 0.3f, 4, rgba(0x14141cff));
    if (d < 0.6f) {
        float on = 1 - d / 0.6f;
        Vector2 bulb{lamp.x + 23, lamp.y};
        fx::beginAdd();
        tri(bulb, {bulb.x - 80, kStreetY + 40}, {bulb.x + 80, kStreetY + 40}, alpha(rgba(0xffc070ff), 0.22f * on), alpha(rgba(0xffc070ff), 0.0f),
            alpha(rgba(0xffc070ff), 0.0f));
        fx::glow(bulb, 80, alpha(rgba(0xffc070ff), 0.5f * on));
        fx::glow({bulb.x, kStreetY + 40}, 120, alpha(rgba(0xffc070ff), 0.2f * on));
        for (int i = 0; i < 30; i++) {  // rain glittering in the lamp light
            float x = bulb.x - 60 + hash(i * 1.1f) * 120, y = std::fmod(hash(i * 2.3f) * 200 + t * 520, 200.0f) + bulb.y;
            float k = 1 - std::abs(x - bulb.x) / (60 + (y - bulb.y) * 0.4f);
            if (k > 0) DrawLineEx({x, y}, {x - 2, y + 10}, 1.2f, alpha(rgba(0xffe0b0ff), 0.6f * k * on * s.rain));
        }
        fx::endAdd();
    }
    // ---- the canal: dark water and wobbling reflections
    float wy = kStreetY + 46;
    DrawRectangleGradientV(-200, (int)wy, 1680, 300, mix(rgba(0x0a0c1cff), rgba(0x5a5a7aff), d), mix(rgba(0x02030aff), rgba(0x2a2a40ff), d));
    for (int i = 0; i < 7; i++) {
        Texture2D& tex = E().tex("spr/house" + std::to_string(kHouseTex[i]));
        for (int k = 0; k < 24; k++) {
            float sy = tex.height - (k + 1) * 10.0f;
            if (sy < 0) break;
            float off = std::sin(t * 1.6f + k * 0.7f + i) * (2 + k * 0.25f);
            DrawTexturePro(tex, {0, sy, (float)tex.width, -10}, {kHouseX0 + i * 170 + off, wy + k * 7.0f, (float)tex.width, 7}, {0, 0}, 0,
                           alpha(darken(tint, 0.45f), 0.55f - k * 0.018f));
        }
    }
    fx::beginAdd();
    for (int k = 0; k < 14; k++) {  // the lamp and Pieter's window, smeared across the water
        float off = std::sin(t * 2.1f + k) * 6;
        if (d < 0.6f) DrawRectangleRec({923 - 8 + off, wy + 4 + k * 8.0f, 16, 5}, alpha(rgba(0xffc070ff), 0.25f * (1 - k / 14.0f) * (1 - d)));
        DrawRectangleRec({kPieterWindow.x + 6 + off, wy + 70 + k * 6.0f, 18, 4}, alpha(rgba(0x8a70ffff), 0.18f * (1 - k / 14.0f)));
    }
    fx::endAdd();
    for (int i = 0; i < 24; i++) {  // rain rings on the water
        float life = std::fmod(t * 0.9f + hash(i * 3.3f), 1.0f);
        Vector2 c{std::fmod(hash(i * 1.9f) * 1500 + std::floor(t * 0.9f + hash(i * 3.3f)) * 377, 1500.0f) - 100, wy + 10 + hash(i * 7.1f) * 100};
        DrawEllipseLines((int)c.x, (int)c.y, 4 + life * 18, 1 + life * 4, alpha(rgba(0xa0a8d0ff), 0.35f * (1 - life) * s.rain));
    }
    rlPopMatrix();
    // ---- rain over everything, mist low over the water
    for (int i = 0; i < 260; i++) {
        float sp = 900 + hash(i * 1.7f) * 500;
        float x = std::fmod(hash(i + 0.3f) * 1500 + t * sp * 0.18f, 1500.0f) - 110, y = std::fmod(hash(i * 2.9f) * 900 + t * sp, 900.0f) - 90;
        float len = 14 + 14 * hash(i * 4.0f);
        DrawLineEx({x, y}, {x - len * 0.2f, y + len}, 1.1f, alpha(rgba(0xb0c0e8ff), (0.12f + 0.12f * hash(i * 6.0f)) * s.rain));
    }
    DrawRectangleGradientV(0, 470, VW, 250, alpha(mix(rgba(0x30305aff), rgba(0xffe0c0ff), d), 0), alpha(mix(rgba(0x30305aff), rgba(0xffe0c0ff), d), 0.35f));
    if (L > 0) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(0, 0, VW, VH, alpha(rgba(0x8090c0ff), 0.25f * L));
        EndBlendMode();
    }
}

}  // namespace pg::fnap::gfx
