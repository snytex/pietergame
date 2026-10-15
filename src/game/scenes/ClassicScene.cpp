// CLASSIC: the mode that has been in every PieterGame. Dodge enemies for 10
// waves, minibosses at wave 5 and 8, THE HORSE at wave 10. You cannot attack
// directly; during boss fights you collect boba which auto-fires at the boss.
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <cstdlib>

namespace pg {
namespace {

constexpr Rectangle kArena{28, 84, VW - 56, VH - 112};
constexpr int kFinalWave = 10;

enum class EType { Chaser, Bouncer, Shooter, Charger, Spinner };
enum class BossKind { None, Davi, Kira, Horse };
enum class Phase { Intro, Playing, Cleared, Dead, Victory };

struct Enemy {
    EType type;
    Vector2 pos{}, vel{}, aim{};
    float r = 18, t = 0, life = 14, spawn = 0, timer = 0;
    int state = 0;
    bool dead = false;
};

struct Bullet {
    Vector2 pos, vel;
    float r;
    Color color;
    int look = 0;  // 0 orb, 1 controller, 2 heart, 3 horseshoe
    float rot = 0, life = 9;
    bool dead = false;
};

struct Pickup {
    enum Kind { Coin, BigCoin, Boba, Power } kind;
    Vector2 pos;
    std::string power;
    float t = 0, life = 12;
    bool dead = false;
};

struct Warning {
    Vector2 pos;
    EType type;
    float t = 0;
};

struct Homing {
    Vector2 pos, vel;
    float t = 0;
    bool dead = false;
};

struct Boss {
    BossKind kind = BossKind::None;
    std::string name, title;
    Vector2 pos{}, vel{}, dir{};
    float r = 60, t = 0, attackT = 2, flash = 0, enter = 0, spiral = 0, talkT = 0;
    int hp = 1, maxHp = 1, pattern = 0, state = 0;
    std::string talk;
    bool alive() const { return kind != BossKind::None && hp > 0; }
    bool enraged() const { return hp * 2 <= maxHp; }
};

class ClassicScene : public Scene {
public:
    Presence presence() const override {
        Presence p;
        p.details = wave_ > kFinalWave ? "Classic - endless wave " + std::to_string(wave_)
                                       : "Classic - wave " + std::to_string(wave_) + " / " + std::to_string(kFinalWave);
        if (phase_ == Phase::Dead) p.state = "Got rekt on wave " + std::to_string(wave_);
        else if (phase_ == Phase::Victory) p.state = "Defeated The Horse!";
        else if (boss_.alive()) p.state = "Fighting " + boss_.name + " (" + std::to_string(boss_.hp) + " HP left)";
        else p.state = std::to_string(hearts_) + (hearts_ == 1 ? " heart left" : " hearts left");
        if (boss_.alive()) {
            p.largeImage = boss_.kind == BossKind::Davi ? "davi" : boss_.kind == BossKind::Kira ? "kira" : "horse";
            p.largeText = boss_.name + " - " + boss_.title;
            p.smallImage = "logo";
            p.smallText = "PieterGame 3";
        }
        return p;
    }

    void enter() override {
        P().stats.classicRuns++;
        P().save();
        player_ = {kArena.x + kArena.width / 2, kArena.y + kArena.height / 2};
        int first = 1;
        if (const char* w = std::getenv("PG_WAVE")) first = std::max(1, std::atoi(w));  // dev hook
        startWave(first);
    }

    void exit() override {
        P().stats.bestWave = std::max(P().stats.bestWave, wave_ - (phase_ == Phase::Dead ? 1 : 0));
        P().save();
    }

    // ================================================================ update
    void update(float dt) override {
        t_ += dt;
        if (E().backPressed()) {
            if (phase_ == Phase::Dead || phase_ == Phase::Victory) E().go(makeMenu);
            else paused_ = !paused_;
        }
        if (paused_) return;

        fx_.update(dt);
        phaseT_ += dt;

        switch (phase_) {
            case Phase::Intro:
                updatePlayer(dt);
                if (phaseT_ > 1.8f) { phase_ = Phase::Playing; phaseT_ = 0; }
                break;
            case Phase::Playing: updatePlaying(dt); break;
            case Phase::Cleared:
                updatePlayer(dt);
                updatePickups(dt);
                if (phaseT_ > 2.6f) startWave(wave_ + 1);
                break;
            case Phase::Dead:
            case Phase::Victory: break;
        }
    }

    void startWave(int w) {
        wave_ = w;
        phase_ = Phase::Intro;
        phaseT_ = 0;
        waveT_ = 0;
        spawnT_ = 0.6f;
        coinT_ = 1.0f;
        bobaT_ = 2.0f;
        powerT_ = rnd(8, 14);
        waveLen_ = 22 + std::min(w, 12) * 2.0f;
        boss_ = Boss{};
        BossKind bk = BossKind::None;
        if (w == 5) bk = BossKind::Davi;
        else if (w == 8) bk = BossKind::Kira;
        else if (w == kFinalWave) bk = BossKind::Horse;
        else if (w > kFinalWave && w % 5 == 0) bk = (BossKind)(1 + (w / 5) % 3);
        if (bk != BossKind::None) spawnBoss(bk);
        if (bk == BossKind::None) E().sfx.play(Sfx::Bell, 0.5f);
        else E().sfx.play(Sfx::Boom, 0.6f);
        if (bk == BossKind::Horse) E().sfx.play(Sfx::Neigh);
    }

    void spawnBoss(BossKind k) {
        Boss& b = boss_;
        b.kind = k;
        float extra = wave_ > kFinalWave ? 1.0f + (wave_ - kFinalWave) * 0.1f : 1.0f;
        switch (k) {
            case BossKind::Davi: b.name = "DAVI"; b.title = "PRO (CLAIMS) SINCE 2010"; b.r = 58; b.maxHp = 10; break;
            case BossKind::Kira: b.name = "KIRA"; b.title = "THE SISTER RIVAL"; b.r = 56; b.maxHp = 12; break;
            case BossKind::Horse: b.name = "THE HORSE"; b.title = "JUHUH!!!"; b.r = 74; b.maxHp = 18; break;
            default: break;
        }
        b.maxHp = (int)(b.maxHp * extra);
        b.hp = b.maxHp;
        b.pos = {kArena.x + kArena.width / 2, kArena.y - 120};
        b.attackT = 2.5f;
    }

    float enemyTimeScale() const { return slowT_ > 0 ? 0.35f : 1.0f; }

    void updatePlaying(float dt) {
        waveT_ += dt;
        updatePlayer(dt);
        if (phase_ != Phase::Playing) return;

        float edt = dt * enemyTimeScale();
        bool bossWave = boss_.kind != BossKind::None;
        float diff = 1 + (wave_ - 1) * 0.07f;

        // spawn regular enemies (fewer during boss fights)
        spawnT_ -= dt;
        int maxEnemies = bossWave ? 2 + wave_ / 4 : 5 + wave_ * 2;
        float interval = std::max(0.35f, 1.5f - wave_ * 0.11f) * (bossWave ? 2.4f : 1.0f);
        if (spawnT_ <= 0 && (int)(enemies_.size() + warnings_.size()) < maxEnemies) {
            spawnT_ = interval * rnd(0.7f, 1.3f);
            queueEnemy(pickType());
        }

        // pickups
        coinT_ -= dt;
        if (coinT_ <= 0) {
            coinT_ = rnd(0.8f, 1.6f);
            pickups_.push_back({chance(0.12f) ? Pickup::BigCoin : Pickup::Coin, randomArenaPoint(40, 0)});
        }
        powerT_ -= dt;
        if (powerT_ <= 0) {
            powerT_ = rnd(14, 22);
            static const char* ps[] = {"shield", "speed", "slowmo", "magnet", "heal", "nuke"};
            Pickup p{Pickup::Power, randomArenaPoint(60, 150)};
            p.power = ps[rndi(0, chance(0.15f) ? 5 : 4)];
            pickups_.push_back(p);
        }
        if (boss_.alive() && boss_.enter >= 1) {
            bobaT_ -= dt;
            int bobaOnField = (int)std::count_if(pickups_.begin(), pickups_.end(), [](auto& p) { return p.kind == Pickup::Boba; });
            if (bobaT_ <= 0 && bobaOnField < 2) {
                bobaT_ = rnd(2.0f, 3.2f);
                Vector2 p = randomArenaPoint(60, 120);
                for (int tries = 0; tries < 8 && Vector2Distance(p, boss_.pos) < boss_.r + 100; tries++) p = randomArenaPoint(60, 120);
                pickups_.push_back({Pickup::Boba, p});
                pickups_.back().life = 9;
            }
        }

        // spawn warnings
        for (auto& w : warnings_) {
            w.t += dt;
            if (w.t >= 0.85f) spawnEnemy(w.type, w.pos, diff);
        }
        std::erase_if(warnings_, [](auto& w) { return w.t >= 0.85f; });

        for (auto& e : enemies_) updateEnemy(e, edt, diff);
        if (boss_.kind != BossKind::None) updateBoss(edt, dt);
        for (auto& b : bullets_) {
            b.pos += b.vel * edt;
            b.rot += edt * 6;
            b.life -= edt;
            if (b.life <= 0 || !CheckCollisionPointRec(b.pos, shrink(kArena, -60))) b.dead = true;
        }
        updatePickups(dt);
        updateHoming(dt);

        // collisions
        for (auto& e : enemies_)
            if (!e.dead && e.spawn >= 1 && Vector2Distance(e.pos, player_) < e.r + kPlayerR - 6) hitPlayer(e.pos);
        for (auto& b : bullets_)
            if (!b.dead && Vector2Distance(b.pos, player_) < b.r + kPlayerR - 7) {
                if (hitPlayer(b.pos)) b.dead = true;
            }
        if (boss_.alive() && boss_.enter >= 1 && Vector2Distance(boss_.pos, player_) < boss_.r + kPlayerR - 10)
            hitPlayer(boss_.pos);

        std::erase_if(enemies_, [](auto& e) { return e.dead; });
        std::erase_if(bullets_, [](auto& b) { return b.dead; });

        // wave end
        bool done = bossWave ? !boss_.alive() && boss_.enter >= 1 : waveT_ >= waveLen_;
        if (done) clearWave();
    }

    EType pickType() {
        std::vector<std::pair<EType, float>> pool = {{EType::Chaser, 3}, {EType::Bouncer, 2.5f}};
        if (wave_ >= 2) pool.push_back({EType::Shooter, 1.6f});
        if (wave_ >= 3) pool.push_back({EType::Charger, 1.4f});
        if (wave_ >= 6) pool.push_back({EType::Spinner, 1.0f});
        float total = 0;
        for (auto& p : pool) total += p.second;
        float r = rnd(0, total);
        for (auto& p : pool) {
            if ((r -= p.second) <= 0) return p.first;
        }
        return EType::Chaser;
    }

    Vector2 randomArenaPoint(float margin, float awayFromPlayer) {
        Vector2 p{};
        for (int i = 0; i < 20; i++) {
            p = {rnd(kArena.x + margin, kArena.x + kArena.width - margin), rnd(kArena.y + margin, kArena.y + kArena.height - margin)};
            if (Vector2Distance(p, player_) >= awayFromPlayer) break;
        }
        return p;
    }

    void queueEnemy(EType t) { warnings_.push_back({randomArenaPoint(40, 230), t}); }

    void spawnEnemy(EType t, Vector2 at, float diff) {
        Enemy e{t, at};
        e.life = rnd(11, 16);
        switch (t) {
            case EType::Chaser: e.r = 18; break;
            case EType::Bouncer: e.r = 15; e.vel = rndDir() * 250 * diff; break;
            case EType::Shooter: e.r = 21; e.timer = rnd(1.0f, 2.0f); e.vel = rndDir() * 40; break;
            case EType::Charger: e.r = 19; e.timer = rnd(0.8f, 1.6f); break;
            case EType::Spinner: e.r = 20; e.timer = 2.0f; e.vel = rndDir() * 60; break;
        }
        enemies_.push_back(e);
        E().sfx.play(Sfx::Spawn, 0.35f);
    }

    void fire(Vector2 from, Vector2 dir, float speed, Color c, int look = 0, float r = 9) {
        bullets_.push_back({from, Vector2Normalize(dir) * speed, r, c, look});
    }

    void ring(Vector2 from, int n, float speed, Color c, int look, float offset = 0, int gap = -1) {
        for (int i = 0; i < n; i++) {
            if (gap >= 0 && (i == gap || i == (gap + 1) % n)) continue;
            fire(from, fromAngle(offset + i * 2 * PI / n), speed, c, look);
        }
    }

    void bounceInArena(Vector2& p, Vector2& v, float r) {
        if (p.x < kArena.x + r) { p.x = kArena.x + r; v.x = std::abs(v.x); }
        if (p.x > kArena.x + kArena.width - r) { p.x = kArena.x + kArena.width - r; v.x = -std::abs(v.x); }
        if (p.y < kArena.y + r) { p.y = kArena.y + r; v.y = std::abs(v.y); }
        if (p.y > kArena.y + kArena.height - r) { p.y = kArena.y + kArena.height - r; v.y = -std::abs(v.y); }
    }

    void updateEnemy(Enemy& e, float dt, float diff) {
        e.t += dt;
        e.spawn = std::min(1.0f, e.spawn + dt * 3);
        e.life -= dt;
        if (e.life <= 0) {
            // expired enemies pop into coins
            e.dead = true;
            fx_.burst(e.pos, 12, enemyColor(e.type), 200, 5, 0.4f);
            pickups_.push_back({Pickup::Coin, e.pos});
            return;
        }
        Vector2 toP = Vector2Normalize(player_ - e.pos);
        switch (e.type) {
            case EType::Chaser: {
                float sp = 145 * diff;
                e.vel = damp(e.vel, toP * sp, 2.2f, dt);
                break;
            }
            case EType::Bouncer: break;
            case EType::Shooter:
                e.timer -= dt;
                e.vel = damp(e.vel, toP * -30 + fromAngle(e.t) * 40, 1, dt);
                if (e.timer <= 0) {
                    e.timer = std::max(1.1f, 2.4f - wave_ * 0.1f);
                    fire(e.pos, toP, 250 * diff, col::green);
                    if (wave_ >= 7) {
                        fire(e.pos, Vector2Rotate(toP, 0.25f), 250 * diff, col::green);
                        fire(e.pos, Vector2Rotate(toP, -0.25f), 250 * diff, col::green);
                    }
                    E().sfx.play(Sfx::Shoot, 0.4f);
                }
                break;
            case EType::Charger:
                e.timer -= dt;
                if (e.state == 0) {  // drifting
                    e.vel = damp(e.vel, toP * 50, 2, dt);
                    if (e.timer <= 0) { e.state = 1; e.timer = 0.75f; e.aim = toP; }
                } else if (e.state == 1) {  // telegraph
                    e.vel = damp(e.vel, {0, 0}, 8, dt);
                    e.aim = Vector2Normalize(damp(e.aim, toP, 1.5f, dt));
                    if (e.timer <= 0) { e.state = 2; e.timer = 0.55f; e.vel = e.aim * 700 * diff; E().sfx.play(Sfx::Dash, 0.4f); }
                } else {  // dashing
                    if (e.timer <= 0) { e.state = 0; e.timer = rnd(1.2f, 2.0f); }
                }
                break;
            case EType::Spinner:
                e.timer -= dt;
                if (e.timer <= 0) {
                    e.timer = 2.8f;
                    ring(e.pos, 8, 170 * diff, col::cyan, 0, e.t);
                    E().sfx.play(Sfx::Shoot, 0.35f, 0.7f);
                }
                break;
        }
        e.pos += e.vel * dt;
        bounceInArena(e.pos, e.vel, e.r);
    }

    void say(const std::string& s) {
        boss_.talk = s;
        boss_.talkT = 2.0f;
    }

    void updateBoss(float dt, float realDt) {
        Boss& b = boss_;
        b.t += dt;
        b.flash = std::max(0.0f, b.flash - realDt * 4);
        b.talkT -= realDt;
        if (b.hp <= 0) return;
        if (b.enter < 1) {
            b.enter = std::min(1.0f, b.enter + realDt * 0.6f);
            Vector2 target{kArena.x + kArena.width / 2, kArena.y + 150};
            b.pos = Vector2Lerp({target.x, kArena.y - 120}, target, easeOutCubic(b.enter));
            if (b.enter >= 1) say(b.kind == BossKind::Horse ? "JUHUH!!!" : b.kind == BossKind::Davi ? "Ez. I'm pro." : "Hiii Pieter <3");
            return;
        }
        Vector2 toP = Vector2Normalize(player_ - b.pos);
        float rage = b.enraged() ? 1.35f : 1.0f;
        b.attackT -= dt;

        switch (b.kind) {
            case BossKind::Davi: {
                b.vel = damp(b.vel, toP * 85 * rage, 1.2f, dt);
                if (b.attackT <= 0) {
                    b.pattern++;
                    if (b.enraged() && b.pattern % 3 == 0) {
                        ring(b.pos, 16, 210, col::violet, 1, b.t);
                        say(chance(0.5f) ? "RAGE QUIT MODE" : "my controller is broken!!");
                        E().shake(6);
                    } else {
                        for (int i = -2; i <= 2; i++) fire(b.pos, Vector2Rotate(toP, i * 0.22f), 290 * rage, col::violet, 1, 11);
                        if (chance(0.3f)) say(chance(0.5f) ? "PRO SINCE 2010" : "lag!! that was lag");
                    }
                    b.attackT = 1.9f / rage;
                    E().sfx.play(Sfx::Shoot, 0.6f, 0.8f);
                }
                break;
            }
            case BossKind::Kira: {
                Vector2 c = center(kArena);
                Vector2 target{c.x + std::sin(b.t * 0.7f) * 380, c.y + std::sin(b.t * 1.1f) * 170};
                b.vel = damp(b.vel, (target - b.pos) * 2.0f, 2, dt);
                b.spiral += dt;
                float rate = b.enraged() ? 0.09f : 0.13f;
                while (b.spiral > rate) {
                    b.spiral -= rate;
                    float a = b.t * 2.2f;
                    fire(b.pos, fromAngle(a), 175, col::pink, 2, 10);
                    if (b.enraged()) fire(b.pos, fromAngle(a + PI), 175, col::pink, 2, 10);
                }
                if (b.attackT <= 0) {
                    b.attackT = 6.0f;
                    say(chance(0.5f) ? "*wink*" : "Brother sister rivals FOREVER");
                    for (int i = 0; i < 2; i++) queueEnemy(EType::Chaser);
                    E().sfx.play(Sfx::Kiss);
                }
                break;
            }
            case BossKind::Horse: {
                // state 0 roam, 1 charge telegraph, 2 charging, 3 rain
                if (b.state == 0) {
                    b.vel = damp(b.vel, toP * 70 * rage, 1.5f, dt);
                    if (b.attackT <= 0) {
                        b.pattern++;
                        if (b.pattern % 3 == 2) {
                            b.state = 3;
                            b.attackT = 3.0f;
                            say("HORSESHOE RAIN!");
                        } else {
                            b.state = 1;
                            b.attackT = 0.9f / rage;
                            b.dir = toP;
                            E().sfx.play(Sfx::Neigh, 0.8f);
                            say("JUHUH!");
                        }
                    }
                } else if (b.state == 1) {
                    b.vel = damp(b.vel, {0, 0}, 10, dt);
                    b.dir = Vector2Normalize(damp(b.dir, toP, 2.5f, dt));
                    if (b.attackT <= 0) { b.state = 2; b.vel = b.dir * 820 * rage; E().sfx.play(Sfx::Dash); }
                } else if (b.state == 2) {
                    Vector2 before = b.vel;
                    b.pos += b.vel * dt;
                    bounceInArena(b.pos, b.vel, b.r);
                    if (before.x != b.vel.x || before.y != b.vel.y) {
                        E().shake(12);
                        E().sfx.play(Sfx::Boom, 0.7f);
                        ring(b.pos, b.enraged() ? 18 : 12, 220, col::orange, 3, rnd(0, 6.28f));
                        fx_.burst(b.pos, 30, col::orange, 400, 8, 0.6f);
                        b.vel = {0, 0};
                        b.pattern++;
                        b.state = (b.enraged() && b.pattern % 2 == 1) ? 1 : 0;
                        b.attackT = b.state == 1 ? 0.6f : 1.6f;
                        b.dir = Vector2Normalize(player_ - b.pos);
                    }
                    return;
                } else if (b.state == 3) {
                    b.vel = damp(b.vel, {0, 0}, 3, dt);
                    b.spiral += dt;
                    float rate = b.enraged() ? 0.1f : 0.16f;
                    while (b.spiral > rate) {
                        b.spiral -= rate;
                        Vector2 from{rnd(kArena.x + 20, kArena.x + kArena.width - 20), kArena.y - 20};
                        bullets_.push_back({from, {rnd(-30, 30), rnd(230, 320)}, 11, col::orange, 3});
                    }
                    if (b.attackT <= 0) { b.state = 0; b.attackT = 1.8f; }
                }
                break;
            }
            default: break;
        }
        b.pos += b.vel * dt;
        bounceInArena(b.pos, b.vel, b.r);
    }

    void updatePlayer(float dt) {
        Vector2 in{};
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) in.x -= 1;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.x += 1;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) in.y -= 1;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) in.y += 1;
        if (Vector2Length(in) > 0) in = Vector2Normalize(in);

        invuln_ = std::max(0.0f, invuln_ - dt);
        dashCd_ = std::max(0.0f, dashCd_ - dt);
        speedT_ = std::max(0.0f, speedT_ - dt);
        slowT_ = std::max(0.0f, slowT_ - dt);
        magnetT_ = std::max(0.0f, magnetT_ - dt);

        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_K)) && dashCd_ <= 0 && dashT_ <= 0) {
            dashDir_ = Vector2Length(in) > 0 ? in : (Vector2Length(pvel_) > 10 ? Vector2Normalize(pvel_) : Vector2{1, 0});
            dashT_ = 0.17f;
            dashCd_ = 1.0f;
            E().sfx.play(Sfx::Dash, 0.6f);
        }
        if (dashT_ > 0) {
            dashT_ -= dt;
            pvel_ = dashDir_ * 950;
            ghosts_.push_back({player_, 0.25f});
        } else {
            float sp = 330 * (speedT_ > 0 ? 1.6f : 1.0f);
            pvel_ = damp(pvel_, in * sp, 14, dt);
        }
        player_ += pvel_ * dt;
        player_.x = std::clamp(player_.x, kArena.x + kPlayerR, kArena.x + kArena.width - kPlayerR);
        player_.y = std::clamp(player_.y, kArena.y + kPlayerR, kArena.y + kArena.height - kPlayerR);
        tilt_ = damp(tilt_, pvel_.x * 0.03f, 10, dt);
        for (auto& g : ghosts_) g.second -= dt;
        std::erase_if(ghosts_, [](auto& g) { return g.second <= 0; });

        // powerups 1/2/3
        for (int i = 0; i < 3; i++)
            if (IsKeyPressed(KEY_ONE + i) && phase_ == Phase::Playing) useSlot(i);

        // cosmetic trail
        trailT_ -= dt;
        if (!P().trail.empty() && Vector2Length(pvel_) > 60 && trailT_ <= 0) {
            trailT_ = 0.025f;
            Particle p;
            p.pos = player_ + rndDir() * rnd(0, 8) + Vector2{0, 14};
            p.vel = pvel_ * -0.15f + rndDir() * 20;
            p.life = p.maxLife = 0.55f;
            p.size = rnd(5, 8);
            if (P().trail == "trail_rainbow") p.color = ColorFromHSV(std::fmod(t_ * 360, 360), 0.8f, 1);
            else if (P().trail == "trail_fire") { p.color = mix(col::gold, col::red, rnd()); p.vel.y -= 80; }
            else { p.color = rgba(0x3a2418ff); p.size = 6; p.endSize = 5; }
            fx_.add(p);
        }
    }

    void useSlot(int i) {
        const std::string& id = P().loadout[i];
        if (id.empty() || !P().take(id)) {
            E().sfx.play(Sfx::Error, 0.5f);
            fx_.floatText("none left!", player_ - Vector2{0, 40}, col::muted, 20);
            return;
        }
        applyPower(id);
    }

    void applyPower(const std::string& id) {
        const ItemDef* def = findItem(id);
        E().sfx.play(Sfx::Power);
        fx_.burst(player_, 24, def ? def->color : WHITE, 300, 6, 0.6f, PShape::Star);
        if (def) fx_.floatText(def->name + "!", player_ - Vector2{0, 46}, def->color, 24);
        if (id == "shield") shield_ = true;
        else if (id == "speed") speedT_ = 6;
        else if (id == "slowmo") slowT_ = 5;
        else if (id == "magnet") magnetT_ = 10;
        else if (id == "heal") hearts_++;
        else if (id == "nuke") {
            E().flash(col::green, 0.6f);
            E().shake(10);
            E().sfx.play(Sfx::Boom);
            for (auto& e : enemies_) {
                fx_.burst(e.pos, 14, enemyColor(e.type), 260, 6, 0.5f);
                pickups_.push_back({Pickup::Coin, e.pos});
            }
            enemies_.clear();
            bullets_.clear();
            warnings_.clear();
        }
    }

    // returns true if the player actually took the hit (or a shield ate it)
    bool hitPlayer(Vector2 from) {
        if (invuln_ > 0 || dashT_ > 0 || phase_ != Phase::Playing) return false;
        if (shield_) {
            shield_ = false;
            invuln_ = 1.0f;
            E().sfx.play(Sfx::Bubble);
            fx_.burst(player_, 30, col::cyan, 350, 6, 0.5f, PShape::Bubble);
            fx_.floatText("BLOCKED", player_ - Vector2{0, 40}, col::cyan, 24);
            return true;
        }
        hearts_--;
        invuln_ = 1.5f;
        pvel_ = Vector2Normalize(player_ - from) * 500;
        E().sfx.play(Sfx::Hurt);
        E().shake(9);
        E().flash(col::red, 0.35f);
        fx_.burst(player_, 20, col::red, 300, 6, 0.5f);
        if (hearts_ <= 0) die();
        return true;
    }

    void die() {
        phase_ = Phase::Dead;
        phaseT_ = 0;
        E().sfx.play(Sfx::Lose);
        fx_.burst(player_, 60, col::gold, 500, 8, 1.0f, PShape::Star);
        P().stats.bestWave = std::max(P().stats.bestWave, wave_ - 1);
        P().save();
    }

    void addCoins(int n, Vector2 at) {
        P().earn(n);
        earned_ += n;
        fx_.floatText("+" + std::to_string(n), at, col::gold, n >= 10 ? 30 : 22);
    }

    void updatePickups(float dt) {
        for (auto& p : pickups_) {
            p.t += dt;
            p.life -= dt;
            if (p.life <= 0) { p.dead = true; continue; }
            float d = Vector2Distance(p.pos, player_);
            bool coin = p.kind == Pickup::Coin || p.kind == Pickup::BigCoin;
            bool attract = coin && (magnetT_ > 0 || phase_ == Phase::Cleared);
            if (attract || d < 70) p.pos = damp(p.pos, player_, attract ? 6.0f : 9.0f, dt);
            if (d < kPlayerR + 16) {
                p.dead = true;
                switch (p.kind) {
                    case Pickup::Coin: addCoins(2, p.pos); E().sfx.play(Sfx::Coin, 0.45f, rnd(0.95f, 1.1f)); break;
                    case Pickup::BigCoin: addCoins(10, p.pos); E().sfx.play(Sfx::Coin, 0.7f, 0.8f); break;
                    case Pickup::Power: applyPower(p.power); break;
                    case Pickup::Boba:
                        E().sfx.play(Sfx::Gem);
                        homing_.push_back({player_, rndDir() * 300});
                        fx_.floatText("BOBA!", p.pos, col::boba, 24);
                        break;
                }
            }
        }
        std::erase_if(pickups_, [](auto& p) { return p.dead; });
    }

    void updateHoming(float dt) {
        for (auto& h : homing_) {
            h.t += dt;
            if (!boss_.alive()) { h.dead = true; continue; }
            Vector2 want = Vector2Normalize(boss_.pos - h.pos) * (500 + h.t * 600);
            h.vel = damp(h.vel, want, 5, dt);
            h.pos += h.vel * dt;
            if (std::fmod(h.t, 0.03f) < dt) {
                Particle p;
                p.pos = h.pos;
                p.life = p.maxLife = 0.3f;
                p.size = 6;
                p.color = col::boba;
                fx_.add(p);
            }
            if (Vector2Distance(h.pos, boss_.pos) < boss_.r) {
                h.dead = true;
                damageBoss(h.pos);
            }
        }
        std::erase_if(homing_, [](auto& h) { return h.dead; });
    }

    void damageBoss(Vector2 at) {
        Boss& b = boss_;
        b.hp--;
        b.flash = 1;
        E().sfx.play(Sfx::Boom, 0.5f, 1.5f);
        E().shake(5);
        fx_.burst(at, 18, col::boba, 300, 7, 0.5f);
        static const char* ouch[] = {"OW!", "hey!!", "not fair", "rigged", "lag!", "bruh"};
        if (chance(0.4f)) say(ouch[rndi(0, 5)]);
        if (b.hp <= 0) {
            E().sfx.play(Sfx::Boom);
            E().sfx.play(Sfx::Win);
            E().shake(18);
            E().flash(WHITE, 0.7f);
            fx_.burst(b.pos, 80, col::gold, 600, 10, 1.2f, PShape::Star);
            fx_.confetti(b.pos, 80);
            int bonus = b.kind == BossKind::Horse ? 500 : 200;
            addCoins(bonus, b.pos);
            for (int i = 0; i < 12; i++) pickups_.push_back({Pickup::BigCoin, b.pos + rndDir() * rnd(20, 120)});
            bullets_.clear();
        }
    }

    void clearWave() {
        for (auto& e : enemies_) {
            fx_.burst(e.pos, 10, enemyColor(e.type), 220, 5, 0.4f);
            pickups_.push_back({Pickup::Coin, e.pos});
        }
        enemies_.clear();
        bullets_.clear();
        warnings_.clear();
        homing_.clear();
        int bonus = 15 * wave_;
        addCoins(bonus, {VW / 2.0f, VH / 2.0f + 60});
        P().stats.bestWave = std::max(P().stats.bestWave, wave_);
        if (wave_ == kFinalWave) {
            phase_ = Phase::Victory;
            phaseT_ = 0;
            P().stats.classicWins++;
            addCoins(1000, {VW / 2.0f, VH / 2.0f});
            fx_.confetti({VW / 2.0f, VH / 2.0f}, 200, 800);
            E().sfx.play(Sfx::Win);
            P().save();
            return;
        }
        phase_ = Phase::Cleared;
        phaseT_ = 0;
        E().sfx.play(Sfx::Perfect);
        P().save();
    }

    // ================================================================ draw
    static Color enemyColor(EType t) {
        switch (t) {
            case EType::Chaser: return col::red;
            case EType::Bouncer: return col::orange;
            case EType::Shooter: return col::green;
            case EType::Charger: return col::pink;
            case EType::Spinner: return col::cyan;
        }
        return WHITE;
    }

    void drawEyes(Vector2 c, float r, Vector2 look, bool angry) {
        for (int s = -1; s <= 1; s += 2) {
            Vector2 e{c.x + s * r * 0.36f, c.y - r * 0.1f};
            DrawCircleV(e, r * 0.27f, WHITE);
            DrawCircleV(e + look * r * 0.1f, r * 0.13f, col::outline);
            if (angry) DrawLineEx({e.x + s * r * 0.32f, e.y - r * 0.5f}, {e.x - s * r * 0.3f, e.y - r * 0.28f}, r * 0.13f, col::outline);
        }
    }

    void drawEnemy(const Enemy& e) {
        float s = easeOutBack(e.spawn);
        float fade = clamp01(e.life / 0.5f);
        float r = e.r * s;
        Color c = alpha(enemyColor(e.type), fade);
        Vector2 look = Vector2Normalize(player_ - e.pos);
        DrawCircleV(e.pos + Vector2{3, 5}, r, alpha(BLACK, 0.3f * fade));
        switch (e.type) {
            case EType::Chaser: {
                // Useless piece of elektronik: angry little box
                Rectangle b{e.pos.x - r, e.pos.y - r, r * 2, r * 2};
                DrawRectangleRounded(shrink(b, -3), 0.35f, 8, alpha(col::outline, fade));
                DrawRectangleRounded(b, 0.35f, 8, alpha(rgba(0x8a8ca0ff), fade));
                DrawRectangleRec({b.x + r * 0.3f, b.y + r * 1.45f, r * 1.4f, r * 0.25f}, c);
                drawEyes(e.pos, r, look, true);
                DrawLineEx({e.pos.x + r * 0.5f, b.y}, {e.pos.x + r * 0.8f, b.y - r * 0.6f}, 3, alpha(col::outline, fade));
                DrawCircleV({e.pos.x + r * 0.8f, b.y - r * 0.6f}, 3, c);
                break;
            }
            case EType::Bouncer: {
                for (int i = 0; i < 8; i++) {
                    float a = e.t * 5 + i * PI / 4;
                    DrawTriangle(e.pos + fromAngle(a - 0.25f, r), e.pos + fromAngle(a, r * 1.45f), e.pos + fromAngle(a + 0.25f, r), c);
                }
                DrawCircleV(e.pos, r + 2, alpha(col::outline, fade));
                DrawCircleV(e.pos, r, c);
                drawEyes(e.pos, r, look, true);
                break;
            }
            case EType::Shooter: {
                // a chat bubble spamming messages at you
                Rectangle b{e.pos.x - r * 1.2f, e.pos.y - r, r * 2.4f, r * 1.8f};
                DrawTriangle({b.x + r * 0.4f, b.y + b.height - 2}, {b.x + r * 0.3f, b.y + b.height + r * 0.6f}, {b.x + r * 1.0f, b.y + b.height - 2}, c);
                DrawRectangleRounded(shrink(b, -3), 0.4f, 8, alpha(col::outline, fade));
                DrawRectangleRounded(b, 0.4f, 8, c);
                drawEyes({e.pos.x, e.pos.y - r * 0.1f}, r, look, false);
                if (e.timer < 0.4f) DrawCircleV(e.pos + look * r * 1.3f, 6 * (1 - e.timer / 0.4f), WHITE);
                break;
            }
            case EType::Charger: {
                if (e.state == 1) {
                    float k = 1 - e.timer / 0.75f;
                    DrawLineEx(e.pos, e.pos + e.aim * 900, 2 + k * 10, alpha(col::red, 0.15f + 0.35f * k));
                }
                float a = angleOf(e.state == 0 ? look : e.aim);
                Vector2 tip = e.pos + fromAngle(a, r * 1.5f);
                Vector2 l = e.pos + fromAngle(a + 2.4f, r * 1.1f), rr = e.pos + fromAngle(a - 2.4f, r * 1.1f);
                DrawTriangle(tip, rr, l, alpha(col::outline, fade));
                DrawTriangle(tip, l, rr, alpha(col::outline, fade));
                DrawCircleV(e.pos, r + 2, alpha(col::outline, fade));
                DrawCircleV(e.pos, r, e.state == 1 && std::fmod(e.t, 0.12f) < 0.06f ? WHITE : c);
                drawEyes(e.pos, r, look, true);
                break;
            }
            case EType::Spinner: {
                DrawPoly(e.pos, 6, r + 3, e.t * 90, alpha(col::outline, fade));
                DrawPoly(e.pos, 6, r, e.t * 90, c);
                DrawPoly(e.pos, 6, r * 0.5f, -e.t * 120, alpha(WHITE, 0.6f * fade));
                break;
            }
        }
    }

    void drawBullet(const Bullet& b) {
        switch (b.look) {
            case 1: {  // controller
                Rectangle r{b.pos.x - b.r * 1.3f, b.pos.y - b.r * 0.7f, b.r * 2.6f, b.r * 1.4f};
                DrawRectangleRounded(shrink(r, -2), 0.8f, 8, col::outline);
                DrawRectangleRounded(r, 0.8f, 8, b.color);
                DrawCircleV({r.x + b.r * 0.6f, b.pos.y}, 2.5f, WHITE);
                DrawCircleV({r.x + r.width - b.r * 0.6f, b.pos.y}, 2.5f, col::gold);
                break;
            }
            case 2:
                ui::heart({b.pos.x, b.pos.y + 1}, b.r * 2.6f, col::outline);
                ui::heart(b.pos, b.r * 2.1f, b.color);
                break;
            case 3:
                DrawRing(b.pos, b.r * 0.55f, b.r + 2, b.rot * RAD2DEG + 40, b.rot * RAD2DEG + 320, 16, col::outline);
                DrawRing(b.pos, b.r * 0.65f, b.r, b.rot * RAD2DEG + 45, b.rot * RAD2DEG + 315, 16, rgba(0xc9ccd6ff));
                break;
            default:
                DrawCircleV(b.pos, b.r + 2, col::outline);
                DrawCircleV(b.pos, b.r, b.color);
                DrawCircleV(b.pos, b.r * 0.45f, alpha(WHITE, 0.8f));
        }
    }

    void drawPickup(const Pickup& p) {
        float bob = std::sin(p.t * 5) * 3;
        float a = p.life < 2 ? (std::fmod(p.life, 0.25f) < 0.12f ? 0.3f : 1.0f) : 1.0f;
        Vector2 c{p.pos.x, p.pos.y + bob};
        switch (p.kind) {
            case Pickup::Coin: {
                float sx = std::abs(std::sin(p.t * 4));
                DrawEllipse((int)c.x, (int)c.y, 10 * sx + 2, 11, alpha(col::outline, a));
                DrawEllipse((int)c.x, (int)c.y, 9 * sx + 1, 10, alpha(col::gold, a));
                break;
            }
            case Pickup::BigCoin: ui::coin(c, 15); break;
            case Pickup::Boba:
                DrawCircleGradient(c, 46, alpha(col::boba, 0.4f), alpha(col::boba, 0));
                art::boba(c, 46);
                break;
            case Pickup::Power:
                DrawCircleGradient(c, 40, alpha(col::violet, 0.5f), alpha(col::violet, 0));
                DrawCircleV(c, 24, alpha(col::bgDeep, 0.8f * a));
                DrawRing(c, 22, 25, p.t * 200, p.t * 200 + 270, 20, col::gold);
                art::itemIcon(p.power, c, 34);
                break;
        }
    }

    void drawBoss() {
        Boss& b = boss_;
        if (b.kind == BossKind::None || (b.hp <= 0)) return;
        if (b.kind == BossKind::Horse && b.state == 1) {
            float k = 1 - b.attackT / 0.9f;
            DrawLineEx(b.pos, b.pos + b.dir * 1400, b.r * 2 * (0.3f + 0.7f * k), alpha(col::red, 0.12f + 0.2f * k));
        }
        Color ring = b.flash > 0 ? WHITE : (b.enraged() ? col::red : col::gold);
        float wob = std::sin(b.t * 6) * 4;
        if (b.enraged()) DrawCircleGradient(b.pos, b.r * 1.6f, alpha(col::red, 0.35f), alpha(col::red, 0));
        const char* who = b.kind == BossKind::Davi ? "davi" : b.kind == BossKind::Kira ? "kira" : "horse";
        art::portrait(who, b.pos, b.r, ring, wob, b.vel.x > 0 && b.kind == BossKind::Horse, b.flash > 0 ? mix(WHITE, col::red, b.flash) : WHITE);
        if (b.talkT > 0) {
            float w = ui::measure(b.talk, 22) + 28;
            Rectangle r{b.pos.x - w / 2, b.pos.y - b.r - 58, w, 38};
            DrawRectangleRounded(r, 0.5f, 10, WHITE);
            DrawTriangle({b.pos.x - 8, r.y + r.height}, {b.pos.x, r.y + r.height + 12}, {b.pos.x + 8, r.y + r.height}, WHITE);
            ui::text(b.talk, b.pos.x, r.y + 19, 22, col::outline, ui::Center);
        }
    }

    void drawPlayer() {
        for (auto& g : ghosts_) art::pieter(g.first, kPlayerR, P().hat, tilt_, alpha(col::violet, g.second * 2));
        bool blink = invuln_ > 0 && std::fmod(invuln_, 0.16f) < 0.08f;
        if (phase_ == Phase::Dead) return;
        if (!blink) art::pieter(player_, kPlayerR, P().hat, tilt_);
        if (shield_) {
            DrawCircleV(player_, kPlayerR + 10, alpha(col::cyan, 0.15f));
            DrawRing(player_, kPlayerR + 7, kPlayerR + 11, 0, 360, 36, alpha(col::cyan, 0.6f + 0.3f * std::sin(t_ * 8)));
        }
        if (speedT_ > 0)
            for (int i = 0; i < 3; i++) {
                Vector2 o{-pvel_.x * 0.06f, -pvel_.y * 0.06f};
                DrawLineEx(player_ + o + Vector2{0, (i - 1) * 12.0f}, player_ + o * 2.5f + Vector2{0, (i - 1) * 12.0f}, 3, alpha(col::gold, 0.6f));
            }
        if (magnetT_ > 0) DrawRing(player_, 70, 72, t_ * 100, t_ * 100 + 300, 30, alpha(col::red, 0.4f));
        // dash cooldown ring
        if (dashCd_ > 0) DrawRing(player_, kPlayerR + 4, kPlayerR + 7, -90, -90 + 360 * (1 - dashCd_ / 1.0f), 30, alpha(WHITE, 0.6f));
    }

    void drawHud() {
        DrawRectangleGradientV(0, 0, VW, 84, alpha(col::bgDeep, 0.95f), alpha(col::bgDeep, 0.6f));
        for (int i = 0; i < std::max(hearts_, 3); i++) {
            Vector2 p{40 + i * 40.0f, 42};
            ui::heart({p.x, p.y + 2}, 34, col::outline);
            ui::heart(p, 28, i < hearts_ ? col::red : alpha(col::muted, 0.3f));
        }
        float ex = 40 + std::max(hearts_, 3) * 40.0f + 10;
        auto effect = [&](const char* id, float t, float maxT) {
            if (t <= 0) return;
            art::itemIcon(id, {ex, 42}, 30);
            DrawRing({ex, 42}, 19, 22, -90, -90 + 360 * t / maxT, 24, col::gold);
            ex += 46;
        };
        effect("speed", speedT_, 6);
        effect("slowmo", slowT_, 5);
        effect("magnet", magnetT_, 10);

        bool bossWave = boss_.kind != BossKind::None;
        std::string w = wave_ > kFinalWave ? "ENDLESS WAVE " + std::to_string(wave_) : "WAVE " + std::to_string(wave_) + " / " + std::to_string(kFinalWave);
        ui::textShadow(w, VW / 2.0f, 28, 30, col::text, ui::Center, true, 2);
        Rectangle tb{VW / 2.0f - 200, 52, 400, 14};
        if (bossWave && boss_.kind != BossKind::None) {
            ui::bar(tb, (float)std::max(0, boss_.hp) / boss_.maxHp, boss_.enraged() ? col::red : col::orange);
            ui::text(boss_.name, tb.x - 10, tb.y + 7, 18, col::orange, ui::Right, true);
        } else {
            ui::bar(tb, phase_ == Phase::Playing ? 1 - waveT_ / waveLen_ : 1, col::violet);
        }

        ui::coinCounter(VW - 20, 42, P().coins);
        ui::text("+" + fmtNum(earned_) + " this run", VW - 26, 74, 16, col::gold, ui::Right);

        // powerup slots
        for (int i = 0; i < 3; i++) {
            Rectangle r{VW / 2.0f - 120 + i * 82, VH - 74, 72, 64};
            const std::string& id = P().loadout[i];
            int n = id.empty() ? 0 : P().count(id);
            DrawRectangleRounded(r, 0.25f, 8, alpha(col::bgDeep, 0.85f));
            DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, n > 0 ? col::violet : alpha(col::muted, 0.4f));
            if (!id.empty()) art::itemIcon(id, {r.x + r.width / 2, r.y + 30}, 38);
            if (n == 0) DrawRectangleRounded(r, 0.25f, 8, alpha(col::bgDeep, 0.6f));
            ui::text(std::to_string(i + 1), r.x + 8, r.y + 12, 16, col::gold, ui::Left, true);
            ui::text("x" + std::to_string(n), r.x + r.width - 6, r.y + r.height - 10, 15, col::text, ui::Right);
        }
        ui::text("WASD move   SPACE dash   1-3 powerups   ESC pause", 30, VH - 14, 14, alpha(col::text, 0.4f));
    }

    void drawBanner(const std::string& big, const std::string& small, Color c, float t) {
        float in = easeOutBack(t / 0.4f);
        float a = clamp01(t / 0.2f);
        DrawRectangle(0, (int)(VH / 2.0f - 70), VW, 140, alpha(col::bgDeep, 0.7f * a));
        ui::textOutline(big, VW / 2.0f + (1 - in) * -400, VH / 2.0f - 12, 76, alpha(c, a), alpha(col::outline, a), 5);
        ui::text(small, VW / 2.0f, VH / 2.0f + 44, 24, alpha(col::text, a), ui::Center);
    }

    void draw() override {
        art::purpleBackdrop(t_);
        DrawRectangleRounded(shrink(kArena, -6), 0.03f, 8, col::outline);
        DrawRectangleRounded(kArena, 0.03f, 8, alpha(col::bgDeep, 0.75f));
        DrawRectangleRoundedLinesEx(shrink(kArena, -2), 0.03f, 8, 2, alpha(slowT_ > 0 ? col::cyan : col::violet, 0.5f));
        for (float x = kArena.x + 40; x < kArena.x + kArena.width; x += 40) DrawLineEx({x, kArena.y + 4}, {x, kArena.y + kArena.height - 4}, 1, alpha(col::violet, 0.07f));
        for (float y = kArena.y + 40; y < kArena.y + kArena.height; y += 40) DrawLineEx({kArena.x + 4, y}, {kArena.x + kArena.width - 4, y}, 1, alpha(col::violet, 0.07f));
        DrawCircleGradient(center(kArena), 520, alpha(col::purple, 0.10f), alpha(col::purple, 0));
        if (slowT_ > 0) DrawRectangleRec(kArena, alpha(col::cyan, 0.05f));

        for (auto& w : warnings_) {
            float k = w.t / 0.85f;
            float r = 24 * (1.2f - k * 0.4f);
            Color c = alpha(enemyColor(w.type), 0.3f + 0.5f * (std::fmod(w.t, 0.2f) < 0.1f));
            DrawRing(w.pos, r - 3, r, 0, 360 * k, 30, c);
            DrawLineEx(w.pos - Vector2{8, 8}, w.pos + Vector2{8, 8}, 3, c);
            DrawLineEx(w.pos - Vector2{8, -8}, w.pos + Vector2{8, -8}, 3, c);
        }
        for (auto& p : pickups_) drawPickup(p);
        for (auto& e : enemies_) drawEnemy(e);
        drawBoss();
        drawPlayer();
        for (auto& h : homing_) art::boba(h.pos, 26);
        for (auto& b : bullets_) drawBullet(b);
        fx_.draw();
        drawHud();

        switch (phase_) {
            case Phase::Intro:
                if (boss_.kind != BossKind::None) {
                    bool final = boss_.kind == BossKind::Horse;
                    drawBanner(boss_.name, (final ? "FINAL BOSS - " : "MINIBOSS - ") + boss_.title, final ? col::orange : col::pink, phaseT_);
                    ui::text("collect BOBA to fight back!", VW / 2.0f, VH / 2.0f + 100, 22, col::boba, ui::Center);
                } else {
                    drawBanner("WAVE " + std::to_string(wave_), wave_ == 1 ? "dodge everything. you got this pieter" : "survive!", col::gold, phaseT_);
                }
                break;
            case Phase::Cleared:
                drawBanner("WAVE CLEARED!", "+" + std::to_string(15 * wave_) + " bonus coins", col::green, phaseT_);
                break;
            case Phase::Dead: drawEndScreen(false); break;
            case Phase::Victory: drawEndScreen(true); break;
            default: break;
        }
        if (paused_) drawPause();
    }

    void drawEndScreen(bool won) {
        float a = clamp01(phaseT_ / 0.6f);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f * a));
        if (phaseT_ < 0.5f) return;
        Rectangle r{VW / 2.0f - 300, VH / 2.0f - 210, 600, 420};
        ui::panel(r);
        art::pieter({r.x + r.width / 2, r.y + 10}, 60, P().hat, won ? std::sin(t_ * 4) * 10 : 180 * easeOutCubic(phaseT_ - 0.5f));
        ui::textOutline(won ? "PIETER WINS!" : "PIETER GOT REKT", r.x + r.width / 2, r.y + 110, 52, won ? col::gold : col::red, col::outline, 4);
        ui::text(won ? "The Horse has been defeated. JUHUH no more." : "Reached wave " + std::to_string(wave_), r.x + r.width / 2, r.y + 160, 22, col::muted, ui::Center);
        ui::coin({r.x + r.width / 2 - 90, r.y + 210}, 18);
        ui::text("+" + fmtNum(earned_) + " earned", r.x + r.width / 2 - 64, r.y + 210, 30, col::gold);
        ui::text("best wave: " + std::to_string(P().stats.bestWave), r.x + r.width / 2, r.y + 252, 20, col::text, ui::Center);
        float by = r.y + r.height - 90;
        if (won) {
            if (ui::button({r.x + 30, by, 170, 56}, "ENDLESS", col::violet)) { phase_ = Phase::Cleared; phaseT_ = 2.0f; }
            if (ui::button({r.x + 215, by, 170, 56}, "AGAIN", col::purple)) E().go(makeClassic);
            if (ui::button({r.x + 400, by, 170, 56}, "MENU", col::panelHi)) E().go(makeMenu);
        } else {
            if (ui::button({r.x + 60, by, 220, 56}, "RETRY", col::purple)) E().go(makeClassic);
            if (ui::button({r.x + 320, by, 220, 56}, "MENU", col::panelHi)) E().go(makeMenu);
        }
        if (IsKeyPressed(KEY_ENTER)) E().go(makeClassic);
    }

    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.55f));
        Rectangle r{VW / 2.0f - 200, VH / 2.0f - 130, 400, 260};
        ui::panel(r);
        ui::text("PAUSED", r.x + r.width / 2, r.y + 50, 44, col::text, ui::Center, true);
        if (ui::button({r.x + 50, r.y + 100, 300, 56}, "RESUME", col::purple)) paused_ = false;
        if (ui::button({r.x + 50, r.y + 170, 300, 56}, "QUIT TO MENU", col::red)) E().go(makeMenu);
    }

    static constexpr float kPlayerR = 24;

    Phase phase_ = Phase::Intro;
    float t_ = 0, phaseT_ = 0, waveT_ = 0, waveLen_ = 25;
    float spawnT_ = 0, coinT_ = 0, bobaT_ = 0, powerT_ = 0, trailT_ = 0;
    int wave_ = 1;
    bool paused_ = false;
    long long earned_ = 0;

    Vector2 player_{}, pvel_{}, dashDir_{1, 0};
    float tilt_ = 0, invuln_ = 0, dashT_ = 0, dashCd_ = 0;
    int hearts_ = 3;
    bool shield_ = false;
    float speedT_ = 0, slowT_ = 0, magnetT_ = 0;
    std::vector<std::pair<Vector2, float>> ghosts_;

    std::vector<Enemy> enemies_;
    std::vector<Bullet> bullets_;
    std::vector<Pickup> pickups_;
    std::vector<Warning> warnings_;
    std::vector<Homing> homing_;
    Boss boss_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeClassic() { return std::make_unique<ClassicScene>(); }

}  // namespace pg
