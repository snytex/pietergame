// PIETER KART: top-down 3-lap race through tulip country against Davi, Kira
// and the Horse. Hold SPACE while turning to drift; let go for a boost. Boba
// boxes give items: BOOST, HAGELSLAG (a slippery spill) or SHIELD.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <array>

namespace pg {
namespace {

constexpr int kLaps = 3;
constexpr float kTrackW = 170;
constexpr int kSamples = 480;
constexpr float kMaxSpeed = 520, kBoostSpeed = 760, kGrassSpeed = 250;

enum class Item { None, Boost, Spill, Shield };
enum class Phase { Intro, Countdown, Race, Results };

struct Kart {
    const char* who;
    const char* name;
    Color body;
    bool player = false;
    Vector2 pos{};
    float heading = 0, speed = 0, slip = 0;
    int idx = 0;            // nearest centreline sample
    int lap = 0;
    float lapStart = 0, bestLap = 0;
    bool finished = false;
    float finishTime = 0;
    float driftT = 0;       // how long the current drift has charged
    int driftDir = 0;
    float boostT = 0, spinT = 0, shieldT = 0;
    Item item = Item::None;
    float aiOffset = 0, aiSkill = 1, aiItemT = 0;
    float bump = 0;
    int place = 0;
    float progress() const { return lap + (float)idx / kSamples; }
};

struct Box { int sample; float lateral; float respawn = 0; };
struct Spill { Vector2 pos; float life = 14; };
struct Skid { Vector2 a, b; float life; };

class KartScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Race) return {"Pieter Kart", TextFormat("Lap %d/%d - %s place", std::min(kLaps, karts_[0].lap + 1), kLaps, ordinal(karts_[0].place))};
        return {"Pieter Kart", "Tulip Circuit"};
    }

    static const char* ordinal(int p) {
        static const char* s[] = {"-", "1st", "2nd", "3rd", "4th"};
        return s[std::clamp(p, 0, 4)];
    }

    void enter() override {
        buildTrack();
        reset();
        // dev hook: PG_KART_AUTO=1 skips the intro and lets a bot drive you
        if (std::getenv("PG_KART_AUTO")) {
            autopilot_ = true;
            phase_ = Phase::Race;
        }
    }

    // ------------------------------------------------------------ track
    void buildTrack() {
        // control points of the circuit (world units), Catmull-Rom smoothed
        const Vector2 cp[] = {{400, 1500}, {400, 900}, {620, 520}, {1100, 420}, {1500, 650}, {1700, 380}, {2250, 340},
                              {2600, 700}, {2450, 1150}, {2000, 1250}, {1750, 1650}, {2050, 2000}, {1500, 2200}, {800, 2050}};
        const int n = sizeof(cp) / sizeof(cp[0]);
        center_.clear();
        for (int i = 0; i < kSamples; i++) {
            float f = (float)i / kSamples * n;
            int k = (int)f;
            float u = f - k;
            Vector2 p0 = cp[(k + n - 1) % n], p1 = cp[k % n], p2 = cp[(k + 1) % n], p3 = cp[(k + 2) % n];
            float u2 = u * u, u3 = u2 * u;
            Vector2 p = (p1 * 2 + (p2 - p0) * u + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * u2 + (p1 * 3 - p0 - p2 * 3 + p3) * u3) * 0.5f;
            center_.push_back(p);
        }
        normal_.resize(kSamples);
        for (int i = 0; i < kSamples; i++) {
            Vector2 d = Vector2Normalize(center_[(i + 1) % kSamples] - center_[(i + kSamples - 1) % kSamples]);
            normal_[i] = {-d.y, d.x};
        }
        boxes_.clear();
        for (int s : {90, 250, 400})
            for (int l = -1; l <= 1; l++) boxes_.push_back({s, l * 45.0f});
    }

    Vector2 dirAt(int i) const { return Vector2Normalize(center_[(i + 1) % kSamples] - center_[i]); }

    int nearest(Vector2 p, int hint) const {
        int best = hint;
        float bd = 1e18f;
        for (int k = -24; k <= 24; k++) {
            int i = (hint + k + kSamples) % kSamples;
            float d = Vector2DistanceSqr(p, center_[i]);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

    bool onTrack(const Kart& k) const { return Vector2Distance(k.pos, center_[k.idx]) < kTrackW / 2 + 6; }

    void reset() {
        karts_ = {Kart{"pieter", "Pieter", rgba(0x6b3fd1ff), true}, Kart{"davi", "Davi", rgba(0x2a9ad8ff)}, Kart{"kira", "Kira", rgba(0xe85a9aff)},
                  Kart{"horse", "Horse", rgba(0xd8822aff)}};
        Vector2 d = dirAt(0), n = normal_[0];
        for (int i = 0; i < 4; i++) {
            Kart& k = karts_[i];
            // 2x2 grid behind the line
            k.pos = center_[0] - d * (70.0f + (i / 2) * 90) + n * ((i % 2) ? 40.0f : -40.0f);
            k.heading = angleOf(d);
            k.idx = nearest(k.pos, kSamples - 10);
            k.lap = k.idx > kSamples / 2 ? -1 : 0;  // started just behind the line
            k.aiOffset = rnd(-35, 35);
        }
        karts_[1].aiSkill = 1.0f;
        karts_[2].aiSkill = 0.97f;
        karts_[3].aiSkill = 1.02f;
        spills_.clear();
        skids_.clear();
        fx_.clear();
        raceT_ = 0;
        phase_ = Phase::Intro;
        phaseT_ = 0;
        cam_ = karts_[0].pos;
        lastLight_ = -1;
        finishOrder_ = 0;
        reward_ = 0;
    }

    // ------------------------------------------------------------ physics
    void drive(Kart& k, float throttle, float steer, bool drift, bool useItem, float dt) {
        if (k.finished && !k.player) throttle = 0.6f;
        float maxS = k.boostT > 0 ? kBoostSpeed : kMaxSpeed;
        if (!k.player) {
            // rubber band: the pack stays close to the player
            float gap = karts_[0].progress() - k.progress();
            maxS *= k.aiSkill * std::clamp(1 + gap * 0.35f, 0.9f, 1.08f);
        }
        bool grass = !onTrack(k);
        if (grass && k.boostT <= 0) maxS = kGrassSpeed;
        if (k.spinT > 0) {
            k.spinT -= dt;
            k.heading += dt * 14;
            k.speed = damp(k.speed, 0, 3, dt);
            throttle = 0;
            steer = 0;
            drift = false;
        }
        // speed
        if (throttle > 0) k.speed += 560 * throttle * dt;
        else if (throttle < 0) k.speed += (k.speed > 0 ? -900 : -300) * -throttle * dt;
        else k.speed = damp(k.speed, 0, 0.8f, dt);
        k.speed = std::clamp(k.speed, -160.0f, maxS);
        if (k.speed > maxS) k.speed = damp(k.speed, maxS, 4, dt);
        // steering + drift
        float grip = std::clamp(std::abs(k.speed) / 260, 0.0f, 1.0f);
        bool drifting = drift && std::abs(steer) > 0.2f && k.speed > 280;
        if (drifting) {
            if (k.driftDir == 0) k.driftDir = steer > 0 ? 1 : -1;
            k.driftT += dt;
            k.slip = damp(k.slip, k.driftDir * 0.42f, 6, dt);
            k.heading += (k.driftDir * 1.6f + steer * 1.1f) * dt * grip;
            if (rndi(0, 2) == 0) {
                Particle sm;
                sm.pos = k.pos - fromAngle(k.heading, 20) + fromAngle(k.heading + PI / 2, rnd(-14, 14));
                sm.vel = {rnd(-20, 20), rnd(-20, 20)};
                sm.life = sm.maxLife = rnd(0.5f, 0.9f);
                sm.size = rnd(6, 10);
                sm.endSize = 26;
                sm.color = alpha(rgba(0xd8d4e0ff), 0.35f);
                fx_.add(sm);
            }
            if (k.player && rndi(0, 2) == 0) {
                Color sc = k.driftT > 1.6f ? col::violet : k.driftT > 0.8f ? col::orange : col::cyan;
                for (int side : {-1, 1}) {
                    Particle p;
                    Vector2 back = k.pos - fromAngle(k.heading, 18) + fromAngle(k.heading + PI / 2, side * 12.0f);
                    p.pos = back;
                    p.vel = fromAngle(k.heading + PI + rnd(-0.6f, 0.6f), rnd(60, 180));
                    p.life = p.maxLife = 0.25f;
                    p.size = 4;
                    p.color = sc;
                    p.shape = PShape::Spark;
                    fx_.add(p);
                }
            }
        } else {
            if (k.driftDir != 0 && k.driftT > 0.8f) {
                k.boostT = k.driftT > 1.6f ? 1.4f : 0.8f;  // mini-turbo
                if (k.player) E().sfx.play(Sfx::Dash, 0.7f);
            }
            k.driftDir = 0;
            k.driftT = 0;
            k.slip = damp(k.slip, 0, 8, dt);
            k.heading += steer * 2.6f * dt * grip * (k.speed < 0 ? -1 : 1);
        }
        if (k.boostT > 0) {
            k.boostT -= dt;
            k.speed = std::max(k.speed, kBoostSpeed * 0.95f);
        }
        k.shieldT = std::max(0.0f, k.shieldT - dt);
        Vector2 dir = fromAngle(k.heading - k.slip);
        k.pos += dir * k.speed * dt;
        // skid marks while drifting or on grass at speed
        if ((drifting || (grass && k.speed > 200)) && std::fmod(raceT_, 0.03f) < dt) {
            Vector2 b = k.pos - fromAngle(k.heading, 16);
            skids_.push_back({b, b - dir * 10, 4});
        }
        // keep it inside the world
        k.pos.x = std::clamp(k.pos.x, 80.0f, 2920.0f);
        k.pos.y = std::clamp(k.pos.y, 80.0f, 2420.0f);
        // progress + laps
        int before = k.idx;
        k.idx = nearest(k.pos, k.idx);
        if (before > kSamples * 3 / 4 && k.idx < kSamples / 4) {
            k.lap++;
            if (k.lap >= 1 && k.lap <= kLaps) {
                float lt = raceT_ - k.lapStart;
                if (k.lap > 0 && (k.bestLap == 0 || lt < k.bestLap)) k.bestLap = lt;
                k.lapStart = raceT_;
                if (k.player && k.lap < kLaps) {
                    E().sfx.play(Sfx::Bell, 0.5f);
                    lapFlash_ = 2;
                }
            }
            if (k.lap >= kLaps && !k.finished) {
                k.finished = true;
                k.finishTime = raceT_;
                finishOrder_++;
                k.place = finishOrder_;
                if (k.player) finishRace();
            }
        } else if (before < kSamples / 4 && k.idx > kSamples * 3 / 4) {
            k.lap--;  // went backwards over the line
        }
        if (k.lap == 0 && k.lapStart == 0 && k.idx < kSamples / 2) k.lapStart = 0;
        // items
        if (useItem && k.item != Item::None) {
            switch (k.item) {
                case Item::Boost:
                    k.boostT = 2.0f;
                    if (k.player) E().sfx.play(Sfx::Power, 0.6f);
                    break;
                case Item::Spill:
                    spills_.push_back({k.pos - fromAngle(k.heading, 40)});
                    if (k.player) E().sfx.play(Sfx::Pop, 0.6f);
                    break;
                case Item::Shield:
                    k.shieldT = 8;
                    if (k.player) E().sfx.play(Sfx::Gem, 0.6f);
                    break;
                default: break;
            }
            k.item = Item::None;
        }
    }

    void steerAI(Kart& k, float dt) {
        int look = 10 + (int)(k.speed / 80);
        int ti = (k.idx + look) % kSamples;
        Vector2 target = center_[ti] + normal_[ti] * k.aiOffset;
        // dodge spills ahead
        for (auto& s : spills_)
            if (Vector2Distance(s.pos, target) < 60) target += normal_[ti] * (k.aiOffset > 0 ? -70.0f : 70.0f);
        float want = angleOf(target - k.pos);
        float diff = std::remainder(want - k.heading, 2 * PI);
        float steer = std::clamp(diff * 2.2f, -1.0f, 1.0f);
        float throttle = std::abs(diff) > 0.7f && k.speed > 300 ? 0.2f : 1.0f;
        bool drift = std::abs(diff) > 0.35f && k.speed > 380 && k.aiSkill > 0.98f;
        k.aiItemT -= dt;
        bool use = false;
        if (k.item != Item::None && k.aiItemT <= 0) {
            use = k.item == Item::Boost ? std::abs(diff) < 0.2f : true;
            k.aiItemT = rnd(1.0f, 4.0f);
        }
        drive(k, throttle, steer, drift, use, dt);
    }

    void interact(float dt) {
        (void)dt;
        // kart vs kart bumps
        for (size_t i = 0; i < karts_.size(); i++)
            for (size_t j = i + 1; j < karts_.size(); j++) {
                Kart &a = karts_[i], &b = karts_[j];
                Vector2 d = b.pos - a.pos;
                float dist = Vector2Length(d);
                if (dist < 40 && dist > 0.01f) {
                    Vector2 n = d / dist;
                    float push = (40 - dist) / 2;
                    a.pos -= n * push;
                    b.pos += n * push;
                    float avg = (a.speed + b.speed) / 2;
                    a.speed = lerpf(a.speed, avg, 0.3f) * 0.96f;
                    b.speed = lerpf(b.speed, avg, 0.3f) * 0.96f;
                    if ((a.player || b.player) && a.bump <= 0) {
                        E().sfx.play(Sfx::Drop, 0.5f);
                        E().shake(3);
                        a.bump = 0.3f;
                    }
                }
            }
        for (auto& k : karts_) k.bump = std::max(0.0f, k.bump - dt);
        // item boxes
        for (auto& b : boxes_) {
            if (b.respawn > 0) { b.respawn -= dt; continue; }
            Vector2 bp = center_[b.sample] + normal_[b.sample] * b.lateral;
            for (auto& k : karts_)
                if (Vector2Distance(k.pos, bp) < 34) {
                    b.respawn = 3;
                    fx_.burst(bp, 14, col::gold, 220, 5, 0.4f, PShape::Star);
                    if (k.item == Item::None) {
                        float r = rnd(0, 1);
                        // whoever is behind gets better items
                        float behind = clamp01((karts_[0].progress() - k.progress()) * -1 + 0.5f);
                        if (k.player) behind = (k.place - 1) / 3.0f;
                        k.item = r < 0.3f + behind * 0.4f ? Item::Boost : r < 0.7f ? Item::Spill : Item::Shield;
                        if (k.player) {
                            E().sfx.play(Sfx::Coin, 0.6f);
                            itemRoll_ = 0.7f;
                        }
                    }
                    break;
                }
        }
        // spills
        for (auto& s : spills_) {
            s.life -= dt;
            for (auto& k : karts_)
                if (s.life > 0 && k.spinT <= 0 && Vector2Distance(k.pos, s.pos) < 34) {
                    s.life = 0;
                    if (k.shieldT > 0) {
                        k.shieldT = 0;
                        if (k.player) E().sfx.play(Sfx::Pop, 0.7f);
                    } else {
                        k.spinT = 0.9f;
                        if (k.player) {
                            E().sfx.play(Sfx::Hurt, 0.6f);
                            E().shake(6);
                        }
                    }
                }
        }
        std::erase_if(spills_, [](auto& s) { return s.life <= 0; });
        for (auto& s : skids_) s.life -= dt;
        std::erase_if(skids_, [](auto& s) { return s.life <= 0; });
        if (skids_.size() > 900) skids_.erase(skids_.begin(), skids_.begin() + 200);
        // positions
        std::array<int, 4> order{0, 1, 2, 3};
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const Kart &ka = karts_[a], &kb = karts_[b];
            if (ka.finished != kb.finished) return ka.finished;
            if (ka.finished) return ka.finishTime < kb.finishTime;
            return ka.progress() > kb.progress();
        });
        for (int i = 0; i < 4; i++) karts_[order[i]].place = i + 1;
    }

    void finishRace() {
        Kart& me = karts_[0];
        const long long prize[] = {0, 150, 80, 40, 15};
        reward_ = prize[std::clamp(me.place, 1, 4)];
        if (me.place == 1) P().stats.kartWins++;
        if (me.bestLap > 0 && (P().stats.kartBestLap == 0 || me.bestLap < P().stats.kartBestLap)) P().stats.kartBestLap = me.bestLap;
        P().earn(reward_);
        P().save();
        phase_ = Phase::Results;
        phaseT_ = 0;
        E().sfx.play(me.place == 1 ? Sfx::Win : me.place <= 3 ? Sfx::Perfect : Sfx::Lose);
        if (me.place == 1) uiFx_.confetti({VW / 2.0f, 200}, 160);
    }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        uiFx_.update(dt);
        phaseT_ += dt;
        itemRoll_ = std::max(0.0f, itemRoll_ - dt);
        lapFlash_ = std::max(0.0f, lapFlash_ - dt);
        switch (phase_) {
            case Phase::Intro:
                if (phaseT_ > 2.2f || IsKeyPressed(KEY_SPACE)) { phase_ = Phase::Countdown; phaseT_ = 0; }
                break;
            case Phase::Countdown: {
                int light = (int)phaseT_;
                if (light != lastLight_ && light < 4) {
                    lastLight_ = light;
                    E().sfx.play(light < 3 ? Sfx::Tick : Sfx::Bell, 0.8f, light < 3 ? 1.0f : 1.4f);
                }
                if (phaseT_ >= 3) { phase_ = Phase::Race; phaseT_ = 0; }
                break;
            }
            case Phase::Race:
            case Phase::Results: {
                raceT_ += dt;
                Kart& me = karts_[0];
                if (phase_ == Phase::Race && autopilot_) {
                    steerAI(me, dt);
                } else if (phase_ == Phase::Race) {
                    float throttle = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1.0f : (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? -1.0f : 0.0f;
                    float steer = ((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0f : 0.0f) - ((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0f : 0.0f);
                    bool drift = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_SHIFT);
                    bool item = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_F);
                    drive(me, throttle, steer, drift, item, dt);
                } else {
                    steerAI(me, dt);  // autopilot victory lap
                }
                for (size_t i = 1; i < karts_.size(); i++) steerAI(karts_[i], dt);
                interact(dt);
                break;
            }
        }
        // camera leads the player a little
        Kart& me = karts_[0];
        Vector2 lead = me.pos + fromAngle(me.heading, std::clamp(me.speed, 0.0f, kBoostSpeed) * 0.35f);
        cam_ = damp(cam_, lead, 5, dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    // ------------------------------------------------------------ draw: world
    Vector2 camOff() const { return {VW / 2.0f - cam_.x, VH / 2.0f + 40 - cam_.y}; }

    void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
        DrawTriangle(a, b, c, col);
        DrawTriangle(a, c, b, col);
    }

    // textured strip between two offsets from the centreline (u runs along the track)
    void strip(Texture2D& tex, float in, float out, float uvScale, Color tint, const Rectangle& view) {
        float dist = 0;
        for (int i = 0; i < kSamples; i++) {
            int j = (i + 1) % kSamples;
            float seg = Vector2Distance(center_[i], center_[j]);
            if (CheckCollisionPointRec(center_[i], view) || CheckCollisionPointRec(center_[j], view)) {
                Vector2 p[4] = {center_[i] + normal_[i] * in, center_[j] + normal_[j] * in, center_[j] + normal_[j] * out,
                                center_[i] + normal_[i] * out};
                Vector2 uv[4] = {{dist * uvScale, in * uvScale}, {(dist + seg) * uvScale, in * uvScale}, {(dist + seg) * uvScale, out * uvScale},
                                 {dist * uvScale, out * uvScale}};
                fx::texQuad(tex, p, uv, tint);
            }
            dist += seg;
        }
    }

    void drawWorld() {
        Vector2 o = camOff();
        Rectangle view{-o.x - 160, -o.y - 160, VW + 320.0f, VH + 320.0f};
        Texture2D& grass = E().tex("tex/grass");
        Texture2D& asphalt = E().tex("tex/asphalt");
        Texture2D& water = E().tex("tex/water");
        Texture2D& tulips = E().tex("tex/tulips");
        Texture2D& wood = E().tex("tex/wood");
        // grass, tiled in world space
        fx::tiled(grass, {0, 0, (float)VW, (float)VH}, 1.0f, {-o.x, -o.y});
        rlPushMatrix();
        rlTranslatef(o.x, o.y, 0);
        // mowing stripes on top of the texture
        for (float y = std::floor(view.y / 160) * 160; y < view.y + view.height; y += 160)
            DrawRectangleRec({view.x, y, view.width, 80}, alpha(WHITE, 0.035f));
        // tulip fields with dirt borders and a soft shadow
        const Rectangle fields[] = {{900, 900, 700, 500}, {2400, 1500, 500, 600}, {100, 150, 900, 250}, {1150, 1500, 500, 400}};
        for (const Rectangle& r : fields) {
            if (!CheckCollisionRecs(r, view)) continue;
            DrawRectangleRounded({r.x - 10, r.y - 4, r.width + 20, r.height + 20}, 0.04f, 8, alpha(BLACK, 0.18f));
            DrawRectangleRounded(shrink(r, -8), 0.04f, 8, rgba(0x6a4628ff));
            fx::tiled(tulips, r, 1.0f, {r.x * 0.5f, r.y});
            DrawRectangleLinesEx(r, 2, alpha(BLACK, 0.3f));
        }
        // canal: animated water with stone banks
        Rectangle canal{1980, -200, 180, 2900};
        if (CheckCollisionRecs(canal, view)) {
            fx::tiled(water, canal, 1.0f, {t_ * 12, t_ * 20});
            DrawRectangleGradientH((int)canal.x, (int)canal.y, 24, (int)canal.height, alpha(BLACK, 0.35f), alpha(BLACK, 0));
            DrawRectangleGradientH((int)(canal.x + canal.width - 24), (int)canal.y, 24, (int)canal.height, alpha(BLACK, 0), alpha(BLACK, 0.35f));
            fx::tiled(E().tex("tex/brick_dark"), {canal.x - 14, canal.y, 14, canal.height}, 0.5f);
            fx::tiled(E().tex("tex/brick_dark"), {canal.x + canal.width, canal.y, 14, canal.height}, 0.5f);
        }
        // track: drop shadow, curbs, asphalt, then lines
        float half = kTrackW / 2;
        for (int i = 0; i < kSamples; i++) {
            int j = (i + 1) % kSamples;
            if (!CheckCollisionPointRec(center_[i], view) && !CheckCollisionPointRec(center_[j], view)) continue;
            for (int side : {-1, 1}) {
                Vector2 a0 = center_[i] + normal_[i] * (side * (half + 30)), a1 = center_[i] + normal_[i] * (side * half);
                Vector2 b0 = center_[j] + normal_[j] * (side * (half + 30)), b1 = center_[j] + normal_[j] * (side * half);
                tri(a0, b0, b1, alpha(BLACK, 0.16f));
                tri(a0, b1, a1, alpha(BLACK, 0.16f));
                // curb: red/white blocks with a lit top edge
                Vector2 c0 = center_[i] + normal_[i] * (side * (half + 14)), c1 = center_[j] + normal_[j] * (side * (half + 14));
                Color cc = (i / 3) % 2 ? rgba(0xd8283aff) : rgba(0xf4f0f4ff);
                tri(a1, b1, c1, cc);
                tri(a1, c1, c0, cc);
                DrawLineEx(c0, c1, 2, darken(cc, 0.35f));
            }
        }
        strip(asphalt, -half, half, 1.0f, WHITE, view);
        // racing line wear + edge lines
        for (int i = 0; i < kSamples; i++) {
            int j = (i + 1) % kSamples;
            if (!CheckCollisionPointRec(center_[i], view)) continue;
            for (int side : {-1, 1}) DrawLineEx(center_[i] + normal_[i] * (side * (half - 8)), center_[j] + normal_[j] * (side * (half - 8)), 3, alpha(WHITE, 0.85f));
            DrawLineEx(center_[i], center_[j], 26, alpha(BLACK, 0.06f));
        }
        for (int i = 0; i < kSamples; i += 8) {
            if (!CheckCollisionPointRec(center_[i], view)) continue;
            DrawLineEx(center_[i], center_[(i + 3) % kSamples], 4, alpha(WHITE, 0.35f));
        }
        // wooden bridge deck + railings where the track crosses the canal
        for (int i = 0; i < kSamples; i++) {
            int j = (i + 1) % kSamples;
            if (center_[i].x < canal.x - 20 || center_[i].x > canal.x + canal.width + 20) continue;
            if (!CheckCollisionPointRec(center_[i], view)) continue;
            Vector2 p[4] = {center_[i] - normal_[i] * (half + 18), center_[j] - normal_[j] * (half + 18), center_[j] + normal_[j] * (half + 18),
                            center_[i] + normal_[i] * (half + 18)};
            float v0 = i * 25.0f, v1 = v0 + Vector2Distance(center_[i], center_[j]);
            Vector2 uv[4] = {{0, v0}, {0, v1}, {2 * (half + 18), v1}, {2 * (half + 18), v0}};
            fx::texQuad(wood, p, uv, WHITE);
            for (int side : {-1, 1}) DrawLineEx(center_[i] + normal_[i] * (side * (half + 18)), center_[j] + normal_[j] * (side * (half + 18)), 6, rgba(0x3a2418ff));
        }
        // start/finish: chequered band + grid boxes
        Vector2 n0 = normal_[0], d0 = dirAt(0);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 10; c++) {
                Vector2 p = center_[0] + n0 * (-half + c * kTrackW / 10) + d0 * (r * 17.0f);
                Vector2 q = p + n0 * (kTrackW / 10), d = d0 * 17.0f;
                Color cc = (r + c) % 2 ? WHITE : col::outline;
                tri(p, q, q + d, cc);
                tri(p, q + d, p + d, cc);
            }
        for (int g = 0; g < 4; g++) {
            Vector2 gp = center_[0] - d0 * (70.0f + (g / 2) * 90) + n0 * ((g % 2) ? 40.0f : -40.0f);
            DrawLineEx(gp + n0 * 22 + d0 * 24, gp - n0 * 22 + d0 * 24, 3, alpha(WHITE, 0.8f));
            DrawLineEx(gp + n0 * 22 + d0 * 24, gp + n0 * 22 - d0 * 10, 3, alpha(WHITE, 0.8f));
            DrawLineEx(gp - n0 * 22 + d0 * 24, gp - n0 * 22 - d0 * 10, 3, alpha(WHITE, 0.8f));
        }
        // skid marks
        for (auto& sk : skids_) DrawLineEx(sk.a, sk.b, 6, alpha(rgba(0x15121cff), 0.3f * std::min(1.0f, sk.life)));
        // item boxes: spinning glossy cubes with a "?"
        for (auto& bx : boxes_) {
            if (bx.respawn > 0) continue;
            Vector2 bp = center_[bx.sample] + normal_[bx.sample] * bx.lateral;
            float bob = std::sin(t_ * 3 + bx.sample) * 3;
            DrawEllipse((int)bp.x + 3, (int)bp.y + 14, 18, 7, alpha(BLACK, 0.3f));
            fx::beginAdd();
            fx::glow({bp.x, bp.y + bob}, 46, alpha(ColorFromHSV(std::fmod(t_ * 90 + bx.sample, 360), 0.6f, 1), 0.35f));
            fx::endAdd();
            rlPushMatrix();
            rlTranslatef(bp.x, bp.y + bob, 0);
            rlRotatef(std::sin(t_ * 2 + bx.sample) * 18, 0, 0, 1);
            Color hc = ColorFromHSV(std::fmod(t_ * 90 + bx.sample, 360), 0.55f, 1);
            DrawRectangleRounded({-17, -17, 34, 34}, 0.3f, 6, col::outline);
            fx::gradRoundRect({-15, -15, 30, 30}, 0.3f, lighten(hc, 0.4f), darken(hc, 0.2f));
            DrawRectangleRounded({-12, -12, 24, 8}, 0.6f, 6, alpha(WHITE, 0.45f));
            rlPopMatrix();
            ui::textShadow("?", bp.x, bp.y + bob + 1, 20, WHITE, ui::Center, true, 2);
        }
        // hagelslag spills: a puddle of sprinkles
        for (auto& sp : spills_) {
            DrawEllipse((int)sp.pos.x, (int)sp.pos.y, 34, 26, alpha(rgba(0x2a1608ff), 0.35f));
            for (int i = 0; i < 40; i++) {
                Vector2 p = sp.pos + fromAngle(i * 2.4f, 4 + (i % 9) * 3.2f);
                DrawLineEx(p, p + fromAngle(i * 1.3f, 5), 3, i % 4 ? rgba(0x3b2112ff) : rgba(0x7a4a2aff));
            }
        }
        // grandstand alongside the start straight
        Vector2 gs = center_[6] - normal_[6] * (kTrackW / 2 + 90);
        rlPushMatrix();
        rlTranslatef(gs.x, gs.y, 0);
        rlRotatef(angleOf(dirAt(6)) * RAD2DEG, 0, 0, 1);
        DrawRectangleRec({-178, -30, 356, 80}, alpha(BLACK, 0.25f));
        fx::gradRoundRect({-170, -40, 340, 76}, 0.1f, rgba(0x4a3a6aff), rgba(0x2a1a4aff));
        for (int row = 0; row < 3; row++)
            for (int i = 0; i < 32; i++) {
                Vector2 hp{-158 + i * 10.0f + (row % 2) * 5, -26 + row * 20 - std::abs(std::sin(t_ * 6 + i + row)) * 3};
                DrawCircleV({hp.x, hp.y + 5}, 5, ColorFromHSV((float)((i * 47 + row * 90) % 360), 0.5f, 0.8f));
                DrawCircleV(hp, 3.6f, i % 3 ? rgba(0xf0c8a0ff) : rgba(0x8a5a3aff));
            }
        fx::gradRoundRect({-176, -52, 352, 14}, 0.5f, rgba(0x8f5cffff), rgba(0x5b2fb0ff));
        rlPopMatrix();
        // windmills, trees, bushes (sprites with soft shadows)
        windmill({1260, 1150});
        windmill({2700, 300});
        Texture2D& tree = E().tex("spr/tree");
        Texture2D& bush = E().tex("spr/bush");
        const Vector2 trees[] = {{250, 600}, {700, 1200}, {1300, 250}, {2000 - 60, 800}, {2800, 1200}, {1600, 1900}, {600, 2300}, {2300, 2300}, {150, 1900}, {2700, 2000}};
        for (int i = 0; i < 10; i++) {
            Vector2 tp = trees[i];
            if (!CheckCollisionPointRec(tp, view)) continue;
            DrawEllipse((int)tp.x + 26, (int)tp.y + 34, 92, 70, alpha(BLACK, 0.22f));
            fx::sprite(tree, tp, 1.0f + (i % 3) * 0.15f, i * 37.0f);
        }
        for (int i = 0; i < kSamples; i += 37) {
            Vector2 bp = center_[i] + normal_[i] * ((i % 2 ? 1 : -1) * (half + 70));
            if (!CheckCollisionPointRec(bp, view) || (bp.x > canal.x - 40 && bp.x < canal.x + canal.width + 40)) continue;
            fx::sprite(bush, bp, 0.8f, (float)i * 13);
        }
        rlPopMatrix();
    }

    void windmill(Vector2 p) {
        Vector2 o = camOff();
        if (p.x + o.x < -200 || p.x + o.x > VW + 200 || p.y + o.y < -200 || p.y + o.y > VH + 200) return;
        DrawEllipse((int)p.x + 22, (int)p.y + 26, 70, 62, alpha(BLACK, 0.22f));
        // octagonal thatched cap, lit from the top-left
        DrawPoly(p, 8, 62, 22.5f, col::outline);
        DrawPoly(p, 8, 58, 22.5f, rgba(0x6a4a30ff));
        DrawCircleGradient(p, 56, rgba(0x9a6a40ff), rgba(0x4a2e18ff));
        for (int i = 0; i < 8; i++) DrawLineEx(p, p + fromAngle(i * PI / 4 + PI / 8, 56), 2, alpha(BLACK, 0.25f));
        // sails with a lattice
        for (int i = 0; i < 4; i++) {
            float a = t_ * 0.8f + i * PI / 2;
            Vector2 d = fromAngle(a), n{-d.y, d.x};
            Vector2 root = p + d * 16, tip = p + d * 130;
            Vector2 q[4] = {root + n * 2, tip + n * 2, tip + n * 22, root + n * 22};
            DrawTriangle(q[0], q[1], q[2], alpha(rgba(0xf4ecd8ff), 0.92f));
            DrawTriangle(q[0], q[2], q[1], alpha(rgba(0xf4ecd8ff), 0.92f));
            DrawTriangle(q[0], q[2], q[3], alpha(rgba(0xf4ecd8ff), 0.92f));
            DrawTriangle(q[0], q[3], q[2], alpha(rgba(0xf4ecd8ff), 0.92f));
            for (int k = 1; k < 6; k++) DrawLineEx(root + d * (k * 20.0f) + n * 2, root + d * (k * 20.0f) + n * 22, 1.5f, rgba(0x6a4a30ff));
            DrawLineEx(p, tip, 5, rgba(0x3a2418ff));
        }
        DrawCircleV(p, 12, rgba(0x2a1a10ff));
        DrawCircleV({p.x - 3, p.y - 3}, 4, alpha(WHITE, 0.3f));
    }

    void drawKart(const Kart& k) {
        Vector2 o = camOff();
        Vector2 p = k.pos + o;
        if (p.x < -80 || p.x > VW + 80 || p.y < -80 || p.y > VH + 80) return;
        float ang = k.heading * RAD2DEG;
        DrawEllipse((int)p.x + 6, (int)p.y + 9, 34, 24, alpha(BLACK, 0.3f));
        if (k.boostT > 0) {
            fx::beginAdd();
            for (int i = 0; i < 4; i++) {
                float f = std::sin(t_ * 40 + i) * 3;
                fx::glow(p - fromAngle(k.heading, 34.0f + i * 12 + f), 20 - i * 3.0f, alpha(i < 2 ? col::gold : col::orange, 0.75f));
            }
            fx::endAdd();
        }
        std::string spr = std::string("spr/kart_") + k.who;
        fx::sprite(E().tex(spr), p, 0.62f, ang);
        // driver's head stays upright
        Vector2 hp = p - fromAngle(k.heading, 2) + Vector2{0, -3};
        art::portrait(k.who, hp, 14, col::outline);
        if (k.shieldT > 0) {
            fx::beginAdd();
            fx::glow(p, 52, alpha(col::cyan, 0.3f));
            fx::endAdd();
            DrawRing(p, 38, 41, 0, 360, 36, alpha(col::cyan, 0.6f + 0.3f * std::sin(t_ * 8)));
        }
        if (k.spinT > 0) ui::textShadow("@#!", p.x, p.y - 44, 18, col::red, ui::Center, true, 2);
        if (!k.player) {
            float w = ui::measure(k.name, 13, true) + 14;
            DrawRectangleRounded({p.x - w / 2, p.y - 52, w, 20}, 1, 8, alpha(col::bgDeep, 0.65f));
            ui::text(k.name, p.x, p.y - 42, 13, lighten(k.body, 0.4f), ui::Center, true);
        }
    }

    // ------------------------------------------------------------ HUD
    void minimap() {
        Rectangle r{VW - 230, 20, 210, 180};
        DrawRectangleRounded(r, 0.12f, 8, alpha(col::bgDeep, 0.7f));
        float sc = 0.068f;
        Vector2 off{r.x + 4, r.y + 6};
        for (int i = 0; i < kSamples; i += 2) {
            Vector2 a = center_[i] * sc + off, b = center_[(i + 2) % kSamples] * sc + off;
            DrawLineEx(a, b, 6, rgba(0x6a6a72ff));
        }
        for (int i = (int)karts_.size() - 1; i >= 0; i--) {
            Vector2 p = karts_[i].pos * sc + off;
            DrawCircleV(p, karts_[i].player ? 7 : 5, col::outline);
            DrawCircleV(p, karts_[i].player ? 5.5f : 4, karts_[i].body);
        }
    }

    void drawHud() {
        Kart& me = karts_[0];
        // place
        Rectangle pl{20, 72, 170, 96};
        DrawRectangleRounded(pl, 0.2f, 8, alpha(col::bgDeep, 0.75f));
        Color pc = me.place == 1 ? col::gold : me.place == 2 ? rgba(0xc8d0e0ff) : me.place == 3 ? rgba(0xd8904aff) : col::text;
        ui::textShadow(std::to_string(me.place), pl.x + 42, pl.y + 50, 76, pc, ui::Center, true, 3);
        ui::text(me.place == 1 ? "st" : me.place == 2 ? "nd" : me.place == 3 ? "rd" : "th", pl.x + 74, pl.y + 30, 24, pc, ui::Left, true);
        ui::text(TextFormat("LAP %d/%d", std::clamp(me.lap + 1, 1, kLaps), kLaps), pl.x + 92, pl.y + 62, 18, col::text, ui::Left, true);
        ui::text(TextFormat("%d:%05.2f", (int)raceT_ / 60, std::fmod(raceT_, 60.0f)), pl.x + 92, pl.y + 84, 15, col::muted, ui::Left, true);
        // item slot
        Rectangle it{210, 72, 96, 96};
        DrawRectangleRounded(it, 0.25f, 8, alpha(col::bgDeep, 0.75f));
        DrawRectangleRoundedLinesEx(it, 0.25f, 8, 2, alpha(col::gold, 0.6f));
        Item show = me.item;
        if (itemRoll_ > 0) show = (Item)(1 + (int)(t_ * 20) % 3);
        Vector2 ic = center(it);
        switch (show) {
            case Item::Boost: art::boba(ic, 56); break;
            case Item::Spill: art::itemIcon("hagelslag", ic, 64); break;
            case Item::Shield: art::itemIcon("shield", ic, 60); break;
            default: ui::text("-", ic.x, ic.y, 30, col::muted, ui::Center, true); break;
        }
        if (me.item != Item::None && itemRoll_ <= 0) ui::text("E", it.x + it.width - 12, it.y + it.height - 12, 14, col::gold, ui::Center, true);
        // drift charge
        if (me.driftDir != 0) {
            float k = clamp01(me.driftT / 1.6f);
            Color dc = me.driftT > 1.6f ? col::violet : me.driftT > 0.8f ? col::orange : col::cyan;
            ui::bar({VW / 2.0f - 100, VH - 40.0f, 200, 14}, k, dc);
            ui::text("DRIFT", VW / 2.0f, VH - 54.0f, 14, dc, ui::Center, true);
        }
        // speed
        ui::text(TextFormat("%d km/h", (int)(std::abs(me.speed) / 5)), 24, VH - 30.0f, 20, col::text, ui::Left, true);
        minimap();
        if (lapFlash_ > 0) {
            float a = clamp01(lapFlash_);
            ui::textOutline(me.lap == kLaps - 1 ? "FINAL LAP!" : TextFormat("LAP %d", me.lap + 1), VW / 2.0f, 180, 60, alpha(col::gold, a), alpha(col::outline, a), 4);
        }
        ui::text("W/S drive   A/D steer   SPACE drift   E item", VW - 20.0f, VH - 24.0f, 14, alpha(col::text, 0.6f), ui::Right, true);
    }

    void drawCountdown() {
        Rectangle box{VW / 2.0f - 130, 120, 260, 90};
        DrawRectangleRounded(box, 0.3f, 8, rgba(0x15121cff));
        int lit = phase_ == Phase::Countdown ? (int)phaseT_ + 1 : 0;
        for (int i = 0; i < 3; i++) {
            Vector2 c{box.x + 50 + i * 80.0f, box.y + 45};
            bool on = i < lit && lit <= 3;
            Color cc = on ? col::red : rgba(0x3a1a1aff);
            if (lit > 3 || (phase_ == Phase::Countdown && phaseT_ >= 3)) cc = col::green;
            if (on) {
                fx::beginAdd();
                fx::glow(c, 60, alpha(cc, 0.6f));
                fx::endAdd();
            }
            DrawCircleV(c, 28, cc);
        }
        if (phase_ == Phase::Intro) {
            Rectangle pn{VW / 2.0f - 360, 240, 720, 380};
            ui::panel(pn, alpha(col::bgDeep, 0.88f), 0.08f, alpha(col::orange, 0.6f));
            fx::gradientText("PIETER KART", VW / 2.0f, 300, 80, WHITE, rgba(0xffb08aff), 5);
            ui::text("TULIP CIRCUIT  -  3 LAPS", VW / 2.0f, 360, 22, col::gold, ui::Center, true);
            for (int i = 0; i < 4; i++) {
                Vector2 p{VW / 2.0f - 240 + i * 160.0f, 470};
                art::portrait(karts_[i].who, p, 48, karts_[i].body);
                ui::text(karts_[i].name, p.x, p.y + 70, 20, karts_[i].body, ui::Center, true);
            }
        }
    }

    void drawResults() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f * clamp01(phaseT_ * 2)));
        float k = easeOutBack(clamp01(phaseT_ / 0.5f));
        fx::gradientText("FINISH!", VW / 2.0f, 110, 80 * k, WHITE, col::gold, 5);
        // podium
        std::array<int, 4> order{0, 1, 2, 3};
        std::sort(order.begin(), order.end(), [&](int a, int b) { return karts_[a].place < karts_[b].place; });
        const float heights[3] = {150, 110, 80};
        const float xs[3] = {VW / 2.0f, VW / 2.0f - 200, VW / 2.0f + 200};
        for (int i = 0; i < 3; i++) {
            const Kart& kk = karts_[order[i]];
            float h = heights[i] * k;
            Rectangle col{xs[i] - 85, 470 - h, 170, h};
            fx::gradRoundRect(col, 0.08f, lighten(kk.body, 0.2f), darken(kk.body, 0.4f));
            ui::textShadow(std::to_string(i + 1), col.x + col.width / 2, col.y + 40, 48, WHITE, ui::Center, true, 3);
            art::portrait(kk.who, {xs[i], col.y - 50 + std::sin(t_ * 4 + i) * 4}, 44, kk.player ? col::gold : col::outline);
            ui::text(kk.name, xs[i], 500, 20, kk.player ? col::gold : col::text, ui::Center, true);
        }
        Kart& me = karts_[0];
        ui::text(TextFormat("You: %s   -   best lap %.2fs", ordinal(me.place), me.bestLap), VW / 2.0f, 545, 22, col::text, ui::Center, true);
        ui::coin({VW / 2.0f - 70, 585}, 14);
        ui::text("+" + fmtNum(reward_) + " Pietercoins", VW / 2.0f - 48, 585, 22, col::gold, ui::Left, true);
        if (phaseT_ > 1) {
            if (ui::button({VW / 2.0f - 260, 625, 250, 60}, "RACE AGAIN", rgba(0xd8582aff), 24)) reset();
            if (ui::button({VW / 2.0f + 10, 625, 250, 60}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void draw() override {
        drawWorld();
        // karts, back to front by y
        std::array<int, 4> order{0, 1, 2, 3};
        std::sort(order.begin(), order.end(), [&](int a, int b) { return karts_[a].pos.y < karts_[b].pos.y; });
        for (int i : order) drawKart(karts_[i]);
        rlPushMatrix();
        Vector2 o = camOff();
        rlTranslatef(o.x, o.y, 0);
        fx_.draw();
        rlPopMatrix();
        art::vignette(0.35f);
        if (phase_ == Phase::Race) drawHud();
        if (phase_ == Phase::Intro || phase_ == Phase::Countdown) drawCountdown();
        if (phase_ == Phase::Results) drawResults();
        uiFx_.draw();
        if (phase_ != Phase::Results && ui::backButton()) E().go(makeMenu);
    }

private:
    Phase phase_ = Phase::Intro;
    std::vector<Vector2> center_, normal_;
    std::vector<Kart> karts_;
    std::vector<Box> boxes_;
    std::vector<Spill> spills_;
    std::vector<Skid> skids_;
    Vector2 cam_{};
    float t_ = 0, phaseT_ = 0, raceT_ = 0, itemRoll_ = 0, lapFlash_ = 0;
    int lastLight_ = -1, finishOrder_ = 0;
    bool autopilot_ = false;
    long long reward_ = 0;
    Particles fx_, uiFx_;
};

}  // namespace

std::unique_ptr<Scene> makeKart() { return std::make_unique<KartScene>(); }

}  // namespace pg
