// CLASSIC: the mode that has been in every PieterGame. Dodge enemies for 10
// waves, minibosses at wave 5 and 8, THE HORSE at wave 10. You cannot attack
// directly; during boss fights you collect boba which auto-fires at the boss.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Admin.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <cstdlib>

#include <rlgl.h>

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
    float r = 18, t = 0, life = 14, spawn = 0, timer = 0, blink = 0;
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
        practice_ = Admin().practicePending;
        Admin().practicePending = false;
        if (!practice_) {
            P().stats.classicRuns++;
            P().save();
        } else {
            hearts_ = Admin().hearts;
        }
        player_ = {kArena.x + kArena.width / 2, kArena.y + kArena.height / 2};
        god_ = std::getenv("PG_GOD") != nullptr;  // dev hook
        int first = 1;
        if (const char* w = std::getenv("PG_WAVE")) first = std::max(1, std::atoi(w));  // dev hook
        if (practice_) first = Admin().startWave;
        startWave(first);
    }

    void exit() override {
        if (practice_) return;  // practice runs don't count
        P().stats.bestWave = std::max(P().stats.bestWave, wave_ - (phase_ == Phase::Dead ? 1 : 0));
        P().save();
    }

    // ================================================================ update
    void update(float dt) override {
        t_ += dt;
        totalT_ += dt;
        if (E().backPressed()) {
            if (phase_ == Phase::Dead || phase_ == Phase::Victory) toMenu();
            else paused_ = !paused_;
        }
        if (paused_) return;
        dt *= Admin().speed;  // practice speed (1 normally)

        // presentation state keeps animating even during hit-stop
        worldZoom_ = damp(worldZoom_, 1.0f, 6, dt);
        heartAnim_ = std::max(0.0f, heartAnim_ - dt * 2.5f);
        hurtGlow_ = std::max(0.0f, hurtGlow_ - dt * 1.6f);
        if (boss_.kind != BossKind::None) bossChip_ = damp(bossChip_, (float)std::max(0, boss_.hp), 3, dt);
        if (hitStop_ > 0) { hitStop_ -= dt; return; }  // freeze-frame on big impacts

        fx_.update(dt);
        shocks_.update(dt);
        updateMotes(dt);
        phaseT_ += dt;

        switch (phase_) {
            case Phase::Intro:
                updatePlayer(dt);
                if (phaseT_ > (boss_.kind != BossKind::None ? 2.6f : 1.8f)) { phase_ = Phase::Playing; phaseT_ = 0; }
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
        waveLen_ = 18 + std::min(w, 12) * 1.5f;
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
        if (practice_ && Admin().enragedBoss) b.hp = b.maxHp / 2;  // practice the second phase
        bossChip_ = (float)b.hp;
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
        float diff = 1 + (wave_ - 1) * 0.045f;

        // spawn regular enemies (fewer during boss fights)
        spawnT_ -= dt;
        int maxEnemies = bossWave ? 1 + wave_ / 5 : 3 + wave_;
        float interval = std::max(0.75f, 2.0f - wave_ * 0.11f) * (bossWave ? 2.6f : 1.0f);
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
        if (wave_ >= 2) pool.push_back({EType::Shooter, 1.2f});
        if (wave_ >= 4) pool.push_back({EType::Charger, 1.1f});
        if (wave_ >= 7) pool.push_back({EType::Spinner, 0.8f});
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
        e.life = rnd(8, 12);
        switch (t) {
            case EType::Chaser: e.r = 18; break;
            case EType::Bouncer: e.r = 15; e.vel = rndDir() * 210 * diff; break;
            case EType::Shooter: e.r = 21; e.timer = rnd(1.0f, 2.0f); e.vel = rndDir() * 40; break;
            case EType::Charger: e.r = 19; e.timer = rnd(0.8f, 1.6f); break;
            case EType::Spinner: e.r = 20; e.timer = 2.0f; e.vel = rndDir() * 60; break;
        }
        enemies_.push_back(e);
        E().sfx.playAt(Sfx::Spawn, e.pos.x, 0.35f);
        shocks_.add(at, 4, 46, enemyColor(t), 0.35f, 6);
        fx_.burst(at, 10, lighten(enemyColor(t), 0.3f), 220, 4, 0.4f, PShape::Spark);
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
            popEnemy(e);
            return;
        }
        Vector2 toP = Vector2Normalize(player_ - e.pos);
        switch (e.type) {
            case EType::Chaser: {
                float sp = 120 * diff;
                e.vel = damp(e.vel, toP * sp, 2.2f, dt);
                break;
            }
            case EType::Bouncer: break;
            case EType::Shooter:
                e.timer -= dt;
                e.vel = damp(e.vel, toP * -30 + fromAngle(e.t) * 40, 1, dt);
                if (e.timer <= 0) {
                    e.timer = std::max(1.8f, 3.0f - wave_ * 0.1f);
                    fire(e.pos, toP, 215 * diff, col::green);
                    if (wave_ >= 9) {
                        fire(e.pos, Vector2Rotate(toP, 0.3f), 215 * diff, col::green);
                        fire(e.pos, Vector2Rotate(toP, -0.3f), 215 * diff, col::green);
                    }
                    E().sfx.playAt(Sfx::Shoot, e.pos.x, 0.4f);
                }
                break;
            case EType::Charger:
                e.timer -= dt;
                if (e.state == 0) {  // drifting
                    e.vel = damp(e.vel, toP * 50, 2, dt);
                    if (e.timer <= 0) { e.state = 1; e.timer = 0.95f; e.aim = toP; }
                } else if (e.state == 1) {  // telegraph
                    e.vel = damp(e.vel, {0, 0}, 8, dt);
                    e.aim = Vector2Normalize(damp(e.aim, toP, 1.5f, dt));
                    if (e.timer <= 0) {
                        e.state = 2;
                        e.timer = 0.55f;
                        e.vel = e.aim * 600 * diff;
                        E().sfx.playAt(Sfx::Dash, e.pos.x, 0.4f);
                        shocks_.add(e.pos, 10, 50, col::pink, 0.3f, 6);
                    }
                } else {  // dashing
                    if (e.timer <= 0) { e.state = 0; e.timer = rnd(1.2f, 2.0f); }
                }
                break;
            case EType::Spinner:
                e.timer -= dt;
                if (e.timer <= 0) {
                    e.timer = 3.4f;
                    ring(e.pos, 6, 150 * diff, col::cyan, 0, e.t);
                    shocks_.add(e.pos, e.r, e.r + 40, col::cyan, 0.35f, 6);
                    E().sfx.playAt(Sfx::Shoot, e.pos.x, 0.35f, 0.7f);
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
                b.vel = damp(b.vel, toP * 70 * rage, 1.2f, dt);
                if (b.attackT <= 0) {
                    b.pattern++;
                    if (b.enraged() && b.pattern % 3 == 0) {
                        ring(b.pos, 12, 190, col::violet, 1, b.t);
                        shocks_.add(b.pos, b.r, b.r + 160, col::violet, 0.6f, 14);
                        say(chance(0.5f) ? "RAGE QUIT MODE" : "my controller is broken!!");
                        E().shake(6);
                    } else {
                        for (int i = -1; i <= 1; i++) fire(b.pos, Vector2Rotate(toP, i * 0.28f), 250 * rage, col::violet, 1, 11);
                        if (chance(0.3f)) say(chance(0.5f) ? "PRO SINCE 2010" : "lag!! that was lag");
                    }
                    b.attackT = 2.3f / rage;
                    E().sfx.playAt(Sfx::Shoot, b.pos.x, 0.6f, 0.8f);
                }
                break;
            }
            case BossKind::Kira: {
                Vector2 c = center(kArena);
                Vector2 target{c.x + std::sin(b.t * 0.7f) * 380, c.y + std::sin(b.t * 1.1f) * 170};
                b.vel = damp(b.vel, (target - b.pos) * 2.0f, 2, dt);
                b.spiral += dt;
                float rate = b.enraged() ? 0.13f : 0.19f;
                while (b.spiral > rate) {
                    b.spiral -= rate;
                    float a = b.t * 2.2f;
                    fire(b.pos, fromAngle(a), 175, col::pink, 2, 10);
                    if (b.enraged()) fire(b.pos, fromAngle(a + PI), 175, col::pink, 2, 10);
                }
                if (b.attackT <= 0) {
                    b.attackT = 6.0f;
                    say(chance(0.5f) ? "*wink*" : "Brother sister rivals FOREVER");
                    queueEnemy(EType::Chaser);
                    E().sfx.play(Sfx::Kiss);
                }
                break;
            }
            case BossKind::Horse: {
                // state 0 roam, 1 charge telegraph, 2 charging, 3 rain
                if (b.state == 0) {
                    b.vel = damp(b.vel, toP * 60 * rage, 1.5f, dt);
                    if (b.attackT <= 0) {
                        b.pattern++;
                        if (b.pattern % 3 == 2) {
                            b.state = 3;
                            b.attackT = 3.0f;
                            say("HORSESHOE RAIN!");
                        } else {
                            b.state = 1;
                            b.attackT = 1.15f / rage;
                            b.dir = toP;
                            E().sfx.playAt(Sfx::Neigh, b.pos.x, 0.8f);
                            say("JUHUH!");
                        }
                    }
                } else if (b.state == 1) {
                    b.vel = damp(b.vel, {0, 0}, 10, dt);
                    b.dir = Vector2Normalize(damp(b.dir, toP, 2.5f, dt));
                    if (b.attackT <= 0) { b.state = 2; b.vel = b.dir * 720 * rage; E().sfx.play(Sfx::Dash); }
                } else if (b.state == 2) {
                    Vector2 before = b.vel;
                    b.pos += b.vel * dt;
                    bounceInArena(b.pos, b.vel, b.r);
                    if (before.x != b.vel.x || before.y != b.vel.y) {
                        E().shake(12);
                        E().sfx.playAt(Sfx::Boom, b.pos.x, 0.7f);
                        ring(b.pos, b.enraged() ? 12 : 8, 200, col::orange, 3, rnd(0, 6.28f));
                        fx_.burst(b.pos, 30, col::orange, 400, 8, 0.6f);
                        fx_.burst(b.pos, 20, rgba(0xc8a070ff), 250, 10, 0.9f, PShape::Circle, 200);
                        shocks_.add(b.pos, b.r, b.r + 220, col::orange, 0.6f, 18);
                        E().aberrate(0.006f);
                        hitStop_ = 0.05f;
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
                    float rate = b.enraged() ? 0.16f : 0.24f;
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
            shocks_.add(player_, 10, 60, col::violet, 0.3f, 6);
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
            for (auto& e : enemies_) popEnemy(e);
            shocks_.add(player_, 20, 900, col::green, 0.8f, 30);
            E().aberrate(0.012f);
            enemies_.clear();
            bullets_.clear();
            warnings_.clear();
        }
    }

    // returns true if the player actually took the hit (or a shield ate it)
    bool hitPlayer(Vector2 from) {
        if (invuln_ > 0 || dashT_ > 0 || phase_ != Phase::Playing || god_ || Admin().god) return false;
        if (shield_) {
            shield_ = false;
            invuln_ = 1.0f;
            E().sfx.play(Sfx::Bubble);
            fx_.burst(player_, 30, col::cyan, 350, 6, 0.5f, PShape::Bubble);
            fx_.floatText("BLOCKED", player_ - Vector2{0, 40}, col::cyan, 24);
            shocks_.add(player_, kPlayerR, kPlayerR + 90, col::cyan, 0.4f, 10);
            hitStop_ = 0.05f;
            return true;
        }
        hearts_--;
        invuln_ = 2.0f;
        pvel_ = Vector2Normalize(player_ - from) * 500;
        E().sfx.play(Sfx::Hurt);
        E().shake(9);
        E().flash(col::red, 0.25f);
        E().aberrate(0.014f);
        fx_.burst(player_, 20, col::red, 300, 6, 0.5f);
        fx_.burst(player_, 12, WHITE, 420, 4, 0.3f, PShape::Spark);
        shocks_.add(player_, kPlayerR, kPlayerR + 110, col::red, 0.45f, 12);
        hitStop_ = 0.09f;
        heartAnim_ = 1;
        hurtGlow_ = 1;
        if (hearts_ <= 0) die();
        return true;
    }

    void die() {
        phase_ = Phase::Dead;
        phaseT_ = 0;
        E().sfx.play(Sfx::Lose);
        fx_.burst(player_, 60, col::gold, 500, 8, 1.0f, PShape::Star);
        if (!practice_) P().stats.bestWave = std::max(P().stats.bestWave, wave_ - 1);
        P().save();
    }

    // enemy pops into a coin with a little explosion
    void popEnemy(const Enemy& e) {
        Color c = enemyColor(e.type);
        fx_.burst(e.pos, 14, c, 260, 6, 0.5f);
        fx_.burst(e.pos, 8, WHITE, 380, 3, 0.25f, PShape::Spark);
        shocks_.add(e.pos, e.r * 0.5f, e.r + 50, c, 0.35f, 8);
        pickups_.push_back({Pickup::Coin, e.pos});
    }

    void updateMotes(float dt) {
        motes_.update(dt);
        moteT_ -= dt;
        if (moteT_ > 0) return;
        moteT_ = 0.12f;
        Particle p;
        p.pos = {rnd(kArena.x, kArena.x + kArena.width), rnd(kArena.y, kArena.y + kArena.height)};
        p.vel = {rnd(-8, 8), rnd(-18, -6)};
        p.life = p.maxLife = rnd(3, 6);
        p.size = rnd(1, 2.4f);
        p.endSize = p.size;
        p.color = alpha(chance(0.5f) ? col::violet : col::pink, rnd(0.3f, 0.6f));
        motes_.add(p);
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
                    case Pickup::Coin:
                        addCoins(2, p.pos);
                        E().sfx.playAt(Sfx::Coin, p.pos.x, 0.45f, rnd(0.95f, 1.1f));
                        fx_.burst(p.pos, 5, col::gold, 160, 3, 0.3f, PShape::Spark);
                        break;
                    case Pickup::BigCoin:
                        addCoins(10, p.pos);
                        E().sfx.playAt(Sfx::Coin, p.pos.x, 0.7f, 0.8f);
                        fx_.burst(p.pos, 10, col::gold, 220, 4, 0.4f, PShape::Star);
                        shocks_.add(p.pos, 8, 40, col::gold, 0.3f, 5);
                        break;
                    case Pickup::Power: applyPower(p.power); break;
                    case Pickup::Boba:
                        E().sfx.playAt(Sfx::Gem, p.pos.x);
                        homing_.push_back({player_, rndDir() * 300});
                        fx_.floatText("BOBA!", p.pos, col::boba, 24);
                        shocks_.add(p.pos, 10, 70, col::boba, 0.4f, 8);
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
        E().sfx.playAt(Sfx::Boom, at.x, 0.5f, 1.5f);
        E().shake(5);
        fx_.burst(at, 18, col::boba, 300, 7, 0.5f);
        fx_.burst(at, 10, WHITE, 450, 4, 0.3f, PShape::Spark);
        shocks_.add(at, 10, 90, col::gold, 0.35f, 8);
        E().aberrate(0.005f);
        hitStop_ = 0.04f;
        static const char* ouch[] = {"OW!", "hey!!", "not fair", "rigged", "lag!", "bruh"};
        if (chance(0.4f)) say(ouch[rndi(0, 5)]);
        if (b.hp <= 0) {
            E().sfx.play(Sfx::Boom);
            E().sfx.play(Sfx::Win);
            E().shake(18);
            E().flash(WHITE, 0.7f);
            E().aberrate(0.03f);
            hitStop_ = 0.35f;
            worldZoom_ = 1.08f;
            shocks_.add(b.pos, b.r, 700, WHITE, 0.9f, 30);
            shocks_.add(b.pos, b.r, 450, col::gold, 0.7f, 20);
            fx_.burst(b.pos, 80, col::gold, 600, 10, 1.2f, PShape::Star);
            fx_.confetti(b.pos, 80);
            int bonus = b.kind == BossKind::Horse ? 500 : 200;
            addCoins(bonus, b.pos);
            for (int i = 0; i < 12; i++) pickups_.push_back({Pickup::BigCoin, b.pos + rndDir() * rnd(20, 120)});
            bullets_.clear();
        }
    }

    void clearWave() {
        for (auto& e : enemies_) popEnemy(e);
        for (auto& b : bullets_) fx_.burst(b.pos, 3, b.color, 120, 3, 0.3f);
        enemies_.clear();
        bullets_.clear();
        warnings_.clear();
        homing_.clear();
        int bonus = 15 * wave_;
        addCoins(bonus, {VW / 2.0f, VH / 2.0f + 60});
        if (!practice_) P().stats.bestWave = std::max(P().stats.bestWave, wave_);
        if (wave_ == kFinalWave) {
            phase_ = Phase::Victory;
            phaseT_ = 0;
            if (!practice_) P().stats.classicWins++;
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

    static Color bossColor(BossKind k) {
        return k == BossKind::Davi ? col::cyan : k == BossKind::Kira ? col::pink : col::orange;
    }

    static void ellipse(Vector2 c, float rx, float ry, Color color) {
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        rlScalef(1, ry / std::max(0.01f, rx), 1);
        DrawCircleV({0, 0}, rx, color);
        rlPopMatrix();
    }

    // cartoon eyes with pupils tracking the player, blinking and angry brows
    void eyes(Vector2 c, float r, Vector2 look, bool angry, float blinkPhase, float spacing = 0.36f, float size = 0.27f) {
        bool blink = std::fmod(blinkPhase, 3.4f) < 0.13f;
        for (int s = -1; s <= 1; s += 2) {
            Vector2 e{c.x + s * r * spacing, c.y - r * 0.1f};
            float w = r * size, h = blink ? r * 0.05f : r * size * 1.15f;
            ellipse(e, w + 2, h + 2, col::outline);
            ellipse(e, w, h, WHITE);
            if (!blink) {
                Vector2 pp = e + look * r * 0.1f;
                DrawCircleV(pp, r * 0.13f, col::outline);
                DrawCircleV(pp - Vector2{r * 0.04f, r * 0.05f}, r * 0.045f, WHITE);
            }
            if (angry) {
                Vector2 a{e.x + s * w * 1.25f, e.y - h - r * 0.16f}, b{e.x - s * w * 0.9f, e.y - h + r * 0.02f};
                DrawLineEx(a, b, r * 0.14f, col::outline);
            }
        }
    }

    void drawWarning(const Warning& w) {
        float k = w.t / 0.85f;
        Color c = enemyColor(w.type);
        float r = 30 * (1.25f - k * 0.35f);
        fx::dashedRing(w.pos, r, 3, 10, t_ * 180, alpha(c, 0.4f + 0.5f * k));
        DrawRing(w.pos, r * 0.55f - 2, r * 0.55f, 0, 360 * k, 24, alpha(c, 0.8f));
        DrawCircleV(w.pos, r * 0.45f * easeInCubic(k), alpha(c, 0.35f));
        // rising beam
        fx::beginAdd();
        fx::glow(w.pos, r * 2.2f, alpha(c, 0.25f + 0.35f * k));
        fx::glowLine(w.pos, w.pos - Vector2{0, 70 * k}, 10, alpha(c, 0.4f * k));
        fx::endAdd();
    }

    void drawEnemy(const Enemy& e) {
        float sp = easeOutBack(e.spawn);
        float fade = clamp01(e.life / 0.6f);
        if (e.life < 1.5f && std::fmod(e.life, 0.2f) < 0.07f) fade *= 0.4f;  // about to pop
        float r = e.r * sp;
        Color c = enemyColor(e.type);
        Vector2 look = Vector2Normalize(player_ - e.pos);
        float speed = Vector2Length(e.vel);
        float bob = std::sin(e.t * 6 + e.r) * 1.5f;
        Vector2 p{e.pos.x, e.pos.y + bob};
        // squash & stretch along the movement direction
        float stretch = std::min(0.25f, speed / 2800.0f);
        rlPushMatrix();
        rlTranslatef(p.x, p.y, 0);
        rlRotatef(angleOf(e.vel) * RAD2DEG, 0, 0, 1);
        rlScalef(1 + stretch, 1 - stretch * 0.7f, 1);
        rlRotatef(-angleOf(e.vel) * RAD2DEG, 0, 0, 1);
        Vector2 o{0, 0};

        switch (e.type) {
            case EType::Chaser: {
                // "Useless Elektronik": a furious little router
                for (int s = -1; s <= 1; s += 2) {
                    float wob = std::sin(e.t * 9 + s) * 0.25f;
                    Vector2 base{s * r * 0.5f, -r * 0.75f}, tip = base + fromAngle(-PI / 2 + s * 0.35f + wob, r * 0.75f);
                    DrawLineEx(base, tip, 4, col::outline);
                    DrawLineEx(base, tip, 2, rgba(0x55566aff));
                    DrawCircleV(tip, 4, col::outline);
                    DrawCircleV(tip, 3, std::fmod(e.t * 2 + s, 1.0f) < 0.5f ? col::red : darken(col::red, 0.5f));
                }
                Rectangle b{-r * 1.1f, -r * 0.85f, r * 2.2f, r * 1.75f};
                fx::shadedRoundRect(b, 0.35f, alpha(rgba(0x7d8099ff), fade), 3);
                DrawRectangleRounded({b.x + r * 0.15f, b.y + b.height - r * 0.55f, b.width - r * 0.3f, r * 0.38f}, 0.5f, 6, alpha(rgba(0x2a2a38ff), fade));
                for (int i = 0; i < 4; i++) {
                    bool on = std::fmod(e.t * 3 + i * 0.37f, 1.0f) < 0.6f;
                    DrawCircleV({b.x + r * 0.42f + i * r * 0.45f, b.y + b.height - r * 0.36f}, r * 0.09f,
                                on ? (i % 2 ? col::green : col::red) : rgba(0x44445aff));
                }
                eyes({0, -r * 0.12f}, r, look, true, e.t + e.r);
                break;
            }
            case EType::Bouncer: {
                for (int i = 0; i < 9; i++) {
                    float a = e.t * 4 + i * 2 * PI / 9;
                    Vector2 t1 = fromAngle(a - 0.2f, r * 0.9f), t2 = fromAngle(a, r * 1.5f), t3 = fromAngle(a + 0.2f, r * 0.9f);
                    DrawTriangle(t1, t2, t3, col::outline);
                    DrawTriangle(t1, t3, t2, col::outline);
                    Vector2 i1 = fromAngle(a - 0.13f, r * 0.95f), i2 = fromAngle(a, r * 1.36f), i3 = fromAngle(a + 0.13f, r * 0.95f);
                    DrawTriangle(i1, i2, i3, lighten(c, 0.2f));
                    DrawTriangle(i1, i3, i2, lighten(c, 0.2f));
                }
                fx::shadedBall(o, r, alpha(c, fade), 3);
                eyes({0, r * 0.05f}, r, look, true, e.t * 1.3f + e.r, 0.34f, 0.25f);
                break;
            }
            case EType::Shooter: {
                // chat bubble spamming messages
                Rectangle b{-r * 1.25f, -r * 1.0f, r * 2.5f, r * 1.8f};
                DrawTriangle({b.x + r * 0.35f, b.y + b.height - 3}, {b.x + r * 0.2f, b.y + b.height + r * 0.65f}, {b.x + r * 1.05f, b.y + b.height - 3}, col::outline);
                DrawTriangle({b.x + r * 0.45f, b.y + b.height - 4}, {b.x + r * 0.32f, b.y + b.height + r * 0.45f}, {b.x + r * 0.95f, b.y + b.height - 4}, darken(c, 0.2f));
                fx::shadedRoundRect(b, 0.5f, alpha(c, fade), 3);
                eyes({0, -r * 0.25f}, r, look, false, e.t + e.r, 0.38f, 0.24f);
                // typing dots charge up before a shot
                float charge = clamp01(1 - e.timer / 0.9f);
                for (int i = 0; i < 3; i++) {
                    float on = clamp01(charge * 3 - i);
                    DrawCircleV({-r * 0.45f + i * r * 0.45f, r * 0.42f}, r * 0.11f, mix(darken(c, 0.4f), WHITE, on));
                }
                break;
            }
            case EType::Charger: {
                Vector2 dir = e.state == 0 ? look : e.aim;
                float a = angleOf(dir);
                // horns
                for (int s = -1; s <= 1; s += 2) {
                    Vector2 b1 = fromAngle(a + s * 0.75f, r * 0.75f), b2 = fromAngle(a + s * 0.35f, r * 0.85f);
                    Vector2 tip = fromAngle(a + s * 0.55f, r * 1.65f);
                    DrawTriangle(b1, tip, b2, col::outline);
                    DrawTriangle(b1, b2, tip, col::outline);
                    DrawTriangle(Vector2Lerp(b1, tip, 0.1f), Vector2Lerp(tip, b1, 0.12f), Vector2Lerp(b2, tip, 0.1f), rgba(0xf4ead8ff));
                    DrawTriangle(Vector2Lerp(b1, tip, 0.1f), Vector2Lerp(b2, tip, 0.1f), Vector2Lerp(tip, b1, 0.12f), rgba(0xf4ead8ff));
                }
                bool hot = e.state == 1 && std::fmod(e.t, 0.14f) < 0.07f;
                fx::shadedBall(o, r, alpha(hot ? lighten(c, 0.5f) : c, fade), 3);
                eyes(dir * r * 0.15f, r, dir, true, e.t + e.r);
                break;
            }
            case EType::Spinner: {
                float rot = e.t * 70;
                float charge = clamp01(1 - e.timer / 0.8f);
                DrawPoly(o, 6, r * 1.15f + 3, rot, col::outline);
                DrawPoly(o, 6, r * 1.15f, rot, darken(c, 0.35f));
                DrawPoly({-r * 0.06f, -r * 0.08f}, 6, r * 0.98f, rot, alpha(c, fade));
                DrawPoly(o, 6, r * 0.62f, -rot * 1.6f, lighten(c, 0.35f));
                DrawPoly(o, 6, r * 0.32f, rot * 2, mix(WHITE, col::cyan, 1 - charge));
                for (int i = 0; i < 3; i++) DrawCircleV(fromAngle(e.t * 3 + i * 2.09f, r * 1.55f), 3.5f, lighten(c, 0.5f));
                break;
            }
        }
        rlPopMatrix();
    }

    void drawEnemyFx(const Enemy& e) {
        // emissive parts, drawn in the additive pass
        Color c = enemyColor(e.type);
        fx::glow(e.pos, e.r * 3.8f, alpha(c, 0.42f * easeOutCubic(e.spawn)));
        if (e.type == EType::Charger && e.state == 1) {
            float k = 1 - e.timer / 0.95f;
            Vector2 end = e.pos + e.aim * 900;
            fx::glowLine(e.pos, end, 14 + 20 * k, alpha(col::red, 0.08f + 0.2f * k));
        }
        if (e.type == EType::Charger && e.state == 2)
            for (int i = 1; i <= 4; i++) fx::glow(e.pos - e.vel * (0.018f * i), e.r * 1.6f, alpha(col::pink, 0.25f - i * 0.05f));
        if (e.type == EType::Spinner) fx::glow(e.pos, e.r * (1.2f + 1.5f * clamp01(1 - e.timer / 0.8f)), alpha(WHITE, 0.25f * clamp01(1 - e.timer / 0.8f)));
        if (e.type == EType::Shooter && e.timer < 0.3f) fx::glow(e.pos, e.r * 2.5f, alpha(col::green, 0.5f * (1 - e.timer / 0.3f)));
    }

    void drawChargerTelegraph(const Enemy& e) {
        if (e.type != EType::Charger || e.state != 1) return;
        float k = 1 - e.timer / 0.95f;
        float a = angleOf(e.aim);
        // marching chevrons along the charge lane
        for (int i = 0; i < 9; i++) {
            float d = 50 + i * 70 + std::fmod(t_ * 260, 70.0f);
            Vector2 p = e.pos + e.aim * d;
            float al = (0.25f + 0.55f * k) * (1 - i / 10.0f);
            Vector2 l = p + fromAngle(a + 2.5f, 16), rr = p + fromAngle(a - 2.5f, 16);
            DrawLineEx(l, p, 5, alpha(col::red, al));
            DrawLineEx(rr, p, 5, alpha(col::red, al));
        }
    }

    void drawBulletGlow(const Bullet& b) {
        fx::glow(b.pos, b.r * 4.2f, alpha(b.color, 0.75f));
        fx::glowLine(b.pos - b.vel * 0.07f, b.pos, b.r * 1.5f, alpha(b.color, 0.55f));
    }

    void drawBullet(const Bullet& b) {
        switch (b.look) {
            case 1: {  // controller
                rlPushMatrix();
                rlTranslatef(b.pos.x, b.pos.y, 0);
                rlRotatef(std::sin(b.rot) * 25, 0, 0, 1);
                Rectangle r{-b.r * 1.35f, -b.r * 0.75f, b.r * 2.7f, b.r * 1.5f};
                fx::shadedRoundRect(r, 0.9f, b.color, 2);
                DrawCircleV({r.x + b.r * 0.6f, 0}, 2.5f, WHITE);
                DrawCircleV({r.x + r.width - b.r * 0.6f, -1}, 2.5f, col::gold);
                rlPopMatrix();
                break;
            }
            case 2:
                ui::heart({b.pos.x, b.pos.y + 1.5f}, b.r * 2.7f, col::outline);
                ui::heart(b.pos, b.r * 2.2f, b.color);
                DrawCircleV({b.pos.x - b.r * 0.4f, b.pos.y - b.r * 0.35f}, b.r * 0.22f, alpha(WHITE, 0.8f));
                break;
            case 3:
                DrawRing(b.pos, b.r * 0.5f, b.r + 2.5f, b.rot * RAD2DEG + 40, b.rot * RAD2DEG + 320, 16, col::outline);
                DrawRing(b.pos, b.r * 0.62f, b.r, b.rot * RAD2DEG + 45, b.rot * RAD2DEG + 315, 16, rgba(0xd9dce6ff));
                DrawRing(b.pos, b.r * 0.62f, b.r * 0.78f, b.rot * RAD2DEG + 60, b.rot * RAD2DEG + 160, 8, WHITE);
                break;
            default:
                DrawCircleV(b.pos, b.r + 2, col::outline);
                DrawCircleV(b.pos, b.r, b.color);
                DrawCircleV(b.pos, b.r * 0.55f, lighten(b.color, 0.6f));
                DrawCircleV(b.pos - Vector2{b.r * 0.25f, b.r * 0.25f}, b.r * 0.25f, WHITE);
        }
    }

    void drawPickup(const Pickup& p) {
        float bob = std::sin(p.t * 5 + p.pos.x) * 4;
        bool blink = p.life < 2.5f && std::fmod(p.life, 0.25f) < 0.1f;
        if (blink) return;
        Vector2 c{p.pos.x, p.pos.y - 6 + bob};
        fx::softShadow({p.pos.x, p.pos.y + 12}, 14 - bob * 0.5f, 5, 0.45f);
        switch (p.kind) {
            case Pickup::Coin: {
                float sx = std::cos(p.t * 5 + p.pos.y);
                float w = std::max(2.0f, std::abs(sx) * 11);
                ellipse(c, w + 2, 13, col::outline);
                ellipse(c, w, 11, sx > 0 ? col::gold : col::goldDark);
                ellipse({c.x - w * 0.25f, c.y - 2}, w * 0.4f, 6, alpha(WHITE, 0.45f));
                break;
            }
            case Pickup::BigCoin:
                ui::coin(c, 16);
                if (std::fmod(p.t * 1.5f + p.pos.x, 2.0f) < 0.25f) {
                    float k = std::fmod(p.t * 1.5f + p.pos.x, 2.0f) / 0.25f;
                    Vector2 g{c.x + 8, c.y - 8};
                    DrawLineEx({g.x - 9 * k, g.y}, {g.x + 9 * k, g.y}, 2, WHITE);
                    DrawLineEx({g.x, g.y - 9 * k}, {g.x, g.y + 9 * k}, 2, WHITE);
                }
                break;
            case Pickup::Boba:
                fx::dashedRing(c, 40, 3, 12, p.t * 90, alpha(col::boba, 0.8f));
                art::boba(c, 50);
                // pointer arrow above
                DrawTriangle({c.x - 9, c.y - 62 + bob}, {c.x, c.y - 50 + bob}, {c.x + 9, c.y - 62 + bob}, col::gold);
                break;
            case Pickup::Power:
                DrawCircleV(c, 27, col::outline);
                DrawCircleV(c, 25, rgba(0x2a1a50ff));
                DrawCircleGradient({c.x - 6, c.y - 8}, 22, alpha(col::violet, 0.8f), alpha(col::violet, 0));
                fx::dashedRing(c, 31, 3, 8, p.t * 120, col::gold);
                art::itemIcon(p.power, c, 34);
                DrawCircleV({c.x - 10, c.y - 12}, 5, alpha(WHITE, 0.5f));
                break;
        }
    }

    void drawPickupGlow(const Pickup& p) {
        Color c = p.kind == Pickup::Boba ? col::boba : p.kind == Pickup::Power ? col::violet : col::gold;
        float r = p.kind == Pickup::Coin ? 26 : p.kind == Pickup::BigCoin ? 44 : 80;
        float pulse = 0.8f + 0.2f * std::sin(p.t * 6);
        fx::glow(p.pos, r * pulse, alpha(c, p.kind == Pickup::Coin ? 0.3f : 0.45f));
    }

    void drawBoss() {
        Boss& b = boss_;
        if (b.kind == BossKind::None || b.hp <= 0) return;
        Color bc = b.enraged() ? col::red : bossColor(b.kind);
        if (b.kind == BossKind::Horse && b.state == 1) {
            // charge lane
            float k = 1 - b.attackT / 1.15f;
            Vector2 n{-b.dir.y, b.dir.x};
            Vector2 a0 = b.pos + n * b.r, a1 = b.pos - n * b.r, e0 = a0 + b.dir * 1600, e1 = a1 + b.dir * 1600;
            DrawTriangle(a0, e1, e0, alpha(col::red, 0.08f + 0.12f * k));
            DrawTriangle(a0, a1, e1, alpha(col::red, 0.08f + 0.12f * k));
            DrawTriangle(a0, e0, e1, alpha(col::red, 0.08f + 0.12f * k));
            DrawTriangle(a0, e1, a1, alpha(col::red, 0.08f + 0.12f * k));
            DrawLineEx(a0, e0, 3, alpha(col::red, 0.5f + 0.4f * k));
            DrawLineEx(a1, e1, 3, alpha(col::red, 0.5f + 0.4f * k));
            float a = angleOf(b.dir);
            for (int i = 0; i < 8; i++) {
                Vector2 p = b.pos + b.dir * (b.r + 40 + i * 110 + std::fmod(t_ * 400, 110.0f));
                DrawLineEx(p + fromAngle(a + 2.4f, 34), p, 8, alpha(col::red, 0.6f * k));
                DrawLineEx(p + fromAngle(a - 2.4f, 34), p, 8, alpha(col::red, 0.6f * k));
            }
        }
        float wob = std::sin(b.t * 5) * 4;
        float breathe = 1 + std::sin(b.t * 3) * 0.03f;
        fx::dashedRing(b.pos, b.r + 18, 4, 16, b.t * (b.enraged() ? 160 : 60), alpha(bc, 0.8f));
        const char* who = b.kind == BossKind::Davi ? "davi" : b.kind == BossKind::Kira ? "kira" : "horse";
        Color tint = b.flash > 0 ? mix(WHITE, col::red, b.flash) : WHITE;
        rlPushMatrix();
        rlTranslatef(b.pos.x, b.pos.y, 0);
        rlScalef(breathe + b.flash * 0.08f, breathe - b.flash * 0.05f, 1);
        art::portrait(who, {0, 0}, b.r, bc, wob, b.vel.x > 0 && b.kind == BossKind::Horse, tint);
        rlPopMatrix();
        // speech bubble
        if (b.talkT > 0) {
            float pop = easeOutBack(std::min(1.0f, (2.0f - b.talkT) / 0.2f));
            float w = ui::measure(b.talk, 22) + 32;
            Rectangle r{b.pos.x - w / 2 * pop, b.pos.y - b.r - 72, w * pop, 42};
            DrawRectangleRounded({r.x + 3, r.y + 5, r.width, r.height}, 0.5f, 10, alpha(BLACK, 0.35f));
            DrawRectangleRounded(shrink(r, -3), 0.5f, 10, col::outline);
            DrawTriangle({b.pos.x - 10, r.y + r.height}, {b.pos.x, r.y + r.height + 14}, {b.pos.x + 10, r.y + r.height}, col::outline);
            DrawRectangleRounded(r, 0.5f, 10, WHITE);
            DrawTriangle({b.pos.x - 7, r.y + r.height - 1}, {b.pos.x, r.y + r.height + 9}, {b.pos.x + 7, r.y + r.height - 1}, WHITE);
            if (pop > 0.9f) ui::text(b.talk, b.pos.x, r.y + 21, 22, col::outline, ui::Center, true);
        }
    }

    void drawBossGlow() {
        Boss& b = boss_;
        if (b.kind == BossKind::None || b.hp <= 0) return;
        Color bc = b.enraged() ? col::red : bossColor(b.kind);
        float pulse = 0.85f + 0.15f * std::sin(b.t * (b.enraged() ? 10 : 3));
        fx::glow(b.pos, b.r * 4.4f * pulse, alpha(bc, b.enraged() ? 0.7f : 0.5f));
        if (b.flash > 0) fx::glow(b.pos, b.r * 2, alpha(WHITE, 0.6f * b.flash));
    }

    void drawPieterAt(Vector2 c, float sx, float sy, float rot, Color tint) {
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        rlScalef(sx, sy, 1);
        art::pieter({0, 0}, kPlayerR, P().hat, rot, tint);
        rlPopMatrix();
    }

    void drawPlayer() {
        if (phase_ == Phase::Dead) return;
        for (auto& g : ghosts_) drawPieterAt(g.first, 1, 1, tilt_, alpha(lighten(col::violet, 0.3f), g.second * 2.4f));
        bool blink = invuln_ > 0 && std::fmod(invuln_, 0.14f) < 0.07f;
        float speed = Vector2Length(pvel_);
        float st = std::min(0.12f, speed / 4000.0f) + (dashT_ > 0 ? 0.12f : 0);
        float sx = 1 + st, sy = 1 - st * 0.8f;
        if (std::abs(pvel_.y) > std::abs(pvel_.x)) std::swap(sx, sy);
        drawPieterAt(player_, sx, sy, tilt_, blink ? alpha(WHITE, 0.35f) : WHITE);
        if (shield_) {
            float wob = std::sin(t_ * 5) * 2;
            DrawCircleV(player_, kPlayerR + 13 + wob, alpha(col::cyan, 0.12f));
            DrawRing(player_, kPlayerR + 11 + wob, kPlayerR + 14 + wob, 0, 360, 40, alpha(col::cyan, 0.85f));
            fx::dashedRing(player_, kPlayerR + 20, 2, 12, -t_ * 90, alpha(col::cyan, 0.5f));
            DrawRing(player_, kPlayerR + 4, kPlayerR + 8, 200, 250, 10, alpha(WHITE, 0.6f));
        }
        if (magnetT_ > 0) fx::dashedRing(player_, 74, 3, 10, t_ * 140, alpha(col::red, 0.6f));
        if (speedT_ > 0)
            for (int i = 0; i < 4; i++) {
                Vector2 dir = speed > 10 ? pvel_ / speed : Vector2{1, 0};
                Vector2 n{-dir.y, dir.x};
                Vector2 a = player_ - dir * (kPlayerR + 8) + n * ((i - 1.5f) * 11);
                DrawLineEx(a, a - dir * (20 + std::fmod(t_ * 200 + i * 13, 26.0f)), 3, alpha(col::gold, 0.7f));
            }
        // dash cooldown arc
        if (dashCd_ > 0) DrawRing(player_, kPlayerR + 5, kPlayerR + 8, 90 - 180 * (1 - dashCd_), 90 + 180 * (1 - dashCd_), 30, alpha(WHITE, 0.7f));
    }

    void drawPlayerGlow() {
        if (phase_ == Phase::Dead) return;
        fx::glow(player_, 170, alpha(col::violet, 0.45f));
        fx::glow(player_, 60, alpha(col::pink, 0.18f));
        if (shield_) fx::glow(player_, 70, alpha(col::cyan, 0.35f));
        if (dashT_ > 0) fx::glow(player_, 90, alpha(WHITE, 0.3f));
        for (auto& g : ghosts_) fx::glow(g.first, 40, alpha(col::violet, g.second));
    }

    // -------------------------------------------------------------- arena
    Vector2 perimeter(float u) const {
        const float W = kArena.width, H = kArena.height;
        u = std::fmod(u, 2 * (W + H));
        if (u < W) return {kArena.x + u, kArena.y};
        u -= W;
        if (u < H) return {kArena.x + W, kArena.y + u};
        u -= H;
        if (u < W) return {kArena.x + W - u, kArena.y + H};
        u -= W;
        return {kArena.x, kArena.y + H - u};
    }

    Color neon(float u) const {
        if (slowT_ > 0) return col::cyan;
        if (boss_.alive() && boss_.enraged()) return mix(col::red, col::orange, 0.5f + 0.5f * std::sin(u * 0.01f - t_ * 4));
        float k = std::fmod(u * 0.0012f - t_ * 0.25f, 1.0f);
        if (k < 0) k += 1;
        const Color stops[] = {col::violet, col::pink, col::cyan, col::violet};
        float f = k * 3;
        int i = (int)f;
        return mix(stops[i], stops[i + 1], f - i);
    }

    void drawArena() {
        Rectangle a = kArena;
        fx::softShadow({a.x + a.width / 2, a.y + a.height / 2 + 20}, a.width * 0.62f, a.height * 0.7f, 0.7f);
        DrawRectangleRounded(shrink(a, -8), 0.025f, 8, col::outline);
        DrawRectangleRounded(a, 0.02f, 8, rgba(0x120a24f0));
        DrawRectangleGradientV((int)a.x + 6, (int)a.y + 6, (int)a.width - 12, (int)a.height - 12, alpha(rgba(0x241446ff), 0.8f),
                               alpha(rgba(0x0b0618ff), 0.8f));
        // slow nebula light drifting across the floor
        fx::beginAdd();
        for (int i = 0; i < 5; i++) {
            Vector2 c{a.x + a.width * (0.5f + 0.42f * std::sin(t_ * 0.11f + i * 1.7f)), a.y + a.height * (0.5f + 0.38f * std::cos(t_ * 0.09f + i * 2.3f))};
            fx::glow(c, 380 + i * 40, alpha(i % 2 ? col::pink : col::violet, 0.09f));
        }
        fx::endAdd();
        // giant floor logo
        ui::text("PIETERGAME 3", a.x + a.width / 2, a.y + a.height / 2 + 10, 150, alpha(col::violet, 0.05f), ui::Center, true);
        // hex-tile floor: faint everywhere, lit up around the player (and the boss)
        const float R = 30, hw = R * 1.7320508f;
        E().beginClip(shrink(a, 6));
        for (int row = 0; row * R * 1.5f < a.height + R; row++)
            for (int colI = 0; colI * hw < a.width + hw; colI++) {
                Vector2 c{a.x + colI * hw + (row % 2) * hw / 2, a.y + row * R * 1.5f};
                float l = clamp01(1 - Vector2Distance(c, player_) / 230);
                l = l * l;
                Color tint = col::violet;
                if (boss_.alive()) {
                    float lb = clamp01(1 - Vector2Distance(c, boss_.pos) / 300);
                    if (lb * lb > l) { l = lb * lb; tint = boss_.enraged() ? col::red : bossColor(boss_.kind); }
                }
                if (l > 0.02f) DrawPoly(c, 6, R - 3, 30, alpha(tint, 0.10f * l));
                DrawPolyLinesEx(c, 6, R - 1.5f, 30, 1.2f + l * 1.3f, alpha(mix(tint, WHITE, l * 0.4f), 0.07f + 0.5f * l));
            }
        E().endClip();
    }

    void drawArenaBorder() {
        Rectangle a = kArena;
        const float per = 2 * (a.width + a.height), seg = 8;
        for (float u = 0; u < per; u += seg) DrawLineEx(perimeter(u), perimeter(u + seg), 4, neon(u));
        fx::beginAdd();
        for (float u = 0; u < per; u += seg * 3) fx::glowLine(perimeter(u), perimeter(u + seg * 3), 18, alpha(neon(u), 0.4f));
        fx::endAdd();
        // gold corner brackets
        float L = 34;
        Vector2 cs[] = {{a.x, a.y}, {a.x + a.width, a.y}, {a.x + a.width, a.y + a.height}, {a.x, a.y + a.height}};
        Vector2 dx[] = {{1, 0}, {-1, 0}, {-1, 0}, {1, 0}}, dy[] = {{0, 1}, {0, 1}, {0, -1}, {0, -1}};
        for (int i = 0; i < 4; i++) {
            Vector2 c = cs[i] + (dx[i] + dy[i]) * -6;
            DrawLineEx(c, c + dx[i] * L, 6, col::outline);
            DrawLineEx(c, c + dy[i] * L, 6, col::outline);
            DrawLineEx(c, c + dx[i] * (L - 2), 3, col::gold);
            DrawLineEx(c, c + dy[i] * (L - 2), 3, col::gold);
        }
    }

    // -------------------------------------------------------------- HUD
    void drawHeart(Vector2 p, float s, bool full, int i) {
        ui::heart({p.x, p.y + 3}, s * 1.18f, col::outline);
        ui::heart(p, s, full ? col::red : rgba(0x3a2a52ff));
        if (full) {
            ui::heart({p.x, p.y - s * 0.05f}, s * 0.7f, lighten(col::red, 0.15f));
            DrawCircleV({p.x - s * 0.22f, p.y - s * 0.16f}, s * 0.09f, alpha(WHITE, 0.8f));
        }
        (void)i;
    }

    void drawHud() {
        // hearts chip
        int shown = std::max(hearts_, 3);
        Rectangle hc{18, 14, 30.0f + shown * 42, 56};
        DrawRectangleRounded(hc, 0.5f, 12, alpha(col::bgDeep, 0.8f));
        DrawRectangleRoundedLinesEx(hc, 0.5f, 12, 2, alpha(col::violet, 0.5f));
        for (int i = 0; i < shown; i++) {
            bool full = i < hearts_;
            float beat = hearts_ == 1 && full ? 1 + 0.15f * std::max(0.0f, std::sin(t_ * 9)) : 1;
            Vector2 p{42 + i * 42.0f, 42};
            if (i == hearts_ && heartAnim_ > 0) p += rndDir() * heartAnim_ * 4;  // the heart you just lost shakes
            drawHeart(p, 30 * beat, full, i);
        }
        float ex = hc.x + hc.width + 14;
        auto effect = [&](const char* id, float t, float maxT, Color c) {
            if (t <= 0) return;
            Vector2 p{ex + 24, 42};
            DrawCircleV(p, 25, alpha(col::bgDeep, 0.85f));
            DrawRing(p, 21, 25, -90, -90 + 360 * t / maxT, 30, c);
            art::itemIcon(id, p, 28);
            ex += 56;
        };
        effect("speed", speedT_, 6, col::gold);
        effect("slowmo", slowT_, 5, col::cyan);
        effect("magnet", magnetT_, 10, col::red);

        // wave track: one pip per wave, boss waves get a portrait
        float cx = VW / 2.0f;
        if (wave_ <= kFinalWave) {
            float span = 420, x0 = cx - span / 2;
            DrawLineEx({x0, 50}, {x0 + span, 50}, 4, alpha(col::bgDeep, 0.9f));
            DrawLineEx({x0, 50}, {x0 + span * (wave_ - 1) / (kFinalWave - 1.0f), 50}, 4, col::gold);
            for (int w = 1; w <= kFinalWave; w++) {
                Vector2 p{x0 + span * (w - 1) / (kFinalWave - 1.0f), 50};
                bool done = w < wave_, cur = w == wave_;
                const char* face = w == 5 ? "davi" : w == 8 ? "kira" : w == 10 ? "horse" : nullptr;
                if (face) {
                    art::portrait(face, p, cur ? 15 : 12, done ? col::gold : (cur ? WHITE : col::muted), 0, false,
                                  done || cur ? WHITE : alpha(WHITE, 0.45f));
                } else {
                    float r = cur ? 8 + std::sin(t_ * 6) : 6;
                    DrawCircleV(p, r + 2, col::outline);
                    DrawCircleV(p, r, done ? col::gold : cur ? WHITE : rgba(0x3a2a52ff));
                }
            }
            fx::gradientText("WAVE " + std::to_string(wave_), cx, 22, 26, WHITE, lighten(col::violet, 0.4f), 2.5f);
        } else {
            fx::gradientText("ENDLESS WAVE " + std::to_string(wave_), cx, 26, 30, WHITE, col::pink, 3);
        }
        // timer under the track
        if (!boss_.alive() && boss_.kind == BossKind::None) {
            float k = phase_ == Phase::Playing ? 1 - waveT_ / waveLen_ : 1;
            Rectangle tb{cx - 210, 66, 420, 6};
            DrawRectangleRounded(tb, 1, 6, alpha(col::bgDeep, 0.9f));
            DrawRectangleRounded({tb.x, tb.y, tb.width * k, tb.height}, 1, 6, col::violet);
        }

        ui::coinCounter(VW - 20, 38, P().coins);
        ui::text("+" + fmtNum(earned_) + " this run", VW - 26, 72, 15, col::gold, ui::Right);

        // boss bar
        if (boss_.kind != BossKind::None && (boss_.hp > 0 || bossChip_ > 0.05f)) {
            Rectangle bb{cx - 300, VH - 40, 600, 18};
            Color bc = boss_.enraged() ? col::red : bossColor(boss_.kind);
            DrawRectangleRounded(shrink(bb, -4), 1, 10, col::outline);
            DrawRectangleRounded(bb, 1, 10, rgba(0x1a1030ff));
            float hpK = (float)std::max(0, boss_.hp) / boss_.maxHp, chipK = bossChip_ / boss_.maxHp;
            DrawRectangleRounded({bb.x, bb.y, bb.width * chipK, bb.height}, 1, 10, WHITE);
            if (hpK > 0) DrawRectangleRounded({bb.x, bb.y, std::max(bb.height, bb.width * hpK), bb.height}, 1, 10, bc);
            DrawRectangleRounded({bb.x + 4, bb.y + 2, std::max(0.0f, bb.width * hpK - 8), 5}, 1, 6, alpha(WHITE, 0.35f));
            for (int i = 1; i < boss_.maxHp; i++) {
                float x = bb.x + bb.width * i / boss_.maxHp;
                DrawLineEx({x, bb.y + 2}, {x, bb.y + bb.height - 2}, 2, alpha(col::outline, 0.6f));
            }
            const char* who = boss_.kind == BossKind::Davi ? "davi" : boss_.kind == BossKind::Kira ? "kira" : "horse";
            art::portrait(who, {bb.x - 30, bb.y + 9}, 26, bc);
            ui::textOutline(boss_.name, bb.x + 8, bb.y - 14, 22, WHITE, col::outline, 2, ui::Left, true);
            ui::text(boss_.title, bb.x + bb.width, bb.y - 13, 15, alpha(bc, 0.9f), ui::Right, true);
        }

        // powerup keycaps (bottom-left so the boss bar has the center)
        for (int i = 0; i < 3; i++) {
            Rectangle r{40 + i * 74.0f, VH - 78, 64, 60};
            const std::string& id = P().loadout[i];
            int n = id.empty() ? 0 : P().count(id);
            bool ready = n > 0;
            DrawRectangleRounded({r.x, r.y + 5, r.width, r.height}, 0.25f, 8, col::outline);
            DrawRectangleRounded(r, 0.25f, 8, ready ? rgba(0x3a2670ff) : rgba(0x21183aff));
            DrawRectangleRounded({r.x + 4, r.y + 3, r.width - 8, r.height * 0.4f}, 0.4f, 8, alpha(WHITE, ready ? 0.1f : 0.04f));
            DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, ready ? alpha(col::violet, 0.9f) : alpha(col::muted, 0.3f));
            if (!id.empty()) art::itemIcon(id, {r.x + r.width / 2, r.y + 30}, 36);
            if (!ready) DrawRectangleRounded(r, 0.25f, 8, alpha(rgba(0x150d29ff), 0.6f));
            DrawRectangleRounded({r.x - 6, r.y - 8, 22, 22}, 0.4f, 6, col::gold);
            ui::text(std::to_string(i + 1), r.x + 5, r.y + 3, 15, col::outline, ui::Center, true);
            ui::text("x" + std::to_string(n), r.x + r.width - 5, r.y + r.height - 9, 14, ready ? WHITE : col::muted, ui::Right, true);
        }
        // cheats / practice badge
        std::string badge;
        if (practice_) badge += "PRACTICE  ";
        if (Admin().god || god_) badge += "GOD MODE  ";
        if (Admin().speed != 1) badge += TextFormat("%.2gx SPEED", Admin().speed);
        if (!badge.empty()) {
            float bw = ui::measure(badge, 16, true) + 24;
            Rectangle br{VW - 20 - bw, 86, bw, 26};
            DrawRectangleRounded(br, 1, 8, alpha(col::red, 0.85f));
            ui::text(badge, br.x + bw / 2, br.y + 13, 16, WHITE, ui::Center, true);
        }
        float hintA = clamp01(1.5f - totalT_ / 8);
        if (hintA > 0)
            ui::text("WASD move   SPACE dash   1-3 powerups   ESC pause", VW - 30, VH - 14, 15, alpha(col::text, 0.6f * hintA), ui::Right);
    }

    void drawBanner(const std::string& big, const std::string& small, Color c, float t, float dur) {
        float in = easeOutCubic(t / 0.35f), out = clamp01((dur - t) / 0.3f);
        float a = std::min(in, out);
        if (a <= 0) return;
        float h = 150 * a;
        float y = VH / 2.0f - 20;
        DrawRectangle(0, (int)(y - h / 2), VW, (int)h, alpha(col::bgDeep, 0.78f));
        // moving diagonal stripes inside the band
        E().beginClip({0, y - h / 2, (float)VW, h});
        for (float x = -200 + std::fmod(t_ * 80, 60.0f); x < VW + 200; x += 60)
            DrawLineEx({x, y + h / 2}, {x + 120, y - h / 2}, 16, alpha(c, 0.06f));
        E().endClip();
        DrawRectangle(0, (int)(y - h / 2), VW, 4, alpha(c, a));
        DrawRectangle(0, (int)(y + h / 2 - 4), VW, 4, alpha(c, a));
        fx::beginAdd();
        fx::glowLine({0, y - h / 2}, {(float)VW, y - h / 2}, 14, alpha(c, 0.4f * a));
        fx::glowLine({0, y + h / 2}, {(float)VW, y + h / 2}, 14, alpha(c, 0.4f * a));
        fx::endAdd();
        float slide = (1 - in) * -500 + (1 - out) * 500;
        fx::gradientText(big, VW / 2.0f + slide, y - 14, 84, WHITE, c, 5);
        ui::text(small, VW / 2.0f - slide, y + 48, 24, alpha(col::text, a), ui::Center, true);
    }

    void drawBossIntro(float t) {
        float dur = 2.6f;
        float in = easeOutCubic(t / 0.5f), out = clamp01((dur - t) / 0.35f);
        float a = std::min(in, out);
        Color c = bossColor(boss_.kind);
        // cinematic letterbox
        float bar = 90 * a;
        DrawRectangle(0, 0, VW, (int)bar, BLACK);
        DrawRectangle(0, VH - (int)bar, VW, (int)bar, BLACK);
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.45f * a));
        float slide = (1 - easeOutBack(clamp01(t / 0.6f))) * 600 + (1 - out) * -800;
        const char* who = boss_.kind == BossKind::Davi ? "davi" : boss_.kind == BossKind::Kira ? "kira" : "horse";
        Vector2 pc{VW * 0.72f + slide, VH / 2.0f};
        fx::beginAdd();
        fx::glow(pc, 330, alpha(c, 0.55f * a));
        fx::endAdd();
        fx::dashedRing(pc, 180, 6, 20, t_ * 80, alpha(c, a));
        art::portrait(who, pc, 160, c, std::sin(t_ * 3) * 4, false, alpha(WHITE, a));
        bool final = boss_.kind == BossKind::Horse && wave_ == kFinalWave;
        float tx = VW * 0.33f - slide * 0.6f;
        ui::text(final ? "FINAL BOSS" : "MINIBOSS", tx, VH / 2.0f - 92, 26, alpha(c, a), ui::Center, true);
        fx::gradientText(boss_.name, tx, VH / 2.0f - 22, 96, WHITE, c, 6);
        ui::text(boss_.title, tx, VH / 2.0f + 50, 26, alpha(WHITE, a), ui::Center, true);
        ui::text("grab the BOBA to hit back!", tx, VH / 2.0f + 92, 20, alpha(col::boba, a), ui::Center, true);
    }

    void draw() override {
        fx::blurredBackdrop("menu_bg", 0.6f, t_);
        fx::bokeh(t_, 14, col::violet, 3);

        Vector2 ac = center(kArena);
        rlPushMatrix();
        rlTranslatef(ac.x, ac.y, 0);
        rlScalef(worldZoom_, worldZoom_, 1);
        rlTranslatef(-ac.x, -ac.y, 0);

        drawArena();
        // floor light: everything glows onto the floor
        fx::beginAdd();
        drawPlayerGlow();
        drawBossGlow();
        for (auto& e : enemies_) drawEnemyFx(e);
        for (auto& p : pickups_) drawPickupGlow(p);
        for (auto& h : homing_) fx::glow(h.pos, 50, alpha(col::boba, 0.5f));
        fx::endAdd();
        for (auto& w : warnings_) drawWarning(w);
        for (auto& e : enemies_) drawChargerTelegraph(e);

        // shadows
        for (auto& e : enemies_) fx::softShadow({e.pos.x, e.pos.y + e.r * 0.9f}, e.r * 1.3f, e.r * 0.5f, 0.55f);
        if (boss_.alive()) fx::softShadow({boss_.pos.x, boss_.pos.y + boss_.r * 0.85f}, boss_.r * 1.2f, boss_.r * 0.4f, 0.6f);
        if (phase_ != Phase::Dead) fx::softShadow({player_.x, player_.y + kPlayerR * 0.95f}, kPlayerR * 1.2f, kPlayerR * 0.45f, 0.55f);

        for (auto& p : pickups_) drawPickup(p);
        for (auto& e : enemies_) drawEnemy(e);
        drawBoss();
        drawPlayer();
        for (auto& h : homing_) {
            fx::beginAdd();
            fx::glowLine(h.pos - h.vel * 0.05f, h.pos, 14, alpha(col::boba, 0.5f));
            fx::endAdd();
            art::boba(h.pos, 28);
        }
        fx::beginAdd();
        for (auto& b : bullets_) drawBulletGlow(b);
        fx::endAdd();
        for (auto& b : bullets_) drawBullet(b);
        motes_.draw();
        fx_.draw();
        shocks_.draw();
        drawArenaBorder();
        if (Admin().hitboxes) drawHitboxes();
        rlPopMatrix();

        // screen-space mood: slow-mo tint, low health heartbeat, damage
        if (slowT_ > 0) {
            Color c = alpha(col::cyan, 0.22f * clamp01(slowT_));
            DrawRectangleGradientV(0, 0, VW, 160, c, alpha(c, 0));
            DrawRectangleGradientV(0, VH - 160, VW, 160, alpha(c, 0), c);
        }
        float danger = hearts_ == 1 && phase_ == Phase::Playing ? 0.18f + 0.18f * std::max(0.0f, std::sin(t_ * 9)) : 0;
        danger = std::max(danger, hurtGlow_ * 0.55f);
        if (danger > 0) {
            Color c = alpha(col::red, danger), z = alpha(col::red, 0);
            DrawRectangleGradientV(0, 0, VW, 180, c, z);
            DrawRectangleGradientV(0, VH - 180, VW, 180, z, c);
            DrawRectangleGradientH(0, 0, 220, VH, c, z);
            DrawRectangleGradientH(VW - 220, 0, 220, VH, z, c);
        }

        drawHud();

        switch (phase_) {
            case Phase::Intro:
                if (boss_.kind != BossKind::None) drawBossIntro(phaseT_);
                else drawBanner("WAVE " + std::to_string(wave_), wave_ == 1 ? "dodge everything. you got this pieter" : "survive!", col::gold, phaseT_, 1.8f);
                break;
            case Phase::Cleared:
                drawBanner("WAVE CLEARED", "+" + std::to_string(15 * wave_) + " bonus coins", col::green, phaseT_, 2.6f);
                break;
            case Phase::Dead: drawEndScreen(false); break;
            case Phase::Victory: drawEndScreen(true); break;
            default: break;
        }
        if (paused_) drawPause();
    }

    void drawHitboxes() {
        Color c = alpha(col::green, 0.9f);
        // the radii here are the ones the collision checks use
        ui::circleOutline(player_, kPlayerR - 6, alpha(WHITE, 0.9f), 2);
        for (auto& e : enemies_) ui::circleOutline(e.pos, e.r, c, 2);
        for (auto& b : bullets_) ui::circleOutline(b.pos, b.r, alpha(col::red, 0.9f), 2);
        if (boss_.alive()) ui::circleOutline(boss_.pos, boss_.r - 10, alpha(col::red, 0.9f), 2);
    }

    void restart() {
        if (practice_) Admin().practicePending = true;  // same practice setup again
        E().go(makeClassic);
    }

    void toMenu() { E().go(practice_ ? makeAdmin : makeMenu); }

    void drawEndScreen(bool won) {
        float a = clamp01(phaseT_ / 0.6f);
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.65f * a));
        if (phaseT_ < 0.5f) return;
        float pop = easeOutBack(clamp01((phaseT_ - 0.5f) / 0.4f));
        Rectangle r{VW / 2.0f - 310, VH / 2.0f - 220 + (1 - pop) * 60, 620, 440};
        Color c = won ? col::gold : col::red;
        fx::beginAdd();
        fx::glow(center(r), 520, alpha(c, 0.25f));
        fx::endAdd();
        ui::panel(r, rgba(0x1d1236f5), 0.08f, col::outline);
        DrawRectangleRoundedLinesEx(shrink(r, 6), 0.07f, 8, 2, alpha(c, 0.5f));
        fx::dashedRing({r.x + r.width / 2, r.y + 8}, 76, 4, 14, t_ * 60, c);
        art::pieter({r.x + r.width / 2, r.y + 8}, 64, P().hat, won ? std::sin(t_ * 4) * 10 : 180 * easeOutCubic(phaseT_ - 0.5f));
        fx::gradientText(won ? "PIETER WINS!" : "PIETER GOT REKT", r.x + r.width / 2, r.y + 118, 56, WHITE, c, 5);
        ui::text(won ? "The Horse has been defeated. JUHUH no more." : "you reached wave " + std::to_string(wave_), r.x + r.width / 2,
                 r.y + 168, 22, col::muted, ui::Center, true);
        Rectangle st{r.x + 60, r.y + 196, r.width - 120, 96};
        DrawRectangleRounded(st, 0.25f, 8, alpha(col::bgDeep, 0.8f));
        ui::coin({st.x + 40, st.y + 34}, 18);
        ui::text("+" + fmtNum(earned_), st.x + 68, st.y + 34, 34, col::gold, ui::Left, true);
        ui::text("coins earned", st.x + 68, st.y + 68, 16, col::muted);
        ui::text(std::to_string(P().stats.bestWave), st.x + st.width - 40, st.y + 34, 34, WHITE, ui::Right, true);
        ui::text("best wave", st.x + st.width - 40, st.y + 68, 16, col::muted, ui::Right);
        float by = r.y + r.height - 100;
        if (won) {
            if (ui::button({r.x + 40, by, 170, 60}, "ENDLESS", col::violet)) { phase_ = Phase::Cleared; phaseT_ = 2.0f; }
            if (ui::button({r.x + 225, by, 170, 60}, "AGAIN", col::purple)) restart();
            if (ui::button({r.x + 410, by, 170, 60}, practice_ ? "ADMIN" : "MENU", col::panelHi)) toMenu();
        } else {
            if (ui::button({r.x + 60, by, 240, 60}, "RETRY", col::purple, 28)) restart();
            if (ui::button({r.x + 320, by, 240, 60}, practice_ ? "ADMIN" : "MENU", col::panelHi, 28)) toMenu();
        }
        if (IsKeyPressed(KEY_ENTER)) restart();
    }

    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f));
        Rectangle r{VW / 2.0f - 210, VH / 2.0f - 150, 420, 300};
        ui::panel(r, rgba(0x1d1236f5), 0.1f);
        fx::gradientText("PAUSED", r.x + r.width / 2, r.y + 62, 56, WHITE, col::violet, 4);
        if (ui::button({r.x + 50, r.y + 120, 320, 60}, "RESUME", col::purple, 26)) paused_ = false;
        if (ui::button({r.x + 50, r.y + 196, 320, 60}, practice_ ? "BACK TO ADMIN" : "QUIT TO MENU", col::red, 24)) toMenu();
    }

    static constexpr float kPlayerR = 26;

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
    Particles fx_, motes_;
    fx::Shocks shocks_;
    bool god_ = false, practice_ = false;
    float hitStop_ = 0, worldZoom_ = 1, heartAnim_ = 0, hurtGlow_ = 0, bossChip_ = 0, moteT_ = 0, totalT_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeClassic() { return std::make_unique<ClassicScene>(); }

}  // namespace pg
