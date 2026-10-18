// Five Nights at Pieter's: the rules of the night and the minds of everything in
// the house. Pure logic, no drawing or audio: the scene feeds it the player's
// input every frame and turns the events it emits into sound and pictures. The
// simulator (PG_FNAP_SIM) runs it headless against a bot to balance the nights.
//
// Every monster is an agent with a memory of how you play:
//   Davi   - methodical. Every time you chase him off he comes back angrier.
//   Kira   - fast and sneaky. The more you shine the light at her, the more often
//            she ducks into the bathroom doorway instead of running. Hacks the PC.
//   Both     punish the door you neglect, follow the noise you make, freeze while
//            you watch them, and on later nights fake walking away from the door.
//   Het Nachtpaard - grows in the closet while you're not looking.
//   Paardjes - plush horses gathering on the bed; three of them call the horse.
//   Monkey Man - at the window. Stare too long and he wakes the whole house.
// Above them sits a director that paces the night like a film: it builds tension,
// lets it peak, gives you a breather, caps how many lethal threats can stack up at
// once, and stages set pieces (the house going silent before a rush).
#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace pg::fnap {

constexpr float kHourLen = 45;  // seconds per in-game hour: a night is 4.5 minutes
constexpr int kNights = 6;

enum class View { Center, Left, Right, Closet, Bed };
enum class Who { Davi, Kira, Horse, Paardjes };
enum class Mood { Calm, Build, Peak, Relax, Hush };

enum class Ev {
    Roam,         // a: side. Footsteps somewhere else in the house
    Advance,      // a: side, x: new position. One step closer down the hall
    Back,         // a: side. Drifted back a step on their own
    Seen,         // a: side, x: position. First time you light them on this approach
    Flee,         // a: side. Chased off by the flashlight
    Hide,         // a: side. Kira slipped into the bathroom doorway
    Unhide,       // a: side
    AtDoor,       // a: side. Reached the door
    Cue,          // a: side, x: 1 if you're at that door. Breathing (Davi) / giggling (Kira)
    Push,         // a: side. Throwing themselves against the door you're holding
    Leave,        // a: side. Gave up and walked off
    Feint,        // a: side. Sounds exactly like Leave, but they're still there
    Lure,         // a: side. A stroopwafel / boba thrown down the hall
    LureFail,
    HorseGrow,    // x: new stage (1..3)
    HorseOpen,    // the closet doors swing open
    HorseBack,    // pushed back into the closet
    HorseStrain,  // the closet straining against you
    Snort,
    PlushIn,      // x: how many are on the bed now
    PlushOut,     // x: index of the one that fled
    PlushCall,    // three plushies: they call the horse
    PcHack, PcSong, PcOff,
    MonkeyTap, MonkeyStare, MonkeyCalm, MonkeyBang, Cymbal,
    Hour,         // a: the new hour
    BatteryLow, BatteryDead,
    Knock,        // x: pan. Nobody there
    Whisper,
    Hush,         // the director silences the house...
    Rush,         // ...and they all come at once
    Jumpscare,    // a: Who
    Win,
};

struct Event {
    Ev ev;
    int a = 0;
    float x = 0;
};

struct Input {
    View view = View::Center;
    bool moving = false;              // running between spots: can't act
    bool light = false;               // flashlight held (it only reaches down halls, into the closet and onto the bed)
    bool hold = false;                // holding a door / the closet shut
    bool lure = false;                // Q this frame
    bool pcOff = false;               // E this frame
    bool hasLure[2] = {true, true};   // a stroopwafel / boba in the inventory
};

struct Stalker {
    int pos = 0;  // 0 elsewhere in the house, 1 far end of the hall, 2 by the clock, 3 close, 4 at the door
    float think = 0, stun = 0, lightT = 0, hideT = 0;
    float doorT = 0, pushT = 0, needPush = 0, pushCue = 0, cueT = 0, feintT = 0;
    bool hidden = false, seen = false, feint = false, feinted = false, lureUsed = false, wasHere = false;
    // memory
    float neglect = 0;  // seconds since you last looked down this hall
    float watchT = 0;   // seconds you've been standing at this door
    float rage = 0;     // Davi: grows every time he's chased off
    int repelled = 0, flashed = 0;
    // animation clocks for the scene
    float moveAnim = 9, fleeAnim = -1, lureAnim = -1;
    int fleeFrom = 0;
};

struct NightCfg {
    int davi, kira, horse, plush, pc;  // aggression 0..20
    int maxCritical;                   // lethal threats the director allows at once
    float feint;                       // chance they fake leaving the door
    bool monkey;
    const char* tag;
};
extern const NightCfg kNight[kNights];

class Brain {
public:
    explicit Brain(uint32_t seed = 1) : rng_(seed) {}
    void start(int night);
    void update(float dt, const Input& in, std::vector<Event>& out);

    int hour() const;
    float danger() const;  // 0..1, the worst threat right now
    bool lightWorks() const { return battery > 0; }
    float speed() const;   // PC hack makes everything faster

    int night = 0;
    float t = 0;
    Stalker hall[2];
    int horse = 0;  // 0 empty .. 3 pressing on the doors, 4 doors open
    float horseOpenT = 0, horseHold = 0, horseAnim = 9, horseLook = 0;
    int plush = 0;
    float plushCall = 0, plushLit = 0, plushAge[3]{};
    bool pc = false;
    bool monkey = false, monkeyRiser = false;
    float monkeyT = 0, monkeyStare = 0, monkeyBangT = 9;
    float battery = 1, noise = 0, intensity = 0;
    Mood mood = Mood::Calm;
    float moodT = 0;
    bool over = false, won = false;
    Who killer = Who::Davi;
    const char* cause = "";  // how you died, for the simulator
    bool frozen = false;  // dev: nothing moves or attacks

private:
    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }
    bool roll(float p) { return rnd(0, 1) < p; }
    int level(int base) const { return base + (hour() >= 2) + (hour() >= 4); }
    float moodScale() const;
    int critical() const;
    bool mayGoCritical() const;
    void director(float dt, std::vector<Event>& out);
    void stalker(int s, float dt, const Input& in, bool light, bool hold, std::vector<Event>& out);
    void closet(float dt, const Input& in, bool hold, std::vector<Event>& out);
    void bed(float dt, const Input& in, bool light, std::vector<Event>& out);
    void window(float dt, const Input& in, std::vector<Event>& out);
    void scare(Who w, std::vector<Event>& out, const char* why = "");

    std::mt19937 rng_;
    float closetNeglect_ = 0, bedNeglect_ = 0, horseThink_ = 8, plushThink_ = 10, pcThink_ = 20, pcSong_ = 0;
    float monkeyNext_ = 50, knockT_ = 40, whisperT_ = 20, peakT_ = 0, moodLen_ = 12, strainT_ = 0, snortT_ = 6;
    int lastHour_ = 0;
    bool wasMoving_ = false, wasHold_ = false, lowWarned_ = false, hushDone_[2]{};
};

// Headless balance run: a bot plays `runs` copies of every night. Prints win rates.
int simulate(int runs);

}  // namespace pg::fnap
