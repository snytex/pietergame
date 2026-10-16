// FIETSEN!: endless bike ride along an Amsterdam canal on Pieter's omafiets.
// Three lanes on the red fietspad: switch lanes, jump the small stuff, and
// whatever you do, don't end up in the canal.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/ModeArt.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>

namespace pg {
namespace {

constexpr float kPlayerX = 280;
const float kLaneY[3] = {492, 568, 650};
const float kLaneS[3] = {0.82f, 0.91f, 1.0f};

enum class Kind { Tourist, Scooter, Rack, Bollard, Pothole, Duck, Coin, Wafel, Boba, Bell };
bool tall(Kind k) { return k == Kind::Tourist || k == Kind::Scooter || k == Kind::Rack; }
bool pickup(Kind k) { return k == Kind::Coin || k == Kind::Wafel || k == Kind::Boba || k == Kind::Bell; }

struct Thing {
    Kind kind;
    float x;      // world-to-screen x (scrolls left)
    int lane;
    float speed = 0;  // own movement (scooters come at you, tourists dawdle)
    float hue = 0;
    bool gone = false;
    float laneT = 0;  // tourists drifting between lanes
    int toLane = -1;
};

enum class Phase { Ready, Ride, Splash, Over };

class FietsScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Ride) return {"Fietsen!", std::to_string((int)dist_) + " m along the canal"};
        return {"Fietsen!", "Best: " + std::to_string(P().stats.fietsBest) + " m"};
    }

    void enter() override {
        reset();
        if (std::getenv("PG_FIETS")) phase_ = Phase::Ride;  // dev hook: skip the start screen
    }

    void reset() {
        things_.clear();
        dist_ = 0;
        speed_ = 380;
        lane_ = 1;
        y_ = kLaneY[1];
        jumpT_ = -1;
        coins_ = 0;
        shield_ = false;
        bellT_ = 0;
        spawnX_ = 1500;
        phase_ = Phase::Ready;
        phaseT_ = 0;
        fx_.clear();
    }

    // ------------------------------------------------------------ spawning
    void spawnChunk() {
        // a "beat" of obstacles and pickups, harder as the speed goes up
        float hard = clamp01((speed_ - 380) / 520);
        int pattern = rndi(0, 6);
        float x = spawnX_;
        auto add = [&](Kind k, float dx, int lane) {
            Thing t{k, x + dx, lane};
            if (k == Kind::Scooter) t.speed = -rnd(160, 260);
            if (k == Kind::Tourist) {
                t.speed = rnd(-40, 30);
                t.hue = rnd(0, 360);
                if (chance(0.25f + hard * 0.3f)) t.toLane = std::clamp(lane + (chance(0.5f) ? 1 : -1), 0, 2);
            }
            if (k == Kind::Duck) t.speed = -30;
            things_.push_back(t);
        };
        int free = rndi(0, 2);
        switch (pattern) {
            case 0:  // two lanes blocked by tourists, coins in the free one
                for (int l = 0; l < 3; l++)
                    if (l != free) add(Kind::Tourist, rnd(-30, 30), l);
                for (int i = 0; i < 5; i++) add(Kind::Coin, -40 + i * 50.0f, free);
                break;
            case 1:  // bollards across the path: jump!
                for (int l = 0; l < 3; l++) add(Kind::Bollard, 0, l);
                for (int i = 0; i < 3; i++) add(Kind::Coin, i * 45.0f, rndi(0, 2));
                break;
            case 2:  // a scooter coming at you
                add(Kind::Scooter, 300, rndi(0, 2));
                add(Kind::Coin, 0, free);
                add(Kind::Coin, 50, free);
                break;
            case 3:  // bike rack + pothole
                add(Kind::Rack, 0, free);
                add(Kind::Pothole, 120, (free + 1) % 3);
                for (int i = 0; i < 4; i++) add(Kind::Coin, 100 + i * 45.0f, (free + 2) % 3);
                break;
            case 4:  // a duck family crossing
                for (int i = 0; i < 3; i++) add(Kind::Duck, i * 34.0f, rndi(0, 2));
                break;
            case 5:  // coin arc over a pothole
                add(Kind::Pothole, 0, free);
                for (int i = 0; i < 5; i++) add(Kind::Coin, -80 + i * 40.0f, free);
                break;
            default: {  // power-up between tourists
                Kind pu = chance(0.4f) ? Kind::Wafel : chance(0.5f) ? Kind::Boba : Kind::Bell;
                add(pu, 0, free);
                add(Kind::Tourist, 160, (free + 1) % 3);
                break;
            }
        }
        spawnX_ += rnd(380, 560) * (1.1f - hard * 0.35f) + speed_ * 0.25f;
    }

    // ------------------------------------------------------------ update
    bool jumping() const { return jumpT_ >= 0; }
    float jumpH() const { return jumping() ? std::sin(jumpT_ / 0.62f * PI) * 90 : 0; }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        phaseT_ += dt;
        if (phase_ == Phase::Ready) {
            scroll_ += dt * 60;
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN) || E().mousePressed()) {
                E().consumeClick();
                phase_ = Phase::Ride;
                E().sfx.play(Sfx::Bell, 0.6f, 1.4f);
            }
            if (E().backPressed()) E().go(makeMenu);
            return;
        }
        if (phase_ == Phase::Splash) {
            phaseT_ += 0;
            fallV_.y += 1400 * dt;
            fallP_ += fallV_ * dt;
            fallRot_ += dt * 600;
            if (!splashed_ && fallP_.y > 400) {
                splashed_ = true;
                E().sfx.play(Sfx::Water, 0.9f);
                E().sfx.play(Sfx::Bubble, 0.8f, 0.6f);
                for (int i = 0; i < 40; i++) {
                    Particle p;
                    p.pos = {fallP_.x + rnd(-30, 30), 400};
                    p.vel = {rnd(-180, 180), rnd(-520, -200)};
                    p.gravity = 1200;
                    p.life = p.maxLife = rnd(0.5f, 1.0f);
                    p.size = rnd(3, 7);
                    p.color = alpha(rgba(0xbfeaffff), 0.9f);
                    fx_.add(p);
                }
            }
            if (phaseT_ > 1.6f) gameOver();
            return;
        }
        if (phase_ == Phase::Over) {
            if (E().backPressed()) E().go(makeMenu);
            return;
        }

        // --- riding
        speed_ = std::min(950.0f, speed_ + dt * 9);
        float move = speed_ * dt;
        dist_ += move / 40;
        scroll_ += move;
        pedal_ += dt * speed_ / 40;
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { lane_ = std::max(0, lane_ - 1); E().sfx.play(Sfx::Whoosh, 0.25f, 1.4f); }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { lane_ = std::min(2, lane_ + 1); E().sfx.play(Sfx::Whoosh, 0.25f, 1.4f); }
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J)) && !jumping()) {
            jumpT_ = 0;
            E().sfx.play(Sfx::Jump, 0.6f);
        }
        if (jumping()) {
            jumpT_ += dt;
            if (jumpT_ > 0.62f) {
                jumpT_ = -1;
                E().sfx.play(Sfx::Drop, 0.4f, 1.3f);
            }
        }
        y_ = damp(y_, kLaneY[lane_], 18, dt);
        bellT_ = std::max(0.0f, bellT_ - dt);
        if (E().backPressed()) E().go(makeMenu);

        // spawn ahead
        spawnX_ -= move;
        while (spawnX_ < VW + 600) spawnChunk();

        for (auto& th : things_) {
            th.x -= move + th.speed * dt;
            if (th.toLane >= 0 && th.x < 1000) {
                th.laneT += dt * 1.2f;
                if (th.laneT >= 1) { th.lane = th.toLane; th.toLane = -1; }
            }
            if (bellT_ > 0 && th.kind == Kind::Tourist && th.x < 760 && th.x > kPlayerX && !th.gone) {
                th.gone = true;  // they jump aside when they hear the bell
                fx_.floatText("sorry!", {th.x, kLaneY[th.lane] - 120}, WHITE, 18);
            }
            if (th.gone) continue;
            float lane = th.toLane >= 0 && th.laneT > 0 ? lerpf((float)th.lane, (float)th.toLane, easeInOutSine(th.laneT)) : (float)th.lane;
            int nearLane = (int)std::round(lane);
            if (nearLane != lane_ || std::abs(kLaneY[lane_] - y_) > 30) continue;
            float reach = pickup(th.kind) ? 44 : th.kind == Kind::Rack ? 70 : 38;
            if (std::abs(th.x - kPlayerX) > reach) continue;
            if (pickup(th.kind)) {
                collect(th);
            } else if (!tall(th.kind) && jumpH() > 30) {
                // cleared it
            } else {
                hit(th);
                if (phase_ != Phase::Ride) return;
            }
        }
        std::erase_if(things_, [](const Thing& th) { return th.x < -200 || (th.gone && pickup(th.kind)); });
    }

    void collect(Thing& th) {
        th.gone = true;
        Vector2 at{th.x, kLaneY[th.lane] - 70};
        switch (th.kind) {
            case Kind::Coin:
                coins_++;
                E().sfx.play(Sfx::Coin, 0.35f, 1 + std::fmod(dist_, 0.3f));
                fx_.burst(at, 6, col::gold, 160, 4, 0.3f, PShape::Spark);
                break;
            case Kind::Wafel:
                coins_ += 10;
                E().sfx.play(Sfx::Gem, 0.7f);
                fx_.floatText("+10", at, col::gold, 26);
                break;
            case Kind::Boba:
                shield_ = true;
                E().sfx.play(Sfx::Power, 0.6f);
                fx_.floatText("BOBA SHIELD", at, col::cyan, 24);
                break;
            default:
                bellT_ = 6;
                E().sfx.play(Sfx::Bell, 0.8f);
                fx_.floatText("RING RING!", at, col::gold, 26);
                break;
        }
    }

    void hit(Thing& th) {
        if (shield_) {
            shield_ = false;
            th.gone = true;
            E().sfx.play(Sfx::Pop, 0.8f);
            E().shake(6);
            fx_.burst({th.x, kLaneY[th.lane] - 60}, 20, col::cyan, 300, 6, 0.5f, PShape::Bubble);
            return;
        }
        // into the canal you go
        phase_ = Phase::Splash;
        phaseT_ = 0;
        fallP_ = {kPlayerX, y_ - 60 - jumpH()};
        fallV_ = {-120, -780};
        fallRot_ = 0;
        splashed_ = false;
        E().sfx.play(Sfx::Hurt);
        E().sfx.play(Sfx::Yawn, 0.6f, 1.6f);
        E().shake(12);
    }

    void gameOver() {
        phase_ = Phase::Over;
        phaseT_ = 0;
        int d = (int)dist_;
        best_ = d > P().stats.fietsBest;
        P().stats.fietsBest = std::max(P().stats.fietsBest, d);
        earned_ = coins_ + d / 25;
        if (earned_ > 0) P().earn(earned_);
        P().save();
        E().sfx.play(best_ ? Sfx::Win : Sfx::Lose, 0.7f);
    }

    // ------------------------------------------------------------ draw: world
    void canalHouse(float x, float baseY, float w, float h, int seed, float shade) {
        const Color walls[] = {rgba(0x8a3a2aff), rgba(0x6a2a20ff), rgba(0xd8c8a8ff), rgba(0x3a4a6aff), rgba(0x5a3a28ff), rgba(0xb8783aff)};
        Color wc = darken(walls[seed % 6], shade);
        DrawRectangleRec({x, baseY - h, w, h}, wc);
        // gable: stepped, bell or neck
        int g = seed % 3;
        if (g == 0)
            for (int s = 1; s <= 3; s++) DrawRectangleRec({x + s * w / 8, baseY - h - s * 9, w - s * w / 4, 9}, wc);
        else if (g == 1) {
            DrawCircleSector({x + w / 2, baseY - h}, w * 0.38f, 180, 360, 16, wc);
            DrawRectangleRec({x + w / 2 - 4, baseY - h - w * 0.5f, 8, 10}, wc);
        } else {
            DrawRectangleRec({x + w * 0.25f, baseY - h - 22, w * 0.5f, 22}, wc);
            DrawTriangle({x + w * 0.25f, baseY - h - 22}, {x + w / 2, baseY - h - 40}, {x + w * 0.75f, baseY - h - 22}, wc);
            DrawTriangle({x + w * 0.25f, baseY - h - 22}, {x + w * 0.75f, baseY - h - 22}, {x + w / 2, baseY - h - 40}, wc);
        }
        // white-framed windows and a door
        Color frame = darken(rgba(0xf4f0e8ff), shade);
        Color glass = darken(rgba(0x2a3a5aff), shade);
        int cols = w > 70 ? 3 : 2;
        for (int r = 0; r < (int)(h / 46); r++)
            for (int c = 0; c < cols; c++) {
                Rectangle wr{x + 10 + c * (w - 20) / cols + 3, baseY - h + 14 + r * 46.0f, (w - 20) / cols - 6, 30};
                if (wr.y + wr.height > baseY - 40) continue;
                DrawRectangleRec(shrink(wr, -2), frame);
                DrawRectangleRec(wr, glass);
                DrawLineEx({wr.x + wr.width / 2, wr.y}, {wr.x + wr.width / 2, wr.y + wr.height}, 2, frame);
            }
        DrawRectangleRec({x + w / 2 - 10, baseY - 36, 20, 36}, darken(rgba(0x2a5a3aff), shade));
        DrawRectangleRec({x + w / 2 - 14, baseY - 38, 28, 4}, frame);
    }

    void drawWorld() {
        // sky
        DrawRectangleGradientV(0, 0, VW, 360, rgba(0x6ab8ffff), rgba(0xd8eeffff));
        fx::beginAdd();
        fx::glow({1040, 90}, 200, alpha(rgba(0xfff0c0ff), 0.6f));
        fx::endAdd();
        DrawCircleV({1040, 90}, 40, rgba(0xfff6d0ff));
        for (int i = 0; i < 5; i++) {
            float x = std::fmod(i * 310 - scroll_ * 0.03f, 1600.0f);
            if (x < -200) x += 1600;
            x -= 150;
            float y = 60 + i * 23 % 90;
            for (int k = 0; k < 4; k++) DrawCircleV({x + k * 30.0f, y - (k % 2) * 12}, 24.0f + (k % 2) * 8, alpha(WHITE, 0.9f));
        }
        // back row of houses (far, hazier) then the canal-side row
        for (int layer = 0; layer < 2; layer++) {
            float par = layer == 0 ? 0.25f : 0.5f, base = layer == 0 ? 300 : 352, sc = layer == 0 ? 0.7f : 1.0f;
            float off = std::fmod(scroll_ * par, 2400.0f);
            for (int i = 0; i < 30; i++) {
                float w = (60 + (i * 37 % 40)) * sc, h = (130 + (i * 53 % 90)) * sc;
                float x = i * 82 * sc - off;
                if (x < -120) x += 2400 * (layer == 0 ? 1 : 1);
                if (x > VW + 50 || x + w < -50) continue;
                canalHouse(x, base, w, h, i * 7 + layer * 3, layer == 0 ? 0.35f : 0.0f);
            }
            if (layer == 0) DrawRectangleGradientV(0, 160, VW, 150, alpha(rgba(0xd8eeffff), 0), alpha(rgba(0xd8eeffff), 0.45f));
        }
        // quay + canal water with reflections and passing boats
        DrawRectangle(0, 352, VW, 10, rgba(0x6a6a72ff));
        DrawRectangleGradientV(0, 362, VW, 76, rgba(0x2a5a8aff), rgba(0x1a3a5aff));
        for (int i = 0; i < 22; i++) {
            float x = std::fmod(i * 97 - scroll_ * 0.55f + std::sin(t_ + i) * 10, 1400.0f) - 60;
            if (x < -60) x += 1400;
            DrawLineEx({x, 375 + (i % 5) * 12.0f}, {x + 30, 375 + (i % 5) * 12.0f}, 2, alpha(WHITE, 0.3f));
        }
        float bx = std::fmod(1600 - scroll_ * 0.35f + t_ * 40, 2200.0f) - 300;
        DrawRectangleRounded({bx, 388, 200, 30}, 0.5f, 8, rgba(0x8a5a32ff));
        DrawRectangleRounded({bx + 30, 370, 120, 22}, 0.3f, 8, rgba(0xe8e2d8ff));
        for (int k = 0; k < 4; k++) DrawRectangleRec({bx + 40 + k * 26.0f, 375, 16, 10}, rgba(0x3a5a8aff));
        // trees + railing between canal and road
        float to = std::fmod(scroll_ * 0.9f, 260.0f);
        for (float x = -to; x < VW + 260; x += 260) {
            DrawRectangle((int)x + 18, 340, 10, 104, rgba(0x4a3020ff));
            DrawCircleV({x + 23, 330}, 44, rgba(0x3a7a3aff));
            DrawCircleV({x + 5, 312}, 32, rgba(0x4a9a4aff));
            DrawCircleV({x + 44, 318}, 30, rgba(0x3a8a3aff));
        }
        float ro = std::fmod(scroll_, 60.0f);
        DrawRectangle(0, 436, VW, 5, rgba(0x2a2a30ff));
        for (float x = -ro; x < VW + 60; x += 60) DrawRectangle((int)x, 422, 4, 20, rgba(0x2a2a30ff));
        // the road: red fietspad lanes with markings
        DrawRectangleGradientV(0, 444, VW, VH - 444, rgba(0xa8443aff), rgba(0x7a2a24ff));
        DrawRectangle(0, 444, VW, 8, rgba(0x9a9aa2ff));
        float mo = std::fmod(scroll_, 140.0f);
        for (int l = 0; l < 2; l++) {
            float y = (kLaneY[l] + kLaneY[l + 1]) / 2 + 6;
            for (float x = -mo; x < VW + 140; x += 140) DrawRectangleRec({x, y, 70, 5}, alpha(WHITE, 0.75f));
        }
        float so = std::fmod(scroll_, 700.0f);
        for (float x = -so + 200; x < VW + 700; x += 700) {  // painted bike symbols
            DrawRing({x, kLaneY[1] + 30}, 10, 13, 0, 360, 20, alpha(WHITE, 0.5f));
            DrawRing({x + 34, kLaneY[1] + 30}, 10, 13, 0, 360, 20, alpha(WHITE, 0.5f));
            DrawLineEx({x, kLaneY[1] + 30}, {x + 17, kLaneY[1] + 16}, 3, alpha(WHITE, 0.5f));
            DrawLineEx({x + 17, kLaneY[1] + 16}, {x + 34, kLaneY[1] + 30}, 3, alpha(WHITE, 0.5f));
        }
    }

    void drawThing(const Thing& th) {
        if (th.gone && !pickup(th.kind)) {
            if (th.kind != Kind::Tourist) return;
        }
        float lane = th.toLane >= 0 && th.laneT > 0 ? lerpf((float)th.lane, (float)th.toLane, easeInOutSine(th.laneT)) : (float)th.lane;
        int li = std::clamp((int)std::round(lane), 0, 2);
        float y = lerpf(kLaneY[(int)std::floor(lane)], kLaneY[std::min(2, (int)std::floor(lane) + 1)], lane - std::floor(lane));
        float s = kLaneS[li];
        float x = th.x;
        if (th.gone && th.kind == Kind::Tourist) y -= 40, x += 30;  // jumped aside
        fx::softShadow({x, y + 4}, 34 * s, 7 * s, 0.4f);
        switch (th.kind) {
            case Kind::Tourist: {
                // tourist with a camera and a map, looking the wrong way
                float bob = std::sin(t_ * 6 + x * 0.01f) * 2;
                Color shirt = ColorFromHSV(th.hue, 0.5f, 0.85f);
                DrawRectangleRounded({x - 12 * s, y - 30 * s, 10 * s, 30 * s}, 0.5f, 6, rgba(0x3a3a5aff));
                DrawRectangleRounded({x + 2 * s, y - 30 * s, 10 * s, 30 * s}, 0.5f, 6, rgba(0x3a3a5aff));
                DrawRectangleRounded({x - 20 * s, y - 80 * s + bob, 40 * s, 54 * s}, 0.4f, 8, shirt);
                DrawCircleV({x, y - 96 * s + bob}, 16 * s, rgba(0xf0c8a0ff));
                DrawRectangleRounded({x - 18 * s, y - 116 * s + bob, 36 * s, 10 * s}, 1, 6, rgba(0xe8e2d0ff));  // sun hat
                DrawRectangleRec({x - 10 * s, y - 64 * s + bob, 20 * s, 14 * s}, rgba(0x2a2a30ff));             // camera
                DrawRectangleRec({x - 34 * s, y - 72 * s + bob, 20 * s, 26 * s}, rgba(0xf4f0e0ff));             // map
                if (th.toLane >= 0 && th.laneT < 0.05f && th.x < 1100) ui::text("!", x, y - 140 * s, 30, col::gold, ui::Center, true);
                break;
            }
            case Kind::Scooter: {
                DrawCircleV({x - 24 * s, y - 10 * s}, 11 * s, rgba(0x1a1a20ff));
                DrawCircleV({x + 24 * s, y - 10 * s}, 11 * s, rgba(0x1a1a20ff));
                DrawRectangleRounded({x - 30 * s, y - 38 * s, 60 * s, 24 * s}, 0.5f, 8, col::red);
                DrawLineEx({x - 26 * s, y - 36 * s}, {x - 32 * s, y - 70 * s}, 4 * s, rgba(0x6a6a72ff));
                DrawCircleV({x + 4 * s, y - 70 * s}, 15 * s, rgba(0x2a2a30ff));  // helmet
                DrawRectangleRounded({x - 6 * s, y - 60 * s, 22 * s, 30 * s}, 0.4f, 6, rgba(0x2a4a8aff));
                fx::beginAdd();
                fx::glow({x - 36 * s, y - 46 * s}, 40 * s, alpha(col::gold, 0.6f));
                fx::endAdd();
                break;
            }
            case Kind::Rack:
                for (int i = 0; i < 3; i++) {
                    float bx = x - 60 * s + i * 50 * s;
                    art::bike({bx - 20 * s, y - 14 * s}, 70 * s, 0.5f, ColorFromHSV(i * 110.0f + 30, 0.6f, 0.6f));
                }
                DrawRectangleRec({x - 80 * s, y - 34 * s, 160 * s, 5 * s}, rgba(0x9a9aa2ff));
                break;
            case Kind::Bollard:
                DrawRectangleRounded({x - 7 * s, y - 50 * s, 14 * s, 50 * s}, 0.5f, 6, rgba(0x6a2a24ff));
                DrawRectangleRec({x - 7 * s, y - 40 * s, 14 * s, 6 * s}, WHITE);
                DrawCircleV({x, y - 50 * s}, 8 * s, rgba(0x6a2a24ff));
                break;
            case Kind::Pothole:
                DrawEllipse((int)x, (int)y - 2, 36 * s, 10 * s, rgba(0x2a1a14ff));
                DrawEllipse((int)x, (int)y - 4, 30 * s, 7 * s, rgba(0x3a5a8aff));
                break;
            case Kind::Duck: {
                float w = std::sin(t_ * 12 + x) * 3;
                DrawEllipse((int)x, (int)(y - 14 * s), 16 * s, 11 * s, rgba(0xf4f0e8ff));
                DrawCircleV({x - 13 * s, y - 26 * s + w}, 8 * s, rgba(0x2a7a3aff));
                DrawTriangle({x - 20 * s, y - 26 * s + w}, {x - 28 * s, y - 23 * s + w}, {x - 20 * s, y - 22 * s + w}, col::orange);
                DrawTriangle({x - 20 * s, y - 26 * s + w}, {x - 20 * s, y - 22 * s + w}, {x - 28 * s, y - 23 * s + w}, col::orange);
                break;
            }
            case Kind::Coin: {
                float sq = std::abs(std::cos(t_ * 4 + x * 0.02f));
                Vector2 c{x, y - 60 * s + std::sin(t_ * 5 + x * 0.05f) * 4};
                DrawEllipse((int)c.x, (int)c.y, 13 * s * std::max(0.2f, sq), 13 * s, col::outline);
                DrawEllipse((int)c.x, (int)c.y, 11 * s * std::max(0.2f, sq), 11 * s, col::gold);
                break;
            }
            case Kind::Wafel:
                art::stroopwafelTop({x, y - 60 * s + std::sin(t_ * 4) * 5}, 22 * s);
                break;
            case Kind::Boba:
                fx::beginAdd();
                fx::glow({x, y - 64 * s}, 40, alpha(col::cyan, 0.5f));
                fx::endAdd();
                art::boba({x, y - 60 * s + std::sin(t_ * 4) * 5}, 44 * s);
                break;
            case Kind::Bell:
                fx::beginAdd();
                fx::glow({x, y - 64 * s}, 40, alpha(col::gold, 0.5f));
                fx::endAdd();
                DrawCircleSector({x, y - 60 * s}, 18 * s, 180, 360, 16, col::gold);
                DrawRectangleRec({x - 18 * s, y - 60 * s, 36 * s, 6 * s}, darken(col::gold, 0.2f));
                DrawCircleV({x, y - 52 * s}, 4 * s, col::goldDark);
                break;
        }
    }

    void drawPlayer() {
        if (phase_ == Phase::Splash || phase_ == Phase::Over) {
            // the empty bike rolls on; Pieter flies into the canal
            art::bike({kPlayerX - 60 + phaseT_ * 200, y_ - 22}, 115, pedal_, rgba(0x1a1a24ff));
            if (!splashed_) art::pieter(fallP_, 30, P().hat, fallRot_);
            return;
        }
        float jh = jumpH();
        float s = lerpf(kLaneS[0], kLaneS[2], (y_ - kLaneY[0]) / (kLaneY[2] - kLaneY[0]));
        fx::softShadow({kPlayerX, y_ + 4}, 70 * s * (1 - jh / 200), 9 * s, 0.45f);
        Vector2 wb{kPlayerX - 60 * s, y_ - 22 * s - jh};
        float lean = jumping() ? -8 : 0;
        rlPushMatrix();
        rlTranslatef(kPlayerX, y_ - jh, 0);
        rlRotatef(lean, 0, 0, 1);
        rlTranslatef(-kPlayerX, -(y_ - jh), 0);
        art::bike(wb, 115 * s, pedal_, rgba(0x1a1a24ff));
        // rider: body, arm to the bars, head
        Vector2 seat{wb.x + 115 * s * 0.3f, wb.y - 115 * s * 0.62f};
        Vector2 sh{seat.x + 14 * s, seat.y - 44 * s};
        DrawLineEx(seat, sh, 22 * s, rgba(0x1f1c26ff));
        DrawLineEx(sh, {wb.x + 115 * s * 0.88f, wb.y - 115 * s * 0.8f}, 9 * s, rgba(0x1f1c26ff));
        Vector2 knee{seat.x + 26 * s, seat.y + 22 * s + std::sin(pedal_) * 6 * s};
        DrawLineEx(seat, knee, 12 * s, rgba(0x2c3a5cff));
        DrawLineEx(knee, {wb.x + 115 * s * 0.45f + std::cos(pedal_) * 18 * s, wb.y + std::sin(pedal_) * 18 * s}, 10 * s, rgba(0x2c3a5cff));
        art::pieter({sh.x + 6 * s, sh.y - 26 * s}, 26 * s, P().hat);
        art::boba({wb.x + 115 * s * 1.0f, wb.y - 115 * s * 0.86f}, 26 * s);  // in the basket
        rlPopMatrix();
        if (shield_) {
            fx::beginAdd();
            fx::glow({kPlayerX, y_ - 70 * s - jh}, 110 * s, alpha(col::cyan, 0.35f + 0.1f * std::sin(t_ * 6)));
            fx::endAdd();
            DrawRing({kPlayerX, y_ - 70 * s - jh}, 84 * s, 88 * s, 0, 360, 48, alpha(col::cyan, 0.6f));
        }
        if (bellT_ > 0) {
            for (int i = 0; i < 3; i++) {
                float k = std::fmod(t_ * 2 + i / 3.0f, 1.0f);
                DrawRing({kPlayerX + 60, y_ - 110 * s - jh}, 20 + k * 80, 23 + k * 80, -60, 60, 16, alpha(col::gold, 1 - k));
            }
        }
    }

    void drawHud() {
        Rectangle r{20, 16, 300, 64};
        DrawRectangleRounded(r, 0.5f, 10, alpha(col::bgDeep, 0.75f));
        ui::textShadow(TextFormat("%d m", (int)dist_), r.x + 20, r.y + 24, 32, WHITE, ui::Left, true, 2);
        ui::text(TextFormat("best %d m", P().stats.fietsBest), r.x + 20, r.y + 50, 14, col::muted, ui::Left, true);
        ui::coin({r.x + 210, r.y + 30}, 14);
        ui::text(std::to_string(coins_), r.x + 232, r.y + 30, 24, col::gold, ui::Left, true);
        // speedometer
        Vector2 sc{VW - 70.0f, 60};
        DrawCircleV(sc, 44, alpha(col::bgDeep, 0.75f));
        float k = clamp01((speed_ - 300) / 700);
        DrawRing(sc, 34, 40, 140, 140 + 260 * k, 24, mix(col::green, col::red, k));
        ui::text(TextFormat("%d", (int)(speed_ / 20)), sc.x, sc.y - 4, 24, WHITE, ui::Center, true);
        ui::text("km/h", sc.x, sc.y + 18, 12, col::muted, ui::Center, true);
        if (bellT_ > 0) ui::text(TextFormat("BELL %.0f", bellT_), VW / 2.0f, 30, 20, col::gold, ui::Center, true);
    }

    void draw() override {
        drawWorld();
        // draw back lanes first so near things overlap far ones
        for (int lane = 0; lane < 3; lane++) {
            for (auto& th : things_)
                if (std::clamp((int)std::round(th.toLane >= 0 ? lerpf((float)th.lane, (float)th.toLane, th.laneT) : (float)th.lane), 0, 2) == lane) drawThing(th);
            int playerLane = (int)std::round((y_ - kLaneY[0]) / (kLaneY[1] - kLaneY[0]));
            if (std::clamp(playerLane, 0, 2) == lane) drawPlayer();
        }
        if (splashed_ && phase_ != Phase::Ride) {
            for (int i = 0; i < 3; i++) {
                float k = std::fmod(phaseT_ * 0.8f + i * 0.3f, 1.0f);
                DrawEllipseLines((int)fallP_.x, 400, 20 + k * 90, 5 + k * 16, alpha(WHITE, 1 - k));
            }
        }
        fx_.draw();
        if (phase_ == Phase::Ride || phase_ == Phase::Splash) drawHud();
        if (phase_ == Phase::Ready) drawReady();
        if (phase_ == Phase::Over) drawOver();
    }

    void drawReady() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.35f));
        Rectangle p{VW / 2.0f - 300, 120, 600, 330};
        ui::panel(p, alpha(col::panel, 0.95f), 0.08f, alpha(col::cyan, 0.6f));
        fx::gradientText("FIETSEN!", VW / 2.0f, p.y + 70, 80, WHITE, rgba(0x8fd0ffff), 5);
        ui::text("UP / DOWN  switch lanes", VW / 2.0f, p.y + 150, 22, col::text, ui::Center, true);
        ui::text("SPACE  jump bollards, potholes and ducks", VW / 2.0f, p.y + 184, 22, col::text, ui::Center, true);
        ui::text("tourists, scooters and bike racks: go around", VW / 2.0f, p.y + 218, 18, col::muted, ui::Center, true);
        ui::text(TextFormat("best: %d m", P().stats.fietsBest), VW / 2.0f, p.y + 256, 20, col::gold, ui::Center, true);
        float a = 0.6f + 0.4f * std::sin(t_ * 4);
        ui::text("press SPACE to ride", VW / 2.0f, p.y + 296, 22, alpha(col::cyan, a), ui::Center, true);
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawOver() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.55f));
        Rectangle p{VW / 2.0f - 300, 110, 600, 420};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::cyan, 0.6f));
        ui::textOutline("SPLASH!", VW / 2.0f, p.y + 66, 72, rgba(0x8fd0ffff), col::outline, 5);
        ui::text("Pieter is in the canal. Again.", VW / 2.0f, p.y + 122, 20, col::muted, ui::Center, true);
        ui::textShadow(TextFormat("%d m", (int)dist_), VW / 2.0f, p.y + 190, 64, WHITE, ui::Center, true, 4);
        if (best_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW RECORD!", VW / 2.0f, p.y + 238, 22, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 280}, 15);
        ui::text("+" + fmtNum(earned_) + " Pietercoins", VW / 2.0f - 66, p.y + 280, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 330, 250, 64}, "RIDE AGAIN", rgba(0x2f7ad8ff), 24) || IsKeyPressed(KEY_SPACE)) reset();
            if (ui::button({p.x + 310, p.y + 330, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

private:
    Phase phase_ = Phase::Ready;
    std::vector<Thing> things_;
    float t_ = 0, phaseT_ = 0, dist_ = 0, speed_ = 380, scroll_ = 0, pedal_ = 0, y_ = 568, jumpT_ = -1, spawnX_ = 900, bellT_ = 0;
    int lane_ = 1, coins_ = 0;
    long long earned_ = 0;
    bool shield_ = false, best_ = false, splashed_ = false;
    Vector2 fallP_{}, fallV_{};
    float fallRot_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeFiets() { return std::make_unique<FietsScene>(); }

}  // namespace pg
