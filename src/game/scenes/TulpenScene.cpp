// TULPENMATCH - match-3 at the Dutch market.
// Swap neighbouring tiles to line up three or more of the same. Four in a row
// makes a striped tile (clears a row or column), an L or T makes a bomb (3x3),
// five in a row makes a rainbow Pieter that clears every tile of the colour you
// swap it with. Specials swapped together combine. Each level has a target score
// and a move limit; leftover moves are paid out as a bonus.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace pg {
namespace {

constexpr int N = 8;          // board size
constexpr int kTypes = 6;     // tile colours
constexpr float C = 74;       // cell size
constexpr float BX = 620, BY = 74;  // board origin
constexpr int kRainbow = -2;  // colourless rainbow tile

const char* kTex[kTypes] = {"spr/tile_tulip", "spr/tile_cheese", "spr/tile_klomp", "spr/tile_wafel", "spr/tile_sprout", "spr/tile_boba"};
const Color kCol[kTypes] = {rgba(0xff4a5aff), rgba(0xffc840ff), rgba(0x6a8ae8ff), rgba(0xd8903aff), rgba(0x6ad04aff), rgba(0xb07ae8ff)};
const char* kCheer[] = {"", "", "Nice!", "Beautiful!", "Awesome!", "Amazing!!", "UNBELIEVABLE!"};

enum class Sp { None, StripeH, StripeV, Bomb, Rainbow };
enum class St { Idle, Swap, SwapBack, Clear, Fall, Bonus, LevelDone, Over };

struct Tile {
    int type = -1;      // -1 empty, 0..5 colour, kRainbow
    Sp sp = Sp::None;
    float off = 0;      // vertical draw offset while falling (px, negative = above)
    float vel = 0;
    float pop = -1;     // >= 0 while being cleared
    float born = 1;     // 0..1 scale-in for new specials / spawned tiles
    float squash = 0;   // landing bounce
};

struct Cell { int x, y; };
bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
bool adjacent(Cell a, Cell b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y) == 1; }

struct Beam { Vector2 a, b; float t = 0, life = 0.4f, w = 22; Color c; };
struct Creation { Cell at; int type; Sp sp; };

class TulpenScene : public Scene {
public:
    Presence presence() const override {
        if (state_ == St::Over) return {"Tulip Match", "Game over - " + fmtNum(score_) + " points"};
        return {"Tulip Match", "Level " + std::to_string(level_) + " - " + fmtNum(score_) + " points"};
    }

    void enter() override {
        newGame();
        autoPlay_ = std::getenv("PG_TULP_AUTO") != nullptr;  // dev hook: a bot plays valid moves
        if (std::getenv("PG_TULP_SPECIALS")) {                // dev hook: show every special
            g_[1][2].sp = Sp::StripeH;
            g_[3][4].sp = Sp::StripeV;
            g_[5][1].sp = Sp::Bomb;
            g_[6][6] = Tile{kRainbow, Sp::Rainbow};
        }
    }

    // ============================================================ setup
    void newGame() {
        score_ = 0;
        shownScore_ = 0;
        level_ = 1;
        earned_ = 0;
        newBest_ = false;
        startLevel();
    }

    void startLevel() {
        levelStart_ = score_;
        target_ = 4000 + (level_ - 1) * 2600 + (level_ - 1) * (level_ - 1) * 300;
        moves_ = std::max(15, 22 - level_ / 2);
        fill();
        state_ = St::Fall;  // the whole board drops in
        stateT_ = 0;
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++) {
                g_[x][y].off = -(N + 2) * C - (N - y) * 12 - x * 18;
                g_[x][y].vel = 0;
            }
        combo_ = 0;
        idleT_ = 0;
        sel_ = {-1, -1};
        cheer_.clear();
        bannerT_ = 0;
        levelBannerT_ = 0;
    }

    void fill() {
        // random board without ready-made matches, with at least one move
        for (int tries = 0; tries < 200; tries++) {
            for (int x = 0; x < N; x++)
                for (int y = 0; y < N; y++) {
                    int t;
                    do t = rndi(0, kTypes - 1);
                    while ((x >= 2 && g_[x - 1][y].type == t && g_[x - 2][y].type == t) || (y >= 2 && g_[x][y - 1].type == t && g_[x][y - 2].type == t));
                    g_[x][y] = Tile{t};
                }
            if (hasMove()) return;
        }
    }

    // ============================================================ matching
    Tile& at(Cell c) { return g_[c.x][c.y]; }
    static bool inside(Cell c) { return c.x >= 0 && c.y >= 0 && c.x < N && c.y < N; }
    bool colour(int x, int y) const { return g_[x][y].type >= 0; }

    struct Run { std::vector<Cell> cells; bool horiz; };

    std::vector<Run> findRuns() const {
        std::vector<Run> runs;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N;) {
                int e = x + 1;
                while (colour(x, y) && e < N && g_[e][y].type == g_[x][y].type) e++;
                if (colour(x, y) && e - x >= 3) {
                    Run r{{}, true};
                    for (int k = x; k < e; k++) r.cells.push_back({k, y});
                    runs.push_back(r);
                }
                x = e;
            }
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N;) {
                int e = y + 1;
                while (colour(x, y) && e < N && g_[x][e].type == g_[x][y].type) e++;
                if (colour(x, y) && e - y >= 3) {
                    Run r{{}, false};
                    for (int k = y; k < e; k++) r.cells.push_back({x, k});
                    runs.push_back(r);
                }
                y = e;
            }
        return runs;
    }

    bool matchAt(Cell c) const {
        int t = g_[c.x][c.y].type;
        if (t < 0) return false;
        int h = 1, v = 1;
        for (int x = c.x - 1; x >= 0 && g_[x][c.y].type == t; x--) h++;
        for (int x = c.x + 1; x < N && g_[x][c.y].type == t; x++) h++;
        for (int y = c.y - 1; y >= 0 && g_[c.x][y].type == t; y--) v++;
        for (int y = c.y + 1; y < N && g_[c.x][y].type == t; y++) v++;
        return h >= 3 || v >= 3;
    }

    bool special(Cell c) const { return g_[c.x][c.y].sp != Sp::None; }

    // would swapping a and b do anything?
    bool goodSwap(Cell a, Cell b) {
        if (at(a).type == kRainbow || at(b).type == kRainbow) return true;
        if (special(a) && special(b)) return true;
        std::swap(at(a), at(b));
        bool ok = matchAt(a) || matchAt(b);
        std::swap(at(a), at(b));
        return ok;
    }

    bool findMove(Cell& a, Cell& b) {
        std::vector<std::pair<Cell, Cell>> moves;
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++) {
                if (x + 1 < N && goodSwap({x, y}, {x + 1, y})) moves.push_back({{x, y}, {x + 1, y}});
                if (y + 1 < N && goodSwap({x, y}, {x, y + 1})) moves.push_back({{x, y}, {x, y + 1}});
            }
        if (moves.empty()) return false;
        auto& m = moves[rndi(0, (int)moves.size() - 1)];
        a = m.first;
        b = m.second;
        return true;
    }
    bool hasMove() {
        Cell a, b;
        return findMove(a, b);
    }

    // ============================================================ clearing
    void mark(Cell c) {
        if (!inside(c) || at(c).type == -1) return;
        if (!mask_[c.x][c.y]) {
            mask_[c.x][c.y] = true;
            queue_.push_back(c);
        }
    }

    Vector2 pos(Cell c) const { return {BX + c.x * C + C / 2, BY + c.y * C + C / 2}; }

    void beam(Vector2 a, Vector2 b, Color c, float w = 26) { beams_.push_back({a, b, 0, 0.45f, w, c}); }

    // the most common colour on the board (what a rainbow caught in a blast eats)
    int commonColour() const {
        int n[kTypes]{};
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++)
                if (g_[x][y].type >= 0) n[g_[x][y].type]++;
        return (int)(std::max_element(n, n + kTypes) - n);
    }

    void eatColour(Cell from, int t) {
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++)
                if (g_[x][y].type == t) {
                    mark({x, y});
                    beam(pos(from), pos({x, y}), lighten(kCol[t], 0.3f), 8);
                }
        E().sfx.play(Sfx::Power, 0.6f, 1.2f);
        E().flash(lighten(kCol[t], 0.5f), 0.25f);
    }

    void activate(Cell c) {
        Tile& t = at(c);
        Vector2 p = pos(c);
        Color k = t.type >= 0 ? kCol[t.type] : WHITE;
        switch (t.sp) {
            case Sp::StripeH:
                for (int x = 0; x < N; x++) mark({x, c.y});
                beam({BX - 20, p.y}, {BX + N * C + 20, p.y}, lighten(k, 0.4f));
                E().sfx.play(Sfx::Zap, 0.6f, rnd(0.9f, 1.1f), (p.x - VW / 2.0f) / (VW / 2.0f));
                E().shake(5);
                break;
            case Sp::StripeV:
                for (int y = 0; y < N; y++) mark({c.x, y});
                beam({p.x, BY - 20}, {p.x, BY + N * C + 20}, lighten(k, 0.4f));
                E().sfx.play(Sfx::Zap, 0.6f, rnd(0.9f, 1.1f), (p.x - VW / 2.0f) / (VW / 2.0f));
                E().shake(5);
                break;
            case Sp::Bomb:
                for (int dx = -1; dx <= 1; dx++)
                    for (int dy = -1; dy <= 1; dy++) mark({c.x + dx, c.y + dy});
                shocks_.add(p, 10, C * 2.2f, lighten(k, 0.3f), 0.45f, 16);
                fx_.burst(p, 26, k, 520, 8, 0.6f, PShape::Star);
                E().sfx.play(Sfx::Boom, 0.55f, 1.3f);
                E().shake(9);
                break;
            case Sp::Rainbow: eatColour(c, commonColour()); break;
            case Sp::None: break;
        }
        t.sp = Sp::None;  // used up
    }

    void area(Cell c, int r) {
        for (int dx = -r; dx <= r; dx++)
            for (int dy = -r; dy <= r; dy++) mark({c.x + dx, c.y + dy});
    }

    // Resolve the mask: activate every special inside it (they can chain), then start popping.
    void resolve() {
        for (size_t i = 0; i < queue_.size(); i++) {
            Cell c = queue_[i];
            if (special(c)) activate(c);
        }
        queue_.clear();
        for (auto& cr : creations_) mask_[cr.at.x][cr.at.y] = false;
        int n = 0;
        Vector2 sum{};
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++)
                if (mask_[x][y]) {
                    g_[x][y].pop = 0;
                    n++;
                    sum = sum + pos({x, y});
                }
        for (auto& cr : creations_) {
            Tile& t = at(cr.at);
            t.type = cr.type;
            t.sp = cr.sp;
            t.born = 0;
            t.pop = -1;
            fx_.burst(pos(cr.at), 16, WHITE, 260, 5, 0.5f, PShape::Star);
            shocks_.add(pos(cr.at), 6, C * 0.9f, cr.type >= 0 ? kCol[cr.type] : WHITE, 0.35f, 8);
        }
        if (!creations_.empty()) E().sfx.play(Sfx::Spawn, 0.6f, 1.2f);
        creations_.clear();
        int mult = std::max(1, combo_);
        int pts = n * 60 * mult;
        score_ += pts;
        if (n > 0) {
            fx_.floatText("+" + fmtNum(pts), sum * (1.0f / n) + Vector2{0, -10}, combo_ >= 3 ? col::gold : WHITE, 22 + std::min(combo_, 5) * 3.0f);
            E().sfx.play(Sfx::Match, 0.55f, std::min(2.2f, 1.0f + 0.12f * (combo_ - 1)));
        }
        if (combo_ >= 2 && combo_ < 7) cheer(kCheer[combo_]);
        else if (combo_ >= 7) cheer(kCheer[6]);
        state_ = St::Clear;
        stateT_ = 0;
    }

    void cheer(const std::string& s) {
        cheer_ = s;
        bannerT_ = 0;
        if (combo_ >= 4) E().sfx.play(Sfx::Perfect, 0.5f);
    }

    // a normal match after a swap or a cascade
    void startMatches(Cell swapA, Cell swapB) {
        auto runs = findRuns();
        clearMask();
        // group runs that share cells
        std::vector<int> parent(runs.size());
        for (size_t i = 0; i < runs.size(); i++) parent[i] = (int)i;
        std::function<int(int)> root = [&](int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
        for (size_t i = 0; i < runs.size(); i++)
            for (size_t j = i + 1; j < runs.size(); j++)
                for (Cell a : runs[i].cells)
                    for (Cell b : runs[j].cells)
                        if (a == b) parent[root((int)i)] = root((int)j);
        std::vector<bool> done(runs.size(), false);
        for (size_t i = 0; i < runs.size(); i++) {
            int r = root((int)i);
            if (done[r]) continue;
            done[r] = true;
            std::vector<Cell> cells;
            int longest = 0;
            bool h = false, v = false;
            Run* longRun = nullptr;
            for (size_t j = 0; j < runs.size(); j++)
                if (root((int)j) == r) {
                    for (Cell c : runs[j].cells)
                        if (std::find(cells.begin(), cells.end(), c) == cells.end()) cells.push_back(c);
                    if ((int)runs[j].cells.size() > longest) {
                        longest = (int)runs[j].cells.size();
                        longRun = &runs[j];
                    }
                    (runs[j].horiz ? h : v) = true;
                }
            for (Cell c : cells) mark(c);
            // which special does this shape make?
            Sp make = Sp::None;
            if (longest >= 5) make = Sp::Rainbow;
            else if (h && v) make = Sp::Bomb;
            else if (longest == 4) make = longRun->horiz ? Sp::StripeV : Sp::StripeH;
            if (make == Sp::None) continue;
            Cell spot = longRun->cells[longRun->cells.size() / 2];
            if (h && v) {  // the corner of the L / middle of the T
                for (size_t j = 0; j < runs.size(); j++)
                    if (root((int)j) == r)
                        for (size_t k = 0; k < runs.size(); k++)
                            if (k != j && root((int)k) == r)
                                for (Cell a : runs[j].cells)
                                    for (Cell b : runs[k].cells)
                                        if (a == b) spot = a;
            }
            for (Cell c : cells)
                if (c == swapA || c == swapB) spot = c;
            int type = at(spot).type;
            // if the spot already holds a special, it still goes off
            creations_.push_back({spot, make == Sp::Rainbow ? kRainbow : type, make});
        }
        resolve();
    }

    void clearMask() {
        for (auto& col : mask_) col.fill(false);
        queue_.clear();
        creations_.clear();
    }

    // two specials (or a rainbow) swapped together
    void startCombo(Cell a, Cell b) {
        clearMask();
        Tile ta = at(a), tb = at(b);
        if (ta.type == kRainbow && tb.type != kRainbow) std::swap(ta, tb), std::swap(a, b);
        // now: if one is a rainbow, it's `a`
        mark(a);
        mark(b);
        bool stripeA = ta.sp == Sp::StripeH || ta.sp == Sp::StripeV, stripeB = tb.sp == Sp::StripeH || tb.sp == Sp::StripeV;
        at(a).sp = Sp::None;
        at(b).sp = Sp::None;
        Vector2 pb = pos(b);
        if (ta.type == kRainbow && tb.type == kRainbow) {
            for (int x = 0; x < N; x++)
                for (int y = 0; y < N; y++) mark({x, y});
            shocks_.add(pb, 10, 700, WHITE, 0.8f, 30);
            E().sfx.play(Sfx::Boom, 0.9f, 0.8f);
            E().sfx.play(Sfx::Power, 0.8f);
            E().flash(WHITE, 0.6f);
            E().shake(20);
            cheer("BOARD CLEARED!");
        } else if (ta.type == kRainbow) {
            int t = tb.type;
            if (tb.sp == Sp::None) {
                eatColour(a, t);
            } else {
                // every tile of that colour becomes the same special, then they all go off
                for (int x = 0; x < N; x++)
                    for (int y = 0; y < N; y++)
                        if (g_[x][y].type == t) {
                            g_[x][y].sp = tb.sp == Sp::Bomb ? Sp::Bomb : (chance(0.5f) ? Sp::StripeH : Sp::StripeV);
                            mark({x, y});
                            beam(pos(a), pos({x, y}), lighten(kCol[t], 0.4f), 8);
                        }
                E().sfx.play(Sfx::Power, 0.8f, 0.9f);
                cheer("RAINBOW!");
            }
        } else if (stripeA && stripeB) {
            for (int k = 0; k < N; k++) mark({k, b.y}), mark({b.x, k});
            beam({BX - 20, pb.y}, {BX + N * C + 20, pb.y}, WHITE);
            beam({pb.x, BY - 20}, {pb.x, BY + N * C + 20}, WHITE);
            E().sfx.play(Sfx::Zap, 0.8f, 0.8f);
            E().shake(10);
        } else if ((stripeA && tb.sp == Sp::Bomb) || (stripeB && ta.sp == Sp::Bomb)) {
            for (int d = -1; d <= 1; d++)
                for (int k = 0; k < N; k++) mark({k, b.y + d}), mark({b.x + d, k});
            beam({BX - 20, pb.y}, {BX + N * C + 20, pb.y}, WHITE, 70);
            beam({pb.x, BY - 20}, {pb.x, BY + N * C + 20}, WHITE, 70);
            E().sfx.play(Sfx::Zap, 0.9f, 0.7f);
            E().sfx.play(Sfx::Boom, 0.6f, 1.2f);
            E().shake(14);
            cheer("CROSS BLAST!");
        } else {  // bomb + bomb
            area(b, 2);
            shocks_.add(pb, 10, C * 4, col::orange, 0.6f, 24);
            fx_.burst(pb, 50, col::orange, 700, 10, 0.8f, PShape::Star);
            E().sfx.play(Sfx::Boom, 0.9f, 1.0f);
            E().shake(18);
            cheer("KABOOM!");
        }
        combo_ = std::max(combo_, 1);
        resolve();
    }

    // ============================================================ falling
    void gravity() {
        for (int x = 0; x < N; x++) {
            int write = N - 1;
            for (int y = N - 1; y >= 0; y--) {
                if (g_[x][y].type == -1) continue;
                if (write != y) {
                    g_[x][write] = g_[x][y];
                    g_[x][write].off -= (write - y) * C;
                    g_[x][y] = Tile{};
                }
                write--;
            }
            int missing = write + 1;
            for (int y = write; y >= 0; y--) {
                g_[x][y] = Tile{rndi(0, kTypes - 1)};
                g_[x][y].off = -missing * C - 20;
            }
        }
    }

    bool falling() const {
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++)
                if (g_[x][y].off < 0) return true;
        return false;
    }

    void shuffleBoard() {
        // keep specials where they are, reshuffle the plain tiles until there's a move and no match
        std::vector<int> types;
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++)
                if (g_[x][y].type >= 0 && g_[x][y].sp == Sp::None) types.push_back(g_[x][y].type);
        for (int tries = 0; tries < 300; tries++) {
            std::shuffle(types.begin(), types.end(), rng());
            size_t i = 0;
            for (int x = 0; x < N; x++)
                for (int y = 0; y < N; y++)
                    if (g_[x][y].type >= 0 && g_[x][y].sp == Sp::None) g_[x][y].type = types[i++];
            if (findRuns().empty() && hasMove()) break;
            if (tries == 299) fill();
        }
        shuffleT_ = 1;
        cheer_ = "Shuffle!";
        bannerT_ = 0;
        E().sfx.play(Sfx::Whoosh, 0.7f);
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        stateT_ += dt;
        fx_.update(dt);
        shocks_.update(dt);
        for (auto& b : beams_) b.t += dt;
        std::erase_if(beams_, [](const Beam& b) { return b.t > b.life; });
        bannerT_ += dt;
        levelBannerT_ += dt;
        shuffleT_ = std::max(0.0f, shuffleT_ - dt * 2);
        shownScore_ = std::min<long long>(score_, shownScore_ + std::max<long long>(1, (long long)((score_ - shownScore_) * dt * 8)));
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++) {
                Tile& tl = g_[x][y];
                tl.born = std::min(1.0f, tl.born + dt * 4);
                tl.squash = std::max(0.0f, tl.squash - dt * 5);
            }
        petals(dt);

        switch (state_) {
            case St::Idle: updateIdle(dt); break;
            case St::Swap:
            case St::SwapBack:
                if (stateT_ >= kSwapTime) finishSwap();
                break;
            case St::Clear:
                for (int x = 0; x < N; x++)
                    for (int y = 0; y < N; y++)
                        if (g_[x][y].pop >= 0) g_[x][y].pop += dt;
                if (stateT_ >= 0.24f) {
                    for (int x = 0; x < N; x++)
                        for (int y = 0; y < N; y++)
                            if (g_[x][y].pop >= 0) {
                                int t = g_[x][y].type;
                                Color k = t >= 0 ? kCol[t] : WHITE;
                                fx_.burst(pos({x, y}), 7, k, 280, 6, 0.5f, t == 0 ? PShape::Heart : PShape::Circle, 500);
                                fx_.burst(pos({x, y}), 3, WHITE, 160, 4, 0.35f, PShape::Spark);
                                g_[x][y] = Tile{};
                            }
                    gravity();
                    state_ = St::Fall;
                    stateT_ = 0;
                }
                break;
            case St::Fall: updateFall(dt); break;
            case St::Bonus: updateBonus(dt); break;
            case St::LevelDone:
            case St::Over: break;
        }
    }

    static constexpr float kSwapTime = 0.17f;

    void updateFall(float dt) {
        bool landed = false;
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++) {
                Tile& tl = g_[x][y];
                if (tl.off >= 0) continue;
                tl.vel += 3200 * dt;
                tl.off += tl.vel * dt;
                if (tl.off >= 0) {
                    tl.off = 0;
                    tl.squash = std::min(1.0f, tl.vel / 1400);
                    tl.vel = 0;
                    landed = true;
                }
            }
        if (landed && t_ - lastTick_ > 0.05f) {
            lastTick_ = t_;
            E().sfx.play(Sfx::Tick, 0.12f, rnd(0.8f, 1.1f));
        }
        if (falling()) return;
        if (!findRuns().empty()) {
            combo_++;
            startMatches({-1, -1}, {-1, -1});
            return;
        }
        combo_ = 0;
        settle();
    }

    // the board is still: what now?
    void settle() {
        state_ = St::Idle;
        stateT_ = 0;
        idleT_ = 0;
        if (score_ - levelStart_ >= target_) {
            state_ = St::Bonus;
            bonusT_ = 0;
            cheer_ = "TARGET REACHED!";
            bannerT_ = 0;
            E().sfx.play(Sfx::Win, 0.6f);
            fx_.confetti({BX + N * C / 2, BY + 40}, 90);
            return;
        }
        if (moves_ <= 0) {
            gameOver();
            return;
        }
        if (!hasMove()) shuffleBoard();
    }

    void updateBonus(float dt) {
        bonusT_ += dt;
        if (bonusT_ < 0.8f) return;
        bonusAcc_ += dt;
        if (moves_ > 0 && bonusAcc_ >= 0.14f) {
            bonusAcc_ = 0;
            moves_--;
            Cell c{rndi(0, N - 1), rndi(0, N - 1)};
            int pts = 250 + level_ * 50;
            score_ += pts;
            g_[c.x][c.y].squash = 1;
            fx_.burst(pos(c), 14, col::gold, 300, 6, 0.5f, PShape::Star);
            fx_.floatText("+" + fmtNum(pts), pos(c), col::gold, 20);
            E().sfx.play(Sfx::Coin, 0.35f, 1.0f + bonusCount_ * 0.04f);
            bonusCount_++;
        }
        if (moves_ <= 0 && bonusT_ > 1.2f && shownScore_ >= score_) {
            state_ = St::LevelDone;
            stateT_ = 0;
            bonusCount_ = 0;
            long long coins = 40 + level_ * 20;
            earned_ += coins;
            levelCoins_ = coins;
            P().earn(coins);
            if (score_ > P().stats.tulpBest) {
                P().stats.tulpBest = (int)std::min<long long>(score_, 2000000000);
                newBest_ = true;
            }
            P().save();
        }
    }

    void gameOver() {
        state_ = St::Over;
        stateT_ = 0;
        long long coins = score_ / 300;
        earned_ += coins;
        if (coins > 0) P().earn(coins);
        if (score_ > P().stats.tulpBest) {
            P().stats.tulpBest = (int)std::min<long long>(score_, 2000000000);
            newBest_ = true;
        }
        P().save();
        E().sfx.play(Sfx::Lose, 0.6f);
    }

    Cell cellAt(Vector2 m) const {
        if (m.x < BX || m.y < BY) return {-1, -1};
        Cell c{(int)((m.x - BX) / C), (int)((m.y - BY) / C)};
        return inside(c) ? c : Cell{-1, -1};
    }

    void trySwap(Cell a, Cell b) {
        if (!inside(a) || !inside(b) || !adjacent(a, b)) return;
        swapA_ = a;
        swapB_ = b;
        state_ = St::Swap;
        stateT_ = 0;
        sel_ = {-1, -1};
        E().sfx.play(Sfx::Swap, 0.5f, rnd(0.95f, 1.05f));
    }

    void finishSwap() {
        if (state_ == St::SwapBack) {  // already swapped back logically; the animation is done
            state_ = St::Idle;
            stateT_ = 0;
            return;
        }
        std::swap(at(swapA_), at(swapB_));
        Tile &a = at(swapA_), &b = at(swapB_);
        bool combo = a.type == kRainbow || b.type == kRainbow || (a.sp != Sp::None && b.sp != Sp::None);
        if (combo) {
            moves_--;
            combo_ = 1;
            startCombo(swapA_, swapB_);
            return;
        }
        if (matchAt(swapA_) || matchAt(swapB_)) {
            moves_--;
            combo_ = 1;
            startMatches(swapA_, swapB_);
            return;
        }
        // no match: swap back (the SwapBack animation plays it in reverse)
        std::swap(at(swapA_), at(swapB_));
        state_ = St::SwapBack;
        stateT_ = 0;
        E().sfx.play(Sfx::Error, 0.25f, 1.3f);
    }

    void updateIdle(float dt) {
        idleT_ += dt;
        if (autoPlay_ && idleT_ > 0.35f) {
            Cell a, b;
            if (findMove(a, b)) trySwap(a, b);
            return;
        }
        Vector2 m = E().mouse();
        Cell c = cellAt(m);
        if (E().mousePressed() && inside(c)) {
            E().consumeClick();
            idleT_ = 0;
            if (inside(sel_) && adjacent(sel_, c)) {
                trySwap(sel_, c);
            } else if (sel_ == c) {
                sel_ = {-1, -1};
            } else {
                sel_ = c;
                dragFrom_ = m;
                dragging_ = true;
                E().sfx.play(Sfx::Click, 0.3f, 1.3f);
            }
        }
        if (dragging_ && E().mouseDown() && inside(sel_)) {
            Vector2 d = m - dragFrom_;
            if (std::abs(d.x) > C * 0.4f || std::abs(d.y) > C * 0.4f) {
                Cell to = sel_;
                if (std::abs(d.x) > std::abs(d.y)) to.x += d.x > 0 ? 1 : -1;
                else to.y += d.y > 0 ? 1 : -1;
                dragging_ = false;
                if (inside(to)) trySwap(sel_, to);
            }
        }
        if (!E().mouseDown()) dragging_ = false;
        // hint after a while
        if (idleT_ > 6 && !inside(hintA_)) findMove(hintA_, hintB_);
        if (idleT_ < 6) hintA_ = hintB_ = {-1, -1};
    }

    // falling tulip petals in the background
    void petals(float dt) {
        petalT_ -= dt;
        if (petalT_ <= 0) {
            petalT_ = 0.25f;
            Particle p;
            p.pos = {rnd(0, VW), -20};
            p.vel = {rnd(20, 60), rnd(40, 80)};
            p.life = p.maxLife = 12;
            p.size = rnd(4, 7);
            p.endSize = p.size;
            p.rotVel = rnd(-3, 3);
            p.color = alpha(kCol[rndi(0, 1) ? 0 : 5], 0.45f);
            p.endColor = p.color;
            p.shape = PShape::Heart;
            bg_.add(p);
        }
        bg_.update(dt);
    }

    // ============================================================ draw
    Vector2 drawPos(int x, int y) const {
        Cell c{x, y};
        Vector2 p = pos(c);
        p.y += g_[x][y].off;
        if (state_ == St::Swap || state_ == St::SwapBack) {
            float k = easeInOutSine(stateT_ / kSwapTime);
            if (state_ == St::SwapBack) k = 1 - k;
            if (c == swapA_) p = Vector2Lerp(pos(swapA_), pos(swapB_), k);
            if (c == swapB_) p = Vector2Lerp(pos(swapB_), pos(swapA_), k);
        }
        return p;
    }

    void drawTile(int x, int y) {
        const Tile& tl = g_[x][y];
        if (tl.type == -1) return;
        Vector2 p = drawPos(x, y);
        if (p.y < BY - C) return;
        Cell c{x, y};
        float s = 1;
        if (tl.pop >= 0) {
            float k = tl.pop / 0.24f;
            s = k < 0.35f ? 1 + k * 0.8f : (1.28f) * (1 - (k - 0.35f) / 0.65f);
        }
        s *= easeOutBack(tl.born);
        bool selected = c == sel_;
        bool hint = (c == hintA_ || c == hintB_) && state_ == St::Idle;
        if (selected) s *= 1.1f + std::sin(t_ * 8) * 0.03f;
        float rot = hint ? std::sin(t_ * 14) * 10 : 0;
        if (shuffleT_ > 0) rot += shuffleT_ * 360 * ((x + y) % 2 ? 1 : -1);
        float sx = s * (1 + tl.squash * 0.18f), sy = s * (1 - tl.squash * 0.18f);
        Vector2 dp{p.x, p.y + (1 - sy) * C * 0.35f};
        if (selected || hint) {
            fx::beginAdd();
            fx::glow(dp, C * 0.7f, alpha(selected ? WHITE : col::gold, 0.35f));
            fx::endAdd();
        }
        fx::softShadow({dp.x + 2, dp.y + C * 0.36f}, C * 0.3f * sx, C * 0.07f, 0.35f);
        float base = (C * 0.84f) / 128;
        if (tl.type == kRainbow) {
            drawRainbow(dp, s);
        } else {
            Texture2D& tex = E().tex(kTex[tl.type]);
            if (sx == sy) fx::sprite(tex, dp, base * s, rot);
            else DrawTexturePro(tex, {0, 0, (float)tex.width, (float)tex.height}, {dp.x, dp.y, 128 * base * sx, 128 * base * sy}, {64 * base * sx, 64 * base * sy}, rot, WHITE);
        }
        if (tl.pop >= 0 && tl.pop < 0.12f) {
            fx::beginAdd();
            fx::glow(dp, C * 0.6f, alpha(WHITE, 0.8f * (1 - tl.pop / 0.12f)));
            fx::endAdd();
        }
        switch (tl.sp) {
            case Sp::StripeH:
            case Sp::StripeV: {
                bool h = tl.sp == Sp::StripeH;
                float w = C * 0.36f * s;
                fx::beginAdd();
                for (int k = -1; k <= 1; k++) {
                    float o = k * 11 * s;
                    Vector2 a = h ? Vector2{dp.x - w, dp.y + o} : Vector2{dp.x + o, dp.y - w}, b = h ? Vector2{dp.x + w, dp.y + o} : Vector2{dp.x + o, dp.y + w};
                    fx::glowLine(a, b, 6, alpha(WHITE, 0.55f + 0.25f * std::sin(t_ * 6 + k)));
                }
                fx::endAdd();
                for (int k : {-1, 1}) {  // arrow heads
                    Vector2 tip = h ? Vector2{dp.x + k * C * 0.46f * s, dp.y} : Vector2{dp.x, dp.y + k * C * 0.46f * s};
                    Vector2 a = h ? Vector2{tip.x - k * 9 * s, tip.y - 7 * s} : Vector2{tip.x - 7 * s, tip.y - k * 9 * s};
                    Vector2 b = h ? Vector2{tip.x - k * 9 * s, tip.y + 7 * s} : Vector2{tip.x + 7 * s, tip.y - k * 9 * s};
                    DrawTriangle(tip, a, b, WHITE);
                    DrawTriangle(tip, b, a, WHITE);
                }
                break;
            }
            case Sp::Bomb: {
                float pulse = 0.5f + 0.5f * std::sin(t_ * 5 + x + y);
                fx::beginAdd();
                fx::glow(dp, C * 0.62f * s, alpha(kCol[tl.type], 0.45f + 0.25f * pulse));
                fx::endAdd();
                DrawRing(dp, C * 0.4f * s, C * 0.45f * s, 0, 360, 36, alpha(WHITE, 0.8f));
                for (int k = 0; k < 4; k++) {
                    Vector2 o = dp + fromAngle(t_ * 2.5f + k * PI / 2, C * 0.425f * s);
                    DrawCircleV(o, 3.5f * s, col::gold);
                }
                break;
            }
            default: break;
        }
    }

    void drawRainbow(Vector2 c, float s) {
        float r = C * 0.38f * s;
        fx::beginAdd();
        fx::glow(c, r * 1.8f, alpha(WHITE, 0.35f));
        fx::endAdd();
        for (int i = 0; i < 12; i++) {
            float a0 = t_ * 90 + i * 30;
            DrawRing(c, r * 0.82f, r * 1.12f, a0, a0 + 30, 6, ColorFromHSV(std::fmod(a0, 360.0f), 0.75f, 1));
        }
        art::pieter(c, r * 0.8f, "", std::sin(t_ * 3) * 8);
    }

    void drawBoard() {
        Rectangle board{BX, BY, N * C, N * C};
        Rectangle frame = shrink(board, -18);
        fx::softShadow({frame.x + frame.width / 2, frame.y + frame.height + 6}, frame.width * 0.55f, 26, 0.6f);
        DrawRectangleRounded(shrink(frame, -3), 0.05f, 10, col::outline);
        DrawRectangleRounded(frame, 0.05f, 10, rgba(0x6a4428ff));
        fx::tiled(E().tex("tex/wood"), shrink(frame, 6), 0.5f, {0, 0}, rgba(0xd8a878ff));
        DrawRectangleRoundedLinesEx(shrink(frame, 3), 0.05f, 10, 2, alpha(WHITE, 0.25f));
        DrawRectangleRec(shrink(board, -4), col::outline);
        for (int x = 0; x < N; x++)
            for (int y = 0; y < N; y++) {
                Rectangle r{BX + x * C, BY + y * C, C, C};
                DrawRectangleRec(r, (x + y) % 2 ? rgba(0x2a1e3eff) : rgba(0x342650ff));
            }
        DrawRectangleGradientV((int)BX, (int)BY, (int)(N * C), 60, alpha(BLACK, 0.35f), alpha(BLACK, 0));
        // hover cell
        Cell hc = cellAt(E().mouse());
        if (inside(hc) && state_ == St::Idle) DrawRectangleRec({BX + hc.x * C, BY + hc.y * C, C, C}, alpha(WHITE, 0.07f));
        if (inside(sel_)) DrawRectangleRoundedLinesEx({BX + sel_.x * C + 3, BY + sel_.y * C + 3, C - 6, C - 6}, 0.2f, 8, 3, alpha(WHITE, 0.8f));
        E().beginClip({BX, BY - 2, N * C, N * C + 4});
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                bool moving = (state_ == St::Swap || state_ == St::SwapBack) && (Cell{x, y} == swapA_ || Cell{x, y} == swapB_);
                if (!moving) drawTile(x, y);
            }
        if (state_ == St::Swap || state_ == St::SwapBack) {
            drawTile(swapB_.x, swapB_.y);
            drawTile(swapA_.x, swapA_.y);
        }
        E().endClip();
        // beams
        fx::beginAdd();
        for (auto& b : beams_) {
            float k = b.t / b.life;
            float w = b.w * (1 - k) * (k < 0.1f ? k / 0.1f : 1);
            fx::glowLine(b.a, b.b, w * 2.2f, alpha(b.c, 0.5f * (1 - k)));
            fx::glowLine(b.a, b.b, w * 0.6f, alpha(WHITE, 0.9f * (1 - k)));
        }
        fx::endAdd();
        shocks_.draw();
    }

    void drawPanel() {
        Rectangle p{40, 74, 540, 592};
        ui::panel(p, alpha(col::panel, 0.92f), 0.06f, alpha(col::pink, 0.5f));
        fx::gradientText("TULIP MATCH", p.x + p.width / 2, p.y + 52, 54, rgba(0xffe0e8ff), col::pink, 4);
        ui::text("LEVEL " + std::to_string(level_), p.x + p.width / 2, p.y + 98, 22, col::gold, ui::Center, true);
        // score
        ui::text("SCORE", p.x + 40, p.y + 150, 16, col::muted, ui::Left, true);
        ui::textShadow(fmtNum(shownScore_), p.x + 40, p.y + 186, 44, WHITE, ui::Left, true, 3);
        ui::text("best " + fmtNum(std::max<long long>(P().stats.tulpBest, 0)), p.x + 40, p.y + 222, 15, col::muted, ui::Left, true);
        // moves bubble
        Vector2 mc{p.x + p.width - 100, p.y + 180};
        bool low = moves_ <= 5 && state_ != St::Bonus && state_ != St::LevelDone;
        float pulse = low ? 0.5f + 0.5f * std::sin(t_ * 8) : 0;
        fx::beginAdd();
        fx::glow(mc, 90, alpha(low ? col::red : col::pink, 0.25f + 0.25f * pulse));
        fx::endAdd();
        fx::shadedBall(mc, 54, low ? mix(col::pink, col::red, pulse) : rgba(0xc2418aff), 3);
        ui::textShadow(std::to_string(std::max(0, moves_)), mc.x, mc.y - 6, 46, WHITE, ui::Center, true, 2);
        ui::text("MOVES", mc.x, mc.y + 28, 14, alpha(WHITE, 0.8f), ui::Center, true);
        // target bar with three stars
        long long got = score_ - levelStart_;
        float frac = clamp01((float)got / target_);
        Rectangle bar{p.x + 40, p.y + 268, p.width - 80, 26};
        ui::text("TARGET  " + fmtNum(target_), p.x + 40, p.y + 254, 15, col::muted, ui::Left, true);
        DrawRectangleRounded(shrink(bar, -3), 1, 12, col::outline);
        DrawRectangleRounded(bar, 1, 12, col::bgDeep);
        if (frac > 0.01f) fx::gradRoundRect({bar.x, bar.y, bar.width * frac, bar.height}, 1, rgba(0xffd84aff), col::orange);
        DrawRectangleRounded({bar.x + 6, bar.y + 4, std::max(0.0f, bar.width * frac - 12), 6}, 1, 6, alpha(WHITE, 0.35f));
        for (int i = 0; i < 3; i++) {
            float sx = bar.x + bar.width * (0.5f + i * 0.25f);
            bool lit = frac >= 0.5f + i * 0.25f - 0.001f;
            DrawPoly({sx, bar.y + bar.height / 2}, 5, 15, -90 + 36, col::outline);
            DrawPoly({sx, bar.y + bar.height / 2}, 5, 12, -90 + 36, lit ? col::gold : rgba(0x4a3a6aff));
        }
        // Pieter cheering along
        float bounce = cheer_.empty() ? 0 : std::abs(std::sin(bannerT_ * 9)) * clamp01(1.5f - bannerT_) * 18;
        Vector2 pc{p.x + 120, p.y + 430 - bounce};
        fx::beginAdd();
        fx::glow(pc, 120, alpha(col::pink, 0.25f));
        fx::endAdd();
        art::pieter(pc, 70, P().hat, std::sin(t_ * 1.6f) * 5);
        // speech bubble
        std::string say = state_ == St::Over ? "No... out of moves." : bannerT_ < 1.8f && !cheer_.empty() ? cheer_ : combo_ > 0 ? "Yes! Yes!" : idleT_ > 6 ? "Psst... look over there!" : "Let's match!";
        Rectangle bub{p.x + 210, p.y + 360, 290, 84};
        DrawRectangleRounded(shrink(bub, -3), 0.4f, 10, col::outline);
        DrawRectangleRounded(bub, 0.4f, 10, rgba(0xfff6fbff));
        DrawTriangle({bub.x, bub.y + 50}, {bub.x - 22, bub.y + 66}, {bub.x + 8, bub.y + 62}, rgba(0xfff6fbff));
        ui::text(say, bub.x + bub.width / 2, bub.y + bub.height / 2, 20, rgba(0x3a1a3aff), ui::Center, true);
        ui::text("drag or click two tiles to swap", p.x + p.width / 2, p.y + 538, 15, col::muted, ui::Center, true);
        ui::text("4 = striped   L/T = bomb   5 = rainbow Pieter", p.x + p.width / 2, p.y + 562, 14, alpha(col::muted, 0.8f), ui::Center, true);
    }

    void drawBackground() {
        // dusk sky over the bollenvelden
        DrawRectangleGradientV(0, 0, VW, 400, rgba(0x2a1450ff), rgba(0xd86a8aff));
        fx::beginAdd();
        fx::glow({VW * 0.33f, 392}, 420, alpha(rgba(0xffb070ff), 0.45f));
        fx::endAdd();
        DrawCircleV({VW * 0.33f, 396}, 46, rgba(0xffd8a0ff));
        // tulip rows running to the horizon
        const Color rows[] = {rgba(0xe8304aff), rgba(0xffc840ff), rgba(0xff6fb5ff), rgba(0x9a5ae0ff), rgba(0xff7a2aff)};
        Vector2 vp{VW * 0.33f, 386};
        DrawRectangle(0, 392, VW, VH - 392, rgba(0x2a5a2aff));
        for (int i = -14; i < 22; i++) {
            float x0 = vp.x + i * 110.0f, x1 = vp.x + (i + 0.62f) * 110.0f;
            Vector2 a{vp.x + (x0 - vp.x) * 0.02f, 394}, b{vp.x + (x1 - vp.x) * 0.02f, 394};
            Vector2 c{x1 + (x1 - vp.x) * 0.4f, (float)VH}, d{x0 + (x0 - vp.x) * 0.4f, (float)VH};
            Color k = rows[(i + 14) % 5];
            DrawTriangle(a, d, c, k);
            DrawTriangle(a, c, b, k);
            DrawTriangle(a, c, d, k);
            DrawTriangle(a, b, c, k);
        }
        fx::tiled(E().tex("tex/tulips"), {0, 394, (float)VW, (float)VH - 394}, 1.0f, {0, 0}, alpha(WHITE, 0.25f));
        DrawRectangleGradientV(0, 392, VW, 120, rgba(0xc06080ff), alpha(rgba(0xc06080ff), 0));
        DrawRectangle(0, 0, VW, VH, alpha(rgba(0x1a0a2eff), 0.45f));
        // windmill silhouettes on the horizon
        for (int i = 0; i < 3; i++) {
            Vector2 b{120.0f + i * 520, 384};
            Color c = rgba(0x3a1a4aff);
            DrawTriangle({b.x - 18, b.y}, {b.x + 18, b.y}, {b.x + 9, b.y - 80}, c);
            DrawTriangle({b.x - 18, b.y}, {b.x + 9, b.y - 80}, {b.x - 9, b.y - 80}, c);
            for (int k = 0; k < 4; k++) DrawLineEx({b.x, b.y - 78}, Vector2{b.x, b.y - 78} + fromAngle(t_ * 0.6f + i + k * PI / 2, 58), 6, c);
        }
        fx::beginAdd();
        fx::glow({VW * 0.72f, 120}, 300, alpha(col::pink, 0.15f));
        fx::endAdd();
        bg_.draw();
    }

    void draw() override {
        drawBackground();
        drawBoard();
        drawPanel();
        fx_.draw();
        // combo banner over the board
        if (!cheer_.empty() && bannerT_ < 1.4f && state_ != St::LevelDone && state_ != St::Over) {
            float k = easeOutBack(clamp01(bannerT_ / 0.25f));
            float a = clamp01((1.4f - bannerT_) / 0.3f);
            float size = 64 * k;
            ui::textOutline(cheer_, BX + N * C / 2, BY + N * C / 2 - 20 - bannerT_ * 30, size, alpha(col::gold, a), alpha(col::outline, a), 5);
        }
        // level intro
        if (levelBannerT_ < 1.6f && state_ != St::Over) {
            float a = clamp01((1.6f - levelBannerT_) / 0.4f) * clamp01(levelBannerT_ / 0.2f);
            DrawRectangle((int)BX, (int)(BY + N * C / 2 - 50), (int)(N * C), 100, alpha(col::bgDeep, 0.7f * a));
            ui::textShadow("LEVEL " + std::to_string(level_), BX + N * C / 2, BY + N * C / 2 - 12, 48, alpha(WHITE, a), ui::Center, true, 3);
            ui::text("score " + fmtNum(target_) + " in " + std::to_string(moves_) + " moves", BX + N * C / 2, BY + N * C / 2 + 28, 18, alpha(col::gold, a), ui::Center, true);
        }
        if (state_ == St::LevelDone) drawLevelDone();
        if (state_ == St::Over) drawOver();
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawResultPanel(const std::string& title, Color tc, const std::string& sub) {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 300, 130, 600, 420};
        float k = easeOutBack(clamp01(stateT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::pink, 0.6f));
        ui::textOutline(title, VW / 2.0f, p.y + 64, 64, tc, col::outline, 5);
        ui::text(sub, VW / 2.0f, p.y + 116, 20, col::muted, ui::Center, true);
        ui::textShadow(fmtNum(score_), VW / 2.0f, p.y + 180, 60, WHITE, ui::Center, true, 4);
        if (newBest_ && std::fmod(t_, 0.8f) < 0.55f) ui::text("NEW HIGH SCORE!", VW / 2.0f, p.y + 226, 22, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 100, p.y + 270}, 15);
        long long shown = state_ == St::LevelDone ? levelCoins_ : earned_;
        ui::text("+" + fmtNum(shown) + " Pietercoins", VW / 2.0f - 76, p.y + 270, 24, col::gold, ui::Left, true);
    }

    void drawLevelDone() {
        drawResultPanel("LEVEL " + std::to_string(level_) + "!", rgba(0xffe0e8ff), "Pieter is proud of you. Keep it up.");
        Rectangle p{VW / 2.0f - 300, 130, 600, 420};
        if (stateT_ > 0.5f) {
            if (ui::button({p.x + 40, p.y + 330, 250, 64}, "NEXT LEVEL", rgba(0xc2418aff), 24) || IsKeyPressed(KEY_SPACE) || (autoPlay_ && stateT_ > 1)) {
                level_++;
                startLevel();
                E().sfx.play(Sfx::Whoosh, 0.6f);
            }
            if (ui::button({p.x + 310, p.y + 330, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void drawOver() {
        drawResultPanel("OUT OF MOVES", rgba(0xffb0c0ff), "You reached level " + std::to_string(level_) + ".");
        Rectangle p{VW / 2.0f - 300, 130, 600, 420};
        if (stateT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 330, 250, 64}, "PLAY AGAIN", rgba(0xc2418aff), 24) || IsKeyPressed(KEY_SPACE)) newGame();
            if (ui::button({p.x + 310, p.y + 330, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

private:
    std::array<std::array<Tile, N>, N> g_{};
    std::array<std::array<bool, N>, N> mask_{};
    std::vector<Cell> queue_;
    std::vector<Creation> creations_;
    std::vector<Beam> beams_;
    St state_ = St::Idle;
    Cell sel_{-1, -1}, swapA_{-1, -1}, swapB_{-1, -1}, hintA_{-1, -1}, hintB_{-1, -1};
    Vector2 dragFrom_{};
    bool dragging_ = false, autoPlay_ = false, newBest_ = false;
    float t_ = 0, stateT_ = 0, idleT_ = 0, bannerT_ = 9, levelBannerT_ = 9, shuffleT_ = 0, bonusT_ = 0, bonusAcc_ = 0, petalT_ = 0, lastTick_ = 0;
    int level_ = 1, moves_ = 0, combo_ = 0, bonusCount_ = 0;
    long long score_ = 0, shownScore_ = 0, levelStart_ = 0, target_ = 0, earned_ = 0, levelCoins_ = 0;
    std::string cheer_;
    Particles fx_, bg_;
    fx::Shocks shocks_;
};

}  // namespace

std::unique_ptr<Scene> makeTulpen() { return std::make_unique<TulpenScene>(); }

}  // namespace pg
