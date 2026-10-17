// GRACHTENGOLF - nine holes of mini golf through Amsterdam.
// Drag back from the ball and let go to putt. Canals cost a stroke (cross on
// the bridges), tulip beds slow you down, arrows boost you, stroopwafel
// bumpers bounce you, windmill sails and cyclists get in the way.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <array>
#include <vector>

namespace pg {
namespace {

constexpr float kBallR = 9;
constexpr float kCupR = 13;
constexpr float kMaxShot = 1250;

struct Booster { Rectangle r; Vector2 dir; };
struct Bumper { Vector2 p; float r = 30; float punch = 0; };
struct Mill { Vector2 hub; float len, speed, angle = 0; };
struct Mover { Vector2 a, b; float period, phase; Vector2 pos{}, vel{}; };

struct Hole {
    const char* name;
    int par;
    Vector2 tee, cup;
    std::vector<Vector2> outline;               // the course boundary (walls)
    std::vector<std::vector<Vector2>> blocks;   // brick islands inside the course
    std::vector<Rectangle> water, bridges, rough;
    std::vector<Booster> boosters;
    std::vector<Bumper> bumpers;
    std::vector<Mill> mills;
    std::vector<Mover> movers;
};

std::vector<Vector2> rectPoly(float x0, float y0, float x1, float y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

std::vector<Hole> makeCourse() {
    std::vector<Hole> c;
    {  // 1: a warm-up
        Hole h{"De Eerste Put", 2, {290, 400}, {990, 400}};
        h.outline = {{200, 300}, {1000, 300}, {1090, 340}, {1090, 460}, {1000, 500}, {200, 500}};
        h.rough = {{560, 300, 120, 40}, {560, 460, 120, 40}};
        c.push_back(h);
    }
    {  // 2: round the corner
        Hole h{"Om De Hoek", 3, {250, 540}, {975, 210}};
        h.outline = {{150, 450}, {860, 450}, {860, 120}, {1090, 120}, {1090, 640}, {150, 640}};
        h.bumpers = {{{1000, 580}, 34}};
        h.rough = {{860, 120, 230, 60}};
        c.push_back(h);
    }
    {  // 3: cross the canal on the little bridge
        Hole h{"De Gracht", 3, {250, 440}, {1030, 260}};
        h.outline = {{140, 170}, {1140, 170}, {1140, 570}, {140, 570}};
        h.water = {{560, 170, 150, 400}};
        h.bridges = {{560, 360, 150, 80}};
        h.rough = {{140, 170, 160, 90}};
        c.push_back(h);
    }
    {  // 4: through the windmill
        Hole h{"De Molen", 3, {230, 380}, {1060, 380}};
        h.outline = {{120, 220}, {1160, 220}, {1160, 540}, {120, 540}};
        h.blocks = {rectPoly(600, 220, 680, 320), rectPoly(600, 440, 680, 540)};
        h.mills = {{{640, 270}, 150, 1.25f}};
        c.push_back(h);
    }
    {  // 5: slalom through the tulip fields
        Hole h{"Tulpenslalom", 4, {220, 570}, {1060, 200}};
        h.outline = {{120, 110}, {1160, 110}, {1160, 650}, {120, 650}};
        h.blocks = {rectPoly(410, 110, 450, 480), rectPoly(810, 290, 850, 650)};
        h.rough = {{120, 110, 290, 120}, {450, 520, 360, 130}, {850, 110, 310, 70}};
        h.boosters = {{{560, 160, 160, 70}, {1, 0}}};
        c.push_back(h);
    }
    {  // 6: stroopwafel pinball
        Hole h{"Stroopwafel Flipper", 2, {640, 620}, {640, 190}};
        for (int i = 0; i < 10; i++) {
            float a = -PI / 2 + i * 2 * PI / 10 + PI / 10;
            h.outline.push_back({640 + std::cos(a) * 330, 405 + std::sin(a) * 285});
        }
        h.bumpers = {{{640, 400}, 34}, {{530, 320}, 28}, {{750, 320}, 28}, {{540, 490}, 26}, {{740, 490}, 26}};
        c.push_back(h);
    }
    {  // 7: mind the cyclists
        Hole h{"Fietspad", 3, {210, 380}, {1080, 380}};
        h.outline = {{120, 250}, {1170, 250}, {1170, 510}, {120, 510}};
        h.movers = {{{560, 280}, {560, 480}, 2.6f, 0}, {{720, 480}, {720, 280}, 2.1f, 1.3f}};
        h.rough = {{960, 250, 210, 40}, {960, 470, 210, 40}};
        c.push_back(h);
    }
    {  // 8: the U-turn with a speed strip
        Hole h{"De Haarspeld", 3, {250, 210}, {250, 550}};
        h.outline = {{150, 120}, {1100, 120}, {1100, 640}, {150, 640}};
        h.blocks = {rectPoly(150, 300, 900, 460)};
        h.boosters = {{{940, 300, 140, 160}, {0, 1}}};
        h.rough = {{560, 460, 90, 180}};
        h.bumpers = {{{1050, 590}, 26}};
        c.push_back(h);
    }
    {  // 9: the grand finale over the canal, past the mill
        Hole h{"Pieter's Kanaal", 4, {200, 200}, {1070, 580}};
        h.outline = {{100, 100}, {1180, 100}, {1180, 660}, {100, 660}};
        h.water = {{100, 330, 1080, 100}};
        h.bridges = {{290, 330, 90, 100}, {900, 330, 90, 100}};
        h.bumpers = {{{640, 215}, 32}};
        h.blocks = {rectPoly(760, 430, 800, 540)};
        h.mills = {{{780, 540}, 112, -1.4f}};
        h.rough = {{100, 560, 260, 100}};
        h.movers = {{{480, 140}, {480, 300}, 2.4f, 0.5f}};
        c.push_back(h);
    }
    return c;
}

// ------------------------------------------------------------------ geometry
float cross(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }

bool inTri(Vector2 p, Vector2 a, Vector2 b, Vector2 c) {
    float d1 = cross(b - a, p - a), d2 = cross(c - b, p - b), d3 = cross(a - c, p - c);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Ear clipping. Returns triangles as index triples.
std::vector<int> triangulate(const std::vector<Vector2>& poly) {
    std::vector<int> idx(poly.size()), out;
    for (size_t i = 0; i < poly.size(); i++) idx[i] = (int)i;
    float area = 0;
    for (size_t i = 0; i < poly.size(); i++) area += cross(poly[i], poly[(i + 1) % poly.size()]);
    float sgn = area > 0 ? 1.0f : -1.0f;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 1000) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            Vector2 a = poly[ia], b = poly[ib], cc = poly[ic];
            if (cross(b - a, cc - b) * sgn <= 0) continue;  // reflex
            bool ear = true;
            for (int j : idx)
                if (j != ia && j != ib && j != ic && inTri(poly[j], a, b, cc)) { ear = false; break; }
            if (!ear) continue;
            out.insert(out.end(), {ia, ib, ic});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return out;
}

bool pointIn(const std::vector<Vector2>& poly, Vector2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}

Vector2 closestOnSeg(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = b - a;
    float t = std::clamp(Vector2DotProduct(p - a, ab) / std::max(1e-6f, Vector2DotProduct(ab, ab)), 0.0f, 1.0f);
    return a + ab * t;
}

// Fill a polygon with a repeating texture (world-space UVs).
void texPoly(Texture2D& tex, const std::vector<Vector2>& poly, float scale, Color tint) {
    auto tris = triangulate(poly);
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        Vector2 p[4] = {poly[tris[i]], poly[tris[i + 1]], poly[tris[i + 2]], poly[tris[i + 2]]};
        Vector2 uv[4];
        for (int k = 0; k < 4; k++) uv[k] = p[k] * (1 / scale);
        fx::texQuad(tex, p, uv, tint);
    }
}
void fillPoly(const std::vector<Vector2>& poly, Color c) {
    auto tris = triangulate(poly);
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        DrawTriangle(poly[tris[i]], poly[tris[i + 1]], poly[tris[i + 2]], c);
        DrawTriangle(poly[tris[i]], poly[tris[i + 2]], poly[tris[i + 1]], c);
    }
}
std::vector<Vector2> offsetPoly(const std::vector<Vector2>& p, Vector2 d) {
    std::vector<Vector2> o = p;
    for (auto& v : o) v = v + d;
    return o;
}

enum class Phase { Intro, Aim, Roll, Sink, Splash, Card, Final };

class GolfScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Final) return {"Grachtengolf", "Finished: " + std::to_string(total()) + " strokes"};
        return {"Grachtengolf", "Hole " + std::to_string(hole_ + 1) + "/9 - " + course_[hole_].name};
    }

    void enter() override {
        course_ = makeCourse();
        scores_.fill(0);
        autoPlay_ = std::getenv("PG_GOLF_AUTO") != nullptr;  // dev hook: a bot putts at the cup
        int start = 0;
        if (const char* h = std::getenv("PG_GOLF_HOLE")) start = std::clamp(std::atoi(h), 1, 9) - 1;  // dev hook
        startHole(start);
    }

    // ============================================================ flow
    void startHole(int i) {
        hole_ = i;
        Hole& h = course_[i];
        ball_ = h.tee;
        lastPos_ = h.tee;
        vel_ = {0, 0};
        strokes_ = 0;
        phase_ = Phase::Intro;
        phaseT_ = 0;
        trail_.clear();
        fx_.clear();
        aiming_ = false;
        // scenery outside the course: trees and bushes on the cobbles
        decor_.clear();
        for (int k = 0; k < 400 && decor_.size() < 16; k++) {
            Vector2 p{rnd(20, VW - 20.0f), rnd(70, VH - 10.0f)};
            bool clear = !pointIn(h.outline, p);
            for (auto& v : h.outline) if (Vector2Distance(v, p) < 95) clear = false;
            for (size_t e = 0; e < h.outline.size(); e++)
                if (Vector2Distance(closestOnSeg(p, h.outline[e], h.outline[(e + 1) % h.outline.size()]), p) < 95) clear = false;
            for (auto& d : decor_) if (Vector2Distance(d.p, p) < 90) clear = false;
            if (clear) decor_.push_back({p, rnd(0.45f, 0.75f), rndi(0, 2), rnd(0, 360)});
        }
        E().sfx.play(Sfx::Whoosh, 0.5f, 0.8f);
    }

    int total() const {
        int t = 0;
        for (int i = 0; i < 9; i++) t += scores_[i];
        return t;
    }
    int parSoFar(int upto) const {
        int p = 0;
        for (int i = 0; i < upto; i++) p += course_[i].par;
        return p;
    }
    int cap() const { return course_[hole_].par + 4; }

    void shoot(Vector2 v) {
        vel_ = v;
        lastPos_ = ball_;
        strokes_++;
        phase_ = Phase::Roll;
        phaseT_ = 0;
        float k = Vector2Length(v) / kMaxShot;
        E().sfx.play(Sfx::Putt, 0.35f + 0.6f * k, rnd(0.95f, 1.05f) * (1.1f - 0.2f * k));
        fx_.burst(ball_, 6 + (int)(k * 10), rgba(0x8ad06aff), 120 + 200 * k, 3, 0.4f, PShape::Square);
        if (k > 0.75f) E().shake(3);
    }

    void holeDone() {
        scores_[hole_] = std::min(strokes_, cap());
        phase_ = Phase::Card;
        phaseT_ = 0;
        int d = scores_[hole_] - course_[hole_].par;
        if (strokes_ == 1) {
            holeInOnes_++;
            E().sfx.play(Sfx::Perfect, 0.8f);
            E().sfx.play(Sfx::Win, 0.6f);
            fx_.confetti(course_[hole_].cup, 160, 700);
            E().flash(col::gold, 0.4f);
        } else if (d < 0) {
            E().sfx.play(Sfx::Win, 0.5f, 1.1f);
            fx_.confetti(course_[hole_].cup, 70);
        } else if (d > 1) {
            E().sfx.play(Sfx::Lose, 0.35f, 1.2f);
        }
    }

    void finish() {
        phase_ = Phase::Final;
        phaseT_ = 0;
        int over = total() - parSoFar(9);
        coins_ = std::max(25, 160 - over * 15) + holeInOnes_ * 50;
        best_ = P().stats.golfBest == 0 || total() < P().stats.golfBest;
        if (best_) P().stats.golfBest = total();
        P().earn(coins_);
        P().save();
        E().sfx.play(over <= 0 ? Sfx::Win : Sfx::Power, 0.7f);
        if (over <= 0) fx_.confetti({VW / 2.0f, 200}, 160);
    }

    // ============================================================ physics
    float friction(Vector2 p, float& linear) const {
        const Hole& h = course_[hole_];
        for (auto& r : h.rough)
            if (CheckCollisionPointRec(p, r)) { linear = 2.6f; return 520; }
        for (auto& r : h.bridges)
            if (CheckCollisionPointRec(p, r)) { linear = 0.25f; return 90; }
        linear = 0.45f;
        return 150;
    }

    // a bridge over a canal running top-to-bottom is crossed left-to-right
    bool crossesVertWater(Rectangle br) const {
        for (auto& w : course_[hole_].water)
            if (CheckCollisionRecs(br, w)) return w.width < w.height;
        return br.width > br.height;
    }

    bool inWater(Vector2 p) const {
        const Hole& h = course_[hole_];
        for (auto& b : h.bridges)
            if (CheckCollisionPointRec(p, b)) return false;
        for (auto& w : h.water)
            if (CheckCollisionPointRec(p, shrink(w, 2))) return true;
        return false;
    }

    // walls: bounce off a segment, `wallVel` = velocity of the wall at the contact point
    bool collideSeg(Vector2 a, Vector2 b, float e, Vector2 wallVel = {0, 0}) {
        Vector2 q = closestOnSeg(ball_, a, b);
        Vector2 d = ball_ - q;
        float dist = Vector2Length(d);
        if (dist >= kBallR || dist < 1e-4f) return false;
        Vector2 n = d / dist;
        ball_ = q + n * kBallR;
        Vector2 rel = vel_ - wallVel;
        float vn = Vector2DotProduct(rel, n);
        if (vn < 0) {
            rel = rel - n * ((1 + e) * vn);
            vel_ = rel + wallVel;
            hitSound(-vn);
        }
        return true;
    }

    bool collideCircle(Vector2 c, float r, float e, float kick = 0, Vector2 objVel = {0, 0}) {
        Vector2 d = ball_ - c;
        float dist = Vector2Length(d);
        if (dist >= r + kBallR || dist < 1e-4f) return false;
        Vector2 n = d / dist;
        ball_ = c + n * (r + kBallR);
        Vector2 rel = vel_ - objVel;
        float vn = Vector2DotProduct(rel, n);
        if (vn < 0) {
            rel = rel - n * ((1 + e) * vn);
            vel_ = rel + objVel + n * kick;
            if (kick == 0) hitSound(-vn);
        }
        return true;
    }

    void hitSound(float speed) {
        if (speed < 40 || t_ - lastHit_ < 0.05f) return;
        lastHit_ = t_;
        float k = clamp01(speed / 900);
        E().sfx.playAt(Sfx::Click, ball_.x, 0.2f + 0.6f * k, rnd(0.7f, 0.9f));
        if (k > 0.4f) fx_.burst(ball_, 5, rgba(0xe8d0a0ff), 140, 3, 0.3f, PShape::Spark);
    }

    void step(float dt) {
        Hole& h = course_[hole_];
        // boosters
        for (auto& b : h.boosters)
            if (CheckCollisionPointRec(ball_, b.r)) {
                vel_ = vel_ + b.dir * (2200 * dt);
                if (t_ - lastBoost_ > 0.4f) {
                    lastBoost_ = t_;
                    E().sfx.play(Sfx::Whoosh, 0.5f, 1.4f);
                }
            }
        // rolling friction
        float lin;
        float decel = friction(ball_, lin);
        float sp = Vector2Length(vel_);
        if (sp > 0) {
            float ns = std::max(0.0f, sp * std::exp(-lin * dt) - decel * dt);
            vel_ = vel_ * (ns / sp);
        }
        // the cup: a little pull when close and slow, drop in when slow enough
        Vector2 toCup = h.cup - ball_;
        float dc = Vector2Length(toCup);
        if (dc < kCupR + 8 && sp < 320) vel_ = vel_ + Vector2Normalize(toCup) * (900 * dt);
        if (dc < kCupR - 2) {
            if (sp < 560) {
                phase_ = Phase::Sink;
                phaseT_ = 0;
                sinkFrom_ = ball_;
                E().sfx.play(Sfx::Cup, 0.9f);
                return;
            }
            // too fast: it skips over the lip
            if (t_ - lastLip_ > 0.3f) {
                lastLip_ = t_;
                vel_ = Vector2Rotate(vel_, rnd(-0.25f, 0.25f)) * 0.85f;
                E().sfx.play(Sfx::Tick, 0.5f, 0.7f);
            }
        }
        ball_ = ball_ + vel_ * dt;
        // walls
        for (size_t i = 0; i < h.outline.size(); i++) collideSeg(h.outline[i], h.outline[(i + 1) % h.outline.size()], 0.72f);
        for (auto& b : h.blocks)
            for (size_t i = 0; i < b.size(); i++) collideSeg(b[i], b[(i + 1) % b.size()], 0.7f);
        for (auto& br : h.bridges) {  // railings along the bridge (parallel to the way across)
            if (crossesVertWater(br)) {
                collideSeg({br.x, br.y}, {br.x + br.width, br.y}, 0.6f);
                collideSeg({br.x, br.y + br.height}, {br.x + br.width, br.y + br.height}, 0.6f);
            } else {
                collideSeg({br.x, br.y}, {br.x, br.y + br.height}, 0.6f);
                collideSeg({br.x + br.width, br.y}, {br.x + br.width, br.y + br.height}, 0.6f);
            }
        }
        for (auto& b : h.bumpers)
            if (collideCircle(b.p, b.r, 0.9f, 240)) {
                b.punch = 1;
                E().sfx.playAt(Sfx::Boop, b.p.x, 0.6f, rnd(0.9f, 1.1f));
                fx_.burst(ball_, 8, col::wafel, 220, 4, 0.35f, PShape::Star);
            }
        for (auto& m : h.mills) {
            collideCircle(m.hub, 16, 0.5f);
            for (int k = 0; k < 4; k++) {
                float a = m.angle + k * PI / 2;
                Vector2 tip = m.hub + fromAngle(a, m.len);
                // blade velocity at the contact point
                Vector2 q = closestOnSeg(ball_, m.hub, tip);
                Vector2 r = q - m.hub;
                Vector2 wv{-r.y * m.speed, r.x * m.speed};
                if (collideSeg(m.hub, tip, 0.5f, wv)) E().sfx.playAt(Sfx::Creak, ball_.x, 0.25f, 1.6f);
            }
        }
        for (auto& mv : h.movers) {
            Vector2 dir = Vector2Normalize(mv.b - mv.a);
            for (int k = -1; k <= 1; k++) {
                if (collideCircle(mv.pos + dir * (k * 22.0f), 18, 0.6f, 0, mv.vel) && t_ - lastBell_ > 0.6f) {
                    lastBell_ = t_;
                    E().sfx.playAt(Sfx::Bell, mv.pos.x, 0.5f, 1.6f);
                }
            }
        }
    }

    void updateWorld(float dt) {
        Hole& h = course_[hole_];
        for (auto& m : h.mills) m.angle += m.speed * dt;
        for (auto& b : h.bumpers) b.punch = std::max(0.0f, b.punch - dt * 5);
        for (auto& mv : h.movers) {
            float k = 0.5f - 0.5f * std::cos((t_ / mv.period + mv.phase) * 2 * PI);
            Vector2 np = Vector2Lerp(mv.a, mv.b, k);
            mv.vel = dt > 0 ? (np - mv.pos) / dt : Vector2{0, 0};
            if (Vector2Length(mv.vel) > 2000) mv.vel = {0, 0};  // first frame
            mv.pos = np;
        }
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        fx_.update(dt);
        updateWorld(dt);
        switch (phase_) {
            case Phase::Intro:
                if (phaseT_ > 1.6f || (phaseT_ > 0.3f && (E().mousePressed() || IsKeyPressed(KEY_SPACE)))) {
                    E().consumeClick();
                    phase_ = Phase::Aim;
                    phaseT_ = 0;
                }
                break;
            case Phase::Aim: updateAim(); break;
            case Phase::Roll: {
                const int sub = 10;
                for (int i = 0; i < sub && phase_ == Phase::Roll; i++) {
                    step(dt / sub);
                    if (inWater(ball_)) {
                        splash();
                        break;
                    }
                }
                trailT_ += dt;
                if (trailT_ > 0.016f) {
                    trailT_ = 0;
                    trail_.push_back(ball_);
                    if (trail_.size() > 18) trail_.erase(trail_.begin());
                }
                // safety nets: never let the ball escape the course or roll forever
                if (phase_ == Phase::Roll && !pointIn(course_[hole_].outline, ball_)) {
                    ball_ = lastPos_;
                    vel_ = {0, 0};
                }
                if (phase_ == Phase::Roll && phaseT_ > 15) vel_ = {0, 0};
                if (phase_ == Phase::Roll && Vector2Length(vel_) < 6) {
                    vel_ = {0, 0};
                    trail_.clear();
                    if (strokes_ >= cap()) {
                        fx_.floatText("Pick it up...", ball_ + Vector2{0, -30}, col::muted, 22);
                        holeDone();
                    } else {
                        phase_ = Phase::Aim;
                        phaseT_ = 0;
                    }
                }
                break;
            }
            case Phase::Sink:
                if (phaseT_ > 0.7f) holeDone();
                break;
            case Phase::Splash:
                if (phaseT_ > 1.3f) {
                    ball_ = lastPos_;
                    vel_ = {0, 0};
                    strokes_++;  // penalty
                    trail_.clear();
                    if (strokes_ >= cap()) holeDone();
                    else {
                        phase_ = Phase::Aim;
                        phaseT_ = 0;
                    }
                }
                break;
            case Phase::Card:
                if (phaseT_ > 0.6f && (E().mousePressed() || IsKeyPressed(KEY_SPACE) || (autoPlay_ && phaseT_ > 1.5f))) {
                    E().consumeClick();
                    if (hole_ == 8) finish();
                    else startHole(hole_ + 1);
                }
                break;
            case Phase::Final: break;
        }
    }

    void splash() {
        phase_ = Phase::Splash;
        phaseT_ = 0;
        splashAt_ = ball_;
        E().sfx.play(Sfx::Splash, 0.9f);
        fx_.floatText("PLONS! +1", ball_ + Vector2{0, -30}, rgba(0x8fd0ffff), 28);
        for (int i = 0; i < 30; i++) {
            Particle p;
            p.pos = ball_;
            p.vel = fromAngle(rnd(-PI, 0), rnd(80, 300));
            p.gravity = 700;
            p.life = p.maxLife = rnd(0.4f, 0.8f);
            p.size = rnd(2, 5);
            p.color = alpha(rgba(0xd8f0ffff), 0.9f);
            fx_.add(p);
        }
        E().shake(4);
    }

    void updateAim() {
        Vector2 m = E().mouse();
        if (autoPlay_) {
            if (phaseT_ > 0.5f) {
                Vector2 d = course_[hole_].cup - ball_;
                float pw = std::min(kMaxShot, Vector2Length(d) * 1.35f + 120);
                shoot(Vector2Rotate(Vector2Normalize(d), rnd(-0.12f, 0.12f)) * pw);
            }
            return;
        }
        if (!aiming_ && E().mousePressed() && m.y > 64) {
            E().consumeClick();
            aiming_ = true;
            aimFrom_ = m;
        }
        if (aiming_) {
            if (E().rightPressed()) {
                aiming_ = false;
                return;
            }
            if (!E().mouseDown()) {
                aiming_ = false;
                Vector2 v = aimVector();
                if (Vector2Length(v) > 40) shoot(v);
            }
        }
    }

    // pull back like a slingshot: the shot goes away from where you drag
    Vector2 aimVector() const {
        Vector2 d = aimFrom_ - E().mouse();
        float len = Vector2Length(d);
        if (len < 1) return {0, 0};
        float power = std::min(1.0f, len / 220);
        return Vector2Normalize(d) * (power * kMaxShot);
    }

    // ============================================================ draw
    void drawGround() {
        fx::tiled(E().tex("tex/cobble"), {0, 0, (float)VW, (float)VH}, 0.9f, {0, 0}, rgba(0x9a90a8ff));
        DrawRectangle(0, 0, VW, VH, alpha(rgba(0x1a1030ff), 0.25f));
    }

    void drawDecor(bool shadows) {
        for (auto& d : decor_) {
            if (shadows) {
                fx::softShadow({d.p.x + 14, d.p.y + 18}, 70 * d.s, 52 * d.s, 0.45f);
                continue;
            }
            if (d.kind == 2) fx::sprite(E().tex("spr/bush"), d.p, d.s * 1.1f);
            else fx::sprite(E().tex("spr/tree"), d.p, d.s * (d.kind ? 0.8f : 1.0f), d.rot, d.kind ? rgba(0xd8f0c0ff) : WHITE);
        }
    }

    void drawCourse() {
        Hole& h = course_[hole_];
        // drop shadow + raised turf
        fillPoly(offsetPoly(h.outline, {10, 14}), alpha(BLACK, 0.35f));
        texPoly(E().tex("tex/grass"), h.outline, 1.4f, rgba(0xb8f0a0ff));
        stripes(h.outline);  // mowing bands
        // water
        for (auto& w : h.water) {
            DrawRectangleRec(shrink(w, -6), rgba(0x6a6478ff));
            fx::tiled(E().tex("tex/stone"), shrink(w, -6), 0.5f, {0, 0}, rgba(0xb8b0c8ff));
            fx::tiled(E().tex("tex/water"), w, 0.8f, {t_ * 6, t_ * 3}, WHITE);
            DrawRectangleGradientV((int)w.x, (int)w.y, (int)w.width, 18, alpha(BLACK, 0.35f), alpha(BLACK, 0));
            fx::beginAdd();
            for (int i = 0; i < 8; i++) {
                float x = w.x + std::fmod(i * 97 + t_ * 20, w.width), y = w.y + std::fmod(i * 53.0f, w.height);
                fx::glowLine({x, y}, {x + 18, y}, 3, alpha(WHITE, 0.25f + 0.2f * std::sin(t_ * 3 + i)));
            }
            fx::endAdd();
        }
        for (auto& b : h.bridges) {
            fx::softShadow({b.x + b.width / 2 + 6, b.y + b.height / 2 + 10}, b.width * 0.55f, b.height * 0.55f, 0.5f);
            DrawRectangleRec(shrink(b, -3), col::outline);
            fx::tiled(E().tex("tex/wood"), b, 0.6f, {0, 0}, rgba(0xf0c890ff));
            bool vertWater = crossesVertWater(b);
            for (int k = 0; k < 2; k++) {  // railings
                if (vertWater) {
                    float y = k ? b.y + b.height : b.y;
                    DrawLineEx({b.x - 4, y}, {b.x + b.width + 4, y}, 8, col::outline);
                    DrawLineEx({b.x - 4, y}, {b.x + b.width + 4, y}, 5, rgba(0xf4f0e8ff));
                    for (float x = b.x; x <= b.x + b.width; x += b.width / 3) DrawCircleV({x, y}, 5, rgba(0x2a5a3aff));
                } else {
                    float x = k ? b.x + b.width : b.x;
                    DrawLineEx({x, b.y - 4}, {x, b.y + b.height + 4}, 8, col::outline);
                    DrawLineEx({x, b.y - 4}, {x, b.y + b.height + 4}, 5, rgba(0xf4f0e8ff));
                    for (float y = b.y; y <= b.y + b.height; y += b.height / 3) DrawCircleV({x, y}, 5, rgba(0x2a5a3aff));
                }
            }
        }
        // tulip beds
        for (auto& r : h.rough) {
            DrawRectangleRec(r, rgba(0x5a3a24ff));
            fx::tiled(E().tex("tex/tulips"), shrink(r, 3), 0.9f, {0, 0}, WHITE);
            DrawRectangleLinesEx(r, 2, alpha(rgba(0x3a2414ff), 0.8f));
        }
        // boosters: animated chevrons
        for (auto& b : h.boosters) {
            DrawRectangleRounded(b.r, 0.2f, 8, alpha(rgba(0x2a1a3aff), 0.55f));
            Vector2 c = center(b.r);
            float span = (std::abs(b.dir.x) > 0.5f ? b.r.width : b.r.height) - 30;
            for (int i = 0; i < 4; i++) {
                float k = std::fmod(t_ * 1.6f + i / 4.0f, 1.0f);
                Vector2 p = c + b.dir * ((k - 0.5f) * span);
                float a = std::sin(k * PI);
                Vector2 n{-b.dir.y, b.dir.x};
                Vector2 tip = p + b.dir * 14.0f, l = p - b.dir * 6.0f + n * 22.0f, r = p - b.dir * 6.0f - n * 22.0f;
                DrawLineEx(l, tip, 9, alpha(col::orange, a));
                DrawLineEx(r, tip, 9, alpha(col::orange, a));
            }
            fx::beginAdd();
            fx::glow(c, std::max(b.r.width, b.r.height) * 0.5f, alpha(col::orange, 0.15f));
            fx::endAdd();
        }
        // blocks: brick islands with a shadow
        for (auto& b : h.blocks) {
            fillPoly(offsetPoly(b, {8, 12}), alpha(BLACK, 0.4f));
            texPoly(E().tex("tex/brick_red"), b, 0.9f, WHITE);
            for (size_t i = 0; i < b.size(); i++) {
                DrawLineEx(b[i], b[(i + 1) % b.size()], 4, col::outline);
                DrawCircleV(b[i], 2, col::outline);
            }
        }
        // walls: wooden rails
        auto rail = [&](const std::vector<Vector2>& p) {
            for (size_t i = 0; i < p.size(); i++) {
                Vector2 a = p[i], b = p[(i + 1) % p.size()];
                DrawLineEx(a + Vector2{4, 6}, b + Vector2{4, 6}, 14, alpha(BLACK, 0.3f));
            }
            for (size_t i = 0; i < p.size(); i++) {
                Vector2 a = p[i], b = p[(i + 1) % p.size()];
                DrawLineEx(a, b, 15, col::outline);
                DrawCircleV(a, 7.5f, col::outline);
            }
            for (size_t i = 0; i < p.size(); i++) {
                Vector2 a = p[i], b = p[(i + 1) % p.size()];
                DrawLineEx(a, b, 10, rgba(0x9a6a3aff));
                DrawCircleV(a, 5, rgba(0x9a6a3aff));
            }
            for (size_t i = 0; i < p.size(); i++) {
                Vector2 a = p[i], b = p[(i + 1) % p.size()];
                DrawLineEx(a + Vector2{-1, -2}, b + Vector2{-1, -2}, 3, rgba(0xd8a870ff));
            }
        };
        rail(h.outline);
        // tee mat
        DrawRectangleRounded({h.tee.x - 26, h.tee.y - 26, 52, 52}, 0.3f, 8, alpha(rgba(0x1a4a2aff), 0.6f));
        DrawRectangleRoundedLinesEx({h.tee.x - 26, h.tee.y - 26, 52, 52}, 0.3f, 8, 2, alpha(WHITE, 0.4f));
        drawCup();
        for (auto& b : h.bumpers) drawBumper(b);
    }

    void stripes(const std::vector<Vector2>& outline) {
        // light mowing bands, sampled in small strips so they stay inside the course
        float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (auto& v : outline) minX = std::min(minX, v.x), minY = std::min(minY, v.y), maxX = std::max(maxX, v.x), maxY = std::max(maxY, v.y);
        for (float x = minX; x < maxX; x += 80) {
            // only draw the parts of each band that lie inside the polygon (sampled in strips)
            for (float y = minY; y < maxY; y += 10) {
                float x1 = std::min(x + 40, maxX);
                if (pointIn(outline, {x + 2, y + 5}) && pointIn(outline, {x1 - 2, y + 5})) DrawRectangleRec({x, y, x1 - x, 10}, alpha(WHITE, 0.06f));
            }
        }
    }

    void drawCup() {
        const Hole& h = course_[hole_];
        Vector2 c = h.cup;
        DrawCircleV(c, kCupR + 4, alpha(rgba(0xf4f0e8ff), 0.9f));
        DrawCircleV(c, kCupR, rgba(0x0a0612ff));
        DrawCircleGradient({c.x, c.y - 3}, kCupR - 2, rgba(0x2a2238ff), rgba(0x0a0612ff));
        // flag pole + shadow + Dutch flag
        float lift = phase_ == Phase::Roll && Vector2Distance(ball_, c) < 90 ? 1 : 0;
        poleLift_ = damp(poleLift_, lift, 8, GetFrameTime());
        Vector2 base{c.x + poleLift_ * 6, c.y - poleLift_ * 10};
        Vector2 top{base.x + 14, base.y - 92};
        DrawLineEx(c + Vector2{2, 0}, c + Vector2{70, 28}, 4, alpha(BLACK, 0.25f));
        DrawLineEx(base, top, 4, col::outline);
        DrawLineEx(base, top, 2.5f, rgba(0xf4f0e8ff));
        for (int k = 0; k < 3; k++) {
            Color fc = k == 0 ? rgba(0xd8283aff) : k == 1 ? WHITE : rgba(0x2a4aa8ff);
            for (int i = 0; i < 8; i++) {
                float u0 = i / 8.0f, u1 = (i + 1) / 8.0f;
                float w0 = std::sin(t_ * 6 - u0 * 5) * 5 * u0, w1 = std::sin(t_ * 6 - u1 * 5) * 5 * u1;
                Vector2 p0{top.x + u0 * 58, top.y + k * 12 + w0}, p1{top.x + u1 * 58, top.y + k * 12 + w1};
                DrawRectangleRec({p0.x, p0.y, p1.x - p0.x + 0.5f, 12}, fc);
            }
        }
        DrawCircleV(top, 4, col::gold);
    }

    void drawBumper(const Bumper& b) {
        float s = 1 + b.punch * 0.18f;
        fx::softShadow({b.p.x + 5, b.p.y + 8}, b.r * s, b.r * 0.8f * s, 0.5f);
        art::stroopwafelTop(b.p, b.r * s);
        if (b.punch > 0) {
            fx::beginAdd();
            fx::glow(b.p, b.r * 2.2f, alpha(col::gold, 0.5f * b.punch));
            fx::endAdd();
        }
    }

    void drawMill(const Mill& m) {
        // the mill's body sits on the block next to the hub; the sails sweep the gap
        fx::softShadow({m.hub.x + 8, m.hub.y + 10}, 46, 40, 0.5f);
        DrawPoly(m.hub, 8, 44, 22.5f, col::outline);
        DrawPoly(m.hub, 8, 40, 22.5f, rgba(0x6a4a3aff));
        DrawPoly(m.hub, 8, 30, 22.5f, rgba(0x8a6a4aff));
        for (int k = 0; k < 8; k++) DrawLineEx(m.hub, m.hub + fromAngle(k * PI / 4 + PI / 8, 40), 2, alpha(BLACK, 0.3f));
        for (int k = 0; k < 4; k++) {
            float a = m.angle + k * PI / 2;
            Vector2 d = fromAngle(a), n{-d.y, d.x};
            Vector2 p0 = m.hub + d * 14.0f, p1 = m.hub + d * m.len;
            // shadow
            DrawLineEx(p0 + Vector2{10, 14}, p1 + Vector2{10, 14}, 18, alpha(BLACK, 0.25f));
            // lattice sail
            Vector2 q0 = p0 + n * 16.0f, q1 = p1 + n * 16.0f;
            DrawTriangle(p0, q1, q0, alpha(rgba(0xf4ecdcff), 0.85f));
            DrawTriangle(p0, p1, q1, alpha(rgba(0xf4ecdcff), 0.85f));
            DrawTriangle(p0, q0, q1, alpha(rgba(0xf4ecdcff), 0.85f));
            DrawTriangle(p0, q1, p1, alpha(rgba(0xf4ecdcff), 0.85f));
            for (int i = 0; i <= 6; i++) DrawLineEx(Vector2Lerp(p0, p1, i / 6.0f), Vector2Lerp(q0, q1, i / 6.0f), 2, rgba(0x5a3a24ff));
            DrawLineEx(q0, q1, 2.5f, rgba(0x5a3a24ff));
            DrawLineEx(p0, p1, 6, col::outline);
            DrawLineEx(p0, p1, 4, rgba(0x8a5a32ff));
        }
        DrawCircleV(m.hub, 12, col::outline);
        DrawCircleV(m.hub, 9, rgba(0xc8a060ff));
    }

    void drawMover(const Mover& mv) {
        // a cyclist seen from above, riding along a-b
        Vector2 d = Vector2Normalize(mv.vel.x * mv.vel.x + mv.vel.y * mv.vel.y > 1 ? mv.vel : mv.b - mv.a);
        Vector2 n{-d.y, d.x};
        Vector2 p = mv.pos;
        fx::softShadow({p.x + 8, p.y + 10}, 40, 26, 0.45f);
        DrawLineEx(p - d * 40.0f, p + d * 40.0f, 7, col::outline);  // wheels + frame
        DrawLineEx(p - d * 40.0f, p - d * 18.0f, 5, rgba(0x2a2534ff));
        DrawLineEx(p + d * 18.0f, p + d * 40.0f, 5, rgba(0x2a2534ff));
        DrawLineEx(p - d * 16.0f, p + d * 18.0f, 4, rgba(0x8a1a2aff));
        DrawLineEx(p + d * 26.0f + n * 16.0f, p + d * 26.0f - n * 16.0f, 4, col::outline);  // handlebar
        DrawEllipse((int)p.x, (int)p.y, 16, 16, rgba(0x2a7ad8ff));  // shoulders
        DrawCircleV(p - n * 14.0f + d * 6.0f, 6, rgba(0x2a7ad8ff));
        DrawCircleV(p + n * 14.0f + d * 6.0f, 6, rgba(0x2a7ad8ff));
        DrawCircleV(p + d * 6.0f, 10, rgba(0xe8b888ff));  // head
        DrawCircleV(p + d * 4.0f, 9, rgba(0x5a3a24ff));   // hair from above
    }

    void drawBall() {
        // trail
        for (size_t i = 0; i < trail_.size(); i++) {
            float k = (float)i / trail_.size();
            DrawCircleV(trail_[i], kBallR * k * 0.8f, alpha(WHITE, 0.25f * k));
        }
        if (phase_ == Phase::Splash) {
            float k = phaseT_;
            for (int i = 0; i < 3; i++) {
                float r = std::fmod(k * 1.2f + i * 0.3f, 1.0f);
                DrawEllipseLines((int)splashAt_.x, (int)splashAt_.y, 6 + r * 50, 4 + r * 30, alpha(WHITE, 1 - r));
            }
            return;
        }
        Vector2 b = ball_;
        float s = 1;
        if (phase_ == Phase::Sink) {
            float k = clamp01(phaseT_ / 0.35f);
            b = Vector2Lerp(sinkFrom_, course_[hole_].cup, easeInCubic(k));
            s = 1 - 0.6f * k;
            if (phaseT_ > 0.35f) return;
        }
        fx::softShadow({b.x + 4, b.y + 6}, kBallR * 1.2f * s, kBallR * 0.8f * s, 0.5f);
        fx::shadedBall(b, kBallR * s, rgba(0xf8f6ffff), 2);
        DrawCircleV({b.x - 3 * s, b.y - 3 * s}, 2.5f * s, alpha(WHITE, 0.9f));
    }

    void drawAim() {
        if (phase_ != Phase::Aim) return;
        // pulsing ring around a ball that's ready
        float pulse = 0.5f + 0.5f * std::sin(t_ * 5);
        DrawRing(ball_, kBallR + 6 + pulse * 4, kBallR + 8 + pulse * 4, 0, 360, 32, alpha(WHITE, 0.5f));
        if (!aiming_) return;
        Vector2 v = aimVector();
        float k = Vector2Length(v) / kMaxShot;
        if (k <= 0.01f) return;
        Vector2 d = Vector2Normalize(v);
        Color c = mix(col::green, col::red, k);
        // dotted guide in the shot direction
        float len = 60 + k * 260;
        for (float u = 18; u < len; u += 16) {
            float a = 1 - u / len;
            DrawCircleV(ball_ + d * u, 3.5f * a + 1.5f, alpha(WHITE, 0.85f * a));
        }
        // pull-back band from the ball towards the mouse
        Vector2 back = ball_ - d * (k * 120);
        DrawLineEx(ball_, back, 6, alpha(col::outline, 0.6f));
        DrawLineEx(ball_, back, 3, c);
        DrawCircleV(back, 6, c);
        // power meter next to the ball
        Rectangle m{ball_.x + 26, ball_.y - 40, 12, 80};
        DrawRectangleRounded(shrink(m, -2), 1, 6, col::outline);
        DrawRectangleRounded({m.x, m.y + m.height * (1 - k), m.width, m.height * k}, 1, 6, c);
        ui::text(TextFormat("%d%%", (int)(k * 100)), m.x + 20, m.y + m.height * (1 - k), 14, WHITE, ui::Left, true);
    }

    void drawHud() {
        DrawRectangleGradientV(0, 0, VW, 60, alpha(col::bgDeep, 0.92f), alpha(col::bgDeep, 0.75f));
        DrawLineEx({0, 60}, {(float)VW, 60}, 2, alpha(col::green, 0.5f));
        const Hole& h = course_[hole_];
        ui::textShadow("GRACHTENGOLF", 170, 30, 26, rgba(0xbff0a0ff), ui::Left, true, 2);
        ui::text(TextFormat("HOLE %d/9  -  %s", hole_ + 1, h.name), 430, 22, 20, WHITE, ui::Left, true);
        ui::text(TextFormat("PAR %d", h.par), 430, 44, 15, col::muted, ui::Left, true);
        // strokes as balls
        float x = 860;
        ui::text("STROKES", x, 30, 15, col::muted, ui::Left, true);
        for (int i = 0; i < cap(); i++) {
            Vector2 p{x + 86 + i * 20.0f, 30};
            bool used = i < strokes_;
            if (used) fx::shadedBall(p, 7, i >= h.par ? col::red : rgba(0xf8f6ffff), 1.5f);
            else DrawCircleLinesV(p, 7, alpha(WHITE, 0.3f));
        }
        int rel = total() - parSoFar(hole_);
        ui::text(rel == 0 ? "E" : TextFormat("%+d", rel), VW - 30, 30, 26, rel < 0 ? col::green : rel > 0 ? col::orange : WHITE, ui::Right, true);
    }

    static const char* verdict(int strokes, int par) {
        if (strokes == 1) return "HOLE IN ONE!";
        int d = strokes - par;
        if (d <= -3) return "ALBATROS!";
        if (d == -2) return "EAGLE!";
        if (d == -1) return "BIRDIE!";
        if (d == 0) return "PAR";
        if (d == 1) return "BOGEY";
        if (d == 2) return "DOUBLE BOGEY";
        return "OEPS...";
    }

    void drawCard(bool final) {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.55f));
        Rectangle p{VW / 2.0f - 420, 120, 840, final ? 480.0f : 400.0f};
        float k = easeOutBack(clamp01(phaseT_ / 0.35f));
        p.y += (1 - k) * 50;
        ui::panel(p, alpha(col::panel, 0.97f), 0.06f, alpha(col::green, 0.6f));
        if (final) {
            int over = total() - parSoFar(9);
            ui::textOutline("RONDE KLAAR!", VW / 2.0f, p.y + 58, 60, rgba(0xbff0a0ff), col::outline, 5);
            ui::text(TextFormat("%d strokes  (%s)", total(), over == 0 ? "even par" : TextFormat("%+d", over)), VW / 2.0f, p.y + 108, 24, WHITE, ui::Center, true);
        } else {
            int s = scores_[hole_];
            const char* v = verdict(s, course_[hole_].par);
            Color vc = s == 1 ? col::gold : s < course_[hole_].par ? col::green : s == course_[hole_].par ? WHITE : col::orange;
            float pop = 1 + 0.15f * std::max(0.0f, 1 - phaseT_ * 3);
            ui::textOutline(v, VW / 2.0f, p.y + 62, 64 * pop, vc, col::outline, 5);
            ui::text(TextFormat("Hole %d  -  %d strokes  -  par %d", hole_ + 1, s, course_[hole_].par), VW / 2.0f, p.y + 112, 20, col::muted, ui::Center, true);
        }
        // scorecard grid: label column, nine holes, total
        float gx = p.x + 40, gy = p.y + 150, cw = (p.width - 80) / 11;
        const char* rows[3] = {"HOLE", "PAR", "YOU"};
        for (int r = 0; r < 3; r++) {
            Rectangle row{gx, gy + r * 50, p.width - 80, 46};
            float cy = row.y + 23;
            DrawRectangleRounded(row, 0.3f, 6, r % 2 ? alpha(col::bgDeep, 0.6f) : alpha(col::panelHi, 0.6f));
            ui::text(rows[r], gx + cw / 2, cy, 16, col::muted, ui::Center, true);
            for (int i = 0; i < 9; i++) {
                float cx = gx + cw * (i + 1) + cw / 2;
                std::string s = r == 0 ? std::to_string(i + 1) : r == 1 ? std::to_string(course_[i].par) : scores_[i] ? std::to_string(scores_[i]) : "-";
                Color c = WHITE;
                if (r == 2 && scores_[i]) {
                    int d = scores_[i] - course_[i].par;
                    c = scores_[i] == 1 ? col::gold : d < 0 ? col::green : d > 0 ? col::orange : WHITE;
                    if (i == hole_ && !final) DrawCircleV({cx, cy}, 18, alpha(c, 0.25f));
                }
                ui::text(s, cx, cy, 18, c, ui::Center, true);
            }
            std::string tot = r == 0 ? "TOT" : r == 1 ? std::to_string(parSoFar(9)) : std::to_string(total());
            ui::text(tot, gx + cw * 10 + cw / 2, cy, 18, r == 2 ? col::gold : WHITE, ui::Center, true);
        }
        if (final) {
            if (best_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW COURSE RECORD!", VW / 2.0f, p.y + 330, 22, col::gold, ui::Center, true);
            ui::coin({VW / 2.0f - 100, p.y + 368}, 15);
            ui::text("+" + fmtNum(coins_) + " Pietercoins", VW / 2.0f - 76, p.y + 368, 24, col::gold, ui::Left, true);
            if (phaseT_ > 0.6f) {
                if (ui::button({p.x + 150, p.y + 400, 250, 60}, "PLAY AGAIN", rgba(0x2a8a4aff), 24)) {
                    scores_.fill(0);
                    holeInOnes_ = 0;
                    startHole(0);
                }
                if (ui::button({p.x + 440, p.y + 400, 250, 60}, "MENU", col::panelHi, 24)) E().go(makeMenu);
            }
        } else if (phaseT_ > 0.6f) {
            float a = 0.6f + 0.4f * std::sin(t_ * 4);
            ui::text(hole_ == 8 ? "click for the final score" : "click for the next hole", VW / 2.0f, p.y + p.height - 34, 18, alpha(col::green, a), ui::Center, true);
        }
    }

    void draw() override {
        Hole& h = course_[hole_];
        drawGround();
        drawDecor(true);
        drawCourse();
        for (auto& mv : h.movers) drawMover(mv);
        drawAim();
        drawBall();
        for (auto& m : h.mills) drawMill(m);
        drawDecor(false);
        fx_.draw();
        drawHud();
        if (ui::backButton()) E().go(makeMenu);
        if (phase_ == Phase::Intro) {
            float a = clamp01(phaseT_ / 0.25f) * clamp01((1.6f - phaseT_) / 0.3f);
            float slide = (1 - easeOutCubic(clamp01(phaseT_ / 0.4f))) * 200;
            DrawRectangle(0, 300, VW, 120, alpha(col::bgDeep, 0.75f * a));
            ui::textShadow(TextFormat("HOLE %d", hole_ + 1), VW / 2.0f - slide, 340, 54, alpha(WHITE, a), ui::Center, true, 3);
            ui::text(TextFormat("%s  -  par %d", h.name, h.par), VW / 2.0f + slide, 388, 22, alpha(col::gold, a), ui::Center, true);
        }
        if (phase_ == Phase::Aim && strokes_ == 0 && hole_ == 0 && !aiming_)
            ui::textShadow("click and drag back from anywhere, let go to putt", VW / 2.0f, VH - 30, 20, WHITE, ui::Center, true, 2);
        if (phase_ == Phase::Card) drawCard(false);
        if (phase_ == Phase::Final) drawCard(true);
    }

private:
    struct Decor { Vector2 p; float s; int kind; float rot; };
    std::vector<Hole> course_;
    std::array<int, 9> scores_{};
    std::vector<Decor> decor_;
    std::vector<Vector2> trail_;
    Phase phase_ = Phase::Intro;
    int hole_ = 0, strokes_ = 0, holeInOnes_ = 0;
    long long coins_ = 0;
    bool aiming_ = false, autoPlay_ = false, best_ = false;
    Vector2 ball_{}, vel_{}, lastPos_{}, aimFrom_{}, sinkFrom_{}, splashAt_{};
    float t_ = 0, phaseT_ = 0, trailT_ = 0, lastHit_ = 0, lastBoost_ = 0, lastLip_ = 0, lastBell_ = 0, poleLift_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeGolf() { return std::make_unique<GolfScene>(); }

}  // namespace pg
