#include "game/Profile.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace pg {

static Profile g_profile;
Profile& P() { return g_profile; }

long long unixNow() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

const std::vector<ItemDef>& itemDb() {
    static const std::vector<ItemDef> db = {
        // ---- Classic powerups (consumed when activated with 1/2/3)
        {"shield", "Boba Shield", "Blocks the next hit. Tapioca armor.", 120, ItemKind::Powerup, col::boba},
        {"speed", "Game Fuel", "+60% speed for 6 seconds. Zoom.", 90, ItemKind::Powerup, col::gold},
        {"slowmo", "Stream Lag", "Enemies move in slow motion for 5s.", 150, ItemKind::Powerup, col::cyan},
        {"magnet", "Coin Magnet", "Pulls every Pietercoin to you for 10s.", 80, ItemKind::Powerup, col::red},
        {"heal", "Extra Heart", "Restores one heart (can go above max).", 200, ItemKind::Powerup, col::pink},
        {"nuke", "Useless Elektronik", "EMP that wipes every enemy on screen.", 260, ItemKind::Powerup, col::green},
        // ---- Food for Pieter Room
        {"boba", "Boba Tea", "Pieter's fuel. Big fun boost.", 25, ItemKind::Food, col::boba, 25, 15, 5},
        {"stroopwafel", "Stroopwafel", "Warm caramel waffle. Dutch classic.", 15, ItemKind::Food, col::wafel, 20, 8, 0},
        {"frikandel", "Frikandel", "Speciaal: mayo, curry, onions. Max filling.", 35, ItemKind::Food, col::caramel, 45, 5, 0},
        {"kroket", "Kroket", "From the wall. Hot. Careful.", 30, ItemKind::Food, col::orange, 35, 8, 0},
        {"gamefuel", "Energy Can", "Energy drink. Wakes Pieter right up.", 20, ItemKind::Food, col::gold, 5, 5, 35},
        {"hagelslag", "Hagelslag", "Chocolate sprinkles on bread.", 18, ItemKind::Food, rgba(0x5a3420ff), 22, 10, 5},
        // ---- Cosmetics (shown on Pieter in Classic and Pieter Room)
        {"hat_party", "Party Hat", "It's always Pieter's birthday.", 250, ItemKind::Hat, col::pink},
        {"hat_cap", "Snapback", "Backwards. Obviously.", 400, ItemKind::Hat, col::red},
        {"hat_cowboy", "Horse Rider Hat", "For the horse lover.", 800, ItemKind::Hat, rgba(0x8b5a2bff)},
        {"hat_crown", "Gamer Crown", "King of the Bec IT Room.", 1500, ItemKind::Hat, col::gold},
        {"hat_halo", "Holy Halo", "Pieter has never done anything wrong.", 2500, ItemKind::Hat, rgba(0xfff3a0ff)},
        {"trail_boba", "Boba Trail", "Leave tapioca pearls behind you.", 600, ItemKind::Trail, rgba(0x3a2418ff)},
        {"trail_rainbow", "Rainbow Trail", "RGB like the keyboard.", 1200, ItemKind::Trail, col::violet},
        {"trail_fire", "Fire Trail", "Too fast. Too hot.", 2000, ItemKind::Trail, col::orange},
    };
    return db;
}

const ItemDef* findItem(const std::string& id) {
    for (auto& it : itemDb())
        if (it.id == id) return &it;
    return nullptr;
}

int Profile::count(const std::string& id) const {
    auto it = inventory.find(id);
    return it == inventory.end() ? 0 : it->second;
}

void Profile::give(const std::string& id, int n) { inventory[id] += n; }

bool Profile::take(const std::string& id, int n) {
    auto it = inventory.find(id);
    if (it == inventory.end() || it->second < n) return false;
    it->second -= n;
    if (it->second == 0) inventory.erase(it);
    return true;
}

bool Profile::spend(long long amount) {
    if (amount > coins) return false;
    coins -= amount;
    return true;
}

void Profile::earn(long long amount, bool fromGambling) {
    coins += amount;
    stats.totalEarned += amount;
    if (fromGambling && amount > stats.biggestWin) stats.biggestWin = amount;
}

constexpr long long kPityCooldown = 5 * 60;
bool Profile::canPity() const { return coins < 25 && pitySecondsLeft() == 0; }
long long Profile::pitySecondsLeft() const { return std::max(0LL, lastPity + kPityCooldown - unixNow()); }
void Profile::takePity() {
    lastPity = unixNow();
    coins += 150;
    save();
}

std::string Profile::path() const {
    std::string base;
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA")) base = std::string(a) + "/PieterGame3";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) base = std::string(x) + "/PieterGame3";
    else if (const char* h = std::getenv("HOME")) base = std::string(h) + "/.local/share/PieterGame3";
#endif
    if (base.empty()) base = "save";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base + "/save.txt";
}

void Profile::save() const {
    std::ofstream f(path() + ".tmp");
    if (!f) return;
    f << "# PieterGame 3 save. Editing this is cheating. Pieter will know.\n";
    f << "coins=" << coins << "\n";
    for (auto& [k, v] : inventory) f << "item." << k << "=" << v << "\n";
    for (int i = 0; i < 3; i++) f << "loadout" << i << "=" << loadout[i] << "\n";
    f << "hat=" << hat << "\n" << "trail=" << trail << "\n";
    f << "p.hunger=" << pieter.hunger << "\np.fun=" << pieter.fun << "\np.energy=" << pieter.energy
      << "\np.hygiene=" << pieter.hygiene << "\np.sleeping=" << pieter.sleeping << "\np.xp=" << pieter.xp
      << "\np.level=" << pieter.level << "\np.last=" << pieter.lastUpdate << "\n";
    f << "s.bestWave=" << stats.bestWave << "\ns.classicWins=" << stats.classicWins << "\ns.classicRuns="
      << stats.classicRuns << "\ns.totalEarned=" << stats.totalEarned << "\ns.totalWagered=" << stats.totalWagered
      << "\ns.biggestWin=" << stats.biggestWin << "\ns.horseWins=" << stats.horseWins << "\ns.rideWins="
      << stats.rideWins << "\ns.stroopBest=" << stats.stroopBest << "\n";
    f << "lastPity=" << lastPity << "\n";
    f << "musicVolume=" << musicVolume << "\nsfxVolume=" << sfxVolume << "\nfullscreen=" << fullscreen
      << "\nscreenShake=" << screenShake << "\ndiscordRpc=" << discordRpc << "\nmonitor=" << monitor << "\nresW=" << resW << "\nresH=" << resH << "\n";
    f.close();
    std::error_code ec;
    fs::rename(path() + ".tmp", path(), ec);
}

void Profile::load() {
    std::ifstream f(path());
    if (!f) {
        // fresh save: a couple of starter goodies
        give("shield", 2);
        give("speed", 1);
        give("boba", 2);
        give("stroopwafel", 3);
        pieter.lastUpdate = unixNow();
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto L = [&] { return std::atoll(v.c_str()); };
        auto F = [&] { return (float)std::atof(v.c_str()); };
        if (k == "coins") coins = L();
        else if (k.rfind("item.", 0) == 0) { if (L() > 0) inventory[k.substr(5)] = (int)L(); }
        else if (k.rfind("loadout", 0) == 0 && k.size() == 8) { int i = k[7] - '0'; if (i >= 0 && i < 3) loadout[i] = v; }
        else if (k == "hat") hat = v;
        else if (k == "trail") trail = v;
        else if (k == "p.hunger") pieter.hunger = F();
        else if (k == "p.fun") pieter.fun = F();
        else if (k == "p.energy") pieter.energy = F();
        else if (k == "p.hygiene") pieter.hygiene = F();
        else if (k == "p.sleeping") pieter.sleeping = L() != 0;
        else if (k == "p.xp") pieter.xp = L();
        else if (k == "p.level") pieter.level = std::max(1, (int)L());
        else if (k == "p.last") pieter.lastUpdate = L();
        else if (k == "s.bestWave") stats.bestWave = (int)L();
        else if (k == "s.classicWins") stats.classicWins = (int)L();
        else if (k == "s.classicRuns") stats.classicRuns = (int)L();
        else if (k == "s.totalEarned") stats.totalEarned = L();
        else if (k == "s.totalWagered") stats.totalWagered = L();
        else if (k == "s.biggestWin") stats.biggestWin = L();
        else if (k == "s.horseWins") stats.horseWins = (int)L();
        else if (k == "s.rideWins") stats.rideWins = (int)L();
        else if (k == "s.stroopBest") stats.stroopBest = (int)L();
        else if (k == "lastPity") lastPity = L();
        else if (k == "musicVolume") musicVolume = F();
        else if (k == "sfxVolume") sfxVolume = F();
        else if (k == "fullscreen") fullscreen = L() != 0;
        else if (k == "screenShake") screenShake = L() != 0;
        else if (k == "discordRpc") discordRpc = L() != 0;
        else if (k == "monitor") monitor = v;
        else if (k == "resW") resW = (int)L();
        else if (k == "resH") resH = (int)L();
    }
    if (pieter.lastUpdate == 0) pieter.lastUpdate = unixNow();
}

void Profile::resetAll() {
    float mv = musicVolume, sv = sfxVolume;
    bool fsOn = fullscreen, sh = screenShake;
    int rw = resW, rh = resH;
    std::string mon = monitor;
    std::error_code ec;
    fs::remove(path(), ec);
    *this = Profile{};
    musicVolume = mv; sfxVolume = sv; fullscreen = fsOn; screenShake = sh;
    resW = rw; resH = rh; monitor = mon;
    load();
    save();
}

}  // namespace pg
