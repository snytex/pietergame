// POLDER DEFENSE: tower defense on a Dutch polder. Davi has emptied his
// basement and his useless elektronik (routers, gaming chairs, printers,
// drones and sad monitors) marches down the dike road to Pieter's farm to
// steal the stroopwafel stash. Build clog cannons, windmills, cheese
// catapults and tulip zappers along the road, upgrade them twice and survive
// twenty waves (then keep going for the record).
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
#include <iterator>
#include <vector>

namespace pg {
namespace {

constexpr float kT = 48;
constexpr int kCols = 21, kRows = 15;
constexpr float kMapW = kCols * kT;
constexpr int kFinalWave = 20;

// the dike road, in tile coordinates
const Vector2 kPath[] = {{-1.6f, 2}, {5, 2}, {5, 7}, {1, 7}, {1, 12}, {9, 12}, {9, 4}, {14, 4}, {14, 11}, {18, 11}, {18, 2}, {20.4f, 2}};
constexpr int kPathN = sizeof(kPath) / sizeof(kPath[0]);
// drones ignore the road and fly straight at the farm
const Vector2 kFlyA{-1, 13.5f}, kFlyB{19.6f, 2};

Vector2 tileC(Vector2 t) { return {(t.x + 0.5f) * kT, (t.y + 0.5f) * kT}; }

enum Cell { Grass, Road, Water, Blocked };

// ---------------------------------------------------------------- towers
enum TType { Klomp, Molen, Kaas, Zap, TCount };
struct TDef {
    const char* name;
    const char* desc;
    int cost, up[2];
    float range[3], dmg[3], rate[3], extra[3];
    Color color;
};
const TDef kTowers[TCount] = {
    {"CLOG CANNON", "Fires wooden clogs at anything, flying or not.", 50, {55, 95}, {125, 140, 158}, {11, 20, 34}, {1.15f, 1.45f, 1.9f}, {0, 0, 0}, rgba(0xd8a24aff)},
    {"WINDMILL", "Blows the elektronik back: slows everything nearby and wears it down.", 70, {65, 110}, {100, 116, 134}, {3, 6, 11}, {0, 0, 0}, {0.32f, 0.44f, 0.55f}, rgba(0x6ab0e0ff)},
    {"CHEESE CATAPULT", "Lobs whole Goudas. Big splash, long range, ground only.", 110, {110, 170}, {190, 210, 235}, {26, 46, 80}, {0.5f, 0.6f, 0.72f}, {52, 62, 74}, rgba(0xffc531ff)},
    {"TULIP ZAPPER", "Lightning that jumps from target to target.", 140, {125, 185}, {130, 142, 158}, {15, 24, 38}, {0.9f, 1.1f, 1.35f}, {2, 3, 4}, rgba(0xff5a8aff)},
};

struct Tower {
    int type = 0, lvl = 0, cx = 0, cy = 0;
    Vector2 pos{};
    float cd = 0, aim = -PI / 2, recoil = 0, anim = 0, sails = 0;
    int spent = 0, kills = 0;
};

// ---------------------------------------------------------------- the elektronik
enum FType { Router, Chair, Printer, Drone, Monitor, DaviBoss, HorseBoss, FCount };
struct FDef {
    const char* name;
    float hp, speed;
    int reward, dmg;
    float armor;
    bool flying;
};
const FDef kFoes[FCount] = {
    {"Router", 40, 68, 6, 1, 0, false},       {"Gaming chair", 30, 122, 6, 1, 0, false}, {"Printer", 170, 38, 15, 2, 5, false},
    {"Drone", 50, 84, 9, 1, 0, true},         {"Sad monitor", 110, 52, 14, 1, 2, false}, {"DAVI", 1000, 30, 150, 8, 4, false},
    {"THE HORSE", 2200, 44, 400, 12, 6, false},
};

struct Foe {
    int type = 0, id = 0;
    float d = 0, hp = 1, maxHp = 1, slow = 0, flash = 0, t = 0, dash = 0;
    int spawned = 0;  // boss minion waves already spawned
    Vector2 pos{};
    float face = 1;
    bool dead = false;
};

struct Shot {
    int kind;  // 0 clog (homing), 1 cheese (arc)
    Vector2 p, from, to;
    int target;
    float t, dur, dmg, splash, spin;
};
struct Bolt { std::vector<Vector2> pts; float t; };
struct Spawn { int type; float at; };

enum class Phase { Play, Lost, Won };

class PolderScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Lost) return {"Polder Defense", "The polder fell at wave " + std::to_string(wave_)};
        return {"Polder Defense", "Wave " + std::to_string(std::max(1, wave_)) + " / " + std::to_string(kFinalWave) + "  -  " + std::to_string(lives_) + " stroopwafels left"};
    }

    void enter() override {
        buildMap();
        reset();
        bot_ = std::getenv("PG_POLDER_BOT") != nullptr;
        if (bot_) speed_ = 6;
        if (const char* w = std::getenv("PG_POLDER")) {  // dev hook: start at a wave with some money
            wave_ = std::max(0, std::atoi(w) - 1);
            money_ = 200 + wave_ * 140;
        }
    }

    void reset() {
        towers_.clear();
        foes_.clear();
        shots_.clear();
        bolts_.clear();
        queue_.clear();
        fx_.clear();
        money_ = 250;
        lives_ = 20;
        wave_ = 0;
        cleared_ = 0;
        waveOn_ = false;
        countdown_ = -1;
        build_ = -1;
        sel_ = -1;
        phase_ = Phase::Play;
        phaseT_ = 0;
        paused_ = false;
        wonShown_ = false;
        earnedTotal_ = 0;
        speed_ = bot_ ? 6 : 1;
    }

    // ------------------------------------------------------------ map
    void buildMap() {
        for (auto& row : cells_) std::fill(std::begin(row), std::end(row), (int)Grass);
        // road
        pathLen_ = 0;
        segLen_.clear();
        for (int i = 0; i + 1 < kPathN; i++) {
            Vector2 a = tileC(kPath[i]), b = tileC(kPath[i + 1]);
            segLen_.push_back(Vector2Distance(a, b));
            pathLen_ += segLen_.back();
            for (float k = 0; k <= 1; k += 0.02f) {
                Vector2 p = Vector2Lerp(kPath[i], kPath[i + 1], k);
                int cx = (int)std::round(p.x), cy = (int)std::round(p.y);
                if (cx >= 0 && cx < kCols && cy >= 0 && cy < kRows) cells_[cy][cx] = Road;
            }
        }
        flyLen_ = Vector2Distance(tileC(kFlyA), tileC(kFlyB));
        // the ditch between the loops, the canal along the top, the tulip field, the farm
        for (int y = 6; y < kRows; y++) cells_[y][11] = Water;
        for (int x = 7; x <= 16; x++) cells_[0][x] = Water;
        for (int y = 13; y < kRows; y++)
            for (int x = 15; x < kCols; x++) cells_[y][x] = Blocked;
        for (int y = 0; y <= 3; y++)
            for (int x = 19; x < kCols; x++) cells_[y][x] = Blocked;
        trees_ = {{0, 0}, {3, 0}, {0, 14}, {7, 14}, {20, 8}, {12, 1}};
        for (auto& t : trees_) cells_[(int)t.y][(int)t.x] = Blocked;
        animals_ = {{"spr/ani_cow", {6.6f, 9.4f}, 0.22f, false}, {"spr/ani_sheep", {16.3f, 7.5f}, 0.24f, true}, {"spr/ani_sheep", {3.2f, 4.6f}, 0.22f, false},
                    {"spr/ani_cow", {12.6f, 13.2f}, 0.2f, true}};
        for (auto& a : animals_) cells_[(int)a.at.y][(int)a.at.x] = Blocked;
    }

    Vector2 roadPos(float d) const {
        float acc = 0;
        for (int i = 0; i + 1 < kPathN; i++) {
            if (d <= acc + segLen_[i] || i + 2 == kPathN)
                return Vector2Lerp(tileC(kPath[i]), tileC(kPath[i + 1]), std::clamp((d - acc) / segLen_[i], 0.0f, 1.0f));
            acc += segLen_[i];
        }
        return tileC(kPath[kPathN - 1]);
    }
    float lengthOf(const Foe& f) const { return kFoes[f.type].flying ? flyLen_ : pathLen_; }
    Vector2 posAt(const Foe& f, float d) const {
        if (kFoes[f.type].flying) return Vector2Lerp(tileC(kFlyA), tileC(kFlyB), std::clamp(d / flyLen_, 0.0f, 1.0f));
        return roadPos(d);
    }

    // ------------------------------------------------------------ waves
    static float hpMul(int w) { return 1 + 0.15f * (w - 1) + 0.011f * (w - 1) * (w - 1); }

    std::vector<Spawn> makeWave(int w) {
        std::vector<Spawn> s;
        float t = 0;
        auto add = [&](int type, int n, float gap) {
            for (int i = 0; i < n; i++) {
                s.push_back({type, t});
                t += gap;
            }
            t += 1.4f;
        };
        switch (w) {
            case 1: add(Router, 8, 1.1f); break;
            case 2: add(Router, 8, 0.9f); add(Chair, 4, 0.7f); break;
            case 3: add(Chair, 8, 0.6f); add(Router, 8, 0.8f); break;
            case 4: add(Router, 8, 0.8f); add(Printer, 3, 2.2f); add(Chair, 6, 0.5f); break;
            case 5: add(Router, 10, 0.7f); add(DaviBoss, 1, 1); add(Router, 4, 0.6f); break;
            case 6: add(Drone, 6, 1.0f); add(Chair, 10, 0.45f); add(Drone, 4, 0.8f); break;
            case 7: add(Router, 10, 0.6f); add(Monitor, 2, 1.5f); add(Printer, 4, 1.6f); break;
            default: {
                std::mt19937 r((unsigned)w * 7919u);
                auto R = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(r); };
                float budget = 10 + w * 3.2f + w * w * 0.22f;
                const float cost[5] = {1, 1.2f, 3.5f, 2, 4};
                const float gap[5] = {0.55f, 0.4f, 1.4f, 0.7f, 1.2f};
                while (budget > 1) {
                    int type = R(0, 4);
                    int n = std::max(1, std::min((int)(budget / cost[type]), R(4, 10)));
                    add(type, n, gap[type] * std::max(0.55f, 1 - w * 0.015f));
                    budget -= n * cost[type];
                }
                if (w == kFinalWave || (w > kFinalWave && w % 10 == 0)) {
                    add(HorseBoss, 1, 1);
                    add(Chair, 8, 0.35f);
                } else if (w % 5 == 0) {
                    add(DaviBoss, w >= 15 ? 2 : 1, 4);
                }
                break;
            }
        }
        return s;
    }

    void startWave() {
        if (waveOn_) return;
        if (countdown_ > 0) {
            int bonus = (int)countdown_ * 2;
            if (bonus > 0) {
                money_ += bonus;
                fx_.floatText("+" + std::to_string(bonus) + " early bonus", {kMapW / 2, 80}, col::gold, 24);
            }
        }
        wave_++;
        queue_ = makeWave(wave_);
        waveT_ = 0;
        waveOn_ = true;
        countdown_ = -1;
        E().sfx.play(Sfx::Horn, 0.5f);
        banner_ = 2.2f;
    }

    void waveCleared() {
        waveOn_ = false;
        cleared_ = wave_;
        int bonus = 25 + wave_ * 5;
        money_ += bonus;
        fx_.floatText("WAVE " + std::to_string(wave_) + " CLEARED  +" + std::to_string(bonus), {kMapW / 2, 110}, col::green, 30);
        E().sfx.play(Sfx::Perfect, 0.6f);
        if (bot_) std::printf("polder bot: cleared wave %d, stash %d, money %d, %zu towers\n", wave_, lives_, money_, towers_.size());
        if (!bot_) {
            int coins = 5 + wave_ * 2;
            P().earn(coins);
            earnedTotal_ += coins;
            P().stats.polderBest = std::max(P().stats.polderBest, cleared_);
            P().save();
        }
        if (wave_ == kFinalWave && !wonShown_) {
            wonShown_ = true;
            phase_ = Phase::Won;
            phaseT_ = 0;
            if (!bot_) {
                P().earn(250);
                earnedTotal_ += 250;
                P().save();
            }
            E().sfx.play(Sfx::Win, 0.8f);
            fx_.confetti({kMapW / 2, 200}, 120);
            return;
        }
        countdown_ = 14;
    }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        if (phase_ == Phase::Lost) {
            fx_.update(dt);
            if (E().backPressed()) E().go(makeMenu);
            return;
        }
        if (phase_ == Phase::Won) {
            fx_.update(dt);
            if (E().backPressed()) phase_ = Phase::Play;
            return;
        }
        if (E().backPressed()) {
            if (build_ >= 0 || sel_ >= 0) build_ = sel_ = -1;
            else paused_ = !paused_;
        }
        if (IsKeyPressed(KEY_P)) paused_ = !paused_;
        if (paused_) return;
        input();
        if (bot_) botThink();
        // fixed sub-steps so 3x speed stays stable
        float step = dt * speed_;
        int n = std::max(1, (int)std::ceil(step / 0.02f));
        for (int i = 0; i < n && phase_ == Phase::Play; i++) sim(step / n);
        fx_.update(dt * std::min((float)speed_, 2.0f));
        banner_ = std::max(0.0f, banner_ - dt);
    }

    void input() {
        for (int i = 0; i < TCount; i++)
            if (IsKeyPressed(KEY_ONE + i)) {
                build_ = build_ == i ? -1 : i;
                sel_ = -1;
            }
        if (IsKeyPressed(KEY_SPACE)) startWave();
        if (IsKeyPressed(KEY_F)) speed_ = speed_ >= 3 ? 1 : speed_ + 1;
        if (E().rightPressed()) build_ = sel_ = -1;
        if (sel_ >= 0 && IsKeyPressed(KEY_U)) upgrade(sel_);
        if (sel_ >= 0 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))) sell(sel_);
        Vector2 m = E().mouse();
        if (m.x >= kMapW || !E().mousePressed()) return;
        int cx = (int)(m.x / kT), cy = (int)(m.y / kT);
        if (cx < 0 || cy < 0 || cx >= kCols || cy >= kRows) return;
        int at = towerAt(cx, cy);
        if (build_ >= 0) {
            if (at < 0) place(build_, cx, cy);
            else { sel_ = at; build_ = -1; }
        } else {
            sel_ = at;
        }
        E().consumeClick();
    }

    int towerAt(int cx, int cy) const {
        for (size_t i = 0; i < towers_.size(); i++)
            if (towers_[i].cx == cx && towers_[i].cy == cy) return (int)i;
        return -1;
    }
    bool canPlace(int cx, int cy) const { return cells_[cy][cx] == Grass && towerAt(cx, cy) < 0; }

    bool place(int type, int cx, int cy) {
        if (!canPlace(cx, cy)) {
            E().sfx.play(Sfx::Error, 0.4f);
            return false;
        }
        int cost = kTowers[type].cost;
        if (money_ < cost) {
            E().sfx.play(Sfx::Error, 0.4f);
            fx_.floatText("not enough stroopwafels", tileC({(float)cx, (float)cy - 0.6f}), col::red, 18);
            return false;
        }
        money_ -= cost;
        Tower t;
        t.type = type;
        t.cx = cx;
        t.cy = cy;
        t.pos = tileC({(float)cx, (float)cy});
        t.spent = cost;
        towers_.push_back(t);
        E().sfx.play(Sfx::Hammer, 0.6f);
        fx_.burst(t.pos + Vector2{0, 14}, 12, rgba(0xc8b088ff), 140, 5, 0.4f);
        if (!IsKeyDown(KEY_LEFT_SHIFT) && !bot_) build_ = -1;  // hold shift to keep building
        return true;
    }

    void upgrade(int i) {
        Tower& t = towers_[i];
        if (t.lvl >= 2) return;
        int cost = kTowers[t.type].up[t.lvl];
        if (money_ < cost) {
            E().sfx.play(Sfx::Error, 0.4f);
            return;
        }
        money_ -= cost;
        t.spent += cost;
        t.lvl++;
        E().sfx.play(Sfx::Hammer, 0.6f, 1.2f);
        E().sfx.play(Sfx::Power, 0.4f);
        fx_.burst(t.pos, 18, col::gold, 200, 5, 0.5f, PShape::Star);
    }

    void sell(int i) {
        int refund = towers_[i].spent * 7 / 10;
        money_ += refund;
        fx_.floatText("+" + std::to_string(refund), towers_[i].pos, col::gold, 22);
        fx_.burst(towers_[i].pos, 14, rgba(0xc8b088ff), 160, 5, 0.4f);
        E().sfx.play(Sfx::Coin, 0.5f);
        towers_.erase(towers_.begin() + i);
        for (auto& o : shotOwner_)
            if (o == (size_t)i) o = SIZE_MAX;
            else if (o > (size_t)i) o--;
        sel_ = -1;
    }

    Foe* byId(int id) {
        for (auto& f : foes_)
            if (f.id == id && !f.dead) return &f;
        return nullptr;
    }

    void spawn(int type, float d = 0) {
        Foe f;
        f.type = type;
        f.id = nextId_++;
        f.maxHp = f.hp = kFoes[type].hp * hpMul(std::max(1, wave_));
        f.d = d;
        f.pos = posAt(f, d);
        foes_.push_back(f);
        if (type == DaviBoss) {
            E().sfx.play(Sfx::Error, 0.8f, 0.7f);
            fx_.floatText("DAVI IS COMING", {kMapW / 2, 160}, col::cyan, 34);
        }
        if (type == HorseBoss) {
            E().sfx.play(Sfx::Neigh, 0.9f, 0.8f);
            E().shake(8);
            fx_.floatText("JUHUH!!! THE HORSE!", {kMapW / 2, 160}, col::orange, 38);
        }
    }

    void damage(Foe& f, float dmg, Tower* by, bool pierce = false) {
        if (f.dead) return;
        if (!pierce) dmg = std::max(dmg * 0.3f, dmg - kFoes[f.type].armor);
        f.hp -= dmg;
        f.flash = 0.1f;
        if (f.hp > 0) return;
        f.dead = true;
        if (by) by->kills++;
        int reward = kFoes[f.type].reward;
        money_ += reward;
        fx_.floatText("+" + std::to_string(reward), f.pos + Vector2{0, -26}, col::gold, f.type >= DaviBoss ? 34 : 18);
        bool big = f.type >= DaviBoss;
        fx_.burst(f.pos, big ? 60 : 14, f.type == Drone ? col::red : rgba(0x8a8aa0ff), big ? 320 : 180, big ? 8 : 5, big ? 0.9f : 0.45f, PShape::Square, 300);
        fx_.burst(f.pos, 8, col::gold, 120, 4, 0.4f, PShape::Spark);
        shocks_.add(f.pos, 4, big ? 140 : 46, alpha(WHITE, 0.7f), 0.35f, 6);
        E().sfx.playAt(big ? Sfx::Boom : Sfx::Pop, f.pos.x * VW / kMapW, big ? 0.9f : 0.35f, rnd(0.85f, 1.15f));
        if (big) {
            E().shake(10);
            fx_.confetti(f.pos, 60);
        }
    }

    void sim(float dt) {
        // spawn queue
        if (waveOn_) {
            waveT_ += dt;
            while (!queue_.empty() && queue_.front().at <= waveT_) {
                spawn(queue_.front().type);
                queue_.erase(queue_.begin());
            }
        } else if (countdown_ > 0) {
            countdown_ -= dt;
            if (countdown_ <= 0) startWave();
        }

        // windmills slow and wear down everything in their wind
        for (auto& f : foes_) f.slow = 0;
        for (auto& tw : towers_) {
            tw.sails += dt * (1.5f + tw.lvl * 0.6f);
            tw.recoil = std::max(0.0f, tw.recoil - dt * 4);
            tw.anim = std::max(0.0f, tw.anim - dt);
            if (tw.type != Molen) continue;
            const TDef& d = kTowers[Molen];
            for (auto& f : foes_)
                if (!f.dead && Vector2Distance(f.pos, tw.pos) < d.range[tw.lvl]) {
                    f.slow = std::max(f.slow, d.extra[tw.lvl] * (f.type >= DaviBoss ? 0.5f : 1.0f));
                    damage(f, d.dmg[tw.lvl] * dt, &tw, true);
                }
        }

        // the elektronik moves
        for (auto& f : foes_) {
            if (f.dead) continue;
            f.t += dt;
            f.flash = std::max(0.0f, f.flash - dt);
            float sp = kFoes[f.type].speed * (1 - f.slow);
            if (f.type == HorseBoss) {
                f.dash -= dt;
                if (f.dash < -4.5f) {
                    f.dash = 0.9f;
                    E().sfx.play(Sfx::Neigh, 0.6f, 1.1f);
                    fx_.floatText("JUHUH!", f.pos + Vector2{0, -50}, col::orange, 26);
                }
                if (f.dash > 0) sp *= 2.6f;
            }
            if (f.type == Monitor) {  // sad monitors patch up whatever is near them
                for (auto& o : foes_)
                    if (&o != &f && !o.dead && o.hp < o.maxHp && Vector2Distance(o.pos, f.pos) < 80) {
                        o.hp = std::min(o.maxHp, o.hp + o.maxHp * 0.05f * dt);
                        if (chance(dt * 3)) fx_.floatText("+", o.pos + Vector2{rnd(-10, 10), -20}, col::green, 18);
                    }
            }
            if (f.type == DaviBoss) {  // calls in reinforcements when hurt
                int want = f.hp < f.maxHp * 0.33f ? 2 : f.hp < f.maxHp * 0.66f ? 1 : 0;
                while (f.spawned < want) {
                    f.spawned++;
                    for (int k = 0; k < 3; k++) pending_.push_back({Router, std::max(0.0f, f.d - 20 - k * 22)});
                    fx_.floatText("MORE ELEKTRONIK!", f.pos + Vector2{0, -60}, col::cyan, 26);
                    E().sfx.play(Sfx::Error, 0.7f, 0.8f);
                }
            }
            Vector2 old = f.pos;
            f.d += sp * dt;
            f.pos = posAt(f, f.d);
            if (std::abs(f.pos.x - old.x) > 0.01f) f.face = f.pos.x > old.x ? 1 : -1;
            if (f.d >= lengthOf(f)) {
                f.dead = true;
                int lost = std::min(lives_, kFoes[f.type].dmg);
                lives_ -= lost;
                hurtT_ = 0.6f;
                E().shake(f.type >= DaviBoss ? 14 : 5);
                E().sfx.play(Sfx::Hurt, 0.6f);
                fx_.floatText("-" + std::to_string(lost) + " stroopwafel" + (lost > 1 ? "s" : ""), {kMapW - 120, 210}, col::red, 24);
                if (lives_ <= 0) {
                    lose();
                    return;
                }
            }
        }

        for (auto& [type, d] : pending_) spawn(type, d);  // spawned after the loop: foes_ may grow
        pending_.clear();

        // towers fire
        for (auto& tw : towers_) {
            const TDef& d = kTowers[tw.type];
            tw.cd -= dt;
            if (tw.type == Molen) continue;
            Foe* target = nullptr;
            for (auto& f : foes_) {
                if (f.dead || Vector2Distance(f.pos, tw.pos) > d.range[tw.lvl]) continue;
                if (tw.type == Kaas && kFoes[f.type].flying) continue;
                if (!target || f.d / lengthOf(f) > target->d / lengthOf(*target)) target = &f;
            }
            if (target && tw.type == Klomp) {
                float want = angleOf(target->pos - tw.pos);
                float diff = std::remainder(want - tw.aim, 2 * PI);
                tw.aim += diff * std::min(1.0f, dt * 14);
            }
            if (!target || tw.cd > 0) continue;
            tw.cd = 1 / d.rate[tw.lvl];
            switch (tw.type) {
                case Klomp: {
                    Vector2 muzzle = tw.pos + Vector2{0, -14} + fromAngle(tw.aim, 22);
                    shots_.push_back({0, muzzle, muzzle, target->pos, target->id, 0, 0, d.dmg[tw.lvl], 0, rnd(0, 360)});
                    shotOwner_.push_back(&tw - towers_.data());
                    tw.recoil = 1;
                    E().sfx.playAt(Sfx::Klomp, tw.pos.x * VW / kMapW, 0.35f, rnd(1.3f, 1.6f));
                    fx_.burst(muzzle, 5, alpha(WHITE, 0.7f), 90, 6, 0.3f);
                    break;
                }
                case Kaas: {
                    float T = 0.85f;
                    float ahead = target->d + kFoes[target->type].speed * (1 - target->slow) * T;
                    Vector2 to = posAt(*target, std::min(ahead, lengthOf(*target)));
                    Vector2 from = tw.pos + Vector2{0, -26};
                    shots_.push_back({1, from, from, to, -1, 0, T, d.dmg[tw.lvl], d.extra[tw.lvl], 0});
                    shotOwner_.push_back(&tw - towers_.data());
                    tw.anim = 0.5f;
                    E().sfx.playAt(Sfx::Whoosh, tw.pos.x * VW / kMapW, 0.4f, 0.8f);
                    break;
                }
                case Zap: {
                    Bolt b;
                    b.t = 0.18f;
                    b.pts.push_back(tw.pos + Vector2{0, -30});
                    std::vector<int> hit;
                    Foe* cur = target;
                    float dmg = d.dmg[tw.lvl];
                    for (int k = 0; k < (int)d.extra[tw.lvl] && cur; k++) {
                        b.pts.push_back(cur->pos);
                        hit.push_back(cur->id);
                        damage(*cur, dmg, &tw);
                        dmg *= 0.82f;
                        Foe* next = nullptr;
                        float bd = 115;
                        for (auto& f : foes_) {
                            if (f.dead || std::find(hit.begin(), hit.end(), f.id) != hit.end()) continue;
                            float dd = Vector2Distance(f.pos, b.pts.back());
                            if (dd < bd) { bd = dd; next = &f; }
                        }
                        cur = next;
                    }
                    bolts_.push_back(b);
                    tw.anim = 0.25f;
                    E().sfx.playAt(Sfx::Zap, tw.pos.x * VW / kMapW, 0.4f, rnd(0.9f, 1.2f));
                    break;
                }
            }
        }

        // shots
        for (size_t i = 0; i < shots_.size(); i++) {
            Shot& s = shots_[i];
            Tower* owner = shotOwner_[i] < towers_.size() ? &towers_[shotOwner_[i]] : nullptr;
            s.t += dt;
            s.spin += dt * 900;
            if (s.kind == 0) {
                Foe* f = byId(s.target);
                if (f) s.to = f->pos;
                Vector2 dir = s.to - s.p;
                float dist = Vector2Length(dir);
                float step = 620 * dt;
                if (dist <= step + 8) {
                    if (f) damage(*f, s.dmg, owner);
                    fx_.burst(s.to, 4, rgba(0xd8a24aff), 90, 4, 0.25f, PShape::Square);
                    s.t = 99;
                } else {
                    s.p += dir * (step / dist);
                }
            } else {
                float k = s.t / s.dur;
                s.p = Vector2Lerp(s.from, s.to, k);
                if (k >= 1) {
                    for (auto& f : foes_)
                        if (!f.dead && !kFoes[f.type].flying && Vector2Distance(f.pos, s.to) < s.splash) damage(f, s.dmg, owner);
                    fx_.burst(s.to, 16, col::gold, 200, 6, 0.5f, PShape::Square, 400);
                    shocks_.add(s.to, 6, s.splash, alpha(col::gold, 0.8f), 0.35f, 8);
                    E().sfx.playAt(Sfx::Cannon, s.to.x * VW / kMapW, 0.4f, 1.3f);
                    s.t = 99;
                }
            }
        }
        for (size_t i = shots_.size(); i-- > 0;)
            if (shots_[i].t >= 99) {
                shots_.erase(shots_.begin() + i);
                shotOwner_.erase(shotOwner_.begin() + i);
            }
        for (auto& b : bolts_) b.t -= dt;
        std::erase_if(bolts_, [](const Bolt& b) { return b.t <= 0; });
        std::erase_if(foes_, [](const Foe& f) { return f.dead; });
        shocks_.update(dt);
        hurtT_ = std::max(0.0f, hurtT_ - dt);
        if (waveOn_ && queue_.empty() && foes_.empty()) waveCleared();
    }

    void lose() {
        phase_ = Phase::Lost;
        phaseT_ = 0;
        waveOn_ = false;
        E().sfx.play(Sfx::Lose, 0.8f);
        if (bot_) {
            std::printf("polder bot: lost on wave %d (cleared %d), %zu towers\n", wave_, cleared_, towers_.size());
            E().quit();
        }
    }

    // Dev autopilot (PG_POLDER_BOT=1): builds by a fixed plan on the tiles that see the most road.
    void botThink() {
        if (!waveOn_ && phase_ == Phase::Play && countdown_ < 0) startWave();
        if (countdown_ > 0) startWave();
        static const int plan[] = {Klomp, Klomp, Molen, Kaas, Klomp, Zap, Kaas, Molen, Zap, Klomp, Kaas, Zap};
        int next = plan[std::min<size_t>(towers_.size(), std::size(plan) - 1)];
        // upgrade first once a few towers stand
        if (towers_.size() >= 6) {
            int best = -1;
            for (size_t i = 0; i < towers_.size(); i++)
                if (towers_[i].lvl < 2 && (best < 0 || towers_[i].lvl < towers_[best].lvl)) best = (int)i;
            if (best >= 0 && money_ >= kTowers[towers_[best].type].up[towers_[best].lvl] && (towers_.size() >= 14 || chance(0.5f))) {
                upgrade(best);
                return;
            }
        }
        if (money_ < kTowers[next].cost || towers_.size() >= 18) return;
        int bx = -1, by = -1;
        float bs = -1;
        for (int y = 0; y < kRows; y++)
            for (int x = 0; x < kCols; x++) {
                if (!canPlace(x, y)) continue;
                Vector2 c = tileC({(float)x, (float)y});
                float score = 0;
                for (float d = 0; d < pathLen_; d += 24)
                    if (Vector2Distance(roadPos(d), c) < kTowers[next].range[0]) score += 1 + d / pathLen_ * 0.5f;
                if (next != Kaas)  // the drones' route counts too
                    for (float k = 0; k < 1; k += 0.02f)
                        if (Vector2Distance(Vector2Lerp(tileC(kFlyA), tileC(kFlyB), k), c) < kTowers[next].range[0]) score += 0.6f;
                if (score > bs) { bs = score; bx = x; by = y; }
            }
        if (bx >= 0) place(next, bx, by);
    }

    // ------------------------------------------------------------ draw: map
    void drawMap() {
        fx::tiled(E().tex("tex/grass"), {0, 0, kMapW, (float)VH}, 0.6f);
        for (int x = 0; x < kCols; x += 2) DrawRectangle((int)(x * kT), 0, (int)kT, VH, alpha(WHITE, 0.035f));  // mowing stripes
        // water: canal along the top and the ditch between the loops
        auto water = [&](Rectangle r) {
            fx::tiled(E().tex("tex/water"), r, 0.6f, {r.x / 0.6f + t_ * 6, r.y / 0.6f + t_ * 4});
            DrawRectangleLinesEx(r, 2, alpha(rgba(0x2a5a2aff), 0.6f));
            DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, 8, alpha(BLACK, 0.25f), alpha(BLACK, 0));
        };
        water({7 * kT, 0, 10 * kT, kT * 0.8f});
        water({11 * kT + 8, 6 * kT, kT - 16, (kRows - 6) * kT});
        for (int i = 0; i < 18; i++) {  // reeds and lily pads
            float y = 6 * kT + 10 + i * 30.0f;
            DrawLineEx({11 * kT + 6, y}, {11 * kT + 2, y - 14}, 2, rgba(0x3a7a2aff));
            DrawLineEx({12 * kT - 6, y + 12}, {12 * kT - 2, y - 2}, 2, rgba(0x3a7a2aff));
            if (i % 3 == 0) DrawCircleSector({11.5f * kT + std::sin(i * 3.0f) * 6, y + 6}, 6, 30, 330, 10, rgba(0x4aa04aff));
        }
        // tulip field
        Rectangle tul{15 * kT, 13 * kT, 6 * kT, 2 * kT};
        fx::tiled(E().tex("tex/tulips"), tul, 0.5f);
        DrawRectangleLinesEx(tul, 2, alpha(rgba(0x3a2a1aff), 0.5f));
        // the road with kerbs
        for (int i = 0; i + 1 < kPathN; i++) {
            Vector2 a = tileC(kPath[i]), b = tileC(kPath[i + 1]);
            Rectangle r{std::min(a.x, b.x) - 21, std::min(a.y, b.y) - 21, std::abs(b.x - a.x) + 42, std::abs(b.y - a.y) + 42};
            DrawRectangleRec(shrink(r, -4), alpha(rgba(0x3a5a24ff), 0.6f));
        }
        for (int i = 0; i + 1 < kPathN; i++) {
            Vector2 a = tileC(kPath[i]), b = tileC(kPath[i + 1]);
            Rectangle r{std::min(a.x, b.x) - 21, std::min(a.y, b.y) - 21, std::abs(b.x - a.x) + 42, std::abs(b.y - a.y) + 42};
            fx::tiled(E().tex("tex/klinker"), r, 0.42f, {r.x / 0.42f, r.y / 0.42f});
        }
        for (int i = 0; i + 1 < kPathN; i++) {  // dashed centre line
            Vector2 a = tileC(kPath[i]), b = tileC(kPath[i + 1]);
            float len = Vector2Distance(a, b);
            Vector2 dir = (b - a) / len;
            for (float s = 10; s < len - 10; s += 30) DrawLineEx(a + dir * s, a + dir * (s + 14), 2.5f, alpha(WHITE, 0.45f));
        }
        // flight path of the drones (faint dotted line)
        if (wave_ >= 5) {
            Vector2 a = tileC(kFlyA), b = tileC(kFlyB);
            for (float k = 0; k < 1; k += 0.02f) DrawCircleV(Vector2Lerp(a, b, k), 1.6f, alpha(col::red, 0.25f));
        }
        // cloud shadows drifting over the polder
        for (int i = 0; i < 3; i++) {
            float x = std::fmod(t_ * 14 + i * 420, kMapW + 500) - 250;
            fx::softShadow({x, 140.0f + i * 220}, 200, 90, 0.12f);
        }
        // trees and grazing animals
        for (auto& a : animals_) {
            Vector2 c = tileC(a.at);
            float bob = std::sin(t_ * 1.5f + c.x) * 1;
            fx::softShadow({c.x, c.y + 14}, 22, 6, 0.3f);
            fx::sprite(E().tex(a.spr), {c.x, c.y + bob}, a.s, 0, WHITE, a.flip);
        }
        // Davi's van at the start of the road
        Vector2 v{18, tileC(kPath[0]).y + 52};
        DrawRectangleRounded({v.x - 40, v.y - 34, 74, 34}, 0.3f, 8, col::outline);
        DrawRectangleRounded({v.x - 38, v.y - 32, 70, 30}, 0.3f, 8, rgba(0x2a9ad8ff));
        DrawRectangleRec({v.x + 14, v.y - 30, 14, 12}, rgba(0x9ad0f0ff));
        ui::text("DAVI", v.x - 12, v.y - 17, 12, WHITE, ui::Center, true);
        DrawCircleV({v.x - 22, v.y}, 8, col::outline);
        DrawCircleV({v.x + 18, v.y}, 8, col::outline);
    }

    void drawFarm() {
        Vector2 base = tileC({19.5f, 2.6f});
        Texture2D& t = E().tex("spr/farm");
        float s = 0.5f;
        fx::softShadow({base.x, base.y + 8}, 60, 12, 0.4f);
        fx::sprite(t, {base.x, base.y - t.height * s / 2 + 10}, s);
        // the stash: a pile of stroopwafels, one per life
        int n = std::max(0, lives_);
        for (int i = 0; i < std::min(n, 20); i++) {
            Vector2 c{base.x - 28 + (i % 5) * 7.0f, base.y + 30 - (i / 5) * 5.0f};
            art::stroopwafel(c, 16, 5);
        }
        float shake = hurtT_ > 0 ? std::sin(t_ * 60) * 3 : 0;
        art::pieter({base.x + 30 + shake, base.y + 20}, 15, P().hat);
    }

    // ------------------------------------------------------------ draw: towers and foes
    void drawTower(const Tower& tw, bool ghost = false, float a = 1) {
        Vector2 p = tw.pos;
        Color tint = alpha(WHITE, a);
        int L = tw.lvl;
        if (!ghost) fx::softShadow({p.x, p.y + 16}, 22, 7, 0.4f);
        switch (tw.type) {
            case Klomp: {
                fx::sprite(E().tex("spr/tw_pedestal"), {p.x, p.y + 4}, 0.5f, 0, tint);
                Vector2 piv{p.x, p.y - 12};
                Vector2 dir = fromAngle(tw.aim, 1);
                Vector2 back = piv - dir * (6 + tw.recoil * 6);
                Vector2 tip = back + dir * (26 + L * 3);
                Color iron = alpha(L == 2 ? rgba(0xc8861aff) : rgba(0x3a3a46ff), a);
                DrawLineEx(back, tip, 13 + L * 2, alpha(col::outline, a));
                DrawLineEx(back, tip, 10 + L * 2, iron);
                DrawCircleV(tip, 7 + L, alpha(col::outline, a));
                DrawCircleV(tip, 5.5f + L, alpha(darken(iron, 0.4f), a));
                DrawLineEx(back + dir * 4 + Vector2{-dir.y, dir.x} * 2, tip - dir * 6 + Vector2{-dir.y, dir.x} * 2, 2, alpha(WHITE, 0.25f * a));
                DrawCircleV(piv, 7, alpha(rgba(0x8a5a3aff), a));
                // a little clog stack as ammo
                fx::sprite(E().tex("spr/tile_klomp"), {p.x + 14, p.y + 12}, 0.16f, 0, tint);
                break;
            }
            case Molen: {
                Texture2D& m = E().tex("spr/tw_mill");
                float s = 0.4f + L * 0.04f;
                Vector2 bottom{p.x, p.y + 18};
                fx::sprite(m, {bottom.x, bottom.y - m.height * s / 2}, s, 0, tint);
                Vector2 hub{bottom.x, bottom.y - (m.height - 29) * s};
                float len = 34 + L * 5;
                for (int k = 0; k < 4; k++) {
                    float ang = tw.sails + k * PI / 2;
                    Vector2 d = fromAngle(ang, 1), n{-d.y, d.x};
                    Vector2 e = hub + d * len;
                    DrawLineEx(hub, e, 3, alpha(rgba(0x4a2a14ff), a));
                    Vector2 q0 = hub + d * 8, q1 = e, q2 = e + n * 9, q3 = hub + d * 8 + n * 9;
                    Color sail = alpha(L == 2 ? rgba(0xff6a5aff) : rgba(0xf4ecd8ff), 0.92f * a);
                    DrawTriangle(q0, q2, q1, sail);
                    DrawTriangle(q0, q3, q2, sail);
                    for (int j = 1; j < 4; j++) DrawLineEx(q0 + d * (len - 8) * j / 4.0f, q0 + d * (len - 8) * j / 4.0f + n * 9, 1, alpha(rgba(0x4a2a14ff), a));
                    DrawLineEx(q3, q2, 1, alpha(rgba(0x4a2a14ff), a));
                }
                DrawCircleV(hub, 4, alpha(rgba(0x3a2410ff), a));
                break;
            }
            case Kaas: {
                // wooden frame + throwing arm
                Color wood = alpha(rgba(0x8a5a3aff), a), dark = alpha(rgba(0x4a2a14ff), a);
                DrawRectangleRounded({p.x - 20, p.y + 6, 40, 10}, 0.4f, 6, dark);
                DrawRectangleRounded({p.x - 18, p.y + 5, 36, 8}, 0.4f, 6, wood);
                DrawLineEx({p.x - 12, p.y + 8}, {p.x - 2, p.y - 12}, 5, dark);
                DrawLineEx({p.x + 12, p.y + 8}, {p.x + 2, p.y - 12}, 5, dark);
                for (float wx : {-16.0f, 16.0f}) {
                    DrawCircleV({p.x + wx, p.y + 16}, 6, alpha(col::outline, a));
                    DrawCircleV({p.x + wx, p.y + 16}, 4, wood);
                }
                float k = tw.anim > 0 ? (tw.anim > 0.38f ? (0.5f - tw.anim) / 0.12f : tw.anim / 0.38f) : 0;
                float ang = lerpf(-160, -60, clamp01(k)) * DEG2RAD;
                Vector2 piv{p.x, p.y - 10}, end = piv + fromAngle(ang, 28 + L * 3);
                DrawLineEx(piv - fromAngle(ang, 8), end, 6, dark);
                DrawLineEx(piv - fromAngle(ang, 8), end, 4, wood);
                DrawCircleV(piv, 4, alpha(rgba(0x3a3a46ff), a));
                DrawCircleV(end, 8, dark);
                if (tw.cd < 0.6f || ghost) fx::sprite(E().tex("spr/tile_cheese"), end + Vector2{0, -4}, 0.15f + L * 0.02f, 0, tint);
                if (L > 0) fx::sprite(E().tex("spr/tile_cheese"), {p.x + 22, p.y + 4}, 0.12f, 0, tint);
                if (L > 1) fx::sprite(E().tex("spr/tile_cheese"), {p.x + 24, p.y - 6}, 0.12f, 0, tint);
                break;
            }
            case Zap: {
                fx::sprite(E().tex("spr/tw_pedestal"), {p.x, p.y + 6}, 0.45f, 0, tint);
                for (int k = 0; k < 3 + L; k++) {
                    float y = p.y - 6 - k * 6;
                    DrawEllipse((int)p.x, (int)y, 11 - k, 4, alpha(col::outline, a));
                    DrawEllipse((int)p.x, (int)y, 9.5f - k, 3, alpha(rgba(0xd8823aff), a));
                }
                Vector2 top{p.x, p.y - 30 - L * 6};
                float pulse = 0.5f + 0.5f * std::sin(t_ * 6 + p.x);
                if (!ghost) {
                    fx::beginAdd();
                    fx::glow(top, 30 + tw.anim * 80, alpha(col::pink, (0.25f + 0.2f * pulse + tw.anim * 2) * a));
                    fx::endAdd();
                }
                fx::sprite(E().tex("spr/tile_tulip"), top, 0.28f + L * 0.03f, std::sin(t_ * 2 + p.y) * 5, tint);
                if (!ghost && chance(0.04f)) fx_.add(Particle{top + rndDir() * 10, rndDir() * 30, 0.2f, 0.2f, 2, 0, 0, 0, 0, 0, col::pink, BLANK, PShape::Spark});
                break;
            }
        }
        // level pips
        if (!ghost)
            for (int i = 0; i < L; i++) DrawPoly({p.x - 16 + i * 9.0f, p.y + 20}, 5, 4.5f, -90, col::gold);
    }

    void drawFoe(const Foe& f) {
        Vector2 p = f.pos;
        bool fly = kFoes[f.type].flying;
        float bob = fly ? std::sin(f.t * 5) * 3 : -std::abs(std::sin(f.t * 9)) * 3;
        Color tint = f.flash > 0 ? rgba(0xffa0a0ff) : WHITE;
        if (fly) fx::softShadow({p.x + 8, p.y + 26}, 16, 5, 0.3f);
        else fx::softShadow({p.x, p.y + 14}, 18 + (f.type >= DaviBoss ? 14 : 0), 5, 0.4f);
        bool flip = f.face < 0;
        switch (f.type) {
            case Router: fx::sprite(E().tex("spr/router"), {p.x, p.y + bob}, 0.36f, std::sin(f.t * 9) * 5, tint, flip); break;
            case Chair: fx::sprite(E().tex("spr/chair"), {p.x, p.y + bob * 0.3f}, 0.19f, f.t * 400, tint); break;
            case Printer: fx::sprite(E().tex("spr/printer"), {p.x, p.y + bob * 0.5f}, 0.38f, std::sin(f.t * 5) * 3, tint, flip); break;
            case Drone: fx::sprite(E().tex("spr/drone"), {p.x, p.y + bob - 8}, 0.34f, std::sin(f.t * 3) * 6, tint); break;
            case Monitor: {
                fx::sprite(E().tex("spr/turret"), {p.x, p.y + bob}, 0.34f, 0, tint, flip);
                float k = std::fmod(f.t, 1.5f) / 1.5f;
                ui::circleOutline(p, 20 + k * 60, alpha(col::green, 0.5f * (1 - k)), 2);
                break;
            }
            case DaviBoss:
                fx::sprite(E().tex("spr/chair"), {p.x, p.y}, 0.3f, std::sin(f.t * 2) * 6, tint);
                art::portrait("davi", {p.x, p.y - 12 + bob}, 22, f.flash > 0 ? col::red : col::cyan);
                break;
            case HorseBoss:
                art::horse({p.x, p.y + 16}, 0.5f, f.t * (f.dash > 0 ? 18 : 9), rgba(0x8a5a3aff), rgba(0x2a1a10ff), col::red, "none", f.face);
                if (f.flash > 0) DrawCircleV({p.x, p.y - 10}, 30, alpha(WHITE, 0.2f));
                break;
        }
        if (f.hp < f.maxHp) {
            float w = f.type >= DaviBoss ? 60 : 30;
            Rectangle r{p.x - w / 2, p.y - (f.type >= DaviBoss ? 50 : 28) - (fly ? 8 : 0), w, 5};
            DrawRectangleRec(shrink(r, -1), col::outline);
            DrawRectangleRec({r.x, r.y, r.width * clamp01(f.hp / f.maxHp), r.height}, f.slow > 0 ? col::cyan : f.hp / f.maxHp > 0.5f ? col::green : col::red);
        }
        if (f.slow > 0 && chance(0.05f)) fx_.add(Particle{p + Vector2{rnd(-14, 14), rnd(-14, 6)}, {-30, -10}, 0.4f, 0.4f, 2, 0, 0, 0, 0, 0, alpha(WHITE, 0.6f)});
    }

    void drawShots() {
        for (auto& s : shots_) {
            if (s.kind == 0) {
                fx::sprite(E().tex("spr/tile_klomp"), s.p, 0.14f, s.spin);
            } else {
                float k = s.t / s.dur;
                float h = std::sin(k * PI) * 90;
                fx::softShadow(s.p, 10, 4, 0.35f);
                fx::sprite(E().tex("spr/tile_cheese"), {s.p.x, s.p.y - h}, 0.17f, s.spin * 0.4f);
            }
        }
        fx::beginAdd();
        for (auto& b : bolts_) {
            float a = b.t / 0.18f;
            for (size_t i = 0; i + 1 < b.pts.size(); i++) {
                Vector2 p0 = b.pts[i], p1 = b.pts[i + 1];
                Vector2 prev = p0;
                for (int k = 1; k <= 6; k++) {
                    Vector2 q = Vector2Lerp(p0, p1, k / 6.0f);
                    if (k < 6) q += Vector2{rnd(-7, 7), rnd(-7, 7)};
                    fx::glowLine(prev, q, 8, alpha(col::pink, 0.8f * a));
                    prev = q;
                }
                fx::glow(p1, 26, alpha(col::pink, 0.8f * a));
            }
        }
        fx::endAdd();
        for (auto& b : bolts_)
            for (size_t i = 0; i + 1 < b.pts.size(); i++) DrawLineEx(b.pts[i], b.pts[i + 1], 1.5f, alpha(WHITE, b.t / 0.18f));
    }

    void draw() override {
        drawMap();
        // depth-sort towers and walking foes
        std::vector<std::pair<float, std::function<void()>>> layer;
        for (auto& tw : towers_) layer.push_back({tw.pos.y + 10, [&] { drawTower(tw); }});
        for (auto& f : foes_)
            if (!kFoes[f.type].flying) layer.push_back({f.pos.y, [&] { drawFoe(f); }});
        layer.push_back({tileC({19.5f, 2.6f}).y + 20, [&] { drawFarm(); }});
        for (int i = 0; i < (int)trees_.size(); i++) {
            Vector2 c = tileC(trees_[i]);
            layer.push_back({c.y + 16, [c] {
                                 fx::softShadow({c.x, c.y + 18}, 26, 8, 0.4f);
                                 fx::sprite(E().tex("spr/tree"), {c.x, c.y - 8}, 0.34f);
                             }});
        }
        std::stable_sort(layer.begin(), layer.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& l : layer) l.second();
        for (auto& f : foes_)
            if (kFoes[f.type].flying) drawFoe(f);
        drawShots();
        shocks_.draw();
        fx_.draw();
        drawCursor();
        if (hurtT_ > 0) DrawRectangleLinesEx({0, 0, kMapW, (float)VH}, 10 * hurtT_, alpha(col::red, hurtT_));
        if (banner_ > 0) {
            float k = std::min(1.0f, banner_ * 2) * std::min(1.0f, (2.2f - banner_) * 4);
            ui::textOutline(wave_ == kFinalWave ? "FINAL WAVE" : "WAVE " + std::to_string(wave_), kMapW / 2, 300, 70, alpha(WHITE, k), alpha(col::outline, k), 6);
        }
        drawSidebar();
        if (paused_) drawPause();
        if (phase_ == Phase::Lost) drawLost();
        if (phase_ == Phase::Won) drawWon();
    }

    void drawCursor() {
        Vector2 m = E().mouse();
        if (sel_ >= 0) {
            const Tower& tw = towers_[sel_];
            float r = kTowers[tw.type].range[tw.lvl];
            DrawCircleV(tw.pos, r, alpha(WHITE, 0.08f));
            ui::circleOutline(tw.pos, r, alpha(WHITE, 0.6f), 2);
            if (tw.lvl < 2) fx::dashedRing(tw.pos, kTowers[tw.type].range[tw.lvl + 1], 2, 40, t_ * 0.3f, alpha(col::gold, 0.5f));
        }
        if (build_ < 0 || m.x >= kMapW || paused_ || phase_ != Phase::Play) return;
        int cx = (int)(m.x / kT), cy = (int)(m.y / kT);
        if (cx < 0 || cy < 0 || cx >= kCols || cy >= kRows) return;
        for (int y = 0; y < kRows; y++)  // faint build grid
            for (int x = 0; x < kCols; x++)
                if (canPlace(x, y)) DrawRectangleLinesEx({x * kT + 1, y * kT + 1, kT - 2, kT - 2}, 1, alpha(WHITE, 0.07f));
        bool ok = canPlace(cx, cy) && money_ >= kTowers[build_].cost;
        Color c = ok ? col::green : col::red;
        Vector2 center = tileC({(float)cx, (float)cy});
        DrawRectangleRec({cx * kT, cy * kT, kT, kT}, alpha(c, 0.25f));
        float r = kTowers[build_].range[0];
        DrawCircleV(center, r, alpha(c, 0.08f));
        ui::circleOutline(center, r, alpha(c, 0.7f), 2);
        Tower ghost;
        ghost.type = build_;
        ghost.pos = center;
        drawTower(ghost, true, 0.65f);
    }

    // ------------------------------------------------------------ sidebar
    void towerIcon(int type, Vector2 c, float s) {
        switch (type) {
            case Klomp: fx::sprite(E().tex("spr/tile_klomp"), c, 0.36f * s); break;
            case Molen: fx::sprite(E().tex("spr/tw_mill"), c, 0.32f * s); break;
            case Kaas: fx::sprite(E().tex("spr/tile_cheese"), c, 0.36f * s); break;
            default: fx::sprite(E().tex("spr/tile_tulip"), c, 0.36f * s); break;
        }
    }

    void drawSidebar() {
        Rectangle sb{kMapW, 0, VW - kMapW, (float)VH};
        DrawRectangleRec(sb, rgba(0x150e26ff));
        DrawRectangleGradientH((int)kMapW - 14, 0, 14, VH, alpha(BLACK, 0), alpha(BLACK, 0.35f));
        DrawLineEx({kMapW, 0}, {kMapW, (float)VH}, 2, alpha(col::violet, 0.5f));
        float x = kMapW + 16, w = sb.width - 32;
        fx::gradientText("POLDER", x + w / 2, 30, 30, WHITE, rgba(0x9ad08aff), 3);
        fx::gradientText("DEFENSE", x + w / 2, 60, 22, rgba(0xffe08aff), col::orange, 3);
        // stats
        Rectangle st{x, 84, w, 92};
        DrawRectangleRounded(st, 0.2f, 8, alpha(WHITE, 0.05f));
        art::stroopwafel({x + 24, st.y + 24}, 26, 9);
        ui::text(std::to_string(lives_), x + 46, st.y + 24, 22, hurtT_ > 0 ? col::red : WHITE, ui::Left, true);
        ui::text("stash", x + 46 + ui::measure(std::to_string(lives_), 22, true) + 6, st.y + 26, 12, col::muted, ui::Left, true);
        ui::coin({x + w - 92, st.y + 24}, 11);
        ui::text(std::to_string(money_), x + w - 76, st.y + 24, 22, col::gold, ui::Left, true);
        ui::text(TextFormat("WAVE %d / %d", std::max(wave_, 0), kFinalWave), x + 12, st.y + 64, 18, WHITE, ui::Left, true);
        if (wave_ > kFinalWave) ui::text("ENDLESS", x + w - 12, st.y + 64, 14, col::orange, ui::Right, true);
        ui::bar({x + 12, st.y + 78, w - 24, 6}, clamp01(wave_ / (float)kFinalWave), col::green);
        // build buttons
        float by = 190;
        for (int i = 0; i < TCount; i++) {
            Rectangle r{x + (i % 2) * (w / 2 + 4), by + (i / 2) * 104.0f, w / 2 - 4, 96};
            bool afford = money_ >= kTowers[i].cost;
            bool over = ui::hovered(r) && !paused_ && phase_ == Phase::Play;
            bool on = build_ == i;
            DrawRectangleRounded(r, 0.18f, 8, on ? alpha(kTowers[i].color, 0.35f) : alpha(WHITE, over ? 0.1f : 0.05f));
            DrawRectangleRoundedLinesEx(r, 0.18f, 8, on ? 2.5f : 1, on ? kTowers[i].color : alpha(WHITE, 0.12f));
            towerIcon(i, {r.x + r.width / 2, r.y + 34}, 1);
            ui::text(std::to_string(i + 1), r.x + 10, r.y + 12, 12, col::muted, ui::Left, true);
            std::string nm = kTowers[i].name;
            float fs = ui::measure(nm, 11, true) > r.width - 8 ? 9.5f : 11;
            ui::text(nm, r.x + r.width / 2, r.y + 68, fs, WHITE, ui::Center, true);
            ui::coin({r.x + r.width / 2 - 16, r.y + 84}, 6);
            ui::text(std::to_string(kTowers[i].cost), r.x + r.width / 2 - 6, r.y + 84, 13, afford ? col::gold : col::red, ui::Left, true);
            if (!afford) DrawRectangleRounded(r, 0.18f, 8, alpha(col::bgDeep, 0.45f));
            if (over && E().mousePressed()) {
                E().consumeClick();
                E().sfx.play(Sfx::Click);
                build_ = on ? -1 : i;
                sel_ = -1;
            }
        }
        // info about the selected tower or the one being built
        Rectangle info{x, 404, w, 170};
        DrawRectangleRounded(info, 0.12f, 8, alpha(WHITE, 0.05f));
        int type = sel_ >= 0 ? towers_[sel_].type : build_;
        if (type >= 0) {
            const TDef& d = kTowers[type];
            int L = sel_ >= 0 ? towers_[sel_].lvl : 0;
            ui::text(d.name, info.x + 12, info.y + 18, 15, d.color, ui::Left, true);
            for (int i = 0; i < 3; i++) DrawPoly({info.x + w - 50 + i * 16.0f, info.y + 18}, 5, 6, -90, i <= L ? col::gold : alpha(WHITE, 0.15f));
            ui::paragraph(d.desc, {info.x + 12, info.y + 34, w - 24, 0}, 12, col::muted);
            std::string stat = type == Molen ? TextFormat("slow %d%%   %g dmg/s   range %d", (int)(d.extra[L] * 100), d.dmg[L], (int)d.range[L])
                               : type == Zap ? TextFormat("%g dmg x%d jumps   range %d", d.dmg[L], (int)d.extra[L], (int)d.range[L])
                                             : TextFormat("%g dmg   %.1f/s   range %d", d.dmg[L], d.rate[L], (int)d.range[L]);
            ui::text(stat, info.x + 12, info.y + 84, 12, WHITE, ui::Left, true);
            if (sel_ >= 0) {
                Tower& tw = towers_[sel_];
                ui::text(TextFormat("%d kills", tw.kills), info.x + 12, info.y + 102, 12, col::muted, ui::Left, true);
                if (tw.lvl < 2) {
                    int c = d.up[tw.lvl];
                    if (ui::button({info.x + 10, info.y + 118, w / 2 - 14, 42}, TextFormat("UP %d", c), money_ >= c ? col::green : col::panelHi, 17, money_ >= c)) upgrade(sel_);
                } else {
                    ui::text("MAX LEVEL", info.x + 10 + (w / 2 - 14) / 2, info.y + 139, 14, col::gold, ui::Center, true);
                }
                if (sel_ >= 0 && ui::button({info.x + w / 2 + 4, info.y + 118, w / 2 - 14, 42}, TextFormat("SELL %d", towers_[sel_].spent * 7 / 10), col::red, 17))
                    sell(sel_);
            } else {
                ui::text("click a grass tile to build", info.x + 12, info.y + 112, 12, col::muted, ui::Left, true);
                ui::text("hold SHIFT to build more", info.x + 12, info.y + 130, 12, col::muted, ui::Left, true);
            }
        } else {
            ui::text("BUILD", info.x + 12, info.y + 18, 15, WHITE, ui::Left, true);
            ui::paragraph("Pick a tower (1-4) and put it on the grass next to the road. Click a tower to upgrade or sell it. Davi's drones fly straight over the fields.",
                          {info.x + 12, info.y + 36, w - 24, 0}, 12, col::muted);
        }
        // wave button + speed + menu
        Rectangle wb{x, 586, w, 58};
        if (waveOn_) {
            int left = (int)queue_.size() + (int)foes_.size();
            DrawRectangleRounded(wb, 0.3f, 8, alpha(WHITE, 0.05f));
            ui::text(TextFormat("WAVE %d", wave_), wb.x + 14, wb.y + 20, 18, WHITE, ui::Left, true);
            ui::text(TextFormat("%d left", left), wb.x + 14, wb.y + 40, 12, col::muted, ui::Left, true);
        } else {
            std::string label = wave_ == 0 ? "START" : countdown_ > 0 ? TextFormat("NEXT  %d", (int)std::ceil(countdown_)) : "NEXT WAVE";
            if (ui::button(wb, label, rgba(0x2a9a5aff), 24) && phase_ == Phase::Play) startWave();
            ui::text("SPACE", wb.x + wb.width - 10, wb.y + wb.height + 10, 10, col::muted, ui::Right, true);
        }
        if (ui::button({x, 660, w / 2 - 4, 44}, TextFormat("%dx  F", speed_), col::panelHi, 18)) speed_ = speed_ >= 3 ? 1 : speed_ + 1;
        if (ui::button({x + w / 2 + 4, 660, w / 2 - 4, 44}, "PAUSE", col::panelHi, 18)) paused_ = true;
    }

    // ------------------------------------------------------------ overlays
    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 220, 200, 440, 300};
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::green, 0.6f));
        ui::textOutline("PAUSED", VW / 2.0f, p.y + 56, 56, WHITE, col::outline, 5);
        if (ui::button({p.x + 40, p.y + 110, 360, 50}, "RESUME", col::green, 22)) paused_ = false;
        if (ui::button({p.x + 40, p.y + 170, 360, 50}, "RESTART", col::panelHi, 22)) reset();
        if (ui::button({p.x + 40, p.y + 230, 360, 50}, "MENU", col::red, 22)) E().go(makeMenu);
    }

    void drawLost() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 300, 120, 600, 430};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::red, 0.6f));
        ui::textOutline("THE POLDER FELL", VW / 2.0f, p.y + 64, 56, rgba(0xffb0a0ff), col::outline, 5);
        ui::text("Davi's elektronik ate every last stroopwafel.", VW / 2.0f, p.y + 116, 18, col::muted, ui::Center, true);
        ui::textShadow(TextFormat("%d", cleared_), VW / 2.0f, p.y + 184, 72, WHITE, ui::Center, true, 4);
        ui::text(cleared_ == 1 ? "wave survived" : "waves survived", VW / 2.0f, p.y + 230, 18, col::text, ui::Center, true);
        if (cleared_ >= P().stats.polderBest && cleared_ > 0 && std::fmod(t_, 0.8f) < 0.55f) ui::text("PERSONAL BEST!", VW / 2.0f, p.y + 258, 20, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 296}, 15);
        ui::text("+" + fmtNum(earnedTotal_) + " Pietercoins", VW / 2.0f - 66, p.y + 296, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 344, 250, 64}, "TRY AGAIN", rgba(0x2a9a5aff), 24)) reset();
            if (ui::button({p.x + 310, p.y + 344, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void drawWon() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.5f));
        Rectangle p{VW / 2.0f - 320, 110, 640, 460};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::gold, 0.7f));
        fx::gradientText("POLDER SAVED!", VW / 2.0f, p.y + 64, 64, WHITE, col::gold, 5);
        art::pieter({VW / 2.0f, p.y + 160}, 50, P().hat, std::sin(t_ * 3) * 8);
        ui::text("Twenty waves, and the stroopwafels are still there. Even the Horse went home.", VW / 2.0f, p.y + 236, 16, col::text, ui::Center, true);
        ui::text(TextFormat("%d stroopwafels left in the stash", lives_), VW / 2.0f, p.y + 264, 18, col::muted, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 306}, 15);
        ui::text("+" + fmtNum(earnedTotal_) + " Pietercoins", VW / 2.0f - 66, p.y + 306, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 360, 270, 64}, "KEEP GOING", rgba(0x2a9a5aff), 22)) {
                phase_ = Phase::Play;
                countdown_ = 14;
            }
            if (ui::button({p.x + 330, p.y + 360, 270, 64}, "MENU", col::panelHi, 22)) E().go(makeMenu);
        }
    }

private:
    struct Animal { const char* spr; Vector2 at; float s; bool flip; };
    int cells_[kRows][kCols]{};
    std::vector<float> segLen_;
    float pathLen_ = 0, flyLen_ = 0;
    std::vector<Vector2> trees_;
    std::vector<Animal> animals_;
    std::vector<Tower> towers_;
    std::vector<Foe> foes_;
    std::vector<Shot> shots_;
    std::vector<size_t> shotOwner_;  // index into towers_ per shot (SIZE_MAX once the tower is sold)
    std::vector<Bolt> bolts_;
    std::vector<Spawn> queue_;
    std::vector<std::pair<int, float>> pending_;
    fx::Shocks shocks_;
    Phase phase_ = Phase::Play;
    int money_ = 200, lives_ = 20, wave_ = 0, cleared_ = 0, build_ = -1, sel_ = -1, nextId_ = 1, speed_ = 1;
    long long earnedTotal_ = 0;
    float t_ = 0, phaseT_ = 0, waveT_ = 0, countdown_ = -1, banner_ = 0, hurtT_ = 0;
    bool waveOn_ = false, paused_ = false, wonShown_ = false, bot_ = false;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makePolder() { return std::make_unique<PolderScene>(); }

}  // namespace pg
