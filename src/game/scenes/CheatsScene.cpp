// CHEATS DOWNLOADEN - get PieterHax onto your PC without getting caught.
// You sit at Pieter's PC. The desktop is a real little window manager: drag
// windows by the title bar, minimise, maximise, close, use the taskbar and
// double-click desktop icons. The download has to reach 100% while:
//   - Mama, Papa and your sister come to check on you. Watch the webcam (the
//     door is behind you) and listen for footsteps; hide every cheat window
//     before the door opens. Homework in front earns you points.
//   - gorillas knock on the window: click it to close the blinds before the
//     noise brings someone upstairs
//   - a RAT takes over your cursor and spams pop-ups until you scan with PieterDefender
//   - the download stalls: resume it, type a captcha, allow it in the
//     antivirus or empty the recycle bin
//   - your boba runs out (energy): order more, your sister brings it up
//   - Pietergram messages want answers, PieterOS wants to update
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace pg {
namespace {

constexpr Rectangle S{160, 38, 960, 560};  // the monitor's screen
constexpr float kTask = 34;                  // taskbar height
constexpr float kTitle = 28;                 // window title bar height
constexpr Rectangle kRoomWindow{1146, 110, 122, 300};
constexpr Rectangle kCup{1010, 640, 54, 70};

enum App { Loader, Huiswerk, Cam, Gram, Boba, Defender, Trash, Popup, Update, AppCount };
const char* kAppName[AppCount] = {"PieterHax Loader", "Homework.docx", "PieterCam", "Pietergram", "BobaDelivery", "PieterDefender", "Recycle Bin", "!!!", "PieterOS Update"};
const Color kAppCol[AppCount] = {rgba(0x1a8a3aff), rgba(0x2a5bd7ff), rgba(0x3a3a4aff), rgba(0xc2418aff), rgba(0xd8822aff), rgba(0x2a9ad8ff), rgba(0x6a6a7aff), rgba(0xc81a2aff), rgba(0x2a6ad8ff)};
const char* kIconLetter[AppCount] = {"H", "W", "C", "P", "B", "D", "T", "!", "U"};

enum class Who { Mama, Papa, Zus };
enum class Intr { None, Lost, Captcha, Blocked, DiskFull };
enum class Phase { Title, Play, Caught, Restart, Lose, Win };

struct Win {
    int id;
    App app;
    Rectangle r, restore;
    bool min = false, max = false;
    float anim = 0;  // 0..1 open animation
    std::string msg; // pop-up text
};

struct Reply { std::string text; float susp; int effect; };  // effect: 0 none, 1 RAT, 2 Davi's tip
struct Msg {
    std::string from, text;
    std::vector<Reply> replies;
    float deadline = 0;  // > 0: must be answered in time
    float missPenalty = 0;
    bool answered = false, mine = false;
};

struct Note { std::string title, text; Color c; float t = 0; };

// a person in the doorway (drawn small in the webcam and big when you're caught)
void person(Who w, Vector2 feet, float s, float anger, float t) {
    Color body = w == Who::Mama ? rgba(0xd86a9aff) : w == Who::Papa ? rgba(0x3a6ab8ff) : rgba(0x3aa86aff);
    Color hair = w == Who::Mama ? rgba(0xe8c060ff) : w == Who::Papa ? rgba(0x5a4030ff) : rgba(0x6a3a1aff);
    Color skin = rgba(0xf0c8a0ff), ink = rgba(0x1a1020ff);
    float breathe = std::sin(t * 2) * 2 * s;
    // arms (crossed on the hips when angry)
    for (int k : {-1, 1}) {
        Vector2 sh{feet.x + k * 46 * s, feet.y - 140 * s + breathe};
        Vector2 hand = anger > 0.5f ? Vector2{feet.x + k * 34 * s, feet.y - 70 * s} : Vector2{feet.x + k * 58 * s, feet.y - 50 * s};
        Vector2 elbow = anger > 0.5f ? Vector2{feet.x + k * 74 * s, feet.y - 96 * s} : Vector2{feet.x + k * 60 * s, feet.y - 96 * s};
        DrawLineEx(sh, elbow, 26 * s, darken(body, 0.2f));
        DrawLineEx(elbow, hand, 24 * s, darken(body, 0.12f));
        DrawCircleV(elbow, 12 * s, darken(body, 0.16f));
        DrawCircleV(hand, 12 * s, skin);
    }
    // torso with shading and a collar
    Rectangle torso{feet.x - 46 * s, feet.y - 156 * s + breathe, 92 * s, 160 * s};
    DrawRectangleRounded(shrink(torso, -3 * s), 0.45f, 10, ink);
    fx::gradRoundRect(torso, 0.45f, lighten(body, 0.12f), darken(body, 0.3f));
    DrawRectangleRounded({torso.x + 8 * s, torso.y + 10 * s, 18 * s, torso.height - 30 * s}, 1, 6, alpha(WHITE, 0.12f));
    DrawTriangle({feet.x - 16 * s, torso.y}, {feet.x, torso.y + 22 * s}, {feet.x + 16 * s, torso.y}, w == Who::Papa ? WHITE : darken(body, 0.25f));
    if (w == Who::Papa) DrawLineEx({feet.x, torso.y + 18 * s}, {feet.x, torso.y + 80 * s}, 8 * s, rgba(0xc81a2aff));  // tie
    // neck + head
    DrawRectangleRec({feet.x - 12 * s, feet.y - 172 * s + breathe, 24 * s, 22 * s}, darken(skin, 0.12f));
    Vector2 h{feet.x, feet.y - 200 * s + breathe};
    if (w == Who::Mama) {
        DrawCircleV({h.x, h.y - 40 * s}, 21 * s, ink);
        DrawCircleV({h.x, h.y - 40 * s}, 18 * s, hair);
    }
    if (w == Who::Zus) {
        DrawRectangleRounded({h.x + 26 * s, h.y - 14 * s, 16 * s, 58 * s}, 1, 6, ink);
        DrawRectangleRounded({h.x + 28 * s, h.y - 12 * s, 12 * s, 54 * s}, 1, 6, hair);
    }
    DrawCircleV(h, 39 * s, ink);
    DrawCircleV(h, 36 * s, skin);
    DrawCircleGradient({h.x - 10 * s, h.y - 10 * s}, 30 * s, alpha(WHITE, 0.15f), alpha(WHITE, 0));
    for (int k : {-1, 1}) DrawCircleV({h.x + k * 37 * s, h.y + 4 * s}, 7 * s, darken(skin, 0.1f));  // ears
    if (w == Who::Papa) {
        DrawCircleSector(h, 37 * s, 195, 345, 16, hair);  // receding hairline
    } else {
        DrawCircleSector({h.x, h.y - 4 * s}, 38 * s, 175, 365, 16, hair);
        DrawCircleSector({h.x - 18 * s, h.y - 14 * s}, 16 * s, 0, 180, 10, hair);  // fringe
        DrawCircleSector({h.x + 12 * s, h.y - 16 * s}, 14 * s, 0, 180, 10, hair);
    }
    // eyes, brows, cheeks, mouth
    for (int k : {-1, 1}) {
        Vector2 e{h.x + k * 13 * s, h.y + 4 * s};
        DrawEllipse((int)e.x, (int)e.y, 7 * s, 6 * s, WHITE);
        DrawCircleV({e.x - k * 1 * s, e.y + 1 * s}, 3.6f * s, ink);
        DrawCircleV({e.x - k * 2 * s, e.y - 0.5f * s}, 1.2f * s, WHITE);
        float tilt = anger * 7 * s;
        DrawLineEx({e.x + k * 9 * s, e.y - 10 * s - tilt}, {e.x - k * 6 * s, e.y - 9 * s + tilt}, 3.2f * s, darken(hair, 0.4f));
        DrawCircleV({h.x + k * 22 * s, h.y + 16 * s}, 6 * s, alpha(col::pink, 0.25f + 0.3f * anger));
    }
    if (w == Who::Papa) {
        for (int k : {-1, 1}) DrawRing({h.x + k * 13 * s, h.y + 4 * s}, 9 * s, 11 * s, 0, 360, 20, ink);
        DrawLineEx({h.x - 3 * s, h.y + 4 * s}, {h.x + 3 * s, h.y + 4 * s}, 2 * s, ink);
        DrawRectangleRounded({h.x - 15 * s, h.y + 17 * s, 30 * s, 8 * s}, 1, 6, hair);  // moustache
    }
    if (anger > 0.5f) {
        DrawEllipse((int)h.x, (int)(h.y + 26 * s), 11 * s, 9 * s * anger, rgba(0x5a1020ff));
        DrawEllipse((int)h.x, (int)(h.y + 22 * s), 8 * s, 2.5f * s, WHITE);
    } else {
        DrawLineEx({h.x - 9 * s, h.y + 26 * s}, {h.x + 9 * s, h.y + 24 * s}, 2.5f * s, rgba(0x5a1020ff));
    }
}

void gorilla(Vector2 c, float s, float bang, float t) {
    Color fur = rgba(0x1c1a20ff), face = rgba(0x3a3440ff), hi = rgba(0x2c2a32ff);
    // fists pounding on the glass
    for (int k : {-1, 1}) {
        float lift = bang > 0 ? std::abs(std::sin(t * 18 + k)) * 14 * s : 0;
        DrawCircleV({c.x + k * 52 * s, c.y + 70 * s - lift}, 22 * s, fur);
        DrawCircleV({c.x + k * 52 * s, c.y + 66 * s - lift}, 14 * s, face);
    }
    DrawEllipse((int)c.x, (int)(c.y + 110 * s), 80 * s, 50 * s, fur);  // shoulders
    DrawCircleV(c, 50 * s, fur);
    DrawEllipse((int)c.x, (int)(c.y - 40 * s), 30 * s, 24 * s, hi);  // crest
    DrawEllipse((int)c.x, (int)(c.y + 12 * s), 34 * s, 30 * s, face);
    DrawEllipse((int)c.x, (int)(c.y - 10 * s), 40 * s, 12 * s, fur);  // brow
    for (int k : {-1, 1}) {
        DrawCircleV({c.x + k * 14 * s, c.y - 2 * s}, 5 * s, rgba(0x0a0806ff));
        DrawCircleV({c.x + k * 14 * s - 1.5f * s, c.y - 3.5f * s}, 1.6f * s, WHITE);
        DrawEllipse((int)(c.x + k * 7 * s), (int)(c.y + 14 * s), 5 * s, 4 * s, rgba(0x0a0806ff));
    }
    float open = bang > 0 ? 0.5f + 0.5f * std::sin(t * 12) : 0.1f;
    DrawEllipse((int)c.x, (int)(c.y + 30 * s), 16 * s, (3 + 9 * open) * s, rgba(0x0a0806ff));
}

class CheatsScene : public Scene {
public:
    Presence presence() const override {
        if (phase_ == Phase::Play) return {"Cheat Downloader", TextFormat("PieterHax %d%% - don't get caught", (int)(pct() * 100))};
        return {"Cheat Downloader", "Thinking about it..."};
    }

    void enter() override {
        if (const char* d = std::getenv("PG_CHEATS")) {  // dev hook: PG_CHEATS=<difficulty 1-3> starts right away
            start(std::clamp(std::atoi(d), 1, 3) - 1);
            if (const char* p = std::getenv("PG_CHEATS_PCT")) got_ = total_ * (float)std::atof(p) / 100;
            if (std::getenv("PG_CHEATS_RAT")) startRat();
            if (std::getenv("PG_CHEATS_GORILLA")) startGorilla();
            if (std::getenv("PG_CHEATS_DOOR")) { startCheck(Who::Mama, 0.5f); }
            if (const char* i = std::getenv("PG_CHEATS_INTR")) interrupt((Intr)std::clamp(std::atoi(i), 1, 4));
            devCalm_ = std::getenv("PG_CHEATS_CALM") != nullptr;
        }
    }
    void exit() override { ShowCursor(); }

    // ============================================================ setup
    void start(int diff) {
        diff_ = diff;
        phase_ = Phase::Play;
        phaseT_ = 0;
        wins_.clear();
        msgs_.clear();
        notes_.clear();
        nextId_ = 1;
        got_ = 0;
        total_ = 6900;
        speed_ = 25;
        intr_ = Intr::None;
        susp_ = 0;
        energy_ = 100;
        money_ = 30;
        homework_ = 0;
        t_ = 0;
        playT_ = 0;
        check_ = {};
        gor_ = {};
        rat_ = false;
        ratOff_ = {0, 0};
        updateOn_ = false;
        delivery_ = -1;
        blinds_ = 0;
        installT_ = -1;
        caughtCount_ = 0;
        float k = diff == 0 ? 1.4f : diff == 1 ? 1.0f : 0.72f;  // time between events
        nextCheck_ = 22 * k;
        nextGorilla_ = 40 * k;
        nextIntr_ = 18 * k;
        nextMsg_ = 6;
        nextUpdate_ = 95 * k;
        ratAt_ = rnd(0.35f, 0.55f);
        ratDone_ = false;
        msgScript_ = 0;
        open(Huiswerk);
        open(Cam);
        open(Loader);
        HideCursor();
        E().sfx.play(Sfx::Power, 0.5f, 0.8f);
    }

    float pct() const { return clamp01(got_ / total_); }
    float warnTime() const { return diff_ == 0 ? 5.0f : diff_ == 1 ? 3.6f : 2.5f; }

    // ============================================================ window manager
    Rectangle desk() const { return {S.x, S.y, S.width, S.height - kTask}; }

    Win* find(App a) {
        for (auto& w : wins_)
            if (w.app == a) return &w;
        return nullptr;
    }
    int indexOf(int id) const {
        for (size_t i = 0; i < wins_.size(); i++)
            if (wins_[i].id == id) return (int)i;
        return -1;
    }
    void focus(int id) {
        int i = indexOf(id);
        if (i < 0) return;
        Win w = wins_[i];
        wins_.erase(wins_.begin() + i);
        wins_.push_back(w);
    }
    Win* top() {
        for (int i = (int)wins_.size() - 1; i >= 0; i--)
            if (!wins_[i].min) return &wins_[i];
        return nullptr;
    }

    Rectangle defaultRect(App a) const {
        switch (a) {
            case Loader: return {S.x + 250, S.y + 70, 460, 300};
            case Huiswerk: return {S.x + 120, S.y + 30, 520, 420};
            case Cam: return {S.x + S.width - 262, S.y + 12, 250, 196};
            case Gram: return {S.x + 470, S.y + 140, 320, 340};
            case Boba: return {S.x + 300, S.y + 110, 340, 320};
            case Defender: return {S.x + 330, S.y + 90, 360, 270};
            case Trash: return {S.x + 380, S.y + 150, 320, 220};
            case Update: return {S.x + S.width / 2 - 190, S.y + 170, 380, 180};
            default: return {S.x + rnd(40, S.width - 360), S.y + rnd(30, S.height - 220), 320, 150};
        }
    }

    Win& open(App a, const std::string& msg = "") {
        if (a != Popup) {
            if (Win* w = find(a)) {
                w->min = false;
                int id = w->id;
                focus(id);
                return wins_.back();
            }
        }
        Win w{nextId_++, a, defaultRect(a), {}, false, false, 0, msg};
        w.restore = w.r;
        wins_.push_back(w);
        E().sfx.play(a == Popup ? Sfx::Error : Sfx::Pop, a == Popup ? 0.5f : 0.35f, a == Popup ? rnd(0.9f, 1.3f) : 1.2f);
        return wins_.back();
    }
    void close(int id) {
        int i = indexOf(id);
        if (i >= 0) wins_.erase(wins_.begin() + i);
        E().sfx.play(Sfx::Click, 0.3f, 0.9f);
    }
    void minimize(Win& w) {
        w.min = true;
        E().sfx.play(Sfx::Whoosh, 0.25f, 1.6f);
    }
    void toggleMax(Win& w) {
        if (w.max) w.r = w.restore;
        else {
            w.restore = w.r;
            w.r = desk();
        }
        w.max = !w.max;
    }

    bool suspiciousVisible() const {
        for (auto& w : wins_)
            if (!w.min && (w.app == Loader || w.app == Popup)) return true;
        return false;
    }

    // ============================================================ events
    struct Check {
        bool on = false;
        Who who = Who::Mama;
        int stage = 0;  // 0 coming up the stairs, 1 door opening, 2 watching, 3 leaving
        float t = 0, warn = 3, watch = 3;
        bool delivery = false, judged = false;
        float stepT = 0;
    };
    struct Gorilla {
        bool on = false;
        float t = 0, knockT = 0;
        bool banging = false;
    };

    void startCheck(Who w, float warn, bool delivery = false) {
        if (check_.on) return;
        check_ = {};
        check_.on = true;
        check_.who = w;
        check_.warn = warn;
        check_.watch = delivery ? 2.2f : w == Who::Papa ? 3.8f : 3.0f;
        check_.delivery = delivery;
        caption(delivery ? "[ footsteps... \"YOUR BOBA IS HERE!\" ]" : w == Who::Papa ? "[ heavy footsteps on the stairs ]" : w == Who::Mama ? "[ footsteps on the stairs ]" : "[ sneaky little footsteps ]");
    }

    void startGorilla() {
        gor_ = {};
        gor_.on = true;
        blinds_ = std::min(blinds_, 0.0f);
        E().sfx.play(Sfx::Knock, 0.45f, 0.75f, 0.9f);
        note("Window", "Something is knocking on your window...", col::orange);
    }

    void startRat() {
        if (rat_) return;
        rat_ = true;
        ratT_ = 0;
        ratTarget_ = {0, 0};
        popT_ = 0.5f;
        E().sfx.play(Sfx::Static, 0.6f);
        E().sfx.play(Sfx::Error, 0.6f, 0.7f);
        note("PieterDefender", "RAT detected! Someone is taking over your mouse. Scan now!", col::red);
    }

    void interrupt(Intr i) {
        intr_ = i;
        captcha_ = "";
        captchaIn_ = "";
        if (i == Intr::Captcha) {
            const char* L = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
            for (int k = 0; k < 5; k++) captcha_ += L[rndi(0, 30)];
        }
        resumeBtn_ = {rnd(0.15f, 0.65f), rnd(0.55f, 0.75f)};
        if (i == Intr::DiskFull) trashFull_ = true;
        E().sfx.play(Sfx::Error, 0.45f);
        note("PieterHax Loader", i == Intr::Lost ? "Download paused: connection lost." : i == Intr::Captcha ? "Download paused: captcha required."
                                  : i == Intr::Blocked ? "PieterDefender is blocking PieterHax!" : "Disk full! Empty the recycle bin.", col::orange);
    }

    void caught(const std::string& line) {
        float add = check_.who == Who::Papa ? 55.0f : check_.who == Who::Mama ? 40.0f : 30.0f;
        susp_ += add;
        caughtCount_++;
        caughtLine_ = line;
        caughtWho_ = check_.who;
        phase_ = Phase::Caught;
        phaseT_ = 0;
        got_ = std::max(0.0f, got_ - total_ * 0.08f);  // they pull the plug for a bit
        E().sfx.play(Sfx::Slam, 0.8f);
        E().sfx.play(Sfx::Hurt, 0.6f, 0.8f);
        E().shake(14);
        for (auto& w : wins_)
            if (w.app == Loader || w.app == Popup) w.min = true;
    }

    void note(const std::string& title, const std::string& text, Color c) {
        notes_.push_back({title, text, c, 0});
        if (notes_.size() > 3) notes_.erase(notes_.begin());
    }
    void caption(const std::string& s) {
        captionText_ = s;
        captionT_ = 3;
    }

    // ------------------------------------------------------------ Pietergram
    void pushMsg(Msg m) {
        msgs_.push_back(m);
        unread_++;
        E().sfx.play(Sfx::Msg, 0.5f);
        note("Pietergram - " + m.from, m.text, kAppCol[Gram]);
    }

    void nextMessage() {
        int n = msgScript_++;
        Msg m;
        switch (n % 9) {
            case 0: m = {"Kira", "do you have PieterHax yet?? everyone has it", {{"almost!", 0, 0}, {"shhh mom can read this", 0, 0}}}; break;
            case 1:
                m = {"Mom", "Pieter, are you doing your homework?",
                     {{"Yes mom! Math.", -5, 0}, {"uhh... sure", 6, 0}, {"No, I'm downloading cheats", 45, 0}}, 22, 15};
                break;
            case 2: m = {"Unknown", "FREE ROBUX!!! click here: bit.ly/p13ter-r0bux", {{"click the link", 0, 1}, {"block", 0, 0}}}; break;
            case 3: m = {"Davi", "yo heads up, your dad is about to come check on you", {{"thanks bro", 0, 2}, {"ok", 0, 2}}}; break;
            case 4: m = {"The Horse", "JUHUH", {{"JUHUH", 0, 0}, {"who are you", 0, 0}}}; break;
            case 5:
                m = {"Dad", "Why is the wifi so slow? What are you downloading?",
                     {{"Windows updates!", 0, 0}, {"A math app", -3, 0}, {"Nothing!!!", 12, 0}}, 20, 18};
                break;
            case 6: m = {"Davi", "tip: if the download gets stuck you have to resume it manually", {{"i know", 0, 0}}}; break;
            case 7: m = {"Kira", "did you see the Monkey Men on your street??", {{"what", 0, 0}, {"that explains the gorilla", 0, 0}}}; break;
            default:
                m = {"Mom", "Dinner is ready in 10 minutes! Are you coming?", {{"Yes!", -2, 0}, {"In a minute...", 4, 0}}, 25, 8};
                break;
        }
        pushMsg(m);
    }

    void answer(Msg& m, const Reply& r) {
        m.answered = true;
        msgs_.push_back({"You", r.text, {}, 0, 0, true, true});
        susp_ = std::max(0.0f, susp_ + r.susp);
        E().sfx.play(Sfx::Send, 0.4f);
        if (r.susp >= 40) {
            E().sfx.play(Sfx::Lose, 0.5f);
            note("Pietergram", "Mom: WHAT?! I'm coming upstairs.", col::red);
            startCheck(Who::Mama, warnTime());
        }
        if (r.effect == 1) startRat();
        if (r.effect == 2) {
            nextCheck_ = 6;
            nextWho_ = 1;
            note("Pietergram", "Davi warned you: Dad is coming soon.", col::cyan);
        }
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        phaseT_ += dt;
        clickId_ = -1;  // a click only counts on the frame it happened
        fx_.update(dt);
        for (auto& n : notes_) n.t += dt;
        std::erase_if(notes_, [](const Note& n) { return n.t > 5.5f; });
        captionT_ -= dt;
        for (auto& w : wins_) w.anim = std::min(1.0f, w.anim + dt * 7);
        mouse_ = E().mouse();

        switch (phase_) {
            case Phase::Title:
                ShowCursor();
                if (E().backPressed()) E().go(makeMenu);
                return;
            case Phase::Caught:
                if (phaseT_ > 2.6f) {
                    check_.on = false;
                    nextCheck_ = rnd(20, 32) * (diff_ == 2 ? 0.75f : 1);
                    if (susp_ >= 100) lose("Grounded! No PC for a month.");
                    else phase_ = Phase::Play;
                }
                return;
            case Phase::Restart:
                if (phaseT_ > 5.5f) {
                    phase_ = Phase::Play;
                    open(Cam);
                    E().sfx.play(Sfx::Power, 0.5f, 0.8f);
                }
                return;
            case Phase::Lose:
            case Phase::Win:
                ShowCursor();
                return;
            case Phase::Play: break;
        }
        HideCursor();
        if (E().backPressed()) {
            E().go(makeMenu);
            return;
        }
        playT_ += dt;
        updateCursor(dt);
        handleInput();
        updateDownload(dt);
        updateEvents(dt);
    }

    void lose(const std::string& why) {
        phase_ = Phase::Lose;
        phaseT_ = 0;
        loseWhy_ = why;
        E().sfx.play(Sfx::Lose, 0.8f);
        ShowCursor();
    }

    void win() {
        phase_ = Phase::Win;
        phaseT_ = 0;
        reward_ = (diff_ == 0 ? 120 : diff_ == 1 ? 220 : 360) + (long long)((100 - std::min(100.0f, susp_)) * 2);
        P().earn(reward_);
        P().save();
        E().sfx.play(Sfx::Win, 0.8f);
        fx_.confetti({VW / 2.0f, 200}, 180);
        ShowCursor();
    }

    void updateCursor(float dt) {
        if (rat_) {
            ratT_ += dt;
            // the RAT drags your cursor around with jerks
            if (Vector2Distance(ratOff_, ratTarget_) < 10 || rnd() < dt * 0.8f) ratTarget_ = {rnd(-220, 220), rnd(-150, 150)};
            ratOff_ = damp(ratOff_, ratTarget_, 1.6f, dt);
            if (rnd() < dt * 0.6f) ratOff_ = ratOff_ + Vector2{rnd(-80, 80), rnd(-60, 60)};
            popT_ -= dt;
            int pops = 0;
            for (auto& w : wins_) pops += w.app == Popup;
            if (popT_ <= 0 && pops < 5) {
                popT_ = rnd(2.5f, 4.5f);
                const char* txt[] = {"HACKED BY xX_P1ET3R_Xx", "Your files are being encrypted...", "Webcam is being shared with 1,337 viewers",
                                     "Pay 0.5 Bitcoin to the RAT King", "Your password is: password123"};
                open(Popup, txt[rndi(0, 4)]);
            }
        } else {
            ratOff_ = damp(ratOff_, {0, 0}, 6, dt);
        }
        cur_ = mouse_ + ratOff_;
        cur_.x = std::clamp(cur_.x, 0.0f, (float)VW - 2);
        cur_.y = std::clamp(cur_.y, 0.0f, (float)VH - 2);
    }

    void updateDownload(float dt) {
        if (installT_ >= 0) {
            installT_ += dt;
            if (installT_ >= 6) win();
            return;
        }
        if (intr_ != Intr::None || got_ >= total_) return;
        // the line wobbles; RAT and the update eat bandwidth
        float target = rnd(18, 40) * (rat_ ? 0.45f : 1) * (diff_ == 0 ? 1.15f : diff_ == 2 ? 0.9f : 1);
        speed_ = damp(speed_, target, 1.5f, dt);
        got_ += speed_ * dt;
        if (got_ >= total_) {
            got_ = total_;
            E().sfx.play(Sfx::Perfect, 0.7f);
            note("PieterHax Loader", "Download complete! Click INSTALL.", col::green);
        }
        if (!ratDone_ && pct() > ratAt_) {
            ratDone_ = true;
            startRat();
        }
    }

    void updateEvents(float dt) {
        if (devCalm_) {  // dev hook: only the events that are already running
            nextCheck_ = nextGorilla_ = nextIntr_ = nextMsg_ = nextUpdate_ = 1e9f;
            energy_ = 100;
        }
        // energy: Pieter sips boba all the time
        energy_ -= dt * (diff_ == 0 ? 0.7f : diff_ == 1 ? 0.9f : 1.05f);
        if (std::fmod(playT_, 9.0f) < dt && energy_ > 0) E().sfx.play(Sfx::Gulp, 0.15f, rnd(0.9f, 1.1f));
        if (energy_ <= 25 && !tiredWarned_) {
            tiredWarned_ = true;
            note("Pieter", "I'm getting tired... order more boba!", col::orange);
        }
        if (energy_ <= 0) {
            lose("Pieter fell asleep at his desk.");
            return;
        }
        if (delivery_ > 0) {
            delivery_ -= dt;
            if (delivery_ <= 0) {
                E().sfx.play(Sfx::Bell, 0.6f, 1.3f);
                startCheck(Who::Zus, warnTime() * 0.9f, true);
            }
        }
        // parents checking
        nextCheck_ -= dt;
        if (nextCheck_ <= 0 && !check_.on) {
            Who w = nextWho_ >= 0 ? (Who)nextWho_ : (Who)rndi(0, 2);
            nextWho_ = -1;
            float warn = warnTime() * (w == Who::Zus ? 0.6f : w == Who::Papa ? 1.1f : 1);
            startCheck(w, warn);
            nextCheck_ = 1e9f;
        }
        if (check_.on) updateCheck(dt);
        // gorilla
        nextGorilla_ -= dt;
        if (nextGorilla_ <= 0 && !gor_.on) {
            nextGorilla_ = rnd(35, 60) * (diff_ == 2 ? 0.7f : 1);
            startGorilla();
        }
        if (gor_.on) {
            gor_.t += dt;
            gor_.knockT -= dt;
            if (blinds_ >= 0.95f) {
                gor_.on = false;
                note("Window", "Blinds closed. The gorilla wanders off.", col::muted);
            } else {
                if (gor_.knockT <= 0) {
                    gor_.knockT = gor_.banging ? 0.7f : 1.8f;
                    E().sfx.play(Sfx::Knock, gor_.banging ? 0.9f : 0.45f, gor_.banging ? 0.65f : 0.8f, 0.9f);
                    if (gor_.banging) E().shake(4);
                }
                if (gor_.t > 6.5f && !gor_.banging) {
                    gor_.banging = true;
                    E().sfx.play(Sfx::Monkey, 0.7f, 0.55f, 0.9f);
                    susp_ += 8;
                    caption("[ BONK BONK BONK - \"What is that noise?!\" ]");
                    if (!check_.on) {
                        nextWho_ = 1;
                        nextCheck_ = 1.5f;
                    }
                }
                if (gor_.t > 14) gor_.on = false;
            }
        }
        if (!gor_.on) blinds_ = std::max(0.0f, blinds_ - dt * 0.03f);  // the blinds creep back open
        // download hiccups
        nextIntr_ -= dt;
        if (nextIntr_ <= 0 && intr_ == Intr::None && got_ < total_) {
            nextIntr_ = rnd(20, 34) * (diff_ == 0 ? 1.3f : diff_ == 2 ? 0.8f : 1);
            interrupt((Intr)rndi(1, 4));
        }
        // messages
        nextMsg_ -= dt;
        if (nextMsg_ <= 0) {
            nextMsg_ = rnd(13, 22);
            nextMessage();
        }
        for (auto& m : msgs_) {
            if (m.deadline > 0 && !m.answered) {
                m.deadline -= dt;
                if (m.deadline <= 0) {
                    m.answered = true;
                    susp_ += m.missPenalty;
                    note("Pietergram", m.from + " thinks it's suspicious that you're not answering.", col::orange);
                }
            }
        }
        // PieterOS update
        nextUpdate_ -= dt;
        if (nextUpdate_ <= 0 && !updateOn_) {
            updateOn_ = true;
            updateT_ = 25;
            open(Update);
            nextUpdate_ = 1e9f;
        }
        if (updateOn_) {
            updateT_ -= dt;
            if (updateT_ <= 0) forcedRestart();
        }
        susp_ = std::clamp(susp_ - dt * 0.4f, 0.0f, 120.0f);  // suspicion slowly fades
        if (susp_ >= 100) lose("Too suspicious. Mom took the wifi cable.");
    }

    void forcedRestart() {
        updateOn_ = false;
        phase_ = Phase::Restart;
        phaseT_ = 0;
        wins_.clear();
        got_ = std::max(0.0f, got_ - total_ * 0.12f);
        intr_ = Intr::None;
        rat_ = false;
        E().sfx.play(Sfx::Lose, 0.4f, 0.8f);
        nextUpdate_ = rnd(80, 120);
    }

    void updateCheck(float dt) {
        Check& c = check_;
        c.t += dt;
        switch (c.stage) {
            case 0:
                c.stepT -= dt;
                if (c.stepT <= 0) {
                    float k = c.t / c.warn;
                    c.stepT = c.who == Who::Papa ? 0.55f : c.who == Who::Zus ? 0.32f : 0.42f;
                    E().sfx.play(Sfx::Step, (c.who == Who::Zus ? 0.2f : 0.35f) + 0.45f * k, c.who == Who::Papa ? 0.75f : c.who == Who::Zus ? 1.25f : 1.0f, -0.6f);
                }
                if (c.t >= c.warn) {
                    c.stage = 1;
                    c.t = 0;
                    E().sfx.play(Sfx::Creak, 0.7f, 1.1f, -0.5f);
                }
                break;
            case 1:
                if (c.t >= 0.55f) {
                    c.stage = 2;
                    c.t = 0;
                }
                break;
            case 2: {
                // they look at your screen the whole time they're in the doorway
                if (suspiciousVisible()) {
                    const char* lines[3] = {"PIETER! What is that?!", "WHAT. IS. THIS.", "I'M TELLING DAD!"};
                    caught(lines[(int)c.who]);
                    return;
                }
                if (!c.judged) {
                    c.judged = true;
                    Win* t = top();
                    if (c.delivery) {
                        energy_ = 100;
                        tiredWarned_ = false;
                        E().sfx.play(Sfx::Gulp, 0.6f);
                        note("Sister", "Here's your boba. You owe me.", col::gold);
                    } else if (t && t->app == Huiswerk && homework_ > 0.05f) {
                        susp_ = std::max(0.0f, susp_ - 15);
                        caption(c.who == Who::Papa ? "[ Dad: \"Good job, son.\" ]" : c.who == Who::Mama ? "[ Mom: \"Look at you, working hard!\" ]" : "[ Sister: \"Boring. Homework.\" ]");
                    } else {
                        susp_ += 5;
                        caption(c.who == Who::Zus ? "[ Sister: \"What are you even doing?\" ]" : "[ \"Hmm. What are you up to?\" ]");
                    }
                }
                if (c.t >= c.watch) {
                    c.stage = 3;
                    c.t = 0;
                    E().sfx.play(Sfx::Slam, 0.35f, 1.3f, -0.5f);
                }
                break;
            }
            default:
                if (c.t > 0.5f) {
                    c.on = false;
                    if (!c.delivery) nextCheck_ = rnd(20, 34) * (diff_ == 0 ? 1.3f : diff_ == 2 ? 0.75f : 1);
                }
                break;
        }
    }

    // how open the door is (0..1), for the webcam and the light spill
    float doorOpen() const {
        if (!check_.on) return 0;
        if (check_.stage == 1) return easeOutCubic(check_.t / 0.55f);
        if (check_.stage == 2) return 1;
        if (check_.stage == 3) return 1 - clamp01(check_.t / 0.4f);
        return 0;
    }

    // ============================================================ input
    void handleInput() {
        bool pressed = E().mousePressed();
        // dragging a window
        if (drag_ >= 0) {
            int i = indexOf(drag_);
            if (i >= 0 && E().mouseDown()) {
                Win& w = wins_[i];
                w.r.x = std::clamp(cur_.x - dragOff_.x, S.x - w.r.width + 60, S.x + S.width - 60);
                w.r.y = std::clamp(cur_.y - dragOff_.y, S.y, S.y + S.height - kTask - kTitle);
            } else {
                drag_ = -1;
            }
        }
        // keyboard goes to the focused window
        Win* tw = top();
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (!tw) continue;
            if (tw->app == Huiswerk) typeHomework();
            if (tw->app == Loader && intr_ == Intr::Captcha && captchaIn_.size() < 8 && ch < 128) captchaIn_ += (char)std::toupper(ch);
        }
        if (tw && tw->app == Loader && intr_ == Intr::Captcha && IsKeyPressed(KEY_BACKSPACE) && !captchaIn_.empty()) captchaIn_.pop_back();
        if (tw && tw->app == Loader && intr_ == Intr::Captcha && IsKeyPressed(KEY_ENTER)) submitCaptcha();
        if (!pressed) return;
        E().consumeClick();
        // things in the room
        if (CheckCollisionPointRec(cur_, kRoomWindow)) {
            if (blinds_ < 0.9f) {
                blinds_ = 1;
                E().sfx.play(Sfx::Whoosh, 0.5f, 0.7f, 0.9f);
            }
            return;
        }
        if (!CheckCollisionPointRec(cur_, S)) return;
        // taskbar
        if (cur_.y > S.y + S.height - kTask) {
            taskbarClick();
            return;
        }
        // windows, front to back
        for (int i = (int)wins_.size() - 1; i >= 0; i--) {
            Win& w = wins_[i];
            if (w.min || !CheckCollisionPointRec(cur_, w.r)) continue;
            int id = w.id;
            focus(id);
            Win& f = wins_.back();
            if (cur_.y < f.r.y + kTitle) {
                // title bar buttons: close, maximise, minimise (right to left)
                float bx = f.r.x + f.r.width - 30;
                if (cur_.x > bx) {
                    if (f.app == Update) {  // closing the update nag = "later"
                        updateOn_ = false;
                        nextUpdate_ = rnd(45, 70);
                    }
                    close(f.id);
                    return;
                }
                if (f.app != Popup && f.app != Update && cur_.x > bx - 30) {
                    toggleMax(f);
                    return;
                }
                if (f.app != Popup && f.app != Update && cur_.x > bx - 60) {
                    minimize(f);
                    return;
                }
                if (!f.max) {
                    drag_ = f.id;
                    dragOff_ = cur_ - Vector2{f.r.x, f.r.y};
                }
                return;
            }
            clickId_ = f.id;  // content click, handled while drawing the app
            clickPos_ = cur_;
            return;
        }
        // desktop icons (double click)
        for (int a = 0; a < 7; a++) {
            if (CheckCollisionPointRec(cur_, iconRect(a))) {
                if (lastIcon_ == a && t_ - lastIconT_ < 0.45f) {
                    open((App)a);
                    lastIcon_ = -1;
                } else {
                    lastIcon_ = a;
                    lastIconT_ = t_;
                    E().sfx.play(Sfx::Click, 0.2f, 1.4f);
                }
                return;
            }
        }
        lastIcon_ = -1;
    }

    Rectangle iconRect(int a) const { return {S.x + 14, S.y + 14 + a * 72.0f, 70, 66}; }

    std::vector<int> taskOrder() const {
        std::vector<int> ids;
        for (auto& w : wins_) ids.push_back(w.id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }
    Rectangle taskRect(int slot) const { return {S.x + 56 + slot * 128.0f, S.y + S.height - kTask + 4, 122, kTask - 8}; }

    void taskbarClick() {
        auto ids = taskOrder();
        for (size_t k = 0; k < ids.size(); k++) {
            if (!CheckCollisionPointRec(cur_, taskRect((int)k))) continue;
            int i = indexOf(ids[k]);
            Win& w = wins_[i];
            Win* t = top();
            if (!w.min && t && t->id == w.id) minimize(w);
            else {
                w.min = false;
                focus(w.id);
                E().sfx.play(Sfx::Click, 0.25f, 1.2f);
            }
            return;
        }
    }

    void typeHomework() {
        homework_ = std::min(1.0f, homework_ + 0.006f);
        E().sfx.play(Sfx::Key, 0.3f, rnd(0.9f, 1.15f));
        keyFlash_ = 0.12f;
    }

    void submitCaptcha() {
        if (captchaIn_ == captcha_) {
            intr_ = Intr::None;
            E().sfx.play(Sfx::Coin, 0.5f, 1.3f);
        } else {
            captchaIn_.clear();
            E().sfx.play(Sfx::Error, 0.5f);
        }
    }

    // immediate-mode button for app contents (uses the virtual cursor and this frame's click)
    bool btn(const Win& w, Rectangle r, const std::string& label, Color c, float size = 15, bool enabled = true) {
        bool hover = enabled && CheckCollisionPointRec(cur_, r) && top() && top()->id == w.id;
        Color base = enabled ? (hover ? lighten(c, 0.15f) : c) : rgba(0x4a4a5aff);
        DrawRectangleRounded({r.x, r.y + 3, r.width, r.height}, 0.3f, 8, darken(base, 0.45f));
        DrawRectangleRounded(r, 0.3f, 8, base);
        DrawRectangleRounded({r.x + 3, r.y + 2, r.width - 6, r.height * 0.4f}, 0.4f, 8, alpha(WHITE, 0.15f));
        ui::text(label, r.x + r.width / 2, r.y + r.height / 2, size, enabled ? WHITE : rgba(0x9a9aaaff), ui::Center, true);
        if (enabled && clickId_ == w.id && CheckCollisionPointRec(clickPos_, r)) {
            clickId_ = -1;
            E().sfx.play(Sfx::Click, 0.35f, 1.1f);
            return true;
        }
        return false;
    }

    // ============================================================ draw: room
    void drawRoom() {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x1c1230ff), rgba(0x0e0818ff));
        // poster + shelf on the left wall
        Rectangle poster{20, 120, 110, 150};
        DrawRectangleRec(poster, rgba(0x2a1a40ff));
        ui::text("PIETER", poster.x + 55, poster.y + 50, 22, col::pink, ui::Center, true);
        ui::text("GAME 3", poster.x + 55, poster.y + 80, 22, col::gold, ui::Center, true);
        art::pieter({poster.x + 55, poster.y + 118}, 22, "");
        // the window on the right, with whatever is outside
        Rectangle w = kRoomWindow;
        DrawRectangleRec(shrink(w, -8), rgba(0x2a2040ff));
        E().beginClip(w);
        DrawRectangleGradientV((int)w.x, (int)w.y, (int)w.width, (int)w.height, rgba(0x0a1030ff), rgba(0x1a2050ff));
        for (int i = 0; i < 10; i++) DrawCircleV({w.x + std::fmod(i * 37.0f, w.width), w.y + std::fmod(i * 61.0f, w.height * 0.6f)}, 1.2f, alpha(WHITE, 0.6f));
        DrawCircleV({w.x + 80, w.y + 50}, 14, rgba(0xfff6d8ff));
        if (gor_.on || gorLeave_ > 0) {
            float rise = gor_.on ? clamp01(gor_.t / 0.8f) : gorLeave_;
            gorilla({w.x + w.width / 2, w.y + w.height - rise * 150 + 30}, 0.85f, gor_.banging ? 1 : 0, t_);
        }
        E().endClip();
        gorLeave_ = gor_.on ? 1 : std::max(0.0f, gorLeave_ - GetFrameTime() * 1.5f);
        DrawRectangleLinesEx(shrink(w, -8), 8, rgba(0xe9e2f2ff));
        DrawRectangleRec({w.x + w.width / 2 - 3, w.y, 6, w.height}, rgba(0xe9e2f2ff));
        // blinds
        float bh = w.height * blinds_;
        for (float y = w.y; y < w.y + bh; y += 10) {
            DrawRectangleRec({w.x, y, w.width, 8}, rgba(0xd8d0c0ff));
            DrawRectangleRec({w.x, y + 6, w.width, 2}, rgba(0x9a9080ff));
        }
        bool hov = CheckCollisionPointRec(cur_, w) && phase_ == Phase::Play;
        if (hov && blinds_ < 0.9f) ui::textShadow("CLOSE", w.x + w.width / 2, w.y + w.height + 24, 18, WHITE, ui::Center, true, 2);
        // desk
        DrawRectangle(0, 610, VW, 110, rgba(0x5a3a24ff));
        fx::tiled(E().tex("tex/wood"), {0, 612, (float)VW, 108}, 0.6f, {0, 0}, rgba(0xc89870ff));
        DrawRectangleGradientV(0, 610, VW, 30, alpha(BLACK, 0.4f), alpha(BLACK, 0));
        // keyboard
        Rectangle kb{340, 640, 520, 70};
        DrawRectangleRounded({kb.x, kb.y + 5, kb.width, kb.height}, 0.15f, 8, alpha(BLACK, 0.4f));
        DrawRectangleRounded(kb, 0.15f, 8, rgba(0x1a1622ff));
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 15; k++) {
                Rectangle key{kb.x + 12 + k * 33.0f, kb.y + 8 + r * 15.0f, 29, 12};
                bool lit = keyFlash_ > 0 && (k * 7 + r * 3 + (int)(t_ * 60)) % 9 == 0;
                DrawRectangleRounded(key, 0.3f, 4, lit ? col::violet : rgba(0x2a2534ff));
                DrawRectangleRec({key.x, key.y + 10, key.width, 2}, hue((t_ * 80 + k * 20), 0.6f));
            }
        // mouse
        DrawEllipse(930, 668, 26, 36, rgba(0x1a1622ff));
        DrawLineEx({930, 640}, {930, 660}, 2, rgba(0x3a3448ff));
        // boba cup = energy
        drawCup();
    }

    static Color hue(float h, float a) {
        Color c = ColorFromHSV(std::fmod(h, 360.0f), 0.7f, 1);
        return alpha(c, a);
    }

    void drawCup() {
        Rectangle c = kCup;
        fx::softShadow({c.x + c.width / 2, c.y + c.height}, 34, 8, 0.5f);
        float lvl = clamp01(energy_ / 100);
        // tea level inside a tapered cup
        float top = c.y + 8, bot = c.y + c.height;
        float ty = lerpf(bot - 4, top + 4, lvl);
        DrawTriangle({c.x, top}, {c.x + 6, bot}, {c.x + c.width - 6, bot}, alpha(WHITE, 0.25f));
        DrawTriangle({c.x, top}, {c.x + c.width - 6, bot}, {c.x + c.width, top}, alpha(WHITE, 0.25f));
        DrawRectangleRec({c.x + 5, ty, c.width - 10, bot - ty - 2}, lvl < 0.25f ? mix(col::boba, col::red, 0.4f + 0.3f * std::sin(t_ * 6)) : col::boba);
        for (int i = 0; i < 6; i++) DrawCircleV({c.x + 12 + (i % 3) * 14.0f, bot - 8 - (i / 3) * 9.0f}, 4.5f, rgba(0x2a1410ff));
        DrawRectangleRounded({c.x - 4, c.y + 2, c.width + 8, 8}, 1, 6, rgba(0xf4ecffff));
        DrawLineEx({c.x + c.width * 0.6f, c.y + 4}, {c.x + c.width * 0.75f, c.y - 34}, 6, col::pink);
        ui::text(TextFormat("%d%%", (int)energy_), c.x + c.width / 2, c.y - 46, 15, lvl < 0.25f ? col::red : col::text, ui::Center, true);
        ui::text("ENERGY", c.x + c.width / 2, c.y - 62, 11, col::muted, ui::Center, true);
    }

    // ============================================================ draw: desktop
    void drawDesktop() {
        // bezel
        Rectangle bz = shrink(S, -18);
        DrawRectangleRounded({bz.x, bz.y + 8, bz.width, bz.height}, 0.03f, 8, alpha(BLACK, 0.5f));
        DrawRectangleRounded(bz, 0.03f, 8, rgba(0x15121cff));
        DrawRectangleRounded(shrink(bz, 2), 0.03f, 8, rgba(0x1e1a28ff));
        ui::text("PieterTech", bz.x + bz.width / 2, bz.y + bz.height - 9, 11, alpha(col::muted, 0.6f), ui::Center, true);
        DrawCircleV({bz.x + bz.width / 2, bz.y + 9}, 4, rgba(0x0a0812ff));  // webcam
        if (rat_) DrawCircleV({bz.x + bz.width / 2 + 12, bz.y + 9}, 2.5f, std::fmod(t_, 0.6f) < 0.3f ? col::red : darken(col::red, 0.6f));
        // stand
        DrawRectangleRec({VW / 2.0f - 40, bz.y + bz.height, 80, 20}, rgba(0x15121cff));
        E().beginClip(S);
        // wallpaper
        DrawRectangleGradientEx(S, rgba(0x3a1a6aff), rgba(0x1a0a3aff), rgba(0x5a2a6aff), rgba(0x6a2a8aff));
        fx::beginAdd();
        fx::glow({S.x + S.width * 0.65f, S.y + S.height * 0.45f}, 320, alpha(col::pink, 0.2f));
        fx::endAdd();
        fx::gradientText("PieterOS", S.x + S.width * 0.65f, S.y + S.height * 0.42f, 70, alpha(WHITE, 0.25f), alpha(col::pink, 0.25f), 0);
        // icons
        for (int a = 0; a < 7; a++) {
            Rectangle r = iconRect(a);
            bool sel = lastIcon_ == a;
            if (sel) DrawRectangleRounded(r, 0.2f, 6, alpha(WHITE, 0.15f));
            Rectangle ic{r.x + 17, r.y + 4, 36, 36};
            DrawRectangleRounded({ic.x, ic.y + 3, ic.width, ic.height}, 0.3f, 6, alpha(BLACK, 0.4f));
            fx::gradRoundRect(ic, 0.3f, lighten(kAppCol[a], 0.2f), darken(kAppCol[a], 0.2f));
            ui::text(kIconLetter[a], ic.x + 18, ic.y + 18, 20, WHITE, ui::Center, true);
            std::string name = a == Loader ? "PieterHax" : kAppName[a];
            ui::text(name, r.x + 35, r.y + 54, 11, WHITE, ui::Center, true);
            if (a == Gram && unread_ > 0) {
                DrawCircleV({ic.x + 36, ic.y}, 9, col::red);
                ui::text(std::to_string(unread_), ic.x + 36, ic.y, 11, WHITE, ui::Center, true);
            }
        }
        // windows back to front
        for (size_t i = 0; i < wins_.size(); i++) {
            if (wins_[i].min) continue;
            drawWindow(wins_[i]);
        }
        drawTaskbar();
        drawNotes();
        // the hallway light spills over the screen when the door is open behind you
        float d = doorOpen();
        if (d > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleGradientH((int)S.x, (int)S.y, (int)S.width, (int)S.height, alpha(rgba(0xffd8a0ff), 0.12f * d), alpha(rgba(0xffd8a0ff), 0));
            EndBlendMode();
        }
        E().endClip();
        // glare
        DrawTriangle({S.x, S.y}, {S.x + 220, S.y}, {S.x, S.y + 260}, alpha(WHITE, 0.03f));
    }

    void drawWindow(Win& w) {
        int id = w.id;
        float k = easeOutBack(w.anim);
        Rectangle r = w.r;
        if (k < 1) {
            Vector2 c = center(r);
            r = {c.x - r.width * k / 2, c.y - r.height * k / 2, r.width * k, r.height * k};
        }
        bool focused = top() && top()->id == id;
        fx::softShadow({r.x + r.width / 2, r.y + r.height}, r.width * 0.55f, 18, 0.4f);
        DrawRectangleRounded({r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 0.03f, 6, focused ? alpha(WHITE, 0.4f) : alpha(BLACK, 0.5f));
        DrawRectangleRounded(r, 0.03f, 6, w.app == Loader ? rgba(0x0a140eff) : rgba(0xf4f2f8ff));
        // title bar
        Color tc = focused ? kAppCol[w.app] : darken(kAppCol[w.app], 0.35f);
        DrawRectangleRounded({r.x, r.y, r.width, kTitle + 6}, 0.3f, 6, tc);
        DrawRectangleRec({r.x, r.y + kTitle - 4, r.width, 4}, tc);
        std::string title = w.app == Popup ? "!!! WAARSCHUWING !!!" : kAppName[w.app];
        ui::text(title, r.x + 12, r.y + kTitle / 2, 14, WHITE, ui::Left, true);
        if (k < 0.98f) return;
        float bx = r.x + r.width - 30;
        bool hc = CheckCollisionPointRec(cur_, {bx, r.y, 30, kTitle});
        if (hc) DrawRectangleRec({bx, r.y + 2, 28, kTitle - 4}, col::red);
        DrawLineEx({bx + 9, r.y + 9}, {bx + 19, r.y + 19}, 2, WHITE);
        DrawLineEx({bx + 19, r.y + 9}, {bx + 9, r.y + 19}, 2, WHITE);
        if (w.app != Popup && w.app != Update) {
            DrawRectangleLinesEx({bx - 21, r.y + 9, 11, 10}, 1.5f, WHITE);
            DrawLineEx({bx - 50, r.y + 18}, {bx - 40, r.y + 18}, 2, WHITE);
        }
        Rectangle c{r.x, r.y + kTitle, r.width, r.height - kTitle};
        E().beginClip({std::max(c.x, S.x), std::max(c.y, S.y), std::min(c.x + c.width, S.x + S.width) - std::max(c.x, S.x), std::min(c.y + c.height, S.y + S.height) - std::max(c.y, S.y)});
        switch (w.app) {
            case Loader: appLoader(w, c); break;
            case Huiswerk: appHomework(w, c); break;
            case Cam: appCam(w, c); break;
            case Gram: appGram(w, c); break;
            case Boba: appBoba(w, c); break;
            case Defender: appDefender(w, c); break;
            case Trash: appTrash(w, c); break;
            case Popup: appPopup(w, c); break;
            case Update: appUpdate(w, c); break;
            default: break;
        }
        E().endClip();
        E().beginClip(S);
    }

    void drawTaskbar() {
        Rectangle tb{S.x, S.y + S.height - kTask, S.width, kTask};
        DrawRectangleRec(tb, alpha(rgba(0x0e0a18ff), 0.92f));
        DrawLineEx({tb.x, tb.y}, {tb.x + tb.width, tb.y}, 1, alpha(WHITE, 0.15f));
        fx::gradRoundRect({tb.x + 8, tb.y + 5, 40, kTask - 10}, 0.3f, col::pink, col::purple);
        ui::text("P", tb.x + 28, tb.y + kTask / 2, 16, WHITE, ui::Center, true);
        auto ids = taskOrder();
        Win* t = top();
        for (size_t k = 0; k < ids.size(); k++) {
            const Win& w = wins_[indexOf(ids[k])];
            Rectangle r = taskRect((int)k);
            bool active = t && t->id == w.id;
            DrawRectangleRounded(r, 0.3f, 6, active ? alpha(WHITE, 0.18f) : alpha(WHITE, CheckCollisionPointRec(cur_, r) ? 0.1f : 0.05f));
            DrawRectangleRounded({r.x + 6, r.y + 5, 16, 16}, 0.3f, 4, kAppCol[w.app]);
            std::string n = w.app == Popup ? "!!!" : kAppName[w.app];
            if (n.size() > 13) n = n.substr(0, 12) + ".";
            ui::text(n, r.x + 28, r.y + r.height / 2, 12, w.min ? col::muted : WHITE, ui::Left, true);
            if (!w.min) DrawRectangleRec({r.x + r.width / 2 - 12, r.y + r.height - 2, 24, 2}, kAppCol[w.app]);
        }
        // tray: clock + wifi
        int mins = 22 * 60 + 47 + (int)(playT_ / 4);
        ui::text(TextFormat("%02d:%02d", (mins / 60) % 24, mins % 60), tb.x + tb.width - 14, tb.y + kTask / 2, 14, WHITE, ui::Right, true);
        for (int i = 0; i < 3; i++) DrawRing({tb.x + tb.width - 78, tb.y + 24}, 4 + i * 5.0f, 6 + i * 5.0f, 225, 315, 8, alpha(WHITE, rat_ && i == 2 ? 0.3f : 0.8f));
    }

    void drawNotes() {
        float y = S.y + S.height - kTask - 10;
        for (int i = (int)notes_.size() - 1; i >= 0; i--) {
            auto& n = notes_[i];
            float slide = (1 - easeOutCubic(n.t / 0.3f)) * 300 + std::max(0.0f, n.t - 5) * 600;
            Rectangle r{S.x + S.width - 300 + slide, y - 64, 290, 60};
            DrawRectangleRounded(r, 0.2f, 8, alpha(rgba(0x1a1428ff), 0.95f));
            DrawRectangleRounded({r.x, r.y, 5, r.height}, 1, 4, n.c);
            ui::text(n.title, r.x + 14, r.y + 15, 13, n.c, ui::Left, true);
            ui::paragraph(n.text, {r.x + 14, r.y + 26, r.width - 22, 30}, 12, WHITE);
            y -= 68;
        }
    }

    // ------------------------------------------------------------ apps
    void appLoader(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0x08120cff));
        for (int i = 0; i < 18; i++) {  // matrix rain
            float x = c.x + std::fmod(i * 53.0f, c.width), y = c.y + std::fmod(t_ * (40 + i * 7) + i * 90, c.height);
            ui::text(TextFormat("%c", 'A' + (i * 7 + (int)(t_ * 10)) % 26), x, y, 12, alpha(col::green, 0.15f), ui::Left, false);
        }
        ui::text("PieterHax v6.9 (all games, undetected)", c.x + 16, c.y + 20, 14, col::green, ui::Left, true);
        float p = pct();
        Rectangle bar{c.x + 16, c.y + 46, c.width - 32, 26};
        DrawRectangleRec(bar, rgba(0x0a2a14ff));
        DrawRectangleRec({bar.x, bar.y, bar.width * p, bar.height}, intr_ != Intr::None ? rgba(0xc8a020ff) : col::green);
        for (float x = bar.x - 30 + std::fmod(t_ * 40, 30.0f); x < bar.x + bar.width * p; x += 30)
            DrawLineEx({std::max(bar.x, x), bar.y + bar.height}, {std::min(bar.x + bar.width * p, x + 14), bar.y}, 6, alpha(WHITE, 0.12f));
        DrawRectangleLinesEx(bar, 1.5f, col::green);
        ui::text(TextFormat("%d%%", (int)(p * 100)), bar.x + bar.width / 2, bar.y + 13, 15, WHITE, ui::Center, true);
        ui::text(TextFormat("%.0f / %.0f MB   %s", got_, total_, intr_ != Intr::None ? "PAUSED" : got_ >= total_ ? "DONE" : TextFormat("%.1f MB/s", speed_)),
                 c.x + 16, c.y + 88, 13, col::green, ui::Left, false);
        float y = c.y + 108;
        if (got_ >= total_) {
            if (installT_ < 0) {
                if (btn(w, {c.x + c.width / 2 - 90, y + 20, 180, 44}, "INSTALL", rgba(0x1a8a3aff), 18)) {
                    installT_ = 0;
                    E().sfx.play(Sfx::Power, 0.6f);
                }
            } else {
                ui::text("Installing... do not turn off!", c.x + 16, y + 20, 15, col::gold, ui::Left, true);
                DrawRectangleRec({c.x + 16, y + 40, (c.width - 32) * clamp01(installT_ / 6), 10}, col::gold);
            }
            return;
        }
        Rectangle panel{c.x + 12, y, c.width - 24, c.height - (y - c.y) - 12};
        if (intr_ == Intr::None) {
            const char* files[] = {"aimbot.dll", "wallhack.sys", "pieter_skins.pak", "infinite_boba.exe", "juhuh.cfg"};
            for (int i = 0; i < 5; i++) {
                bool done = p > (i + 1) / 6.0f;
                ui::text(TextFormat("%s %s", done ? "[OK]" : "[..]", files[i]), panel.x + 6, panel.y + 12 + i * 20, 13, done ? col::green : alpha(col::green, 0.5f), ui::Left, false);
            }
            return;
        }
        DrawRectangleRounded(panel, 0.1f, 6, rgba(0x2a1a0aff));
        DrawRectangleRoundedLinesEx(panel, 0.1f, 6, 1.5f, col::orange);
        switch (intr_) {
            case Intr::Lost: {
                ui::text("Connection lost.", panel.x + 14, panel.y + 18, 15, col::orange, ui::Left, true);
                Rectangle b{panel.x + (panel.width - 150) * resumeBtn_.x, panel.y + 40 + (panel.height - 90) * (resumeBtn_.y - 0.55f) * 2, 150, 36};
                if (btn(w, b, "RESUME", rgba(0x1a8a3aff))) intr_ = Intr::None;
                break;
            }
            case Intr::Captcha: {
                ui::text("Prove you are not a robot:", panel.x + 14, panel.y + 16, 14, col::orange, ui::Left, true);
                Rectangle cap{panel.x + 14, panel.y + 32, 170, 46};
                DrawRectangleRec(cap, rgba(0xe8e0d0ff));
                for (int i = 0; i < 6; i++) DrawLineEx({cap.x, cap.y + hashf(i) * cap.height}, {cap.x + cap.width, cap.y + hashf(i + 9) * cap.height}, 2, alpha(rgba(0x6a4a8aff), 0.5f));
                for (size_t i = 0; i < captcha_.size(); i++)
                    ui::textRotated(std::string(1, captcha_[i]), {cap.x + 22 + i * 30.0f, cap.y + 10 + hashf(i * 3.0f) * 12}, 26, (hashf(i + 4.0f) - 0.5f) * 40, rgba(0x2a1a4aff), true);
                Rectangle in{panel.x + 200, panel.y + 38, panel.width - 214, 34};
                DrawRectangleRec(in, rgba(0x0a140eff));
                DrawRectangleLinesEx(in, 1.5f, col::green);
                ui::text(captchaIn_ + (std::fmod(t_, 1) < 0.5f ? "_" : ""), in.x + 8, in.y + 17, 18, col::green, ui::Left, true);
                ui::text("type the code, ENTER", panel.x + 200, panel.y + 86, 11, col::muted, ui::Left, true);
                if (btn(w, {panel.x + 14, panel.y + 88, 110, 28}, "OK", rgba(0x1a8a3aff), 13)) submitCaptcha();
                break;
            }
            case Intr::Blocked:
                ui::text("PieterDefender has blocked PieterHax.", panel.x + 14, panel.y + 18, 14, col::orange, ui::Left, true);
                ui::text("Open PieterDefender and click ALLOW.", panel.x + 14, panel.y + 40, 13, WHITE, ui::Left, false);
                if (btn(w, {panel.x + 14, panel.y + 62, 190, 32}, "Open PieterDefender", kAppCol[Defender], 13)) open(Defender);
                break;
            case Intr::DiskFull:
                ui::text("Drive C: is full (0 KB free).", panel.x + 14, panel.y + 18, 15, col::orange, ui::Left, true);
                ui::text("Empty the recycle bin to continue.", panel.x + 14, panel.y + 40, 13, WHITE, ui::Left, false);
                if (btn(w, {panel.x + 14, panel.y + 62, 170, 32}, "Open Recycle Bin", kAppCol[Trash], 13)) open(Trash);
                break;
            default: break;
        }
    }

    static float hashf(float n) { return std::fmod(std::abs(std::sin(n * 12.9898f) * 43758.5453f), 1.0f); }

    void appHomework(Win& w, Rectangle c) {
        (void)w;
        DrawRectangleRec(c, rgba(0xe8eaf0ff));
        DrawRectangleRec({c.x, c.y, c.width, 26}, rgba(0xdde0eaff));
        const char* tools[] = {"B", "I", "U", "Aa", "1."};
        for (int i = 0; i < 5; i++) ui::text(tools[i], c.x + 16 + i * 30, c.y + 13, 13, rgba(0x3a3a4aff), ui::Left, true);
        Rectangle page{c.x + 30, c.y + 36, c.width - 60, c.height + 200};
        DrawRectangleRec(page, WHITE);
        ui::text("Math - Chapter 7", page.x + 24, page.y + 26, 18, rgba(0x1a1a2aff), ui::Left, true);
        static const char* body =
            "Exercise 1. Solve: 2x + 7 = 19. First I subtract 7 from both sides, which gives 2x = 12, so x = 6. "
            "Exercise 2. A stroopwafel is 9 cm across. Work out the circumference: pi times 9 is about 28.3 cm. "
            "Exercise 3. Davi bikes at 15 km per hour. How long does 6 km take him? 6 divided by 15 is 0.4 hours, that's 24 minutes. "
            "Exercise 4. A boba costs 6.50 euros. Kira buys three. That's 19.50 euros. Exercise 5. Draw the graph of y = x squared. "
            "Exercise 6. The horse runs 60 km per hour and yells JUHUH. Exercise 7. Prove that Pieter is the best. Proof: he just is.";
        std::string s(body);
        size_t n = (size_t)(homework_ * s.size());
        float h = ui::paragraph(s.substr(0, n) + (std::fmod(t_, 1) < 0.5f ? "|" : ""), {page.x + 24, page.y + 48, page.width - 48, 0}, 14, rgba(0x2a2a3aff));
        (void)h;
        if (homework_ < 0.02f) ui::text("(type anything to 'do' your homework)", page.x + 24, page.y + 60, 13, rgba(0x9a9aaaff), ui::Left, false);
        DrawRectangleRec({c.x, c.y + c.height - 20, c.width, 20}, rgba(0xdde0eaff));
        ui::text(TextFormat("Page 1 of 1   %d words", (int)(n / 6)), c.x + 10, c.y + c.height - 10, 11, rgba(0x5a5a6aff), ui::Left, false);
    }

    void appCam(Win& w, Rectangle c) {
        (void)w;
        // the webcam sees you, and the door behind you
        DrawRectangleRec(c, rgba(0x1a1424ff));
        Vector2 door{c.x + c.width * 0.5f, c.y + c.height - 16};
        Rectangle frame{door.x - 34, door.y - 120, 68, 120};
        float d = doorOpen();
        DrawRectangleRec(shrink(frame, -5), rgba(0x5a4a3aff));
        if (d > 0) {
            DrawRectangleRec(frame, rgba(0xffe0a8ff));
            if (check_.on && check_.stage >= 1) person(check_.who, {door.x, door.y}, 0.55f, check_.stage == 2 ? 0.3f : 0, t_);
        } else {
            DrawRectangleRec(frame, rgba(0x2a2018ff));
        }
        // the door leaf swinging open
        float lw = frame.width * (1 - d);
        DrawRectangleRec({frame.x, frame.y, lw, frame.height}, rgba(0x8a6a4aff));
        if (lw > 8) DrawCircleV({frame.x + lw - 8, frame.y + 64}, 3, col::gold);
        // warning signs: a shadow under the door, the handle moving
        if (check_.on && check_.stage == 0) {
            float k = clamp01(check_.t / check_.warn);
            if (k > 0.45f) DrawRectangleRec({frame.x, frame.y + frame.height - 5, frame.width, 5}, alpha(rgba(0xffd890ff), 0.5f * (1 - k) + 0.2f));
            if (k > 0.55f) DrawRectangleRec({frame.x + 10 + std::sin(t_ * 9) * 4, frame.y + frame.height - 5, 30, 5}, alpha(BLACK, 0.8f));
            if (k > 0.8f) DrawLineEx({frame.x + lw - 8, frame.y + 64}, {frame.x + lw - 18, frame.y + 64 + std::sin(t_ * 20) * 4}, 3, col::gold);
        }
        // you, in front
        art::pieter({c.x + c.width * 0.5f, c.y + c.height - 34}, 38, P().hat);
        // REC + noise
        DrawCircleV({c.x + 14, c.y + 14}, 5, std::fmod(t_, 1) < 0.5f ? col::red : darken(col::red, 0.5f));
        ui::text("LIVE", c.x + 24, c.y + 14, 11, WHITE, ui::Left, true);
        for (int i = 0; i < 30; i++) DrawPixelV({c.x + hashf(i + t_) * c.width, c.y + hashf(i * 2 + t_) * c.height}, alpha(WHITE, 0.4f));
        if (check_.on && check_.stage == 0 && check_.t / check_.warn > 0.45f && std::fmod(t_, 0.5f) < 0.3f)
            ui::textShadow("!", c.x + c.width - 18, c.y + 22, 26, col::red, ui::Center, true, 2);
    }

    void appGram(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0xfaf4faff));
        unread_ = 0;
        float y = c.y + c.height - 10;
        // newest at the bottom; answer buttons for the newest unanswered message
        Msg* pending = nullptr;
        for (auto& m : msgs_)
            if (!m.mine && !m.answered && !m.replies.empty()) pending = &m;
        if (pending) {
            float by = y - pending->replies.size() * 34;
            for (size_t i = 0; i < pending->replies.size(); i++) {
                Rectangle r{c.x + 10, by + i * 34, c.width - 20, 28};
                if (btn(w, r, pending->replies[i].text, kAppCol[Gram], 13)) {
                    Reply rep = pending->replies[i];
                    answer(*pending, rep);
                    return;
                }
            }
            if (pending->deadline > 0) ui::text(TextFormat("answer within %ds!", (int)pending->deadline + 1), c.x + c.width - 10, by - 10, 11, col::red, ui::Right, true);
            y = by - 22;
        }
        for (int i = (int)msgs_.size() - 1; i >= 0 && y > c.y; i--) {
            auto& m = msgs_[i];
            float wdt = std::min(c.width - 60, ui::measure(m.text, 13) + 24);
            float h = ui::paragraph(m.text, {0, 0, wdt - 20, 0}, 13, WHITE, false) + 26;
            y -= h + 8;
            Rectangle b{m.mine ? c.x + c.width - wdt - 10 : c.x + 10, y, wdt, h};
            DrawRectangleRounded(b, 0.25f, 8, m.mine ? kAppCol[Gram] : rgba(0xe8e0ecff));
            ui::text(m.from, b.x + 10, b.y + 10, 11, m.mine ? alpha(WHITE, 0.8f) : kAppCol[Gram], ui::Left, true);
            ui::paragraph(m.text, {b.x + 10, b.y + 20, wdt - 20, 0}, 13, m.mine ? WHITE : rgba(0x2a1a2aff));
        }
        if (msgs_.empty()) ui::text("No messages", c.x + c.width / 2, c.y + 40, 14, col::muted, ui::Center, true);
    }

    void appBoba(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0xfff4e8ff));
        ui::text("BobaDelivery", c.x + 16, c.y + 22, 20, kAppCol[Boba], ui::Left, true);
        ui::text(TextFormat("Balance: EUR %.2f", money_), c.x + c.width - 16, c.y + 22, 13, rgba(0x5a3a2aff), ui::Right, true);
        const char* names[] = {"Brown Sugar", "Taro", "Matcha"};
        const Color teas[] = {col::boba, rgba(0xb08ad8ff), rgba(0x8ac86aff)};
        for (int i = 0; i < 3; i++) {
            Rectangle r{c.x + 16 + i * ((c.width - 32) / 3), c.y + 46, (c.width - 32) / 3 - 8, 130};
            bool sel = flavour_ == i;
            DrawRectangleRounded(r, 0.15f, 8, sel ? lighten(kAppCol[Boba], 0.6f) : WHITE);
            DrawRectangleRoundedLinesEx(r, 0.15f, 8, sel ? 3 : 1, sel ? kAppCol[Boba] : rgba(0xe0d0c0ff));
            art::boba({r.x + r.width / 2, r.y + 54}, 52, teas[i]);
            ui::text(names[i], r.x + r.width / 2, r.y + 110, 12, rgba(0x3a2a2aff), ui::Center, true);
            if (clickId_ == w.id && CheckCollisionPointRec(clickPos_, r)) {
                flavour_ = i;
                clickId_ = -1;
                E().sfx.play(Sfx::Click, 0.3f);
            }
        }
        float y = c.y + 190;
        if (delivery_ > 0) {
            ui::text(TextFormat("Driver is on the way... %ds", (int)delivery_ + 1), c.x + 16, y + 16, 15, kAppCol[Boba], ui::Left, true);
            DrawRectangleRounded({c.x + 16, y + 34, (c.width - 32), 10}, 1, 6, rgba(0xf0e0d0ff));
            DrawRectangleRounded({c.x + 16, y + 34, (c.width - 32) * (1 - delivery_ / 28), 10}, 1, 6, kAppCol[Boba]);
            return;
        }
        bool can = money_ >= 6.5f;
        if (btn(w, {c.x + 16, y + 10, c.width - 32, 42}, can ? "ORDER  -  EUR 6.50" : "Not enough money", kAppCol[Boba], 16, can)) {
            money_ -= 6.5f;
            delivery_ = 28;
            E().sfx.play(Sfx::Coin, 0.5f);
            note("BobaDelivery", "Order placed! Your sister will bring it upstairs.", kAppCol[Boba]);
        }
    }

    void appDefender(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0xeef6fcff));
        bool scanning = scanT_ >= 0;
        Color st = rat_ ? col::red : col::green;
        DrawCircleV({c.x + 50, c.y + 50}, 26, st);
        ui::text(rat_ ? "!" : "OK", c.x + 50, c.y + 50, 20, WHITE, ui::Center, true);
        ui::text(rat_ ? "Threat found: RAT" : "Your PC is protected", c.x + 90, c.y + 40, 16, rgba(0x1a2a3aff), ui::Left, true);
        ui::text(rat_ ? "Someone is controlling your mouse!" : "No threats", c.x + 90, c.y + 62, 12, rgba(0x5a6a7aff), ui::Left, false);
        if (scanning) {
            scanT_ += GetFrameTime();
            DrawRectangleRounded({c.x + 20, c.y + 100, c.width - 40, 14}, 1, 6, rgba(0xd0e0eaff));
            DrawRectangleRounded({c.x + 20, c.y + 100, (c.width - 40) * clamp01(scanT_ / 4.5f), 14}, 1, 6, kAppCol[Defender]);
            ui::text("Scanning...", c.x + 20, c.y + 128, 13, rgba(0x1a2a3aff), ui::Left, true);
            if (scanT_ >= 4.5f) {
                scanT_ = -1;
                if (rat_) {
                    rat_ = false;
                    std::erase_if(wins_, [](const Win& x) { return x.app == Popup; });
                    E().sfx.play(Sfx::Perfect, 0.6f);
                    note("PieterDefender", "RAT removed. Your mouse is yours again.", col::green);
                }
            }
        } else if (btn(w, {c.x + 20, c.y + 96, 150, 36}, "SCAN NOW", kAppCol[Defender])) {
            scanT_ = 0;
        }
        if (intr_ == Intr::Blocked) {
            Rectangle r{c.x + 14, c.y + c.height - 80, c.width - 28, 66};
            DrawRectangleRounded(r, 0.15f, 6, rgba(0xfff0d8ff));
            ui::text("Blocked: PieterHax.exe", r.x + 10, r.y + 16, 13, col::orange, ui::Left, true);
            if (btn(w, {r.x + r.width - 120, r.y + 24, 110, 34}, "ALLOW", col::orange, 13)) {
                intr_ = Intr::None;
                note("PieterDefender", "PieterHax.exe allowed. (Dangerous!)", col::orange);
            }
        }
    }

    void appTrash(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0xf4f2f8ff));
        const char* files[] = {"old_memes (2.1 GB)", "fortnite_clips (3.4 GB)", "juhuh_remix_final_FINAL.mp3", "homework_real.docx"};
        bool full = trashFull_;
        for (int i = 0; i < 4 && full; i++) ui::text(files[i], c.x + 20, c.y + 24 + i * 22, 13, rgba(0x2a2a3aff), ui::Left, false);
        if (!full) ui::text("The recycle bin is empty.", c.x + 20, c.y + 30, 14, rgba(0x6a6a7aff), ui::Left, true);
        if (btn(w, {c.x + 20, c.y + c.height - 56, 170, 38}, "EMPTY", rgba(0xc8283aff), 15, full)) {
            trashFull_ = false;
            E().sfx.play(Sfx::Whoosh, 0.6f, 0.6f);
            if (intr_ == Intr::DiskFull) intr_ = Intr::None;
        }
    }

    void appPopup(Win& w, Rectangle c) {
        DrawRectangleRec(c, std::fmod(t_ + w.id, 0.4f) < 0.2f ? rgba(0xffe0e0ff) : rgba(0xfff0c0ff));
        ui::paragraph(w.msg, {c.x + 14, c.y + 14, c.width - 28, 0}, 15, rgba(0x8a0a1aff));
        ui::text("close this window (X) or scan with PieterDefender", c.x + 14, c.y + c.height - 16, 11, rgba(0x5a2a2aff), ui::Left, false);
    }

    void appUpdate(Win& w, Rectangle c) {
        DrawRectangleRec(c, rgba(0xeef2fcff));
        ui::text("PieterOS needs to restart", c.x + 16, c.y + 24, 16, rgba(0x1a2a4aff), ui::Left, true);
        ui::text(TextFormat("Restarting automatically in %d seconds.", (int)updateT_ + 1), c.x + 16, c.y + 48, 13, rgba(0x4a5a7aff), ui::Left, false);
        if (btn(w, {c.x + 16, c.y + c.height - 52, 160, 36}, "LATER", kAppCol[Update])) {
            updateOn_ = false;
            nextUpdate_ = rnd(45, 70);
            close(w.id);
            return;
        }
        if (btn(w, {c.x + c.width - 176, c.y + c.height - 52, 160, 36}, "Restart now", rgba(0x8a8a9aff))) forcedRestart();
    }

    // ============================================================ draw: overlays
    void drawCursor() {
        Vector2 p = cur_;
        Vector2 a = p, b{p.x, p.y + 20}, c{p.x + 14, p.y + 14};
        DrawTriangle(a, b, c, col::outline);
        DrawTriangle({a.x + 1.5f, a.y + 3.5f}, {b.x + 1.5f, b.y - 3}, {c.x - 3, c.y - 1}, rat_ ? col::red : WHITE);
        if (rat_) ui::text("RAT", p.x + 14, p.y + 26, 11, col::red, ui::Left, true);
    }

    void drawHudBar() {
        // suspicion meter above the monitor
        Rectangle r{20, 20, 120, 12};
        ui::text("SUSPICION", r.x, r.y - 2, 11, col::muted, ui::Left, true);
        Rectangle bar{20, 34, 120, 14};
        DrawRectangleRounded(bar, 1, 6, col::bgDeep);
        float k = clamp01(susp_ / 100);
        if (k > 0.01f) DrawRectangleRounded({bar.x, bar.y, bar.width * k, bar.height}, 1, 6, mix(col::gold, col::red, k));
        ui::text(TextFormat("%d%%", (int)susp_), bar.x + bar.width + 6, bar.y + 7, 12, k > 0.7f ? col::red : WHITE, ui::Left, true);
        // the download, always visible on the left
        ui::text("PIETERHAX", 20, 70, 11, col::muted, ui::Left, true);
        DrawRectangleRounded({20, 82, 120, 14}, 1, 6, col::bgDeep);
        DrawRectangleRounded({20, 82, 120 * pct(), 14}, 1, 6, intr_ != Intr::None ? col::orange : col::green);
        ui::text(TextFormat("%d%%", (int)(pct() * 100)), 146, 89, 12, WHITE, ui::Left, true);
        if (intr_ != Intr::None && std::fmod(t_, 0.8f) < 0.5f) ui::text("PAUSED", 20, 108, 11, col::orange, ui::Left, true);
        if (captionT_ > 0) {
            float a = clamp01(captionT_ / 0.4f);
            float wdt = ui::measure(captionText_, 16, true) + 30;
            DrawRectangleRounded({VW / 2.0f - wdt / 2, S.y + S.height + 30, wdt, 30}, 0.5f, 8, alpha(BLACK, 0.7f * a));
            ui::text(captionText_, VW / 2.0f, S.y + S.height + 45, 16, alpha(col::text, a), ui::Center, true);
        }
    }

    void drawTitle() {
        art::purpleBackdrop(t_);
        Rectangle p{VW / 2.0f - 380, 60, 760, 600};
        ui::panel(p, alpha(col::panel, 0.96f), 0.06f, alpha(col::green, 0.6f));
        fx::gradientText("CHEAT DOWNLOADER", VW / 2.0f, p.y + 56, 56, rgba(0xc0ffd0ff), col::green, 4);
        ui::text("Get PieterHax onto the PC. Don't let anyone find out.", VW / 2.0f, p.y + 104, 18, col::muted, ui::Center, true);
        const char* rules[] = {
            "Drag windows by the title bar. _ minimises, the square maximises, X closes. Double-click desktop icons.",
            "Footsteps? Look at PieterCam: the door is behind you. Minimise PieterHax and every pop-up before it opens.",
            "Homework in front when they look = bonus. Type anything in Homework.docx.",
            "Download stuck? Resume it, type the captcha, allow it in PieterDefender or empty the recycle bin.",
            "Gorilla at the window: click the window to close the blinds before the noise wakes the house.",
            "RAT: your mouse is not yours anymore. Scan with PieterDefender.",
            "Boba = energy. Order more in BobaDelivery. Answer Mom on Pietergram!",
        };
        for (int i = 0; i < 7; i++) ui::paragraph(rules[i], {p.x + 50, p.y + 140 + i * 46.0f, p.width - 100, 0}, 15, col::text);
        const char* diffs[] = {"EASY", "NORMAL", "DAD MODE"};
        const Color dc[] = {rgba(0x2a8a4aff), rgba(0xd8822aff), rgba(0xc81a2aff)};
        for (int i = 0; i < 3; i++)
            if (ui::button({p.x + 40 + i * 230.0f, p.y + p.height - 100, 220, 62}, diffs[i], dc[i], 22)) start(i);
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawCaught() {
        float k = easeOutBack(clamp01(phaseT_ / 0.25f));
        DrawRectangle(0, 0, VW, VH, alpha(rgba(0x2a0008ff), 0.75f));
        Vector2 feet{VW / 2.0f, VH + 210.0f};
        person(caughtWho_, feet, 2.6f * k, 1, t_);
        ui::textOutline(caughtLine_, VW / 2.0f, 110, 60 * k, col::red, col::outline, 6);
        ui::textShadow(TextFormat("SUSPICION %d%%", (int)susp_), VW / 2.0f, 170, 26, WHITE, ui::Center, true, 3);
        ui::text("(PieterHax got closed and the download lost some progress)", VW / 2.0f, 205, 16, col::muted, ui::Center, true);
    }

    void drawEnd(bool won) {
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.8f));
        Rectangle p{VW / 2.0f - 320, 150, 640, 400};
        float k = easeOutBack(clamp01(phaseT_ / 0.4f));
        p.y += (1 - k) * 60;
        ui::panel(p, alpha(col::panel, 0.97f), 0.08f, alpha(won ? col::green : col::red, 0.6f));
        ui::textOutline(won ? "INSTALLED!" : "BUSTED!", VW / 2.0f, p.y + 66, 64, won ? rgba(0xc0ffd0ff) : rgba(0xffb0b0ff), col::outline, 5);
        ui::text(won ? "PieterHax is installed. Now you just need to get good." : loseWhy_, VW / 2.0f, p.y + 124, 19, col::muted, ui::Center, true);
        ui::text(TextFormat("Download: %d%%   Suspicion: %d%%   Caught: %dx", (int)(pct() * 100), (int)susp_, caughtCount_), VW / 2.0f, p.y + 170, 17, WHITE, ui::Center, true);
        if (won) {
            ui::coin({VW / 2.0f - 100, p.y + 220}, 15);
            ui::text("+" + fmtNum(reward_) + " Pietercoins", VW / 2.0f - 76, p.y + 220, 24, col::gold, ui::Left, true);
        }
        if (phaseT_ > 0.6f) {
            if (ui::button({p.x + 50, p.y + 300, 250, 62}, "RETRY", rgba(0x2a8a4aff), 24)) start(diff_);
            if (ui::button({p.x + 340, p.y + 300, 250, 62}, "MENU", col::panelHi, 24)) E().go(makeMenu);
        }
    }

    void draw() override {
        if (phase_ == Phase::Title) {
            drawTitle();
            return;
        }
        keyFlash_ = std::max(0.0f, keyFlash_ - GetFrameTime());
        drawRoom();
        if (phase_ == Phase::Restart) {
            Rectangle bz = shrink(S, -18);
            DrawRectangleRounded(bz, 0.03f, 8, rgba(0x15121cff));
            DrawRectangleRec(S, rgba(0x0a2a6aff));
            ui::text("Installing updates", S.x + S.width / 2, S.y + S.height / 2 - 30, 30, WHITE, ui::Center, true);
            ui::text(TextFormat("%d%% complete. Do not turn off your PC.", (int)(clamp01(phaseT_ / 5.5f) * 100)), S.x + S.width / 2, S.y + S.height / 2 + 10, 18, WHITE, ui::Center, true);
            for (int i = 0; i < 6; i++) {
                Vector2 c = Vector2{S.x + S.width / 2, S.y + S.height / 2 + 80} + fromAngle(t_ * 4 - i * 0.35f, 22);
                DrawCircleV(c, 4 - i * 0.4f, WHITE);
            }
        } else {
            drawDesktop();
        }
        drawHudBar();
        fx_.draw();
        if (phase_ == Phase::Play || phase_ == Phase::Restart) {
            // tired: eyelids closing
            if (energy_ < 25) {
                float k = (25 - energy_) / 25 * (0.6f + 0.4f * std::sin(t_ * 1.5f));
                DrawRectangle(0, 0, VW, (int)(VH * 0.3f * k), BLACK);
                DrawRectangle(0, (int)(VH - VH * 0.3f * k), VW, VH, BLACK);
            }
            drawCursor();
        }
        if (phase_ == Phase::Caught) drawCaught();
        if (phase_ == Phase::Lose) drawEnd(false);
        if (phase_ == Phase::Win) drawEnd(true);
    }

private:
    std::vector<Win> wins_;
    std::vector<Msg> msgs_;
    std::vector<Note> notes_;
    Phase phase_ = Phase::Title;
    int diff_ = 1, nextId_ = 1, drag_ = -1, clickId_ = -1, lastIcon_ = -1, unread_ = 0, msgScript_ = 0, nextWho_ = -1, flavour_ = 0, caughtCount_ = 0;
    Vector2 mouse_{}, cur_{}, dragOff_{}, clickPos_{}, ratOff_{}, ratTarget_{}, resumeBtn_{0.5f, 0.6f};
    float t_ = 0, phaseT_ = 0, playT_ = 0, lastIconT_ = 0;
    float got_ = 0, total_ = 6900, speed_ = 25, susp_ = 0, energy_ = 100, money_ = 30, homework_ = 0;
    float nextCheck_ = 0, nextGorilla_ = 0, nextIntr_ = 0, nextMsg_ = 0, nextUpdate_ = 0, updateT_ = 0, delivery_ = -1, blinds_ = 0, gorLeave_ = 0;
    float ratT_ = 0, popT_ = 0, ratAt_ = 0.4f, scanT_ = -1, installT_ = -1, captionT_ = 0, keyFlash_ = 0;
    bool rat_ = false, ratDone_ = false, updateOn_ = false, tiredWarned_ = false, trashFull_ = true, devCalm_ = false;
    Intr intr_ = Intr::None;
    std::string captcha_, captchaIn_, captionText_, caughtLine_, loseWhy_;
    Who caughtWho_ = Who::Mama;
    Check check_;
    Gorilla gor_;
    long long reward_ = 0;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeCheats() { return std::make_unique<CheatsScene>(); }

}  // namespace pg
