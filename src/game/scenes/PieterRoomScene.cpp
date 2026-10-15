// PIETER ROOM: take care of Pieter. Feed him, play, shower, sleep, and talk.
// Needs decay in real time (also while the game is closed, capped).
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <ctime>

namespace pg {
namespace {

constexpr Vector2 kPieter{640, 330};
constexpr float kHeadR = 92;

enum class Mode { Idle, Feed, Shower, Chat };

struct ChatLine {
    bool fromPieter;
    std::string text;
};

struct Flying {
    std::string item;
    Vector2 from;
    float t = 0;
};

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const std::string& pick(const std::vector<std::string>& v) { return v[rndi(0, (int)v.size() - 1)]; }

class PieterRoomScene : public Scene {
public:
    void enter() override {
        auto& n = P().pieter;
        long long now = unixNow();
        float hours = std::clamp((now - n.lastUpdate) / 3600.0f, 0.0f, 48.0f);
        n.hunger -= hours * 6;
        n.fun -= hours * 5;
        n.hygiene -= hours * 3;
        n.energy += hours * (n.sleeping ? 20.0f : -4.0f);
        clampNeeds();
        n.lastUpdate = now;
        chat_.push_back({true, "Hoi! Ik ben Pieter. Type something!"});
        thoughtT_ = -4.5f;  // first thought shortly after entering
    }

    void exit() override {
        P().pieter.lastUpdate = unixNow();
        P().save();
    }

    void clampNeeds() {
        auto& n = P().pieter;
        for (float* v : {&n.hunger, &n.fun, &n.energy, &n.hygiene}) *v = std::clamp(*v, 0.0f, 100.0f);
    }

    void addXp(int amount) {
        auto& n = P().pieter;
        n.xp += amount;
        while (n.xp >= PieterNeeds::xpForLevel(n.level)) {
            n.xp -= PieterNeeds::xpForLevel(n.level);
            n.level++;
            long long reward = 50LL * n.level;
            P().earn(reward);
            E().sfx.play(Sfx::Win);
            fx_.confetti({kPieter.x, kPieter.y - 40}, 120);
            E().toast("Pieter reached level " + std::to_string(n.level) + "!  +" + fmtNum(reward) + " coins", col::gold);
            P().save();
        }
    }

    void think(const std::string& s) {
        thought_ = s;
        thoughtT_ = 3.5f;
    }

    std::string moodLine() {
        auto& n = P().pieter;
        float lowest = std::min({n.hunger, n.fun, n.energy, n.hygiene});
        if (n.sleeping) return "zzz... boba... zzz";
        if (lowest > 70) return pick({"Life is good. JUHUH!", "Eat. Sleep. Game. Repeat.", "I feel like a legend today", "Best day ever"});
        if (lowest == n.hunger) return pick({"I'm STARVING. boba pls", "Is there a frikandel somewhere?", "My stomach is making noises"});
        if (lowest == n.fun) return pick({"I'm bored... let's play", "Pet me or something", "So bored I might get a job"});
        if (lowest == n.energy) return pick({"So... sleepy...", "I need to sleep. or Game Fuel.", "*yawns*"});
        return pick({"I kinda stink lol", "Shower time? maybe?", "Don't smell me rn"});
    }

    std::string reply(const std::string& raw) {
        std::string s = lower(raw);
        auto has = [&](std::initializer_list<const char*> keys) {
            for (auto k : keys)
                if (s.find(k) != std::string::npos) return true;
            return false;
        };
        auto& n = P().pieter;
        if (has({"how are", "how r u", "hoe gaat", "you ok", "u ok"})) {
            if (n.mood() > 75) return "Amazing! Feeling like a pro gamer (unlike Davi)";
            if (n.mood() > 45) return "I'm okay. Could use some boba tho";
            return "Not great... " + moodLine();
        }
        if (has({"hungry", "food", "eat", "eten", "honger"}))
            return n.hunger < 50 ? "YES I'm hungry. Feed me something!" : "Nah I'm full. But I could eat a stroopwafel";
        if (has({"horse", "paard", "juhuh", "neigh"}))
            return pick({"JUHUH!!! I love horses", "Horses are the best animal. no debate.", "My horse wears headphones. does yours?",
                         "Go to the horse races and bet on Juhuh Express"});
        if (has({"davi"})) return pick({"Davi claims he's pro since 2010. he's not.", "Davi is my brother. unfortunately.", "Davi rage quits every game lol"});
        if (has({"kira"})) return pick({"Kira is my sister. Brother sister rivals FOREVER.", "Kira cheats at every game", "Don't tell Kira but she's ok"});
        if (has({"job", "work", "werk", "baan"})) return pick({"Get a job? I AM the job.", "No.", "I have a job. It's gaming.", "There's literally a song about this"});
        if (has({"boba", "tea", "thee"})) return pick({"Boba is life", "Can you buy me boba pls", "Boba > water"});
        if (has({"stroop", "wafel", "waffle"})) return pick({"Stroopwafel on top of your coffee cup. that's the way.", "Best Dutch invention. Better than the bicycle."});
        if (has({"love", "like you", "cute", "lief"})) return pick({"Ong you fking like me or sum", "Aww <3", "I know. I'm Pieter."});
        if (has({"money", "coin", "rich", "casino", "gamble", "bet"}))
            return pick({"Pietercoins are the strongest currency on earth", "Gambling is a strategy. trust.", "Go all in on red. what could go wrong",
                         "Mines is easy just don't click the bomb"});
        if (has({"sleep", "tired", "night", "slaap", "moe"})) return pick({"Good night Pieter... wait that's me", "Sleep is for people without games"});
        if (has({"skibidi", "skid"})) return "Skibidi Pieter yes yes";
        if (has({"song", "music", "muziek", "ost"})) return pick({"Have you heard 'pee pee pee pieter'? Banger.", "My OST goes hard", "Press F9 to skip songs btw"});
        if (has({"dutch", "netherlands", "holland", "nederland"})) return pick({"Netherlands #1", "Hagelslag on bread is breakfast. fight me.", "Lekker man"});
        if (has({"who", "name", "naam", "wie"})) return "Ik ben Pieter. Dutch meme legend. Third game already.";
        if (has({"game", "play", "gaming", "spel"})) return pick({"Eat sleep game repeat", "Wanna play? press PLAY!", "PieterGame 3 is the best one, don't tell 1 and 2"});
        if (has({"hi", "hello", "hey", "hoi", "hallo", "yo", "sup"})) return pick({"Hoi!", "Yo what's up", "Hallo! Ik ben Pieter.", "Heyyy"});
        if (has({"stink", "smell", "shower", "douche"})) return n.hygiene < 50 ? "...ok maybe I need a shower" : "I smell like roses. gamer roses.";
        if (has({"stupid", "dumb", "noob", "bad", "trash"})) return pick({"Rude.", "You're a noob", "I'm telling Kira"});
        if (has({"?"})) return pick({"Good question. No idea.", "Ja.", "Nee.", "Maybe? ask Davi"});
        return pick({"Huh?", "Ja ja", "Interesting... anyway, boba?", "lol", "What do you mean", "I don't understand but okay", "Zeker!", "Wat?",
                     "Juhuh"});
    }

    void sendChat() {
        if (input_.empty()) return;
        chat_.push_back({false, input_});
        pendingReply_ = reply(input_);
        replyT_ = rnd(0.6f, 1.3f);
        input_.clear();
        if (talkCd_ <= 0) {
            P().pieter.fun += 3;
            addXp(3);
            talkCd_ = 5;
        }
        clampNeeds();
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        auto& n = P().pieter;
        fx_.update(dt);
        talkCd_ -= dt;
        petCd_ -= dt;
        thoughtT_ -= dt;
        playT_ = std::max(0.0f, playT_ - dt);

        // live decay
        n.hunger -= dt * 0.05f;
        n.fun -= dt * 0.04f;
        n.hygiene -= dt * 0.025f;
        if (n.sleeping) {
            n.energy += dt * 1.6f;
            if (n.energy >= 100) { n.sleeping = false; think("Good morning! Fully charged."); E().sfx.play(Sfx::Bell, 0.5f); }
            zzzT_ -= dt;
            if (zzzT_ <= 0) {
                zzzT_ = 0.7f;
                fx_.floatText("z", {kPieter.x + 60 + rnd(-10, 10), kPieter.y - 70}, col::cyan, rnd(22, 34));
            }
        } else {
            n.energy -= dt * 0.03f;
        }
        clampNeeds();

        // autosave every 20s so the pet state survives crashes
        saveT_ += dt;
        if (saveT_ > 20) { saveT_ = 0; n.lastUpdate = unixNow(); P().save(); }

        if (thoughtT_ < -6 && mode_ != Mode::Chat) think(moodLine());

        if (replyT_ > 0) {
            replyT_ -= dt;
            if (replyT_ <= 0) {
                chat_.push_back({true, pendingReply_});
                if (chat_.size() > 30) chat_.erase(chat_.begin());
                E().sfx.play(Sfx::Bubble, 0.6f);
                think(pendingReply_);
            }
        }

        if (playT_ > 0) {
            if (std::fmod(playT_, 0.5f) < dt) {
                static const char* gg[] = {"GG!", "EZ", "CLUTCH", "NO WAY", "JUHUH!", "+1 WIN"};
                fx_.floatText(gg[rndi(0, 5)], {kPieter.x + rnd(-160, 160), kPieter.y - rnd(40, 140)}, ColorFromHSV(rnd(0, 360), 0.6f, 1), 26);
                E().sfx.play(Sfx::Tick, 0.6f, rnd(0.8f, 1.5f));
            }
        }

        for (auto& f : flying_) f.t += dt * 2.2f;
        for (auto& f : flying_)
            if (f.t >= 1) eat(f.item);
        std::erase_if(flying_, [](auto& f) { return f.t >= 1; });

        if (mode_ == Mode::Chat) {
            int c;
            while ((c = GetCharPressed()) != 0)
                if (c >= 32 && c < 127 && input_.size() < 56) input_ += (char)c;
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !input_.empty()) input_.pop_back();
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) sendChat();
        }

        // shower scrubbing
        if (mode_ == Mode::Shower) {
            Vector2 m = E().mouse();
            if (E().mouseDown() && Vector2Distance(m, kPieter) < kHeadR + 60) {
                float moved = Vector2Distance(m, lastMouse_);
                n.hygiene += moved * 0.035f;
                scrub_ += moved;
                if (scrub_ > 30) {
                    scrub_ = 0;
                    Particle p;
                    p.pos = m + rndDir() * rnd(0, 20);
                    p.vel = {rnd(-40, 40), rnd(-90, -30)};
                    p.life = p.maxLife = rnd(0.8f, 1.5f);
                    p.size = rnd(5, 14);
                    p.endSize = p.size;
                    p.color = alpha(WHITE, 0.85f);
                    p.shape = PShape::Bubble;
                    fx_.add(p);
                    E().sfx.play(Sfx::Bubble, 0.25f, rnd(0.8f, 1.4f));
                }
                if (n.hygiene >= 100 && !cleanAnnounced_) {
                    cleanAnnounced_ = true;
                    think("Squeaky clean!");
                    addXp(10);
                    E().sfx.play(Sfx::Perfect);
                }
            }
            lastMouse_ = m;
            clampNeeds();
        }

        if (E().backPressed()) {
            if (mode_ != Mode::Idle) mode_ = Mode::Idle;
            else E().go(makeMenu);
        }
    }

    void eat(const std::string& id) {
        const ItemDef* d = findItem(id);
        if (!d) return;
        auto& n = P().pieter;
        n.hunger += d->hunger;
        n.fun += d->fun;
        n.energy += d->energy;
        clampNeeds();
        addXp(10);
        E().sfx.play(Sfx::Chew);
        fx_.burst({kPieter.x, kPieter.y + 40}, 14, d->color, 200, 5, 0.6f, PShape::Square, 400);
        fx_.floatText("+" + std::to_string(d->hunger) + " food", {kPieter.x, kPieter.y - 120}, col::green, 24);
        static const char* yum[] = {"Lekker!", "Mmmm", "YUM", "More pls", "Delicious", "10/10"};
        think(id == "boba" ? "BOBAAAA" : yum[rndi(0, 5)]);
        if (n.hunger >= 100) think("I'm so full I could explode");
    }

    void petPieter() {
        auto& n = P().pieter;
        if (n.sleeping) {
            think("five more minutes...");
            return;
        }
        petCd_ = 0.22f;
        n.fun += 2;
        n.energy -= 0.3f;
        clampNeeds();
        addXp(1);
        E().sfx.play(Sfx::Kiss, 0.5f, rnd(0.9f, 1.3f));
        fx_.burst(E().mouse(), 4, col::pink, 160, 9, 0.8f, PShape::Heart, -100);
        squish_ = 1;
    }

    // ============================================================ draw
    void drawRoom() {
        bool night = isNight();
        float dark = P().pieter.sleeping ? 0.55f : 0.0f;
        DrawRectangleGradientV(0, 0, VW, 540, rgba(0x3b2470ff), rgba(0x24164aff));
        // wall stripes
        for (int x = 0; x < VW; x += 80) DrawRectangle(x, 0, 40, 540, alpha(WHITE, 0.015f));
        // floor
        DrawRectangle(0, 520, VW, VH - 520, rgba(0x2a1a14ff));
        for (int i = 0; i < 9; i++) DrawLineEx({0, 520.0f + i * 26}, {(float)VW, 520.0f + i * 26}, 1.5f, rgba(0x1e120dff));
        DrawRectangle(0, 512, VW, 10, rgba(0x1a0f2eff));
        // rug
        DrawEllipse(640, 600, 330, 60, rgba(0x6b3fd1ff));
        DrawEllipse(640, 600, 300, 50, rgba(0x8f5cffff));

        // window with real day/night
        Rectangle win{960, 70, 230, 170};
        DrawRectangleRec(shrink(win, -10), rgba(0x1a0f2eff));
        DrawRectangleGradientV((int)win.x, (int)win.y, (int)win.width, (int)win.height, night ? rgba(0x0b0b2aff) : rgba(0x6ec6ffff),
                               night ? rgba(0x2a1650ff) : rgba(0xffd9a0ff));
        if (night) {
            DrawCircleV({win.x + 170, win.y + 50}, 22, rgba(0xfff3c0ff));
            DrawCircleV({win.x + 180, win.y + 44}, 20, rgba(0x0b0b2aff));
            for (int i = 0; i < 12; i++) DrawCircleV({win.x + (i * 73 % 220) + 5, win.y + (i * 41 % 150) + 8}, 1.5f + (i % 2), alpha(WHITE, 0.5f + 0.5f * std::sin(t_ * 2 + i)));
        } else {
            DrawCircleV({win.x + 60, win.y + 55}, 26, rgba(0xfff3a0ff));
            DrawEllipse((int)win.x + 150, (int)win.y + 110, 50, 16, alpha(WHITE, 0.9f));
            DrawEllipse((int)win.x + 175, (int)win.y + 100, 34, 18, alpha(WHITE, 0.9f));
        }
        DrawRectangle((int)(win.x + win.width / 2 - 4), (int)win.y, 8, (int)win.height, rgba(0x1a0f2eff));
        DrawRectangle((int)win.x, (int)(win.y + win.height / 2 - 4), (int)win.width, 8, rgba(0x1a0f2eff));

        // posters
        Rectangle p1{318, 236, 128, 160};
        DrawRectangleRec(shrink(p1, -6), col::outline);
        DrawRectangleRec(p1, rgba(0x15121cff));
        const char* lines[] = {"EAT", "SLEEP", "GAME", "REPEAT"};
        for (int i = 0; i < 4; i++) ui::text(lines[i], p1.x + p1.width / 2, p1.y + 28 + i * 35, 26, rgba(0xe8e2d0ff), ui::Center, true);

        Rectangle p2{310, 70, 230, 120};
        float flick = (std::fmod(t_, 4.0f) < 0.08f) ? 0.4f : 1.0f;
        DrawRectangleRec(shrink(p2, -6), col::outline);
        DrawRectangleRec(p2, rgba(0x2a0f1eff));
        ui::textOutline("BEC IT", p2.x + p2.width / 2, p2.y + 38, 44, alpha(rgba(0xff4a5aff), flick), alpha(rgba(0x7a0f1aff), flick), 3);
        ui::textOutline("ROOM", p2.x + p2.width / 2, p2.y + 86, 44, alpha(rgba(0xff4a5aff), flick), alpha(rgba(0x7a0f1aff), flick), 3);

        // desk + monitor on the right
        DrawRectangle(890, 420, 330, 22, rgba(0x3a2418ff));
        DrawRectangle(905, 442, 16, 90, rgba(0x2a1a10ff));
        DrawRectangle(1190, 442, 16, 90, rgba(0x2a1a10ff));
        Rectangle mon{960, 300, 190, 112};
        DrawRectangleRounded(shrink(mon, -6), 0.08f, 6, rgba(0x15121cff));
        Color screen = playT_ > 0 ? ColorFromHSV(std::fmod(t_ * 120, 360), 0.5f, 0.9f) : rgba(0x6b3fd1ff);
        DrawRectangleRec(mon, P().pieter.sleeping ? rgba(0x0a0a10ff) : screen);
        if (!P().pieter.sleeping) ui::text(playT_ > 0 ? "GAMING" : "PieterOS", mon.x + mon.width / 2, mon.y + mon.height / 2, 22, WHITE, ui::Center, true);
        DrawRectangle(1048, 412, 14, 10, rgba(0x15121cff));
        // RGB keyboard
        for (int i = 0; i < 10; i++) DrawRectangle(965 + i * 18, 408, 15, 8, ColorFromHSV(std::fmod(t_ * 90 + i * 30, 360), 0.8f, 0.9f));
        // game fuel can + boba on desk
        art::itemIcon("gamefuel", {1180, 398}, 40);
        art::boba({920, 396}, 42);

        // bed on the left
        DrawRectangleRounded({40, 470, 230, 70}, 0.2f, 8, rgba(0x4a2d8aff));
        DrawRectangleRounded({50, 450, 80, 34}, 0.5f, 8, rgba(0xe8e2f0ff));
        DrawRectangle(40, 530, 16, 40, rgba(0x2a1a10ff));
        DrawRectangle(254, 530, 16, 40, rgba(0x2a1a10ff));

        if (dark > 0) DrawRectangle(0, 0, VW, VH, alpha(rgba(0x05031aff), dark));
    }

    bool isNight() const {
        std::time_t now = std::time(nullptr);
        std::tm* lt = std::localtime(&now);
        return lt->tm_hour < 7 || lt->tm_hour >= 20;
    }

    void drawPieter() {
        auto& n = P().pieter;
        squish_ = damp(squish_, 0, 8, GetFrameTime());
        float breathe = std::sin(t_ * 2.2f) * 4;
        float bounce = playT_ > 0 ? std::abs(std::sin(t_ * 9)) * -24 : 0;
        Vector2 head{kPieter.x, kPieter.y + breathe * 0.5f + bounce};
        // body: black t-shirt
        Rectangle torso{kPieter.x - 110, kPieter.y + 70 + bounce, 220, 170};
        DrawRectangleRounded(shrink(torso, -4), 0.45f, 12, col::outline);
        DrawRectangleRounded(torso, 0.45f, 12, rgba(0x1f1c26ff));
        DrawCircleV({kPieter.x, kPieter.y + 160 + bounce}, 22, rgba(0x2c2836ff));
        ui::text("P", kPieter.x, kPieter.y + 160 + bounce, 30, rgba(0x47404fff), ui::Center, true);
        // arms
        float wave = playT_ > 0 ? std::sin(t_ * 14) * 20 : std::sin(t_ * 1.5f) * 4;
        DrawLineEx({torso.x + 20, torso.y + 30}, {torso.x - 30, torso.y + 140 + wave}, 40, col::outline);
        DrawLineEx({torso.x + torso.width - 20, torso.y + 30}, {torso.x + torso.width + 30, torso.y + 140 - wave}, 40, col::outline);
        DrawLineEx({torso.x + 20, torso.y + 30}, {torso.x - 30, torso.y + 140 + wave}, 34, rgba(0x1f1c26ff));
        DrawLineEx({torso.x + torso.width - 20, torso.y + 30}, {torso.x + torso.width + 30, torso.y + 140 - wave}, 34, rgba(0x1f1c26ff));
        DrawCircleV({torso.x - 30, torso.y + 150 + wave}, 20, rgba(0xe8a982ff));
        DrawCircleV({torso.x + torso.width + 30, torso.y + 150 - wave}, 20, rgba(0xe8a982ff));
        if (playT_ > 0) {
            // controller in hands
            Rectangle c{kPieter.x - 70, torso.y + 120, 140, 50};
            DrawRectangleRounded(c, 0.8f, 10, rgba(0x5b2fb0ff));
            DrawCircleV({c.x + 30, c.y + 25}, 8, col::outline);
            DrawCircleV({c.x + 110, c.y + 25}, 8, col::gold);
        }
        // neck
        DrawRectangleRounded({kPieter.x - 30, head.y + kHeadR * 0.6f, 60, 50}, 0.4f, 8, col::outline);
        DrawRectangleRounded({kPieter.x - 26, head.y + kHeadR * 0.6f, 52, 50}, 0.4f, 8, rgba(0xe0a07aff));

        float sq = 1 + squish_ * 0.08f;
        float tilt = std::sin(t_ * 1.1f) * 3 + (n.mood() < 30 ? 8 : 0);
        Color tint = n.hygiene < 25 ? mix(WHITE, rgba(0xa8d08dff), 0.35f) : WHITE;
        art::pieter(head, kHeadR * sq, P().hat, tilt, tint);
        if (n.hygiene < 25) {  // stink lines
            for (int i = 0; i < 3; i++) {
                float x = head.x - 60 + i * 60, y = head.y - kHeadR - 20 - std::fmod(t_ * 30 + i * 20, 40);
                DrawSplineSegmentBezierQuadratic({x, y + 30}, {x + 14, y + 15}, {x, y}, 3, alpha(col::green, 0.6f));
            }
        }
        if (n.sleeping) {
            Rectangle mask{head.x - kHeadR * 0.85f, head.y - kHeadR * 0.05f, kHeadR * 1.7f, kHeadR * 0.38f};
            DrawRectangleRounded(shrink(mask, -3), 0.8f, 10, col::outline);
            DrawRectangleRounded(mask, 0.8f, 10, rgba(0x7a4fd8ff));
            ui::text("zz", head.x, mask.y + mask.height / 2, 26, WHITE, ui::Center, true);
        }
        if (mode_ == Mode::Shower) {
            // shower head + water
            DrawRectangleRounded({kPieter.x - 60, 40, 120, 26}, 0.6f, 8, rgba(0xb0b8c8ff));
            for (int i = 0; i < 26; i++) {
                float x = kPieter.x - 100 + (i * 37 % 200);
                float y = 70 + std::fmod(t_ * 600 + i * 47, 380);
                DrawLineEx({x, y}, {x, y + 14}, 2, alpha(col::cyan, 0.6f));
            }
        }
    }

    void drawThought() {
        if (thoughtT_ <= 0 || thought_.empty()) return;
        float a = clamp01(thoughtT_ / 0.4f) * clamp01((3.5f - thoughtT_) / 0.2f + 0.2f);
        float w = std::min(380.0f, ui::measure(thought_, 22) + 40);
        Rectangle box{kPieter.x + 90, kPieter.y - 200, w, 0};
        box.height = ui::paragraph(thought_, {0, 0, w - 36, 0}, 22, WHITE, false) + 26;
        DrawCircleV({kPieter.x + 82, kPieter.y - 92}, 8, alpha(WHITE, a));
        DrawCircleV({kPieter.x + 98, kPieter.y - 116}, 12, alpha(WHITE, a));
        DrawRectangleRounded(box, 0.4f, 12, alpha(WHITE, a));
        DrawRectangleRoundedLinesEx(box, 0.4f, 12, 2, alpha(col::outline, a));
        ui::paragraph(thought_, {box.x + 18, box.y + 12, w - 36, 0}, 22, alpha(col::outline, a));
    }

    void drawStats() {
        auto& n = P().pieter;
        Rectangle r{20, 84, 260, 210};
        ui::panel(r, alpha(col::panel, 0.92f));
        struct S { const char* name; float v; Color c; } ss[] = {
            {"HUNGER", n.hunger, col::orange}, {"FUN", n.fun, col::pink}, {"ENERGY", n.energy, col::gold}, {"HYGIENE", n.hygiene, col::cyan}};
        for (int i = 0; i < 4; i++) {
            float y = r.y + 30 + i * 42;
            Color c = ss[i].v < 25 && std::fmod(t_, 0.8f) < 0.4f ? col::red : ss[i].c;
            ui::text(ss[i].name, r.x + 16, y - 6, 16, col::muted);
            ui::text(std::to_string((int)ss[i].v), r.x + r.width - 16, y - 6, 16, col::text, ui::Right);
            ui::bar({r.x + 16, y + 8, r.width - 32, 14}, ss[i].v / 100, c);
        }
        Rectangle lv{20, 304, 260, 70};
        ui::panel(lv, alpha(col::panel, 0.92f));
        ui::text("LEVEL " + std::to_string(n.level), lv.x + 16, lv.y + 22, 22, col::gold, ui::Left, true);
        float m = n.mood();
        const char* mood = m > 85 ? "ECSTATIC" : m > 65 ? "HAPPY" : m > 45 ? "OKAY" : m > 25 ? "SAD" : "MISERABLE";
        ui::text(mood, lv.x + lv.width - 16, lv.y + 22, 18, m > 45 ? col::green : col::red, ui::Right, true);
        ui::bar({lv.x + 16, lv.y + 42, lv.width - 32, 12}, (float)n.xp / PieterNeeds::xpForLevel(n.level), col::violet);
    }

    void drawFeedTray() {
        std::vector<const ItemDef*> foods;
        for (auto& it : itemDb())
            if (it.kind == ItemKind::Food) foods.push_back(&it);
        float w = foods.size() * 104.0f + 20;
        Rectangle r{VW / 2.0f - w / 2, VH - 230, w, 130};
        ui::panel(r, alpha(col::panel, 0.96f));
        for (size_t i = 0; i < foods.size(); i++) {
            const ItemDef* f = foods[i];
            Rectangle s{r.x + 14 + i * 104.0f, r.y + 12, 94, 106};
            int cnt = P().count(f->id);
            bool hot = ui::hovered(s) && cnt > 0;
            DrawRectangleRounded(s, 0.2f, 8, hot ? col::panelHi : alpha(col::bgDeep, 0.7f));
            art::itemIcon(f->id, {s.x + s.width / 2, s.y + 42}, 56);
            if (cnt == 0) DrawRectangleRounded(s, 0.2f, 8, alpha(col::bgDeep, 0.65f));
            ui::text("x" + std::to_string(cnt), s.x + s.width - 8, s.y + 14, 16, cnt ? col::gold : col::muted, ui::Right, true);
            ui::text(f->name.substr(0, 11), s.x + s.width / 2, s.y + s.height - 14, 14, col::text, ui::Center);
            if (ui::hovered(s) && E().mousePressed()) {
                E().consumeClick();
                if (cnt > 0 && P().take(f->id)) {
                    flying_.push_back({f->id, center(s)});
                    E().sfx.play(Sfx::Whoosh, 0.5f);
                } else {
                    E().sfx.play(Sfx::Error, 0.5f);
                    E().toast("No " + f->name + " left. Buy some in the SHOP!", col::orange);
                }
            }
        }
        if (ui::button({r.x + r.width - 130, r.y - 44, 130, 36}, "GO TO SHOP", col::green, 16)) E().go(makeShop);
    }

    void drawChat() {
        Rectangle r{VW - 420, 84, 400, 470};
        ui::panel(r, alpha(col::panel, 0.97f));
        ui::text("CHAT WITH PIETER", r.x + 18, r.y + 26, 20, col::violet, ui::Left, true);
        // messages bottom-up
        float y = r.y + r.height - 78;
        E().beginClip({r.x, r.y + 48, r.width, r.height - 112});
        for (int i = (int)chat_.size() - 1; i >= 0 && y > r.y + 40; i--) {
            auto& m = chat_[i];
            float maxW = 270;
            float h = ui::paragraph(m.text, {0, 0, maxW, 0}, 19, WHITE, false);
            float tw = std::min(maxW, ui::measure(m.text, 19)) + 24;
            float bh = h + 18;
            y -= bh;
            Rectangle b{m.fromPieter ? r.x + 16 : r.x + r.width - 16 - tw, y, tw, bh};
            DrawRectangleRounded(b, 0.35f, 10, m.fromPieter ? col::panelHi : col::purple);
            ui::paragraph(m.text, {b.x + 12, b.y + 7, maxW, 0}, 19, col::text);
            y -= 10;
        }
        if (replyT_ > 0) {
            Rectangle b{r.x + 16, r.y + r.height - 108, 70, 26};
            DrawRectangleRounded(b, 0.5f, 8, col::panelHi);
            for (int i = 0; i < 3; i++) DrawCircleV({b.x + 20 + i * 15.0f, b.y + 13 + std::sin(t_ * 10 + i) * 3}, 4, col::muted);
        }
        E().endClip();
        Rectangle in{r.x + 14, r.y + r.height - 58, r.width - 28, 44};
        DrawRectangleRounded(in, 0.4f, 10, col::bgDeep);
        DrawRectangleRoundedLinesEx(in, 0.4f, 10, 2, col::violet);
        std::string shown = input_.empty() ? "type here, ENTER to send" : input_;
        ui::text(shown, in.x + 14, in.y + in.height / 2, 19, input_.empty() ? col::muted : col::text);
        if (!input_.empty() && std::fmod(t_, 1.0f) < 0.5f)
            DrawRectangle((int)(in.x + 16 + ui::measure(input_, 19)), (int)in.y + 10, 2, 24, col::text);
    }

    void draw() override {
        auto& n = P().pieter;
        drawRoom();
        drawPieter();
        for (auto& f : flying_) {
            Vector2 to{kPieter.x, kPieter.y + 40};
            Vector2 p = Vector2Lerp(f.from, to, easeInCubic(f.t));
            p.y -= std::sin(f.t * PI) * 160;
            art::itemIcon(f.item, p, 56 * (1 - f.t * 0.5f));
        }
        fx_.draw();
        drawThought();

        ui::titleBar("PIETER ROOM", col::pink);
        if (ui::backButton()) E().go(makeMenu);
        ui::coinCounter(VW - 20, 38, P().coins);
        drawStats();

        // pet by clicking him
        Rectangle headBox{kPieter.x - kHeadR, kPieter.y - kHeadR, kHeadR * 2, kHeadR * 2};
        if (mode_ != Mode::Shower && ui::hovered(headBox) && E().mousePressed() && petCd_ <= 0) {
            E().consumeClick();
            petPieter();
        }
        if (mode_ == Mode::Idle && !n.sleeping)
            ui::text("click Pieter to pet him", kPieter.x, kPieter.y + 268, 16, alpha(col::text, 0.5f), ui::Center);

        // action bar
        float bw = 150, gap = 14, total = 5 * bw + 4 * gap, x = VW / 2.0f - total / 2, y = VH - 82;
        auto modeBtn = [&](int i, const char* label, Color c, bool active) {
            Rectangle r{x + i * (bw + gap), y, bw, 62};
            if (active) DrawRectangleRounded(shrink(r, -6), 0.3f, 10, alpha(col::gold, 0.7f));
            return ui::button(r, label, c, 22, !n.sleeping || std::string(label) == "WAKE UP");
        };
        if (modeBtn(0, "FEED", col::orange, mode_ == Mode::Feed)) mode_ = mode_ == Mode::Feed ? Mode::Idle : Mode::Feed;
        if (modeBtn(1, "PLAY", col::pink, playT_ > 0)) {
            if (n.energy < 12) { think("Too tired to play..."); E().sfx.play(Sfx::Error); }
            else if (playT_ <= 0) {
                playT_ = 4;
                n.fun += 25;
                n.energy -= 12;
                n.hunger -= 5;
                clampNeeds();
                addXp(15);
                think("LET'S GOOO");
                E().sfx.play(Sfx::Power);
            }
        }
        if (modeBtn(2, "SHOWER", col::cyan, mode_ == Mode::Shower)) {
            mode_ = mode_ == Mode::Shower ? Mode::Idle : Mode::Shower;
            cleanAnnounced_ = n.hygiene >= 100;
            if (mode_ == Mode::Shower) think("Scrub me! (drag over me)");
            lastMouse_ = E().mouse();
        }
        if (modeBtn(3, n.sleeping ? "WAKE UP" : "SLEEP", col::purple, n.sleeping)) {
            n.sleeping = !n.sleeping;
            mode_ = Mode::Idle;
            think(n.sleeping ? "Good night Pieter..." : "Huh?! I'm awake!");
            E().sfx.play(n.sleeping ? Sfx::Whoosh : Sfx::Bell, 0.6f);
        }
        if (modeBtn(4, "TALK", col::green, mode_ == Mode::Chat)) mode_ = mode_ == Mode::Chat ? Mode::Idle : Mode::Chat;

        if (mode_ == Mode::Feed && !n.sleeping) drawFeedTray();
        if (mode_ == Mode::Chat) drawChat();
    }

private:
    float t_ = 0, talkCd_ = 0, petCd_ = 0, thoughtT_ = 0, replyT_ = 0, playT_ = 0, zzzT_ = 0, saveT_ = 0, squish_ = 0, scrub_ = 0;
    std::string thought_, pendingReply_, input_;
    bool cleanAnnounced_ = false;
    Vector2 lastMouse_{};
    Mode mode_ = Mode::Idle;
    std::vector<ChatLine> chat_;
    std::vector<Flying> flying_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makePieterRoom() { return std::make_unique<PieterRoomScene>(); }

}  // namespace pg
