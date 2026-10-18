// DAKKENLOPER: a platformer over the rooftops of Amsterdam. Five levels, from
// the Jordaan to the top of the Westertoren: run, jump, wall-jump, bounce off
// striped awnings, ride hoists and window-cleaner lifts, stomp pigeons and
// gulls, time the steam vents and collect stroopwafels and three golden bobas
// per level.
//
// Levels are ASCII, 18 rows of 40px tiles:
//   B building  C chimney  X crumbling roof tiles  = ledge (one way)
//   @ start  F checkpoint  E exit bell  o stroopwafel  b golden boba
//   P pigeon  G gull  ^ steam vent  S awning (spring)
//   H hoist platform (swings sideways)  L window-cleaner lift (goes up)
// Every level was checked to be finishable with these exact jump physics.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <string>
#include <vector>

namespace pg {
namespace {

constexpr float T = 40;
constexpr int kRowsN = 18;
// physics (the level checker uses the same numbers)
constexpr float kGUp = 2300, kGDown = 2600, kGCut = 5200, kMaxFall = 980;
constexpr float kRun = 320, kAccGround = 2600, kAccAir = 1700, kJump = -820, kSpring = -1250;
constexpr float kPW = 26, kPH = 50;
constexpr float kStep = 1.0f / 120;

const char* kLevel0[] = {
    "......................................................................................................................",
    "......................................................................................................................",
    "......................................................................................................................",
    "............................................................................b.........................................",
    ".........................................b.........................................................b..................",
    ".......................................oooo......................................................BBBB.................",
    "......................................BBBBBB.....................................................BBBB.................",
    "......................................BBBBBB..............................BBBB...................BBBB.............E...",
    "......................................BBBBBB..............................BBBB....ooooooo........BBBB.............CC..",
    "...........................P..........BBBBBB........oooo............BBBB..BBBB......P..P.........BBBB.....oooooo..CC..",
    ".....o.o.o.o....ooooo...BBBBBBB.......BBBBBB..F.....====.....P......BBBB..BBBB..BBBBBBBBBB.......BBBB...F.........CC..",
    "..@.....................BBBBBBB....S..BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB....S..BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
    "BBBBBBBBBBBBB..BBBBBBB..BBBBBBB..BBBB.BBBBBBBBBBBB........BBBBBBBB..BBBB..BBBB..BBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBBCCBB",
};
const char* kLevel1[] = {
    "............................................................................................................................................",
    "............................................................................................................................................",
    "............................................................................................................................................",
    "............................................................................................................................................",
    "..................................b.................................................b.......................................................",
    "................................oooo...............................................BBB.................b....................................",
    "...............................BBBBBB..............................................BBB..................................................E...",
    "...............................BBBBBB..............................................BBB.....................===..........................CC..",
    "...............................BBBBBB.........ooo...ooo....................P.......BBB................===...........P...................CC..",
    "...oooooo......................BBBBBB...F.....XXX...XXX.....G...........BBBBBB.....BBB..F........===............BBBBBBBB..........ooooooCC..",
    "..@...................P........BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB...H.....BBBBBBBCCBB",
    "BBBBBBBBBB...H.....BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB...H.....BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB.........BBBBBB..L..BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB..L..BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
    "BBBBBBBBBB.........BBBBBBB.....BBBBBBBBBBBBB.............BBBBBB.........BBBBBB.....BBBBBBBBBBBB.................BBBBBBBB.........BBBBBBBCCBB",
};
const char* kLevel2[] = {
    "........................................................................................................................................",
    "........................................................................................................................................",
    "....................................................................oo.oo...............................................................",
    ".............................................b........................^.................................................................",
    "...................................................................BBBBBBB..............................................................",
    "...................................................................BBBBBBB..........................................................E...",
    "............................................CC.....................BBBBBBB..........................b...............................CC..",
    ".................b..........................CC.....................BBBBBBB....................^..............................XXX....CC..",
    "........................................CC..CC.....^..P..^.........BBBBBBB...F...G..........BBBB..........P...^...P.F...XXX.........CC..",
    "...................................F....CC..CC..BBBBBBBBBBBB.......BBBBBBBBBBBBBBBBB....^...BBBB....^...BBBBBBBBBBBBBB............BBCCBB",
    "..............ooCCo.ooooo.......BBBBBB..CC..CC..BBBBBBBBBBBB....S..BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "..@..........^..CC.^.....^......BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
    "BBBBBBBBBBBBBBBBCCBBBBBBBBBBBB..BBBBBB..CC..CC..BBBBBBBBBBBB..BBBB.BBBBBBBBBBBBBBBBB..BBBB..BBBB..BBBB..BBBBBBBBBBBBBB............BBCCBB",
};
const char* kLevel3[] = {
    "......................................................................................................................................................",
    "......................................................................................................................................................",
    "......................................................................................................................................................",
    "...................................b........................................b.........G...............................................................",
    ".................................BBBB..................................................................G....................b......G..................",
    ".................................BBBB.......G....................................................^...............................................E....",
    ".................................BBBB......................................XXX.....F..........BBBBB..............................................CC...",
    "...............G.................BBBB...........................P.....XXX.......BBBBBBBB......BBBBB........................===...................CC...",
    ".................................BBBB...F..............H.....BBBBBBB............BBBBBBBB......BBBBB..BBBB.....PF.P....===.......BBBBBB...oooooo..CC...",
    ".....................BBBB........BBBBBBBBBBBBB...H...........BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "..@..................BBBB.....S..BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB...L..BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
    "BBBBBBBBBB...BBBBB...BBBB...BBBB.BBBBBBBBBBBBB...............BBBBBBB............BBBBBBBB......BBBBB..BBBB..BBBBBBBBB............BBBBBB..BBBBBBBBBCCBBB",
};
const char* kLevel4[] = {
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    ".......................................................................................................................b.......G................................",
    "...................................................................................G...................................................BBBB................E....",
    ".................................b............................................................b........................................BBBB................CC...",
    "...............................^........G...................................................BBBB......................===....F.........BBBB...P...^.oooooo.CC...",
    ".............................BBBBB..........................................................BBBB.................===.......BBBBB.......BBBBBBBBBBBBBBBBBBBBCCBBB",
    ".............................BBBBB..............BBBB..XXX..XXX....^..P.F.........BBBBB......BBBB..F..^..P...===............BBBBB....S..BBBBBBBBBBBBBBBBBBBBCCBBB",
    ".............................BBBBB...F..........BBBB............BBBBBBBB...H.....BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "....................P........BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "..................BBBB.......BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "..................BBBB....S..BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "............BBBB..BBBB..BBBB.BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB...L..BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "..@.........BBBB..BBBB..BBBB.BBBBBBBBBBBBB...L..BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "BBBBBBBBBB..BBBB..BBBB..BBBB.BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "BBBBBBBBBB..BBBB..BBBB..BBBB.BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "BBBBBBBBBB..BBBB..BBBB..BBBB.BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
    "BBBBBBBBBB..BBBB..BBBB..BBBB.BBBBBBBBBBBBB......BBBB............BBBBBBBB.........BBBBB......BBBBBBBBBBBBBB.................BBBBB..BBBB.BBBBBBBBBBBBBBBBBBBBCCBBB",
};

struct LevelDef {
    const char* const* rows;
    const char* name;
    const char* sub;
    Color skyTop, skyBot;
    bool rain, dusk;
};
const LevelDef kLevels[] = {
    {kLevel0, "Jordaan Rooftops", "Learn to fly. Well, jump.", rgba(0x4a9ae8ff), rgba(0xcfeaffff), false, false},
    {kLevel1, "Over the Canal", "Hoists, lifts and loose roof tiles", rgba(0x5a8ad8ff), rgba(0xffe4c0ff), false, false},
    {kLevel2, "Chimney Alley", "Mind the steam", rgba(0x7a7ab8ff), rgba(0xf0c8a8ff), false, false},
    {kLevel3, "Storm over the IJ", "Gulls, rain and big gaps", rgba(0x2a3448ff), rgba(0x5a6a7aff), true, false},
    {kLevel4, "The Westertoren", "One long climb into the sunset", rgba(0x2a2a6aff), rgba(0xff9a6aff), false, true},
};
constexpr int kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);

struct Pigeon { Vector2 p; float vx; bool alive = true; float deadT = 0, t = 0; };
struct Gull { Vector2 home, p; float t; bool alive = true; float deadT = 0; };
struct Vent { int x, y; float ph; bool was = false; };
struct Spring { int x, y; float squash = 0; };
struct Mover { int x, y; bool lift; float ph; Rectangle r{}, prev{}; };
struct Crumble { int x, y; float t = -1, down = 0; };
struct Pickup { Vector2 p; bool taken = false; int idx = 0; };
struct Flag { Vector2 p; bool on = false; };

enum class Phase { Select, Play, Dead, Clear };

class RoofScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Select) return {"Dakkenloper", TextFormat("%d / %d levels cleared", cleared(), kLevelCount)};
        return {"Dakkenloper", TextFormat("Level %d: %s", level_ + 1, kLevels[level_].name)};
    }

    void enter() override {
        if (const char* l = std::getenv("PG_ROOF")) {  // dev hook: jump straight into a level
            load(std::clamp(std::atoi(l) - 1, 0, kLevelCount - 1));
            if (const char* x = std::getenv("PG_ROOF_X")) {
                int tx = std::clamp(std::atoi(x), 0, W_ - 1), ty = 0;
                while (ty < kRowsN - 1 && !solidAt(tx, ty + 1)) ty++;
                respawn_ = {tx * T + (T - kPW) / 2, (ty + 1) * T - kPH};
                spawnPlayer();
                camX_ = std::max(0.0f, px_ - VW * 0.4f);
            }
        }
    }
    void exit() override { E().sfx.stop(Sfx::Rain); }

    static int cleared() {
        int n = 0;
        for (int i = 0; i < kLevelCount; i++) n += (P().stats.roofDone >> i) & 1;
        return n;
    }
    static int bobasFound(int lvl) {
        int n = 0;
        for (int k = 0; k < 3; k++) n += (P().stats.roofBoba >> (lvl * 3 + k)) & 1;
        return n;
    }
    static bool unlocked(int lvl) { return lvl == 0 || (P().stats.roofDone >> (lvl - 1)) & 1; }

    // ------------------------------------------------------------ level loading
    void load(int lvl) {
        level_ = lvl;
        grid_.clear();
        for (int y = 0; y < kRowsN; y++) grid_.push_back(kLevels[lvl].rows[y]);
        W_ = (int)grid_[0].size();
        pigeons_.clear(); gulls_.clear(); vents_.clear(); springs_.clear(); movers_.clear(); crumbles_.clear();
        wafels_.clear(); bobas_.clear(); flags_.clear();
        int bi = 0;
        for (int y = 0; y < kRowsN; y++)
            for (int x = 0; x < W_; x++) {
                char c = grid_[y][x];
                Vector2 mid{x * T + T / 2, y * T + T / 2};
                switch (c) {
                    case '@': start_ = {x * T + (T - kPW) / 2, (y + 1) * T - kPH}; break;
                    case 'E': exit_ = {x * T + T / 2, y * T + T / 2}; break;
                    case 'o': wafels_.push_back({mid}); break;
                    case 'b': bobas_.push_back({mid, false, bi++}); break;
                    case 'F': flags_.push_back({{x * T + T / 2, (y + 1) * T}}); break;
                    case 'P': pigeons_.push_back({{mid.x, (y + 1) * T}, chance(0.5f) ? 40.0f : -40.0f}); break;
                    case 'G': gulls_.push_back({mid, mid, rnd(0, 6)}); break;
                    case '^': vents_.push_back({x, y, (float)((x * 37) % 24) / 10}); break;
                    case 'S': springs_.push_back({x, y}); break;
                    case 'H': movers_.push_back({x, y, false, rnd(0, 6)}); break;
                    case 'L': movers_.push_back({x, y, true, rnd(0, 6)}); break;
                    case 'X': crumbles_.push_back({x, y}); break;
                    default: break;
                }
                if (std::string("@EobFPG^SHL").find(c) != std::string::npos) grid_[y][x] = '.';
            }
        // buildings: runs of columns with the same roof line share a facade
        buildings_.clear();
        for (int x = 0; x < W_;) {
            int top = -1;
            for (int y = 0; y < kRowsN; y++)
                if (grid_[y][x] == 'B') { top = y; break; }
            if (top < 0) { x++; continue; }
            int x1 = x;
            while (x1 < W_) {
                int t2 = -1;
                for (int y = 0; y < kRowsN; y++)
                    if (grid_[y][x1] == 'B') { t2 = y; break; }
                if (t2 != top) break;
                x1++;
            }
            buildings_.push_back({x, x1, top, (int)buildings_.size()});
            x = x1;
        }
        wafelsGot_ = 0;
        deaths_ = 0;
        time_ = 0;
        levelT_ = 0;
        respawn_ = start_;
        spawnPlayer();
        camX_ = std::max(0.0f, px_ - VW * 0.4f);
        phase_ = Phase::Play;
        phaseT_ = 0;
        paused_ = false;
        acc_ = 0;
    }

    void spawnPlayer() {
        px_ = respawn_.x;
        py_ = respawn_.y;
        vx_ = vy_ = 0;
        ground_ = true;
        riding_ = -1;
        coyote_ = jumpBuf_ = lock_ = 0;
        wall_ = 0;
        springing_ = false;
        for (auto& c : crumbles_) c.t = -1, c.down = 0;
    }

    // ------------------------------------------------------------ tiles
    bool solidAt(int x, int y) const {
        if (x < 0) return true;
        if (x >= W_ || y < 0 || y >= kRowsN) return false;
        char c = grid_[y][x];
        if (c == 'X') {
            for (auto& cr : crumbles_)
                if (cr.x == x && cr.y == y) return cr.down <= 0;
        }
        return c == 'B' || c == 'C' || c == 'X';
    }
    bool ledgeAt(int x, int y) const { return x >= 0 && x < W_ && y >= 0 && y < kRowsN && grid_[y][x] == '='; }
    bool boxSolid(float x, float y, float w, float h) const {
        int x0 = (int)std::floor(x / T), x1 = (int)std::floor((x + w - 0.01f) / T);
        int y0 = (int)std::floor(y / T), y1 = (int)std::floor((y + h - 0.01f) / T);
        for (int ty = y0; ty <= y1; ty++)
            for (int tx = x0; tx <= x1; tx++)
                if (solidAt(tx, ty)) return true;
        return false;
    }

    Rectangle moverRect(const Mover& m, float t) const {
        if (m.lift) {
            float k = 0.5f - 0.5f * std::cos(t * 2 * PI / 5 + m.ph);
            return {m.x * T, (m.y + 1) * T - 14 - k * 4 * T, 2 * T, 14};
        }
        return {m.x * T + std::sin(t * 2 * PI / 4 + m.ph) * 3 * T, (m.y + 1) * T - 14, 2 * T, 14};
    }

    // ------------------------------------------------------------ update
    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        fx_.update(dt);
        world_.update(dt);
        if (phase_ == Phase::Select) {
            updateSelect();
            return;
        }
        if (kLevels[level_].rain) E().sfx.loop(Sfx::Rain, 0.35f);
        if (phase_ == Phase::Clear) {
            if (E().backPressed()) phase_ = Phase::Select;
            return;
        }
        if (E().backPressed()) paused_ = !paused_;
        if (paused_) return;
        if (IsKeyPressed(KEY_R)) {  // quick restart from the checkpoint
            die(true);
        }
        // gather input once per frame; the physics runs in fixed steps
        bool jp = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K);
        if (jp) jumpBuf_ = 0.12f;
        jumpHeld_ = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_K);
        dir_ = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1 : 0) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 1 : 0);
        if (autoRun_) {  // dev: PG_ROOF_AUTO=1 runs right and jumps at gaps and walls
            int tx = (int)((px_ + kPW / 2) / T), fy = (int)((py_ + kPH + 1) / T);
            bool gap = !solidAt(tx + 1, fy) && !ledgeAt(tx + 1, fy), wall = solidAt(tx + 1, fy - 1) || solidAt(tx + 1, fy - 2);
            dir_ = 1;
            if (ground_ && (gap || wall)) jumpBuf_ = 0.12f;
            jumpHeld_ = vy_ < 0 || jumpBuf_ > 0;
        }
        acc_ += std::min(dt, 0.05f);
        while (acc_ >= kStep) {
            acc_ -= kStep;
            levelT_ += kStep;
            if (phase_ == Phase::Play) {
                time_ += kStep;
                step(kStep);
            } else if (phase_ == Phase::Dead) {
                deadV_.y += 1800 * kStep;
                deadP_ += deadV_ * kStep;
                if (phaseT_ > 0.9f) {
                    phase_ = Phase::Play;
                    spawnPlayer();
                }
            } else {
                break;
            }
            world(kStep);
        }
        camX_ = damp(camX_, std::clamp(px_ + vx_ * 0.25f - VW * 0.4f, 0.0f, W_ * T - (float)VW), 6, dt);
        if (land_ > 0) land_ = std::max(0.0f, land_ - dt * 5);
        if (kLevels[level_].rain && chance(dt * 0.12f)) {
            lightning_ = 1;
            E().sfx.play(Sfx::Thunder, 0.5f);
        }
        lightning_ = std::max(0.0f, lightning_ - dt * 3);
    }

    void step(float h) {
        // ride a mover
        if (riding_ >= 0) {
            const Mover& m = movers_[riding_];
            px_ += m.r.x - m.prev.x;
            py_ += m.r.y - m.prev.y;
        }
        lock_ = std::max(0.0f, lock_ - h);
        jumpBuf_ -= h;
        coyote_ = ground_ ? 0.1f : coyote_ - h;
        float target = dir_ * kRun;
        if (lock_ <= 0) {
            if (ground_) vx_ = dir_ ? approach(vx_, target, kAccGround * h) : approach(vx_, 0, 3000 * h);
            else vx_ = dir_ ? approach(vx_, target, kAccAir * h) : approach(vx_, 0, 600 * h);
        }
        // wall contact (airborne, pushing into a wall)
        wall_ = 0;
        if (!ground_ && dir_ != 0 && boxSolid(dir_ > 0 ? px_ + kPW : px_ - 2, py_ + 8, 2, kPH - 16)) wall_ = dir_;
        if (jumpBuf_ > 0 && coyote_ > 0) {
            vy_ = kJump;
            jumpBuf_ = coyote_ = 0;
            ground_ = false;
            riding_ = -1;
            stretch_ = 1;
            E().sfx.play(Sfx::Jump, 0.45f, rnd(0.95f, 1.1f));
            dust(4);
        } else if (jumpBuf_ > 0 && wall_ != 0) {
            vx_ = -wall_ * 330;
            vy_ = -780;
            lock_ = 0.16f;
            face_ = -wall_;
            jumpBuf_ = 0;
            stretch_ = 1;
            E().sfx.play(Sfx::Jump, 0.45f, 1.2f);
            for (int i = 0; i < 6; i++) puff({wall_ > 0 ? px_ + kPW : px_, py_ + kPH / 2 + rnd(-10, 10)});
        }
        bool held = jumpHeld_ || springing_;
        float g = vy_ < 0 ? (held ? kGUp : kGCut) : kGDown;
        vy_ = std::min(kMaxFall, vy_ + g * h);
        if (vy_ >= 0) springing_ = false;
        if (wall_ != 0 && vy_ > 170) {
            vy_ = 170;
            if (chance(h * 20)) puff({wall_ > 0 ? px_ + kPW : px_, py_ + 10});
        }
        if (dir_ != 0 && lock_ <= 0) face_ = (float)dir_;

        // move X
        float nx = px_ + vx_ * h;
        if (boxSolid(nx, py_, kPW, kPH)) {
            if (vx_ > 0) nx = std::floor((nx + kPW) / T) * T - kPW - 0.01f;
            else if (vx_ < 0) nx = std::floor(nx / T + 1) * T + 0.01f;
            if (boxSolid(nx, py_, kPW, kPH)) nx = px_;
            vx_ = 0;
        }
        px_ = nx;
        // move Y
        bool wasGround = ground_;
        float prevBottom = py_ + kPH;
        float ny = py_ + vy_ * h;
        ground_ = false;
        int ride = -1;
        if (boxSolid(px_, ny, kPW, kPH)) {
            if (vy_ > 0) {
                ny = std::floor((ny + kPH) / T) * T - kPH;
                ground_ = true;
            } else {
                ny = std::floor(ny / T + 1) * T + 0.01f;
            }
            if (boxSolid(px_, ny, kPW, kPH)) ny = py_;
            vy_ = 0;
        } else if (vy_ >= 0) {
            // one-way ledges
            int ty = (int)std::floor((ny + kPH) / T);
            float top = ty * T;
            int x0 = (int)std::floor(px_ / T), x1 = (int)std::floor((px_ + kPW - 0.01f) / T);
            for (int x = x0; x <= x1; x++)
                if (ledgeAt(x, ty) && prevBottom <= top + 0.5f && ny + kPH >= top) {
                    ny = top - kPH;
                    vy_ = 0;
                    ground_ = true;
                }
            // movers
            for (size_t i = 0; i < movers_.size(); i++) {
                const Rectangle& r = movers_[i].r;
                float prevTop = movers_[i].prev.y;
                if (px_ + kPW > r.x && px_ < r.x + r.width && prevBottom <= prevTop + 2 && ny + kPH >= r.y) {
                    ny = r.y - kPH;
                    vy_ = 0;
                    ground_ = true;
                    ride = (int)i;
                }
            }
        }
        py_ = ny;
        riding_ = ride;
        if (ground_ && !wasGround) {
            land_ = 1;
            E().sfx.play(Sfx::Drop, 0.25f, 1.4f);
            dust(5);
        }
        if (ground_ && std::abs(vx_) > 200 && chance(h * 12)) dust(1);
        stretch_ = std::max(0.0f, stretch_ - h * 4);

        // crumbling tiles start to go when you stand on them
        if (ground_) {
            int ty = (int)std::floor((py_ + kPH + 1) / T);
            for (auto& c : crumbles_)
                if (c.y == ty && c.t < 0 && c.down <= 0 && px_ + kPW > c.x * T && px_ < (c.x + 1) * T) {
                    c.t = 0;
                    E().sfx.play(Sfx::Creak, 0.35f, 1.4f);
                }
        }

        // pickups, flags, the exit
        Vector2 pc{px_ + kPW / 2, py_ + kPH / 2};
        for (auto& w : wafels_)
            if (!w.taken && Vector2Distance(pc, w.p) < 28) {
                w.taken = true;
                wafelsGot_++;
                E().sfx.play(Sfx::Coin, 0.35f, 1 + (wafelsGot_ % 8) * 0.04f);
                world_.burst(w.p, 8, col::wafel, 140, 4, 0.35f, PShape::Spark);
            }
        for (auto& b : bobas_)
            if (!b.taken && Vector2Distance(pc, b.p) < 30) {
                b.taken = true;
                E().sfx.play(Sfx::Gem, 0.8f);
                E().sfx.play(Sfx::Power, 0.4f, 1.3f);
                world_.burst(b.p, 24, col::gold, 260, 6, 0.6f, PShape::Star);
                fx_.floatText("GOLDEN BOBA!", {b.p.x - camX_, b.p.y - 30}, col::gold, 26);
                E().flash(col::gold, 0.2f);
            }
        for (auto& f : flags_)
            if (!f.on && std::abs(pc.x - f.p.x) < 26 && pc.y > f.p.y - 90 && pc.y < f.p.y + 10) {
                f.on = true;
                respawn_ = {f.p.x - kPW / 2, f.p.y - kPH};
                E().sfx.play(Sfx::FietsBel, 0.5f, 1.2f);
                fx_.floatText("CHECKPOINT", {f.p.x - camX_, f.p.y - 110}, col::green, 24);
                fx_.confetti({f.p.x - camX_, f.p.y - 70}, 30, 300);
            }
        if (Vector2Distance(pc, exit_) < 44) finish();

        // hazards
        if (py_ > kRowsN * T + 20) die(false);
        for (auto& v : vents_)
            if (ventOn(v) && CheckCollisionRecs({px_ + 4, py_ + 4, kPW - 8, kPH - 8}, ventRect(v))) die(false);
        for (auto& s : springs_) {
            Rectangle r{s.x * T + 4, (s.y + 1) * T - 16, T - 8, 16};
            if (vy_ >= 0 && CheckCollisionRecs({px_, py_ + kPH - 8, kPW, 10}, r)) {
                vy_ = kSpring;
                springing_ = true;
                ground_ = false;
                s.squash = 1;
                stretch_ = 1;
                E().sfx.play(Sfx::Boing, 0.6f);
            }
        }
        for (auto& p : pigeons_) {
            if (!p.alive) continue;
            Rectangle r{p.p.x - 15, p.p.y - 26, 30, 26};
            if (!CheckCollisionRecs({px_, py_, kPW, kPH}, r)) continue;
            if (vy_ > 0 && prevBottom <= r.y + 10) stomp(p.p, p.alive, p.deadT);
            else die(false);
        }
        for (auto& g2 : gulls_) {
            if (!g2.alive) continue;
            if (Vector2Distance(pc, g2.p) > 34) continue;
            if (vy_ > 0 && prevBottom <= g2.p.y - 4) stomp(g2.p, g2.alive, g2.deadT);
            else die(false);
        }
    }

    void stomp(Vector2 at, bool& alive, float& deadT) {
        alive = false;
        deadT = 0;
        vy_ = jumpHeld_ ? -720 : -520;
        E().sfx.play(Sfx::Pop, 0.6f);
        E().sfx.play(Sfx::Coo, 0.4f, 1.5f);
        E().shake(3);
        for (int i = 0; i < 10; i++) {
            Particle p;
            p.pos = at + Vector2{0, -12};
            p.vel = rndDir() * rnd(60, 200);
            p.life = p.maxLife = rnd(0.6f, 1.1f);
            p.size = rnd(4, 7);
            p.endSize = 3;
            p.gravity = 150;
            p.drag = 2;
            p.rotVel = rnd(-300, 300);
            p.shape = PShape::Square;
            p.color = chance(0.5f) ? rgba(0x8a8ea0ff) : WHITE;
            world_.add(p);
        }
    }

    bool ventOn(const Vent& v) const { return std::fmod(levelT_ + v.ph, 2.6f) < 1.0f; }
    Rectangle ventRect(const Vent& v) const { return {v.x * T + 8, (v.y - 2) * T, T - 16, 3 * T - 10}; }

    void world(float h) {
        for (auto& m : movers_) {
            m.prev = m.r.width > 0 ? m.r : moverRect(m, levelT_ - h);
            m.r = moverRect(m, levelT_);
        }
        for (auto& c : crumbles_) {
            if (c.t >= 0) {
                c.t += h;
                if (c.t > 0.5f) {
                    c.t = -1;
                    c.down = 3;
                    E().sfx.play(Sfx::Rattle, 0.4f);
                    for (int i = 0; i < 6; i++) world_.add(Particle{{c.x * T + rnd(0, T), c.y * T + 10}, {rnd(-40, 40), rnd(0, 100)}, 1, 1, 8, 6, 1400, 0, 0, rnd(-300, 300), rgba(0xc8582aff), BLANK, PShape::Square});
                }
            } else if (c.down > 0) {
                c.down -= h;
                // don't pop back in while Pieter stands where it was
                if (c.down <= 0 && CheckCollisionRecs({px_, py_, kPW, kPH}, {c.x * T, c.y * T, T, T})) c.down = 0.1f;
            }
        }
        for (auto& p : pigeons_) {
            p.t += h;
            if (!p.alive) { p.deadT += h; continue; }
            float nx = p.p.x + p.vx * h;
            int ahead = (int)std::floor((nx + (p.vx > 0 ? 14 : -14)) / T);
            int feet = (int)std::floor((p.p.y + 2) / T);
            int body = (int)std::floor((p.p.y - 10) / T);
            if (!(solidAt(ahead, feet) || ledgeAt(ahead, feet)) || solidAt(ahead, body)) p.vx = -p.vx;
            else p.p.x = nx;
            if (chance(h * 0.15f) && std::abs(p.p.x - px_) < 600) E().sfx.playAt(Sfx::Coo, p.p.x - camX_, 0.3f, rnd(0.9f, 1.1f));
        }
        for (auto& g : gulls_) {
            g.t += h;
            if (!g.alive) { g.deadT += h; g.p.y += 300 * h; continue; }
            g.p = g.home + Vector2{std::sin(g.t * 0.9f) * 130, std::sin(g.t * 1.8f) * 30};
            if (chance(h * 0.1f) && std::abs(g.p.x - px_) < 700) E().sfx.playAt(Sfx::Gull, g.p.x - camX_, 0.35f);
        }
        for (auto& v : vents_) {
            bool on = ventOn(v);
            if (on && !v.was && std::abs(v.x * T - px_) < 700) E().sfx.playAt(Sfx::Steam, v.x * T - camX_, 0.35f);
            v.was = on;
            if (on && chance(h * 50)) {
                Particle p;
                p.pos = {v.x * T + T / 2 + rnd(-6, 6), v.y * T + 10};
                p.vel = {rnd(-20, 20), rnd(-320, -220)};
                p.life = p.maxLife = rnd(0.35f, 0.5f);
                p.size = rnd(8, 14);
                p.endSize = 26;
                p.color = alpha(WHITE, 0.75f);
                steam_.add(p);
            } else if (!on && std::fmod(levelT_ + v.ph, 2.6f) > 2.1f && chance(h * 12)) {
                Particle p;  // a warning wisp before the next puff
                p.pos = {v.x * T + T / 2 + rnd(-4, 4), v.y * T + 18};
                p.vel = {rnd(-10, 10), rnd(-60, -30)};
                p.life = p.maxLife = 0.6f;
                p.size = 5;
                p.endSize = 10;
                p.color = alpha(WHITE, 0.4f);
                steam_.add(p);
            }
        }
        steam_.update(h);
        for (auto& s : springs_) s.squash = std::max(0.0f, s.squash - h * 4);
    }

    void dust(int n) {
        for (int i = 0; i < n; i++) puff({px_ + kPW / 2 + rnd(-10, 10), py_ + kPH});
    }
    void puff(Vector2 at) {
        Particle p;
        p.pos = at;
        p.vel = {rnd(-60, 60), rnd(-60, -10)};
        p.life = p.maxLife = rnd(0.25f, 0.45f);
        p.size = rnd(3, 6);
        p.endSize = 9;
        p.drag = 3;
        p.color = alpha(rgba(0xd8d0e0ff), 0.6f);
        world_.add(p);
    }

    void die(bool quiet) {
        if (phase_ != Phase::Play) return;
        deaths_++;
        phase_ = Phase::Dead;
        phaseT_ = 0;
        deadP_ = {px_ + kPW / 2, py_ + 14};
        deadV_ = {-face_ * 120, -520};
        if (!quiet) {
            E().sfx.play(Sfx::Hurt, 0.6f);
            E().shake(7);
        }
    }

    void finish() {
        phase_ = Phase::Clear;
        phaseT_ = 0;
        auto& st = P().stats;
        bool first = !((st.roofDone >> level_) & 1);
        newBest_ = st.roofTime[level_] <= 0 || time_ < st.roofTime[level_];
        int newBobas = 0;
        for (auto& b : bobas_)
            if (b.taken && !((st.roofBoba >> (level_ * 3 + b.idx)) & 1)) {
                newBobas++;
                st.roofBoba |= 1 << (level_ * 3 + b.idx);
            }
        st.roofDone |= 1 << level_;
        if (newBest_) st.roofTime[level_] = time_;
        earned_ = wafelsGot_ / 2 + newBobas * 25 + (first ? 60 : 10);
        P().earn(earned_);
        P().save();
        E().sfx.play(Sfx::Carillon, 0.7f);
        E().sfx.play(Sfx::Win, 0.6f);
        fx_.confetti({VW / 2.0f, 200}, 120);
    }

    // ------------------------------------------------------------ level select
    void updateSelect() {
        if (E().backPressed()) E().go(makeMenu);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) selFocus_ = std::min(kLevelCount - 1, selFocus_ + 1);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) selFocus_ = std::max(0, selFocus_ - 1);
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) && unlocked(selFocus_)) {
            E().sfx.play(Sfx::Click);
            load(selFocus_);
        }
    }

    void drawMini(int lvl, Rectangle r) {
        // the whole level squeezed into a card
        const char* const* rows = kLevels[lvl].rows;
        int w = (int)std::string(rows[0]).size();
        float s = std::min(r.width / w, r.height / kRowsN);
        float ox = r.x + (r.width - w * s) / 2, oy = r.y + (r.height - kRowsN * s) / 2;
        DrawRectangleGradientV((int)ox, (int)oy, (int)(w * s), (int)(kRowsN * s), kLevels[lvl].skyTop, kLevels[lvl].skyBot);
        for (int y = 0; y < kRowsN; y++)
            for (int x = 0; x < w; x++) {
                char c = rows[y][x];
                Color k = c == 'B' ? rgba(0x8a4a3aff) : c == 'C' ? rgba(0x4a3a4aff) : c == 'X' ? rgba(0xd8703aff) : c == '=' ? rgba(0xc8a070ff) : c == 'b' ? col::gold : c == 'E' ? WHITE : BLANK;
                if (k.a) DrawRectangleRec({ox + x * s, oy + y * s, s + 0.4f, c == '=' ? s * 0.4f : s + 0.4f}, k);
            }
    }

    void drawSelect() {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x3a6ab8ff), rgba(0xffc8a0ff));
        skyline(t_ * 30, 0.15f, 520, 0.55f, alpha(rgba(0x5a5a8aff), 0.8f));
        skyline(t_ * 60, 0.3f, 600, 0.75f, alpha(rgba(0x3a3a5aff), 0.9f));
        ui::titleBar("DAKKENLOPER", col::orange);
        ui::text("Rooftop run across Amsterdam", VW / 2.0f, 92, 18, col::text, ui::Center, true);
        if (ui::backButton()) E().go(makeMenu);
        float cw = 226, gap = 18, x0 = (VW - (cw * kLevelCount + gap * (kLevelCount - 1))) / 2;
        for (int i = 0; i < kLevelCount; i++) {
            Rectangle r{x0 + i * (cw + gap), 150, cw, 420};
            bool open = unlocked(i), over = ui::hovered(r);
            if (over && Vector2Length(GetMouseDelta()) > 0) selFocus_ = i;
            bool foc = selFocus_ == i;
            float& h = hover_[i];
            h = damp(h, foc ? 1.0f : 0.0f, 12, GetFrameTime());
            r.y -= h * 10;
            fx::softShadow({r.x + r.width / 2, r.y + r.height + 6}, r.width * 0.5f, 18, 0.5f);
            DrawRectangleRounded(r, 0.08f, 10, alpha(col::panel, 0.95f));
            drawMini(i, {r.x + 12, r.y + 12, r.width - 24, 110});
            ui::text(TextFormat("LEVEL %d", i + 1), r.x + 16, r.y + 146, 14, col::orange, ui::Left, true);
            ui::paragraph(kLevels[i].name, {r.x + 16, r.y + 160, r.width - 32, 0}, 22, WHITE);
            ui::paragraph(kLevels[i].sub, {r.x + 16, r.y + 222, r.width - 32, 0}, 14, col::muted);
            for (int k = 0; k < 3; k++) {
                bool got = (P().stats.roofBoba >> (i * 3 + k)) & 1;
                Vector2 c{r.x + 36 + k * 40.0f, r.y + 300};
                if (got) art::boba(c, 30);
                else DrawCircleV(c, 12, alpha(WHITE, 0.08f)), ui::text("?", c.x, c.y, 14, alpha(WHITE, 0.3f), ui::Center, true);
            }
            float best = P().stats.roofTime[i];
            ui::text(best > 0 ? TextFormat("best %d:%05.2f", (int)best / 60, std::fmod(best, 60.0f)) : "not cleared yet", r.x + 16, r.y + 340, 14,
                     best > 0 ? col::gold : col::muted, ui::Left, true);
            if (!open) {
                DrawRectangleRounded(r, 0.08f, 10, alpha(col::bgDeep, 0.65f));
                DrawRectangleRounded({r.x + r.width / 2 - 18, r.y + 190, 36, 30}, 0.3f, 6, col::muted);
                DrawRing({r.x + r.width / 2, r.y + 190}, 9, 14, 180, 360, 16, col::muted);
                ui::text("clear the level before", r.x + r.width / 2, r.y + 240, 13, col::muted, ui::Center, true);
            } else if (ui::button({r.x + 16, r.y + 362, r.width - 32, 46}, "PLAY", foc ? col::orange : col::panelHi, 20)) {
                load(i);
            }
            DrawRectangleRoundedLinesEx(r, 0.08f, 10, foc ? 2.5f : 1, foc ? alpha(col::orange, 0.9f) : alpha(WHITE, 0.12f));
        }
        ui::text("A / D  run     SPACE  jump (hold for higher)     jump at a wall to wall-jump     R  back to the checkpoint", VW / 2.0f, 640, 15,
                 alpha(WHITE, 0.85f), ui::Center, true);
        ui::text("stomp pigeons and gulls, bounce on the awnings, don't touch the steam", VW / 2.0f, 666, 15, alpha(WHITE, 0.7f), ui::Center, true);
    }

    // ------------------------------------------------------------ draw: world
    // A strip of canal-house silhouettes for the parallax background.
    void skyline(float scroll, float par, float baseY, float sc, Color tint) {
        float spacing = 170 * sc * 0.98f, period = 12 * spacing;
        float off = std::fmod(scroll * par, period);
        for (int i = 0; i < 24; i++) {
            float x = i * spacing - off;
            if (x > VW + 200 || x < -260) continue;
            int k = (i * 5 + (int)(par * 10)) % 6;
            Texture2D& t = E().tex(TextFormat("spr/house%d", k));
            float hs = sc * (0.85f + 0.15f * ((i * 7) % 3));
            DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {x, baseY - t.height * hs, t.width * hs, t.height * hs}, {0, 0}, 0, tint);
        }
    }

    void drawBackground() {
        const LevelDef& L = kLevels[level_];
        DrawRectangleGradientV(0, 0, VW, VH, L.skyTop, L.skyBot);
        if (L.dusk) {
            fx::beginAdd();
            fx::glow({900 - camX_ * 0.02f, 470}, 380, alpha(rgba(0xff8a4aff), 0.6f));
            fx::endAdd();
            DrawCircleV({900 - camX_ * 0.02f, 470}, 60, rgba(0xffb070ff));
        } else if (!L.rain) {
            fx::beginAdd();
            fx::glow({1050, 90}, 220, alpha(rgba(0xfff0c0ff), 0.6f));
            fx::endAdd();
            DrawCircleV({1050, 90}, 38, rgba(0xfff6d0ff));
        }
        // clouds
        Color cl = L.rain ? rgba(0x3a4250ff) : L.dusk ? rgba(0xffb0a0ff) : WHITE;
        for (int i = 0; i < 7; i++) {
            float x = std::fmod(i * 300 - camX_ * 0.05f - t_ * (L.rain ? 30 : 6), 2100.0f);
            if (x < -300) x += 2100;
            float y = 60.0f + (i * 53) % 140;
            for (int k = 0; k < 4; k++) DrawCircleV({x + k * 34.0f - 50, y - (k % 2) * 14}, 30.0f + (k % 2) * 8, alpha(cl, L.rain ? 0.9f : 0.85f));
        }
        // the Westertoren on the horizon
        float wx = 1500 - camX_ * 0.08f;
        Color far = mix(L.skyBot, rgba(0x2a2a48ff), 0.45f);
        DrawRectangle((int)wx, 300, 60, 340, far);
        DrawRectangle((int)wx + 8, 230, 44, 80, far);
        DrawRectangle((int)wx + 15, 170, 30, 70, far);
        DrawTriangle({wx + 30, 110}, {wx + 12, 175}, {wx + 48, 175}, far);
        DrawCircleV({wx + 30, 106}, 7, far);
        // two hazy rows of canal houses far below the roofs you run on
        skyline(camX_, 0.15f, 690, 0.42f, WHITE);
        DrawRectangle(0, 0, VW, VH, alpha(L.skyBot, 0.62f));
        skyline(camX_, 0.35f, 740, 0.58f, WHITE);
        DrawRectangle(0, 0, VW, VH, alpha(mix(L.skyBot, L.skyTop, 0.4f), 0.42f));
    }

    void drawBuildings() {
        const Color walls[] = {rgba(0xffffffff), rgba(0xd8a090ff), rgba(0xa0b0d8ff), rgba(0xe8d8b0ff), rgba(0xb0d0a0ff), rgba(0xc8a0c8ff)};
        bool night = kLevels[level_].rain || kLevels[level_].dusk;
        for (auto& b : buildings_) {
            float x0 = b.x0 * T - camX_, x1 = b.x1 * T - camX_;
            if (x1 < -20 || x0 > VW + 20) continue;
            Rectangle r{x0, b.top * T, x1 - x0, (kRowsN - b.top) * T};
            Texture2D& tex = E().tex(b.id % 3 == 2 ? "tex/brick_dark" : "tex/brick_red");
            fx::tiled(tex, r, 0.62f, {(b.x0 * T) / 0.62f, 0}, walls[b.id % 6]);
            DrawRectangleGradientH((int)r.x, (int)r.y, 14, (int)r.height, alpha(BLACK, 0.25f), alpha(BLACK, 0));
            DrawRectangleGradientH((int)(r.x + r.width - 14), (int)r.y, 14, (int)r.height, alpha(BLACK, 0), alpha(BLACK, 0.25f));
            // windows
            for (int wy = b.top + 1; wy < kRowsN; wy += 2)
                for (int wxi = b.x0; wxi < b.x1; wxi++) {
                    if ((wxi - b.x0) % 2 != ((b.x1 - b.x0) % 2 ? 0 : 1) && b.x1 - b.x0 > 2) continue;
                    if (b.x1 - b.x0 <= 2 && wxi != b.x0) continue;
                    Rectangle w{wxi * T - camX_ + 8 + (b.x1 - b.x0 <= 2 ? T / 2 : 0), wy * T + 4, T - 16, T - 6};
                    bool lit = night && ((wxi * 7 + wy * 3 + b.id) % 3 == 0);
                    DrawRectangleRec(shrink(w, -3), rgba(0xf4f0e8ff));
                    DrawRectangleRec(w, lit ? rgba(0xffd88aff) : rgba(0x3a4a6aff));
                    if (!lit) DrawRectangleGradientV((int)w.x, (int)w.y, (int)w.width, (int)w.height, alpha(rgba(0x8ab0e0ff), 0.6f), alpha(rgba(0x3a4a6aff), 0));
                    DrawLineEx({w.x + w.width / 2, w.y}, {w.x + w.width / 2, w.y + w.height}, 2, rgba(0xf4f0e8ff));
                    DrawLineEx({w.x, w.y + w.height * 0.45f}, {w.x + w.width, w.y + w.height * 0.45f}, 2, rgba(0xf4f0e8ff));
                    if (lit) {
                        fx::beginAdd();
                        fx::glow(center(w), 34, alpha(rgba(0xffc870ff), 0.35f));
                        fx::endAdd();
                    }
                }
            // cornice and gutter on the roof line
            DrawRectangleRec({r.x - 4, r.y, r.width + 8, 8}, rgba(0xf4f0e8ff));
            DrawRectangleRec({r.x - 4, r.y + 8, r.width + 8, 3}, alpha(BLACK, 0.3f));
            DrawRectangleRec({r.x - 4, r.y - 3, r.width + 8, 4}, rgba(0x3a3a46ff));
        }
        // chimneys, crumbling tiles, ledges
        for (int y = 0; y < kRowsN; y++)
            for (int x = 0; x < W_; x++) {
                float sx = x * T - camX_;
                if (sx < -T || sx > VW) continue;
                char c = grid_[y][x];
                if (c == 'C') {
                    fx::tiled(E().tex("tex/brick_dark"), {sx, y * T, T, T}, 0.5f, {x * T / 0.5f, y * T / 0.5f});
                    if (y == 0 || grid_[y - 1][x] != 'C') {
                        DrawRectangleRec({sx - 3, y * T - 4, T + 6, 10}, rgba(0x2a2a32ff));
                        DrawRectangleRec({sx - 3, y * T - 4, T + 6, 3}, rgba(0x6a6a72ff));
                    }
                } else if (c == 'X') {
                    for (auto& cr : crumbles_) {
                        if (cr.x != x || cr.y != y || cr.down > 0) continue;
                        float shake = cr.t >= 0 ? std::sin(cr.t * 90) * 2.5f : 0;
                        Rectangle r{sx + shake, y * T, T, T};
                        DrawRectangleRec(r, rgba(0x8a3a1aff));
                        for (int k = 0; k < 3; k++) {
                            DrawRectangleRounded({r.x + 1, r.y + 2 + k * 12.0f, T - 2, 12}, 0.5f, 4, rgba(0xc8582aff));
                            DrawRectangleRounded({r.x + 3, r.y + 3 + k * 12.0f, T - 10, 4}, 0.5f, 4, alpha(WHITE, 0.2f));
                        }
                        DrawLineEx({r.x + 8, r.y + 4}, {r.x + 18, r.y + 22}, 1.5f, alpha(BLACK, 0.5f));
                        DrawLineEx({r.x + 18, r.y + 22}, {r.x + 14, r.y + 36}, 1.5f, alpha(BLACK, 0.5f));
                    }
                } else if (c == '=') {
                    // a little iron balcony
                    DrawRectangleRec({sx, y * T, T, 8}, rgba(0x2a2a32ff));
                    DrawRectangleRec({sx, y * T, T, 3}, rgba(0x6a6a7aff));
                    for (int k = 0; k < 4; k++) DrawRectangleRec({sx + 4 + k * 10.0f, y * T + 8, 2, 12}, rgba(0x2a2a32ff));
                    DrawRectangleRec({sx, y * T + 18, T, 3}, rgba(0x2a2a32ff));
                }
            }
    }

    void drawThings() {
        for (auto& m : movers_) {
            Rectangle r{m.r.x - camX_, m.r.y, m.r.width, m.r.height};
            if (r.x > VW + 50 || r.x + r.width < -50) continue;
            if (m.lift) {
                // window cleaner's gondola on two cables
                DrawLineEx({r.x + 8, 0}, {r.x + 8, r.y}, 2, rgba(0x2a2a32ff));
                DrawLineEx({r.x + r.width - 8, 0}, {r.x + r.width - 8, r.y}, 2, rgba(0x2a2a32ff));
                DrawRectangleRec({r.x, r.y - 24, r.width, 3}, rgba(0xc8c8d0ff));
                for (int k = 0; k <= 4; k++) DrawRectangleRec({r.x + k * (r.width - 3) / 4, r.y - 24, 3, 24}, rgba(0xc8c8d0ff));
                fx::shadedRoundRect(r, 0.3f, rgba(0x3a8ad8ff), 2);
            } else {
                // a plank hanging from a rope on a hoist
                Vector2 hook{m.x * T - camX_ + T, 0};
                DrawLineEx(hook, {r.x + r.width / 2, r.y - 18}, 2, rgba(0x8a6a4aff));
                DrawLineEx({r.x + r.width / 2, r.y - 18}, {r.x + 4, r.y}, 1.5f, rgba(0x8a6a4aff));
                DrawLineEx({r.x + r.width / 2, r.y - 18}, {r.x + r.width - 4, r.y}, 1.5f, rgba(0x8a6a4aff));
                fx::shadedRoundRect(r, 0.3f, rgba(0xa8743aff), 2);
            }
        }
        for (auto& s : springs_) {
            float x = s.x * T - camX_ + T / 2, y = (s.y + 1) * T;
            float sq = 1 - s.squash * 0.4f * std::abs(std::sin(s.squash * 12));
            DrawLineEx({x - 16, y}, {x - 16, y - 20 * sq}, 3, rgba(0x5a5a62ff));
            DrawLineEx({x + 16, y}, {x + 16, y - 20 * sq}, 3, rgba(0x5a5a62ff));
            for (int k = 0; k < 5; k++) {
                float a0 = 180 + k * 36, a1 = a0 + 36;
                DrawCircleSector({x, y - 18 * sq}, 22, a0, a1, 6, k % 2 ? WHITE : col::red);
            }
            DrawRectangleRec({x - 22, y - 19 * sq, 44, 4}, rgba(0x8a2a24ff));
        }
        for (auto& v : vents_) {
            float x = v.x * T - camX_;
            DrawRectangleRec({x + 10, (v.y + 1) * T - 22, T - 20, 22}, rgba(0x5a5a66ff));
            DrawRectangleRec({x + 6, (v.y + 1) * T - 26, T - 12, 6}, rgba(0x3a3a46ff));
            DrawRectangleRec({x + 12, (v.y + 1) * T - 20, 4, 18}, alpha(WHITE, 0.2f));
            if (ventOn(v)) {
                fx::beginAdd();
                fx::glow({x + T / 2, (v.y + 1) * T - 30}, 30, alpha(rgba(0xff8a4aff), 0.4f));
                fx::endAdd();
            }
        }
        rlPushMatrix();
        rlTranslatef(-camX_, 0, 0);
        steam_.draw();
        rlPopMatrix();
        for (auto& f : flags_) {
            float x = f.p.x - camX_;
            DrawLineEx({x, f.p.y}, {x, f.p.y - 96}, 3, rgba(0xd8d8e0ff));
            DrawCircleV({x, f.p.y - 98}, 4, col::gold);
            float raise = f.on ? 0 : 60;
            const Color st[3] = {rgba(0xae1c28ff), WHITE, rgba(0x21468bff)};
            for (int i = 0; i < 10; i++)
                for (int b = 0; b < 3; b++) {
                    float wv = std::sin(t_ * 6 - i * 0.7f) * 2.5f * (i / 10.0f);
                    Color c = f.on ? st[b] : alpha(rgba(0x8a8a9aff), 0.9f);
                    DrawRectangleRec({x + 2 + i * 4.0f, f.p.y - 94 + raise + b * 9 + wv, 4.5f, 9}, c);
                }
        }
        // the exit: a carillon bell under a little roof
        {
            Vector2 e{exit_.x - camX_, exit_.y};
            DrawLineEx({e.x - 26, e.y + 20}, {e.x - 26, e.y - 30}, 4, rgba(0x5a3a24ff));
            DrawLineEx({e.x + 26, e.y + 20}, {e.x + 26, e.y - 30}, 4, rgba(0x5a3a24ff));
            DrawTriangle({e.x - 34, e.y - 28}, {e.x + 34, e.y - 28}, {e.x, e.y - 52}, rgba(0x2a5a3aff));
            float sw = std::sin(t_ * 3) * 10;
            fx::beginAdd();
            fx::glow(e, 70, alpha(col::gold, 0.4f + 0.15f * std::sin(t_ * 4)));
            fx::endAdd();
            rlPushMatrix();
            rlTranslatef(e.x, e.y - 24, 0);
            rlRotatef(sw, 0, 0, 1);
            DrawCircleSector({0, 16}, 18, 180, 360, 16, rgba(0xc8861aff));
            DrawRectangleRec({-18, 16, 36, 10}, rgba(0xc8861aff));
            DrawRectangleRec({-22, 24, 44, 5}, rgba(0xffc531ff));
            DrawCircleV({0, 32}, 5, rgba(0x8a5a14ff));
            DrawCircleV({-7, 6}, 4, alpha(WHITE, 0.4f));
            rlPopMatrix();
            ui::text("FINISH", e.x, e.y - 66, 14, col::gold, ui::Center, true);
        }
        for (auto& w : wafels_) {
            if (w.taken) continue;
            float x = w.p.x - camX_;
            if (x < -30 || x > VW + 30) continue;
            float sq = std::abs(std::cos(t_ * 3 + w.p.x * 0.05f));
            rlPushMatrix();
            rlTranslatef(x, w.p.y + std::sin(t_ * 4 + w.p.x) * 3, 0);
            rlScalef(std::max(0.25f, sq), 1, 1);
            art::stroopwafelTop({0, 0}, 12);
            rlPopMatrix();
        }
        for (auto& b : bobas_) {
            if (b.taken) continue;
            Vector2 c{b.p.x - camX_, b.p.y + std::sin(t_ * 3) * 4};
            fx::beginAdd();
            fx::glow(c, 50, alpha(col::gold, 0.55f + 0.2f * std::sin(t_ * 5)));
            fx::endAdd();
            art::boba(c, 36, rgba(0xffd060ff));
        }
        for (auto& p : pigeons_) drawPigeon(p);
        for (auto& g : gulls_) drawGull(g);
    }

    void drawPigeon(const Pigeon& p) {
        float x = p.p.x - camX_, y = p.p.y;
        if (x < -40 || x > VW + 40) return;
        if (!p.alive) {
            if (p.deadT > 0.6f) return;
            DrawEllipse((int)x, (int)y - 4, 16, 4, alpha(rgba(0x8a8ea0ff), 1 - p.deadT / 0.6f));
            return;
        }
        float f = p.vx > 0 ? 1 : -1;
        float peck = std::max(0.0f, std::sin(p.t * 7)) * 5;
        DrawEllipse((int)x, (int)y - 1, 14, 3, alpha(BLACK, 0.25f));
        for (int k = -1; k <= 1; k += 2) DrawLineEx({x + k * 4, y - 8}, {x + k * 4 + std::sin(p.t * 14 + k) * 3, y}, 2, rgba(0xff7a5aff));
        DrawEllipse((int)x, (int)y - 14, 16, 10, col::outline);
        DrawEllipse((int)x, (int)y - 14, 14.5f, 8.5f, rgba(0x8a8ea0ff));
        DrawEllipse((int)(x - f * 4), (int)y - 15, 9, 5, rgba(0x6a6e80ff));
        DrawTriangle({x - f * 12, y - 18}, {x - f * 22, y - 14}, {x - f * 12, y - 10}, rgba(0x4a4e5aff));
        Vector2 head{x + f * 12, y - 22 + peck};
        DrawCircleV({x + f * 9, y - 18 + peck * 0.5f}, 6, rgba(0x5a8a7aff));  // the shiny neck
        DrawCircleV(head, 6.5f, col::outline);
        DrawCircleV(head, 5.5f, rgba(0x7a7e90ff));
        DrawCircleV(head + Vector2{f * 2, -1}, 1.6f, rgba(0xff8a3aff));
        DrawTriangle(head + Vector2{f * 5, -1}, head + Vector2{f * 5, 2}, head + Vector2{f * 10, 1}, rgba(0x3a3a42ff));
    }

    void drawGull(const Gull& g) {
        float x = g.p.x - camX_;
        if (x < -80 || x > VW + 80) return;
        if (!g.alive && g.deadT > 1.2f) return;
        float face = std::cos(g.t * 0.9f) >= 0 ? 1 : -1;
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();  // mirrored when flying left
        rlPushMatrix();
        rlTranslatef(x, g.p.y, 0);
        if (!g.alive) rlRotatef(g.deadT * 500, 0, 0, 1);
        rlScalef(face * 0.6f, 0.6f, 1);
        Texture2D& wing = E().tex("spr/gull_wing");
        Texture2D& body = E().tex("spr/gull_body");
        float wa = -30 + std::sin(g.t * 10) * 80;
        float wl = 0.72f * (0.3f + 0.7f * std::abs(std::sin(wa * DEG2RAD)));
        DrawTexturePro(wing, {0, 0, (float)wing.width, (float)wing.height}, {-6, -14, wing.width * wl, wing.height * 0.72f}, {2 * wl, 24 * 0.72f}, wa - 8, rgba(0xa0a8b8ff));
        DrawTexturePro(body, {0, 0, (float)body.width, (float)body.height}, {0, 0, body.width * 0.75f, body.height * 0.75f}, {63 * 0.75f, 46 * 0.75f}, 0, WHITE);
        DrawCircleV({36, -14}, 15, col::outline);
        DrawCircleV({36, -14}, 13, WHITE);
        DrawCircleV({40, -18}, 2.6f, col::outline);
        DrawTriangle({46, -16}, {46, -6}, {68, -10}, rgba(0xffc531ff));
        DrawCircleV({58, -8}, 2.4f, col::red);
        DrawTexturePro(wing, {0, 0, (float)wing.width, (float)wing.height}, {-2, -10, wing.width * wl, wing.height * 0.72f}, {2 * wl, 24 * 0.72f}, wa, WHITE);
        rlPopMatrix();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }

    void drawPlayer() {
        if (phase_ == Phase::Dead) {
            art::pieter({deadP_.x - camX_, deadP_.y}, 20, P().hat, phaseT_ * 720);
            return;
        }
        float x = px_ + kPW / 2 - camX_, feet = py_ + kPH;
        float sx = 1 + land_ * 0.25f - stretch_ * 0.15f, sy = 1 - land_ * 0.2f + stretch_ * 0.15f;
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();  // mirrored when running left
        rlPushMatrix();
        rlTranslatef(x, feet, 0);
        rlScalef(sx * face_, sy, 1);
        float run = ground_ ? levelT_ * std::abs(vx_) / 26 : 0;
        float sw = ground_ ? std::sin(run) * (std::abs(vx_) > 20 ? 1 : 0) : 0;
        Color jeans = rgba(0x2c3a5cff), hoodie = rgba(0x6b3fd1ff), shoe = rgba(0x15121cff);
        // legs
        Vector2 hip{0, -20};
        Vector2 f1, f2;
        if (!ground_ && wall_ == 0) f1 = {6, -6}, f2 = {-6, -2};
        else if (wall_ != 0) f1 = {8, -4}, f2 = {8, -14};
        else f1 = {sw * 10, 0}, f2 = {-sw * 10, 0};
        for (Vector2 f : {f2, f1}) {
            DrawLineEx(hip, f, 9, col::outline);
            DrawLineEx(hip, f, 6.5f, jeans);
            DrawRectangleRounded({f.x - 4, f.y - 4, 12, 6}, 0.6f, 4, shoe);
        }
        // body + arms
        DrawRectangleRounded({-10, -40, 20, 24}, 0.45f, 6, col::outline);
        DrawRectangleRounded({-8.5f, -38.5f, 17, 21}, 0.45f, 6, hoodie);
        Vector2 sh{2, -34};
        Vector2 hand = wall_ != 0 ? Vector2{12, -46} : !ground_ ? Vector2{10, -48} : Vector2{-sw * 8 + 4, -22};
        DrawLineEx(sh, hand, 7, col::outline);
        DrawLineEx(sh, hand, 4.5f, hoodie);
        rlPopMatrix();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        art::pieter({x + face_ * 2, feet - kPH + 4 - (1 - sy) * 20}, 17, P().hat, wall_ != 0 ? -face_ * 8 : !ground_ ? face_ * 6 : sw * 3);
    }

    void draw() override {
        if (phase_ == Phase::Select) {
            drawSelect();
            fx_.draw();
            return;
        }
        drawBackground();
        drawBuildings();
        drawThings();
        rlPushMatrix();
        rlTranslatef(-camX_, 0, 0);
        world_.draw();
        rlPopMatrix();
        drawPlayer();
        if (kLevels[level_].rain) {
            for (int i = 0; i < 140; i++) {
                float x = std::fmod(i * 97.0f - camX_ * 0.3f + t_ * 260, (float)VW + 100) - 50;
                float y = std::fmod(i * 53.0f + t_ * 900, (float)VH + 40) - 20;
                DrawLineEx({x, y}, {x - 6, y + 18}, 1.2f, alpha(rgba(0xb0c8e8ff), 0.45f));
            }
            if (lightning_ > 0) DrawRectangle(0, 0, VW, VH, alpha(WHITE, lightning_ * 0.35f));
        }
        if (kLevels[level_].dusk) DrawRectangle(0, 0, VW, VH, alpha(rgba(0xff6a3aff), 0.06f));
        fx_.draw();
        drawHud();
        if (paused_) drawPause();
        if (phase_ == Phase::Clear) drawClear();
    }

    void drawHud() {
        Rectangle r{20, 16, 420, 64};
        DrawRectangleRounded(r, 0.5f, 10, alpha(col::bgDeep, 0.75f));
        ui::text(TextFormat("%d. %s", level_ + 1, kLevels[level_].name), r.x + 20, r.y + 22, 18, WHITE, ui::Left, true);
        ui::text(TextFormat("%d:%05.2f", (int)time_ / 60, std::fmod(time_, 60.0f)), r.x + 20, r.y + 46, 16, col::gold, ui::Left, true);
        ui::text(TextFormat("deaths %d", deaths_), r.x + 120, r.y + 46, 14, col::muted, ui::Left, true);
        art::stroopwafelTop({r.x + 250, r.y + 40}, 10);
        ui::text(TextFormat("%d / %d", wafelsGot_, (int)wafels_.size()), r.x + 266, r.y + 41, 15, col::wafel, ui::Left, true);
        for (int k = 0; k < (int)bobas_.size(); k++) {
            Vector2 c{r.x + 340 + k * 26.0f, r.y + 32};
            if (bobas_[k].taken) art::boba(c, 26, rgba(0xffd060ff));
            else DrawCircleV(c, 9, alpha(WHITE, 0.12f));
        }
        ui::text("R  checkpoint     ESC  pause", VW - 20, 30, 12, alpha(WHITE, 0.6f), ui::Right, true);
    }

    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 220, 190, 440, 320};
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::orange, 0.6f));
        ui::textOutline("PAUSED", VW / 2.0f, p.y + 56, 56, WHITE, col::outline, 5);
        if (ui::button({p.x + 40, p.y + 110, 360, 50}, "RESUME", col::green, 22)) paused_ = false;
        if (ui::button({p.x + 40, p.y + 170, 360, 50}, "RESTART LEVEL", col::panelHi, 22)) load(level_);
        if (ui::button({p.x + 40, p.y + 230, 360, 50}, "LEVELS", col::red, 22)) phase_ = Phase::Select, paused_ = false;
    }

    void drawClear() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.5f));
        Rectangle p{VW / 2.0f - 300, 100, 600, 470};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::gold, 0.7f));
        fx::gradientText(level_ == kLevelCount - 1 ? "ON TOP OF AMSTERDAM!" : "LEVEL CLEAR!", VW / 2.0f, p.y + 60, level_ == kLevelCount - 1 ? 44 : 60, WHITE, col::gold, 5);
        ui::text(kLevels[level_].name, VW / 2.0f, p.y + 108, 18, col::muted, ui::Center, true);
        ui::textShadow(TextFormat("%d:%05.2f", (int)time_ / 60, std::fmod(time_, 60.0f)), VW / 2.0f, p.y + 168, 60, WHITE, ui::Center, true, 4);
        if (newBest_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("BEST TIME!", VW / 2.0f, p.y + 212, 20, col::gold, ui::Center, true);
        for (int i = 0; i < (int)bobas_.size(); i++) {
            Vector2 c{VW / 2.0f - 50 + i * 50.0f, p.y + 256};
            if (bobas_[i].taken) art::boba(c, 40, rgba(0xffd060ff));
            else DrawCircleV(c, 14, alpha(WHITE, 0.1f));
        }
        ui::text(TextFormat("%d / %d stroopwafels     %d deaths", wafelsGot_, (int)wafels_.size(), deaths_), VW / 2.0f, p.y + 302, 18, col::text, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 338}, 15);
        ui::text("+" + fmtNum(earned_) + " Pietercoins", VW / 2.0f - 66, p.y + 338, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            float bw = 170;
            bool next = level_ + 1 < kLevelCount;
            if (next && (ui::button({p.x + 30, p.y + 384, bw, 60}, "NEXT", col::orange, 22) || IsKeyPressed(KEY_ENTER))) {
                load(level_ + 1);
                return;
            }
            if (ui::button({p.x + 30 + (bw + 15) * (next ? 1 : 0), p.y + 384, bw, 60}, "RETRY", col::panelHi, 22)) load(level_);
            else if (ui::button({p.x + 30 + (bw + 15) * (next ? 2 : 1), p.y + 384, bw, 60}, "LEVELS", col::panelHi, 22)) phase_ = Phase::Select;
        }
    }

private:
    struct Building { int x0, x1, top, id; };
    std::vector<std::string> grid_;
    int W_ = 0, level_ = 0, selFocus_ = 0;
    std::vector<Building> buildings_;
    std::vector<Pigeon> pigeons_;
    std::vector<Gull> gulls_;
    std::vector<Vent> vents_;
    std::vector<Spring> springs_;
    std::vector<Mover> movers_;
    std::vector<Crumble> crumbles_;
    std::vector<Pickup> wafels_, bobas_;
    std::vector<Flag> flags_;
    Vector2 start_{}, exit_{}, respawn_{}, deadP_{}, deadV_{};
    Phase phase_ = Phase::Select;
    float t_ = 0, phaseT_ = 0, levelT_ = 0, time_ = 0, acc_ = 0, camX_ = 0;
    float px_ = 0, py_ = 0, vx_ = 0, vy_ = 0, coyote_ = 0, jumpBuf_ = 0, lock_ = 0, face_ = 1, land_ = 0, stretch_ = 0, lightning_ = 0;
    float hover_[kLevelCount]{};
    int dir_ = 0, wall_ = 0, riding_ = -1, wafelsGot_ = 0, deaths_ = 0;
    long long earned_ = 0;
    bool autoRun_ = std::getenv("PG_ROOF_AUTO") != nullptr;
    bool ground_ = false, jumpHeld_ = false, springing_ = false, paused_ = false, newBest_ = false;
    Particles fx_, world_, steam_;
};

}  // namespace

std::unique_ptr<Scene> makeRoof() { return std::make_unique<RoofScene>(); }

}  // namespace pg
