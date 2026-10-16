// HORSES: Pieter LOOOVES horses. Two ways to play:
//  - BET: six horses, odds from a Monte Carlo simulation of the same race model.
//  - RIDE: you are the jockey. Alternate LEFT/RIGHT to gallop, SPACE to jump hurdles.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <numeric>

#include <rlgl.h>

namespace pg {
namespace {

enum class Mode { Hub, BetPick, BetRace, BetResult, RideCountdown, Ride, RideResult };

// ------------------------------------------------------------------ race model

struct Runner {
    std::string name;
    Color body, mane, silk;
    float base = 70;     // units per second
    float wobble = 0.1f; // inconsistency
    float burst = 0.2f;  // chance of a surge per second
    float stamina = 0.5f;
    // live race state
    float dist = 0, speed = 0, noise = 0, noiseT = 0, surgeT = 0, phase = 0, finishTime = -1;
    int place = 0;
};

constexpr float kBetLength = 1000;

// One fixed simulation step shared by the live race and the odds simulation.
void stepRunner(Runner& r, float dt, std::mt19937& g) {
    std::uniform_real_distribution<float> u(0, 1);
    r.noiseT -= dt;
    if (r.noiseT <= 0) {
        r.noiseT = 0.4f + u(g) * 0.4f;
        r.noise = (u(g) * 2 - 1) * r.wobble;
    }
    if (r.surgeT > 0) r.surgeT -= dt;
    else if (u(g) < r.burst * dt) r.surgeT = 0.8f + u(g) * 0.6f;
    float tired = r.dist > kBetLength * 0.6f ? (0.88f + 0.12f * r.stamina) : 1.0f;
    float target = r.base * (1 + r.noise) * tired * (r.surgeT > 0 ? 1.28f : 1.0f);
    r.speed += (target - r.speed) * std::min(1.0f, dt * 3);
    r.dist += r.speed * dt;
}

const char* kNames[] = {"Juhuh Express", "Boba Thunder", "Davi's Regret", "Stroopwafel Sprint", "Kira Kaboom",
                        "Useless Elektronik", "Game Fuel", "Hagelslag Hooves", "Pieter's Pride", "Frikandel Flash",
                        "Lag Machine", "Pro Since 2010", "Dutch Disaster", "Skibidi Stallion", "Bec IT Bolt"};

std::vector<Runner> makeField() {
    std::vector<int> idx(15);
    std::iota(idx.begin(), idx.end(), 0);
    std::shuffle(idx.begin(), idx.end(), rng());
    const Color bodies[] = {rgba(0x8b5a2bff), rgba(0x5a3820ff), rgba(0xd9b38cff), rgba(0x2b2420ff), rgba(0xa8a8b0ff), rgba(0xc47a3aff)};
    const Color silks[] = {col::red, col::cyan, col::gold, col::green, col::pink, col::violet};
    std::vector<Runner> f;
    for (int i = 0; i < 6; i++) {
        Runner r;
        r.name = kNames[idx[i]];
        r.body = bodies[rndi(0, 5)];
        r.mane = darken(r.body, 0.5f);
        r.silk = silks[i];
        r.base = rnd(64, 74);
        r.wobble = rnd(0.05f, 0.18f);
        r.burst = rnd(0.1f, 0.45f);
        r.stamina = rnd(0.1f, 1.0f);
        f.push_back(r);
    }
    return f;
}

// Monte Carlo: how often does each horse win / finish top 3?
void simulateOdds(const std::vector<Runner>& field, std::vector<float>& winOdds, std::vector<float>& podiumOdds) {
    std::mt19937 g(rng()());
    const int kSims = 600;
    std::vector<int> wins(field.size(), 0), podium(field.size(), 0);
    for (int s = 0; s < kSims; s++) {
        auto f = field;
        std::vector<float> fin(f.size(), -1);
        float t = 0;
        int done = 0;
        while (done < (int)f.size() && t < 60) {
            t += 0.05f;
            for (size_t i = 0; i < f.size(); i++) {
                if (fin[i] >= 0) continue;
                stepRunner(f[i], 0.05f, g);
                if (f[i].dist >= kBetLength) { fin[i] = t - (f[i].dist - kBetLength) / std::max(1.0f, f[i].speed); done++; }
            }
        }
        std::vector<int> order(f.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return fin[a] < fin[b]; });
        wins[order[0]]++;
        for (int k = 0; k < 3; k++) podium[order[k]]++;
    }
    winOdds.clear();
    podiumOdds.clear();
    for (size_t i = 0; i < field.size(); i++) {
        float pw = std::max(wins[i], 3) / (float)kSims, pp = std::max(podium[i], 6) / (float)kSims;
        winOdds.push_back(std::clamp(std::round(0.9f / pw * 10) / 10, 1.2f, 50.0f));
        podiumOdds.push_back(std::clamp(std::round(0.9f / pp * 10) / 10, 1.05f, 15.0f));
    }
}

// ------------------------------------------------------------------ ride mode

struct Rider {
    std::string name, jockey;
    Color body, mane, silk;
    float x = 0, speed = 0, jumpT = -1, stumble = 0, phase = 0, finish = -1;
    float aiSkill = 0.8f, aiTarget = 230;
    bool player = false;
    int lane = 0;
};

constexpr float kRideLength = 4200;
constexpr float kRideMax = 330;
constexpr int kEntryFee = 50;

class HorseScene : public Scene {
public:
    Presence presence() const override {
        Presence p{"Horse Racing"};
        p.largeImage = "horse";
        p.largeText = "JUHUH!";
        p.smallImage = "logo";
        switch (mode_) {
            case Mode::Hub: p.state = "At the stables"; break;
            case Mode::BetPick: p.state = "Picking a horse"; break;
            case Mode::BetRace:
            case Mode::BetResult: p.state = pick_ >= 0 ? "Betting on " + field_[pick_].name : "Watching a race"; break;
            case Mode::RideCountdown:
            case Mode::Ride:
                p.state = "Riding Juhuh - " + std::to_string((int)std::max(0.0f, kRideLength - riders_[0].x)) + "m to go";
                break;
            case Mode::RideResult: p.state = "Finished a race in place " + std::to_string(ridePlace_); break;
        }
        return p;
    }

    void enter() override {
        newField();
        // dev hook: PG_HORSE=bet|race|ride jumps straight into that part
        if (const char* h = std::getenv("PG_HORSE")) {
            std::string m = h;
            if (m == "bet") mode_ = Mode::BetPick;
            if (m == "race") { pick_ = 2; startBetRace(); }
            if (m == "ride") startRide();
        }
    }

    void newField() {
        field_ = makeField();
        simulateOdds(field_, winOdds_, podiumOdds_);
        pick_ = -1;
        bet_ = std::clamp<long long>(bet_, 1, std::max<long long>(1, P().coins));
    }

    // ============================================================ bet race
    void startBetRace() {
        if (pick_ < 0 || !P().spend(bet_)) {
            E().sfx.play(Sfx::Error);
            return;
        }
        P().wager(bet_);
        mode_ = Mode::BetRace;
        raceT_ = -2.0f;  // countdown
        finished_ = 0;
        leader_ = -1;
        commentary_ = "And they're lining up...";
        E().sfx.play(Sfx::Bell);
    }

    void updateBetRace(float dt) {
        raceT_ += dt;
        if (raceT_ < 0) return;
        if (raceT_ - dt < 0) { E().sfx.play(Sfx::Neigh); commentary_ = "AND THEY'RE OFF!"; }
        for (auto& r : field_) {
            if (r.finishTime >= 0) { r.speed = damp(r.speed, 20, 2, dt); r.phase += r.speed * dt * 0.09f; continue; }
            stepRunner(r, dt, rng());
            r.phase += r.speed * dt * 0.09f;
            if (chance(dt * 8)) {
                Particle p;
                p.pos = {screenX(r.dist) - 30, laneY(&r - field_.data())};
                p.vel = {rnd(-80, -20), rnd(-40, -5)};
                p.life = p.maxLife = 0.5f;
                p.size = rnd(3, 6);
                p.color = alpha(rgba(0xc8a070ff), 0.6f);
                fx_.add(p);
            }
            if (r.dist >= kBetLength) {
                r.finishTime = raceT_;
                r.place = ++finished_;
                if (r.place == 1) { commentary_ = r.name + " WINS IT!"; E().sfx.play(Sfx::Bell); fx_.confetti({screenX(kBetLength), laneY(&r - field_.data())}, 40); }
            }
        }
        // commentary on lead changes
        int lead = 0;
        for (int i = 1; i < 6; i++)
            if (field_[i].dist > field_[lead].dist) lead = i;
        if (lead != leader_ && finished_ == 0 && raceT_ > 1.0f) {
            static const char* fmt[] = {" takes the lead!", " pulls ahead!", " is flying!", " goes to the front!"};
            commentary_ = field_[lead].name + fmt[rndi(0, 3)];
            leader_ = lead;
        }
        if (finished_ == 6) {
            mode_ = Mode::BetResult;
            resultT_ = 0;
            Runner& mine = field_[pick_];
            bool win = podiumBet_ ? mine.place <= 3 : mine.place == 1;
            float odds = podiumBet_ ? podiumOdds_[pick_] : winOdds_[pick_];
            payout_ = win ? (long long)std::llround(bet_ * odds) : 0;
            if (win) {
                P().earn(payout_, true);
                P().stats.horseWins++;
                E().sfx.play(Sfx::Win);
            } else {
                E().sfx.play(Sfx::Lose);
            }
            P().save();
        }
    }

    float screenX(float dist) const { return 150 + dist / kBetLength * 1000; }
    float laneY(long i) const { return 232 + i * 68.0f; }

    // ============================================================ ride
    void startRide() {
        if (!P().spend(kEntryFee)) { E().sfx.play(Sfx::Error); E().toast("You need 50 coins to enter", col::red); return; }
        P().wager(kEntryFee);
        riders_.clear();
        Rider me{"Juhuh (you)", "pieter", rgba(0x8b5a2bff), rgba(0x3a2010ff), col::violet};
        me.player = true;
        me.lane = 2;
        riders_.push_back(me);
        Rider davi{"Lag Machine", "davi", rgba(0x2b2420ff), rgba(0x111111ff), col::cyan};
        davi.lane = 0; davi.aiSkill = rnd(0.82f, 0.95f); davi.aiTarget = rnd(246, 262);
        Rider kira{"Wink Wink", "kira", rgba(0xd9b38cff), rgba(0xf4e0b0ff), col::pink};
        kira.lane = 1; kira.aiSkill = rnd(0.85f, 0.97f); kira.aiTarget = rnd(250, 266);
        Rider rand{"Dutch Disaster", "", rgba(0xa8a8b0ff), rgba(0x55555aff), col::orange};
        rand.lane = 3; rand.aiSkill = rnd(0.7f, 0.9f); rand.aiTarget = rnd(236, 256);
        riders_.push_back(davi);
        riders_.push_back(kira);
        riders_.push_back(rand);
        hurdles_.clear();
        for (float x = 600; x < kRideLength - 300; x += rnd(380, 620)) hurdles_.push_back(x);
        stamina_ = 100;
        lastKey_ = 0;
        rideT_ = 0;
        countdown_ = 3.0f;
        ridePlace_ = 0;
        cam_ = 0;
        mode_ = Mode::RideCountdown;
        E().sfx.play(Sfx::Tick);
    }

    bool hurdleAt(float from, float to, float& hx) {
        for (float h : hurdles_)
            if (h > from && h <= to) { hx = h; return true; }
        return false;
    }

    void updateRide(float dt) {
        if (mode_ == Mode::RideCountdown) {
            int before = (int)std::ceil(countdown_);
            countdown_ -= dt;
            int after = (int)std::ceil(countdown_);
            if (after != before) E().sfx.play(after <= 0 ? Sfx::Bell : Sfx::Tick);
            if (countdown_ <= 0) { mode_ = Mode::Ride; E().sfx.play(Sfx::Neigh); }
            return;
        }
        rideT_ += dt;
        for (auto& r : riders_) {
            if (r.finish >= 0) { r.speed = damp(r.speed, 60, 1.5f, dt); r.x += r.speed * dt; r.phase += r.speed * dt * 0.05f; continue; }
            r.stumble = std::max(0.0f, r.stumble - dt);
            if (r.player) {
                bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
                bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
                int key = left ? 1 : right ? 2 : 0;
                if (key && r.stumble <= 0) {
                    if (key != lastKey_) {
                        float eff = 0.4f + 0.6f * (stamina_ / 100);
                        r.speed += 26 * eff;
                        stamina_ = std::max(0.0f, stamina_ - 1.2f);
                        E().sfx.play(Sfx::Tick, 0.25f, key == 1 ? 0.9f : 1.1f);
                    } else {
                        r.speed *= 0.93f;  // same key twice: the horse gets confused
                    }
                    lastKey_ = key;
                    lastTap_ = 0;
                }
                lastTap_ += dt;
                stamina_ = std::min(100.0f, stamina_ + dt * (lastTap_ > 0.35f ? 26.0f : 6.0f));
                if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) && r.jumpT < 0) {
                    r.jumpT = 0;
                    E().sfx.play(Sfx::Jump, 0.6f);
                }
                r.speed -= r.speed * 0.55f * dt;  // friction
            } else {
                // AI: chase its target speed, rubber-band a little toward the player
                float target = r.aiTarget + (riders_[0].x - r.x) * 0.03f;
                target = std::clamp(target, 150.0f, kRideMax * 0.92f);
                if (r.stumble <= 0) r.speed = damp(r.speed, target, 1.2f, dt);
                float hx;
                if (r.jumpT < 0 && hurdleAt(r.x + 40, r.x + 40 + r.speed * 0.28f, hx) && chance(r.aiSkill)) r.jumpT = 0;
            }
            r.speed = std::clamp(r.speed, 0.0f, kRideMax);
            if (r.stumble > 0) r.speed = std::min(r.speed, 60.0f);
            float before = r.x;
            r.x += r.speed * dt;
            r.phase += r.speed * dt * 0.05f;
            if (r.jumpT >= 0) {
                r.jumpT += dt;
                if (r.jumpT > 0.62f) r.jumpT = -1;
            }
            float hx;
            if (r.jumpT < 0 && r.stumble <= 0 && hurdleAt(before, r.x, hx)) {
                r.stumble = 0.9f;
                r.speed *= 0.3f;
                if (r.player) { E().sfx.play(Sfx::Hurt, 0.7f); E().shake(8); fx_.floatText("OOF", {riderScreenX(r), laneFeet(r.lane) - 120}, col::red, 34); }
            }
            if (r.player && r.speed > 40 && chance(dt * 20)) {
                Particle p;
                p.pos = {riderScreenX(r) - 40, laneFeet(r.lane) - 4};
                p.vel = {-r.speed * 0.3f + rnd(-30, 30), rnd(-60, -10)};
                p.life = p.maxLife = 0.5f;
                p.size = rnd(3, 7);
                p.color = alpha(rgba(0xc8a070ff), 0.7f);
                fx_.add(p);
            }
            if (r.x >= kRideLength) {
                r.finish = rideT_;
                int place = 1;
                for (auto& o : riders_)
                    if (&o != &r && o.finish >= 0) place++;
                if (r.player) {
                    ridePlace_ = place;
                    finishRide();
                }
            }
        }
        cam_ = damp(cam_, riders_[0].x - 380, 6, dt);
    }

    void finishRide() {
        static const long long prize[] = {0, 260, 110, 45, 0};
        rideReward_ = prize[ridePlace_];
        if (rideReward_ > 0) P().earn(rideReward_, true);
        if (ridePlace_ == 1) {
            P().stats.rideWins++;
            E().sfx.play(Sfx::Win);
            fx_.confetti({VW / 2.0f, VH / 2.0f}, 120, 700);
        } else {
            E().sfx.play(ridePlace_ <= 3 ? Sfx::Perfect : Sfx::Lose);
        }
        P().save();
        mode_ = Mode::RideResult;
        resultT_ = 0;
    }

    float riderScreenX(const Rider& r) const { return r.x - cam_; }
    float laneFeet(int lane) const { return 430 + lane * 62.0f; }

    // ============================================================ update/draw
    void update(float dt) override {
        t_ += dt;
        resultT_ += dt;
        fx_.update(dt);
        switch (mode_) {
            case Mode::BetRace: updateBetRace(dt); break;
            case Mode::RideCountdown:
            case Mode::Ride: updateRide(dt); break;
            default: break;
        }
        if (E().backPressed()) {
            if (mode_ == Mode::Hub) E().go(makeMenu);
            else if (mode_ == Mode::BetPick || mode_ == Mode::BetResult || mode_ == Mode::RideResult) mode_ = Mode::Hub;
        }
    }

    // smooth filled band between a curve and the bottom of the screen
    static void hillBand(float base, float scroll, float layer, Color top, Color bottom, float amp1, float amp2) {
        const int step = 16;
        for (int x = -step; x <= VW + step; x += step) {
            auto h = [&](float sx) {
                float wx = sx + scroll;
                return base + std::sin(wx * 0.004f + layer * 2) * amp1 + std::sin(wx * 0.011f + layer) * amp2;
            };
            float y0 = h((float)x), y1 = h((float)x + step);
            Vector2 a{(float)x, y0}, b{(float)x + step, y1}, c{(float)x + step, (float)VH}, d{(float)x, (float)VH};
            rlBegin(RL_TRIANGLES);
            for (int w = 0; w < 2; w++) {
                Vector2 v[6] = {a, b, c, a, c, d};
                Color cv[6] = {top, top, bottom, top, bottom, bottom};
                for (int k = 0; k < 6; k += 3) {
                    int i0 = k, i1 = w ? k + 2 : k + 1, i2 = w ? k + 1 : k + 2;
                    for (int i : {i0, i1, i2}) { rlColor4ub(cv[i].r, cv[i].g, cv[i].b, cv[i].a); rlVertex2f(v[i].x, v[i].y); }
                }
            }
            rlEnd();
            DrawLineEx(a, b, 2, alpha(WHITE, 0.12f));  // lit ridge
        }
    }

    void cloud(Vector2 p, float s, Color c) {
        // flat-bottomed cumulus: lit puffs on top, shaded base
        Color shade = mix(c, rgba(0xb08ac8ff), 0.35f);
        DrawEllipse((int)p.x, (int)(p.y + 6 * s), 80 * s, 16 * s, alpha(shade, c.a / 255.0f));
        const float px[] = {-52, -22, 12, 44}, py[] = {-4, -22, -30, -10}, pr[] = {26, 34, 38, 28};
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * s, p.y + py[i] * s + 4 * s}, pr[i] * s, alpha(shade, c.a / 255.0f));
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * s, p.y + py[i] * s}, pr[i] * s, c);
        DrawEllipse((int)p.x, (int)p.y, 78 * s, 14 * s, c);
        for (int i = 1; i < 3; i++) DrawCircleV({p.x + px[i] * s - 8 * s, p.y + py[i] * s - 10 * s}, pr[i] * 0.45f * s, alpha(WHITE, 0.35f * c.a / 255.0f));
    }

    void grandstand(float x0, float t) {
        // roof + pillars
        Color steel = rgba(0x1a1030ff);
        DrawTriangle({x0 - 20, 236}, {x0 + 720, 236}, {x0 + 700, 216}, rgba(0x5b2fb0ff));
        DrawTriangle({x0 - 20, 236}, {x0 + 700, 216}, {x0, 216}, rgba(0x5b2fb0ff));
        DrawRectangle((int)x0 - 20, 236, 740, 8, rgba(0x3a1d78ff));
        for (int i = 0; i < 8; i++) DrawRectangle((int)(x0 + i * 100), 244, 6, 56, steel);
        // tiered seating with a crowd of little people
        for (int row = 0; row < 3; row++) {
            float y = 262 + row * 16.0f;
            DrawRectangle((int)x0, (int)y + 8, 700, 10, row % 2 ? rgba(0x2a1a48ff) : rgba(0x24163fff));
            for (int k = 0; k < 46; k++) {
                float hx = x0 + 12 + k * 15 + (row % 2) * 7;
                float cheer = std::max(0.0f, std::sin(t * 5 + k * 1.7f + row));
                float hy = y + 4 - cheer * 4;
                Color shirt = ColorFromHSV((float)((k * 53 + row * 97) % 360), 0.55f, 0.85f);
                DrawRectangleRounded({hx - 5, hy, 10, 10}, 0.5f, 4, shirt);
                DrawCircleV({hx, hy - 3}, 4.2f, k % 3 == 0 ? rgba(0xc88a5aff) : k % 3 == 1 ? rgba(0xf0c8a0ff) : rgba(0x8a5a3aff));
                if (cheer > 0.85f && k % 4 == 0) DrawLineEx({hx + 4, hy + 2}, {hx + 8, hy - 9}, 2, shirt);  // waving arm
            }
        }
        // front wall with a banner
        DrawRectangle((int)x0, 300, 700, 22, rgba(0x2a1a48ff));
        DrawRectangleRounded({x0 + 230, 302, 240, 18}, 0.4f, 6, col::gold);
        ui::text("JUHUH DERBY", x0 + 350, 311, 14, col::outline, ui::Center, true);
        for (int f = 0; f < 5; f++) {
            float fx0 = x0 + 40 + f * 155;
            DrawLineEx({fx0, 216}, {fx0, 186}, 3, steel);
            float wave = std::sin(t * 5 + f) * 4;
            DrawTriangle({fx0, 186}, {fx0, 200}, {fx0 + 24, 193 + wave}, f % 2 ? col::pink : col::gold);
        }
    }

    void drawSky(float scroll) {
        DrawRectangleGradientV(0, 0, VW, 200, rgba(0x3b2470ff), rgba(0x8f5cc8ff));
        DrawRectangleGradientV(0, 200, VW, 180, rgba(0x8f5cc8ff), rgba(0xffb08aff));
        Vector2 sun{1040, 200};
        fx::beginAdd();
        fx::glow(sun, 420, alpha(rgba(0xffb36bff), 0.55f));
        fx::glow(sun, 160, alpha(rgba(0xfff0c0ff), 0.6f));
        fx::endAdd();
        DrawCircleV(sun, 64, rgba(0xfff3d0ff));
        for (int i = 0; i < 6; i++) {
            float x = std::fmod(i * 300.0f - scroll * 0.05f - t_ * (6 + i * 2), VW + 400.0f);
            if (x < 0) x += VW + 400;
            x -= 200;
            float y = 70 + (i * 53 % 140);
            cloud({x, y}, 0.6f + (i % 3) * 0.2f, alpha(i % 2 ? rgba(0xffe2eeff) : rgba(0xf6ecffff), 0.9f));
        }
        hillBand(262, scroll * 0.05f, 0, rgba(0xa47ac0ff), rgba(0x7a4f9aff), 30, 14);
        hillBand(300, scroll * 0.12f, 1, rgba(0x7a6aaaff), rgba(0x47386eff), 26, 12);
        float gx = std::fmod(-scroll * 0.3f, 1600.0f);
        for (int st = 0; st < 3; st++) {
            float x0 = gx + st * 1600 - 200;
            if (x0 > VW || x0 + 720 < 0) continue;
            grandstand(x0, t_);
        }
        hillBand(330, scroll * 0.3f, 2, rgba(0x5f9a52ff), rgba(0x2f5a2eff), 8, 4);
    }

    void drawDirt(float top, float scroll) {
        // grass verge, rail, then raked dirt
        DrawRectangleGradientV(0, (int)top - 16, VW, 16, rgba(0x4f8a42ff), rgba(0x3a6a32ff));
        DrawRectangleGradientV(0, (int)top, VW, VH - (int)top, rgba(0xa8743eff), rgba(0x6e4526ff));
        DrawRectangleGradientV(0, (int)top, VW, 30, alpha(BLACK, 0.25f), alpha(BLACK, 0));
        for (int i = 0; i < 9; i++) {  // rake lines
            float y = top + 24 + i * (VH - top) / 9.0f;
            DrawLineEx({0, y}, {(float)VW, y}, 1.5f, alpha(rgba(0x5a3820ff), 0.25f));
        }
        for (int i = 0; i < 70; i++) {  // clods + hoof prints
            float x = std::fmod(i * 97.0f - scroll, (float)VW + 40);
            if (x < 0) x += VW + 40;
            float y = top + 12 + (i * 37 % (int)(VH - top - 20));
            if (i % 3 == 0) {
                DrawEllipse((int)x, (int)y, 5, 2.5f, alpha(rgba(0x4a2a14ff), 0.5f));
                DrawEllipse((int)x + 9, (int)y + 2, 5, 2.5f, alpha(rgba(0x4a2a14ff), 0.5f));
            } else {
                DrawEllipse((int)x, (int)y, 4 + i % 5, 2, alpha(rgba(0xc8945aff), 0.5f));
            }
        }
        // white rail fence along the inside of the track
        float off = std::fmod(scroll, 90.0f);
        for (float x = -off; x < VW + 90; x += 90) {
            DrawRectangle((int)x, (int)top - 34, 6, 30, rgba(0xe8e2f0ff));
            DrawRectangle((int)x + 4, (int)top - 34, 2, 30, alpha(BLACK, 0.2f));
        }
        DrawRectangle(0, (int)top - 34, VW, 6, WHITE);
        DrawRectangle(0, (int)top - 22, VW, 4, rgba(0xe8e2f0ff));
    }

    void drawBetTrack() {
        drawSky(0);
        drawDirt(180, 0);
        for (int i = 0; i <= 6; i++) DrawRectangle(0, (int)laneY(i) - 40 + 6, VW, 2, alpha(WHITE, 0.15f));
        DrawRectangle(0, 176, VW, 8, WHITE);
        DrawRectangle(0, 644, VW, 8, WHITE);
        // start + finish lines
        DrawRectangle((int)screenX(0), 184, 4, 460, alpha(WHITE, 0.6f));
        float fx = screenX(kBetLength);
        for (int y = 184; y < 644; y += 16) {
            DrawRectangle((int)fx, y, 8, 8, WHITE);
            DrawRectangle((int)fx + 8, y + 8, 8, 8, WHITE);
            DrawRectangle((int)fx + 8, y, 8, 8, col::outline);
            DrawRectangle((int)fx, y + 8, 8, 8, col::outline);
        }
        for (int i = 0; i < 6; i++) {
            Runner& r = field_[i];
            float y = laneY(i);
            ui::text(std::to_string(i + 1), 24, y - 20, 22, r.silk, ui::Left, true);
            fx::softShadow({screenX(r.dist) - 40, y + 6}, 40, 8, 0.5f);
            art::horse({screenX(r.dist) - 40, y + 4}, 0.5f, r.phase, r.body, r.mane, r.silk);
            if (i == pick_) {
                DrawTriangle({screenX(r.dist) - 22, y - 86}, {screenX(r.dist) - 38, y - 102}, {screenX(r.dist) - 6, y - 102}, col::gold);
            }
            if (r.place > 0) ui::textOutline(r.place == 1 ? "1st" : r.place == 2 ? "2nd" : r.place == 3 ? "3rd" : std::to_string(r.place) + "th",
                                             screenX(r.dist) + 40, y - 30, 26, r.place == 1 ? col::gold : WHITE, col::outline, 2);
        }
    }

    // illustrated mode card for the hub
    bool modeCard(Rectangle r, const char* title, const char* sub, Color base, int kind) {
        bool hot = ui::hovered(r);
        float& h = hover_[kind];
        h = damp(h, hot ? 1.0f : 0.0f, 12, GetFrameTime());
        Rectangle d{r.x, r.y - h * 8, r.width, r.height};
        fx::softShadow({d.x + d.width / 2, d.y + d.height / 2 + 22}, d.width * 0.55f, d.height * 0.5f, 0.55f);
        fx::gradRoundRect(d, 0.12f, lighten(base, 0.15f + h * 0.08f), darken(base, 0.35f));
        DrawRectangleRoundedLinesEx(d, 0.12f, 12, 3, hot ? col::gold : col::outline);
        // illustration window
        Rectangle ill{d.x + 16, d.y + 16, d.width - 32, d.height - 104};
        fx::gradRoundRect(ill, 0.12f, alpha(BLACK, 0.15f), alpha(BLACK, 0.4f));
        if (kind == 0) {
            // betting slip, odds board and a pile of coins
            Vector2 c{ill.x + ill.width / 2, ill.y + ill.height / 2};
            rlPushMatrix();
            rlTranslatef(c.x - 50, c.y + 4, 0);
            rlRotatef(-8 + std::sin(t_ * 2) * 2, 0, 0, 1);
            DrawRectangleRounded({-60, -60, 120, 120}, 0.08f, 6, rgba(0xfff6e0ff));
            DrawRectangleRec({-60, -60, 120, 24}, col::red);
            ui::text("WIN", 0, -48, 16, WHITE, ui::Center, true);
            ui::text("#3", 0, -10, 30, col::outline, ui::Center, true);
            ui::text("3.5x", 0, 28, 22, rgba(0x1f8a4cff), ui::Center, true);
            for (int i = 0; i < 6; i++) DrawRectangle(-48 + i * 17, 48, 10, 4, alpha(col::outline, 0.4f));
            rlPopMatrix();
            for (int i = 0; i < 6; i++) ui::coin({c.x + 70 + (i % 2) * 6, c.y + 46 - i * 9.0f}, 18);
            for (int i = 0; i < 3; i++) ui::coin({c.x + 112, c.y + 46 - i * 9.0f}, 18);
        } else {
            art::horse({ill.x + ill.width / 2 - 20, ill.y + ill.height - 8}, 0.62f, t_ * 12, rgba(0x8b5a2bff), rgba(0x3a2010ff), col::violet, "pieter");
            Rectangle tag{ill.x + ill.width - 96, ill.y + 10, 86, 26};
            DrawRectangleRounded(tag, 1, 8, col::gold);
            ui::text("ENTRY 50", tag.x + tag.width / 2, tag.y + 13, 13, col::outline, ui::Center, true);
        }
        ui::textShadow(title, d.x + d.width / 2, d.y + d.height - 62, 40, WHITE, ui::Center, true, 3, darken(base, 0.7f));
        ui::text(sub, d.x + d.width / 2, d.y + d.height - 28, 17, alpha(col::text, 0.85f), ui::Center, true);
        if (hot && E().mousePressed()) {
            E().consumeClick();
            E().sfx.play(Sfx::Click);
            return true;
        }
        return false;
    }

    void drawHub() {
        drawSky(t_ * 60);
        drawDirt(470, t_ * 260);
        float hx = std::fmod(t_ * 260, VW + 400.0f) - 200;
        fx::softShadow({hx + 10, 604}, 80, 10, 0.4f);
        art::horse({hx, 600}, 1.1f, t_ * 14, rgba(0x8b5a2bff), rgba(0x3a2010ff), col::violet, "pieter");
        fx::beginAdd();
        fx::glow({200, 290}, 200, alpha(col::gold, 0.25f));
        fx::endAdd();
        art::portrait("horse", {200, 290}, 120, col::gold, std::sin(t_ * 2) * 6);
        ui::textOutline("JUHUH!", 200, 440, 50, col::gold, col::outline, 4);
        Rectangle r1{440, 120, 360, 300}, r2{830, 120, 360, 300};
        if (modeCard(r1, "BET", "pick a horse, win big", rgba(0x1f8a4cff), 0)) { newField(); mode_ = Mode::BetPick; }
        if (modeCard(r2, "RIDE", "be the jockey yourself", col::purple, 1)) startRide();
        Rectangle st{560, 486, 510, 34};
        DrawRectangleRounded(st, 1, 10, alpha(col::bgDeep, 0.7f));
        ui::text("Horse wins: " + std::to_string(P().stats.horseWins) + "      Races won as jockey: " + std::to_string(P().stats.rideWins),
                 st.x + st.width / 2, st.y + 17, 17, col::text, ui::Center, true);
    }

    void drawBetPick() {
        art::purpleBackdrop(t_);
        Rectangle list{40, 90, 760, 610};
        for (int i = 0; i < 6; i++) {
            Runner& r = field_[i];
            Rectangle c{list.x, list.y + i * 100.0f, list.width, 90};
            bool sel = pick_ == i, hot = ui::hovered(c);
            ui::panel(c, sel ? col::panelHi : (hot ? lighten(col::panel, 0.05f) : col::panel), 0.2f, sel ? col::gold : col::outline);
            art::horse({c.x + 70, c.y + 82}, 0.45f, t_ * (hot ? 12 : 3) + i, r.body, r.mane, r.silk);
            ui::text(std::to_string(i + 1) + ". " + r.name, c.x + 150, c.y + 26, 24, col::text, ui::Left, true);
            auto stat = [&](const char* label, float v, float x) {
                ui::text(label, x, c.y + 60, 14, col::muted);
                ui::bar({x + 62, c.y + 54, 80, 10}, v, r.silk);
            };
            stat("SPEED", (r.base - 64) / 10, c.x + 150);
            stat("FORM", 1 - (r.wobble - 0.05f) / 0.13f, c.x + 310);
            stat("STAMINA", r.stamina, c.x + 470 - 16);
            ui::text(TextFormat("%.1fx", podiumBet_ ? podiumOdds_[i] : winOdds_[i]), c.x + c.width - 20, c.y + 45, 34, col::gold, ui::Right, true);
            if (hot && E().mousePressed()) {
                E().consumeClick();
                pick_ = i;
                E().sfx.play(Sfx::Neigh, 0.35f, rnd(1.0f, 1.4f));
            }
        }
        Rectangle side{830, 90, 410, 610};
        ui::panel(side);
        ui::text("YOUR BET", side.x + 24, side.y + 34, 28, col::text, ui::Left, true);
        if (ui::button({side.x + 24, side.y + 70, 175, 50}, "WIN", podiumBet_ ? col::panelHi : col::purple, 22)) podiumBet_ = false;
        if (ui::button({side.x + 211, side.y + 70, 175, 50}, "TOP 3", podiumBet_ ? col::purple : col::panelHi, 22)) podiumBet_ = true;
        ui::paragraph(podiumBet_ ? "Pays if your horse finishes in the top 3. Lower odds, safer." : "Pays only if your horse finishes first.",
                      {side.x + 24, side.y + 135, side.width - 48, 60}, 18, col::muted);
        ui::betSelector({side.x + 20, side.y + 200, side.width - 40, 100}, bet_, P().coins);
        if (pick_ >= 0) {
            float odds = podiumBet_ ? podiumOdds_[pick_] : winOdds_[pick_];
            ui::text("Horse: " + field_[pick_].name, side.x + 24, side.y + 340, 22, col::text);
            ui::text("Potential win: " + fmtNum((long long)std::llround(bet_ * odds)), side.x + 24, side.y + 376, 22, col::gold);
        } else {
            ui::text("Click a horse to pick it", side.x + 24, side.y + 340, 22, col::muted);
        }
        if (ui::button({side.x + 24, side.y + 430, side.width - 48, 64}, "START RACE", col::green, 30, pick_ >= 0 && P().coins >= bet_))
            startBetRace();
        if (ui::button({side.x + 24, side.y + 510, side.width - 48, 50}, "NEW HORSES", col::panelHi, 20)) newField();
    }

    void drawRide() {
        drawSky(cam_);
        float ground = 340;
        drawDirt(ground, cam_);
        // rail posts
        for (float wx = std::floor(cam_ / 120) * 120; wx < cam_ + VW + 120; wx += 120) {
            float x = wx - cam_;
            DrawRectangle((int)x, (int)ground - 30, 8, 40, WHITE);
        }
        DrawRectangle(0, (int)ground - 28, VW, 6, WHITE);
        // finish
        float fx = kRideLength - cam_;
        if (fx > -20 && fx < VW + 20)
            for (int y = (int)ground; y < VH; y += 16) {
                DrawRectangle((int)fx, y, 8, 8, WHITE);
                DrawRectangle((int)fx + 8, y + 8, 8, 8, WHITE);
            }
        // distance markers
        for (float m = 500; m < kRideLength; m += 500) {
            float x = m - cam_;
            if (x > -50 && x < VW + 50) ui::text(std::to_string((int)(kRideLength - m)) + "m", x, ground - 50, 18, WHITE, ui::Center, true);
        }
        // lanes back to front, hurdles drawn per lane
        for (int lane = 0; lane < 4; lane++) {
            float feet = laneFeet(lane);
            for (float h : hurdles_) {
                float x = h - cam_ + lane * 10;
                if (x < -40 || x > VW + 40) continue;
                // brush hurdle with striped poles
                DrawEllipse((int)x, (int)feet, 26, 5, alpha(BLACK, 0.25f));
                DrawRectangleRounded({x - 18, feet - 30, 36, 30}, 0.5f, 8, rgba(0x3a6a2aff));
                for (int k = 0; k < 6; k++) DrawLineEx({x - 16 + k * 6.0f, feet - 2}, {x - 18 + k * 7.0f, feet - 36}, 2, rgba(0x5a8a3aff));
                for (int sd : {-1, 1}) {
                    DrawRectangle((int)(x + sd * 20) - 3, (int)feet - 48, 6, 48, WHITE);
                    for (int k = 0; k < 3; k++) DrawRectangle((int)(x + sd * 20) - 3, (int)feet - 46 + k * 16, 6, 7, col::red);
                }
                DrawRectangle((int)x - 22, (int)feet - 44, 44, 5, WHITE);
            }
            for (auto& r : riders_) {
                if (r.lane != lane) continue;
                float jump = r.jumpT >= 0 ? std::sin(r.jumpT / 0.62f * PI) * 90 : 0;
                float x = riderScreenX(r);
                DrawEllipse((int)x, (int)feet, 50 - jump * 0.2f, 8, alpha(BLACK, 0.25f));
                art::horse({x, feet - jump}, 0.62f, r.phase, r.body, r.mane, r.silk, r.jockey);
                if (r.stumble > 0) ui::text("@#!", x, feet - 130, 20, col::red, ui::Center, true);
                if (r.player) DrawTriangle({x - 10, feet - 150 - jump}, {x + 10, feet - 150 - jump}, {x, feet - 136 - jump}, col::gold);
            }
        }
        fx_.draw();
        // HUD
        Rider& me = riders_[0];
        Rectangle prog{240, 90, 800, 16};
        ui::bar(prog, me.x / kRideLength, col::violet);
        for (auto& r : riders_) {
            float px = prog.x + prog.width * std::min(1.0f, r.x / kRideLength);
            DrawCircleV({px, prog.y + 8}, 9, r.silk);
            ui::circleOutline({px, prog.y + 8}, 9, col::outline);
        }
        ui::text("SPEED", 40, 140, 16, col::muted);
        ui::bar({40, 152, 220, 16}, me.speed / kRideMax, col::orange);
        ui::text("STAMINA", 40, 186, 16, col::muted);
        ui::bar({40, 198, 220, 16}, stamina_ / 100, stamina_ < 25 ? col::red : col::green);
        ui::text("Alternate  LEFT / RIGHT  (or A / D) to gallop    SPACE to jump", VW / 2.0f, VH - 22, 18, alpha(WHITE, 0.8f), ui::Center);

        if (mode_ == Mode::RideCountdown) {
            int n = (int)std::ceil(countdown_);
            float k = countdown_ - std::floor(countdown_);
            ui::textOutline(n > 0 ? std::to_string(n) : "GO!", VW / 2.0f, VH / 2.0f - 60, 120 * (0.8f + k * 0.4f), col::gold, col::outline, 6);
        }
    }

    void drawResult(const std::string& headline, Color c, const std::string& sub, long long won, bool againBet) {
        float a = clamp01(resultT_ / 0.4f);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.5f * a));
        Rectangle r{VW / 2.0f - 280, VH / 2.0f - 170, 560, 340};
        ui::panel(r);
        ui::textOutline(headline, r.x + r.width / 2, r.y + 70, 54, c, col::outline, 4);
        ui::text(sub, r.x + r.width / 2, r.y + 135, 22, col::muted, ui::Center);
        if (won > 0) {
            ui::coin({r.x + r.width / 2 - 80, r.y + 185}, 18);
            ui::text("+" + fmtNum(won), r.x + r.width / 2 - 52, r.y + 185, 36, col::gold);
        }
        if (ui::button({r.x + 40, r.y + 250, 230, 60}, againBet ? "NEW RACE" : "RACE AGAIN", col::purple)) {
            if (againBet) { newField(); mode_ = Mode::BetPick; }
            else startRide();
        }
        if (ui::button({r.x + 290, r.y + 250, 230, 60}, "BACK", col::panelHi)) mode_ = Mode::Hub;
    }

    void draw() override {
        switch (mode_) {
            case Mode::Hub: drawHub(); break;
            case Mode::BetPick: drawBetPick(); break;
            case Mode::BetRace:
            case Mode::BetResult:
                drawBetTrack();
                fx_.draw();
                DrawRectangleRounded({VW / 2.0f - 400, 110, 800, 50}, 0.5f, 10, alpha(col::bgDeep, 0.85f));
                ui::text(raceT_ < 0 ? "Ready... " + std::to_string((int)std::ceil(-raceT_)) : commentary_, VW / 2.0f, 135, 26, col::gold, ui::Center, true);
                if (mode_ == Mode::BetResult && resultT_ > 0.6f) {
                    Runner& m = field_[pick_];
                    bool win = payout_ > 0;
                    drawResult(win ? "YOU WIN!" : "UNLUCKY", win ? col::gold : col::red,
                               m.name + " finished " + std::to_string(m.place) + (m.place == 1 ? "st" : m.place == 2 ? "nd" : m.place == 3 ? "rd" : "th"),
                               payout_, true);
                }
                break;
            case Mode::RideCountdown:
            case Mode::Ride:
            case Mode::RideResult:
                drawRide();
                if (mode_ == Mode::RideResult && resultT_ > 0.5f) {
                    const char* names[] = {"", "1ST PLACE!", "2ND PLACE", "3RD PLACE", "4TH PLACE"};
                    drawResult(names[ridePlace_], ridePlace_ == 1 ? col::gold : ridePlace_ <= 3 ? col::cyan : col::red,
                               ridePlace_ == 1 ? "JUHUH! Pieter is the best jockey alive" : "time: " + std::string(TextFormat("%.1fs", rideT_)),
                               rideReward_, false);
                }
                break;
        }
        ui::titleBar(mode_ == Mode::Ride || mode_ == Mode::RideCountdown ? "" : "HORSE RACING", col::orange);
        if (mode_ != Mode::BetRace && mode_ != Mode::Ride && mode_ != Mode::RideCountdown && ui::backButton()) {
            if (mode_ == Mode::Hub) E().go(makeMenu);
            else mode_ = Mode::Hub;
        }
        ui::coinCounter(VW - 20, 38, P().coins);
    }

private:
    Mode mode_ = Mode::Hub;
    float t_ = 0, resultT_ = 0, hover_[2]{};
    // bet
    std::vector<Runner> field_;
    std::vector<float> winOdds_, podiumOdds_;
    int pick_ = -1, finished_ = 0, leader_ = -1;
    bool podiumBet_ = false;
    long long bet_ = 50, payout_ = 0;
    float raceT_ = 0;
    std::string commentary_;
    // ride
    std::vector<Rider> riders_;
    std::vector<float> hurdles_;
    float stamina_ = 100, rideT_ = 0, countdown_ = 3, cam_ = 0, lastTap_ = 0;
    int lastKey_ = 0, ridePlace_ = 0;
    long long rideReward_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeHorses() { return std::make_unique<HorseScene>(); }

}  // namespace pg
