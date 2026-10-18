// Player profile: wallet, inventory, cosmetics, settings, stats and Pieter's
// needs. Persisted as a simple key=value text file.
#pragma once

#include "engine/Core.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace pg {

enum class ItemKind { Powerup, Food, Hat, Trail };

struct ItemDef {
    std::string id;
    std::string name;
    std::string desc;
    long long price;
    ItemKind kind;
    Color color;
    // food only
    int hunger = 0, fun = 0, energy = 0;
};

const std::vector<ItemDef>& itemDb();
const ItemDef* findItem(const std::string& id);

struct PieterNeeds {
    float hunger = 80, fun = 80, energy = 80, hygiene = 80;  // 0..100
    bool sleeping = false;
    long long xp = 0;
    int level = 1;
    long long lastUpdate = 0;  // unix seconds
    float mood() const { return (hunger + fun + energy + hygiene) / 4; }
    static long long xpForLevel(int lvl) { return 100 + (lvl - 1) * 60; }
};

struct Stats {
    int bestWave = 0;
    int classicWins = 0;
    int classicRuns = 0;
    long long totalEarned = 0;
    long long totalWagered = 0;
    long long biggestWin = 0;
    int horseWins = 0;
    int rideWins = 0;
    int stroopBest = 0;
    int jobBest = 0;       // Get A Job: most jobs survived
    int fietsBest = 0;     // Fietsen: best distance (m)
    int kartWins = 0;      // Pieter Kart: races won
    float kartBestLap = 0; // Pieter Kart: best lap (s), 0 = none
    int dungeonBest = 0;   // Davi's Dungeon: deepest floor
    int golfBest = 0;      // Grachtengolf: fewest strokes for the full course, 0 = never finished
    int tulpBest = 0;      // Tulpenmatch: high score
    int meeuwBest = 0;     // Meeuwen!: high score
    int polderBest = 0;    // Polder Defense: most waves survived
    int fotoBest = 0;      // Natuurfotograaf: best route score
    int fotoRoute = 1;     // Natuurfotograaf: routes unlocked (1..3)
    int fotoSpecies = 0;   // Natuurfotograaf: bitmask of species photographed
    int roofDone = 0;      // Dakkenloper: bitmask of levels finished
    int roofBoba = 0;      // Dakkenloper: bitmask of golden bobas found (3 bits per level)
    std::array<float, 5> roofTime{};  // Dakkenloper: best time per level (s), 0 = none
    int pannenDay = 0;     // Pannenkoekenhuis: furthest day survived
    int pannenBest = 0;    // Pannenkoekenhuis: best takings in one shift
};

struct BeatsBest {  // Pieter Beats personal best for one song + difficulty
    int score = 0;
    float acc = 0;    // 0..1
    bool fc = false;  // full combo
};

class Profile {
public:
    long long coins = 500;
    std::map<std::string, int> inventory;
    std::array<std::string, 3> loadout{"shield", "speed", "slowmo"};
    std::string hat;     // equipped hat item id ("" = none)
    std::string trail;   // equipped trail item id
    PieterNeeds pieter;
    Stats stats;
    long long lastPity = 0;
    bool introSeen = false;
    std::map<std::string, BeatsBest> beats;  // "<song>|<difficulty>"
    int beatsOffsetMs = 0;                    // audio latency calibration
    float beatsSpeed = 1.0f;                  // note scroll speed
    bool beatsHitSounds = true;
    int fnapNight = 1;      // Five Nights at Pieter's: highest night unlocked (6 = Juhuh night)
    int fnapBeaten = 0;     // bitmask of nights survived
    int fnapDeaths = 0;     // how many times Pieter has died. The house remembers.
    bool fnapLeft = false;  // you walked out in the middle of a night

    // settings
    float musicVolume = 0.55f;
    float sfxVolume = 0.7f;
    bool fullscreen = false;
    int resW = 0, resH = 0;  // 0 = pick automatically
    std::string monitor;     // monitor name, empty = primary
    bool screenShake = true;
    bool smoothGraphics = true;
    bool discordRpc = true;

    int count(const std::string& id) const;
    bool owns(const std::string& id) const { return count(id) > 0; }
    void give(const std::string& id, int n = 1);
    bool take(const std::string& id, int n = 1);

    bool spend(long long amount);          // false if broke
    void earn(long long amount, bool fromGambling = false);
    void wager(long long amount) { stats.totalWagered += amount; }

    // Pieter gives a handout when you are broke (cooldown).
    bool canPity() const;
    long long pitySecondsLeft() const;
    void takePity();

    void load();
    void save() const;
    void resetAll();
    std::string path() const;
};

Profile& P();
long long unixNow();

}  // namespace pg
