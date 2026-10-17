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
        float x = spawnX_;
        int free = rndi(0, 2);
        auto add = [&](Kind k, float dx, int lane) {
            Thing t{k, x + dx, lane};
            if (k == Kind::Scooter) t.speed = -rnd(160, 260);
            if (k == Kind::Tourist) {
                t.speed = rnd(-40, 30);
                t.hue = rnd(0, 360);
                if (chance(0.25f + hard * 0.3f)) {
                    int to = std::clamp(lane + (chance(0.5f) ? 1 : -1), 0, 2);
                    if (to != free && to != lane) t.toLane = to;  // never wander into the escape lane
                }
            }
            if (k == Kind::Duck) t.speed = -30;
            things_.push_back(t);
        };
        // retry until there's always a way through: scooters and dawdling tourists move at their
        // own speed, so a fresh chunk can line up with an older one and wall off all three lanes
        for (int attempt = 0;; attempt++) {
        size_t n0 = things_.size();
        int pattern = rndi(0, 6);
        free = rndi(0, 2);
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
        if (passable()) break;
        things_.resize(n0);
        if (attempt >= 8) {  // give up on obstacles this beat, just hand out coins
            for (int i = 0; i < 4; i++) add(Kind::Coin, i * 50.0f, free);
            break;
        }
        }
        spawnX_ += rnd(380, 560) * (1.1f - hard * 0.35f) + speed_ * 0.25f;
    }

    // Predict when every tall (unjumpable) obstacle reaches the bike and check that at each
    // moment at least one lane is open, with some slack for the lane switch itself.
    bool passable() const {
        struct Block { float t0, t1; int lanes; };
        std::vector<Block> bl;
        float end = 0;
        for (auto& th : things_) {
            if (th.gone || !tall(th.kind) || th.x < kPlayerX - 80) continue;
            float rel = std::max(60.0f, speed_ + th.speed);
            float reach = (th.kind == Kind::Rack ? 70 : 38) + 30;
            Block b{(th.x - kPlayerX - reach) / rel - 0.2f, (th.x - kPlayerX + reach) / rel + 0.2f, 1 << th.lane};
            if (th.toLane >= 0) b.lanes |= 1 << th.toLane;
            bl.push_back(b);
            end = std::max(end, b.t1);
        }
        for (float t = 0; t <= end; t += 0.025f) {
            int m = 0;
            for (auto& b : bl)
                if (t >= b.t0 && t <= b.t1) m |= b.lanes;
            if (m == 7) return false;
        }
        return true;
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
    void cloud(Vector2 p, float sc, float a) {
        const float px[] = {-52, -22, 12, 44}, py[] = {-4, -22, -30, -10}, pr[] = {26, 34, 38, 28};
        Color shade = alpha(rgba(0xc8d8f0ff), a), lit = alpha(WHITE, a);
        DrawEllipse((int)p.x, (int)(p.y + 6 * sc), 80 * sc, 16 * sc, shade);
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * sc, p.y + py[i] * sc + 4 * sc}, pr[i] * sc, shade);
        for (int i = 0; i < 4; i++) DrawCircleV({p.x + px[i] * sc, p.y + py[i] * sc}, pr[i] * sc, lit);
        DrawEllipse((int)p.x, (int)p.y, 78 * sc, 14 * sc, lit);
    }

    // a row of canal houses; returns nothing, draws `flip` = upside-down reflection
    void houseRow(float par, float baseY, float sc, Color tint, bool reflect) {
        const int n = 14;
        float spacing = 170 * sc * 0.98f, period = n * spacing;
        float off = std::fmod(scroll_ * par, period);
        for (int i = 0; i < n * 2; i++) {
            float x = i * spacing - off;
            if (x > VW + 200 || x < -260) continue;
            int k = (i * 5 + (int)(par * 10)) % 6;
            Texture2D& t = E().tex(TextFormat("spr/house%d", k));
            float hs = sc * (0.9f + 0.1f * ((i * 7) % 3));
            float w = t.width * hs, h = t.height * hs;
            if (!reflect) {
                DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {x, baseY - h, w, h}, {0, 0}, 0, tint);
            } else {
                DrawTexturePro(t, {0, (float)t.height, (float)t.width, -(float)t.height}, {x, baseY, w, h * 0.55f}, {0, 0}, 0, tint);
            }
        }
    }

    void drawWorld() {
        // sky with sun and soft clouds
        DrawRectangleGradientV(0, 0, VW, 380, rgba(0x5aa8ffff), rgba(0xd8eeffff));
        fx::beginAdd();
        fx::glow({1060, 80}, 260, alpha(rgba(0xfff0c0ff), 0.7f));
        fx::endAdd();
        DrawCircleV({1060, 80}, 42, rgba(0xfff6d0ff));
        for (int i = 0; i < 6; i++) {
            float x = std::fmod(i * 330 - scroll_ * 0.03f - t_ * 6, 1700.0f);
            if (x < -200) x += 1700;
            cloud({x - 150, 50.0f + (i * 37) % 90}, 0.6f + (i % 3) * 0.2f, 0.92f);
        }
        // far row (hazy) and near row of canal houses
        houseRow(0.22f, 318, 0.4f, rgba(0xb0c0e4ff), false);
        DrawRectangleGradientV(0, 170, VW, 150, alpha(rgba(0xd8eeffff), 0), alpha(rgba(0xd8eeffff), 0.4f));
        houseRow(0.5f, 352, 0.62f, WHITE, false);
        // quay: cobbled edge
        fx::tiled(E().tex("tex/cobble"), {0, 350, (float)VW, 14}, 0.35f, {scroll_ * 0.5f / 0.35f, 0});
        DrawRectangle(0, 362, VW, 3, rgba(0x2a2a32ff));
        // canal: water texture + reflections of the houses
        Rectangle canal{0, 365, (float)VW, 76};
        fx::tiled(E().tex("tex/water"), canal, 0.9f, {scroll_ * 0.55f / 0.9f + t_ * 8, t_ * 6});
        E().beginClip(canal);
        rlPushMatrix();
        float wob = std::sin(t_ * 2) * 2;
        rlTranslatef(wob, 0, 0);
        houseRow(0.5f, 366, 0.62f, alpha(rgba(0x8fb0d8ff), 0.35f), true);
        rlPopMatrix();
        E().endClip();
        DrawRectangleGradientV(0, 365, VW, 18, alpha(BLACK, 0.3f), alpha(BLACK, 0));
        // tour boat
        float bx = std::fmod(1800 - scroll_ * 0.3f + t_ * 50, 2400.0f) - 400;
        fx::sprite(E().tex("spr/boat"), {bx + 170, 404}, 0.62f);
        // trees along the canal + a black railing
        float to = std::fmod(scroll_ * 0.9f, 300.0f);
        Texture2D& tree = E().tex("spr/canaltree");
        for (float x = -to - 100; x < VW + 300; x += 300) fx::sprite(tree, {x + 90, 344}, 0.78f);
        float ro = std::fmod(scroll_, 50.0f);
        DrawRectangle(0, 424, VW, 4, rgba(0x1a1a22ff));
        DrawRectangle(0, 438, VW, 3, rgba(0x1a1a22ff));
        for (float x = -ro; x < VW + 50; x += 50) DrawRectangle((int)x, 420, 4, 24, rgba(0x1a1a22ff));
        // sidewalk tiles, kerb, then the red fietspad
        fx::tiled(E().tex("tex/cobble"), {0, 444, (float)VW, 14}, 0.3f, {scroll_ / 0.3f, 0}, rgba(0xd8d0d8ff));
        DrawRectangle(0, 458, VW, 6, rgba(0xb8b8c0ff));
        DrawRectangle(0, 463, VW, 2, rgba(0x6a6a72ff));
        fx::tiled(E().tex("tex/fietspad"), {0, 465, (float)VW, VH - 465.0f}, 0.8f, {scroll_ / 0.8f, 0});
        DrawRectangleGradientV(0, 465, VW, 20, alpha(BLACK, 0.25f), alpha(BLACK, 0));
        float mo = std::fmod(scroll_, 140.0f);
        for (int l = 0; l < 2; l++) {
            float y = (kLaneY[l] + kLaneY[l + 1]) / 2 + 6;
            for (float x = -mo; x < VW + 140; x += 140) DrawRectangleRounded({x, y, 70, 5}, 1, 4, alpha(WHITE, 0.8f));
        }
        float so = std::fmod(scroll_, 700.0f);
        for (float x = -so + 200; x < VW + 700; x += 700) {
            Color pc = alpha(WHITE, 0.55f);
            DrawRing({x, kLaneY[1] + 30}, 10, 13, 0, 360, 20, pc);
            DrawRing({x + 34, kLaneY[1] + 30}, 10, 13, 0, 360, 20, pc);
            DrawLineEx({x, kLaneY[1] + 30}, {x + 17, kLaneY[1] + 16}, 3, pc);
            DrawLineEx({x + 17, kLaneY[1] + 16}, {x + 34, kLaneY[1] + 30}, 3, pc);
        }
    }

    void drawThing(const Thing& th) {
        if (th.gone && !pickup(th.kind) && th.kind != Kind::Tourist) return;
        float lane = th.toLane >= 0 && th.laneT > 0 ? lerpf((float)th.lane, (float)th.toLane, easeInOutSine(th.laneT)) : (float)th.lane;
        int li = std::clamp((int)std::round(lane), 0, 2);
        float y = lerpf(kLaneY[(int)std::floor(lane)], kLaneY[std::min(2, (int)std::floor(lane) + 1)], lane - std::floor(lane));
        float s = kLaneS[li];
        float x = th.x;
        if (th.gone && th.kind == Kind::Tourist) y -= 40, x += 30;  // jumped aside
        DrawEllipse((int)x + 4, (int)y + 2, 30 * s, 7 * s, alpha(BLACK, pickup(th.kind) ? 0.15f : 0.35f));
        auto spr = [&](const char* name, float sc, float lift, float rot = 0, bool flip = false) {
            Texture2D& t = E().tex(name);
            fx::sprite(t, {x, y - t.height * sc * s / 2 - lift}, sc * s, rot, WHITE, flip);
        };
        switch (th.kind) {
            case Kind::Tourist: {
                float bob = std::abs(std::sin(t_ * 6 + x * 0.01f)) * 3;
                spr(TextFormat("spr/tourist%d", (int)(th.hue / 120) % 3), 0.8f, bob, std::sin(t_ * 6 + x) * 2);
                if (th.toLane >= 0 && th.laneT < 0.05f && th.x < 1100) ui::textOutline("!", x, y - 150 * s, 34, col::gold, col::outline, 3);
                break;
            }
            case Kind::Scooter:
                spr("spr/scooter", 0.75f, 0, std::sin(t_ * 20) * 1.5f, true);
                fx::beginAdd();
                fx::glow({x - 58 * s, y - 64 * s}, 50 * s, alpha(col::gold, 0.7f));
                fx::endAdd();
                break;
            case Kind::Rack:
                for (int i = 0; i < 3; i++) {
                    float bx = x - 60 * s + i * 50 * s;
                    art::bike({bx - 20 * s, y - 14 * s}, 70 * s, 0.5f, ColorFromHSV(i * 110.0f + 30, 0.55f, 0.55f));
                }
                DrawRectangleRounded({x - 82 * s, y - 36 * s, 164 * s, 6 * s}, 1, 4, rgba(0xb8b8c8ff));
                break;
            case Kind::Bollard: spr("spr/bollard", 0.7f, 0); break;
            case Kind::Pothole:
                DrawEllipse((int)x, (int)y - 2, 38 * s, 11 * s, rgba(0x3a1a14ff));
                DrawEllipse((int)x, (int)y - 4, 32 * s, 8 * s, rgba(0x2a5a8aff));
                DrawEllipse((int)x - 8, (int)y - 6, 12 * s, 2 * s, alpha(WHITE, 0.4f));
                break;
            case Kind::Duck: spr("spr/duck", 0.7f, std::abs(std::sin(t_ * 10 + x)) * 3, 0, false); break;
            case Kind::Coin: {
                float sq = std::abs(std::cos(t_ * 4 + x * 0.02f));
                Vector2 c{x, y - 60 * s + std::sin(t_ * 5 + x * 0.05f) * 4};
                fx::beginAdd();
                fx::glow(c, 22 * s, alpha(col::gold, 0.4f));
                fx::endAdd();
                DrawEllipse((int)c.x, (int)c.y, 14 * s * std::max(0.2f, sq), 14 * s, col::outline);
                DrawEllipse((int)c.x, (int)c.y, 12 * s * std::max(0.2f, sq), 12 * s, col::goldDark);
                DrawEllipse((int)c.x, (int)c.y, 9 * s * std::max(0.2f, sq), 9 * s, col::gold);
                break;
            }
            case Kind::Wafel:
                fx::beginAdd();
                fx::glow({x, y - 64 * s}, 44, alpha(col::gold, 0.4f));
                fx::endAdd();
                art::stroopwafelTop({x, y - 60 * s + std::sin(t_ * 4) * 5}, 22 * s);
                break;
            case Kind::Boba:
                fx::beginAdd();
                fx::glow({x, y - 64 * s}, 48, alpha(col::cyan, 0.5f));
                fx::endAdd();
                art::boba({x, y - 60 * s + std::sin(t_ * 4) * 5}, 44 * s);
                break;
            case Kind::Bell:
                fx::beginAdd();
                fx::glow({x, y - 64 * s}, 48, alpha(col::gold, 0.5f));
                fx::endAdd();
                fx::shadedBall({x, y - 62 * s}, 16 * s, col::gold, 2);
                DrawRectangleRounded({x - 20 * s, y - 50 * s, 40 * s, 7 * s}, 1, 4, col::goldDark);
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
