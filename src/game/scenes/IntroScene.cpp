// The PieterGame 3 intro animation. It is rendered offline into
// assets/video/intro.mpg (see renderIntroVideo) and synced to the theme song
// "Stroopwafel Rush": 100 BPM, first beat at 0.058s, the drop at 6.658s.
// All timing below is in song seconds.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

namespace pg {
namespace {

constexpr float kBeat = 0.6f;
constexpr float kFirstBeat = 0.058f;
constexpr float kBar0 = 1.858f;  // first bar line
constexpr float kDrop = 6.658f;
constexpr float kChars = kDrop + 4 * kBeat;   // 9.058: Pieter, Davi, Kira, Horse (one bar each)
constexpr float kModes = kChars + 16 * kBeat; // 18.658: game modes montage (8 beats)
constexpr float kFinale = kModes + 8 * kBeat; // 23.458
constexpr float kEnd = 26.8f;

const Color kPink = rgba(0xff3d8bff), kYellow = rgba(0xffd23fff), kCyan = rgba(0x3cd6ffff), kViolet = rgba(0x8f5cffff),
            kOrange = rgba(0xff8a2bff), kGreen = rgba(0x3ddc84ff);

float beatOf(float t) { return (t - kFirstBeat) / kBeat; }
// 1 right on a beat, decaying to 0 before the next one
float pulse(float t, float sharp = 7) {
    float b = beatOf(t);
    if (b < 0) return 0;
    return std::exp(-(b - std::floor(b)) * sharp);
}
float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }

struct Character {
    const char* face;
    const char* name;
    const char* tagline;
    Color a, b;
};
const Character kCast[] = {
    {"pieter", "PIETER", "THE DUTCH LEGEND", kViolet, kPink},
    {"davi", "DAVI", "PRO SINCE 2010 (CLAIMS)", kCyan, rgba(0x2f5aa8ff)},
    {"kira", "KIRA", "THE SISTER RIVAL", kPink, kYellow},
    {"horse", "THE HORSE", "JUHUH!!!", kOrange, kYellow},
};

class IntroScene : public Scene {
public:
    MusicMode music() const override { return MusicMode::None; }

    void update(float dt) override {
        float prev = t_;
        t_ += dt;
        fx_.update(dt);
        shocks_.update(dt);
        auto crossed = [&](float at) { return prev < at && t_ >= at; };

        // the drop: everything explodes
        if (crossed(kDrop)) {
            flash_ = 1;
            for (int i = 0; i < 3; i++) shocks_.add({VW / 2.0f, VH / 2.0f}, 40, 500 + i * 250, i == 1 ? kYellow : WHITE, 0.6f + i * 0.2f, 24);
            fx_.confetti({VW / 2.0f, VH / 2.0f}, 260, 1100);
            fx_.burst({VW / 2.0f, VH / 2.0f}, 60, kYellow, 900, 10, 1.0f, PShape::Star);
        }
        for (int i = 0; i < 4; i++)
            if (crossed(kChars + i * 4 * kBeat)) {
                Vector2 c{i % 2 ? VW * 0.7f : VW * 0.3f, VH * 0.5f};
                shocks_.add(c, 60, 360, kCast[i].a, 0.6f, 20);
                fx_.burst(c, 40, kCast[i].b, 700, 9, 0.9f, i == 2 ? PShape::Heart : PShape::Star);
                fx_.confetti({c.x, VH + 20.0f}, 80, 1000);
            }
        for (int i = 0; i < 5; i++)
            if (crossed(kModes + i * 1.6f * kBeat)) shocks_.add({VW / 2.0f, VH / 2.0f - 20}, 100, 520, WHITE, 0.45f, 12);
        if (crossed(kFinale)) {
            flash_ = 0.8f;
            fx_.confetti({VW * 0.25f, VH + 20.0f}, 160, 1200);
            fx_.confetti({VW * 0.75f, VH + 20.0f}, 160, 1200);
            shocks_.add({VW / 2.0f, VH * 0.35f}, 50, 900, kYellow, 0.9f, 28);
        }
        if (crossed(kFinale + 4 * kBeat)) fx_.confetti({VW / 2.0f, VH * 0.8f}, 220, 1300);
        // steady sparkle rain after the drop
        if (t_ > kDrop && t_ < kEnd - 0.8f && chance(dt * 30)) {
            Particle p;
            p.pos = {rnd(0, VW), -10};
            p.vel = {rnd(-40, 40), rnd(160, 320)};
            p.life = p.maxLife = 3;
            p.size = rnd(4, 9);
            p.endSize = p.size * 0.5f;
            p.color = chance(0.5f) ? WHITE : kYellow;
            p.shape = PShape::Star;
            p.rotVel = rnd(-4, 4);
            fx_.add(p);
        }
        flash_ = std::max(0.0f, flash_ - dt * 2.2f);
    }

    // ----------------------------------------------------------- backgrounds
    void sunburst(Vector2 c, Color a, Color b, float speed, int rays = 18, float alphaK = 1) {
        DrawRectangle(0, 0, VW, VH, alpha(a, alphaK));
        float rot = t_ * speed;
        for (int i = 0; i < rays; i++) {
            float a0 = rot + i * 360.0f / rays;
            DrawCircleSector(c, 1600, a0, a0 + 180.0f / rays, 3, alpha(b, alphaK));
        }
        DrawCircleGradient(c, 520, alpha(WHITE, 0.35f * alphaK), alpha(WHITE, 0));
    }

    void floaters(float k) {
        // soft drifting circles + rings for depth
        for (int i = 0; i < 22; i++) {
            float sx = std::fmod(i * 157.3f + t_ * (20 + i % 5 * 9), (float)VW + 200) - 100;
            float sy = std::fmod(i * 91.7f - t_ * (30 + i % 7 * 8), (float)VH + 200);
            if (sy < 0) sy += VH + 200;
            sy -= 100;
            float r = 10 + (i * 37 % 40);
            Color c = alpha(WHITE, 0.18f * k);
            if (i % 3 == 0) DrawRing({sx, sy}, r - 4, r, 0, 360, 32, c);
            else DrawCircleV({sx, sy}, r * 0.6f, c);
        }
    }

    Color cycle(float speed = 0.25f) const {
        const Color stops[] = {kPink, kViolet, kCyan, kYellow, kPink};
        float k = std::fmod(t_ * speed, 4.0f);
        int i = (int)k;
        return mix(stops[i], stops[i + 1], k - i);
    }

    // ----------------------------------------------------------- sections
    void drawBuild() {
        // dark purple night that warms up as the drop approaches
        float warm = seg(t_, kBar0, kDrop);
        DrawRectangleGradientV(0, 0, VW, VH, mix(rgba(0x0f0820ff), rgba(0x3b1a70ff), warm), mix(rgba(0x1a0f2eff), rgba(0x8f2c78ff), warm));
        if (t_ > kBar0) sunburst({VW / 2.0f, VH / 2.0f}, alpha(BLACK, 0), alpha(kViolet, 0.25f), 12, 16, warm);
        fx::bokeh(t_, 24, kPink, 9);
        // twinkles
        for (int i = 0; i < 70; i++) {
            float x = (i * 211 % VW), y = (i * 137 % VH);
            float tw = 0.5f + 0.5f * std::sin(t_ * 3 + i * 1.7f);
            float appear = seg(t_, i * 0.025f, i * 0.025f + 0.5f);
            DrawCircleV({x, y}, 1.5f + tw * 1.5f, alpha(WHITE, appear * tw * 0.8f));
        }
        // character silhouettes peek in from the corners on beats 4..7 of the build
        const Vector2 corners[] = {{150, 590}, {1130, 590}, {150, 130}, {1130, 130}};
        for (int i = 0; i < 4; i++) {
            float at = kBar0 + i * kBeat;
            float k = easeOutBack(seg(t_, at, at + 0.35f)) * (1 - seg(t_, kDrop - 0.3f, kDrop));
            if (k <= 0) continue;
            Vector2 c = corners[i] + Vector2{(i % 2 ? 1 : -1) * 260 * (1 - k), 0};
            art::portrait(kCast[i].face, c, 110, alpha(kCast[i].a, k), (i % 2 ? -1 : 1) * 12.0f, false, alpha(rgba(0x2a1650ff), k));
            ui::textOutline("?", c.x, c.y, 90 * k, alpha(WHITE, k), alpha(col::outline, k), 4);
        }
        // the Pietercoin spirals in to the center
        float ck = easeOutCubic(seg(t_, 0.2f, kBar0 + kBeat));
        float ang = (1 - ck) * 5;
        Vector2 cc = Vector2Lerp({VW / 2.0f + std::cos(ang) * 500, VH + 120.0f + std::sin(ang) * 120}, {VW / 2.0f, VH / 2.0f}, ck);
        float leave = seg(t_, kDrop - 2 * kBeat, kDrop);
        cc.y -= leave * 160;
        float cr = 40 + 70 * ck - 50 * leave + pulse(t_) * 6 * ck;
        fx::beginAdd();
        fx::glow(cc, cr * 4, alpha(kYellow, 0.55f));
        fx::endAdd();
        float spin = std::abs(std::cos(t_ * 4));
        rlPushMatrix();
        rlTranslatef(cc.x, cc.y, 0);
        rlScalef(0.25f + 0.75f * spin, 1, 1);
        ui::coin({0, 0}, cr);
        rlPopMatrix();
        // the title letters drop one per half-beat before the drop
        const char* word = "PIETERGAME";
        const float letterGap = 2 * kBeat * 2 / 10.0f;
        float fs = 108, total = ui::measure(word, fs, true) + 9 * 6;
        float x = VW / 2.0f - total / 2;
        for (int i = 0; i < 10; i++) {
            char ch[2] = {word[i], 0};
            float w = ui::measure(ch, fs, true);
            float at = kDrop - 4 * kBeat + i * letterGap;
            float k = seg(t_, at, at + 0.22f);
            if (k > 0) {
                float y = lerpf(-120, VH / 2.0f + 40, easeOutBack(k));
                Color c = (i % 4 == 0) ? kPink : (i % 4 == 1) ? kYellow : (i % 4 == 2) ? kCyan : kGreen;
                fx::gradientText(ch, x + w / 2, y, fs, WHITE, c, 6);
            }
            x += w + 6;
        }
    }

    void drawTitleSlam() {
        float k = seg(t_, kDrop, kDrop + 0.35f);
        float out = seg(t_, kChars - 0.35f, kChars);
        sunburst({VW / 2.0f, VH / 2.0f}, kPink, kYellow, 40);
        floaters(1);
        float bump = 1 + pulse(t_) * 0.06f;
        float y = VH / 2.0f - 20 - out * 600;
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, y, 0);
        rlScalef(bump, bump, 1);
        fx::gradientText("PIETERGAME", -70, -10, 118, WHITE, kViolet, 8);
        float s3 = lerpf(6.0f, 1.0f, easeOutBack(k));
        rlPushMatrix();
        rlTranslatef(372, -10, 0);
        rlRotatef(lerpf(-200, -8, easeOutCubic(k)) + std::sin(t_ * 6) * 4, 0, 0, 1);
        rlScalef(s3, s3, 1);
        fx::beginAdd();
        fx::glow({0, 0}, 160, alpha(kYellow, 0.6f));
        fx::endAdd();
        fx::gradientText("3", 0, 0, 200, kYellow, kOrange, 9);
        rlPopMatrix();
        rlPopMatrix();
        float sub = easeOutBack(seg(t_, kDrop + 2 * kBeat, kDrop + 2 * kBeat + 0.3f));
        if (sub > 0) {
            Rectangle r{VW / 2.0f - 260 * sub, y + 100, 520 * sub, 56};
            DrawRectangleRounded(r, 1, 12, col::outline);
            DrawRectangleRounded(shrink(r, 4), 1, 12, kViolet);
            if (sub > 0.9f) ui::text("THE DUTCH LEGEND RETURNS", VW / 2.0f, r.y + 28, 30, WHITE, ui::Center, true);
        }
    }

    void drawCharacter(int i, float t0) {
        const Character& c = kCast[i];
        float local = t_ - t0, dur = 4 * kBeat;
        float in = easeOutBack(seg(local, 0, 0.35f)), out = easeInCubic(seg(local, dur - 0.25f, dur));
        sunburst({i % 2 ? VW * 0.7f : VW * 0.3f, VH / 2.0f}, c.a, c.b, i % 2 ? -50 : 50, 20);
        floaters(1);
        bool right = i % 2;
        float side = right ? 1 : -1;
        Vector2 pc{(right ? VW * 0.7f : VW * 0.3f) + side * (1 - in) * 700 - side * out * 900, VH / 2.0f + 10};
        float bounce = pulse(t_, 9);
        float sx = 1 + bounce * 0.07f, sy = 1 - bounce * 0.05f;
        fx::beginAdd();
        fx::glow(pc, 420, alpha(WHITE, 0.4f));
        fx::endAdd();
        fx::softShadow({pc.x, pc.y + 230}, 200, 30, 0.5f);
        fx::dashedRing(pc, 232, 10, 24, t_ * 90 * side, WHITE);
        rlPushMatrix();
        rlTranslatef(pc.x, pc.y - bounce * 14, 0);
        rlScalef(sx, sy, 1);
        art::portrait(c.face, {0, 0}, 210, WHITE, std::sin(t_ * 3) * 5);
        rlPopMatrix();
        // name card on the other side
        float tx = right ? VW * 0.3f : VW * 0.7f;
        float tk = easeOutBack(seg(local, 0.15f, 0.5f)), tout = seg(local, dur - 0.25f, dur);
        float tslide = (1 - tk) * -side * 600 + tout * side * 900;
        if (tk > 0) {
            float nameSize = std::string(c.name).size() > 6 ? 104 : 140;
            fx::gradientText(c.name, tx + tslide, VH / 2.0f - 50, nameSize * (1 + bounce * 0.05f), WHITE, c.a, 8);
            Rectangle r{tx + tslide - 250, VH / 2.0f + 50, 500, 64};
            DrawRectangleRounded({r.x + 6, r.y + 8, r.width, r.height}, 1, 12, alpha(BLACK, 0.3f));
            DrawRectangleRounded(r, 1, 12, col::outline);
            DrawRectangleRounded(shrink(r, 4), 1, 12, WHITE);
            ui::text(c.tagline, r.x + r.width / 2, r.y + 32, 28, i == 3 ? kOrange : darken(c.a, 0.2f), ui::Center, true);
        }
        // themed props flying around
        for (int k = 0; k < 6; k++) {
            float a = t_ * 1.4f + k * PI / 3;
            Vector2 p = pc + Vector2{std::cos(a) * 300, std::sin(a) * 170};
            float s = 1 + 0.2f * std::sin(t_ * 6 + k);
            switch (i) {
                case 0: art::boba(p, 54 * s); break;
                case 1: {
                    Rectangle r{p.x - 30 * s, p.y - 16 * s, 60 * s, 32 * s};
                    fx::shadedRoundRect(r, 0.9f, kViolet, 3);
                    DrawCircleV({r.x + 14 * s, p.y}, 5 * s, WHITE);
                    DrawCircleV({r.x + r.width - 14 * s, p.y}, 5 * s, kYellow);
                    break;
                }
                case 2: ui::heart(p, 50 * s, k % 2 ? WHITE : kPink); break;
                case 3: art::stroopwafelTop(p, 26 * s); break;
            }
        }
    }

    void drawModeCard(int i, float t0) {
        const char* names[] = {"CLASSIC", "PIETER ROOM", "HORSE RACING", "STROOPWAFEL", "CASINO"};
        const char* subs[] = {"dodge. survive. beat the boss.", "feed him. pet him. talk to him.", "bet on horses or ride one!",
                              "stack the waffles sky high", "roulette - blackjack - mines - slots"};
        const Color cols[] = {kViolet, kPink, kOrange, kYellow, kGreen};
        float local = t_ - t0, dur = 1.6f * kBeat;
        float in = easeOutCubic(seg(local, 0, 0.22f)), out = easeInCubic(seg(local, dur - 0.18f, dur));
        Vector2 c{VW / 2.0f + (1 - in) * 900 - out * 1100, VH / 2.0f - 30};
        float rot = (1 - in) * 14 - out * 14 + std::sin(t_ * 3) * 1.5f;
        Rectangle card{-330, -190, 660, 380};
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        rlRotatef(rot, 0, 0, 1);
        fx::softShadow({10, 30}, 380, 230, 0.5f);
        DrawRectangleRounded(shrink(card, -10), 0.12f, 12, WHITE);
        DrawRectangleRounded(card, 0.1f, 12, darken(cols[i], 0.55f));
        drawModeScene(i, card, local);
        DrawRectangleRoundedLinesEx(card, 0.1f, 12, 4, col::outline);
        rlPopMatrix();
        float lk = easeOutBack(seg(local, 0.08f, 0.35f));
        fx::gradientText(names[i], c.x, VH - 120, 72 * lk, WHITE, cols[i], 6);
        float sw = ui::measure(subs[i], 26, true) + 44;
        DrawRectangleRounded({c.x - sw / 2 * lk, VH - 84, sw * lk, 44}, 1, 10, alpha(col::outline, 0.75f));
        if (lk > 0.9f) ui::text(subs[i], c.x, VH - 62, 26, WHITE, ui::Center, true);
    }

    // a tiny live scene inside each mode card
    void drawModeScene(int i, Rectangle r, float local) {
        Vector2 c = center(r);
        switch (i) {
            case 0: {  // pieter dodging enemies in a neon arena
                DrawRectangleRoundedLinesEx(shrink(r, 22), 0.08f, 8, 4, kCyan);
                for (int k = 0; k < 5; k++) {
                    float a = local * 3 + k * 1.3f;
                    Vector2 p = c + Vector2{std::cos(a * 1.3f) * 220, std::sin(a) * 110};
                    fx::shadedBall(p, 22, k % 2 ? kOrange : col::red, 3);
                    DrawCircleV(p + Vector2{-7, -3}, 6, WHITE);
                    DrawCircleV(p + Vector2{7, -3}, 6, WHITE);
                    DrawCircleV(p + Vector2{-6, -2}, 3, col::outline);
                    DrawCircleV(p + Vector2{8, -2}, 3, col::outline);
                }
                art::portrait("pieter", c + Vector2{std::sin(local * 7) * 60, std::cos(local * 5) * 30}, 52, WHITE);
                break;
            }
            case 1: {
                art::portrait("pieter", c, 120, kPink, std::sin(local * 8) * 8);
                for (int k = 0; k < 6; k++) {
                    float y = std::fmod(local * 160 + k * 60, 300.0f);
                    ui::heart({c.x - 200 + k * 80, r.y + r.height - 30 - y}, 36, kPink);
                }
                art::boba(c + Vector2{200, 40}, 100);
                break;
            }
            case 2: {
                DrawRectangleRec({r.x, c.y + 70, r.width, r.height / 2 - 70}, rgba(0x9a6a3eff));
                art::horse({c.x - 60 + std::sin(local * 4) * 30, c.y + 150}, 1.15f, local * 18, rgba(0x8b5a2bff), rgba(0x3a2010ff), kViolet, "pieter");
                art::horse({c.x + 170, c.y + 140}, 0.7f, local * 16 + 1, rgba(0xd9b38cff), rgba(0xf4e0b0ff), kPink, "kira");
                break;
            }
            case 3: {
                DrawRectangleRounded({c.x - 120, c.y + 70, 240, 110}, 0.3f, 8, WHITE);
                int layers = std::min(9, (int)(local / 0.1f));
                for (int k = 0; k < layers; k++) art::stroopwafel({c.x + std::sin(k * 1.7f) * 12, c.y + 50 - k * 26.0f}, 230 - k * 6.0f, 26);
                break;
            }
            case 4: {
                for (int k = 0; k < 3; k++) {
                    Rectangle reel{c.x - 255 + k * 175, c.y - 110, 160, 220};
                    DrawRectangleRounded(reel, 0.15f, 8, WHITE);
                    bool stopped = local > 0.25f + k * 0.15f;
                    if (stopped) art::portrait("pieter", center(reel), 64, kYellow);
                    else art::portrait(k % 2 ? "horse" : "kira", center(reel) + Vector2{0, std::fmod(local * 2000, 140.0f) - 70}, 54, WHITE);
                }
                for (int k = 0; k < 10; k++) ui::coin({r.x + 40 + k * 65.0f, r.y + r.height - 26 - std::abs(std::sin(local * 8 + k)) * 20}, 18);
                if (local > 0.6f) fx::gradientText("JACKPOT!", c.x, r.y + 30, 54, WHITE, kYellow, 5);
                break;
            }
        }
    }

    void drawFinale() {
        float local = t_ - kFinale;
        sunburst({VW / 2.0f, VH * 0.42f}, cycle(0.6f), WHITE, 70, 24);
        DrawRectangle(0, 0, VW, VH, alpha(kViolet, 0.25f));
        floaters(1);
        // the whole cast bounces in a row, alternating on the beat
        for (int i = 0; i < 4; i++) {
            float in = easeOutBack(seg(local, i * 0.12f, i * 0.12f + 0.4f));
            float b = beatOf(t_);
            bool up = ((int)std::floor(b) + i) % 2 == 0;
            float hop = up ? pulse(t_, 6) * 40 : 0;
            Vector2 p{VW / 2.0f + (i - 1.5f) * 270, VH * 0.62f + (1 - in) * 500 - hop};
            fx::softShadow({p.x, VH * 0.62f + 130}, 100, 18, 0.4f);
            art::portrait(kCast[i].face, p, 112, kCast[i].a, std::sin(t_ * 4 + i) * 8);
        }
        float tk = easeOutBack(seg(local, 0, 0.4f));
        float bump = 1 + pulse(t_) * 0.05f;
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, VH * 0.2f, 0);
        rlScalef(tk * bump, tk * bump, 1);
        fx::gradientText("PIETERGAME", -60, 0, 104, WHITE, kViolet, 7);
        fx::gradientText("3", 318, -6, 150, kYellow, kOrange, 8);
        rlPopMatrix();
        float wk = easeOutElastic(seg(local, 4 * kBeat, 4 * kBeat + 0.8f));
        if (wk > 0) {
            Rectangle r{VW / 2.0f - 380 * wk, VH - 118, 760 * wk, 84};
            DrawRectangleRounded({r.x + 6, r.y + 10, r.width, r.height}, 1, 12, alpha(BLACK, 0.35f));
            DrawRectangleRounded(r, 1, 12, col::outline);
            DrawRectangleRounded(shrink(r, 5), 1, 12, kYellow);
            if (wk > 0.8f) ui::text("WELCOME, PLAYER!", VW / 2.0f, r.y + 42, 50, col::outline, ui::Center, true);
        }
    }

    void draw() override {
        if (t_ < kDrop) drawBuild();
        else if (t_ < kChars) drawTitleSlam();
        else if (t_ < kModes) drawCharacter((int)((t_ - kChars) / (4 * kBeat)), kChars + (int)((t_ - kChars) / (4 * kBeat)) * 4 * kBeat);
        else if (t_ < kFinale) {
            int i = std::min(4, (int)((t_ - kModes) / (1.6f * kBeat)));
            sunburst({VW / 2.0f, VH / 2.0f}, darken(cycle(0.8f), 0.1f), cycle(0.8f), 25, 20);
            floaters(1);
            drawModeCard(i, kModes + i * 1.6f * kBeat);
        } else drawFinale();

        fx_.draw();
        shocks_.draw();
        // beat-synced white pop after the drop
        if (t_ > kDrop && t_ < kEnd - 1) DrawRectangle(0, 0, VW, VH, alpha(WHITE, pulse(t_, 14) * 0.08f));
        if (flash_ > 0) DrawRectangle(0, 0, VW, VH, alpha(WHITE, flash_));
        art::vignette(0.35f);
        // fade in from black, fade out to black
        float fade = std::max(1 - seg(t_, 0, 0.6f), seg(t_, kEnd - 0.9f, kEnd));
        if (fade > 0) DrawRectangle(0, 0, VW, VH, alpha(BLACK, fade));
    }

private:
    float t_ = 0, flash_ = 0;
    Particles fx_;
    fx::Shocks shocks_;
};

}  // namespace

std::unique_ptr<Scene> makeIntroAnimation() { return std::make_unique<IntroScene>(); }
float introDuration() { return kEnd; }

}  // namespace pg
