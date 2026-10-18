// MEEUWEN!: Pieter is a seagull on the beach at Scheveningen and he is hungry.
// Fly along the waterline, dive at tourists and snatch their fries before they
// hide them. Rival gulls want the same snacks (bump them to steal it back),
// kites, patat vendors with spatulas, water pistols and Davi's drones want you
// out of the sky. Drop a little present on someone's head and they drop their
// snack. The day runs from late morning to sunset; keep your belly full.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

namespace pg {
namespace {

constexpr float kHorizon = 330, kShore = 482, kGround = 668;
constexpr int kFoods = 6;
const char* kFoodSpr[kFoods] = {"spr/food_patat", "spr/food_ijs", "spr/food_kroket", "spr/food_haring", "spr/food_frikandel", ""};
const char* kFoodName[kFoods] = {"FRIES", "ICE CREAM", "KROKET", "HERRING", "FRIKANDEL", "BOBA"};
const float kFoodFill[kFoods] = {22, 15, 20, 32, 20, 24};
const int kFoodPts[kFoods] = {100, 80, 120, 220, 120, 160};
const Color kSkin[4] = {rgba(0xf0c8a0ff), rgba(0xc88a5aff), rgba(0xffd8c0ff), rgba(0x8a5a3aff)};
const char* kRobbedLine[] = {"HEY!", "MY FRIES!", "NOOO!", "STUPID BIRD!", "THAT WAS MINE!", "MAMAAA!"};

enum class Role { Tourist, KiteKid, Squirter, Davi };
enum class PState { Calm, Alert, Robbed };

struct Person {
    Role role = Role::Tourist;
    float wx = 0, feet = 600, s = 1;
    int spr = 0, food = -1;
    PState st = PState::Calm;
    float t = 0, notice = 0, react = 1, cool = 0;
    float hide = 0, raise = 0, eatT = 3, face = 1;
    float fire = 1.5f, kitePh = 0;
    bool thrower = false, threw = false, splat = false;
};

struct Stall {
    float wx = 0;
    int cones = 3;
    float swing = -1, cd = 0;  // swing: time since the windup started
};

struct Prop { int kind; float wx, y, s; int var; };  // 0 parasol, 1 windbreak, 2 sandcastle, 3 flag, 4 ball

struct Drone { float wx, y, ph; };

struct Rival {
    float wx, y, vx = -200, vy = 0, flap = 0;
    int carry = -1;
    int state = 0;  // 0 seek, 1 flee, 2 tumble
    float t = 0;
};

struct Shot { float wx, y, vx, vy, rot; bool slipper; };
struct Poop { float wx, y, vx, vy; };
struct Loose { float wx, y, vy, ground; int food; bool landed; float life; };

enum class Phase { Ready, Play, Down, Over };

class MeeuwScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Play) return {"Meeuwen!", fmtNum(score_) + " points, " + std::to_string(eaten_) + " snacks stolen"};
        return {"Meeuwen!", "Best: " + fmtNum(P().stats.meeuwBest)};
    }

    void enter() override {
        reset();
        if (std::getenv("PG_MEEUW")) {  // dev hook: skip the start screen, optionally fast-forward the day
            phase_ = Phase::Play;
            elapsed_ = (float)std::atof(std::getenv("PG_MEEUW"));
        }
        bot_ = std::getenv("PG_MEEUW_BOT") != nullptr;
    }
    void exit() override { E().sfx.stop(Sfx::Surf); }

    void reset() {
        people_.clear();
        stalls_.clear();
        props_.clear();
        drones_.clear();
        rivals_.clear();
        shots_.clear();
        poops_.clear();
        loose_.clear();
        fx_.clear();
        cam_ = 0;
        spawnX_ = 700;
        gull_ = {360, 260};
        vy_ = 0;
        boost_ = 0;
        speed_ = 250;
        hunger_ = 100;
        feathers_ = 3;
        ammo_ = 3;
        score_ = 0;
        eaten_ = 0;
        combo_ = 0;
        bestCombo_ = 0;
        comboT_ = 0;
        inv_ = 0;
        elapsed_ = 0;
        rivalT_ = 8;
        diving_ = false;
        starved_ = false;
        phase_ = Phase::Ready;
        phaseT_ = 0;
        // fill the first screen with scenery
        for (float x = -200; x < 900; x += rnd(160, 260)) addProp(x);
    }

    float diff() const { return clamp01(elapsed_ / 200); }
    float hour() const { return std::min(21.6f, 11 + elapsed_ / 240 * 10.5f); }
    float gx() const { return cam_ + gull_.x; }  // the gull in world x
    Vector2 beak() const {
        float a = tilt_ * DEG2RAD;
        Vector2 o{46, -10};
        return {gull_.x + o.x * std::cos(a) - o.y * std::sin(a), gull_.y + o.x * std::sin(a) + o.y * std::cos(a)};
    }

    // ------------------------------------------------------------ spawning
    void addProp(float wx) {
        Prop p{rndi(0, 9) < 4 ? 0 : rndi(1, 4), wx, rnd(508, 548), rnd(0.62f, 0.78f), rndi(0, 5)};
        if (p.kind == 2 || p.kind == 4) p.y = rnd(590, 700), p.s = rnd(0.8f, 1.0f);
        props_.push_back(p);
    }

    Person tourist(float wx) {
        Person p;
        p.wx = wx;
        p.feet = rnd(575, 700);
        p.s = lerpf(0.78f, 1.0f, (p.feet - 575) / 125);
        p.spr = rndi(0, 3);
        int r = rndi(0, 99);
        p.food = r < 34 ? 0 : r < 52 ? 1 : r < 68 ? 2 : r < 80 ? 4 : 3;
        p.react = lerpf(1.05f, 0.5f, diff()) * rnd(0.8f, 1.25f);
        p.eatT = rnd(1, 5);
        p.thrower = chance(0.15f + diff() * 0.45f);
        return p;
    }

    void spawnChunk() {
        float d = diff(), x = spawnX_;
        int pat = rndi(0, 99);
        if (pat < 44) {  // a few tourists with snacks
            int n = rndi(1, d > 0.4f ? 3 : 2);
            for (int i = 0; i < n; i++) people_.push_back(tourist(x + i * rnd(150, 230)));
        } else if (pat < 58) {  // the patat stand
            stalls_.push_back({x + 120});
            people_.push_back(tourist(x + 420));
        } else if (pat < 70 && d > 0.08f) {  // a kid with a kite
            Person k;
            k.role = Role::KiteKid;
            k.wx = x;
            k.feet = rnd(590, 650);
            k.s = 0.62f;
            k.spr = rndi(0, 3);
            k.kitePh = rnd(0, 10);
            people_.push_back(k);
            people_.push_back(tourist(x + rnd(330, 420)));
        } else if (pat < 80 && d > 0.2f) {  // a kid with a water pistol between tourists
            people_.push_back(tourist(x));
            Person k;
            k.role = Role::Squirter;
            k.wx = x + 220;
            k.feet = rnd(600, 680);
            k.s = 0.64f;
            k.spr = rndi(0, 3);
            k.fire = 0.8f;
            people_.push_back(k);
            people_.push_back(tourist(x + 420));
        } else if (pat < 90 && d > 0.3f) {  // one of Davi's drones patrols the beach
            drones_.push_back({x + 200, rnd(220, 470), rnd(0, 10)});
            people_.push_back(tourist(x + 300));
        } else if (pat < 95) {  // Davi himself, asleep in a beach chair with a boba
            Person p;
            p.role = Role::Davi;
            p.wx = x + 100;
            p.feet = rnd(620, 680);
            p.s = 1;
            p.food = 5;
            p.react = 99;
            people_.push_back(p);
        } else {
            people_.push_back(tourist(x));
        }
        for (int i = rndi(1, 2); i > 0; i--) addProp(x + rnd(-100, 500));
        spawnX_ += rnd(430, 640) * lerpf(1.0f, 0.82f, d);
    }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        fx_.update(dt);
        E().sfx.loop(Sfx::Surf, 0.45f);
        flap_ += dt * (diving_ ? 0 : vy_ < -120 ? 15 : 8);
        if (phase_ == Phase::Ready) {
            cam_ += dt * 80;
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
                phase_ = Phase::Play;
                phaseT_ = 0;
                E().sfx.play(Sfx::Gull, 0.7f);
            }
            if (E().backPressed()) E().go(makeMenu);
            return;
        }
        if (phase_ == Phase::Over) {
            if (E().backPressed()) E().go(makeMenu);
            return;
        }
        if (phase_ == Phase::Down) {
            // the gull tumbles onto the sand
            vy_ += 1300 * dt;
            gull_.y = std::min(kGround, gull_.y + vy_ * dt);
            tilt_ += dt * 420;
            if (gull_.y >= kGround && !landed_) {
                landed_ = true;
                E().sfx.play(Sfx::Thud, 0.7f);
                for (int i = 0; i < 16; i++) puff({gull_.x + rnd(-30, 30), kGround + 6}, rgba(0xe8d4a8ff));
            }
            updateWorld(dt * 0.4f);
            if (phaseT_ > 1.8f) gameOver();
            return;
        }
        if (E().backPressed()) {
            E().go(makeMenu);
            return;
        }
        elapsed_ += dt;
        float d = diff();
        speed_ = lerpf(250, 380, d);
        hunger_ -= dt * lerpf(2.4f, 4.2f, d);
        if (hunger_ <= 0) {
            hunger_ = 0;
            starved_ = true;
            fall();
            return;
        }
        inv_ = std::max(0.0f, inv_ - dt);
        chew_ = std::max(0.0f, chew_ - dt);
        comboT_ -= dt;
        if (comboT_ <= 0) combo_ = 0;
        steer(dt);
        updateWorld(dt);
        if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_F) || E().rightPressed()) dropPoop();
        rivalT_ -= dt;
        if (rivalT_ <= 0) {
            rivalT_ = lerpf(11, 5, d) * rnd(0.8f, 1.2f);
            rivals_.push_back({cam_ + VW + 60, rnd(110, 280)});
        }
    }

    // Dev autopilot (PG_MEEUW_BOT=1): steers for the nearest snack and dives at it.
    void autopilot(bool& up, bool& down, bool& dive) {
        Vector2 best{-1, -1};
        float bd = 1e9f;
        for (auto& p : people_)
            if (p.food >= 0 && p.hide < 0.35f) {
                Vector2 f = foodPos(p);
                if (f.x < gull_.x + 20 || f.x > gull_.x + 520) continue;
                if (f.x - gull_.x < bd) { bd = f.x - gull_.x; best = f; }
            }
        float want = best.x < 0 ? 200 : bd > 260 ? 180 : best.y;
        dive = best.x >= 0 && bd < 300 && gull_.y < best.y - 40 && (diving_ || bd > 150);
        up = !dive && gull_.y > want + 20;
        down = !dive && gull_.y < want - 20;
    }

    void steer(float dt) {
        bool up = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP), down = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
        bool dive = IsKeyDown(KEY_SPACE) || E().mouseDown();
        if (bot_) autopilot(up, down, dive);
        if (dive && !diving_ && gull_.y < kGround - 20) {
            diving_ = true;
            E().sfx.play(Sfx::Whoosh, 0.45f, 0.8f);
        }
        if (diving_ && !dive) {
            diving_ = false;
            if (vy_ > 260) {  // pull out of the dive: swoop up with the speed you built
                vy_ = -vy_ * 0.72f;
                swoop_ = 0.6f;
                E().sfx.play(Sfx::Whoosh, 0.55f, 1.3f);
            }
        }
        swoop_ = std::max(0.0f, swoop_ - dt);
        if (diving_) {
            vy_ = std::min(860.0f, vy_ + 1700 * dt);
            boost_ = approach(boost_, 230, 500 * dt);
            if (chance(dt * 30)) fx_.add(Particle{gull_ + Vector2{-30, rnd(-6, 6)}, {-260, 0}, 0.3f, 0.3f, 3, 0, 0, 0, 0, 0, alpha(WHITE, 0.6f)});
        } else {
            float target = up ? -330.0f : down ? 330.0f : 45.0f;
            vy_ = damp(vy_, target, swoop_ > 0 ? 1.6f : 4.5f, dt);
            boost_ = approach(boost_, 0, 160 * dt);
        }
        if (up && !diving_ && std::fmod(flap_, 2 * PI) < std::fmod(flap_ - dt * 15, 2 * PI)) E().sfx.play(Sfx::Flap, 0.25f, rnd(0.9f, 1.15f));
        gull_.y += vy_ * dt;
        if (gull_.y < 70) { gull_.y = 70; vy_ = std::max(vy_, 0.0f); }
        if (gull_.y > kGround - 10) {
            gull_.y = kGround - 10;
            if (vy_ > 300) {
                for (int i = 0; i < 10; i++) puff({gull_.x + rnd(-20, 30), kGround}, rgba(0xe8d4a8ff));
                E().sfx.play(Sfx::Thud, 0.35f, 1.3f);
            }
            vy_ = std::min(vy_, 0.0f);
            diving_ = false;
        }
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) gull_.x += 320 * dt;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) gull_.x -= 320 * dt;
        gull_.x = std::clamp(gull_.x, 140.0f, 860.0f);
        tilt_ = damp(tilt_, std::clamp(vy_ * 0.055f, -28.0f, 55.0f), 10, dt);
    }

    void updateWorld(float dt) {
        float move = (speed_ + boost_) * dt;
        cam_ += move;
        while (spawnX_ < cam_ + VW + 500) spawnChunk();
        Vector2 bk = beak();
        float d = diff();
        bool playing = phase_ == Phase::Play;

        for (auto& p : people_) {
            p.t += dt;
            p.cool = std::max(0.0f, p.cool - dt);
            float sx = p.wx - cam_;
            float dx = p.wx - gx();
            if (p.role == Role::Tourist && p.food >= 0) {
                // eating: lift the snack to the mouth now and then
                p.eatT -= dt;
                if (p.eatT < 0) { p.raise = approach(p.raise, 1, dt * 4); if (p.eatT < -0.9f) p.eatT = rnd(2, 6); }
                else p.raise = approach(p.raise, 0, dt * 4);
                if (p.st == PState::Calm) {
                    bool near = playing && dx > -70 && dx < 330 && gull_.y > 240;
                    if (near && p.cool <= 0) p.notice += dt * (diving_ ? 0.55f : 1.0f);
                    else p.notice = std::max(0.0f, p.notice - dt);
                    if (p.notice > p.react) {
                        p.st = PState::Alert;
                        p.t = 0;
                        p.face = -1;
                        E().sfx.playAt(Sfx::Error, sx, 0.25f, 1.6f);
                    }
                } else if (p.st == PState::Alert) {
                    p.hide = approach(p.hide, 1, dt * 6);
                    if (p.t > lerpf(1.6f, 2.4f, d)) {
                        p.st = PState::Calm;
                        p.face = 1;
                        p.notice = 0;
                        p.cool = 0.8f;
                    }
                }
                if (p.st == PState::Calm) p.hide = approach(p.hide, 0, dt * 3);
                // snatch!
                if (playing && p.hide < 0.35f && Vector2Distance(bk, foodPos(p)) < 26 * p.s + 10) {
                    rob(p, true);
                }
            }
            if (p.role == Role::Davi && p.food >= 0 && playing && Vector2Distance(bk, foodPos(p)) < 34) rob(p, true);
            if (p.st == PState::Robbed && p.thrower && !p.threw && p.t > 0.9f && playing && dx > -500) {
                // angry tourists throw a slipper at you
                p.threw = true;
                Vector2 from{sx, p.feet - 120 * p.s};
                Vector2 aim = gull_ + Vector2{0, -20};
                float T = 0.8f;
                float vx = (aim.x - from.x) / T + speed_, vy = (aim.y - from.y - 0.5f * 700 * T * T) / T;
                shots_.push_back({p.wx, from.y, vx, vy, 0, true});
                E().sfx.playAt(Sfx::Whoosh, sx, 0.4f, 1.6f);
            }
            if (p.role == Role::Squirter && playing) {
                p.fire -= dt;
                if (p.fire <= 0 && dx > -40 && dx < 520) {
                    p.fire = lerpf(2.4f, 1.4f, d);
                    Vector2 from{sx + 26 * p.s, p.feet - 70 * p.s};
                    float T = 0.65f;
                    Vector2 aim{gull_.x, gull_.y + vy_ * T * 0.5f};
                    float vx = (aim.x - from.x) / T + speed_ + boost_, vy = (aim.y - from.y - 0.5f * 500 * T * T) / T;
                    shots_.push_back({p.wx + 26 * p.s, from.y, vx, vy, 0, false});
                    E().sfx.playAt(Sfx::Water, sx, 0.35f, 1.8f);
                }
            }
            if (p.role == Role::KiteKid && playing && inv_ <= 0) {
                Vector2 k = kitePos(p), h = kidHand(p);
                Vector2 seg = k - h;
                float u = std::clamp(Vector2DotProduct(gull_ - h, seg) / Vector2DotProduct(seg, seg), 0.0f, 1.0f);
                if (Vector2Distance(gull_, k) < 40 || Vector2Distance(gull_, h + seg * u) < 14) hurt("TANGLED!");
            }
        }
        std::erase_if(people_, [&](const Person& p) { return p.wx < cam_ - 300; });

        for (auto& s : stalls_) {
            float sx = s.wx - cam_;
            s.cd -= dt;
            float dx = s.wx - gx();
            if (s.swing < 0 && s.cd <= 0 && playing && dx > -90 && dx < 230 && gull_.y > 280 && gull_.y < 540) {
                s.swing = 0;
                E().sfx.playAt(Sfx::Error, sx, 0.3f);
            }
            if (s.swing >= 0) {
                s.swing += dt;
                if (s.swing > 0.45f && s.swing < 0.72f && playing && inv_ <= 0 && Vector2Distance(gull_, spatula(s)) < 40) hurt("WHACK!");
                if (s.swing > 0.45f && s.swing - dt <= 0.45f) E().sfx.playAt(Sfx::Whoosh, sx, 0.5f, 1.4f);
                if (s.swing > 0.95f) { s.swing = -1; s.cd = 1.4f; }
            }
            for (int i = 0; i < s.cones; i++)
                if (playing && Vector2Distance(bk, conePos(s, i)) < 26) {
                    s.cones--;
                    eat(0, conePos(s, i));
                    fx_.floatText("HEY!", {sx + 40, 340}, col::red, 22);
                    break;
                }
        }
        std::erase_if(stalls_, [&](const Stall& s) { return s.wx < cam_ - 400; });
        std::erase_if(props_, [&](const Prop& p) { return p.wx < cam_ - 300; });

        for (auto& dr : drones_) {
            dr.ph += dt;
            dr.wx += std::sin(dr.ph * 0.9f) * 120 * dt + 60 * dt;
            float y = dr.y + std::sin(dr.ph * 2.1f) * 50;
            if (playing && inv_ <= 0 && Vector2Distance(gull_, {dr.wx - cam_, y}) < 44) {
                hurt("ZAPPED!");
                E().sfx.play(Sfx::Zap, 0.7f);
            }
        }
        std::erase_if(drones_, [&](const Drone& d) { return d.wx < cam_ - 300; });

        for (auto& r : rivals_) updateRival(r, dt);
        std::erase_if(rivals_, [&](const Rival& r) { return r.wx < cam_ - 300 || r.y < -150 || r.y > VH + 100; });

        for (auto& s : shots_) {
            s.wx += s.vx * dt;
            s.vy += (s.slipper ? 700 : 500) * dt;
            s.y += s.vy * dt;
            s.rot += dt * 720;
            if (playing && inv_ <= 0 && Vector2Distance(gull_, {s.wx - cam_, s.y}) < (s.slipper ? 30 : 26)) {
                hurt(s.slipper ? "SLIPPERED!" : "SOAKED!");
                s.y = VH + 200;
            }
        }
        std::erase_if(shots_, [](const Shot& s) { return s.y > VH + 40; });

        for (auto& pp : poops_) {
            pp.wx += pp.vx * dt;
            pp.vy += 1000 * dt;
            pp.y += pp.vy * dt;
            for (auto& p : people_) {
                if (p.splat) continue;
                float sx = p.wx - cam_, top = p.role == Role::Davi ? p.feet - 90 : p.feet - 165 * p.s;
                float bottom = p.role == Role::Davi ? p.feet - 20 : p.feet - 70 * p.s;
                if (std::abs(pp.wx - cam_ - sx) < (p.role == Role::Davi ? 60 : 30 * p.s) && pp.y > top && pp.y < bottom) {
                    splat(p, {pp.wx - cam_, pp.y});
                    pp.y = VH + 100;
                    break;
                }
            }
            if (pp.y > 700 && pp.y < VH + 50) {
                fx_.burst({pp.wx - cam_, 700}, 5, WHITE, 80, 3, 0.3f);
                pp.y = VH + 100;
            }
        }
        std::erase_if(poops_, [](const Poop& p) { return p.y > VH + 40; });

        for (auto& l : loose_) {
            if (!l.landed) {
                l.vy += 900 * dt;
                l.y += l.vy * dt;
                if (l.y >= l.ground) { l.y = l.ground; l.landed = true; E().sfx.playAt(Sfx::Drop, l.wx - cam_, 0.3f); }
            } else {
                l.life -= dt;
            }
            if (playing && Vector2Distance(bk, {l.wx - cam_, l.y}) < 34) {
                eat(l.food, {l.wx - cam_, l.y});
                if (!l.landed) fx_.floatText("MID-AIR!", {l.wx - cam_, l.y - 40}, col::cyan, 22), score_ += 100;
                l.life = -1;
            }
        }
        std::erase_if(loose_, [&](const Loose& l) { return l.life < 0 || l.wx < cam_ - 200; });
    }

    void updateRival(Rival& r, float dt) {
        r.t += dt;
        r.flap += dt * (r.state == 1 ? 16 : 10);
        if (r.state == 2) {  // tumbling after a bump
            r.vy += 900 * dt;
            r.y += r.vy * dt;
            r.wx += r.vx * dt;
            if (r.t > 0.7f) { r.state = 1; r.vy = -200; r.t = 0; }
            return;
        }
        if (r.state == 0) {
            // go for the nearest visible snack ahead of the camera
            Vector2 best{};
            float bd = 1e9f;
            Person* who = nullptr;
            Loose* lo = nullptr;
            for (auto& p : people_)
                if (p.food >= 0 && p.hide < 0.35f && p.role != Role::Davi) {
                    Vector2 f = foodPos(p);
                    float sx = f.x;
                    if (sx < 200 || sx > VW + 40) continue;
                    float dd = Vector2Distance(f, {r.wx - cam_, r.y});
                    if (dd < bd) { bd = dd; best = f; who = &p; lo = nullptr; }
                }
            for (auto& l : loose_) {
                Vector2 f{l.wx - cam_, l.y};
                if (f.x < 200) continue;
                float dd = Vector2Distance(f, {r.wx - cam_, r.y});
                if (dd < bd) { bd = dd; best = f; lo = &l; who = nullptr; }
            }
            Vector2 me{r.wx - cam_, r.y};
            if (who || lo) {
                Vector2 dir = Vector2Normalize(best - me);
                r.vx = damp(r.vx, dir.x * 300 + speed_ * 0.3f, 3, dt);
                r.vy = damp(r.vy, dir.y * 300, 3, dt);
                if (bd < 22) {
                    if (who) {
                        r.carry = who->food;
                        rob(*who, false);
                    } else {
                        r.carry = lo->food;
                        lo->life = -1;
                    }
                    r.state = 1;
                    r.t = 0;
                    E().sfx.playAt(Sfx::Gull, me.x, 0.55f, 1.1f);
                }
            } else {
                r.vx = damp(r.vx, -120, 2, dt);
                r.vy = damp(r.vy, std::sin(r.t * 2) * 60, 2, dt);
            }
            if (me.x < -60) r.state = 1;
        } else {
            r.vx = damp(r.vx, speed_ + 160, 2, dt);
            r.vy = damp(r.vy, -220, 2, dt);
        }
        r.wx += r.vx * dt;
        r.y += r.vy * dt;
        r.y = std::min(r.y, kGround - 20);
        // bump into a rival: steal what it carries, or knock it about when diving
        Vector2 me{r.wx - cam_, r.y};
        if (phase_ == Phase::Play && Vector2Distance(me, gull_) < 44 && (r.carry >= 0 || diving_ || swoop_ > 0)) {
            if (r.carry >= 0) {
                eat(r.carry, me);
                fx_.floatText("STOLEN BACK!", {me.x, me.y - 50}, col::gold, 24);
                score_ += 80;
                r.carry = -1;
            } else {
                fx_.floatText("BONK!", {me.x, me.y - 40}, WHITE, 24);
                score_ += 50;
            }
            r.state = 2;
            r.t = 0;
            r.vx = speed_ * 0.5f;
            r.vy = -150;
            featherPuff(me, 8);
            E().sfx.play(Sfx::Gull, 0.7f, 1.4f);
            E().sfx.play(Sfx::Pop, 0.5f);
            E().shake(4);
        }
    }

    // ------------------------------------------------------------ events
    void rob(Person& p, bool byPlayer) {
        int f = p.food;
        Vector2 at = foodPos(p);
        p.food = -1;
        p.st = PState::Robbed;
        p.t = 0;
        p.face = -1;
        float sx = p.wx - cam_;
        if (p.role == Role::Davi) {
            fx_.floatText("MY BOBA!!", {sx, p.feet - 130}, col::cyan, 26);
            E().sfx.play(Sfx::Error, 0.6f);
        } else {
            fx_.floatText(kRobbedLine[rndi(0, 5)], {sx, p.feet - 190 * p.s}, WHITE, 22);
            E().sfx.playAt(Sfx::Scream, sx, 0.18f, rnd(1.4f, 1.8f));
        }
        if (byPlayer) eat(f, at);
    }

    void eat(int food, Vector2 at) {
        combo_ = comboT_ > 0 ? combo_ + 1 : 1;
        comboT_ = 3.5f;
        bestCombo_ = std::max(bestCombo_, combo_);
        int pts = kFoodPts[food] * std::min(combo_, 8);
        score_ += pts;
        eaten_++;
        hunger_ = std::min(100.0f, hunger_ + kFoodFill[food]);
        ammo_ = std::min(3, ammo_ + 1);
        chew_ = 0.5f;
        E().sfx.play(Sfx::Chew, 0.6f, rnd(1.0f, 1.2f));
        E().sfx.play(Sfx::Coin, 0.25f, 1 + std::min(combo_, 8) * 0.06f);
        if (combo_ >= 3) E().sfx.play(Sfx::Gull, 0.5f, 1.2f);
        fx_.burst(at, 10, food == 1 ? rgba(0xffd0e0ff) : col::wafel, 180, 4, 0.4f, PShape::Square, 400);
        fx_.floatText(TextFormat("+%d %s", pts, kFoodName[food]), {at.x, at.y - 30}, combo_ > 1 ? col::gold : WHITE, 20 + std::min(combo_, 6) * 2.0f);
    }

    void splat(Person& p, Vector2 at) {
        p.splat = true;
        E().sfx.play(Sfx::Splat, 0.8f);
        fx_.burst(at, 12, WHITE, 160, 4, 0.4f, PShape::Circle, 300);
        int pts = p.role == Role::Davi ? 300 : 40;
        score_ += pts;
        fx_.floatText(p.role == Role::Davi ? "BULLSEYE DAVI! +300" : "SPLAT! +40", {at.x, at.y - 40}, p.role == Role::Davi ? col::cyan : WHITE, 24);
        if (p.food >= 0) {
            // they drop the snack: catch it before it hits the sand
            Vector2 f = foodPos(p);
            loose_.push_back({p.wx + (f.x - (p.wx - cam_)), f.y, -260, p.feet - 4, p.food, false, 4.5f});
            p.food = -1;
            p.st = PState::Robbed;
            p.t = 0;
            fx_.floatText("EW!", {p.wx - cam_, p.feet - 200 * p.s}, WHITE, 22);
        }
    }

    void dropPoop() {
        if (ammo_ <= 0) {
            E().sfx.play(Sfx::Error, 0.3f, 1.5f);
            return;
        }
        ammo_--;
        poops_.push_back({gx() - 18, gull_.y + 16, speed_ + boost_ * 0.8f, std::max(vy_, 0.0f) * 0.5f + 80});
        E().sfx.play(Sfx::Pop, 0.4f, 0.7f);
    }

    void hurt(const char* why) {
        if (inv_ > 0 || phase_ != Phase::Play) return;
        feathers_--;
        inv_ = 1.6f;
        combo_ = 0;
        E().shake(9);
        E().sfx.play(Sfx::Hurt, 0.7f);
        E().sfx.play(Sfx::Gull, 0.7f, 1.6f);
        featherPuff(gull_, 14);
        fx_.floatText(why, {gull_.x, gull_.y - 50}, col::red, 28);
        vy_ = 120;
        diving_ = false;
        if (feathers_ <= 0) fall();
    }

    void fall() {
        phase_ = Phase::Down;
        phaseT_ = 0;
        vy_ = -200;
        landed_ = false;
        E().sfx.play(Sfx::Lose, 0.5f);
    }

    void gameOver() {
        if (bot_) std::printf("meeuw bot: score %d, %d snacks, best combo %d, survived %.0fs (%s)\n", score_, eaten_, bestCombo_, elapsed_, starved_ ? "starved" : "plucked");
        phase_ = Phase::Over;
        phaseT_ = 0;
        best_ = score_ > P().stats.meeuwBest;
        if (bot_) {
            E().quit();
            return;
        }
        P().stats.meeuwBest = std::max(P().stats.meeuwBest, score_);
        earned_ = score_ / 40 + eaten_;
        if (earned_ > 0) P().earn(earned_);
        P().save();
        E().sfx.play(best_ ? Sfx::Win : Sfx::Lose, 0.7f);
    }

    void puff(Vector2 at, Color c) {
        Particle p;
        p.pos = at;
        p.vel = {rnd(-120, 120), rnd(-140, -30)};
        p.life = p.maxLife = rnd(0.4f, 0.8f);
        p.size = rnd(4, 9);
        p.endSize = 12;
        p.drag = 3;
        p.color = alpha(c, 0.7f);
        fx_.add(p);
    }

    void featherPuff(Vector2 at, int n) {
        for (int i = 0; i < n; i++) {
            Particle p;
            p.pos = at;
            p.vel = rndDir() * rnd(60, 260);
            p.life = p.maxLife = rnd(0.8f, 1.4f);
            p.size = rnd(5, 8);
            p.endSize = 4;
            p.gravity = 120;
            p.drag = 2.5f;
            p.rot = rnd(0, 360);
            p.rotVel = rnd(-400, 400);
            p.shape = PShape::Square;
            p.color = chance(0.6f) ? WHITE : rgba(0xaab2c4ff);
            fx_.add(p);
        }
    }

    // ------------------------------------------------------------ geometry
    Vector2 shoulder(const Person& p) const { return {p.wx - cam_ + 15 * p.s * p.face, p.feet - 85 * p.s - 27 * p.s}; }
    Vector2 hand(const Person& p) const {
        Vector2 sh = shoulder(p);
        if (p.role == Role::Davi) return {p.wx - cam_ + 44, p.feet - 62};
        Vector2 out{20, 30}, mouth{14, -10}, back{-30, 24};
        Vector2 o = Vector2Lerp(Vector2Lerp(out, mouth, easeInOutSine(p.raise)), back, easeInOutSine(p.hide));
        if (p.st == PState::Robbed) o = {6 + std::sin(p.t * 18) * 6, -40};  // fist in the air
        return {sh.x + o.x * p.s * p.face, sh.y + o.y * p.s};
    }
    Vector2 foodPos(const Person& p) const {
        Vector2 h = hand(p);
        if (p.role == Role::Davi) return {h.x, h.y - 14};
        return {h.x + 4 * p.s * p.face, h.y - 12 * p.s};
    }
    Vector2 kidHand(const Person& p) const { return {p.wx - cam_ + 14 * p.s, p.feet - 120 * p.s}; }
    Vector2 kitePos(const Person& p) const {
        return {p.wx - cam_ + 150 + std::sin(t_ * 0.7f + p.kitePh) * 70, 180 + std::sin(t_ * 1.13f + p.kitePh) * 60};
    }
    Vector2 conePos(const Stall& s, int i) const { return {s.wx - cam_ - 64 + i * 46.0f, 438}; }
    Vector2 spatula(const Stall& s) const {
        Vector2 sh{s.wx - cam_ - 20, 410};
        float a;
        if (s.swing < 0) a = -60;
        else if (s.swing < 0.45f) a = lerpf(-60, -150, easeOutCubic(s.swing / 0.45f));  // wind up
        else a = lerpf(-150, 10, easeOutCubic((s.swing - 0.45f) / 0.25f));             // whack
        return sh + fromAngle(a * DEG2RAD, 78);
    }

    // ------------------------------------------------------------ draw: world
    Color skyAt(float h, bool top) const {
        struct K { float h; uint32_t top, bot; };
        static const K ks[] = {{11, 0x3a8ee8ff, 0xc4e6ffff}, {17.5f, 0x4a86e0ff, 0xd8ecffff}, {19, 0x5a6ad0ff, 0xffc890ff},
                               {20.2f, 0x3a2a7aff, 0xff7a5aff}, {21, 0x141440ff, 0x8a3a6aff}, {21.6f, 0x080826ff, 0x2a2a5aff}};
        for (int i = 0; i < 5; i++)
            if (h <= ks[i + 1].h) {
                float k = (h - ks[i].h) / (ks[i + 1].h - ks[i].h);
                return mix(rgba(top ? ks[i].top : ks[i].bot), rgba(top ? ks[i + 1].top : ks[i + 1].bot), k);
            }
        return rgba(top ? ks[5].top : ks[5].bot);
    }
    float night() const { return clamp01((hour() - 20.2f) / 1.3f); }
    float dusk() const { return clamp01((hour() - 18) / 2.5f); }

    void cloud(Vector2 p, float sc, Color lit, Color shade) {
        const float px[] = {-52, -22, 12, 44}, py[] = {-4, -22, -30, -10}, pr[] = {26, 34, 38, 28};
        DrawEllipse((int)p.x, (int)(p.y + 6 * sc), 80 * sc, 16 * sc, shade);
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * sc, p.y + py[i] * sc + 4 * sc}, pr[i] * sc, shade);
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * sc, p.y + py[i] * sc}, pr[i] * sc, lit);
        DrawEllipse((int)p.x, (int)p.y, 78 * sc, 14 * sc, lit);
    }

    void drawPier(float x) {
        // De Pier at Scheveningen with its Ferris wheel, far out on the water
        Color c = mix(rgba(0x8a9ab8ff), rgba(0x2a2a4aff), night() * 0.7f + dusk() * 0.2f);
        Color lit = mix(rgba(0xe8eef8ff), rgba(0x5a5a7aff), night() * 0.6f + dusk() * 0.2f);
        for (float lx = x; lx < x + 760; lx += 34) DrawRectangle((int)lx, 300, 3, 34, c);
        DrawRectangle((int)x, 292, 760, 10, c);
        DrawRectangle((int)x + 80, 270, 160, 24, lit);
        DrawRectangle((int)x + 300, 262, 120, 32, lit);
        DrawCircleSector({x + 360, 262}, 40, 180, 360, 20, lit);
        DrawRectangle((int)x + 470, 276, 120, 18, lit);
        Vector2 hub{x + 660, 196};
        float R = 86, rot = t_ * 0.15f;
        DrawLineEx({hub.x - 30, 294}, hub, 4, c);
        DrawLineEx({hub.x + 30, 294}, hub, 4, c);
        ui::circleOutline(hub, R, c, 3);
        for (int i = 0; i < 18; i++) {
            Vector2 e = hub + fromAngle(rot + i * 2 * PI / 18, R);
            DrawLineEx(hub, e, 1.2f, alpha(c, 0.8f));
            Color g = night() > 0.1f ? (i % 3 == 0 ? col::pink : i % 3 == 1 ? col::cyan : col::gold) : lit;
            DrawRectangleRec({e.x - 5, e.y, 10, 9}, g);
        }
        if (night() > 0.05f) {
            fx::beginAdd();
            for (int i = 0; i < 18; i++) {
                Vector2 e = hub + fromAngle(rot + i * 2 * PI / 18, R);
                fx::glow(e, 16, alpha(i % 3 == 0 ? col::pink : i % 3 == 1 ? col::cyan : col::gold, 0.6f * night()));
            }
            for (float lx = x + 20; lx < x + 600; lx += 60) fx::glow({lx, 294}, 22, alpha(col::gold, 0.5f * night()));
            fx::endAdd();
        }
    }

    void drawSky() {
        float h = hour();
        DrawRectangleGradientV(0, 0, VW, (int)kHorizon, skyAt(h, true), skyAt(h, false));
        // stars at night
        if (night() > 0) {
            for (int i = 0; i < 70; i++) {
                float x = std::fmod(i * 197.3f, (float)VW), y = std::fmod(i * 71.7f, 260.0f);
                float tw = 0.6f + 0.4f * std::sin(t_ * 2 + i);
                DrawCircleV({x, y}, 1.2f, alpha(WHITE, night() * tw * 0.8f));
            }
            DrawCircleV({220, 90}, 26, alpha(rgba(0xf4f0d8ff), night()));
            DrawCircleV({232, 84}, 24, alpha(skyAt(h, true), night()));
        }
        // the sun sinks into the sea
        float k = clamp01((h - 11) / 9.9f);
        Vector2 sun{lerpf(760, 1060, k), 70 + 290 * k * k};
        Color sc = mix(rgba(0xfff6d0ff), rgba(0xff7a3aff), dusk());
        fx::beginAdd();
        fx::glow(sun, 260 + dusk() * 140, alpha(mix(rgba(0xfff0c0ff), rgba(0xff8a4aff), dusk()), 0.7f * (1 - night())));
        fx::endAdd();
        DrawCircleV(sun, 40 + dusk() * 10, alpha(sc, 1 - night()));
        // clouds
        Color lit = mix(WHITE, rgba(0xffb08aff), dusk()), shade = mix(rgba(0xc8d8f0ff), rgba(0xb05a7aff), dusk());
        lit = mix(lit, rgba(0x2a2a50ff), night());
        shade = mix(shade, rgba(0x1a1a38ff), night());
        for (int i = 0; i < 6; i++) {
            float x = std::fmod(i * 330 - cam_ * 0.04f - t_ * 5, 1700.0f);
            if (x < -200) x += 1700;
            cloud({x - 150, 60.0f + (i * 37) % 120}, 0.55f + (i % 3) * 0.2f, alpha(lit, 0.95f), alpha(shade, 0.95f));
        }
    }

    void drawSea() {
        float h = hour();
        Color top = mix(mix(rgba(0x3a7ab8ff), rgba(0x5a5a9aff), dusk()), rgba(0x101838ff), night());
        Color bot = mix(mix(rgba(0x2aa0b8ff), rgba(0x6a6aa8ff), dusk()), rgba(0x1a2a4aff), night());
        DrawRectangleGradientV(0, (int)kHorizon, VW, (int)(kShore - kHorizon), top, bot);
        DrawRectangle(0, (int)kHorizon, VW, 2, alpha(skyAt(h, false), 0.8f));
        // sailboats on the horizon
        for (int i = 0; i < 3; i++) {
            float x = std::fmod(i * 640 + 300 - cam_ * 0.06f + t_ * 8, 1900.0f);
            if (x < -100) x += 1900;
            Color c = mix(rgba(0xf4f4f8ff), rgba(0x3a3a5aff), night());
            DrawTriangle({x, kHorizon - 34}, {x, kHorizon - 4}, {x + 22, kHorizon - 4}, c);
            DrawTriangle({x - 2, kHorizon - 26}, {x - 16, kHorizon - 4}, {x - 2, kHorizon - 4}, alpha(c, 0.8f));
            DrawRectangle((int)x - 18, (int)kHorizon - 4, 44, 5, mix(rgba(0x8a3a2aff), rgba(0x1a1a2aff), night()));
        }
        float px = 1500 - std::fmod(cam_ * 0.12f, 2700.0f);
        drawPier(px);
        // glitter path under the sun
        float k = clamp01((h - 11) / 9.9f);
        float sunX = lerpf(760, 1060, k);
        fx::beginAdd();
        for (int i = 0; i < 26; i++) {
            float y = kHorizon + 6 + i * 5.6f;
            float w = 12 + i * 3.0f;
            float x = sunX + std::sin(t_ * 3 + i * 1.7f) * w;
            fx::glow({x, y}, 6 + i * 0.4f, alpha(mix(rgba(0xfff0c0ff), rgba(0xff8a4aff), dusk()), 0.5f * (1 - night())));
        }
        fx::endAdd();
        // rolling wave lines, closer = bigger and faster
        for (int row = 0; row < 9; row++) {
            float y = kHorizon + 12 + row * row * 1.75f;
            float sp = 0.15f + row * 0.08f, amp = 1 + row * 0.5f, len = 26 + row * 9;
            float off = std::fmod(cam_ * sp + t_ * 10 * (row % 2 ? 1 : -1), len * 3);
            for (float x = -off; x < VW + len * 3; x += len * 3) {
                float yy = y + std::sin(t_ * 1.5f + x * 0.01f + row) * amp;
                DrawLineEx({x, yy}, {x + len, yy - amp}, 1 + row * 0.25f, alpha(WHITE, 0.25f + row * 0.03f));
            }
        }
        // the surf: a foamy edge that runs up the sand and back
        float wash = std::sin(t_ * 0.8f) * 8;
        Color wet = mix(rgba(0xb89a6aff), rgba(0x3a3a4aff), night());
        DrawRectangle(0, (int)(kShore - 6), VW, 26, wet);
        std::vector<Vector2> foam;
        for (float x = 0; x <= VW + 20; x += 20) foam.push_back({x, kShore + wash + std::sin(x * 0.02f + t_ * 1.3f + cam_ * 0.02f) * 4});
        for (size_t i = 0; i + 1 < foam.size(); i++) {
            DrawTriangle(foam[i], {foam[i].x, kShore - 8}, foam[i + 1], alpha(mix(bot, WHITE, 0.3f), 0.9f));
            DrawTriangle(foam[i + 1], {foam[i].x, kShore - 8}, {foam[i + 1].x, kShore - 8}, alpha(mix(bot, WHITE, 0.3f), 0.9f));
            DrawLineEx(foam[i], foam[i + 1], 4, alpha(WHITE, 0.85f - night() * 0.4f));
        }
    }

    void drawBeach() {
        Color tint = mix(mix(WHITE, rgba(0xffc8a8ff), dusk() * 0.6f), rgba(0x5a5a8aff), night());
        fx::tiled(E().tex("tex/sand"), {0, kShore + 16, (float)VW, VH - kShore - 16}, 0.9f, {cam_ / 0.9f, 0}, tint);
        DrawRectangleGradientV(0, (int)kShore + 16, VW, 30, alpha(rgba(0x6a5030ff), 0.35f), alpha(BLACK, 0));
        // footprints and the tide line of shells and seaweed
        for (int i = 0; i < 40; i++) {
            float wx = std::floor(cam_ / 1600) * 1600 + i * 80;
            for (int rep = 0; rep < 2; rep++) {
                float x = wx + rep * 1600 - cam_;
                if (x < -20 || x > VW + 20) continue;
                float y = 520 + std::fmod(i * 37.0f, 170);
                DrawEllipse((int)x, (int)y, 4, 2, alpha(darken(tint, 0.35f), 0.25f));
                DrawEllipse((int)x + 10, (int)y + 7, 4, 2, alpha(darken(tint, 0.35f), 0.25f));
            }
        }
    }

    void drawProp(const Prop& p) {
        float x = p.wx - cam_;
        if (x < -200 || x > VW + 200) return;
        switch (p.kind) {
            case 0: {
                Texture2D& t = E().tex("spr/parasol");
                fx::softShadow({x + 30, p.y}, 90 * p.s, 10 * p.s, 0.3f);
                fx::sprite(t, {x, p.y - t.height * p.s / 2}, p.s);
                break;
            }
            case 1: {  // a striped windbreak
                const Color cs[] = {rgba(0xff4a5aff), rgba(0x2a7ad8ff), rgba(0x3ddc84ff), rgba(0xffc531ff), rgba(0xff8a2bff), rgba(0x8f5cffff)};
                Color c = cs[p.var];
                float w = 150 * p.s, h = 48 * p.s;
                for (int i = 0; i < 6; i++) {
                    Rectangle r{x + i * w / 6, p.y - h, w / 6 + 1, h};
                    DrawRectangleRec(r, i % 2 ? WHITE : c);
                }
                DrawRectangleLinesEx({x, p.y - h, w, h}, 1.5f, alpha(col::outline, 0.7f));
                for (int i = 0; i <= 3; i++) DrawRectangle((int)(x + i * w / 3) - 2, (int)(p.y - h - 8), 4, (int)h + 12, rgba(0x8a5a3aff));
                break;
            }
            case 2: {  // sandcastle
                Color s0 = rgba(0xe8c890ff), s1 = rgba(0xc8a060ff);
                float sz = 30 * p.s;
                DrawRectangleRec({x - sz * 1.2f, p.y - sz, sz * 2.4f, sz}, s0);
                for (int i = -1; i <= 1; i++) {
                    DrawRectangleRec({x + i * sz * 0.9f - sz * 0.3f, p.y - sz * 1.7f, sz * 0.6f, sz * 0.7f}, s0);
                    DrawTriangle({x + i * sz * 0.9f - sz * 0.32f, p.y - sz * 1.7f}, {x + i * sz * 0.9f + sz * 0.32f, p.y - sz * 1.7f}, {x + i * sz * 0.9f, p.y - sz * 2.2f}, s1);
                }
                DrawRectangleRec({x - sz * 0.25f, p.y - sz * 0.6f, sz * 0.5f, sz * 0.6f}, s1);
                DrawLineEx({x, p.y - sz * 2.2f}, {x, p.y - sz * 2.9f}, 1.5f, col::outline);
                DrawRectangleRec({x, p.y - sz * 2.9f, sz * 0.5f, sz * 0.12f}, rgba(0xae1c28ff));
                DrawRectangleRec({x, p.y - sz * 2.78f, sz * 0.5f, sz * 0.12f}, WHITE);
                DrawRectangleRec({x, p.y - sz * 2.66f, sz * 0.5f, sz * 0.12f}, rgba(0x21468bff));
                break;
            }
            case 3: {  // a Dutch flag on a pole, waving
                DrawLineEx({x, p.y}, {x, p.y - 140 * p.s}, 3, rgba(0xe8e8f0ff));
                for (int i = 0; i < 12; i++) {
                    float fx0 = x + i * 5 * p.s, wv = std::sin(t_ * 5 - i * 0.6f) * 3 * (i / 12.0f) * p.s;
                    const Color st[3] = {rgba(0xae1c28ff), WHITE, rgba(0x21468bff)};
                    for (int b = 0; b < 3; b++) DrawRectangleRec({fx0, p.y - 140 * p.s + b * 12 * p.s + wv, 5 * p.s + 1, 12 * p.s}, st[b]);
                }
                break;
            }
            default: {  // beach ball
                Vector2 c{x, p.y - 16 * p.s};
                DrawCircleV(c, 16 * p.s, WHITE);
                DrawCircleSector(c, 16 * p.s, 0, 60, 8, col::red);
                DrawCircleSector(c, 16 * p.s, 120, 180, 8, col::cyan);
                DrawCircleSector(c, 16 * p.s, 240, 300, 8, col::gold);
                ui::circleOutline(c, 16 * p.s, col::outline, 1.5f);
                break;
            }
        }
    }

    void drawFood(int food, Vector2 c, float s) {
        if (food == 5) { art::boba(c, 34 * s); return; }
        fx::sprite(E().tex(kFoodSpr[food]), c, 0.55f * s);
    }

    void arm(Vector2 a, Vector2 b, float w, Color skin) {
        DrawLineEx(a, b, w + 3, col::outline);
        DrawCircleV(b, w * 0.5f + 1.5f, col::outline);
        DrawLineEx(a, b, w, skin);
        DrawCircleV(b, w * 0.5f, skin);
    }

    void drawPerson(const Person& p) {
        float x = p.wx - cam_;
        if (x < -150 || x > VW + 150) return;
        fx::softShadow({x, p.feet}, 34 * p.s, 7 * p.s, 0.35f);
        if (p.role == Role::Davi) {
            // Davi asleep in a beach chair, sunglasses on, boba on the armrest
            Color wood = rgba(0x8a5a3aff);
            DrawLineEx({x - 70, p.feet}, {x - 30, p.feet - 40}, 5, wood);
            DrawLineEx({x + 60, p.feet}, {x + 20, p.feet - 40}, 5, wood);
            Vector2 q[4] = {{x - 80, p.feet - 44}, {x + 50, p.feet - 44}, {x + 60, p.feet - 32}, {x - 76, p.feet - 32}};
            DrawTriangle(q[0], q[3], q[2], rgba(0x2a9ad8ff));
            DrawTriangle(q[0], q[2], q[1], rgba(0x2a9ad8ff));
            DrawLineEx({x - 80, p.feet - 44}, {x - 100, p.feet - 110}, 18, rgba(0x2a9ad8ff));
            DrawRectangleRounded({x - 74, p.feet - 64, 110, 24}, 0.8f, 8, rgba(0xff8a2bff));
            art::portrait("davi", {x - 92, p.feet - 92}, 30, col::outline, -25);
            DrawRectangleRounded({x - 112, p.feet - 104, 36, 10}, 0.5f, 6, rgba(0x15121cff));
            if (p.st != PState::Robbed) {
                for (int i = 0; i < 3; i++) {
                    float k = std::fmod(t_ * 0.6f + i / 3.0f, 1.0f);
                    ui::text("z", x - 70 + k * 30, p.feet - 130 - k * 50, 16 + k * 10, alpha(WHITE, 1 - k), ui::Center, true);
                }
            } else {
                ui::text("!!", x - 92, p.feet - 140 + std::sin(p.t * 10) * 3, 26, col::red, ui::Center, true);
            }
            if (p.food >= 0) drawFood(p.food, foodPos(p), 1);
            if (p.splat) DrawEllipse((int)x - 92, (int)p.feet - 116, 14, 7, alpha(WHITE, 0.95f));
            return;
        }
        Texture2D& body = E().tex(TextFormat("spr/beach%d", p.spr));
        Color skin = kSkin[p.spr];
        float bob = p.st == PState::Robbed ? std::abs(std::sin(p.t * 14)) * 4 : std::sin(t_ * 2 + p.wx) * 1.5f;
        Vector2 c{x, p.feet - 85 * p.s - bob};
        bool flip = p.face < 0;
        // hidden snack goes behind the body
        if (p.role == Role::Tourist && p.food >= 0 && p.hide > 0.5f) {
            arm(shoulder(p), hand(p), 11 * p.s, skin);
            drawFood(p.food, foodPos(p), p.s);
        }
        fx::sprite(body, c, p.s, 0, WHITE, flip);
        if (p.splat) {
            Vector2 hd{x + 2 * p.face * p.s, p.feet - 85 * p.s - 66 * p.s - bob};
            DrawEllipse((int)hd.x, (int)hd.y, 14 * p.s, 6 * p.s, WHITE);
            DrawEllipse((int)hd.x - 6, (int)(hd.y + 8 * p.s), 3 * p.s, 7 * p.s, WHITE);
        }
        if (p.role == Role::KiteKid) {
            Vector2 h = kidHand(p), k = kitePos(p);
            Vector2 mid = (h + k) * 0.5f + Vector2{0, 30};
            DrawSplineSegmentBezierQuadratic(h, mid, k, 1.5f, alpha(WHITE, 0.85f));
            for (int i = 0; i < 6; i++) {  // tail with bows
                Vector2 tp = k + Vector2{-8.0f - i * 12, 40.0f + i * 8 + std::sin(t_ * 6 + i) * 6};
                DrawRectangleRec({tp.x - 4, tp.y - 3, 8, 6}, i % 2 ? col::red : col::gold);
            }
            fx::sprite(E().tex("spr/kite"), k, 0.7f, std::sin(t_ * 2 + p.kitePh) * 12);
            arm(shoulder(p), h, 9 * p.s, skin);
            return;
        }
        if (p.role == Role::Squirter) {
            Vector2 sh = shoulder(p), h = sh + Vector2{20 * p.s, 2 * p.s};
            arm(sh, h, 9 * p.s, skin);
            DrawRectangleRounded({h.x - 4, h.y - 10, 30, 12}, 0.5f, 6, rgba(0x3ddc84ff));
            DrawRectangleRounded({h.x - 2, h.y - 2, 9, 14}, 0.4f, 4, rgba(0x2a9a5aff));
            DrawCircleV({h.x + 6, h.y - 14}, 7, alpha(col::cyan, 0.85f));
            return;
        }
        if (p.food >= 0 && p.hide <= 0.5f) {
            arm(shoulder(p), hand(p), 11 * p.s, skin);
            drawFood(p.food, foodPos(p), p.s);
        } else if (p.food < 0 || p.st == PState::Robbed) {
            arm(shoulder(p), hand(p), 11 * p.s, skin);
        }
        // alert: they've seen you
        if (p.st == PState::Calm && p.food >= 0 && p.notice > 0.15f) {
            float k = clamp01(p.notice / p.react);
            ui::textOutline("?", x, p.feet - 195 * p.s, 22 + 8 * k, mix(WHITE, col::gold, k), col::outline, 3);
        }
        if (p.st == PState::Alert) ui::textOutline("!", x, p.feet - 198 * p.s + std::sin(p.t * 20) * 2, 34, col::red, col::outline, 3);
    }

    void drawStall(const Stall& s) {
        float x = s.wx - cam_;
        if (x < -250 || x > VW + 250) return;
        Texture2D& t = E().tex("spr/kraam");
        fx::softShadow({x, 562}, 130, 12, 0.35f);
        // the vendor behind the counter, spatula ready
        Texture2D& v = E().tex("spr/beach2");
        fx::sprite(v, {x - 30, 458}, 0.9f, 0, WHITE, true);
        DrawEllipse((int)x - 30, 398, 24, 10, WHITE);  // chef's hat
        DrawRectangleRounded({x - 46, 370, 32, 28}, 0.5f, 6, WHITE);
        Vector2 sh{x - 20, 410}, sp = spatula(s);
        DrawLineEx(sh, sp, 5, rgba(0x5a3a24ff));
        Vector2 dir = Vector2Normalize(sp - sh);
        Vector2 nrm{-dir.y, dir.x};
        Vector2 a = sp + nrm * 13, b = sp - nrm * 13, c2 = a + dir * 20, d2 = b + dir * 20;
        DrawTriangle(a, b, d2, rgba(0xc8ccd8ff));
        DrawTriangle(a, d2, c2, rgba(0xc8ccd8ff));
        if (s.swing >= 0 && s.swing < 0.45f) ui::textOutline("!", x - 30, 330, 34, col::red, col::outline, 3);
        fx::sprite(t, {x, 562 - t.height * 0.8f / 2}, 0.8f);
        ui::text("PATAT", x, 562 - (230 - 133) * 0.8f, 22, col::gold, ui::Center, true);
        for (int i = 0; i < s.cones; i++) drawFood(0, conePos(s, i), 0.9f);
    }

    // The gull: tail, far wing, body, near wing, head. `pieter` puts his face on it.
    void drawGull(Vector2 pos, float tilt, float flap, int mode, float facing, bool pieter, float sc, float alphaK) {
        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0);
        rlRotatef(tilt * facing, 0, 0, 1);
        rlScalef(facing * sc, sc, 1);
        Color tint = alpha(WHITE, alphaK);
        Texture2D& wing = E().tex("spr/gull_wing");
        Texture2D& body = E().tex("spr/gull_body");
        // 0 deg = wing pointing forward; mid-stroke the wing points at the camera, so it gets foreshortened
        float wa = mode == 2 ? -172 : mode == 1 ? -30 + std::sin(flap) * 88 : -150 + std::sin(flap * 0.5f) * 8;
        float wl = mode == 2 ? 0.6f : mode == 1 ? 0.72f * (0.3f + 0.7f * std::abs(std::sin(wa * DEG2RAD))) : 0.72f;
        auto drawWing = [&](Vector2 at, float ang, Color c) {
            DrawTexturePro(wing, {0, 0, (float)wing.width, (float)wing.height}, {at.x, at.y, wing.width * wl, wing.height * 0.72f},
                           {2 * wl, 24 * 0.72f}, ang, c);
        };
        drawWing({-6, -14}, wa - 8, alpha(rgba(0xa0a8b8ff), alphaK));
        DrawTexturePro(body, {0, 0, (float)body.width, (float)body.height}, {0, 0, body.width * 0.75f, body.height * 0.75f}, {63 * 0.75f, 46 * 0.75f}, 0, tint);
        if (pieter) {
            art::pieter({38, -16}, 19, P().hat, 0, tint);
        } else {
            DrawCircleV({36, -14}, 15, alpha(col::outline, alphaK));
            DrawCircleV({36, -14}, 13, tint);
            DrawCircleV({40, -18}, 2.6f, alpha(col::outline, alphaK));
        }
        // beak with the red spot
        Vector2 b0{pieter ? 52.0f : 46.0f, -16}, b1{b0.x + 22, -10}, b2{b0.x, -6};
        float open = chew_ > 0 && pieter ? std::abs(std::sin(chew_ * 30)) * 5 : 0;
        DrawTriangle({b0.x - 1, b0.y - 1.5f}, {b2.x - 1, b2.y + 1.5f + open}, {b1.x + 2, b1.y + open * 0.5f}, alpha(col::outline, alphaK));
        DrawTriangle(b0, b2 + Vector2{0, open}, b1 + Vector2{0, open * 0.5f}, alpha(rgba(0xffc531ff), alphaK));
        DrawCircleV({b0.x + 13, -8 + open * 0.6f}, 2.4f, alpha(col::red, alphaK));
        drawWing({-2, -10}, wa, tint);
        rlPopMatrix();
    }

    void draw() override {
        drawSky();
        drawSea();
        drawBeach();
        // depth-sort everything standing on the beach
        std::vector<std::pair<float, std::function<void()>>> layer;
        for (auto& p : props_) layer.push_back({p.y, [&] { drawProp(p); }});
        for (auto& p : people_) layer.push_back({p.feet, [&] { drawPerson(p); }});
        for (auto& s : stalls_) layer.push_back({562, [&] { drawStall(s); }});
        std::stable_sort(layer.begin(), layer.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& l : layer) l.second();
        for (auto& l : loose_) {
            Vector2 c{l.wx - cam_, l.y};
            if (l.landed && l.life < 1.5f && std::fmod(t_, 0.2f) < 0.1f) continue;
            if (!l.landed) {
                fx::beginAdd();
                fx::glow(c, 40, alpha(col::gold, 0.5f));
                fx::endAdd();
            }
            drawFood(l.food, c, 0.9f);
        }
        // in the air
        for (auto& d : drones_) {
            Vector2 c{d.wx - cam_, d.y + std::sin(d.ph * 2.1f) * 50};
            fx::sprite(E().tex("spr/drone"), c, 0.55f, std::sin(d.ph * 3) * 8);
            fx::beginAdd();
            fx::glow(c + Vector2{0, 14}, 30, alpha(col::red, 0.45f + 0.3f * std::sin(t_ * 8)));
            fx::endAdd();
        }
        for (auto& r : rivals_) {
            Vector2 c{r.wx - cam_, r.y};
            drawGull(c, r.state == 2 ? r.t * 900 : std::clamp(r.vy * 0.05f, -25.0f, 40.0f), r.flap, 1, r.vx < 0 ? -1 : 1, false, 0.85f, 1);
            if (r.carry >= 0) drawFood(r.carry, c + Vector2{(r.vx < 0 ? -1 : 1) * 54.0f, -6}, 0.7f);
        }
        for (auto& s : shots_) {
            Vector2 c{s.wx - cam_, s.y};
            if (s.slipper) {
                rlPushMatrix();
                rlTranslatef(c.x, c.y, 0);
                rlRotatef(s.rot, 0, 0, 1);
                DrawRectangleRounded({-15, -7, 30, 14}, 0.9f, 8, col::outline);
                DrawRectangleRounded({-13, -5, 26, 10}, 0.9f, 8, rgba(0xff6fb5ff));
                DrawLineEx({-2, -5}, {6, 5}, 2, WHITE);
                rlPopMatrix();
            } else {
                DrawCircleV(c, 9, alpha(rgba(0x8fd8ffff), 0.85f));
                DrawCircleV(c - Vector2{s.vx * 0.012f - speed_ * 0.012f, s.vy * 0.012f}, 6, alpha(rgba(0x8fd8ffff), 0.5f));
                DrawCircleV(c + Vector2{-3, -3}, 3, alpha(WHITE, 0.8f));
            }
        }
        for (auto& pp : poops_) {
            Vector2 c{pp.wx - cam_, pp.y};
            DrawCircleV(c, 6, col::outline);
            DrawCircleV(c, 5, WHITE);
            DrawCircleV(c + Vector2{1, -5}, 3, WHITE);
        }
        // the player
        {
            bool blink = inv_ > 0 && std::fmod(t_, 0.16f) < 0.08f;
            int mode = phase_ == Phase::Down ? 0 : diving_ ? 2 : (vy_ < 120 && swoop_ <= 0) ? 1 : 0;
            if (swoop_ > 0 && !diving_) {
                fx::beginAdd();
                fx::glowLine(gull_ + Vector2{-90, 30}, gull_, 18, alpha(WHITE, 0.25f * swoop_));
                fx::endAdd();
            }
            drawGull(gull_, tilt_, flap_, mode, 1, true, 1, blink ? 0.35f : 1);
            if (diving_) {
                for (int i = 0; i < 3; i++) DrawLineEx(gull_ + Vector2{-40.0f - i * 14, -26.0f + i * 18}, gull_ + Vector2{-80.0f - i * 14, -60.0f + i * 18}, 2, alpha(WHITE, 0.5f));
            }
        }
        fx_.draw();
        // evening light
        if (night() > 0 || dusk() > 0) {
            DrawRectangle(0, 0, VW, VH, alpha(rgba(0xff6a3aff), dusk() * (1 - night()) * 0.08f));
            DrawRectangle(0, (int)kHorizon, VW, VH, alpha(rgba(0x0a0a30ff), night() * 0.35f));
        }
        if (phase_ == Phase::Play || phase_ == Phase::Down) drawHud();
        if (phase_ == Phase::Ready) drawReady();
        if (phase_ == Phase::Over) drawOver();
    }

    void drawHud() {
        Rectangle r{20, 16, 300, 64};
        DrawRectangleRounded(r, 0.5f, 10, alpha(col::bgDeep, 0.75f));
        ui::textShadow(fmtNum(score_), r.x + 20, r.y + 24, 32, WHITE, ui::Left, true, 2);
        ui::text("best " + fmtNum(P().stats.meeuwBest), r.x + 20, r.y + 50, 14, col::muted, ui::Left, true);
        // feathers
        for (int i = 0; i < 3; i++) {
            Vector2 c{r.x + 214 + i * 30.0f, r.y + 32};
            bool on = i < feathers_;
            rlPushMatrix();
            rlTranslatef(c.x, c.y, 0);
            rlRotatef(-35, 0, 0, 1);
            DrawEllipse(0, 0, 7, 17, on ? col::outline : alpha(col::outline, 0.4f));
            DrawEllipse(0, 0, 5, 15, on ? WHITE : alpha(WHITE, 0.15f));
            DrawLineEx({0, -14}, {0, 18}, 1.5f, on ? rgba(0x8a92a6ff) : alpha(WHITE, 0.2f));
            rlPopMatrix();
        }
        // belly
        Rectangle b{VW / 2.0f - 190, 22, 380, 50};
        DrawRectangleRounded(b, 0.5f, 10, alpha(col::bgDeep, 0.75f));
        fx::sprite(E().tex("spr/food_patat"), {b.x + 28, b.y + 25}, 0.6f, std::sin(t_ * 3) * 6);
        float k = hunger_ / 100;
        bool low = k < 0.25f;
        ui::text(low ? "STARVING!" : "BELLY", b.x + 56, b.y + 14, 12, low && std::fmod(t_, 0.5f) < 0.25f ? col::red : col::muted, ui::Left, true);
        ui::bar({b.x + 56, b.y + 26, b.width - 72, 14}, k, low ? col::red : k < 0.5f ? col::orange : col::green);
        // poop ammo + clock
        Rectangle c{VW - 220.0f, 16, 200, 64};
        DrawRectangleRounded(c, 0.5f, 10, alpha(col::bgDeep, 0.75f));
        float h = hour();
        int hh = (int)h, mm = (int)((h - hh) * 60) / 5 * 5;
        ui::textShadow(TextFormat("%02d:%02d", hh, mm), c.x + 20, c.y + 24, 28, WHITE, ui::Left, true, 2);
        ui::text(night() > 0.5f ? "night" : dusk() > 0.3f ? "sunset" : "Scheveningen", c.x + 20, c.y + 50, 12, col::muted, ui::Left, true);
        for (int i = 0; i < 3; i++) {
            Vector2 p{c.x + 132 + i * 22.0f, c.y + 32};
            Color pc = i < ammo_ ? WHITE : alpha(WHITE, 0.15f);
            DrawCircleV(p, 7, i < ammo_ ? col::outline : BLANK);
            DrawCircleV(p, 6, pc);
            DrawCircleV(p + Vector2{1, -6}, 3.5f, pc);
        }
        ui::text("E", c.x + 132 + 22, c.y + 52, 11, col::muted, ui::Center, true);
        if (combo_ > 1) {
            float pulse = 1 + 0.1f * std::sin(t_ * 12);
            ui::textOutline(TextFormat("x%d COMBO", std::min(combo_, 8)), VW / 2.0f, 104, 30 * pulse, col::gold, col::outline, 4);
            ui::bar({VW / 2.0f - 60, 124, 120, 6}, comboT_ / 3.5f, col::gold);
        }
    }

    void drawReady() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.35f));
        Rectangle p{VW / 2.0f - 330, 96, 660, 400};
        ui::panel(p, alpha(col::panel, 0.95f), 0.08f, alpha(col::cyan, 0.6f));
        fx::gradientText("MEEUWEN!", VW / 2.0f, p.y + 66, 80, WHITE, rgba(0xbfe4ffff), 5);
        ui::text("Pieter is a seagull now. A very hungry one.", VW / 2.0f, p.y + 122, 18, col::pink, ui::Center, true);
        ui::text("W / S  fly up and down      A / D  forward and back", VW / 2.0f, p.y + 166, 20, col::text, ui::Center, true);
        ui::text("hold SPACE  dive    let go  swoop back up", VW / 2.0f, p.y + 198, 20, col::text, ui::Center, true);
        ui::text("E  drop a little present on someone's head", VW / 2.0f, p.y + 230, 20, col::text, ui::Center, true);
        ui::text("Snatch snacks before they're hidden. Bump rival gulls to steal theirs.", VW / 2.0f, p.y + 268, 16, col::muted, ui::Center, true);
        ui::text("Watch out for kites, spatulas, slippers and Davi's drones.", VW / 2.0f, p.y + 292, 16, col::muted, ui::Center, true);
        ui::text("best: " + fmtNum(P().stats.meeuwBest), VW / 2.0f, p.y + 330, 20, col::gold, ui::Center, true);
        float a = 0.6f + 0.4f * std::sin(t_ * 4);
        ui::text("press SPACE to fly", VW / 2.0f, p.y + 368, 22, alpha(col::cyan, a), ui::Center, true);
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawOver() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.55f));
        Rectangle p{VW / 2.0f - 300, 100, 600, 450};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::cyan, 0.6f));
        ui::textOutline(starved_ ? "STARVED" : "PLUCKED!", VW / 2.0f, p.y + 66, 72, rgba(0xbfe4ffff), col::outline, 5);
        ui::text(starved_ ? "Not one more fry. Pieter faints on the boulevard." : "Pieter the seagull has had enough for today.", VW / 2.0f, p.y + 122, 18,
                 col::muted, ui::Center, true);
        ui::textShadow(fmtNum(score_), VW / 2.0f, p.y + 186, 64, WHITE, ui::Center, true, 4);
        if (best_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW RECORD!", VW / 2.0f, p.y + 232, 22, col::gold, ui::Center, true);
        ui::text(TextFormat("%d snacks stolen     best combo x%d     until %02d:%02d", eaten_, bestCombo_, (int)hour(), (int)((hour() - (int)hour()) * 60)),
                 VW / 2.0f, p.y + 268, 18, col::text, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 310}, 15);
        ui::text("+" + fmtNum(earned_) + " Pietercoins", VW / 2.0f - 66, p.y + 310, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 362, 250, 64}, "FLY AGAIN", rgba(0x2a8ab8ff), 24) || IsKeyPressed(KEY_SPACE)) reset(), phase_ = Phase::Play;
            if (ui::button({p.x + 310, p.y + 362, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

private:
    Phase phase_ = Phase::Ready;
    std::vector<Person> people_;
    std::vector<Stall> stalls_;
    std::vector<Prop> props_;
    std::vector<Drone> drones_;
    std::vector<Rival> rivals_;
    std::vector<Shot> shots_;
    std::vector<Poop> poops_;
    std::vector<Loose> loose_;
    Vector2 gull_{360, 260};
    float t_ = 0, phaseT_ = 0, cam_ = 0, spawnX_ = 700, vy_ = 0, boost_ = 0, speed_ = 250, tilt_ = 0, flap_ = 0, swoop_ = 0;
    float hunger_ = 100, inv_ = 0, elapsed_ = 0, rivalT_ = 8, comboT_ = 0, chew_ = 0;
    int feathers_ = 3, ammo_ = 3, score_ = 0, eaten_ = 0, combo_ = 0, bestCombo_ = 0;
    long long earned_ = 0;
    bool diving_ = false, starved_ = false, best_ = false, landed_ = false, bot_ = false;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeMeeuw() { return std::make_unique<MeeuwScene>(); }

}  // namespace pg
