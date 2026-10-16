// PIETER ROOM: take care of Pieter. Feed him, play, shower, sleep, and talk.
// Needs decay in real time (also while the game is closed, capped).
// The room itself lives in game/RoomArt; this file is Pieter, his moods and the UI.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/RoomArt.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <ctime>

#include <rlgl.h>

namespace pg {
namespace {

// ------------------------------------------------------------------ Pieter rig
// Screen anchors
constexpr Vector2 kSeat{640, 585};    // top of the chair cushion
constexpr Vector2 kNeck{640, 424};    // where the head pivots
constexpr float kHeadScale = 320.0f / 512;
// Positions on the head sprite (assets/images/pieter_head.png, 512x512)
constexpr Vector2 kSprNeck{256, 470};
constexpr Vector2 kSprHead{249, 236};  // matches the round portrait, so hats line up
constexpr float kSprHeadR = 188;
constexpr Vector2 kEyeL{246, 281}, kEyeR{364, 295};
constexpr Vector2 kEyeLR{27, 22}, kEyeRR{24, 21};
constexpr Vector2 kMouth{278, 390};
constexpr Vector2 kNose{318, 360};
constexpr Vector2 kCheekL{200, 345}, kCheekR{372, 352};
const Color kSkin = rgba(0xeb975fff);
const Color kShirt = rgba(0x1f1c26ff);

enum class Mode { Idle, Feed, Shower, Chat };

struct ChatLine {
    bool fromPieter;
    std::string text;
    float t = 0;  // age, for the pop-in
};

struct Flying {
    std::string item;
    Vector2 from;
    float t = 0;
};

struct Drop {
    Vector2 p, v;
};

struct Foam {
    Vector2 sp;  // sprite coords, so it sticks to his head
    float r, life;
};

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const std::string& pick(const std::vector<std::string>& v) { return v[rndi(0, (int)v.size() - 1)]; }

float localHour() {
    if (const char* h = std::getenv("PG_HOUR")) return (float)std::atof(h);  // dev hook: preview other times of day
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    return lt->tm_hour + lt->tm_min / 60.0f + lt->tm_sec / 3600.0f;
}

// ------------------------------------------------------------------ icons
// Simple white glyphs for the needs, dock and buttons.
void iconFood(Vector2 c, float s, Color k) {
    DrawCircleV({c.x - s * 0.12f, c.y - s * 0.1f}, s * 0.3f, k);
    DrawLineEx({c.x, c.y}, {c.x + s * 0.3f, c.y + s * 0.3f}, s * 0.12f, k);
    DrawCircleV({c.x + s * 0.33f, c.y + s * 0.26f}, s * 0.09f, k);
    DrawCircleV({c.x + s * 0.26f, c.y + s * 0.34f}, s * 0.09f, k);
}
void iconGame(Vector2 c, float s, Color k, Color bg) {
    DrawRectangleRounded({c.x - s * 0.42f, c.y - s * 0.2f, s * 0.84f, s * 0.42f}, 0.8f, 8, k);
    DrawRectangleRec({c.x - s * 0.3f, c.y - s * 0.025f, s * 0.2f, s * 0.05f}, bg);
    DrawRectangleRec({c.x - s * 0.225f, c.y - s * 0.1f, s * 0.05f, s * 0.2f}, bg);
    DrawCircleV({c.x + s * 0.18f, c.y - s * 0.05f}, s * 0.05f, bg);
    DrawCircleV({c.x + s * 0.28f, c.y + s * 0.05f}, s * 0.05f, bg);
}
void iconBolt(Vector2 c, float s, Color k) {
    Vector2 a{c.x + s * 0.08f, c.y - s * 0.42f}, b{c.x - s * 0.25f, c.y + s * 0.06f}, m{c.x + s * 0.02f, c.y + s * 0.02f};
    Vector2 d{c.x - s * 0.08f, c.y + s * 0.42f}, e{c.x + s * 0.25f, c.y - s * 0.06f}, n{c.x - s * 0.02f, c.y - s * 0.02f};
    for (auto [p, q, r] : {std::tuple{a, b, m}, {a, m, b}, {d, e, n}, {d, n, e}, {a, b, e}, {a, e, b}, {d, b, e}, {d, e, b}}) DrawTriangle(p, q, r, k);
}
void iconDrop(Vector2 c, float s, Color k) {
    DrawCircleV({c.x, c.y + s * 0.1f}, s * 0.26f, k);
    DrawTriangle({c.x, c.y - s * 0.4f}, {c.x - s * 0.245f, c.y + s * 0.02f}, {c.x + s * 0.245f, c.y + s * 0.02f}, k);
    DrawTriangle({c.x, c.y - s * 0.4f}, {c.x + s * 0.245f, c.y + s * 0.02f}, {c.x - s * 0.245f, c.y + s * 0.02f}, k);
}
void iconMoon(Vector2 c, float s, Color k, Color bg) {
    DrawCircleV(c, s * 0.34f, k);
    DrawCircleV({c.x + s * 0.16f, c.y - s * 0.12f}, s * 0.28f, bg);
}
void iconSun(Vector2 c, float s, Color k) {
    DrawCircleV(c, s * 0.2f, k);
    for (int i = 0; i < 8; i++) {
        Vector2 d = fromAngle(i * PI / 4);
        DrawLineEx(c + d * (s * 0.28f), c + d * (s * 0.42f), s * 0.08f, k);
    }
}
void iconChat(Vector2 c, float s, Color k, Color bg) {
    DrawRectangleRounded({c.x - s * 0.4f, c.y - s * 0.32f, s * 0.8f, s * 0.52f}, 0.5f, 8, k);
    DrawTriangle({c.x - s * 0.2f, c.y + s * 0.15f}, {c.x - s * 0.3f, c.y + s * 0.4f}, {c.x, c.y + s * 0.15f}, k);
    DrawTriangle({c.x - s * 0.2f, c.y + s * 0.15f}, {c.x, c.y + s * 0.15f}, {c.x - s * 0.3f, c.y + s * 0.4f}, k);
    for (int i = -1; i <= 1; i++) DrawCircleV({c.x + i * s * 0.18f, c.y - s * 0.06f}, s * 0.055f, bg);
}
void iconShower(Vector2 c, float s, Color k) {
    DrawCircleSector({c.x, c.y - s * 0.05f}, s * 0.32f, 180, 360, 12, k);
    DrawLineEx({c.x, c.y - s * 0.4f}, {c.x, c.y - s * 0.3f}, s * 0.1f, k);
    for (int i = -2; i <= 2; i++) DrawLineEx({c.x + i * s * 0.12f, c.y + s * 0.08f}, {c.x + i * s * 0.16f, c.y + s * 0.38f}, s * 0.06f, k);
}

// ====================================================================== scene

class PieterRoomScene : public Scene {
public:
    ~PieterRoomScene() override {
        if (lm_.id) UnloadRenderTexture(lm_);
    }

    Presence presence() const override {
        auto& n = P().pieter;
        float m = n.mood();
        const char* mood = m > 85 ? "ecstatic" : m > 65 ? "happy" : m > 45 ? "okay" : m > 25 ? "sad" : "miserable";
        return {"Taking care of Pieter",
                n.sleeping ? "Pieter is sleeping (lvl " + std::to_string(n.level) + ")"
                           : "Pieter is " + std::string(mood) + " (lvl " + std::to_string(n.level) + ")"};
    }

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
        chat_.push_back({true, "Hoi! Ik ben Pieter. Type something!", 1});
        thoughtT_ = -4.5f;  // first thought shortly after entering
        env_.lights = n.sleeping ? 0.0f : 1.0f;
        lm_ = LoadRenderTexture(VW / 2, VH / 2);
        SetTextureFilter(lm_.texture, TEXTURE_FILTER_BILINEAR);
        blinkT_ = rnd(1, 3);
        snoreT_ = 1;
        // dev hook for screenshots: PG_ROOM=sleep|shower|feed|chat|play
        if (const char* m = std::getenv("PG_ROOM")) {
            std::string s = m;
            if (s == "sleep") { n.sleeping = true; env_.lights = 0; }
            if (s == "shower") { mode_ = Mode::Shower; n.hygiene = 20; }
            if (s == "feed") { mode_ = Mode::Feed; P().give("boba", 3); P().give("frikandel", 2); }
            if (s == "chat") {
                mode_ = Mode::Chat;
                chat_.push_back({false, "how are you doing?", 1});
                chat_.push_back({true, "Amazing! Feeling like a pro gamer (unlike Davi)", 1});
                input_ = "do you like horses";
            }
            if (s == "play") { playT_ = 30; n.energy = 90; }
            if (s == "sad") { n.hunger = 10; n.fun = 12; n.hygiene = 8; n.energy = 15; }
        }
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
            fx_.confetti({kNeck.x, kNeck.y - 140}, 140);
            shocks_.add({kNeck.x, kNeck.y - 140}, 40, 420, col::gold, 0.7f, 14);
            levelFlash_ = 1;
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
        replyT_ = rnd(0.7f, 1.4f);
        input_.clear();
        E().sfx.play(Sfx::Send, 0.6f);
        if (talkCd_ <= 0) {
            P().pieter.fun += 3;
            addXp(3);
            talkCd_ = 5;
        }
        clampNeeds();
    }

    // ============================================================ head rig helpers
    float headRot() const { return tilt_; }
    Vector2 neckPos() const { return {kNeck.x + sway_, kNeck.y + bob_}; }
    // sprite coordinates -> screen
    Vector2 toScreen(Vector2 sp) const {
        Vector2 d{(sp.x - kSprNeck.x) * kHeadScale * sqx_, (sp.y - kSprNeck.y) * kHeadScale * sqy_};
        return neckPos() + Vector2Rotate(d, headRot() * DEG2RAD);
    }
    Vector2 toSprite(Vector2 p) const {
        Vector2 d = Vector2Rotate(p - neckPos(), -headRot() * DEG2RAD);
        return {d.x / (kHeadScale * sqx_) + kSprNeck.x, d.y / (kHeadScale * sqy_) + kSprNeck.y};
    }
    Vector2 headCenter() const { return toScreen(kSprHead); }
    bool overHead(Vector2 p) const { return Vector2Distance(toSprite(p), {250, 280}) < 200; }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        auto& n = P().pieter;
        fx_.update(dt);
        shocks_.update(dt);
        talkCd_ -= dt;
        petCd_ -= dt;
        thoughtT_ -= dt;
        playT_ = std::max(0.0f, playT_ - dt);
        happyT_ = std::max(0.0f, happyT_ - dt);
        blushT_ = std::max(0.0f, blushT_ - dt);
        chewT_ = std::max(0.0f, chewT_ - dt);
        mouthTalkT_ = std::max(0.0f, mouthTalkT_ - dt);
        levelFlash_ = std::max(0.0f, levelFlash_ - dt * 1.5f);
        tickleT_ = std::max(0.0f, tickleT_ - dt);
        for (auto& c : chat_) c.t = std::min(1.0f, c.t + dt * 5);

        // live decay
        n.hunger -= dt * 0.05f;
        n.fun -= dt * 0.04f;
        n.hygiene -= dt * 0.025f;
        if (n.sleeping) {
            n.energy += dt * 1.6f;
            if (n.energy >= 100) wake("Good morning! Fully charged.");
            zzzT_ -= dt;
            if (zzzT_ <= 0) {
                zzzT_ = 0.9f;
                Vector2 h = toScreen({380, 170});
                fx_.floatText(rndi(0, 2) ? "z" : "Z", {h.x + rnd(-6, 6), h.y}, rgba(0xb8c4ffff), rnd(24, 36));
            }
            snoreT_ -= dt;
            if (snoreT_ <= 0) {
                snoreT_ = 2.6f;
                E().sfx.play(Sfx::Snore, 0.35f, rnd(0.9f, 1.05f));
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
                E().sfx.play(Sfx::Msg, 0.55f);
                mouthTalkT_ = std::min(2.0f, 0.4f + pendingReply_.size() * 0.03f);
                think(pendingReply_);
            }
        }

        if (playT_ > 0 && std::fmod(playT_, 0.5f) < dt) {
            static const char* gg[] = {"GG!", "EZ", "CLUTCH", "NO WAY", "JUHUH!", "+1 WIN"};
            fx_.floatText(gg[rndi(0, 5)], {kNeck.x + rnd(-200, 200), kNeck.y - rnd(120, 250)}, ColorFromHSV(rnd(0, 360), 0.6f, 1), 26);
            E().sfx.play(Sfx::Key, 0.5f, rnd(0.8f, 1.3f));
        }

        for (auto& f : flying_) f.t += dt * 2.0f;
        for (auto& f : flying_)
            if (f.t >= 1) eat(f.item);
        std::erase_if(flying_, [](auto& f) { return f.t >= 1; });

        if (mode_ == Mode::Chat) {
            int c;
            while ((c = GetCharPressed()) != 0)
                if (c >= 32 && c < 127 && input_.size() < 56) {
                    input_ += (char)c;
                    E().sfx.play(Sfx::Key, 0.35f);
                }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !input_.empty()) {
                input_.pop_back();
                E().sfx.play(Sfx::Key, 0.25f, 0.8f);
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) sendChat();
        }

        updateShower(dt);
        updateMoodFx(dt);
        animate(dt);

        // lights fade when he goes to bed
        env_.t = t_;
        env_.hour = localHour();
        env_.lights = damp(env_.lights, n.sleeping ? 0.0f : 1.0f, 2.5f, dt);
        env_.gaming = damp(env_.gaming, playT_ > 0 ? 1.0f : 0.0f, 4, dt);
        env_.steam = damp(env_.steam, mode_ == Mode::Shower ? 1.0f : 0.0f, 0.8f, dt);
        renderLightMap();

        if (E().backPressed()) {
            if (mode_ != Mode::Idle) mode_ = Mode::Idle;
            else E().go(makeMenu);
        }
    }

    void animate(float dt) {
        auto& n = P().pieter;
        bool asleep = n.sleeping;
        float breathe = std::sin(t_ * (asleep ? 1.3f : 2.2f));
        float bounce = playT_ > 0 ? -std::abs(std::sin(t_ * 9)) * 14 : 0;
        bob_ = breathe * (asleep ? 3.0f : 2.0f) + bounce + (asleep ? 10 : 0);
        sway_ = playT_ > 0 ? std::sin(t_ * 4.5f) * 8 : 0;
        float tiltTarget = std::sin(t_ * 1.1f) * 2.5f + (n.mood() < 30 ? 6 : 0) + (asleep ? 13 : 0) + (playT_ > 0 ? std::sin(t_ * 4.5f) * 6 : 0);
        if (tickleT_ > 0) tiltTarget += std::sin(t_ * 30) * 4;
        tilt_ = damp(tilt_, tiltTarget, 6, dt);
        squish_ = damp(squish_, 0, 9, dt);
        sqx_ = 1 + squish_ * 0.07f;
        sqy_ = 1 - squish_ * 0.07f + breathe * 0.004f;
        armWave_ = playT_ > 0 ? std::sin(t_ * 14) : 0;

        // blinking (sometimes a double blink)
        blinkT_ -= dt;
        if (blinkT_ <= -0.13f) blinkT_ = rndi(0, 5) == 0 ? 0.18f : rnd(2.0f, 5.0f);

        // where the eyes look
        Vector2 m = E().mouse();
        if (Vector2Distance(m, lastMouseLook_) > 2) { mouseIdle_ = 0; lastMouseLook_ = m; }
        else mouseIdle_ += dt;
        glanceT_ -= dt;
        if (glanceT_ <= 0) {
            glanceT_ = rnd(1.8f, 4.0f);
            Vector2 spots[] = {{975, 185}, {1030, 350}, {640, 420}, {445, 140}, {320, 470}, {640, 600}};
            glance_ = spots[rndi(0, 5)];
        }
        Vector2 target = glance_;
        if (mouseIdle_ < 2.5f) target = m;
        if (playT_ > 0) target = {1030, 350};
        if (!flying_.empty()) {
            auto& f = flying_.front();
            target = Vector2Lerp(f.from, toScreen(kMouth), easeInCubic(f.t));
        }
        look_ = damp(look_, target, 12, dt);
    }

    void wake(const std::string& line) {
        auto& n = P().pieter;
        n.sleeping = false;
        think(line);
        E().sfx.play(Sfx::Yawn, 0.6f, 1.1f);
        E().sfx.play(Sfx::Bell, 0.35f);
        squish_ = 1;
    }

    void updateShower(float dt) {
        auto& n = P().pieter;
        // water
        if (mode_ == Mode::Shower) {
            showerSfxT_ -= dt;
            if (showerSfxT_ <= 0) {
                showerSfxT_ = 1.15f;
                E().sfx.play(Sfx::Water, 0.4f, rnd(0.95f, 1.05f));
            }
            for (int i = 0; i < 7; i++)
                drops_.push_back({{kNeck.x + rnd(-70, 70), 104 + rnd(0, 8)}, {rnd(-50, 50), rnd(520, 640)}});
            n.hygiene += dt * 0.6f;  // rinsing helps a little on its own
        }
        for (auto& d : drops_) {
            d.v.y += 1100 * dt;
            d.p += d.v * dt;
        }
        // drops hit his head or the floor and splash
        std::erase_if(drops_, [&](Drop& d) {
            bool hitHead = d.p.y > 180 && overHead(d.p) && rndi(0, 3) == 0;
            bool hitFloor = d.p.y > 640 || (std::abs(d.p.x - kNeck.x) < 130 && d.p.y > kNeck.y + 20 && rndi(0, 6) == 0);
            if (hitHead || hitFloor) {
                if (rndi(0, 2) == 0) {
                    Particle p;
                    p.pos = d.p;
                    p.vel = {rnd(-120, 120), rnd(-180, -60)};
                    p.gravity = 900;
                    p.life = p.maxLife = rnd(0.2f, 0.4f);
                    p.size = rnd(2, 3.5f);
                    p.endSize = 1;
                    p.color = alpha(rgba(0xbfeaffff), 0.9f);
                    fx_.add(p);
                }
                return true;
            }
            return false;
        });

        // scrubbing makes foam where the cursor is
        if (mode_ == Mode::Shower) {
            Vector2 m = E().mouse();
            if (E().mouseDown() && overHead(m)) {
                float moved = Vector2Distance(m, lastMouse_);
                n.hygiene += moved * 0.035f;
                scrub_ += moved;
                if (scrub_ > 18) {
                    scrub_ = 0;
                    foam_.push_back({toSprite(m) + Vector2{rnd(-12, 12), rnd(-12, 12)}, rnd(9, 20), 1});
                    if (foam_.size() > 80) foam_.erase(foam_.begin());
                    if (rndi(0, 2) == 0) E().sfx.play(Sfx::Bubble, 0.22f, rnd(0.8f, 1.5f));
                    Particle p;
                    p.pos = m + rndDir() * rnd(0, 20);
                    p.vel = {rnd(-40, 40), rnd(-90, -30)};
                    p.life = p.maxLife = rnd(0.8f, 1.5f);
                    p.size = rnd(5, 12);
                    p.endSize = p.size;
                    p.color = alpha(WHITE, 0.85f);
                    p.shape = PShape::Bubble;
                    fx_.add(p);
                }
                rinseWait_ = 1.2f;
                if (n.hygiene >= 100 && !cleanAnnounced_) {
                    cleanAnnounced_ = true;
                    think("Squeaky clean!");
                    addXp(10);
                    E().sfx.play(Sfx::Perfect);
                    fx_.burst(headCenter(), 24, WHITE, 300, 6, 0.8f, PShape::Star);
                }
            }
            lastMouse_ = m;
            clampNeeds();
        }
        // the water rinses foam off once he stops scrubbing
        rinseWait_ -= dt;
        float rinse = mode_ == Mode::Shower ? (rinseWait_ <= 0 ? 0.6f : 0.0f) : 0.25f;
        for (auto& f : foam_) {
            f.life -= dt * rinse * rnd(0.5f, 1.5f);
            if (f.life <= 0 && rndi(0, 4) == 0) E().sfx.play(Sfx::Pop, 0.15f, rnd(0.9f, 1.6f));
        }
        std::erase_if(foam_, [](auto& f) { return f.life <= 0; });
    }

    void updateMoodFx(float dt) {
        auto& n = P().pieter;
        if (n.sleeping) return;
        // flies when he stinks
        if (n.hygiene < 25) {
            buzzT_ -= dt;
            if (buzzT_ <= 0) {
                buzzT_ = rnd(3, 6);
                E().sfx.playAt(Sfx::Buzz, flyPos(0).x, 0.4f);
            }
        }
        // stomach growls when starving
        if (n.hunger < 25) {
            growlT_ -= dt;
            if (growlT_ <= 0) {
                growlT_ = rnd(6, 10);
                fx_.floatText("*grumble*", {kNeck.x + 70, kNeck.y + 80}, col::orange, 20);
                E().sfx.play(Sfx::Hurt, 0.25f, 0.55f);
                squish_ = 0.6f;
            }
        }
        // sparkles when he's ecstatic
        if (n.mood() > 85) {
            sparkleT_ -= dt;
            if (sparkleT_ <= 0) {
                sparkleT_ = rnd(0.3f, 0.8f);
                Particle p;
                p.pos = headCenter() + rndDir() * rnd(110, 150);
                p.life = p.maxLife = 0.9f;
                p.size = rnd(6, 11);
                p.endSize = 0;
                p.rotVel = 3;
                p.color = col::gold;
                p.shape = PShape::Star;
                fx_.add(p);
            }
        }
    }

    Vector2 flyPos(int i) const {
        Vector2 c = headCenter();
        float a = t_ * (2.3f + i * 0.7f) + i * 2;
        return {c.x + std::sin(a) * (120 + i * 20) + std::sin(a * 3.1f) * 14, c.y - 40 + std::cos(a * 1.3f) * 70 + std::sin(a * 5) * 8};
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
        bool drink = id == "boba" || id == "gamefuel";
        E().sfx.play(drink ? Sfx::Gulp : Sfx::Chew);
        chewT_ = drink ? 0.5f : 0.9f;
        squish_ = 0.8f;
        Vector2 m = toScreen(kMouth);
        fx_.burst(m, 16, d->color, 220, 5, 0.6f, PShape::Square, 600);
        fx_.floatText("+" + std::to_string(d->hunger) + " food", {kNeck.x, kNeck.y - 250}, col::green, 24);
        static const char* yum[] = {"Lekker!", "Mmmm", "YUM", "More pls", "Delicious", "10/10"};
        think(id == "boba" ? "BOBAAAA" : yum[rndi(0, 5)]);
        if (n.hunger >= 100) think("I'm so full I could explode");
    }

    void petPieter() {
        auto& n = P().pieter;
        if (n.sleeping) {
            think("five more minutes...");
            E().sfx.play(Sfx::Snore, 0.3f, 1.2f);
            return;
        }
        petCd_ = 0.22f;
        n.fun += 2;
        n.energy -= 0.3f;
        clampNeeds();
        addXp(1);
        E().sfx.play(rndi(0, 1) ? Sfx::Boop : Sfx::Kiss, 0.55f, rnd(0.9f, 1.25f));
        fx_.burst(E().mouse(), 5, col::pink, 170, 10, 0.9f, PShape::Heart, -120);
        squish_ = 1;
        happyT_ = 0.6f;
        blushT_ = 2.0f;
        // rapid petting = tickling
        tickles_ = tickleWindow_ > 0 ? tickles_ + 1 : 1;
        tickleWindow_ = 1.4f;
        if (tickles_ >= 6) {
            tickles_ = 0;
            tickleT_ = 1.2f;
            think(pick({"HAHAHA STOP", "that tickles!!", "NEE NEE NEE haha"}));
            fx_.floatText("HAHA", {kNeck.x - 140, kNeck.y - 200}, col::gold, 30);
            fx_.floatText("HAHA", {kNeck.x + 150, kNeck.y - 170}, col::gold, 26);
        }
    }

    // ============================================================ lighting
    void renderLightMap() {
        if (!lm_.id) return;
        BeginTextureMode(lm_);
        ClearBackground(room::ambient(env_));
        rlPushMatrix();
        rlScalef(0.5f, 0.5f, 1);
        fx::beginAdd();
        room::addLights(env_);
        fx::endAdd();
        rlPopMatrix();
        EndTextureMode();
    }

    void applyLightMap() {
        if (!lm_.id) return;
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawTexturePro(lm_.texture, {0, 0, (float)lm_.texture.width, -(float)lm_.texture.height}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, WHITE);
        EndBlendMode();
    }

    // ============================================================ Pieter
    void limb(Vector2 a, Vector2 b, float w, Color c) {
        DrawLineEx(a, b, w + 6, col::outline);
        DrawCircleV(a, w / 2 + 3, col::outline);
        DrawCircleV(b, w / 2 + 3, col::outline);
        DrawLineEx(a, b, w, c);
        DrawCircleV(a, w / 2, c);
        DrawCircleV(b, w / 2, c);
    }

    void hand(Vector2 c, float r) {
        DrawCircleV(c, r + 3, col::outline);
        DrawCircleV(c, r, kSkin);
        DrawCircleV({c.x - r * 0.3f, c.y - r * 0.35f}, r * 0.35f, alpha(WHITE, 0.15f));
    }

    void drawBody() {
        Vector2 nk = neckPos();
        float bodyBob = bob_ * 0.5f;
        // legs come towards us off the seat
        for (int s : {-1, 1}) {
            Rectangle thigh{kSeat.x + s * 50 - 40, kSeat.y - 18, 80, 70};
            DrawRectangleRounded(shrink(thigh, -3), 0.5f, 10, col::outline);
            DrawRectangleRounded(thigh, 0.5f, 10, rgba(0x2c3a5cff));
            DrawRectangleRounded({thigh.x + 8, thigh.y + 6, thigh.width - 16, 16}, 0.8f, 8, alpha(WHITE, 0.08f));
            Rectangle shin{kSeat.x + s * 54 - 32, kSeat.y + 44, 64, 70};
            DrawRectangleRounded(shrink(shin, -3), 0.4f, 10, col::outline);
            DrawRectangleRounded(shin, 0.4f, 10, rgba(0x24304eff));
            DrawEllipse((int)(kSeat.x + s * 56), (int)kSeat.y + 116, 40, 16, col::outline);
            DrawEllipse((int)(kSeat.x + s * 56), (int)kSeat.y + 114, 36, 13, WHITE);
        }
        // shirt: rounded shoulders tapering to the waist
        float top = nk.y - 8 + bodyBob, waist = kSeat.y + 4;
        for (int pass = 0; pass < 2; pass++) {
            float o = pass == 0 ? 3.5f : 0;
            Color c = pass == 0 ? col::outline : kShirt;
            DrawRectangleRounded({nk.x - 96 - o, top + 16 - o, 192 + 2 * o, waist - top - 16 + 2 * o}, 0.3f, 10, c);
            DrawRectangleRounded({nk.x - 102 - o, top - o, 204 + 2 * o, 80 + 2 * o}, 0.6f, 10, c);
        }
        DrawRectangleGradientH((int)nk.x - 96, (int)top + 30, 90, (int)(waist - top - 50), alpha(BLACK, 0.2f), alpha(BLACK, 0));
        DrawRectangleRounded({nk.x - 70, top + 6, 140, 20}, 0.8f, 8, alpha(WHITE, 0.05f));
        Rectangle torso{nk.x - 96, top, 192, waist - top};
        // fold lines
        DrawSplineSegmentBezierQuadratic({torso.x + 30, torso.y + 120}, {torso.x + 55, torso.y + 135}, {torso.x + 75, torso.y + 125}, 2, alpha(BLACK, 0.4f));
        DrawSplineSegmentBezierQuadratic({torso.x + 120, torso.y + 128}, {torso.x + 140, torso.y + 140}, {torso.x + 165, torso.y + 120}, 2, alpha(BLACK, 0.4f));
        // merch logo
        Vector2 logo{nk.x, torso.y + 92};
        DrawCircleV(logo, 26, col::outline);
        DrawCircleV(logo, 23, col::pink);
        ui::text("P", logo.x, logo.y, 30, WHITE, ui::Center, true);
        // rim light from the monitor side
        fx::beginAdd();
        fx::glowLine({torso.x + torso.width - 6, torso.y + 30}, {torso.x + torso.width - 6, torso.y + torso.height - 20}, 8,
                     alpha(mix(col::violet, ColorFromHSV(std::fmod(t_ * 90, 360), 0.7f, 1), env_.gaming), 0.25f));
        fx::endAdd();
    }

    void drawArmsUpper() {
        Vector2 nk = neckPos();
        for (int s : {-1, 1}) {
            Vector2 sh{nk.x + s * 94, nk.y + 26 + bob_ * 0.5f};
            Vector2 el{kSeat.x + s * 146, kSeat.y - 98};
            Vector2 mid = Vector2Lerp(sh, el, 0.5f);
            limb(mid, el, 32, kSkin);
            limb(sh, mid, 46, kShirt);
        }
    }

    void drawArmsLower() {
        for (int s : {-1, 1}) {
            Vector2 el{kSeat.x + s * 146, kSeat.y - 98};
            Vector2 hd = playT_ > 0 ? Vector2{kSeat.x + s * 46, kSeat.y - 40 + armWave_ * s * 6} : Vector2{kSeat.x + s * 132, kSeat.y - 72};
            limb(el, hd, 30, kSkin);
            if (playT_ <= 0) hand(hd, 18);
        }
        if (playT_ > 0) {
            // controller in both hands, mashing buttons
            Vector2 c{kSeat.x, kSeat.y - 40};
            DrawRectangleRounded({c.x - 78, c.y - 24, 156, 52}, 0.8f, 12, col::outline);
            DrawRectangleRounded({c.x - 74, c.y - 21, 148, 46}, 0.8f, 12, rgba(0x5b2fb0ff));
            DrawRectangleRounded({c.x - 66, c.y - 18, 132, 12}, 0.8f, 8, alpha(WHITE, 0.15f));
            DrawCircleV({c.x - 34, c.y + 2}, 9, col::outline);
            DrawCircleV({c.x + 22, c.y - 2}, 5, col::gold);
            DrawCircleV({c.x + 34, c.y + 8}, 5, col::red);
            DrawCircleV({c.x + 46, c.y - 2}, 5, col::cyan);
            hand({c.x - 62, c.y + 6 + armWave_ * 3}, 18);
            hand({c.x + 62, c.y + 6 - armWave_ * 3}, 18);
        }
    }

    void drawHeadphones() {
        // hanging around his neck, seen from the front: the band dips behind
        // the collar and the cups face sideways, so they read as tall ovals
        Vector2 nk = neckPos();
        float y = nk.y + 16 + bob_ * 0.4f;
        DrawSplineSegmentBezierQuadratic({nk.x - 66, y - 10}, {nk.x, y - 46}, {nk.x + 66, y - 10}, 12, col::outline);
        DrawSplineSegmentBezierQuadratic({nk.x - 66, y - 10}, {nk.x, y - 46}, {nk.x + 66, y - 10}, 7, rgba(0x564c6cff));
        for (int s : {-1, 1}) {
            Vector2 c{nk.x + s * 70, y + 4};
            DrawRectangleRounded({c.x - 17, c.y - 30, 34, 60}, 0.9f, 12, col::outline);
            DrawRectangleRounded({c.x - 14, c.y - 27, 28, 54}, 0.9f, 12, rgba(0x564c6cff));
            DrawRectangleRounded({c.x - 10, c.y - 24, 12, 30}, 1, 8, alpha(WHITE, 0.12f));
            DrawRectangleRoundedLinesEx({c.x - 14, c.y - 27, 28, 54}, 0.9f, 12, 2.5f, alpha(col::pink, 0.8f));
            // ear cushion on the inner side
            DrawRectangleRounded({s < 0 ? c.x + 2 : c.x - 12, c.y - 22, 10, 44}, 1, 8, rgba(0x15121cff));
            DrawRectangleRounded({c.x - (s < 0 ? 11 : 1), c.y - 22, 10, 18}, 1, 8, alpha(WHITE, 0.1f));
            DrawCircleV({c.x - s * 5, c.y + 14}, 2.5f, alpha(ColorFromHSV(std::fmod(t_ * 60, 360), 0.8f, 1), 0.5f + 0.5f * env_.lights));
        }
    }

    void drawCollar() {
        Vector2 nk = neckPos();
        float y = nk.y + 12 + bob_ * 0.5f;
        DrawEllipse((int)nk.x, (int)y + 4, 58, 16, kShirt);
        DrawSplineSegmentBezierQuadratic({nk.x - 52, y - 4}, {nk.x, y + 22}, {nk.x + 52, y - 4}, 4, col::outline);
    }

    // eyes, mouth and cheeks are drawn in head-sprite space so they follow tilt and squish
    void drawFace(Color tint) {
        auto& n = P().pieter;
        bool asleep = n.sleeping;
        bool happy = happyT_ > 0 || tickleT_ > 0;
        bool blink = blinkT_ < 0;
        float tired = !asleep && n.energy < 25 ? 0.45f : 0.0f;

        Color lidColor = ColorTint(kSkin, tint);  // must match the tinted sprite
        auto eye = [&](Vector2 c, Vector2 r, int side) {
            if (asleep || blink || happy) {
                DrawEllipse((int)c.x, (int)c.y, r.x + 6, r.y + 6, lidColor);
                if (happy) {  // ^ ^
                    DrawSplineSegmentBezierQuadratic({c.x - r.x, c.y + 6}, {c.x, c.y - r.y - 6}, {c.x + r.x, c.y + 6}, 7, col::outline);
                } else {  // closed, lashes down
                    DrawSplineSegmentBezierQuadratic({c.x - r.x - 2, c.y}, {c.x, c.y + r.y * 0.9f}, {c.x + r.x + 2, c.y}, 6, col::outline);
                    if (asleep) DrawLineEx({c.x + side * (r.x - 4), c.y + 3}, {c.x + side * (r.x + 4), c.y + 9}, 3, col::outline);
                }
                return;
            }
            // open: white, then a pupil that looks around
            DrawEllipse((int)c.x, (int)c.y + 2, r.x - 2, r.y - 1, rgba(0xf7ecdfff));
            Vector2 sc = toScreen(c);
            Vector2 d = look_ - sc;
            float len = Vector2Length(d);
            Vector2 dir = len > 1 ? d / len : Vector2{0, 0};
            float k = std::min(1.0f, len / 260);
            Vector2 local = Vector2Rotate(dir, -headRot() * DEG2RAD);
            Vector2 pp{c.x + local.x * k * (r.x - 15), c.y + 3 + local.y * k * (r.y - 14)};
            DrawCircleV(pp, 15, rgba(0x5a3418ff));
            DrawCircleV(pp, 11, rgba(0x1c0e06ff));
            DrawCircleV({pp.x - 4, pp.y - 5}, 4.5f, WHITE);
            DrawCircleV({pp.x + 5, pp.y + 4}, 2, alpha(WHITE, 0.7f));
            // upper lid line over the white keeps the original art's eyelid
            DrawSplineSegmentBezierQuadratic({c.x - r.x - 2, c.y - 2}, {c.x, c.y - r.y - 10}, {c.x + r.x + 2, c.y - 2}, 7, col::outline);
            if (tired > 0) {  // heavy eyelids
                DrawRectangleRec({c.x - r.x - 6, c.y - r.y - 8, r.x * 2 + 12, (r.y * 2 + 8) * tired}, lidColor);
                DrawLineEx({c.x - r.x - 2, c.y - r.y - 8 + (r.y * 2 + 8) * tired}, {c.x + r.x + 2, c.y - r.y - 8 + (r.y * 2 + 8) * tired}, 5, col::outline);
            }
        };
        eye(kEyeL, kEyeLR, -1);
        eye(kEyeR, kEyeRR, 1);

        // blush
        float blush = std::max(clamp01(blushT_), n.mood() > 85 ? 0.5f : 0.0f);
        if (blush > 0)
            for (Vector2 c : {kCheekL, kCheekR}) {
                DrawEllipse((int)c.x, (int)c.y, 30, 15, alpha(rgba(0xff6f8aff), 0.35f * blush));
                for (int i = -1; i <= 1; i++) DrawLineEx({c.x + i * 12 - 4, c.y + 6}, {c.x + i * 12 + 4, c.y - 6}, 2.5f, alpha(rgba(0xd8405aff), 0.5f * blush));
            }

        // mouth: chewing, talking, eating, sleeping or a sad frown over the smile
        bool foodClose = !flying_.empty() && flying_.front().t > 0.45f;
        bool talking = mouthTalkT_ > 0 && std::fmod(t_ * 9, 1.0f) < 0.55f;
        bool chewing = chewT_ > 0;
        bool sad = !asleep && n.mood() < 30 && !happy;
        if (foodClose || talking || chewing || asleep || sad) {
            DrawEllipse((int)kMouth.x - 2, (int)kMouth.y, 74, 38, lidColor);
            if (sad) {
                DrawSplineSegmentBezierQuadratic({kMouth.x - 40, kMouth.y + 14}, {kMouth.x, kMouth.y - 14}, {kMouth.x + 40, kMouth.y + 14}, 6, col::outline);
            } else {
                float open = foodClose ? 1.0f : talking ? 0.6f : asleep ? 0.35f + 0.1f * std::sin(t_ * 1.3f) : (std::fmod(chewT_ * 7, 1.0f) < 0.5f ? 0.55f : 0.1f);
                float w = foodClose ? 30 : 26, h = 6 + open * 20;
                DrawEllipse((int)kMouth.x, (int)kMouth.y, w + 4, h + 4, col::outline);
                DrawEllipse((int)kMouth.x, (int)kMouth.y, w, h, rgba(0x5a1a22ff));
                if (open > 0.3f) {
                    DrawEllipse((int)kMouth.x, (int)(kMouth.y + h * 0.45f), w * 0.6f, h * 0.45f, rgba(0xff7a8aff));
                    DrawRectangleRec({kMouth.x - w * 0.55f, kMouth.y - h + 2, w * 1.1f, std::min(7.0f, h * 0.35f)}, WHITE);
                }
            }
        }

        // snot bubble while asleep
        if (asleep) {
            float k = 0.5f + 0.5f * std::sin(t_ * 1.3f);
            Vector2 b{kNose.x + 14, kNose.y + 10 + k * 10};
            float r = 6 + k * 22;
            DrawCircleV(b, r, alpha(rgba(0xbfeaffff), 0.35f));
            DrawRing(b, r - 2, r, 0, 360, 24, alpha(rgba(0x8fd0ffff), 0.8f));
            DrawCircleV({b.x - r * 0.35f, b.y - r * 0.35f}, r * 0.2f, alpha(WHITE, 0.8f));
        }
    }

    void drawPieter() {
        auto& n = P().pieter;
        room::drawChairBack(kSeat, env_);
        drawBody();
        drawArmsUpper();
        room::drawChairFront(kSeat, env_);
        drawArmsLower();

        // head: sprite + face overlays in sprite space
        Texture2D& head = E().tex("pieter_head");
        Color tint = n.hygiene < 25 ? mix(WHITE, rgba(0xb8e0a0ff), 0.35f) : WHITE;
        Vector2 nk = neckPos();
        drawHeadphones();
        // soft shadow of the head on the shirt
        fx::softShadow({nk.x + 6, nk.y + 30}, 110, 26, 0.45f);
        rlPushMatrix();
        rlTranslatef(nk.x, nk.y, 0);
        rlRotatef(headRot(), 0, 0, 1);
        rlScalef(kHeadScale * sqx_, kHeadScale * sqy_, 1);
        rlTranslatef(-kSprNeck.x, -kSprNeck.y, 0);
        DrawTexturePro(head, {0, 0, (float)head.width, (float)head.height}, {0, 0, 512, 512}, {0, 0}, 0, tint);
        drawFace(tint);
        // foam from scrubbing
        for (auto& f : foam_) {
            float r = f.r * std::min(1.0f, f.life * 2.5f);
            DrawCircleV(f.sp, r + 2, alpha(rgba(0xc8e8ffff), 0.8f));
            DrawCircleV(f.sp, r, WHITE);
            DrawCircleV({f.sp.x - r * 0.3f, f.sp.y - r * 0.3f}, r * 0.25f, alpha(rgba(0xd8f0ffff), 1));
        }
        // equipped hat
        art::hat(P().hat, kSprHead, kSprHeadR, 0);
        rlPopMatrix();
        drawCollar();

        // wet sheen while showering
        if (env_.steam > 0.05f) {
            fx::beginAdd();
            fx::glow(toScreen({220, 150}), 60, alpha(WHITE, 0.25f * env_.steam));
            fx::endAdd();
        }
        // stink lines + flies
        if (n.hygiene < 25 && !n.sleeping) {
            Vector2 h = headCenter();
            for (int i = 0; i < 3; i++) {
                float x = h.x - 70 + i * 70, y = h.y - 130 - std::fmod(t_ * 30 + i * 20, 40);
                DrawSplineSegmentBezierQuadratic({x, y + 30}, {x + 16, y + 15}, {x, y}, 4, alpha(col::green, 0.6f));
            }
            for (int i = 0; i < 2; i++) {
                Vector2 f = flyPos(i);
                DrawEllipse((int)f.x - 3, (int)f.y - 5, 5, 3, alpha(WHITE, 0.5f + 0.4f * std::sin(t_ * 80)));
                DrawEllipse((int)f.x + 3, (int)f.y - 5, 5, 3, alpha(WHITE, 0.5f + 0.4f * std::cos(t_ * 80)));
                DrawCircleV(f, 4, rgba(0x15121cff));
            }
        }
        // rain cloud when he's sad
        if (n.mood() < 30 && !n.sleeping) {
            Vector2 c = headCenter() + Vector2{0, -170};
            for (int i = 0; i < 6; i++) {
                float x = c.x - 50 + std::fmod(i * 23.0f, 100), y = c.y + 20 + std::fmod(t_ * 260 + i * 41, 70);
                DrawLineEx({x, y}, {x - 2, y + 9}, 2.5f, alpha(col::cyan, 0.8f));
            }
            for (auto [dx, dy, r] : {std::tuple{-34.0f, 4.0f, 22.0f}, {0.0f, -8.0f, 30.0f}, {34.0f, 4.0f, 22.0f}, {0.0f, 12.0f, 24.0f}}) DrawCircleV(c + Vector2{dx, dy}, r + 3, col::outline);
            for (auto [dx, dy, r] : {std::tuple{-34.0f, 4.0f, 22.0f}, {0.0f, -8.0f, 30.0f}, {34.0f, 4.0f, 22.0f}, {0.0f, 12.0f, 24.0f}}) DrawCircleV(c + Vector2{dx, dy}, r, rgba(0x6a6a88ff));
        }
    }

    void drawShower() {
        if (mode_ != Mode::Shower && drops_.empty()) return;
        // pipe + head
        if (mode_ == Mode::Shower) {
            DrawRectangleRec({kNeck.x - 5, 0, 10, 80}, rgba(0xa8b0c4ff));
            DrawEllipse((int)kNeck.x, 94, 62, 18, col::outline);
            DrawEllipse((int)kNeck.x, 92, 58, 15, rgba(0xc8d0e0ff));
            DrawEllipse((int)kNeck.x, 98, 50, 8, rgba(0x8890a4ff));
            for (int i = -4; i <= 4; i++) DrawCircleV({kNeck.x + i * 10.0f, 99}, 2, rgba(0x404858ff));
        }
        for (auto& d : drops_) DrawLineEx(d.p, d.p - d.v * 0.025f, 2, alpha(rgba(0xbfeaffff), 0.6f));
    }

    void drawSteam() {
        if (env_.steam < 0.02f) return;
        fx::beginAdd();
        for (int i = 0; i < 14; i++) {
            float u = std::fmod(i * 0.137f + t_ * 0.05f * (1 + (i % 3) * 0.3f), 1.0f);
            Vector2 p{kNeck.x + std::sin(i * 2.3f + t_ * 0.4f) * 260, 640 - u * 600};
            fx::glow(p, 120 + (i % 4) * 30, alpha(WHITE, 0.07f * env_.steam * std::sin(u * PI)));
        }
        fx::endAdd();
        DrawRectangle(0, 0, VW, VH, alpha(rgba(0xd8e8ffff), 0.08f * env_.steam));
    }

    // ============================================================ UI
    void drawThought() {
        if (thoughtT_ <= 0 || thought_.empty()) return;
        float age = 3.5f - thoughtT_;
        float a = clamp01(thoughtT_ / 0.35f);
        float pop = easeOutBack(clamp01(age / 0.3f));
        size_t shown = std::min(thought_.size(), (size_t)(age * 50) + 1);
        std::string txt = thought_.substr(0, shown);
        float w = std::min(360.0f, ui::measure(thought_, 21) + 44);
        float h = ui::paragraph(thought_, {0, 0, w - 40, 0}, 21, WHITE, false) + 28;
        Vector2 anchor = headCenter() + Vector2{120, -120};
        Rectangle box{anchor.x, anchor.y - h, w, h};
        box.x = std::min(box.x, (float)VW - w - 16);
        Vector2 c = center(box);
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        rlScalef(pop, pop, 1);
        rlTranslatef(-c.x, -c.y, 0);
        Color fill = alpha(WHITE, a), line = alpha(col::outline, a);
        // cloud: rounded box with bumps, outline pass then fill pass
        for (int pass = 0; pass < 2; pass++) {
            float o = pass == 0 ? 3.5f : 0;
            Color cc = pass == 0 ? line : fill;
            DrawRectangleRounded(shrink(box, -o), 0.5f, 12, cc);
            for (float x = box.x + 22; x < box.x + box.width - 16; x += 30) {
                DrawCircleV({x, box.y + 2}, 15 + o, cc);
                DrawCircleV({x + 14, box.y + box.height - 2}, 14 + o, cc);
            }
            DrawCircleV({box.x - 26 + 30, box.y + box.height + 20}, 10 + o, cc);
            DrawCircleV({box.x - 46 + 30, box.y + box.height + 40}, 6 + o, cc);
        }
        ui::paragraph(txt, {box.x + 20, box.y + 14, w - 40, 0}, 21, alpha(col::outline, a));
        rlPopMatrix();
    }

    void needIcon(int i, Vector2 c, float s, Color bg) {
        switch (i) {
            case 0: iconFood(c, s, WHITE); break;
            case 1: iconGame(c, s, WHITE, bg); break;
            case 2: iconBolt(c, s, WHITE); break;
            default: iconDrop(c, s, WHITE); break;
        }
    }

    void drawStats() {
        auto& n = P().pieter;
        Rectangle r{18, 84, 272, 318};
        ui::panel(r, alpha(rgba(0x160e2cff), 0.88f), 0.08f, alpha(col::violet, 0.5f));
        // header: portrait with an XP ring, level and mood
        Vector2 pc{r.x + 44, r.y + 46};
        float xp = (float)n.xp / PieterNeeds::xpForLevel(n.level);
        DrawRing(pc, 27, 32, 0, 360, 40, col::bgDeep);
        DrawRing(pc, 27, 32, -90, -90 + 360 * xp, 40, col::violet);
        art::portrait("pieter", pc, 25, BLANK);
        if (levelFlash_ > 0) {
            fx::beginAdd();
            fx::glow(pc, 70, alpha(col::gold, levelFlash_));
            fx::endAdd();
        }
        ui::text("PIETER", r.x + 86, r.y + 32, 22, col::text, ui::Left, true);
        Rectangle lv{r.x + 86, r.y + 50, 66, 22};
        DrawRectangleRounded(lv, 1, 8, col::gold);
        ui::text("LV " + std::to_string(n.level), lv.x + lv.width / 2, lv.y + 11, 15, rgba(0x4a2a00ff), ui::Center, true);
        float m = n.mood();
        const char* mood = n.sleeping ? "ASLEEP" : m > 85 ? "ECSTATIC" : m > 65 ? "HAPPY" : m > 45 ? "OKAY" : m > 25 ? "SAD" : "MISERABLE";
        Color mc = n.sleeping ? rgba(0xb8c4ffff) : m > 65 ? col::green : m > 45 ? col::gold : col::red;
        ui::text(mood, r.x + r.width - 16, r.y + 61, 15, mc, ui::Right, true);
        // little mood face
        Vector2 fc{r.x + r.width - 30, r.y + 32};
        DrawCircleV(fc, 13, mc);
        DrawCircleV({fc.x - 4.5f, fc.y - 3}, 2, col::outline);
        DrawCircleV({fc.x + 4.5f, fc.y - 3}, 2, col::outline);
        float curve = n.sleeping ? 0 : (m - 50) / 50 * 6;
        DrawSplineSegmentBezierQuadratic({fc.x - 6, fc.y + 4}, {fc.x, fc.y + 4 + curve}, {fc.x + 6, fc.y + 4}, 2, col::outline);
        DrawLineEx({r.x + 16, r.y + 86}, {r.x + r.width - 16, r.y + 86}, 1, alpha(col::violet, 0.3f));

        struct S { const char* name; float v; Color c; } ss[] = {
            {"HUNGER", n.hunger, col::orange}, {"FUN", n.fun, col::pink}, {"ENERGY", n.energy, col::gold}, {"HYGIENE", n.hygiene, col::cyan}};
        for (int i = 0; i < 4; i++) {
            float y = r.y + 104 + i * 53;
            bool low = ss[i].v < 25;
            float pulse = low ? 0.5f + 0.5f * std::sin(t_ * 8) : 0;
            Color c = low ? mix(ss[i].c, col::red, pulse) : ss[i].c;
            Vector2 ic{r.x + 34, y + 16};
            float shake = low ? std::sin(t_ * 40) * 1.5f : 0;
            DrawCircleV({ic.x, ic.y + 2}, 19, darken(c, 0.5f));
            DrawCircleV({ic.x + shake, ic.y}, 18, c);
            needIcon(i, {ic.x + shake, ic.y}, 26, c);
            ui::text(ss[i].name, r.x + 62, y + 4, 14, col::muted, ui::Left, true);
            ui::text(std::to_string((int)ss[i].v) + "%", r.x + r.width - 16, y + 4, 14, low ? col::red : col::text, ui::Right, true);
            Rectangle bar{r.x + 62, y + 16, r.width - 78, 16};
            DrawRectangleRounded(bar, 1, 8, col::bgDeep);
            float f = clamp01(ss[i].v / 100);
            if (f > 0.02f) {
                Rectangle fill{bar.x, bar.y, bar.width * f, bar.height};
                DrawRectangleRounded(fill, 1, 8, c);
                DrawRectangleRounded({fill.x + 3, fill.y + 2, std::max(0.0f, fill.width - 6), 5}, 1, 8, alpha(WHITE, 0.3f));
                // moving shimmer
                E().beginClip(fill);
                float sx = fill.x + std::fmod(t_ * 120 + i * 50, fill.width + 160) - 80;
                DrawRectangleGradientH((int)sx, (int)fill.y, 40, (int)fill.height, alpha(WHITE, 0), alpha(WHITE, 0.25f));
                DrawRectangleGradientH((int)sx + 40, (int)fill.y, 40, (int)fill.height, alpha(WHITE, 0.25f), alpha(WHITE, 0));
                E().endClip();
            }
            DrawRectangleRoundedLinesEx(bar, 1, 8, 1.5f, alpha(BLACK, 0.5f));
        }
    }

    // one dock button; returns true on click
    bool dockButton(Rectangle r, const char* label, Color c, int icon, bool active, bool enabled) {
        bool hot = enabled && ui::hovered(r);
        float& h = hover_[icon];
        h = damp(h, hot ? 1.0f : 0.0f, 14, GetFrameTime());
        Rectangle d{r.x, r.y - h * 5, r.width, r.height};
        if (active) {
            fx::beginAdd();
            fx::glow(center(d), 110, alpha(c, 0.35f + 0.1f * std::sin(t_ * 5)));
            fx::endAdd();
        }
        Color base = enabled ? mix(darken(c, 0.15f), lighten(c, 0.15f), h) : mix(c, GRAY, 0.75f);
        DrawRectangleRounded({d.x, d.y + 6, d.width, d.height}, 0.35f, 10, darken(base, 0.55f));
        DrawRectangleRounded(d, 0.35f, 10, base);
        DrawRectangleRounded({d.x + 5, d.y + 4, d.width - 10, d.height * 0.42f}, 0.6f, 10, alpha(WHITE, 0.14f + h * 0.06f));
        DrawRectangleRoundedLinesEx(d, 0.35f, 10, active ? 3.5f : 2.5f, active ? col::gold : col::outline);
        Vector2 ic{d.x + d.width / 2, d.y + 24};
        Color k = enabled ? WHITE : col::muted;
        switch (icon) {
            case 0: iconFood(ic, 30, k); break;
            case 1: iconGame(ic, 34, k, base); break;
            case 2: iconShower(ic, 30, k); break;
            case 3:
                if (P().pieter.sleeping) iconSun(ic, 32, k);
                else iconMoon(ic, 32, k, base);
                break;
            default: iconChat(ic, 30, k, base); break;
        }
        ui::textShadow(label, ic.x, d.y + d.height - 15, 15, k, ui::Center, true, 2);
        if (hot && E().mousePressed()) {
            E().consumeClick();
            E().sfx.play(Sfx::Click);
            return true;
        }
        return false;
    }

    void drawDock() {
        auto& n = P().pieter;
        float bw = 138, gap = 12, total = 5 * bw + 4 * gap, x = VW / 2.0f - total / 2, y = VH - 80;
        Rectangle bg{x - 14, y - 12, total + 28, 86};
        DrawRectangleRounded(bg, 0.4f, 12, alpha(rgba(0x0c0818ff), 0.75f));
        DrawRectangleRoundedLinesEx(bg, 0.4f, 12, 1.5f, alpha(col::violet, 0.4f));
        bool awake = !n.sleeping;
        auto R = [&](int i) { return Rectangle{x + i * (bw + gap), y, bw, 64}; };
        if (dockButton(R(0), "FEED", col::orange, 0, mode_ == Mode::Feed, awake)) {
            mode_ = mode_ == Mode::Feed ? Mode::Idle : Mode::Feed;
        }
        if (dockButton(R(1), "PLAY", col::pink, 1, playT_ > 0, awake)) {
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
                mode_ = Mode::Idle;
            }
        }
        if (dockButton(R(2), "SHOWER", col::cyan, 2, mode_ == Mode::Shower, awake)) {
            mode_ = mode_ == Mode::Shower ? Mode::Idle : Mode::Shower;
            cleanAnnounced_ = n.hygiene >= 100;
            if (mode_ == Mode::Shower) {
                think("Scrub me! (drag over me)");
                showerSfxT_ = 0;
            }
            lastMouse_ = E().mouse();
        }
        if (dockButton(R(3), n.sleeping ? "WAKE UP" : "SLEEP", col::purple, 3, n.sleeping, true)) {
            if (n.sleeping) wake("Huh?! I'm awake!");
            else {
                n.sleeping = true;
                mode_ = Mode::Idle;
                think("Good night Pieter...");
                E().sfx.play(Sfx::Yawn, 0.6f);
                snoreT_ = 2.2f;
            }
        }
        if (dockButton(R(4), "TALK", col::green, 4, mode_ == Mode::Chat, true)) mode_ = mode_ == Mode::Chat ? Mode::Idle : Mode::Chat;
    }

    void drawFeedTray() {
        std::vector<const ItemDef*> foods;
        for (auto& it : itemDb())
            if (it.kind == ItemKind::Food) foods.push_back(&it);
        float sw = 108;
        float w = foods.size() * sw + 24;
        Rectangle r{VW / 2.0f - w / 2, VH - 252, w, 150};
        ui::panel(r, alpha(rgba(0x160e2cff), 0.94f), 0.12f, alpha(col::orange, 0.6f));
        ui::text("SNACK SHELF", r.x + 16, r.y - 16, 16, col::orange, ui::Left, true);
        for (size_t i = 0; i < foods.size(); i++) {
            const ItemDef* f = foods[i];
            Rectangle s{r.x + 12 + i * sw, r.y + 10, sw - 8, r.height - 20};
            int cnt = P().count(f->id);
            bool hot = ui::hovered(s);
            float& h = trayHover_[i % 8];
            h = damp(h, hot && cnt > 0 ? 1.0f : 0.0f, 14, GetFrameTime());
            DrawRectangleRounded(s, 0.18f, 8, mix(alpha(col::bgDeep, 0.7f), alpha(f->color, 0.35f), h));
            Vector2 ic{s.x + s.width / 2, s.y + 50 - h * 8 + std::sin(t_ * 2.5f + i) * 2};
            DrawEllipse((int)ic.x, (int)(s.y + 86), 30, 7, alpha(BLACK, 0.35f));
            if (h > 0.01f) {
                fx::beginAdd();
                fx::glow(ic, 60, alpha(f->color, 0.35f * h));
                fx::endAdd();
            }
            art::itemIcon(f->id, ic, 58 + h * 10);
            if (cnt == 0) DrawRectangleRounded(s, 0.18f, 8, alpha(col::bgDeep, 0.6f));
            // count pill
            std::string cs = "x" + std::to_string(cnt);
            float pw = ui::measure(cs, 14, true) + 14;
            Rectangle pill{s.x + s.width - pw - 4, s.y + 4, pw, 20};
            DrawRectangleRounded(pill, 1, 8, cnt ? col::gold : col::panelHi);
            ui::text(cs, pill.x + pill.width / 2, pill.y + 10, 14, cnt ? rgba(0x4a2a00ff) : col::muted, ui::Center, true);
            ui::text(f->name.substr(0, 12), s.x + s.width / 2, s.y + s.height - 16, 13, cnt ? col::text : col::muted, ui::Center, true);
            if (hot && E().mousePressed()) {
                E().consumeClick();
                if (cnt > 0 && P().take(f->id)) {
                    flying_.push_back({f->id, ic});
                    E().sfx.play(Sfx::Whoosh, 0.5f);
                } else {
                    E().sfx.play(Sfx::Error, 0.5f);
                    E().toast("No " + f->name + " left. Buy some in the SHOP!", col::orange);
                }
            }
        }
        if (ui::button({r.x + r.width - 136, r.y - 40, 136, 32}, "GO TO SHOP", col::green, 15)) E().go(makeShop);
    }

    void drawChat() {
        // a phone with Pieter's chat open
        Rectangle ph{VW - 392, 84, 372, 532};
        DrawRectangleRounded({ph.x + 8, ph.y + 10, ph.width, ph.height}, 0.09f, 12, alpha(BLACK, 0.4f));
        DrawRectangleRounded(shrink(ph, -4), 0.09f, 12, col::outline);
        DrawRectangleRounded(ph, 0.09f, 12, rgba(0x2a2238ff));
        Rectangle scr = shrink(ph, 10);
        DrawRectangleRounded(scr, 0.07f, 12, rgba(0x120c22ff));
        // header
        Rectangle hd{scr.x, scr.y, scr.width, 64};
        DrawRectangleRounded(hd, 0.4f, 10, rgba(0x231640ff));
        DrawRectangleRounded({hd.x + scr.width / 2 - 40, hd.y + 6, 80, 8}, 1, 8, col::outline);  // notch
        art::portrait("pieter", {hd.x + 34, hd.y + 38}, 18, col::violet);
        DrawCircleV({hd.x + 47, hd.y + 52}, 5, col::outline);
        DrawCircleV({hd.x + 47, hd.y + 52}, 3.5f, P().pieter.sleeping ? col::muted : col::green);
        ui::text("Pieter", hd.x + 62, hd.y + 30, 19, col::text, ui::Left, true);
        const char* status = P().pieter.sleeping ? "sleeping" : replyT_ > 0 ? "typing..." : "online";
        ui::text(status, hd.x + 62, hd.y + 50, 14, replyT_ > 0 ? col::green : col::muted);

        // messages, newest at the bottom
        Rectangle area{scr.x, scr.y + 66, scr.width, scr.height - 66 - 62};
        E().beginClip(area);
        float y = area.y + area.height - 10;
        if (replyT_ > 0) {
            Rectangle b{area.x + 14, y - 34, 74, 34};
            DrawRectangleRounded(b, 0.6f, 8, rgba(0x2e2148ff));
            for (int i = 0; i < 3; i++) DrawCircleV({b.x + 22 + i * 15.0f, b.y + 17 + std::sin(t_ * 10 + i) * 3}, 4.5f, col::muted);
            y -= 46;
        }
        for (int i = (int)chat_.size() - 1; i >= 0 && y > area.y; i--) {
            auto& m = chat_[i];
            float maxW = 250;
            float h = ui::paragraph(m.text, {0, 0, maxW, 0}, 18, WHITE, false);
            float tw = std::min(maxW, ui::measure(m.text, 18)) + 26;
            float bh = h + 18;
            y -= bh;
            float pop = easeOutBack(m.t);
            Rectangle b{m.fromPieter ? area.x + 14 : area.x + area.width - 14 - tw, y, tw, bh};
            Vector2 pc = m.fromPieter ? Vector2{b.x, b.y + bh} : Vector2{b.x + b.width, b.y + bh};
            rlPushMatrix();
            rlTranslatef(pc.x, pc.y, 0);
            rlScalef(pop, pop, 1);
            rlTranslatef(-pc.x, -pc.y, 0);
            if (m.fromPieter) {
                DrawRectangleRounded(b, 0.45f, 10, rgba(0x2e2148ff));
                DrawTriangle({b.x + 2, b.y + bh - 14}, {b.x - 7, b.y + bh + 1}, {b.x + 16, b.y + bh - 3}, rgba(0x2e2148ff));
            } else {
                DrawRectangleRounded(b, 0.45f, 10, col::purple);
                DrawRectangleGradientH((int)(b.x + 10), (int)b.y, (int)(b.width - 20), (int)b.height, alpha(col::purple, 0), alpha(col::pink, 0.45f));
                DrawTriangle({b.x + b.width - 2, b.y + bh - 14}, {b.x + b.width - 16, b.y + bh - 3}, {b.x + b.width + 7, b.y + bh + 1}, col::pink);
            }
            ui::paragraph(m.text, {b.x + 13, b.y + 8, maxW, 0}, 18, col::text);
            rlPopMatrix();
            y -= 10;
        }
        E().endClip();

        // input row
        Rectangle in{scr.x + 10, scr.y + scr.height - 52, scr.width - 72, 42};
        DrawRectangleRounded(in, 1, 10, rgba(0x231640ff));
        DrawRectangleRoundedLinesEx(in, 1, 10, 1.5f, alpha(col::violet, 0.7f));
        std::string shown = input_.empty() ? "Message Pieter..." : input_;
        float maxText = in.width - 30;
        while (ui::measure(shown, 18) > maxText && shown.size() > 1) shown.erase(0, 1);
        ui::text(shown, in.x + 16, in.y + in.height / 2, 18, input_.empty() ? col::muted : col::text);
        if (std::fmod(t_, 1.0f) < 0.5f)
            DrawRectangle((int)(in.x + 17 + (input_.empty() ? 0 : ui::measure(shown, 18))), (int)in.y + 10, 2, 22, col::text);
        Vector2 sb{scr.x + scr.width - 32, in.y + in.height / 2};
        bool hot = Vector2Distance(E().mouse(), sb) < 21;
        DrawCircleV(sb, 21, input_.empty() ? col::panelHi : (hot ? lighten(col::pink, 0.2f) : col::pink));
        DrawTriangle({sb.x - 8, sb.y - 10}, {sb.x - 8, sb.y + 10}, {sb.x + 11, sb.y}, WHITE);
        DrawTriangle({sb.x - 8, sb.y - 10}, {sb.x + 11, sb.y}, {sb.x - 8, sb.y + 10}, WHITE);
        if (hot && E().mousePressed()) {
            E().consumeClick();
            sendChat();
        }
    }

    // ============================================================ draw
    void draw() override {
        auto& n = P().pieter;
        room::drawWalls(env_);
        room::drawWindow(env_);
        room::drawWallDecor(env_);
        room::drawFloorLight(env_);
        room::drawBed(env_);
        room::drawDesk(env_);
        room::drawRug(env_);
        drawPieter();
        drawShower();
        for (auto& f : flying_) {
            Vector2 to = toScreen(kMouth);
            Vector2 p = Vector2Lerp(f.from, to, easeInCubic(f.t));
            p.y -= std::sin(f.t * PI) * 170;
            float s = 58 * (1 - f.t * 0.55f);
            art::itemIcon(f.item, p, s);
        }
        applyLightMap();
        room::drawBloom(env_);
        drawSteam();
        shocks_.draw();
        fx_.draw();
        art::vignette(0.45f);
        drawThought();

        ui::titleBar("PIETER ROOM", col::pink);
        if (ui::backButton()) E().go(makeMenu);
        ui::coinCounter(VW - 20, 38, P().coins);
        drawStats();

        // pet by clicking his head (not while scrubbing in the shower)
        if (mode_ != Mode::Shower && overHead(E().mouse()) && E().mousePressed() && petCd_ <= 0 && !(mode_ == Mode::Chat && E().mouse().x > VW - 400)) {
            E().consumeClick();
            petPieter();
        }
        tickleWindow_ = std::max(0.0f, tickleWindow_ - GetFrameTime());
        if (mode_ == Mode::Idle && !n.sleeping && playT_ <= 0 && t_ < 12) {
            float a = clamp01((12 - t_) / 2);
            Rectangle pill{kNeck.x - 110, VH - 122, 220, 28};
            DrawRectangleRounded(pill, 1, 10, alpha(col::bgDeep, 0.75f * a));
            ui::text("click Pieter to pet him", pill.x + pill.width / 2, pill.y + 14, 15, alpha(col::text, 0.85f * a), ui::Center, true);
        }

        drawDock();
        if (mode_ == Mode::Feed && !n.sleeping) drawFeedTray();
        if (mode_ == Mode::Chat) drawChat();
    }

private:
    float t_ = 0, talkCd_ = 0, petCd_ = 0, thoughtT_ = 0, replyT_ = 0, playT_ = 0, zzzT_ = 0, saveT_ = 0, squish_ = 0, scrub_ = 0;
    float happyT_ = 0, blushT_ = 0, chewT_ = 0, mouthTalkT_ = 0, levelFlash_ = 0, tickleT_ = 0, tickleWindow_ = 0;
    int tickles_ = 0;
    float blinkT_ = 2, glanceT_ = 0, mouseIdle_ = 99, snoreT_ = 1, showerSfxT_ = 0, rinseWait_ = 0, buzzT_ = 2, growlT_ = 4, sparkleT_ = 0;
    float bob_ = 0, sway_ = 0, tilt_ = 0, sqx_ = 1, sqy_ = 1, armWave_ = 0;
    float hover_[5]{}, trayHover_[8]{};
    Vector2 look_{640, 420}, glance_{640, 420}, lastMouseLook_{};
    std::string thought_, pendingReply_, input_;
    bool cleanAnnounced_ = false;
    Vector2 lastMouse_{};
    Mode mode_ = Mode::Idle;
    std::vector<ChatLine> chat_;
    std::vector<Flying> flying_;
    std::vector<Drop> drops_;
    std::vector<Foam> foam_;
    room::Env env_;
    RenderTexture2D lm_{};
    Particles fx_;
    fx::Shocks shocks_;
};

}  // namespace

std::unique_ptr<Scene> makePieterRoom() { return std::make_unique<PieterRoomScene>(); }

}  // namespace pg
