// PANNENKOEKENHUIS: Pieter runs a pancake house on the canal for a week.
// Pour batter into the pans, bake the bacon, cheese or apple in, flip at the
// right moment, slide the pancake onto a plate, add syrup or powdered sugar
// and get it to the right table before the customer gives up. Later days add
// a coffee machine, more tables, more pans and some very particular regulars.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace pg {
namespace {

// toppings baked into the pancake / added on the plate
enum : int { Spek = 1, Kaas = 2, Appel = 4 };
enum : int { Stroop = 1, Suiker = 2 };

struct DayDef {
    const char* name;
    const char* news;
    int tables, customers, pans;
    float interval, patience;
    int target, inPanMask, combos;
    bool coffee, specials;
};
const DayDef kDays[] = {
    {"Monday", "Opening day! Plain pancakes with syrup or sugar.", 2, 6, 2, 13, 50, 40, 0, 0, false, false},
    {"Tuesday", "Bacon pancakes are on the menu: bake the spek in before you flip.", 3, 8, 2, 11, 46, 60, Spek, 0, false, false},
    {"Wednesday", "Cheese and apple too. A third pan, and the regulars are coming.", 3, 10, 3, 10, 44, 90, Spek | Kaas | Appel, 0, false, true},
    {"Thursday", "The coffee machine is fixed. Some customers want a koffie with it.", 4, 12, 3, 9, 42, 120, Spek | Kaas | Appel, 0, true, true},
    {"Friday", "Bacon AND cheese, if you please. It's getting busy.", 4, 14, 3, 8, 40, 150, Spek | Kaas | Appel, 1, true, true},
    {"Saturday", "Weekend rush. A fourth pan, and less patience out there.", 4, 16, 4, 7, 36, 190, Spek | Kaas | Appel, 1, true, true},
    {"Sunday", "The food critic is in town. Everyone is coming.", 4, 18, 4, 6.5f, 34, 230, Spek | Kaas | Appel, 2, true, true},
};
constexpr int kDayCount = sizeof(kDays) / sizeof(kDays[0]);

// pan timings (seconds)
constexpr float kFlipAt = 3.2f, kSmoke1 = 6.5f, kBurn1 = 8.5f;
constexpr float kDoneAt = 2.8f, kSmoke2 = 5.5f, kBurn2 = 7.5f;

struct Pan {
    int state = 0;  // 0 empty, 1 first side, 2 second side, 3 burnt
    float t = 0, side1 = 0, flip = 0;
    int inPan = 0;
};
struct Plate { bool has = false; int inPan = 0, top = 0; float side = 0; };
struct Order { int inPan = 0, top = 0; bool coffee = false; };
struct Customer {
    int table = 0, who = 0;  // 0..3 tourists, 4 Davi, 5 Kira, 6 the Horse, 7 the critic
    Order o;
    float patience = 1, maxPatience = 1, t = 0, x = 0, shake = 0;
    int state = 0;  // 0 walking in, 1 waiting, 2 eating, 3 leaving happy, 4 leaving angry
    bool gotPancake = false, gotCoffee = false;
};

enum class Tool { None, Batter, Spek, Kaas, Appel, Stroop, Suiker, Plate, Coffee };
enum class Phase { Intro, Shift, Results, Week };

const float kTableX[4] = {340, 570, 800, 1030};
constexpr float kTableY = 252;
const Vector2 kPanPos[4] = {{150, 478}, {320, 478}, {490, 478}, {660, 478}};
const Vector2 kPlatePos[3] = {{835, 480}, {960, 480}, {1085, 480}};
const Vector2 kBowl{90, 640}, kTrash{1215, 640}, kCoffee{1215, 456};
const Vector2 kJar[5] = {{230, 648}, {340, 648}, {450, 648}, {590, 648}, {700, 648}};  // spek kaas appel stroop suiker

int bits(int v) { return (v & 1) + ((v >> 1) & 1) + ((v >> 2) & 1); }

class PannenScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Shift) return {"Pannenkoekenhuis", TextFormat("%s: EUR %d / %d", kDays[day_].name, earned_, kDays[day_].target)};
        return {"Pannenkoekenhuis", "Best shift: EUR " + std::to_string(P().stats.pannenBest)};
    }

    void enter() override {
        day_ = std::min(P().stats.pannenDay, kDayCount - 1);
        if (const char* d = std::getenv("PG_PANNEN")) {  // dev hook: start this day's shift right away
            day_ = std::clamp(std::atoi(d) - 1, 0, kDayCount - 1);
            startShift();
        }
    }
    void exit() override { E().sfx.stop(Sfx::Sizzle); }

    // ------------------------------------------------------------ shift
    void startShift() {
        const DayDef& d = kDays[day_];
        for (auto& p : pans_) p = Pan{};
        for (auto& p : plates_) p = Plate{};
        customers_.clear();
        fx_.clear();
        tool_ = Tool::None;
        earned_ = served_ = walkouts_ = spawned_ = 0;
        coffeeState_ = 0;
        coffeeT_ = 0;
        spawnT_ = 1.5f;
        shiftT_ = 0;
        streak_ = 0;
        lastServe_ = -10;
        phase_ = Phase::Shift;
        phaseT_ = 0;
        paused_ = false;
        (void)d;
    }

    Order makeOrder(int who) {
        const DayDef& d = kDays[day_];
        Order o;
        std::vector<int> opts;
        for (int b : {Spek, Kaas, Appel})
            if (d.inPanMask & b) opts.push_back(b);
        if (!opts.empty() && chance(0.65f)) o.inPan = opts[rndi(0, (int)opts.size() - 1)];
        if (d.combos > 0 && o.inPan && o.inPan != Appel && chance(0.3f * d.combos)) o.inPan = Spek | Kaas;
        int r = rndi(0, 9);
        o.top = r < 4 ? Stroop : r < 7 ? Suiker : r < 8 ? (Stroop | Suiker) : 0;
        if (o.inPan & (Spek | Kaas) && chance(0.5f)) o.top &= Stroop;  // savoury: syrup at most
        o.coffee = d.coffee && chance(0.35f);
        if (who == 4) { o.inPan = Spek; o.top = Stroop | Suiker; }        // Davi: everything sweet on bacon
        if (who == 5) { o.inPan = 0; o.top = Suiker; o.coffee = d.coffee; }  // Kira: classic, with coffee
        if (who == 6) { o.inPan = Appel; o.top = Suiker; o.coffee = false; }  // the Horse: apples, obviously
        return o;
    }

    void spawnCustomer() {
        const DayDef& d = kDays[day_];
        std::vector<int> free;
        for (int t = 0; t < d.tables; t++) {
            bool used = false;
            for (auto& c : customers_)
                if (c.table == t) used = true;
            if (!used) free.push_back(t);
        }
        if (free.empty()) return;
        Customer c;
        c.table = free[rndi(0, (int)free.size() - 1)];
        c.who = rndi(0, 3);
        if (d.specials && chance(0.28f)) c.who = rndi(4, 6);
        if (day_ == kDayCount - 1 && spawned_ == d.customers / 2) c.who = 7;  // the critic
        c.o = makeOrder(c.who);
        float pat = d.patience * (1 + 0.12f * bits(c.o.inPan) + (c.o.coffee ? 0.15f : 0));
        if (c.who == 4) pat *= 0.7f;
        if (c.who == 6) pat *= 1.3f;
        if (c.who == 7) pat *= 0.8f;
        c.patience = c.maxPatience = pat;
        c.x = VW + 80;
        customers_.push_back(c);
        spawned_++;
        E().sfx.play(Sfx::Bell, 0.35f, 1.6f);
        if (c.who == 6) E().sfx.play(Sfx::Neigh, 0.5f);
    }

    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        fx_.update(dt);
        steam_.update(dt);
        if (phase_ == Phase::Intro) {
            E().sfx.stop(Sfx::Sizzle);
            if (E().backPressed()) E().go(makeMenu);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) startShift();
            return;
        }
        if (phase_ != Phase::Shift) {
            if (E().backPressed()) phase_ = Phase::Intro;
            return;
        }
        if (E().backPressed()) paused_ = !paused_;
        if (paused_) return;
        if (E().rightPressed()) tool_ = Tool::None;
        shiftT_ += dt;
        const DayDef& d = kDays[day_];

        // customers come in
        if (spawned_ < d.customers) {
            spawnT_ -= dt;
            if (spawnT_ <= 0) {
                spawnCustomer();
                spawnT_ = d.interval * rnd(0.7f, 1.25f);
                if (spawned_ == 1) spawnT_ *= 0.6f;
            }
        }
        for (auto& c : customers_) {
            c.t += dt;
            c.shake = std::max(0.0f, c.shake - dt * 3);
            float tx = kTableX[c.table];
            switch (c.state) {
                case 0:
                    c.x = approach(c.x, tx, 520 * dt);
                    if (c.x == tx) { c.state = 1; c.t = 0; }
                    break;
                case 1:
                    c.patience -= dt;
                    if (c.patience <= 0) {
                        c.state = 4;
                        c.t = 0;
                        walkouts_++;
                        streak_ = 0;
                        E().sfx.play(Sfx::Error, 0.5f, 0.8f);
                        fx_.floatText(c.who == 4 ? "TOO SLOW, NOOB" : c.who == 6 ? "NEIGH." : "Hmph!", {tx, 90}, col::red, 24);
                    }
                    break;
                case 2:
                    if (c.t > 2.5f) { c.state = 3; c.t = 0; }
                    break;
                default:
                    c.x += 420 * dt;
                    break;
            }
        }
        std::erase_if(customers_, [](const Customer& c) { return c.state >= 3 && c.x > VW + 100; });

        // pans
        int cooking = 0;
        for (int i = 0; i < d.pans; i++) {
            Pan& p = pans_[i];
            p.flip = std::max(0.0f, p.flip - dt * 2.5f);
            if (p.state == 1 || p.state == 2) {
                cooking++;
                p.t += dt;
                bool smoke = (p.state == 1 && p.t > kSmoke1) || (p.state == 2 && p.t > kSmoke2);
                if ((p.state == 1 && p.t > kBurn1) || (p.state == 2 && p.t > kBurn2)) {
                    p.state = 3;
                    E().sfx.play(Sfx::Error, 0.4f, 0.6f);
                    fx_.floatText("BURNT!", kPanPos[i] + Vector2{0, -60}, col::red, 22);
                }
                if (chance(dt * (smoke ? 26 : 5))) {
                    Particle q;
                    q.pos = kPanPos[i] + Vector2{rnd(-40, 40), rnd(-14, 8)};
                    q.vel = {rnd(-10, 10), rnd(-90, -50)};
                    q.life = q.maxLife = rnd(0.8f, 1.4f);
                    q.size = rnd(6, 10);
                    q.endSize = 26;
                    q.color = smoke ? alpha(rgba(0x3a3a3aff), 0.55f) : alpha(WHITE, 0.3f);
                    steam_.add(q);
                }
            }
            if (p.state == 3 && chance(dt * 30)) {
                Particle q;
                q.pos = kPanPos[i] + Vector2{rnd(-30, 30), -6};
                q.vel = {rnd(-10, 10), rnd(-110, -70)};
                q.life = q.maxLife = 1.4f;
                q.size = 10;
                q.endSize = 34;
                q.color = alpha(rgba(0x2a2a2aff), 0.6f);
                steam_.add(q);
            }
        }
        E().sfx.loop(Sfx::Sizzle, std::min(0.6f, cooking * 0.2f));
        if (coffeeState_ == 1) {
            coffeeT_ += dt;
            if (coffeeT_ > 2.6f) {
                coffeeState_ = 2;
                E().sfx.play(Sfx::Tick, 0.5f, 1.5f);
            }
        }
        if (bot_) botThink(dt);
        else if (E().mousePressed()) handleClick(E().mouse());

        bool done = spawned_ >= d.customers && customers_.empty();
        if (done && shiftT_ > 3) endShift();
    }

    // ------------------------------------------------------------ clicks
    bool near(Vector2 m, Vector2 c, float rx, float ry) const {
        float dx = (m.x - c.x) / rx, dy = (m.y - c.y) / ry;
        return dx * dx + dy * dy < 1;
    }

    void pick(Tool t) {
        tool_ = tool_ == t ? Tool::None : t;
        E().sfx.play(Sfx::Click, 0.5f);
    }

    void handleClick(Vector2 m) {
        const DayDef& d = kDays[day_];
        // stations
        if (near(m, kBowl, 70, 50)) return pick(Tool::Batter);
        const Tool jt[5] = {Tool::Spek, Tool::Kaas, Tool::Appel, Tool::Stroop, Tool::Suiker};
        for (int j = 0; j < 5; j++) {
            if (j < 3 && !(d.inPanMask & (1 << j))) continue;
            if (near(m, kJar[j], 46, 52)) return pick(jt[j]);
        }
        if (near(m, kTrash, 50, 60)) {
            if (tool_ == Tool::Plate) {
                plates_[heldPlate_] = Plate{};
                E().sfx.play(Sfx::Thud, 0.5f);
            }
            if (tool_ == Tool::Coffee) coffeeState_ = 0;
            tool_ = Tool::None;
            return;
        }
        if (d.coffee && near(m, kCoffee, 60, 80)) {
            if (coffeeState_ == 0 && tool_ == Tool::None) {
                coffeeState_ = 1;
                coffeeT_ = 0;
                E().sfx.play(Sfx::Pour, 0.4f, 0.7f);
                E().sfx.play(Sfx::Hum, 0.3f, 1.5f);
            } else if (coffeeState_ == 2) {
                pick(Tool::Coffee);
            }
            return;
        }
        // pans
        for (int i = 0; i < d.pans; i++) {
            if (!near(m, kPanPos[i], 76, 50)) continue;
            Pan& p = pans_[i];
            if (tool_ == Tool::Batter) {
                if (p.state == 0) {
                    p = Pan{};
                    p.state = 1;
                    E().sfx.play(Sfx::Pour, 0.6f);
                    tool_ = Tool::None;
                } else {
                    nope(kPanPos[i], "pan's busy");
                }
                return;
            }
            if (tool_ == Tool::Spek || tool_ == Tool::Kaas || tool_ == Tool::Appel) {
                int b = tool_ == Tool::Spek ? Spek : tool_ == Tool::Kaas ? Kaas : Appel;
                if (p.state == 1 && !(p.inPan & b)) {
                    p.inPan |= b;
                    E().sfx.play(Sfx::Pour, 0.3f, 1.6f);
                    fx_.burst(kPanPos[i], 6, b == Spek ? col::red : b == Kaas ? col::gold : col::green, 80, 4, 0.3f, PShape::Square);
                    tool_ = Tool::None;
                } else {
                    nope(kPanPos[i], p.state == 1 ? "already in there" : "bake it in before you flip");
                }
                return;
            }
            if (tool_ != Tool::None) {
                tool_ = Tool::None;
                return;
            }
            if (p.state == 1) {
                if (p.t >= kFlipAt) {
                    p.side1 = p.t;
                    p.state = 2;
                    p.t = 0;
                    p.flip = 1;
                    E().sfx.play(Sfx::Whoosh, 0.5f, 1.4f);
                    E().sfx.play(Sfx::Pour, 0.3f, 1.3f);
                    if (p.side1 < kSmoke1) fx_.floatText("nice flip", kPanPos[i] + Vector2{0, -56}, col::green, 18);
                } else {
                    nope(kPanPos[i], "not yet!");
                }
            } else if (p.state == 2) {
                if (p.t < kDoneAt) {
                    nope(kPanPos[i], "still raw");
                    return;
                }
                int slot = -1;
                for (int s = 0; s < 3; s++)
                    if (!plates_[s].has) { slot = s; break; }
                if (slot < 0) {
                    nope(kPanPos[i], "no clean plates");
                    return;
                }
                plates_[slot] = {true, p.inPan, 0, p.side1};
                p = Pan{};
                E().sfx.play(Sfx::Swap, 0.5f);
            } else if (p.state == 3) {
                p = Pan{};
                E().sfx.play(Sfx::Thud, 0.4f);
                fx_.floatText("into the bin", kPanPos[i] + Vector2{0, -56}, col::muted, 18);
            }
            return;
        }
        // plates
        for (int s = 0; s < 3; s++) {
            if (!near(m, kPlatePos[s], 66, 40)) continue;
            Plate& pl = plates_[s];
            if (tool_ == Tool::Stroop || tool_ == Tool::Suiker) {
                int b = tool_ == Tool::Stroop ? Stroop : Suiker;
                if (pl.has && !(pl.top & b)) {
                    pl.top |= b;
                    E().sfx.play(b == Stroop ? Sfx::Gulp : Sfx::Shutter, 0.4f, b == Stroop ? 0.8f : 1.6f);
                    tool_ = Tool::None;
                } else {
                    nope(kPlatePos[s], pl.has ? "already on" : "no pancake here");
                }
                return;
            }
            if (tool_ == Tool::Spek || tool_ == Tool::Kaas || tool_ == Tool::Appel) {
                nope(kPlatePos[s], "that goes IN the pancake");
                return;
            }
            if (pl.has && tool_ == Tool::None) {
                tool_ = Tool::Plate;
                heldPlate_ = s;
                E().sfx.play(Sfx::Click, 0.5f);
            } else {
                tool_ = Tool::None;
            }
            return;
        }
        // tables
        for (auto& c : customers_) {
            if (c.state != 1 || std::abs(m.x - kTableX[c.table]) > 100 || m.y > 300) continue;
            if (tool_ == Tool::Plate) serve(c);
            else if (tool_ == Tool::Coffee) serveCoffee(c);
            return;
        }
    }

    // Dev autopilot (PG_PANNEN_BOT=1): plays a shift by clicking the same spots a
    // player would, one click every 0.3 s, and prints the result.
    void botThink(float dt) {
        botT_ -= dt;
        if (botT_ > 0) return;
        botT_ = 0.3f;
        if (!botQ_.empty()) {
            handleClick(botQ_.front());
            botQ_.erase(botQ_.begin());
            return;
        }
        const DayDef& d = kDays[day_];
        auto click = [&](std::initializer_list<Vector2> ps) { botQ_.assign(ps); };
        tool_ = Tool::None;
        for (int i = 0; i < d.pans; i++) {
            Pan& p = pans_[i];
            bool freePlate = !plates_[0].has || !plates_[1].has || !plates_[2].has;
            if (p.state == 3 || (p.state == 1 && p.t >= kFlipAt) || (p.state == 2 && p.t >= kDoneAt && freePlate)) return click({kPanPos[i]});
        }
        // waiting customers still needing a pancake, oldest first
        std::vector<Customer*> need;
        for (auto& c : customers_)
            if (c.state == 1 && !c.gotPancake) need.push_back(&c);
        std::sort(need.begin(), need.end(), [](auto* a, auto* b) { return a->patience < b->patience; });
        bool used[3] = {};
        for (auto* c : need)
            for (int s = 0; s < 3; s++) {
                Plate& pl = plates_[s];
                if (!pl.has || used[s] || pl.inPan != c->o.inPan || (pl.top & ~c->o.top)) continue;
                used[s] = true;
                int miss = c->o.top & ~pl.top;
                if (miss & Stroop) return click({kJar[3], kPlatePos[s]});
                if (miss & Suiker) return click({kJar[4], kPlatePos[s]});
                return click({kPlatePos[s], {kTableX[c->table], 200}});
            }
        for (auto& c : customers_)
            if (c.state == 1 && c.o.coffee && !c.gotCoffee && d.coffee) {
                if (coffeeState_ == 0) return click({kCoffee});
                if (coffeeState_ == 2) return click({kCoffee, {kTableX[c.table], 200}});
                break;
            }
        // start a pancake nobody is making yet
        std::vector<int> inWork;
        for (int i = 0; i < d.pans; i++)
            if (pans_[i].state == 1 || pans_[i].state == 2) inWork.push_back(pans_[i].inPan);
        for (int s = 0; s < 3; s++)
            if (plates_[s].has && !used[s]) inWork.push_back(plates_[s].inPan);
        for (auto* c : need) {
            auto it = std::find(inWork.begin(), inWork.end(), c->o.inPan);
            if (it != inWork.end()) { inWork.erase(it); continue; }
            for (int i = 0; i < d.pans; i++)
                if (pans_[i].state == 0) {
                    std::vector<Vector2> q = {kBowl, kPanPos[i]};
                    for (int b = 0; b < 3; b++)
                        if (c->o.inPan & (1 << b)) q.push_back(kJar[b]), q.push_back(kPanPos[i]);
                    botQ_ = q;
                    return;
                }
            break;
        }
    }

    void nope(Vector2 at, const char* why) {
        E().sfx.play(Sfx::Error, 0.35f, 1.3f);
        fx_.floatText(why, at + Vector2{0, -56}, col::red, 18);
    }

    void serve(Customer& c) {
        Plate& pl = plates_[heldPlate_];
        if (c.gotPancake || pl.inPan != c.o.inPan || pl.top != c.o.top) {
            c.shake = 1;
            c.patience -= c.maxPatience * 0.12f;
            E().sfx.play(Sfx::Error, 0.5f);
            fx_.floatText(c.gotPancake ? "I already have one!" : "That's not what I ordered!", {kTableX[c.table], 96}, col::red, 20);
            tool_ = Tool::None;
            return;
        }
        c.gotPancake = true;
        pl = Plate{};
        tool_ = Tool::None;
        E().sfx.play(Sfx::Drop, 0.5f, 1.2f);
        tryFinish(c);
    }

    void serveCoffee(Customer& c) {
        if (!c.o.coffee || c.gotCoffee) {
            c.shake = 1;
            E().sfx.play(Sfx::Error, 0.5f);
            fx_.floatText("No coffee for me, thanks", {kTableX[c.table], 96}, col::red, 20);
            return;
        }
        c.gotCoffee = true;
        coffeeState_ = 0;
        tool_ = Tool::None;
        E().sfx.play(Sfx::Drop, 0.4f, 1.6f);
        tryFinish(c);
    }

    void tryFinish(Customer& c) {
        if (!c.gotPancake || (c.o.coffee && !c.gotCoffee)) return;
        c.state = 2;
        c.t = 0;
        served_++;
        float k = c.patience / c.maxPatience;
        int price = 6 + bits(c.o.inPan) * 2 + bits(c.o.top) + (c.o.coffee ? 3 : 0);
        int tip = (int)std::round(k * 4);
        if (c.who == 4) tip = 0;
        if (c.who == 5) tip *= 2;
        if (c.who == 7) tip = k > 0.5f ? 15 : 0;
        streak_ = shiftT_ - lastServe_ < 6 ? streak_ + 1 : 1;
        lastServe_ = shiftT_;
        int bonus = streak_ >= 3 ? streak_ - 2 : 0;
        int total = price + tip + bonus;
        earned_ += total;
        E().sfx.play(Sfx::Register, 0.6f);
        E().sfx.play(Sfx::Chew, 0.4f);
        std::string line = TextFormat("+EUR %d", total);
        if (tip > 0) line += TextFormat("  (tip %d)", tip);
        fx_.floatText(line, {kTableX[c.table], 96}, col::gold, 24);
        if (bonus > 0) fx_.floatText(TextFormat("x%d RUSH +%d", streak_, bonus), {kTableX[c.table], 130}, col::cyan, 20);
        const char* thanks[] = {"Lekker!", "Delicious!", "Perfect!", "Dankjewel!"};
        const char* line2 = c.who == 4 ? "ok not bad" : c.who == 5 ? "hihi <3" : c.who == 6 ? "JUHUH!" : c.who == 7 ? "...acceptable." : thanks[rndi(0, 3)];
        fx_.floatText(line2, {kTableX[c.table], 60}, WHITE, 20);
        fx_.burst({kTableX[c.table], kTableY - 10}, 14, col::gold, 160, 5, 0.5f, PShape::Star);
    }

    void endShift() {
        phase_ = Phase::Results;
        phaseT_ = 0;
        tool_ = Tool::None;
        E().sfx.stop(Sfx::Sizzle);
        passed_ = earned_ >= kDays[day_].target;
        if (bot_) {
            std::printf("pannen bot: day %d earned EUR %d / %d, served %d, walkouts %d\n", day_ + 1, earned_, kDays[day_].target, served_, walkouts_);
            if (day_ + 1 < kDayCount) {
                day_++;
                startShift();
            } else {
                E().quit();
            }
            return;
        }
        auto& st = P().stats;
        coins_ = earned_ * 2 + (passed_ ? 30 : 0);
        P().earn(coins_);
        st.pannenBest = std::max(st.pannenBest, earned_);
        if (passed_) st.pannenDay = std::max(st.pannenDay, day_ + 1);
        P().save();
        E().sfx.play(passed_ ? Sfx::Win : Sfx::Lose, 0.7f);
        if (passed_) fx_.confetti({VW / 2.0f, 200}, 100);
        if (passed_ && day_ == kDayCount - 1) phase_ = Phase::Week;
    }

    // ------------------------------------------------------------ draw helpers
    static Color cookColor(float k) {  // 0 raw batter .. 1 golden .. 2 burnt
        if (k < 1) return mix(rgba(0xf4e2a8ff), rgba(0xe0a040ff), k);
        return mix(rgba(0xe0a040ff), rgba(0x2a1a10ff), k - 1);
    }

    void drawPancake(Vector2 c, float rx, float ry, Color top, Color edge, int inPan, int plateTop, float bubbles, unsigned seed) {
        DrawEllipse((int)c.x, (int)c.y + 2, rx + 2, ry + 2, alpha(darken(edge, 0.5f), 0.8f));
        DrawEllipse((int)c.x, (int)c.y, rx, ry, edge);
        DrawEllipse((int)c.x, (int)c.y - 1, rx * 0.88f, ry * 0.85f, top);
        // golden mottling
        for (unsigned i = 0; i < 9; i++) {
            float a = (seed * 13 + i * 41) % 360 * DEG2RAD, d = ((seed + i * 7) % 10) / 10.0f * 0.7f;
            DrawEllipse((int)(c.x + std::cos(a) * rx * d), (int)(c.y + std::sin(a) * ry * d), rx * 0.14f, ry * 0.14f, alpha(darken(top, 0.12f), 0.6f));
        }
        for (int i = 0; i < (int)(bubbles * 10); i++) {
            float a = (seed * 7 + i * 67) % 360 * DEG2RAD, d = ((seed + i * 3) % 9) / 9.0f * 0.75f;
            Vector2 b{c.x + std::cos(a) * rx * d, c.y + std::sin(a) * ry * d};
            DrawEllipse((int)b.x, (int)b.y, rx * 0.05f, ry * 0.05f, alpha(darken(top, 0.35f), 0.8f));
        }
        if (inPan & Spek)
            for (int i = 0; i < 3; i++) {
                Vector2 p{c.x - rx * 0.45f + i * rx * 0.42f, c.y - ry * 0.25f + (i % 2) * ry * 0.4f};
                Rectangle r{p.x - rx * 0.16f, p.y - ry * 0.12f, rx * 0.34f, ry * 0.22f};
                DrawRectangleRounded(r, 0.5f, 4, rgba(0xc8504aff));
                DrawRectangleRec({r.x + 2, r.y + r.height * 0.4f, r.width - 4, r.height * 0.2f}, rgba(0xf8d8c8ff));
            }
        if (inPan & Kaas)
            for (int i = 0; i < 4; i++) {
                Vector2 p{c.x - rx * 0.35f + (i % 2) * rx * 0.6f, c.y - ry * 0.3f + (i / 2) * ry * 0.55f};
                DrawEllipse((int)p.x, (int)p.y, rx * 0.2f, ry * 0.18f, rgba(0xffd84aff));
                DrawEllipse((int)p.x - 2, (int)p.y - 1, rx * 0.08f, ry * 0.06f, alpha(WHITE, 0.6f));
            }
        if (inPan & Appel)
            for (int i = 0; i < 5; i++) {
                float a = i * 72 * DEG2RAD + 0.4f;
                Vector2 p{c.x + std::cos(a) * rx * 0.45f, c.y + std::sin(a) * ry * 0.45f};
                DrawEllipse((int)p.x, (int)p.y, rx * 0.16f, ry * 0.1f, rgba(0xf4ecb8ff));
                DrawEllipse((int)p.x, (int)p.y - (int)(ry * 0.06f), rx * 0.14f, ry * 0.04f, rgba(0xc83a2aff));
            }
        if (plateTop & Stroop) {
            Vector2 prev{c.x - rx * 0.6f, c.y - ry * 0.2f};
            for (int i = 1; i <= 8; i++) {
                Vector2 q{c.x - rx * 0.6f + i * rx * 0.15f, c.y + (i % 2 ? ry * 0.3f : -ry * 0.3f)};
                DrawLineEx(prev, q, std::max(2.0f, rx * 0.06f), rgba(0x6a3008ff));
                prev = q;
            }
            DrawEllipse((int)c.x + 4, (int)c.y + 2, rx * 0.18f, ry * 0.14f, alpha(rgba(0x8a4a10ff), 0.9f));
        }
        if (plateTop & Suiker)
            for (int i = 0; i < 40; i++) {
                float a = (seed * 11 + i * 97) % 360 * DEG2RAD, d = ((seed * 3 + i * 13) % 17) / 17.0f * 0.8f;
                DrawCircleV({c.x + std::cos(a) * rx * d, c.y + std::sin(a) * ry * d}, std::max(1.2f, rx * 0.03f), alpha(WHITE, 0.9f));
            }
    }

    void icon(int kind, Vector2 c, float s) {
        switch (kind) {
            case 0:  // spek
                DrawRectangleRounded({c.x - 12 * s, c.y - 6 * s, 24 * s, 12 * s}, 0.5f, 4, col::outline);
                DrawRectangleRounded({c.x - 11 * s, c.y - 5 * s, 22 * s, 10 * s}, 0.5f, 4, rgba(0xc8504aff));
                DrawRectangleRec({c.x - 10 * s, c.y - 1 * s, 20 * s, 2.5f * s}, rgba(0xf8d8c8ff));
                break;
            case 1: fx::sprite(E().tex("spr/tile_cheese"), c, 0.2f * s); break;
            case 2:  // apple
                DrawCircleV(c, 10 * s, col::outline);
                DrawCircleV(c, 8.6f * s, rgba(0xd83a2aff));
                DrawCircleV(c + Vector2{-3 * s, -3 * s}, 2.5f * s, alpha(WHITE, 0.5f));
                DrawLineEx(c + Vector2{0, -8 * s}, c + Vector2{2 * s, -13 * s}, 2 * s, rgba(0x5a3a1aff));
                DrawEllipse((int)(c.x + 5 * s), (int)(c.y - 11 * s), 4 * s, 2 * s, col::green);
                break;
            case 3:  // stroop bottle
                DrawRectangleRounded({c.x - 7 * s, c.y - 6 * s, 14 * s, 18 * s}, 0.4f, 4, col::outline);
                DrawRectangleRounded({c.x - 6 * s, c.y - 5 * s, 12 * s, 16 * s}, 0.4f, 4, rgba(0x8a4a10ff));
                DrawRectangleRec({c.x - 3 * s, c.y - 13 * s, 6 * s, 8 * s}, rgba(0x8a4a10ff));
                DrawRectangleRec({c.x - 4 * s, c.y - 15 * s, 8 * s, 3 * s}, col::red);
                DrawRectangleRec({c.x - 5 * s, c.y, 10 * s, 5 * s}, rgba(0xf4ecd8ff));
                break;
            case 4:  // sugar shaker
                DrawRectangleRounded({c.x - 7 * s, c.y - 8 * s, 14 * s, 20 * s}, 0.3f, 4, col::outline);
                DrawRectangleRounded({c.x - 6 * s, c.y - 7 * s, 12 * s, 18 * s}, 0.3f, 4, rgba(0xf8f8ffff));
                DrawCircleSector({c.x, c.y - 7 * s}, 6 * s, 180, 360, 8, rgba(0xc8c8d8ff));
                for (int i = -1; i <= 1; i++) DrawCircleV({c.x + i * 2.5f * s, c.y - 10 * s}, 0.9f * s, col::outline);
                break;
            case 5:  // coffee cup
                DrawRectangleRounded({c.x - 8 * s, c.y - 6 * s, 16 * s, 15 * s}, 0.3f, 4, col::outline);
                DrawRectangleRounded({c.x - 7 * s, c.y - 5 * s, 14 * s, 13 * s}, 0.3f, 4, WHITE);
                DrawEllipse((int)c.x, (int)(c.y - 4 * s), 6 * s, 2 * s, rgba(0x5a3010ff));
                DrawRing({c.x + 8 * s, c.y + 1 * s}, 2.5f * s, 4.5f * s, -90, 90, 8, WHITE);
                break;
        }
    }

    // ------------------------------------------------------------ draw: room
    void drawRoom() {
        // dining room: wall, Delft tiles, windows onto the canal, wooden floor
        DrawRectangleGradientV(0, 0, VW, 200, rgba(0xf4e4c8ff), rgba(0xe8d0a8ff));
        for (int i = 1; i < 4; i++) {  // (the sign hangs where the first window would be)
            float x = 60 + i * 330.0f;
            Rectangle w{x, 22, 110, 120};
            DrawRectangleRec(shrink(w, -6), rgba(0x5a3a24ff));
            DrawRectangleGradientV((int)w.x, (int)w.y, (int)w.width, (int)w.height, rgba(0x8ac8f0ff), rgba(0xd8eef8ff));
            for (int k = 0; k < 3; k++) {  // canal houses across the water
                Texture2D& h = E().tex(TextFormat("spr/house%d", (i * 3 + k) % 6));
                float s = 0.2f;
                DrawTexturePro(h, {0, 0, (float)h.width, (float)h.height}, {w.x + k * 36.0f, w.y + w.height - 26 - h.height * s, h.width * s, h.height * s}, {0, 0}, 0, WHITE);
            }
            DrawRectangleRec({w.x, w.y + w.height - 26, w.width, 26}, rgba(0x3a7a9aff));
            DrawLineEx({w.x + w.width / 2, w.y}, {w.x + w.width / 2, w.y + w.height}, 5, rgba(0x5a3a24ff));
            DrawLineEx({w.x, w.y + 46}, {w.x + w.width, w.y + 46}, 5, rgba(0x5a3a24ff));
            DrawRectangleRec({w.x - 12, w.y + w.height + 4, w.width + 24, 8}, rgba(0x6a4a2aff));
            DrawCircleV({w.x + 20, w.y + w.height - 2}, 10, rgba(0x2a8a3aff));  // a plant on the sill
            DrawRectangleRec({w.x + 12, w.y + w.height - 2, 16, 8}, rgba(0xc85a2aff));
        }
        for (int i = 0; i < 40; i++) {  // the Delft blue tile band
            Rectangle t{i * 32.0f, 160, 32, 32};
            DrawRectangleRec(t, rgba(0xf4f6ffff));
            DrawRectangleLinesEx(t, 1, rgba(0xa8b8d8ff));
            Vector2 c = center(t);
            Color b = rgba(0x2a4aa8ff);
            if (i % 3 == 0) {  // windmill
                DrawLineEx({c.x - 6, c.y - 6}, {c.x + 6, c.y + 6}, 2, b);
                DrawLineEx({c.x + 6, c.y - 6}, {c.x - 6, c.y + 6}, 2, b);
                DrawRectangleRec({c.x - 2, c.y, 4, 10}, b);
            } else if (i % 3 == 1) {  // tulip
                DrawCircleSector(c, 6, 180, 360, 8, b);
                DrawLineEx(c, {c.x, c.y + 10}, 1.5f, b);
            } else {
                DrawPoly(c, 4, 6, 45, b);
            }
            for (int k = 0; k < 4; k++) DrawCircleSector({t.x + (k % 2) * 32.0f, t.y + (k / 2) * 32.0f}, 5, 0, 360, 8, alpha(b, 0.5f));
        }
        fx::tiled(E().tex("tex/wood"), {0, 192, (float)VW, 118}, 0.6f, {0, 0}, rgba(0xd8b890ff));
        DrawRectangleGradientV(0, 192, VW, 20, alpha(BLACK, 0.25f), alpha(BLACK, 0));
        // hanging lamps
        for (int i = 0; i < 4; i++) {
            float x = kTableX[i];
            DrawLineEx({x, 0}, {x, 28}, 2, rgba(0x2a2a2aff));
            DrawCircleSector({x, 40}, 20, 180, 360, 12, rgba(0x2a5a3aff));
            fx::beginAdd();
            fx::glow({x, 46}, 90, alpha(rgba(0xffd890ff), 0.25f));
            fx::endAdd();
        }
        // sign
        DrawRectangleRounded({20, 52, 190, 70}, 0.2f, 8, rgba(0x2a5a3aff));
        DrawRectangleRoundedLinesEx({20, 52, 190, 70}, 0.2f, 8, 3, col::gold);
        ui::text("PIETER'S", 115, 74, 18, col::gold, ui::Center, true);
        ui::text("PANNENKOEKENHUIS", 115, 100, 14, rgba(0xf4ecd8ff), ui::Center, true);
    }

    void drawCustomer(const Customer& c) {
        float x = c.x + std::sin(c.shake * 30) * c.shake * 6;
        float bob = c.state == 0 || c.state >= 3 ? std::abs(std::sin(c.t * 10)) * 4 : 0;
        float y = kTableY;
        if (c.who < 4) {
            Texture2D& b = E().tex(TextFormat("spr/beach%d", c.who));
            fx::sprite(b, {x, y - 42 - bob}, 0.95f, 0, WHITE, c.state >= 3);
        } else {
            const char* who = c.who == 4 ? "davi" : c.who == 5 ? "kira" : c.who == 6 ? "horse" : "pieter";
            Color body = c.who == 4 ? col::cyan : c.who == 5 ? col::pink : c.who == 6 ? col::orange : rgba(0x2a2a3aff);
            DrawRectangleRounded({x - 34, y - 92 - bob, 68, 100}, 0.5f, 8, col::outline);
            DrawRectangleRounded({x - 32, y - 90 - bob, 64, 96}, 0.5f, 8, body);
            if (c.who == 7) {  // the critic: a monocle and a beret on a stranger
                DrawCircleV({x, y - 128 - bob}, 34, col::outline);
                DrawCircleV({x, y - 128 - bob}, 32, rgba(0xe8c8a0ff));
                DrawCircleV({x - 11, y - 132 - bob}, 3, col::outline);
                ui::circleOutline({x + 11, y - 132 - bob}, 8, rgba(0xc8a040ff), 2);
                DrawCircleV({x + 11, y - 132 - bob}, 2.5f, col::outline);
                DrawLineEx({x - 8, y - 114 - bob}, {x + 8, y - 114 - bob}, 2, col::outline);
                DrawEllipse((int)x - 4, (int)(y - 160 - bob), 34, 10, rgba(0x8a1a2aff));
                DrawCircleV({x + 4, y - 170 - bob}, 4, rgba(0x8a1a2aff));
                ui::text("CRITIC", x, y - 40 - bob, 12, col::gold, ui::Center, true);
            } else {
                art::portrait(who, {x, y - 128 - bob}, 36, col::outline, 0, c.state >= 3);
            }
        }
        if (c.state == 2) {  // eating
            float k = std::fmod(c.t * 3, 1.0f);
            ui::text("nom", x + 40, y - 150 - k * 20, 14, alpha(WHITE, 1 - k), ui::Center, true);
        }
        if (c.state == 4) ui::text("!!", x, y - 190, 26, col::red, ui::Center, true);
    }

    void drawTable(int i) {
        float x = kTableX[i], y = kTableY;
        fx::softShadow({x, y + 40}, 90, 14, 0.4f);
        DrawRectangleRec({x - 8, y, 16, 56}, rgba(0x4a2a14ff));
        DrawEllipse((int)x, (int)y + 6, 96, 26, rgba(0xb82a2aff));
        DrawEllipse((int)x, (int)y, 96, 24, rgba(0xe83a3aff));
        for (int k = -3; k <= 3; k++)  // checkered cloth
            for (int j = -1; j <= 1; j++)
                if ((k + j) % 2 == 0) {
                    float cx = x + k * 26.0f, cy = y + j * 13.0f;
                    if (((cx - x) / 92) * ((cx - x) / 92) + ((cy - y) / 22) * ((cy - y) / 22) < 1) DrawRectangleRec({cx - 9, cy - 4.5f, 18, 9}, alpha(WHITE, 0.85f));
                }
        DrawEllipse((int)x - 50, (int)y - 4, 10, 4, alpha(WHITE, 0.5f));
        // the plate in front of a customer who is eating
        for (auto& c : customers_)
            if (c.table == i && c.state == 2) {
                DrawEllipse((int)x, (int)y - 4, 46, 14, rgba(0xf4f4f8ff));
                drawPancake({x, y - 6}, 36 * (1 - c.t / 3), 10 * (1 - c.t / 3), cookColor(1.1f), cookColor(1.3f), c.o.inPan, c.o.top, 0, (unsigned)i);
            }
    }

    void drawBubble(const Customer& c) {
        if (c.state != 1) return;
        float x = kTableX[c.table];
        int n = 1 + bits(c.o.inPan) + bits(c.o.top) + (c.o.coffee ? 1 : 0);
        float w = 30 + n * 34.0f;
        Rectangle r{x - w / 2, 4, w, 52};
        float k = c.patience / c.maxPatience;
        bool urgent = k < 0.3f && std::fmod(t_, 0.5f) < 0.25f;
        DrawRectangleRounded({r.x, r.y + 3, r.width, r.height}, 0.4f, 8, alpha(BLACK, 0.3f));
        DrawRectangleRounded(r, 0.4f, 8, urgent ? rgba(0xffe0e0ff) : WHITE);
        DrawTriangle({x - 8, r.y + r.height}, {x + 8, r.y + r.height}, {x, r.y + r.height + 10}, urgent ? rgba(0xffe0e0ff) : WHITE);
        float ix = r.x + 30;
        if (!c.gotPancake) {
            drawPancake({ix, r.y + 24}, 16, 9, cookColor(0.95f), cookColor(1.2f), c.o.inPan, c.o.top, 0, 3);
            ix += 34;
            for (int b = 0; b < 3; b++)
                if (c.o.inPan & (1 << b)) icon(b, {ix, r.y + 24}, 0.9f), ix += 34;
            for (int b = 0; b < 2; b++)
                if (c.o.top & (1 << b)) icon(3 + b, {ix, r.y + 24}, 0.9f), ix += 34;
        } else {
            ui::text("ok!", ix, r.y + 26, 16, col::green, ui::Center, true);
            ix += 34;
        }
        if (c.o.coffee) {
            icon(5, {ix, r.y + 24}, 0.9f);
            if (c.gotCoffee) DrawLineEx({ix - 10, r.y + 26}, {ix - 2, r.y + 34}, 3, col::green), DrawLineEx({ix - 2, r.y + 34}, {ix + 12, r.y + 14}, 3, col::green);
        }
        Rectangle bar{r.x + 8, r.y + r.height - 8, r.width - 16, 4};
        DrawRectangleRec(bar, alpha(BLACK, 0.12f));
        DrawRectangleRec({bar.x, bar.y, bar.width * clamp01(k), bar.height}, k > 0.5f ? col::green : k > 0.25f ? col::orange : col::red);
    }

    // ------------------------------------------------------------ draw: kitchen
    bool hot(Vector2 c, float rx, float ry) const { return near(E().mouse(), c, rx, ry) && !paused_ && phase_ == Phase::Shift; }

    void highlight(Vector2 c, float rx, float ry, Color k) {
        fx::beginAdd();
        fx::glow(c, std::max(rx, ry) * 1.3f, alpha(k, 0.35f + 0.1f * std::sin(t_ * 6)));
        fx::endAdd();
    }

    void drawKitchen() {
        const DayDef& d = kDays[day_];
        // back wall of white tiles and the counter top
        DrawRectangle(0, 310, VW, 80, rgba(0xe8eef4ff));
        for (int x = 0; x < VW; x += 40)
            for (int y = 310; y < 390; y += 20) DrawRectangleLinesEx({(float)x + ((y / 20) % 2) * 20.0f, (float)y, 40, 20}, 1, rgba(0xc8d0d8ff));
        DrawRectangleGradientV(0, 310, VW, 16, alpha(BLACK, 0.35f), alpha(BLACK, 0));
        fx::tiled(E().tex("tex/wood"), {0, 390, (float)VW, 330}, 0.7f, {0, 0}, rgba(0xc89a6aff));
        DrawRectangle(0, 386, VW, 6, rgba(0x5a3a24ff));
        // stove
        Rectangle stove{50, 410, 700, 150};
        DrawRectangleRounded(stove, 0.08f, 8, rgba(0x2a2a32ff));
        DrawRectangleRounded(shrink(stove, 6), 0.08f, 8, rgba(0x3a3a46ff));
        for (int i = 0; i < 4; i++) drawPan(i, i < d.pans);
        // stations
        bool canTop = false;
        for (int s = 0; s < 3; s++) canTop |= plates_[s].has;
        // batter bowl
        if (tool_ == Tool::None && hot(kBowl, 70, 50)) highlight(kBowl, 70, 50, WHITE);
        DrawEllipse((int)kBowl.x, (int)kBowl.y + 22, 70, 18, alpha(BLACK, 0.3f));
        DrawCircleSector({kBowl.x, kBowl.y}, 66, 0, 180, 24, col::outline);
        DrawCircleSector({kBowl.x, kBowl.y}, 63, 0, 180, 24, rgba(0x3a6ad8ff));
        DrawEllipse((int)kBowl.x, (int)kBowl.y, 66, 18, col::outline);
        DrawEllipse((int)kBowl.x, (int)kBowl.y, 62, 15, rgba(0xf4e2a8ff));
        DrawEllipse((int)kBowl.x - 14, (int)kBowl.y - 3, 18, 4, alpha(WHITE, 0.5f));
        DrawLineEx({kBowl.x + 20, kBowl.y - 4}, {kBowl.x + 60, kBowl.y - 60}, 6, rgba(0x8a8a9aff));  // ladle
        ui::text("BATTER", kBowl.x, kBowl.y + 58, 12, col::outline, ui::Center, true);
        const char* names[5] = {"SPEK", "KAAS", "APPEL", "STROOP", "SUGAR"};
        const Tool jt[5] = {Tool::Spek, Tool::Kaas, Tool::Appel, Tool::Stroop, Tool::Suiker};
        for (int j = 0; j < 5; j++) {
            bool on = j >= 3 || (d.inPanMask & (1 << j));
            Vector2 c = kJar[j];
            if (on && tool_ == jt[j]) highlight(c, 46, 52, col::gold);
            else if (on && tool_ == Tool::None && hot(c, 46, 52)) highlight(c, 46, 52, WHITE);
            Rectangle jar{c.x - 38, c.y - 40, 76, 76};
            DrawRectangleRounded({jar.x, jar.y + 4, jar.width, jar.height}, 0.3f, 8, alpha(BLACK, 0.3f));
            DrawRectangleRounded(jar, 0.3f, 8, on ? (j >= 3 ? rgba(0xf4ecd8ff) : rgba(0xd8e8f0ff)) : alpha(rgba(0xd8d8d8ff), 0.4f));
            DrawRectangleRoundedLinesEx(jar, 0.3f, 8, 2, col::outline);
            if (on) {
                icon(j, {c.x, c.y - 6}, 1.7f);
                ui::text(names[j], c.x, c.y + 26, 11, col::outline, ui::Center, true);
            } else {
                ui::text("soon", c.x, c.y, 12, col::muted, ui::Center, true);
            }
        }
        ui::text("bake in", (kJar[0].x + kJar[2].x) / 2, kJar[0].y - 52, 11, alpha(col::outline, 0.7f), ui::Center, true);
        ui::text("on the plate", (kJar[3].x + kJar[4].x) / 2, kJar[3].y - 52, 11, alpha(col::outline, 0.7f), ui::Center, true);
        (void)canTop;
        // the pass with plates
        DrawRectangleRounded({765, 420, 390, 120}, 0.15f, 8, rgba(0xd8d0c8ff));
        DrawRectangleRoundedLinesEx({765, 420, 390, 120}, 0.15f, 8, 2, rgba(0x8a8078ff));
        ui::text("PASS", 790, 436, 11, rgba(0x6a6058ff), ui::Left, true);
        for (int s = 0; s < 3; s++) {
            Vector2 c = kPlatePos[s];
            const Plate& pl = plates_[s];
            bool held = tool_ == Tool::Plate && heldPlate_ == s;
            if (pl.has && !held && (tool_ == Tool::Stroop || tool_ == Tool::Suiker || tool_ == Tool::None) && hot(c, 66, 40)) highlight(c, 60, 34, WHITE);
            DrawEllipse((int)c.x, (int)c.y + 4, 62, 26, alpha(BLACK, 0.25f));
            DrawEllipse((int)c.x, (int)c.y, 60, 25, rgba(0xf8f8ffff));
            DrawEllipse((int)c.x, (int)c.y, 46, 18, rgba(0xe8e8f0ff));
            for (int k = 0; k < 16; k++) DrawCircleV({c.x + std::cos(k * PI / 8) * 54, c.y + std::sin(k * PI / 8) * 22}, 1.5f, rgba(0x2a4aa8ff));
            if (pl.has && !held) drawPancake({c.x, c.y - 4}, 44, 17, cookColor(std::min(1.9f, pl.side / kFlipAt * 0.9f)), cookColor(1.3f), pl.inPan, pl.top, 0, (unsigned)s * 5 + 1);
        }
        // trash
        if (tool_ == Tool::Plate || tool_ == Tool::Coffee) highlight(kTrash, 40, 50, col::red);
        DrawRectangleRounded({kTrash.x - 34, kTrash.y - 40, 68, 84}, 0.2f, 6, col::outline);
        DrawRectangleRounded({kTrash.x - 32, kTrash.y - 38, 64, 80}, 0.2f, 6, rgba(0x6a7a8aff));
        DrawRectangleRounded({kTrash.x - 38, kTrash.y - 48, 76, 14}, 0.5f, 6, rgba(0x4a5a6aff));
        for (int k = -1; k <= 1; k++) DrawLineEx({kTrash.x + k * 16.0f, kTrash.y - 28}, {kTrash.x + k * 16.0f, kTrash.y + 32}, 3, alpha(BLACK, 0.2f));
        // coffee machine
        if (d.coffee) {
            Vector2 c = kCoffee;
            if ((coffeeState_ == 0 && tool_ == Tool::None) || coffeeState_ == 2)
                if (hot(c, 60, 80)) highlight(c, 50, 70, WHITE);
            DrawRectangleRounded({c.x - 50, c.y - 72, 100, 130}, 0.15f, 8, col::outline);
            DrawRectangleRounded({c.x - 48, c.y - 70, 96, 126}, 0.15f, 8, rgba(0xc8c8d0ff));
            DrawRectangleRec({c.x - 36, c.y - 10, 72, 52}, rgba(0x2a2a32ff));
            DrawRectangleRec({c.x - 10, c.y - 10, 20, 10}, rgba(0x5a5a62ff));
            DrawCircleV({c.x + 30, c.y - 50}, 7, coffeeState_ == 2 ? col::green : coffeeState_ == 1 ? col::orange : col::red);
            ui::text("KOFFIE", c.x - 8, c.y - 50, 12, col::outline, ui::Center, true);
            if (coffeeState_ == 1) {
                DrawLineEx({c.x, c.y}, {c.x, c.y + 26}, 3, rgba(0x5a3010ff));
                ui::bar({c.x - 36, c.y + 62, 72, 6}, coffeeT_ / 2.6f, col::orange);
            }
            if (coffeeState_ >= 1 && tool_ != Tool::Coffee) icon(5, {c.x, c.y + 30}, 1.3f);
        }
    }

    void drawPan(int i, bool on) {
        Vector2 c = kPanPos[i];
        const Pan& p = pans_[i];
        if (!on) {
            DrawEllipse((int)c.x, (int)c.y, 62, 38, rgba(0x2a2a32ff));
            ui::text("closed", c.x, c.y, 12, col::muted, ui::Center, true);
            return;
        }
        // burner glow under an active pan
        if (p.state == 1 || p.state == 2) {
            fx::beginAdd();
            fx::glow({c.x, c.y + 10}, 110, alpha(rgba(0xff6a2aff), 0.35f + 0.08f * std::sin(t_ * 9 + i)));
            fx::endAdd();
        }
        bool canAct = (tool_ == Tool::Batter && p.state == 0) || ((tool_ == Tool::Spek || tool_ == Tool::Kaas || tool_ == Tool::Appel) && p.state == 1) ||
                      (tool_ == Tool::None && ((p.state == 1 && p.t >= kFlipAt) || (p.state == 2 && p.t >= kDoneAt) || p.state == 3));
        if (canAct && (hot(c, 76, 50) || tool_ != Tool::None)) highlight(c, 70, 44, tool_ == Tool::None ? WHITE : col::gold);
        DrawLineEx({c.x - 60, c.y + 20}, {c.x - 104, c.y + 48}, 12, col::outline);  // handle
        DrawLineEx({c.x - 60, c.y + 20}, {c.x - 102, c.y + 47}, 8, rgba(0x2a1a14ff));
        DrawEllipse((int)c.x, (int)c.y + 6, 74, 46, col::outline);
        DrawEllipse((int)c.x, (int)c.y + 4, 72, 44, rgba(0x3a3a42ff));
        DrawEllipse((int)c.x, (int)c.y, 64, 38, rgba(0x1e1e24ff));
        DrawEllipse((int)c.x - 18, (int)c.y - 14, 26, 8, alpha(WHITE, 0.07f));
        if (p.state == 0) return;
        if (p.state == 3) {
            drawPancake(c, 54, 31, rgba(0x1a1210ff), rgba(0x2a1a10ff), p.inPan, 0, 0, (unsigned)i);
            ui::textOutline("BURNT", c.x, c.y + 60, 16, col::red, col::outline, 2);
            return;
        }
        // flip animation: the pancake jumps and turns over
        float f = p.flip;
        float lift = std::sin(f * PI) * 70, sq = std::abs(std::cos(f * PI));
        Color top, edge;
        float bubbles = 0;
        if (p.state == 1) {
            top = cookColor(std::min(0.25f, p.t / kFlipAt * 0.25f));
            edge = cookColor(std::min(2.0f, p.t / kFlipAt * 0.95f));
            bubbles = clamp01(p.t / kFlipAt);
        } else {
            float k1 = p.side1 / kFlipAt * 0.95f;
            top = cookColor(std::min(2.0f, k1 + (k1 > 1 ? (p.side1 - kFlipAt) * 0.12f : 0)));
            edge = cookColor(std::min(2.0f, 0.4f + p.t / kDoneAt * 0.6f + std::max(0.0f, p.t - kSmoke2) * 0.3f));
        }
        rlPushMatrix();
        rlTranslatef(c.x, c.y - lift, 0);
        rlScalef(1, std::max(0.12f, f > 0 ? sq : 1), 1);
        drawPancake({0, 0}, 54, 31, top, edge, p.inPan, 0, bubbles, (unsigned)i * 3 + 7);
        rlPopMatrix();
        // a doneness ring: when to flip, when to take it off
        float k, warn, end;
        if (p.state == 1) k = p.t, warn = kFlipAt, end = kBurn1;
        else k = p.t, warn = kDoneAt, end = kBurn2;
        float smoke = p.state == 1 ? kSmoke1 : kSmoke2;
        Vector2 rc{c.x + 58, c.y - 36};
        DrawCircleV(rc, 16, alpha(col::outline, 0.8f));
        DrawCircleSector(rc, 13, -90, -90 + 360 * clamp01(k / end), 24, k < warn ? col::cyan : k < smoke ? col::green : col::red);
        DrawCircleV(rc, 7, col::outline);
        if (k >= warn) {
            const char* what = p.state == 1 ? "FLIP!" : "DONE!";
            float pulse = 1 + 0.08f * std::sin(t_ * 10);
            ui::textOutline(what, c.x, c.y + 60, 16 * pulse, k < smoke ? col::green : col::orange, col::outline, 2);
        }
    }

    void drawTool() {
        Vector2 m = E().mouse();
        switch (tool_) {
            case Tool::None: return;
            case Tool::Batter:
                DrawLineEx({m.x + 6, m.y - 6}, {m.x + 40, m.y - 44}, 6, rgba(0x8a8a9aff));
                DrawCircleV(m, 16, rgba(0x8a8a9aff));
                DrawCircleV(m, 13, rgba(0xf4e2a8ff));
                break;
            case Tool::Spek: icon(0, m, 1.6f); break;
            case Tool::Kaas: icon(1, m, 1.6f); break;
            case Tool::Appel: icon(2, m, 1.6f); break;
            case Tool::Stroop: icon(3, m, 1.6f); break;
            case Tool::Suiker: icon(4, m, 1.6f); break;
            case Tool::Coffee: icon(5, m, 1.6f); break;
            case Tool::Plate: {
                const Plate& pl = plates_[heldPlate_];
                DrawEllipse((int)m.x, (int)m.y + 4, 52, 21, alpha(BLACK, 0.25f));
                DrawEllipse((int)m.x, (int)m.y, 50, 20, rgba(0xf8f8ffff));
                drawPancake({m.x, m.y - 4}, 38, 14, cookColor(std::min(1.9f, pl.side / kFlipAt * 0.9f)), cookColor(1.3f), pl.inPan, pl.top, 0, (unsigned)heldPlate_ * 5 + 1);
                break;
            }
        }
    }

    void drawHud() {
        const DayDef& d = kDays[day_];
        Rectangle r{VW - 250.0f, 64, 236, 66};
        DrawRectangleRounded(r, 0.3f, 8, alpha(col::bgDeep, 0.85f));
        ui::text(TextFormat("%s  -  day %d", d.name, day_ + 1), r.x + 14, r.y + 18, 14, col::muted, ui::Left, true);
        bool ok = earned_ >= d.target;
        ui::text(TextFormat("EUR %d", earned_), r.x + 14, r.y + 44, 24, ok ? col::green : col::gold, ui::Left, true);
        ui::text(TextFormat("/ %d", d.target), r.x + 24 + ui::measure(TextFormat("EUR %d", earned_), 24, true), r.y + 46, 15, col::muted, ui::Left, true);
        int left = d.customers - spawned_ + (int)customers_.size();
        ui::text(TextFormat("%d to go", left), r.x + r.width - 14, r.y + 46, 13, col::text, ui::Right, true);
        if (streak_ >= 3 && shiftT_ - lastServe_ < 6) ui::textOutline(TextFormat("RUSH x%d", streak_), VW - 132, 150, 22, col::cyan, col::outline, 3);
    }

    void draw() override {
        if (phase_ == Phase::Intro) {
            drawRoom();
            drawKitchen();
            drawIntro();
            fx_.draw();
            return;
        }
        drawRoom();
        // customers sit behind their tables
        for (auto& c : customers_) drawCustomer(c);
        for (int i = 0; i < kDays[day_].tables; i++) drawTable(i);
        for (int i = kDays[day_].tables; i < 4; i++) {  // tables not open yet
            drawTable(i);
            ui::text("RESERVED", kTableX[i], kTableY, 12, alpha(WHITE, 0.8f), ui::Center, true);
        }
        for (auto& c : customers_) drawBubble(c);
        if (tool_ == Tool::Plate || tool_ == Tool::Coffee)
            for (auto& c : customers_)
                if (c.state == 1 && std::abs(E().mouse().x - kTableX[c.table]) < 100 && E().mouse().y < 300) highlight({kTableX[c.table], 180}, 80, 80, col::gold);
        drawKitchen();
        steam_.draw();
        drawHud();
        fx_.draw();
        drawTool();
        if (paused_) drawPause();
        if (phase_ == Phase::Results) drawResults();
        if (phase_ == Phase::Week) drawWeek();
    }

    // ------------------------------------------------------------ overlays
    void drawIntro() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 360, 70, 720, 560};
        ui::panel(p, alpha(col::panel, 0.97f), 0.06f, alpha(col::gold, 0.6f));
        fx::gradientText("PANNENKOEKENHUIS", VW / 2.0f, p.y + 52, 52, WHITE, col::gold, 4);
        ui::text("Pieter's pancake house on the canal. One week to make it work.", VW / 2.0f, p.y + 96, 16, col::pink, ui::Center, true);
        // day picker
        int maxDay = std::min(P().stats.pannenDay, kDayCount - 1);
        for (int i = 0; i < kDayCount; i++) {
            Rectangle b{p.x + 40 + i * 92.0f, p.y + 124, 84, 62};
            bool open = i <= maxDay, sel = i == day_;
            bool over = ui::hovered(b) && open;
            DrawRectangleRounded(b, 0.25f, 8, sel ? col::gold : over ? alpha(WHITE, 0.15f) : alpha(WHITE, 0.06f));
            ui::text(std::string(kDays[i].name).substr(0, 3), b.x + b.width / 2, b.y + 22, 16, sel ? col::outline : open ? WHITE : col::muted, ui::Center, true);
            ui::text(open ? TextFormat("EUR %d", kDays[i].target) : "locked", b.x + b.width / 2, b.y + 44, 11, sel ? col::outline : col::muted, ui::Center, true);
            if (over && E().mousePressed()) {
                E().consumeClick();
                day_ = i;
                E().sfx.play(Sfx::Click);
            }
        }
        const DayDef& d = kDays[day_];
        ui::text(TextFormat("DAY %d: %s", day_ + 1, d.name), VW / 2.0f, p.y + 218, 26, WHITE, ui::Center, true);
        ui::paragraph(d.news, {p.x + 80, p.y + 240, p.width - 160, 0}, 17, col::text);
        ui::text(TextFormat("%d tables   %d pans   %d customers   target EUR %d", d.tables, d.pans, d.customers, d.target), VW / 2.0f, p.y + 300, 16, col::gold,
                 ui::Center, true);
        const char* how[] = {"1. Click the batter, then a pan.", "2. Bake spek, kaas or appel in before you flip.",
                             "3. Flip when the ring turns green. Take it off when it says DONE.",
                             "4. Click it onto the pass, add syrup or sugar, then carry the plate to the table.", "Right-click drops whatever you're holding."};
        for (int i = 0; i < 5; i++) ui::text(how[i], p.x + 90, p.y + 340 + i * 26.0f, 15, i == 4 ? col::muted : col::text, ui::Left, true);
        if (ui::button({p.x + p.width / 2 - 150, p.y + 482, 300, 58}, "OPEN THE DOORS", rgba(0x2a8a4aff), 24)) startShift();
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawPause() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 220, 200, 440, 300};
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::gold, 0.6f));
        ui::textOutline("PAUSED", VW / 2.0f, p.y + 56, 56, WHITE, col::outline, 5);
        if (ui::button({p.x + 40, p.y + 110, 360, 50}, "RESUME", col::green, 22)) paused_ = false;
        if (ui::button({p.x + 40, p.y + 170, 360, 50}, "RESTART DAY", col::panelHi, 22)) startShift();
        if (ui::button({p.x + 40, p.y + 230, 360, 50}, "CLOSE UP SHOP", col::red, 22)) {
            paused_ = false;
            phase_ = Phase::Intro;
            E().sfx.stop(Sfx::Sizzle);
        }
    }

    void drawResults() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 300, 110, 600, 460};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(passed_ ? col::green : col::red, 0.7f));
        ui::textOutline(passed_ ? "GOOD DAY!" : "SLOW DAY...", VW / 2.0f, p.y + 62, 60, passed_ ? rgba(0xb8ffc8ff) : rgba(0xffc0b0ff), col::outline, 5);
        ui::text(TextFormat("%s is over", kDays[day_].name), VW / 2.0f, p.y + 112, 18, col::muted, ui::Center, true);
        ui::textShadow(TextFormat("EUR %d", earned_), VW / 2.0f, p.y + 172, 58, WHITE, ui::Center, true, 4);
        ui::text(TextFormat("target EUR %d", kDays[day_].target), VW / 2.0f, p.y + 214, 18, passed_ ? col::green : col::red, ui::Center, true);
        ui::text(TextFormat("%d served     %d walked out", served_, walkouts_), VW / 2.0f, p.y + 254, 18, col::text, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 296}, 15);
        ui::text("+" + fmtNum(coins_) + " Pietercoins", VW / 2.0f - 66, p.y + 296, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (passed_) {
                if (ui::button({p.x + 40, p.y + 360, 250, 64}, "NEXT DAY", rgba(0x2a8a4aff), 24) || IsKeyPressed(KEY_ENTER)) {
                    day_ = std::min(day_ + 1, kDayCount - 1);
                    phase_ = Phase::Intro;
                }
            } else if (ui::button({p.x + 40, p.y + 360, 250, 64}, "TRY AGAIN", col::orange, 24) || IsKeyPressed(KEY_ENTER)) {
                startShift();
            }
            if (ui::button({p.x + 310, p.y + 360, 250, 64}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void drawWeek() {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.6f));
        Rectangle p{VW / 2.0f - 320, 90, 640, 500};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(col::gold, 0.8f));
        fx::gradientText("A STAR!", VW / 2.0f, p.y + 64, 72, WHITE, col::gold, 5);
        art::pieter({VW / 2.0f, p.y + 170}, 54, P().hat, std::sin(t_ * 3) * 6);
        DrawRectangleRounded({VW / 2.0f - 34, p.y + 96, 68, 22}, 0.5f, 6, WHITE);  // chef's hat
        DrawCircleV({VW / 2.0f - 20, p.y + 92}, 16, WHITE);
        DrawCircleV({VW / 2.0f + 2, p.y + 86}, 18, WHITE);
        DrawCircleV({VW / 2.0f + 22, p.y + 92}, 15, WHITE);
        ui::text("The critic gave Pieter's Pannenkoekenhuis a star. A whole week, survived.", VW / 2.0f, p.y + 250, 16, col::text, ui::Center, true);
        ui::text(TextFormat("Sunday: EUR %d", earned_), VW / 2.0f, p.y + 290, 26, col::gold, ui::Center, true);
        ui::coin({VW / 2.0f - 90, p.y + 336}, 15);
        ui::text("+" + fmtNum(coins_) + " Pietercoins", VW / 2.0f - 66, p.y + 336, 24, col::gold, ui::Left, true);
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 40, p.y + 400, 270, 64}, "PLAY AGAIN", rgba(0x2a8a4aff), 22)) phase_ = Phase::Intro;
            if (ui::button({p.x + 330, p.y + 400, 270, 64}, "MENU", col::panelHi, 22)) E().go(makeMenu);
        }
    }

private:
    Phase phase_ = Phase::Intro;
    int day_ = 0;
    Pan pans_[4];
    Plate plates_[3];
    std::vector<Customer> customers_;
    Tool tool_ = Tool::None;
    int heldPlate_ = 0, earned_ = 0, served_ = 0, walkouts_ = 0, spawned_ = 0, coffeeState_ = 0, streak_ = 0;
    long long coins_ = 0;
    float t_ = 0, phaseT_ = 0, shiftT_ = 0, spawnT_ = 0, coffeeT_ = 0, lastServe_ = -10;
    bool paused_ = false, passed_ = false, bot_ = std::getenv("PG_PANNEN_BOT") != nullptr;
    float botT_ = 0;
    std::vector<Vector2> botQ_;
    Particles fx_, steam_;
};

}  // namespace

std::unique_ptr<Scene> makePannen() { return std::make_unique<PannenScene>(); }

}  // namespace pg
