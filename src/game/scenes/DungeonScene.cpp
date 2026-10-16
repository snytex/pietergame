// DAVI'S DUNGEON: twin-stick roguelite under Davi's house. Clear rooms of
// "useless elektronik", grab upgrades in treasure rooms, beat Davi on his
// gaming chair, take the stairs down. WASD move, mouse aim + shoot, SPACE dash.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <map>

namespace pg {
namespace {

constexpr Rectangle kRoom{90, 110, 1100, 560};  // floor area inside the walls
constexpr float kWall = 44;

enum class RoomType { Start, Fight, Treasure, Boss };
enum class EType { Router, Drone, Turret, Printer, Davi };

struct Room {
    int gx, gy;
    RoomType type = RoomType::Fight;
    bool cleared = false, visited = false, taken = false;
    bool door[4] = {false, false, false, false};  // N E S W
    int seed = 0;
};

struct Enemy {
    EType type;
    Vector2 pos, vel{};
    float hp, maxHp, r;
    float t = 0, cd = 1, flash = 0;
    int state = 0;
};

struct Shot {
    Vector2 pos, vel;
    float r, life, dmg;
    bool enemy;
    int pierce = 0;
    Color color;
};

struct Upgrade { const char* name; const char* desc; Color color; };
const Upgrade kUpgrades[] = {{"Extra Boba", "+1 damage", col::boba},
                             {"Game Fuel", "+15% move speed", col::gold},
                             {"Turbo Straw", "+25% fire rate", col::cyan},
                             {"Triple Pearl", "+2 pearls per shot", col::pink},
                             {"Sharp Tapioca", "pearls pierce 1 more enemy", col::violet},
                             {"Stroopwafel Heart", "+1 max heart, heal 1", col::red},
                             {"Big Boba", "bigger, faster pearls", col::orange},
                             {"Dash Shoes", "dash recharges twice as fast", col::green}};
constexpr int kUpgradeCount = 8;

enum class Phase { Title, Play, Pick, Dead, Descend };

class DungeonScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Title) return {"Davi's Dungeon", "At the top of the stairs"};
        return {"Davi's Dungeon", "Floor " + std::to_string(floor_)};
    }

    void enter() override {
        if (std::getenv("PG_DUNGEON")) {  // dev hook: straight into a run
            newRun();
            if (std::string(std::getenv("PG_DUNGEON")) == "boss") {
                for (auto& [k, r] : rooms_)
                    if (r.type == RoomType::Boss) enterRoom(k, -1);
            }
        }
    }

    // ------------------------------------------------------------ run setup
    void newRun() {
        floor_ = 1;
        maxHp_ = 6;
        hp_ = 6;
        dmg_ = 1;
        speed_ = 300;
        fireRate_ = 3.5f;
        pearls_ = 1;
        pierce_ = 0;
        shotSize_ = 7;
        shotSpeed_ = 640;
        dashRate_ = 1;
        coins_ = 0;
        owned_.clear();
        newFloor();
        phase_ = Phase::Play;
    }

    static int key(int x, int y) { return y * 100 + x; }

    void newFloor() {
        rooms_.clear();
        int n = 6 + floor_ + rndi(0, 2);
        std::vector<std::pair<int, int>> cells = {{0, 0}};
        rooms_[key(0, 0)] = Room{0, 0, RoomType::Start};
        while ((int)cells.size() < n) {
            auto [cx, cy] = cells[rndi(0, (int)cells.size() - 1)];
            const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            int d = rndi(0, 3);
            int nx = cx + dx[d], ny = cy + dy[d];
            if (rooms_.count(key(nx, ny)) || std::abs(nx) > 4 || std::abs(ny) > 3) continue;
            rooms_[key(nx, ny)] = Room{nx, ny, RoomType::Fight};
            cells.push_back({nx, ny});
        }
        // doors between neighbours
        for (auto& [k, r] : rooms_) {
            const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            for (int d = 0; d < 4; d++) r.door[d] = rooms_.count(key(r.gx + dx[d], r.gy + dy[d])) > 0;
            r.seed = rndi(0, 100000);
        }
        // boss = furthest room, treasure = a random dead end
        int bossK = key(0, 0), bestD = -1;
        for (auto& [k, r] : rooms_) {
            int d = std::abs(r.gx) + std::abs(r.gy);
            if (d > bestD) { bestD = d; bossK = k; }
        }
        rooms_[bossK].type = RoomType::Boss;
        for (auto& [k, r] : rooms_)
            if (r.type == RoomType::Fight && (r.door[0] + r.door[1] + r.door[2] + r.door[3]) == 1) {
                r.type = RoomType::Treasure;
                break;
            }
        enterRoom(key(0, 0), -1);
    }

    void enterRoom(int k, int fromDir) {
        cur_ = k;
        Room& r = rooms_[k];
        r.visited = true;
        enemies_.clear();
        shots_.clear();
        pickups_.clear();
        // stand in front of the door you came through
        Vector2 c = center(kRoom);
        switch (fromDir) {
            case 0: player_ = {c.x, kRoom.y + kRoom.height - 60}; break;  // came in going north -> enter at the south door
            case 1: player_ = {kRoom.x + 60, c.y}; break;
            case 2: player_ = {c.x, kRoom.y + 60}; break;
            case 3: player_ = {kRoom.x + kRoom.width - 60, c.y}; break;
            default: player_ = c; break;
        }
        if (r.cleared) return;
        if (r.type == RoomType::Fight) {
            int n = 3 + floor_ + rndi(0, 2);
            for (int i = 0; i < n; i++) {
                EType t = (EType)std::min(3, rndi(0, std::min(3, floor_ + 1)));
                spawn(t, randomSpot());
            }
            E().sfx.play(Sfx::Slam, 0.6f);
            roomIntro_ = 0.8f;
        } else if (r.type == RoomType::Boss) {
            spawn(EType::Davi, {c.x, kRoom.y + 140});
            E().sfx.play(Sfx::Slam, 0.8f);
            E().shake(8);
            bossIntro_ = 2.0f;
        } else {
            r.cleared = true;
        }
    }

    Vector2 randomSpot() {
        for (int tries = 0; tries < 30; tries++) {
            Vector2 p{rnd(kRoom.x + 80, kRoom.x + kRoom.width - 80), rnd(kRoom.y + 80, kRoom.y + kRoom.height - 80)};
            if (Vector2Distance(p, player_) > 260) return p;
        }
        return {kRoom.x + 100, kRoom.y + 100};
    }

    void spawn(EType t, Vector2 p) {
        Enemy e{t, p};
        float hpMul = 1 + (floor_ - 1) * 0.35f;
        switch (t) {
            case EType::Router: e.hp = 4; e.r = 22; break;
            case EType::Drone: e.hp = 3; e.r = 18; break;
            case EType::Turret: e.hp = 7; e.r = 26; break;
            case EType::Printer: e.hp = 9; e.r = 30; break;
            case EType::Davi: e.hp = 70; e.r = 56; hpMul = 1 + (floor_ - 1) * 0.5f; break;
        }
        e.hp *= hpMul;
        e.maxHp = e.hp;
        e.cd = rnd(0.8f, 1.8f);
        enemies_.push_back(e);
        fx_.burst(p, 16, col::violet, 200, 5, 0.5f, PShape::Star);
    }

    // ------------------------------------------------------------ combat
    void fire(Vector2 from, Vector2 dir, float speed, float r, float dmg, bool enemy, Color c, int pierce = 0) {
        shots_.push_back({from, dir * speed, r, enemy ? 4.0f : 0.9f, dmg, enemy, pierce, c});
    }

    void ring(Vector2 from, int n, float speed, float off, Color c) {
        for (int i = 0; i < n; i++) fire(from, fromAngle(off + i * 2 * PI / n), speed, 9, 1, true, c);
    }

    void hurtPlayer() {
        if (iframes_ > 0 || dashT_ > 0) return;
        hp_--;
        iframes_ = 1.2f;
        E().sfx.play(Sfx::Hurt);
        E().shake(10);
        E().flash(col::red, 0.25f);
        if (hp_ <= 0) {
            phase_ = Phase::Dead;
            phaseT_ = 0;
            bool best = floor_ > P().stats.dungeonBest;
            P().stats.dungeonBest = std::max(P().stats.dungeonBest, floor_);
            earned_ = coins_ + (floor_ - 1) * 40;
            if (earned_ > 0) P().earn(earned_);
            P().save();
            newBest_ = best;
            E().sfx.play(Sfx::Lose);
        }
    }

    void killEnemy(Enemy& e) {
        e.hp = 0;
        fx_.burst(e.pos, 24, col::orange, 340, 6, 0.6f);
        fx_.burst(e.pos, 10, rgba(0x8a8aa0ff), 260, 7, 0.8f, PShape::Square, 600);  // gears
        E().sfx.play(e.type == EType::Davi ? Sfx::Boom : Sfx::Drop, e.type == EType::Davi ? 1.0f : 0.6f, e.type == EType::Davi ? 0.8f : 1.3f);
        if (e.type == EType::Davi) {
            E().shake(20);
            fx_.confetti(e.pos, 120);
            coins_ += 50;
        }
        if (chance(0.45f)) pickups_.push_back({e.pos + rndDir() * 10.0f, chance(0.18f) ? 1 : 0});
    }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        phaseT_ += dt;
        if (phase_ == Phase::Title) {
            if (E().backPressed()) E().go(makeMenu);
            return;
        }
        if (phase_ == Phase::Dead) {
            if (E().backPressed()) phase_ = Phase::Title;
            return;
        }
        if (phase_ == Phase::Descend) {
            if (phaseT_ > 1.2f) {
                floor_++;
                newFloor();
                phase_ = Phase::Play;
                fx_.clear();
            }
            return;
        }
        if (phase_ == Phase::Pick) return;
        if (E().backPressed()) { phase_ = Phase::Title; return; }

        Room& room = rooms_[cur_];
        roomIntro_ = std::max(0.0f, roomIntro_ - dt);
        bossIntro_ = std::max(0.0f, bossIntro_ - dt);
        iframes_ = std::max(0.0f, iframes_ - dt);

        // --- player movement + dash
        Vector2 in{(float)((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))),
                   (float)((IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)))};
        if (Vector2Length(in) > 0) {
            in = Vector2Normalize(in);
            lastMove_ = in;
        }
        dashCd_ = std::max(0.0f, dashCd_ - dt * dashRate_);
        if (IsKeyPressed(KEY_SPACE) && dashCd_ <= 0) {
            dashT_ = 0.18f;
            dashCd_ = 1.0f;
            dashDir_ = Vector2Length(in) > 0 ? in : lastMove_;
            E().sfx.play(Sfx::Dash, 0.6f);
        }
        Vector2 vel = in * speed_;
        if (dashT_ > 0) {
            dashT_ -= dt;
            vel = dashDir_ * 900;
            if (rndi(0, 1) == 0) {
                Particle p;
                p.pos = player_;
                p.life = p.maxLife = 0.25f;
                p.size = 22;
                p.endSize = 4;
                p.color = alpha(col::violet, 0.4f);
                fx_.add(p);
            }
        }
        player_ += vel * dt;
        bool locked = !room.cleared;
        // walls, with door gaps when unlocked
        auto clampAxis = [&](float& v, float lo, float hi, bool gap) {
            if (gap) return;
            v = std::clamp(v, lo, hi);
        };
        Vector2 c = center(kRoom);
        bool inVDoor = std::abs(player_.x - c.x) < 46, inHDoor = std::abs(player_.y - c.y) < 46;
        clampAxis(player_.y, kRoom.y + 18, 1e9f, !locked && room.door[0] && inVDoor);
        clampAxis(player_.y, -1e9f, kRoom.y + kRoom.height - 18, !locked && room.door[2] && inVDoor);
        clampAxis(player_.x, kRoom.x + 18, 1e9f, !locked && room.door[3] && inHDoor);
        clampAxis(player_.x, -1e9f, kRoom.x + kRoom.width - 18, !locked && room.door[1] && inHDoor);
        player_.x = std::clamp(player_.x, kRoom.x - 40, kRoom.x + kRoom.width + 40);
        player_.y = std::clamp(player_.y, kRoom.y - 40, kRoom.y + kRoom.height + 40);
        // walked through a door?
        Room cr = room;
        const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        int through = player_.y < kRoom.y - 10 ? 0 : player_.x > kRoom.x + kRoom.width + 10 ? 1 : player_.y > kRoom.y + kRoom.height + 10 ? 2 : player_.x < kRoom.x - 10 ? 3 : -1;
        if (through >= 0) {
            enterRoom(key(cr.gx + dx[through], cr.gy + dy[through]), through);
            E().sfx.play(Sfx::Step, 0.5f);
            return;
        }

        // --- shooting at the mouse
        fireCd_ -= dt;
        Vector2 aim = Vector2Normalize(E().mouse() - player_);
        if (E().mouseDown() && fireCd_ <= 0 && bossIntro_ <= 0) {
            fireCd_ = 1 / fireRate_;
            for (int i = 0; i < pearls_; i++) {
                float spread = pearls_ > 1 ? (i - (pearls_ - 1) / 2.0f) * 0.14f : 0;
                fire(player_ + aim * 26, Vector2Rotate(aim, spread), shotSpeed_, shotSize_, (float)dmg_, false, rgba(0x3a2418ff), pierce_);
            }
            E().sfx.play(Sfx::Pop, 0.25f, rnd(1.1f, 1.4f));
        }

        // --- enemies
        for (auto& e : enemies_) {
            if (e.hp <= 0) continue;
            e.t += dt;
            e.cd -= dt;
            e.flash = std::max(0.0f, e.flash - dt * 6);
            if (roomIntro_ > 0 || bossIntro_ > 0) continue;
            Vector2 to = player_ - e.pos;
            float dist = Vector2Length(to);
            Vector2 dir = dist > 0 ? to / dist : Vector2{0, 1};
            float mul = 1 + (floor_ - 1) * 0.12f;
            switch (e.type) {
                case EType::Router:  // walks at you, blinks, fires a pair of packets
                    e.vel = damp(e.vel, dir * 120 * mul, 3, dt);
                    if (e.cd <= 0) {
                        e.cd = 2.0f / mul;
                        for (int s : {-1, 1}) fire(e.pos, Vector2Rotate(dir, s * 0.18f), 260, 8, 1, true, col::green);
                    }
                    break;
                case EType::Drone: {  // circles you, then dashes in
                    if (e.state == 0) {
                        Vector2 tang{-dir.y, dir.x};
                        e.vel = damp(e.vel, tang * 220 + dir * (dist > 220 ? 120.0f : -80.0f), 4, dt);
                        if (e.cd <= 0) { e.state = 1; e.cd = 0.5f; }
                    } else if (e.state == 1) {
                        e.vel = damp(e.vel, Vector2{0, 0}, 10, dt);  // wind up
                        if (e.cd <= 0) { e.state = 2; e.cd = 0.45f; e.vel = dir * 620 * mul; }
                    } else if (e.cd <= 0) {
                        e.state = 0;
                        e.cd = rnd(1.4f, 2.4f);
                    }
                    break;
                }
                case EType::Turret:  // old monitor: stands still, sprays spirals
                    e.vel = {};
                    if (e.cd <= 0) {
                        e.cd = 1.7f / mul;
                        ring(e.pos, 8 + floor_, 200, e.t, col::cyan);
                    }
                    break;
                case EType::Printer:  // slow, spits a fan of paper
                    e.vel = damp(e.vel, dir * 60, 2, dt);
                    if (e.cd <= 0) {
                        e.cd = 2.4f / mul;
                        for (int i = -2; i <= 2; i++) fire(e.pos, Vector2Rotate(dir, i * 0.22f), 300, 10, 1, true, WHITE);
                        E().sfx.play(Sfx::Card, 0.4f);
                    }
                    break;
                case EType::Davi: {  // rolling chair boss: charges, spins, calls minions
                    bool rage = e.hp < e.maxHp * 0.5f;
                    if (e.state == 0) {  // drift + aimed controller bursts
                        e.vel = damp(e.vel, Vector2{std::sin(e.t * 0.8f) * 200, std::cos(e.t * 0.6f) * 80}, 2, dt);
                        if (e.cd <= 0) {
                            e.cd = rage ? 0.7f : 1.0f;
                            for (int i = -1; i <= 1; i++) fire(e.pos, Vector2Rotate(dir, i * 0.2f), 340, 11, 1, true, col::cyan);
                            if (rndi(0, 3) == 0) { e.state = 1; e.cd = 0.8f; }
                        }
                    } else if (e.state == 1) {  // wind up, then charge
                        e.vel = damp(e.vel, Vector2{0, 0}, 8, dt);
                        if (e.cd <= 0) { e.state = 2; e.cd = 0.7f; e.vel = dir * (rage ? 900.0f : 760.0f); E().sfx.play(Sfx::Dash, 0.8f); }
                    } else if (e.state == 2) {
                        if (e.cd <= 0) { e.state = 3; e.cd = 1.2f; }
                    } else {  // spin attack: rings of controllers
                        e.vel = damp(e.vel, Vector2{0, 0}, 5, dt);
                        if (std::fmod(e.t, 0.3f) < dt) ring(e.pos, rage ? 14 : 10, 230, e.t * 2, col::violet);
                        if (e.cd <= 0) {
                            e.state = 0;
                            e.cd = 1.2f;
                            if (rage && (int)enemies_.size() < 5) spawn(EType::Drone, e.pos + rndDir() * 80.0f);
                        }
                    }
                    break;
                }
            }
            e.pos += e.vel * dt;
            // bounce off walls
            if (e.pos.x < kRoom.x + e.r || e.pos.x > kRoom.x + kRoom.width - e.r) { e.vel.x *= -1; e.pos.x = std::clamp(e.pos.x, kRoom.x + e.r, kRoom.x + kRoom.width - e.r); }
            if (e.pos.y < kRoom.y + e.r || e.pos.y > kRoom.y + kRoom.height - e.r) { e.vel.y *= -1; e.pos.y = std::clamp(e.pos.y, kRoom.y + e.r, kRoom.y + kRoom.height - e.r); }
            if (Vector2Distance(e.pos, player_) < e.r + 16) hurtPlayer();
            if (phase_ != Phase::Play) return;
        }

        // --- shots
        for (auto& s : shots_) {
            s.pos += s.vel * dt;
            s.life -= dt;
            if (!CheckCollisionPointRec(s.pos, kRoom)) {
                s.life = 0;
                fx_.burst(s.pos, 4, s.color, 100, 3, 0.2f);
            }
            if (s.life <= 0) continue;
            if (s.enemy) {
                if (Vector2Distance(s.pos, player_) < s.r + 12) {
                    s.life = 0;
                    hurtPlayer();
                    if (phase_ != Phase::Play) return;
                }
            } else {
                for (auto& e : enemies_) {
                    if (e.hp <= 0 || Vector2Distance(s.pos, e.pos) > e.r + s.r) continue;
                    e.hp -= s.dmg;
                    e.flash = 1;
                    e.vel += Vector2Normalize(s.vel) * (e.type == EType::Davi ? 20.0f : 120.0f);
                    fx_.burst(s.pos, 5, col::gold, 160, 3, 0.25f, PShape::Spark);
                    if (e.hp <= 0) killEnemy(e);
                    if (s.pierce-- <= 0) { s.life = 0; break; }
                }
            }
        }
        std::erase_if(shots_, [](auto& s) { return s.life <= 0; });
        std::erase_if(enemies_, [](auto& e) { return e.hp <= 0; });

        // --- pickups
        for (auto& p : pickups_) {
            if (Vector2Distance(p.pos, player_) < 30) {
                if (p.kind == 1) {
                    if (hp_ < maxHp_) { hp_ = std::min(maxHp_, hp_ + 2); E().sfx.play(Sfx::Gem, 0.6f); p.kind = -1; }
                } else {
                    coins_ += 3;
                    E().sfx.play(Sfx::Coin, 0.5f);
                    p.kind = -1;
                }
            }
        }
        std::erase_if(pickups_, [](auto& p) { return p.kind < 0; });

        // --- room cleared?
        if (!room.cleared && enemies_.empty() && roomIntro_ <= 0) {
            room.cleared = true;
            E().sfx.play(Sfx::Bell, 0.6f, 1.2f);
            fx_.floatText("ROOM CLEARED", {center(kRoom).x, kRoom.y + 60}, col::green, 30);
        }
        // treasure: walk onto the pedestal
        if (room.type == RoomType::Treasure && !room.taken && Vector2Distance(player_, center(kRoom)) < 50) {
            room.taken = true;
            rollUpgrades();
            phase_ = Phase::Pick;
            E().sfx.play(Sfx::Power);
        }
        // stairs after the boss
        if (room.type == RoomType::Boss && room.cleared && Vector2Distance(player_, center(kRoom)) < 44) {
            phase_ = Phase::Descend;
            phaseT_ = 0;
            E().sfx.play(Sfx::Whoosh);
        }
    }

    void rollUpgrades() {
        std::vector<int> all;
        for (int i = 0; i < kUpgradeCount; i++) all.push_back(i);
        std::shuffle(all.begin(), all.end(), rng());
        for (int i = 0; i < 3; i++) offer_[i] = all[i];
    }

    void apply(int u) {
        owned_.push_back(u);
        switch (u) {
            case 0: dmg_ += 1; break;
            case 1: speed_ *= 1.15f; break;
            case 2: fireRate_ *= 1.25f; break;
            case 3: pearls_ += 2; break;
            case 4: pierce_ += 1; break;
            case 5: maxHp_ += 2; hp_ = std::min(maxHp_, hp_ + 2); break;
            case 6: shotSize_ += 3; shotSpeed_ *= 1.2f; break;
            default: dashRate_ *= 2; break;
        }
        phase_ = Phase::Play;
        E().sfx.play(Sfx::Perfect);
        fx_.burst(player_, 30, kUpgrades[u].color, 300, 6, 0.6f, PShape::Star);
    }

    // ------------------------------------------------------------ draw
    void drawRoom() {
        const Room& room = rooms_.at(cur_);
        Color stone = room.type == RoomType::Boss ? rgba(0x5a2a3aff) : room.type == RoomType::Treasure ? rgba(0x5a4a2aff) : rgba(0x4a4060ff);
        DrawRectangle(0, 0, VW, VH, rgba(0x0c0814ff));
        // outer walls (top face lit, side faces darker)
        Rectangle outer{kRoom.x - kWall, kRoom.y - kWall, kRoom.width + 2 * kWall, kRoom.height + 2 * kWall};
        DrawRectangleRec(outer, rgba(0x1e1828ff));
        DrawRectangleRec({outer.x, outer.y, outer.width, kWall}, rgba(0x3a3048ff));  // lit top wall face
        DrawRectangleLinesEx(shrink(outer, 2), 3, alpha(BLACK, 0.5f));
        for (int r = 0; r < 3; r++)
            for (float x = outer.x + (r % 2) * 30; x < outer.x + outer.width; x += 60) DrawRectangleLinesEx({x, outer.y + r * 14.0f, 60, 14}, 1, alpha(BLACK, 0.3f));
        DrawRectangleRec(kRoom, stone);
        DrawRectangleLinesEx(shrink(kRoom, -3), 3, rgba(0x0c0814ff));
        // floor tiles with variation
        for (float y = kRoom.y; y < kRoom.y + kRoom.height; y += 56)
            for (float x = kRoom.x; x < kRoom.x + kRoom.width; x += 56) {
                int h = (int)(x * 7 + y * 13 + room.seed) % 7;
                DrawRectangleRec({x + 1, y + 1, 54, 54}, alpha(h < 2 ? BLACK : WHITE, h < 2 ? 0.12f : 0.025f));
                if (h == 3) DrawLineEx({x + 10, y + 20}, {x + 30, y + 26}, 1.5f, alpha(BLACK, 0.3f));  // crack
            }
        DrawRectangleGradientV((int)kRoom.x, (int)kRoom.y, (int)kRoom.width, 40, alpha(BLACK, 0.45f), alpha(BLACK, 0));
        // doors
        Vector2 c = center(kRoom);
        bool open = room.cleared;
        auto door = [&](Rectangle r, bool vertical) {
            DrawRectangleRec(r, rgba(0x0c0814ff));
            if (!open) {
                DrawRectangleRec(r, rgba(0x5a3a28ff));
                for (int i = 1; i < 4; i++) {
                    if (vertical) DrawLineEx({r.x, r.y + i * r.height / 4}, {r.x + r.width, r.y + i * r.height / 4}, 3, rgba(0x2a1a10ff));
                    else DrawLineEx({r.x + i * r.width / 4, r.y}, {r.x + i * r.width / 4, r.y + r.height}, 3, rgba(0x2a1a10ff));
                }
            }
        };
        if (room.door[0]) door({c.x - 46, kRoom.y - kWall, 92, kWall}, false);
        if (room.door[2]) door({c.x - 46, kRoom.y + kRoom.height, 92, kWall}, false);
        if (room.door[3]) door({kRoom.x - kWall, c.y - 46, kWall, 92}, true);
        if (room.door[1]) door({kRoom.x + kRoom.width, c.y - 46, kWall, 92}, true);
        // torches in the corners
        const Vector2 torches[] = {{kRoom.x + 30, kRoom.y - 14}, {kRoom.x + kRoom.width - 30, kRoom.y - 14}};
        fx::beginAdd();
        for (auto& tp : torches) fx::glow(tp, 140 + std::sin(t_ * 9 + tp.x) * 10, alpha(col::orange, 0.35f));
        fx::endAdd();
        for (auto& tp : torches) DrawCircleV(tp, 6, col::gold);
        // room features
        if (room.type == RoomType::Treasure) {
            DrawRectangleRounded({c.x - 40, c.y - 20, 80, 40}, 0.3f, 8, rgba(0x6a5a3aff));
            if (!room.taken) {
                fx::beginAdd();
                fx::glow({c.x, c.y - 30}, 90, alpha(col::gold, 0.5f + 0.2f * std::sin(t_ * 4)));
                fx::endAdd();
                art::itemIcon("boba", {c.x, c.y - 40 + std::sin(t_ * 3) * 5}, 60);
            }
        }
        if (room.type == RoomType::Boss && room.cleared) {
            DrawRectangleRec({c.x - 40, c.y - 40, 80, 80}, rgba(0x0c0814ff));
            for (int i = 0; i < 4; i++) DrawRectangleRec({c.x - 40 + i * 10.0f, c.y - 40 + i * 20.0f, 80 - i * 20.0f, 20}, rgba(0x3a3448ff));
            ui::text("DOWN", c.x, c.y - 56, 16, col::gold, ui::Center, true);
        }
    }

    void drawEnemy(const Enemy& e) {
        Vector2 p = e.pos;
        fx::softShadow({p.x, p.y + e.r * 0.8f}, e.r, e.r * 0.3f, 0.5f);
        Color fl = e.flash > 0 ? WHITE : BLANK;
        switch (e.type) {
            case EType::Router: {
                DrawRectangleRounded({p.x - 22, p.y - 14, 44, 28}, 0.3f, 6, col::outline);
                DrawRectangleRounded({p.x - 20, p.y - 12, 40, 24}, 0.3f, 6, e.flash > 0 ? WHITE : rgba(0x3a3a4aff));
                for (int s : {-1, 1}) DrawLineEx({p.x + s * 14, p.y - 12}, {p.x + s * 18, p.y - 30}, 3, col::outline);
                for (int i = 0; i < 3; i++) DrawCircleV({p.x - 10 + i * 10.0f, p.y + 4}, 3, std::fmod(t_ * 4 + i, 1.0f) < 0.5f ? col::green : rgba(0x1a4a1aff));
                // angry eyes
                DrawRectangleRec({p.x - 12, p.y - 7, 8, 3}, col::red);
                DrawRectangleRec({p.x + 4, p.y - 7, 8, 3}, col::red);
                break;
            }
            case EType::Drone: {
                float spin = t_ * 30;
                for (int i = 0; i < 4; i++) {
                    Vector2 arm = p + fromAngle(PI / 4 + i * PI / 2, 18);
                    DrawLineEx(p, arm, 3, col::outline);
                    DrawEllipse((int)arm.x, (int)arm.y, 10 * std::abs(std::sin(spin + i)), 3, alpha(WHITE, 0.6f));
                }
                DrawCircleV(p, 12, col::outline);
                DrawCircleV(p, 10, e.flash > 0 ? WHITE : (e.state == 1 ? col::red : rgba(0x5a5a6aff)));
                DrawCircleV(p, 4, col::red);
                break;
            }
            case EType::Turret: {
                DrawRectangleRounded({p.x - 28, p.y - 22, 56, 42}, 0.12f, 6, col::outline);
                DrawRectangleRec({p.x - 24, p.y - 18, 48, 32}, e.flash > 0 ? WHITE : rgba(0x1a3a5aff));
                ui::text(":(", p.x, p.y - 2, 18, col::cyan, ui::Center, true);
                DrawRectangleRec({p.x - 6, p.y + 20, 12, 8}, col::outline);
                break;
            }
            case EType::Printer: {
                DrawRectangleRounded({p.x - 30, p.y - 22, 60, 44}, 0.2f, 6, col::outline);
                DrawRectangleRounded({p.x - 27, p.y - 19, 54, 38}, 0.2f, 6, e.flash > 0 ? WHITE : rgba(0xd8d8e0ff));
                DrawRectangleRec({p.x - 20, p.y - 30 + std::sin(t_ * 6) * 3, 40, 14}, WHITE);
                DrawRectangleRec({p.x - 22, p.y + 2, 44, 5}, col::outline);
                DrawCircleV({p.x + 18, p.y - 10}, 3, col::red);
                break;
            }
            case EType::Davi: {
                // gaming chair seen from above, Davi's head, spinning in phase 3
                float rot = e.state == 3 ? t_ * 900 : std::sin(t_ * 2) * 10;
                rlPushMatrix();
                rlTranslatef(p.x, p.y, 0);
                rlRotatef(rot, 0, 0, 1);
                for (int i = 0; i < 5; i++) {
                    Vector2 leg = fromAngle(i * 2 * PI / 5, 58);
                    DrawLineEx({0, 0}, leg, 8, rgba(0x15121cff));
                    DrawCircleV(leg, 8, rgba(0x2a2534ff));
                }
                DrawRectangleRounded({-46, -40, 92, 80}, 0.4f, 8, col::outline);
                DrawRectangleRounded({-42, -36, 84, 72}, 0.4f, 8, e.flash > 0 ? WHITE : rgba(0x2a9ad8ff));
                DrawRectangleRounded({-42, 18, 84, 22}, 0.4f, 8, rgba(0x15121cff));
                rlPopMatrix();
                art::portrait("davi", p, 34, e.hp < e.maxHp * 0.5f ? col::red : col::cyan, std::sin(t_ * 3) * 8);
                break;
            }
        }
        (void)fl;
        // health pip bar for tougher things
        if (e.type != EType::Davi && e.hp < e.maxHp) {
            DrawRectangle((int)(p.x - 18), (int)(p.y - e.r - 12), 36, 4, alpha(BLACK, 0.6f));
            DrawRectangle((int)(p.x - 18), (int)(p.y - e.r - 12), (int)(36 * e.hp / e.maxHp), 4, col::red);
        }
    }

    void drawPlayer() {
        if (iframes_ > 0 && std::fmod(t_, 0.12f) < 0.06f) return;
        Vector2 aim = Vector2Normalize(E().mouse() - player_);
        fx::softShadow({player_.x, player_.y + 18}, 22, 7, 0.5f);
        // straw blaster pointing at the mouse
        DrawLineEx(player_, player_ + aim * 34, 10, col::outline);
        DrawLineEx(player_, player_ + aim * 32, 6, col::pink);
        DrawCircleV(player_, 22, col::outline);
        DrawCircleV(player_, 20, rgba(0x1f1c26ff));
        art::pieter({player_.x, player_.y - 6}, 18, P().hat);
    }

    void drawMinimap() {
        Rectangle r{VW - 190, 14, 176, 90};
        DrawRectangleRounded(r, 0.2f, 8, alpha(col::bgDeep, 0.8f));
        const Room& cur = rooms_.at(cur_);
        for (auto& [k, rm] : rooms_) {
            int dx = rm.gx - cur.gx, dy = rm.gy - cur.gy;
            if (std::abs(dx) > 3 || std::abs(dy) > 1) continue;
            bool known = rm.visited;
            for (auto& [k2, nb] : rooms_)
                if (nb.visited && std::abs(nb.gx - rm.gx) + std::abs(nb.gy - rm.gy) == 1) known = true;
            if (!known) continue;
            Rectangle cell{r.x + r.width / 2 - 13 + dx * 30.0f, r.y + r.height / 2 - 10 + dy * 26.0f, 26, 20};
            Color c = k == cur_ ? WHITE : rm.visited ? rgba(0x6a5a8aff) : rgba(0x3a3448ff);
            DrawRectangleRounded(cell, 0.2f, 4, c);
            if (rm.type == RoomType::Boss) ui::text("!", cell.x + 13, cell.y + 10, 14, col::red, ui::Center, true);
            if (rm.type == RoomType::Treasure && !rm.taken) DrawCircleV(center(cell), 4, col::gold);
        }
    }

    void drawHud() {
        for (int i = 0; i < (maxHp_ + 1) / 2; i++) {
            Vector2 p{30.0f + i * 40, 30};
            ui::heart({p.x, p.y + 2}, 32, col::outline);
            int fill = std::clamp(hp_ - i * 2, 0, 2);
            ui::heart(p, 28, fill == 2 ? col::red : rgba(0x3a2a52ff));
            if (fill == 1) {
                E().beginClip({p.x - 20, p.y - 20, 20, 40});
                ui::heart(p, 28, col::red);
                E().endClip();
            }
        }
        ui::text(TextFormat("FLOOR %d", floor_), 30, 66, 18, col::gold, ui::Left, true);
        ui::coin({150, 66}, 11);
        ui::text(std::to_string(coins_), 168, 66, 18, col::gold, ui::Left, true);
        // dash charge
        ui::bar({30, 84, 120, 8}, 1 - dashCd_, col::violet);
        // owned upgrades
        for (size_t i = 0; i < owned_.size() && i < 12; i++)
            DrawCircleV({VW - 180 + (float)(i % 6) * 26, 120.0f + (float)(i / 6) * 26}, 10, kUpgrades[owned_[i]].color);
        drawMinimap();
        // boss bar
        for (auto& e : enemies_)
            if (e.type == EType::Davi) {
                Rectangle b{VW / 2.0f - 300, VH - 34.0f, 600, 18};
                ui::text("DAVI, KING OF USELESS ELEKTRONIK", VW / 2.0f, VH - 46.0f, 16, col::cyan, ui::Center, true);
                ui::bar(b, e.hp / e.maxHp, e.hp < e.maxHp * 0.5f ? col::red : col::cyan);
            }
        if (bossIntro_ > 0) {
            float a = clamp01(bossIntro_);
            ui::textOutline("BOSS: DAVI", VW / 2.0f, 260, 70, alpha(col::cyan, a), alpha(col::outline, a), 5);
            ui::text("\"PRO since 2010. Get out of my basement.\"", VW / 2.0f, 320, 20, alpha(WHITE, a), ui::Center, true);
        }
        ui::text("WASD move   mouse aim + shoot   SPACE dash", VW / 2.0f, 24, 14, alpha(col::text, 0.55f), ui::Center, true);
    }

    void drawPick() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.6f));
        fx::gradientText("PICK AN UPGRADE", VW / 2.0f, 150, 56, WHITE, col::gold, 4);
        for (int i = 0; i < 3; i++) {
            const Upgrade& u = kUpgrades[offer_[i]];
            Rectangle r{VW / 2.0f - 450 + i * 310.0f, 230, 280, 300};
            bool hot = ui::hovered(r);
            Rectangle d{r.x, r.y - (hot ? 8.0f : 0.0f), r.width, r.height};
            if (hot) {
                fx::beginAdd();
                fx::glow(center(d), 220, alpha(u.color, 0.3f));
                fx::endAdd();
            }
            fx::gradRoundRect(d, 0.1f, lighten(u.color, 0.1f), darken(u.color, 0.55f));
            DrawRectangleRoundedLinesEx(d, 0.1f, 8, hot ? 3.5f : 2, hot ? WHITE : col::outline);
            fx::shadedBall({d.x + d.width / 2, d.y + 100}, 46, u.color, 3);
            ui::textShadow(u.name, d.x + d.width / 2, d.y + 190, 26, WHITE, ui::Center, true, 2);
            ui::text(u.desc, d.x + d.width / 2, d.y + 230, 17, alpha(WHITE, 0.85f), ui::Center, true);
            ui::text(TextFormat("[%d]", i + 1), d.x + d.width / 2, d.y + 270, 16, alpha(WHITE, 0.6f), ui::Center, true);
            if ((hot && E().mousePressed()) || IsKeyPressed(KEY_ONE + i)) {
                E().consumeClick();
                apply(offer_[i]);
                return;
            }
        }
    }

    void drawTitle() {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x2a1a4aff), rgba(0x0c0814ff));
        for (int i = 0; i < 6; i++) {
            Vector2 tp{100.0f + i * 216, 120};
            fx::beginAdd();
            fx::glow(tp, 120 + std::sin(t_ * 8 + i) * 10, alpha(col::orange, 0.25f));
            fx::endAdd();
        }
        fx::gradientText("DAVI'S DUNGEON", VW / 2.0f, 150, 84, WHITE, rgba(0x8fb0ffff), 6);
        ui::text("a roguelite in Davi's basement", VW / 2.0f, 210, 22, col::cyan, ui::Center, true);
        art::portrait("davi", {VW / 2.0f + 200, 380}, 90, col::cyan, std::sin(t_ * 2) * 6);
        art::pieter({VW / 2.0f - 200, 380}, 90, P().hat);
        ui::text("VS", VW / 2.0f, 380, 50, col::gold, ui::Center, true);
        ui::text("Clear rooms of useless elektronik, grab upgrades, beat Davi, go deeper.", VW / 2.0f, 510, 18, alpha(WHITE, 0.8f), ui::Center, true);
        ui::text("Deepest floor: " + std::to_string(P().stats.dungeonBest), VW / 2.0f, 545, 20, col::gold, ui::Center, true);
        if (ui::bigButton({VW / 2.0f - 170, 580, 340, 84}, "ENTER", "WASD + mouse", rgba(0x4a3a8aff), 40)) newRun();
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawDead() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.7f * clamp01(phaseT_ * 2)));
        float k = easeOutBack(clamp01(phaseT_ / 0.5f));
        rlPushMatrix();
        rlTranslatef(VW / 2.0f, 200, 0);
        rlScalef(k, k, 1);
        ui::textOutline("YOU DIED", 0, 0, 90, col::red, col::outline, 6);
        rlPopMatrix();
        ui::text("Davi: \"told you. PRO.\"", VW / 2.0f, 280, 22, col::cyan, ui::Center, true);
        ui::textShadow(TextFormat("Floor %d", floor_), VW / 2.0f, 360, 44, WHITE, ui::Center, true, 3);
        if (newBest_) ui::text("NEW DEEPEST FLOOR!", VW / 2.0f, 404, 20, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 80, 446}, 14);
        ui::text("+" + fmtNum(earned_) + " Pietercoins", VW / 2.0f - 58, 446, 22, col::gold, ui::Left, true);
        if (phaseT_ > 0.8f) {
            if (ui::button({VW / 2.0f - 260, 520, 250, 64}, "TRY AGAIN", rgba(0x4a3a8aff), 24)) newRun();
            if (ui::button({VW / 2.0f + 10, 520, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void draw() override {
        if (phase_ == Phase::Title) { drawTitle(); return; }
        drawRoom();
        for (auto& p : pickups_) {
            if (p.kind == 1) ui::heart(p.pos, 22, col::red);
            else ui::coin(p.pos, 10);
        }
        for (auto& e : enemies_) drawEnemy(e);
        drawPlayer();
        for (auto& s : shots_) {
            if (s.enemy) {
                fx::beginAdd();
                fx::glow(s.pos, s.r * 2.6f, alpha(s.color, 0.5f));
                fx::endAdd();
                DrawCircleV(s.pos, s.r, s.color);
                DrawCircleV(s.pos, s.r * 0.5f, WHITE);
            } else {
                fx::shadedBall(s.pos, s.r, s.color, 1.5f);
            }
        }
        fx_.draw();
        // warm light pooled around Pieter
        fx::beginAdd();
        fx::glow(player_, 260, alpha(rgba(0xffd8a0ff), 0.10f));
        fx::endAdd();
        art::vignette(0.3f);
        drawHud();
        if (roomIntro_ > 0) ui::textOutline("LOCKED IN!", VW / 2.0f, 240, 50, alpha(col::red, roomIntro_), alpha(col::outline, roomIntro_), 4);
        if (phase_ == Phase::Pick) drawPick();
        if (phase_ == Phase::Dead) drawDead();
        if (phase_ == Phase::Descend) {
            DrawRectangle(0, 0, VW, VH, alpha(BLACK, clamp01(phaseT_ / 1.2f)));
            ui::textOutline(TextFormat("FLOOR %d", floor_ + 1), VW / 2.0f, VH / 2.0f, 70, col::gold, col::outline, 5);
        }
    }

private:
    struct Pickup { Vector2 pos; int kind; };  // 0 coin, 1 heart
    Phase phase_ = Phase::Title;
    std::map<int, Room> rooms_;
    int cur_ = 0, floor_ = 1;
    std::vector<Enemy> enemies_;
    std::vector<Shot> shots_;
    std::vector<Pickup> pickups_;
    std::vector<int> owned_;
    int offer_[3] = {0, 1, 2};
    Vector2 player_{640, 390}, lastMove_{1, 0}, dashDir_{1, 0};
    int hp_ = 6, maxHp_ = 6, dmg_ = 1, pearls_ = 1, pierce_ = 0, coins_ = 0;
    float speed_ = 300, fireRate_ = 3.5f, shotSize_ = 7, shotSpeed_ = 640, dashRate_ = 1;
    float t_ = 0, phaseT_ = 0, fireCd_ = 0, dashT_ = 0, dashCd_ = 0, iframes_ = 0, roomIntro_ = 0, bossIntro_ = 0;
    long long earned_ = 0;
    bool newBest_ = false;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeDungeon() { return std::make_unique<DungeonScene>(); }

}  // namespace pg
