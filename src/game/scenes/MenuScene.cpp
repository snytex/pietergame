// Title screen: a game launcher.
//   sidebar  - logo, categories, now playing, your profile (coins, level) and the shop/bag/settings/quit buttons
//   hero     - the selected game, big: an illustrated widescreen game cover, its description, tags,
//              your personal best and a PLAY button
//   shelf    - the games in the current category as cards; hover to preview, click to play
// The crew avatars in the top bar still hold the secret admin code (Pieter, Pieter, Kira, Pieter, Davi).
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

struct Crew {
    const char* who;
    const char* name;
    const char* line;
    Sfx sound;
    Color color;
};
const Crew kCrew[4] = {{"pieter", "Pieter", "I am Pieter!", Sfx::Power, col::gold},
                       {"davi", "Davi", "PRO since 2010 (claims)", Sfx::Error, col::cyan},
                       {"kira", "Kira", "hihi <3", Sfx::Kiss, col::pink},
                       {"horse", "Horse", "JUHUH!", Sfx::Neigh, col::orange}};

enum Cat { Home, Action, Chill, Casino, Special, CatCount };
const char* kCatName[CatCount] = {"HOME", "ACTION", "CHILL", "CASINO", "SPECIAL"};
const char* kCatSub[CatCount] = {"Everything, newest first", "Fast hands, high scores", "Puzzles, sims and slow vibes",
                                 "Pietercoins in, Pietercoins out", "Pieter himself, and spooky stuff"};
const Color kCatCol[CatCount] = {rgba(0x8f5cffff), rgba(0xff7a3aff), rgba(0x2ab87aff), rgba(0xe0a020ff), rgba(0xff5a8aff)};

struct Game {
    std::string id, title, blurb;
    std::vector<std::string> chips;
    std::string tag;
    Color color;
    SceneFactory go;
    int cats;                          // bitmask of Cat
    std::function<std::string()> best;  // personal best line (may be empty)
};

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

const std::vector<Game>& games() {
    static std::vector<Game> g;
    if (!g.empty()) return g;
    const int A = 1 << Action, C = 1 << Chill, K = 1 << Casino, X = 1 << Special, H = 1 << Home;
    using F = std::function<std::string()>;
    g = {
        {"meeuw", "SEAGULLS!", "Pieter is a seagull on the beach at Scheveningen. Dive at tourists, snatch their fries before they hide them and bump rival gulls out of the sky.",
         {"ARCADE", "FLYING", "ENDLESS"}, "NEW", rgba(0x2a8ab8ff), makeMeeuw, H | A, F([] { return "High score: " + fmtNum(P().stats.meeuwBest); })},
        {"pannen", "PANCAKE HOUSE", "Pieter runs a pancake house on the canal for a week. Pour, bake the bacon in, flip, add syrup and get every plate to the right table before they walk out.",
         {"COOKING", "TIME MANAGEMENT", "7 DAYS"}, "NEW", rgba(0xd89a2aff), makePannen, H | C, F([] {
             if (!P().stats.pannenDay) return std::string("Opening day is Monday");
             return std::to_string(P().stats.pannenDay) + " / 7 days, best shift EUR " + std::to_string(P().stats.pannenBest);
         })},
        {"roof", "ROOFTOP RUN", "A platformer over the rooftops of Amsterdam. Wall-jump up the gables, bounce off awnings, stomp pigeons and dodge the steam, all the way up the Westertoren.",
         {"PLATFORMER", "5 LEVELS", "SPEEDRUN"}, "NEW", rgba(0xd8703aff), makeRoof, H | A, F([] {
             int n = 0, b = 0;
             for (int i = 0; i < 5; i++) n += (P().stats.roofDone >> i) & 1;
             for (int i = 0; i < 15; i++) b += (P().stats.roofBoba >> i) & 1;
             return std::to_string(n) + " / 5 levels, " + std::to_string(b) + " / 15 golden bobas";
         })},
        {"polder", "POLDER DEFENSE", "Davi has emptied his basement and his useless electronics march on Pieter's farm. Build clog cannons, windmills, cheese catapults and tulip zappers to save the stroopwafels.",
         {"TOWER DEFENSE", "STRATEGY", "20 WAVES"}, "NEW", rgba(0x3a8a3aff), makePolder, H | C,
         F([] { return P().stats.polderBest ? "Most waves survived: " + std::to_string(P().stats.polderBest) : std::string("The polder needs you"); })},
        {"cheats", "CHEAT DOWNLOADER", "Get PieterHax onto the PC without getting caught. Juggle windows, hide them before Mom opens the door, fight off a RAT and keep the boba coming.",
         {"SIMULATION", "STEALTH", "~5 MIN"}, "NEW", rgba(0x1a8a3aff), makeCheats, H | C, F([] { return std::string("Can you get away with it?"); })},
        {"photo", "NATURE PHOTOGRAPHER", "A photo safari through the Veluwe, the polder and the Wadden. Frame the shot, whistle for a pose and fill your field guide with 18 species.",
         {"PHOTOGRAPHY", "RELAXED", "3 ROUTES"}, "NEW", rgba(0x2a7a9aff), makePhoto, H | C, F([] {
             int n = 0;
             for (int i = 0; i < 18; i++) n += (P().stats.fotoSpecies >> i) & 1;
             return std::to_string(n) + " / 18 species photographed";
         })},
        {"golf", "CANAL GOLF", "Nine holes of mini golf through Amsterdam: canals and bridges, windmill sails, stroopwafel bumpers and cyclists who don't look where they're going.",
         {"MINI GOLF", "PHYSICS", "9 HOLES"}, "NEW", rgba(0x2a8a4aff), makeGolf, H | C,
         F([] { return P().stats.golfBest ? "Best round: " + std::to_string(P().stats.golfBest) + " strokes" : std::string("No round finished yet"); })},
        {"tulpen", "TULIP MATCH", "Swap tulips, cheese, clogs and boba into rows. Striped tiles, bombs and rainbow Pieters chain into huge combos.",
         {"MATCH-3", "PUZZLE", "LEVELS"}, "NEW", rgba(0xc2418aff), makeTulpen, H | C, F([] { return "High score: " + fmtNum(P().stats.tulpBest); })},
        {"classic", "CLASSIC", "The original. Dodge everything the waves throw at you, grab power-ups and take down the bosses.",
         {"ARCADE", "SURVIVAL"}, "", rgba(0x6b3fd1ff), makeClassic, H | A, F([] { return "Best wave: " + std::to_string(P().stats.bestWave); })},
        {"kart", "PIETER KART", "Race Davi, Kira and the Horse round the canal circuit. Drift for boosts, fire items, don't fall in.",
         {"RACING", "VS AI", "3 LAPS"}, "", rgba(0xd8582aff), makeKart, H | A, F([] {
             std::string s = plural(P().stats.kartWins, "race won", "races won");
             if (P().stats.kartBestLap > 0) s += TextFormat("  -  best lap %.2fs", P().stats.kartBestLap);
             return s;
         })},
        {"dungeon", "DAVI'S DUNGEON", "A twin-stick roguelite in Davi's basement. Smash useless electronics, stack upgrades and beat Davi on his gaming chair.",
         {"ROGUELITE", "SHOOTER"}, "", rgba(0x4a3a8aff), makeDungeon, H | A, F([] { return "Deepest floor: " + std::to_string(P().stats.dungeonBest); })},
        {"fiets", "BIKE RUSH", "Ride along the canal on an old Dutch bike. Switch lanes, jump bollards and ring your bell at tourists.",
         {"RUNNER", "ENDLESS"}, "", rgba(0x2f7ad8ff), makeFiets, H | A, F([] { return "Best distance: " + std::to_string(P().stats.fietsBest) + " m"; })},
        {"beats", "PIETER BEATS", "A rhythm game that charts every song in the jukebox. Four lanes, three difficulties, chase the full combo.",
         {"RHYTHM", "MUSIC"}, "", rgba(0x2a5bd7ff), makeBeats, H | A, F([] { return plural((int)P().beats.size(), "song cleared", "songs cleared"); })},
        {"job", "GET A JOB", "Pieter tries every job there is. Five seconds each. Get fired, try again.", {"MICROGAMES", "FRANTIC"}, "", rgba(0x1f9a8aff), makeJob,
         H | A, F([] { return "Most jobs survived: " + std::to_string(P().stats.jobBest); })},
        {"stroop", "STROOPWAFEL STACKER", "Drop the waffles at just the right moment and build the tallest tower Pieter has ever seen.", {"ARCADE", "TIMING"}, "",
         rgba(0xc98a1fff), makeStroopwafel, H | A, F([] { return "Best tower: " + std::to_string(P().stats.stroopBest); })},
        {"horses", "HORSES", "JUHUH! Bet on a horse race, or saddle up and ride one yourself.", {"RACING", "BETTING"}, "", rgba(0xa0582aff), makeHorses, H | A | K,
         F([] { return plural(P().stats.horseWins, "winning bet", "winning bets"); })},
        {"room", "PIETER ROOM", "Pieter's bedroom. Feed him, pet him, let him game, put him to bed and chat with him on his phone.", {"PET SIM", "COZY"}, "",
         rgba(0xc2418aff), makePieterRoom, H | C | X, F([] { return "Pieter is level " + std::to_string(P().pieter.level); })},
        {"fnap", "FIVE NIGHTS AT PIETER'S", "The Halloween special. Watch the doors, the closet and the bed until 6 AM. Davi and Kira are coming. So is the Night Horse.",
         {"HORROR", "SURVIVAL", "6 NIGHTS"}, "HALLOWEEN", rgba(0x7a1a2aff), makeFnap, H | X, F([] {
             int n = 0;
             for (int i = 0; i < 6; i++) n += (P().fnapBeaten >> i) & 1;
             return std::to_string(n) + " / 6 nights survived";
         })},
        {"casino", "PIETER CASINO", "Seven tables under one roof: roulette, blackjack, crash, plinko, mines, dragon tower and slots.", {"CASINO", "7 GAMES"}, "",
         rgba(0x1f8a4cff), makeCasino, H, F([] { return "Biggest win: " + fmtNum(P().stats.biggestWin); })},
        {"roulette", "ROULETTE", "A European wheel. Bet on numbers, colours or dozens. All in on 17?", {"TABLE GAME"}, "", rgba(0xb8283aff), makeRoulette, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"blackjack", "BLACKJACK", "Beat Davi the dealer to 21. Hit, stand or double down.", {"CARDS"}, "", rgba(0x2a7ab8ff), makeBlackjack, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"crash", "CRASH", "The rocket climbs and the multiplier grows. Cash out before it blows.", {"MULTIPLIER"}, "", rgba(0x2a9a6aff), makeCrash, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"plinko", "PLINKO", "Drop Pieters down the pegs and watch them bounce into the multipliers.", {"LUCK"}, "", rgba(0xd84a8aff), makePlinko, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"mines", "MINES", "Find stroopwafels, avoid the bombs. Cash out whenever you like.", {"RISK"}, "", rgba(0x6a3ad8ff), makeMines, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"tower", "DRAGON TOWER", "Climb the tower one egg at a time. Don't wake the dragon.", {"RISK"}, "", rgba(0x2a8a4aff), makeDragonTower, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
        {"slots", "SLOTS", "Three reels. Three Pieters pays 500x.", {"LUCK"}, "", rgba(0xc8901aff), makeSlots, K, F([] { return "Biggest casino win: " + fmtNum(P().stats.biggestWin); })},
    };
    return g;
}

std::vector<int> gamesIn(int cat) {
    std::vector<int> out;
    const auto& g = games();
    for (size_t i = 0; i < g.size(); i++)
        if (g[i].cats & (1 << cat)) out.push_back((int)i);
    return out;
}

Color tagColor(const std::string& tag) { return tag == "NEW" ? col::green : tag == "HALLOWEEN" ? rgba(0xff7a1aff) : col::gold; }

// remembered between visits to the menu
int g_cat = 0;
int g_focus[CatCount] = {};

constexpr float kSide = 248;  // sidebar width
constexpr Rectangle kHero{266, 78, 996, 372};
constexpr float kShelfY = 500, kCardW = 256, kCardH = 194, kCardGap = 16;

class MenuScene : public Scene {
public:
    Presence presence() const override { return {"Main Menu"}; }
    MusicMode music() const override { return MusicMode::Title; }

    void enter() override {
        P().save();
        if (const char* c = std::getenv("PG_MENU_CAT")) g_cat = std::clamp(std::atoi(c), 0, CatCount - 1);  // dev hook
        if (const char* f = std::getenv("PG_MENU_FOCUS")) g_focus[g_cat] = std::atoi(f);                    // dev hook
        setCat(g_cat, false);
    }
    void exit() override { g_focus[g_cat] = focus_; }

    void setCat(int c, bool sound = true) {
        if (!list_.empty()) g_focus[g_cat] = focus_;
        g_cat = c;
        list_ = gamesIn(c);
        focus_ = std::clamp(g_focus[c], 0, (int)list_.size() - 1);
        scrollTarget_ = 0;
        ensureVisible();
        scroll_ = scrollTarget_;
        shelfT_ = 0;
        heroFrom_ = -1;
        heroT_ = 0;
        if (sound) E().sfx.play(Sfx::Swap, 0.4f, 1.2f);
    }

    const Game& focused() const { return games()[list_[focus_]]; }

    void setFocus(int i) {
        i = std::clamp(i, 0, (int)list_.size() - 1);
        if (i == focus_) return;
        heroFrom_ = list_[focus_];
        heroT_ = 0;
        focus_ = i;
        E().sfx.play(Sfx::Hover, 0.5f);
        ensureVisible();
    }

    float shelfW() const { return VW - kHero.x - 18; }
    float maxScroll() const { return std::max(0.0f, list_.size() * (kCardW + kCardGap) - kCardGap - shelfW()); }
    void ensureVisible() {
        float x = focus_ * (kCardW + kCardGap);
        if (x < scrollTarget_) scrollTarget_ = x;
        if (x + kCardW > scrollTarget_ + shelfW()) scrollTarget_ = x + kCardW - shelfW();
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
    }

    void launch(const Game& g) {
        if (launching_) return;
        launching_ = true;
        E().sfx.play(Sfx::Click);
        E().sfx.play(Sfx::Whoosh, 0.4f, 1.2f);
        E().go(g.go);
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        heroT_ = std::min(1.0f, heroT_ + dt * 3);
        shelfT_ += dt;
        Vector2 m = E().mouse();
        parallax_ = damp(parallax_, {(m.x - VW / 2.0f) / VW * -14, (m.y - VH / 2.0f) / VH * -8}, 4, dt);
        spawn_ += dt;
        while (spawn_ > 0.14f) {
            spawn_ -= 0.14f;
            Particle p;
            p.pos = {rnd(kSide, VW), VH + 10.0f};
            p.vel = {rnd(-10, 10), rnd(-50, -20)};
            p.life = p.maxLife = rnd(6, 10);
            p.size = rnd(1.5f, 3);
            p.endSize = 0.5f;
            p.color = alpha(lighten(focused().color, 0.4f), rnd(0.25f, 0.5f));
            fx_.add(p);
        }
        fx_.update(dt);
        scroll_ = damp(scroll_, scrollTarget_, 12, dt);
        if (E().backPressed()) confirmQuit_ = !confirmQuit_;
        if (confirmQuit_) return;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) setFocus(focus_ + 1);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) setFocus(focus_ - 1);
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_TAB)) setCat((g_cat + 1) % CatCount);
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_Q)) setCat((g_cat + CatCount - 1) % CatCount);
        for (int i = 0; i < CatCount; i++)
            if (IsKeyPressed(KEY_ONE + i)) setCat(i);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) launch(focused());
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && m.y > kShelfY - 30 && m.x > kSide) scrollTarget_ = std::clamp(scrollTarget_ - wheel * 180, 0.0f, maxScroll());
    }

    // ============================================================ background
    void cover(Texture2D& t, float a) {
        float sc = std::max((float)VW / t.width, (float)VH / t.height) * 1.12f;
        float w = t.width * sc, h = t.height * sc;
        DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {(VW - w) / 2 + parallax_.x, (VH - h) / 2 + parallax_.y, w, h}, {0, 0}, 0, alpha(WHITE, a));
    }

    void drawBackdrop() {
        // the focused game's preview, blurred, fills the screen and crossfades
        DrawRectangle(0, 0, VW, VH, col::bgDeep);
        bool fading = heroFrom_ >= 0 && heroT_ < 1;
        if (fading) cover(fx::blurred("covers/" + games()[heroFrom_].id), 1);
        cover(fx::blurred("covers/" + focused().id), fading ? easeInOutSine(heroT_) : 1);
        DrawRectangleGradientV(0, 0, VW, VH, alpha(col::bgDeep, 0.5f), alpha(col::bgDeep, 0.9f));
        fx::beginAdd();
        fx::glow({kHero.x + kHero.width * 0.6f, kHero.y + kHero.height * 0.5f}, 700, alpha(focused().color, 0.16f));
        fx::endAdd();
        fx::bokeh(t_, 8, lighten(focused().color, 0.3f), 3);
        fx_.draw();
    }

    // ============================================================ sidebar
    void drawSidebar() {
        DrawRectangleRec({0, 0, kSide, (float)VH}, alpha(rgba(0x0c0718ff), 0.84f));
        DrawRectangleGradientH((int)kSide, 0, 18, VH, alpha(BLACK, 0.35f), alpha(BLACK, 0));
        DrawLineEx({kSide, 0}, {kSide, (float)VH}, 1, alpha(WHITE, 0.08f));
        // logo
        float bob = std::sin(t_ * 2) * 2;
        fx::gradientText("PIETERGAME", 100, 52 + bob, 36, WHITE, rgba(0xc9b2ffff), 4);
        float s = 1 + std::sin(t_ * 4) * 0.05f;
        ui::textRotated("3", {204 + 2, 30 + bob + 3}, 58 * s, -10, col::outline, true);
        ui::textRotated("3", {204, 30 + bob}, 58 * s, -10, col::gold, true);
        ui::text("the dutch legend returns", 22, 88, 13, col::pink, ui::Left, true);
        // categories
        float y = 140;
        ui::text("LIBRARY", 26, 122, 11, alpha(col::muted, 0.7f), ui::Left, true);
        float target = y + g_cat * 54.0f;
        catY_ = catY_ == 0 ? target : damp(catY_, target, 16, GetFrameTime());
        Rectangle sel{14, catY_, kSide - 28, 46};
        fx::gradRoundRect(sel, 0.4f, alpha(lighten(kCatCol[g_cat], 0.1f), 0.95f), alpha(darken(kCatCol[g_cat], 0.25f), 0.95f));
        DrawRectangleRounded({sel.x + 3, sel.y + 3, sel.width - 6, sel.height * 0.45f}, 0.5f, 8, alpha(WHITE, 0.12f));
        for (int i = 0; i < CatCount; i++) {
            Rectangle r{14, y + i * 54.0f, kSide - 28, 46};
            bool over = ui::hovered(r) && !confirmQuit_;
            float& h = catHover_[i];
            h = damp(h, over ? 1.0f : 0.0f, 14, GetFrameTime());
            if (g_cat != i && h > 0.01f) DrawRectangleRounded(r, 0.4f, 8, alpha(WHITE, 0.06f * h));
            Color ic = g_cat == i ? WHITE : mix(col::muted, kCatCol[i], 0.5f + 0.5f * h);
            catIcon(i, {r.x + 26 + h * 2, r.y + r.height / 2}, ic);
            ui::text(kCatName[i], r.x + 52 + h * 2, r.y + r.height / 2, 17, g_cat == i ? WHITE : lighten(col::muted, 0.3f * h), ui::Left, true);
            int n = (int)gamesIn(i).size();
            Rectangle badge{r.x + r.width - 40, r.y + 13, 28, 20};
            DrawRectangleRounded(badge, 1, 8, g_cat == i ? alpha(BLACK, 0.25f) : alpha(WHITE, 0.08f));
            ui::text(std::to_string(n), badge.x + badge.width / 2, badge.y + 10, 12, g_cat == i ? WHITE : col::muted, ui::Center, true);
            if (over && E().mousePressed() && g_cat != i) {
                E().consumeClick();
                setCat(i);
            }
        }
        drawNowPlaying({14, 428, kSide - 28, 60});
        drawProfile({14, 500, kSide - 28, 204});
    }

    void catIcon(int i, Vector2 c, Color k) {
        switch (i) {
            case Home:
                DrawTriangle({c.x, c.y - 10}, {c.x - 10, c.y}, {c.x + 10, c.y}, k);
                DrawRectangleRec({c.x - 7, c.y, 14, 9}, k);
                break;
            case Action:  // lightning bolt
                DrawTriangle({c.x + 3, c.y - 11}, {c.x - 7, c.y + 2}, {c.x + 1, c.y + 2}, k);
                DrawTriangle({c.x - 1, c.y - 2}, {c.x - 3, c.y + 11}, {c.x + 7, c.y - 2}, k);
                break;
            case Chill:  // leaf
                DrawEllipse((int)c.x, (int)c.y, 6, 10, k);
                DrawLineEx({c.x - 2, c.y + 9}, {c.x + 2, c.y - 7}, 1.5f, alpha(BLACK, 0.35f));
                break;
            case Casino:  // chip
                DrawCircleV(c, 10, k);
                DrawCircleV(c, 5.5f, alpha(BLACK, 0.3f));
                for (int a = 0; a < 4; a++) DrawRectangleRec({c.x + std::cos(a * PI / 2) * 8 - 1.5f, c.y + std::sin(a * PI / 2) * 8 - 1.5f, 3, 3}, alpha(BLACK, 0.3f));
                break;
            default:  // star
                DrawPoly(c, 5, 10, -90 + 36, k);
                DrawPoly({c.x, c.y + 1}, 5, 5, -90, k);
                break;
        }
    }

    void drawNowPlaying(Rectangle r) {
        std::string np = E().music.nowPlaying();
        DrawRectangleRounded(r, 0.35f, 10, alpha(WHITE, 0.05f));
        Vector2 vc{r.x + 30, r.y + r.height / 2};
        DrawCircleV(vc, 20, rgba(0x0e0b14ff));
        for (int k = 0; k < 3; k++) DrawRing(vc, 9 + k * 3.5f, 9.6f + k * 3.5f, 0, 360, 24, alpha(WHITE, 0.08f));
        DrawCircleV(vc, 6, focused().color);
        DrawCircleSector(vc, 19, t_ * 140, t_ * 140 + 30, 6, alpha(WHITE, 0.12f));
        if (np.empty()) np = "...";
        if (np.size() > 16) np = np.substr(0, 15) + "..";
        ui::text("NOW PLAYING", r.x + 58, r.y + 17, 10, col::cyan, ui::Left, true);
        ui::text(np, r.x + 58, r.y + 34, 14, col::text, ui::Left, true);
        DrawRectangleRounded({r.x + 58, r.y + 47, 104, 3}, 1, 4, alpha(WHITE, 0.15f));
        DrawRectangleRounded({r.x + 58, r.y + 47, 104 * E().music.progress(), 3}, 1, 4, col::cyan);
        Vector2 sk{r.x + r.width - 24, r.y + r.height / 2};
        bool over = Vector2Distance(E().mouse(), sk) < 16 && !confirmQuit_;
        DrawCircleV(sk, 15, over ? col::cyan : alpha(WHITE, 0.1f));
        Color k = over ? col::bgDeep : col::text;
        DrawTriangle({sk.x - 6, sk.y - 6}, {sk.x - 6, sk.y + 6}, {sk.x + 3, sk.y}, k);
        DrawRectangleRec({sk.x + 3, sk.y - 6, 2.5f, 12}, k);
        if (over && E().mousePressed()) {
            E().consumeClick();
            E().music.next();
        }
    }

    void drawProfile(Rectangle r) {
        DrawRectangleRounded(r, 0.12f, 10, alpha(WHITE, 0.05f));
        DrawRectangleRoundedLinesEx(r, 0.12f, 10, 1, alpha(WHITE, 0.08f));
        Vector2 av{r.x + 40, r.y + 42};
        fx::beginAdd();
        fx::glow(av, 50, alpha(col::violet, 0.35f));
        fx::endAdd();
        art::pieter(av, 27, P().hat);
        ui::text("Pieter", r.x + 80, r.y + 32, 18, WHITE, ui::Left, true);
        ui::text("Level " + std::to_string(P().pieter.level), r.x + 80, r.y + 52, 13, col::muted, ui::Left, true);
        Rectangle cr{r.x + 12, r.y + 80, r.width - 24, 40};
        DrawRectangleRounded(cr, 0.5f, 8, alpha(BLACK, 0.3f));
        ui::coin({cr.x + 22, cr.y + 20}, 12);
        shownCoins_ = shownCoins_ < 0 ? (float)P().coins : damp(shownCoins_, (float)P().coins, 8, GetFrameTime());
        ui::text(fmtNum((long long)std::round(shownCoins_)), cr.x + 42, cr.y + 20, 19, col::gold, ui::Left, true);
        ui::text("Pietercoins", cr.x + cr.width - 10, cr.y + 21, 11, col::muted, ui::Right, true);
        const char* tips[4] = {"Shop", "Inventory", "Settings", "Quit"};
        const Color cs[4] = {col::pink, col::orange, col::violet, rgba(0xd84a5aff)};
        for (int i = 0; i < 4; i++) {
            Vector2 c{r.x + 32 + i * 52.0f, r.y + 162};
            bool over = Vector2Distance(E().mouse(), c) < 21 && !confirmQuit_;
            float& h = dockHover_[i];
            h = damp(h, over ? 1.0f : 0.0f, 14, GetFrameTime());
            c.y -= h * 3;
            if (h > 0.01f) {
                fx::beginAdd();
                fx::glow(c, 44, alpha(cs[i], 0.4f * h));
                fx::endAdd();
            }
            DrawCircleV({c.x, c.y + 2}, 21, alpha(BLACK, 0.4f));
            DrawCircleV(c, 21, mix(darken(cs[i], 0.35f), cs[i], h));
            DrawRing(c, 20, 21.5f, 0, 360, 32, alpha(WHITE, 0.15f + 0.4f * h));
            dockIcon(i, c, WHITE);
            if (over) {
                float w = ui::measure(tips[i], 13, true) + 16;
                Rectangle tip{c.x - w / 2, c.y - 50, w, 22};
                DrawRectangleRounded(tip, 1, 8, alpha(col::bgDeep, 0.95f));
                ui::text(tips[i], c.x, tip.y + 11, 13, cs[i], ui::Center, true);
                if (E().mousePressed()) {
                    E().consumeClick();
                    E().sfx.play(Sfx::Click);
                    if (i == 0) E().go(makeShop);
                    if (i == 1) E().go(makeInventory);
                    if (i == 2) E().go(makeSettings);
                    if (i == 3) confirmQuit_ = true;
                }
            }
        }
    }

    void dockIcon(int i, Vector2 ic, Color k) {
        switch (i) {
            case 0:  // bag
                DrawRectangleRounded({ic.x - 8, ic.y - 3, 16, 12}, 0.25f, 4, k);
                DrawRing({ic.x, ic.y - 3}, 3.5f, 5.5f, 180, 360, 10, k);
                break;
            case 1:  // backpack
                DrawRectangleRounded({ic.x - 7, ic.y - 7, 14, 16}, 0.4f, 6, k);
                DrawRing({ic.x, ic.y - 7}, 2.5f, 4.5f, 180, 360, 8, k);
                break;
            case 2:  // gear
                DrawCircleV(ic, 7, k);
                for (int a = 0; a < 8; a++) DrawCircleV(ic + fromAngle(a * PI / 4 + t_ * dockHover_[2], 9), 2.6f, k);
                DrawCircleV(ic, 3, darken(col::violet, 0.35f));
                break;
            default:  // power
                DrawRing(ic, 5.5f, 7.8f, -50, 230, 16, k);
                DrawLineEx({ic.x, ic.y - 9}, {ic.x, ic.y - 1}, 3, k);
                break;
        }
    }

    // ============================================================ top bar
    void drawTopBar() {
        Vector2 m = E().mouse();
        for (int i = 0; i < 4; i++) {
            Vector2 c{kHero.x + 22 + i * 44.0f, 40};
            bool over = Vector2Distance(m, c) < 19 && !confirmQuit_;
            float& h = crewHover_[i];
            h = damp(h, over ? 1.0f : 0.0f, 14, GetFrameTime());
            if (h > 0.01f) {
                fx::beginAdd();
                fx::glow(c, 46, alpha(kCrew[i].color, 0.4f * h));
                fx::endAdd();
            }
            art::portrait(kCrew[i].who, {c.x, c.y - h * 2}, 17 + h * 2, over ? kCrew[i].color : col::outline);
            DrawCircleV({c.x + 12, c.y + 12 - h * 2}, 5, col::outline);
            DrawCircleV({c.x + 12, c.y + 12 - h * 2}, 3.5f, col::green);
            if (over) {
                float w = ui::measure(kCrew[i].name, 13, true) + 16;
                DrawRectangleRounded({c.x - w / 2, c.y + 24, w, 22}, 1, 8, alpha(col::bgDeep, 0.9f));
                ui::text(kCrew[i].name, c.x, c.y + 35, 13, kCrew[i].color, ui::Center, true);
                if (E().mousePressed()) {
                    E().consumeClick();
                    E().sfx.play(kCrew[i].sound);
                    fx_.floatText(kCrew[i].line, {c.x + 40, c.y + 50}, kCrew[i].color, 22);
                    fx_.burst(c, 14, kCrew[i].color, 220, 6, 0.6f, kCrew[i].sound == Sfx::Kiss ? PShape::Heart : PShape::Star);
                    if (kCrew[i].sound == Sfx::Neigh) E().shake(4);
                    secretClick(kCrew[i].who);
                }
            }
        }
        DrawLineEx({kHero.x + 192, 28}, {kHero.x + 192, 52}, 1, alpha(WHITE, 0.15f));
        ui::text(kCatName[g_cat], kHero.x + 210, 33, 15, WHITE, ui::Left, true);
        ui::text(kCatSub[g_cat], kHero.x + 210, 51, 12, alpha(col::text, 0.55f), ui::Left, true);
        ui::coinCounter(VW - 18, 40, P().coins);
    }

    // ============================================================ hero
    void drawPreview(const Game& g, Rectangle r, float a) {
        Texture2D& t = E().tex("covers/" + g.id);
        // Preserve the cover typography: hover lifts the card without zooming
        // or cropping the title near an edge.
        if (!t.id || t.width <= 0 || t.height <= 0) return;
        float sw = (float)t.width, sh = sw * r.height / r.width;
        if (sh > t.height) {
            sh = (float)t.height;
            sw = sh * r.width / r.height;
        }
        DrawTexturePro(t, {(t.width - sw) / 2, (t.height - sh) / 2, sw, sh}, r, {0, 0}, 0, alpha(WHITE, a));
    }

    void drawHero() {
        const Game& g = focused();
        Rectangle r = kHero;
        float k = easeOutCubic(heroT_);
        float a = heroFrom_ >= 0 ? k : clamp01(heroT_ * 2);
        fx::softShadow({r.x + r.width / 2, r.y + r.height + 6}, r.width * 0.5f, 30, 0.6f);
        DrawRectangleRounded(shrink(r, -2), 0.06f, 12, alpha(BLACK, 0.6f));
        E().beginClip(r);
        Color base = darken(g.color, 0.8f);
        DrawRectangleRec(r, base);
        // Give the cover its own widescreen frame, clear of the copy.
        Rectangle img{r.x + 424, r.y + 30, r.width - 442, (r.width - 442) * 9 / 16};
        DrawRectangleRounded(shrink(img, -5), 0.035f, 8, alpha(BLACK, 0.6f));
        if (heroFrom_ >= 0 && heroT_ < 1) drawPreview(games()[heroFrom_], img, 1);
        drawPreview(g, img, a);
        DrawRectangleLinesEx(shrink(img, -1), 1, alpha(WHITE, 0.24f));
        ui::text("PIETERGAME COLLECTION", img.x, r.y + r.height - 17, 10, alpha(WHITE, 0.55f), ui::Left, true);
        ui::text(g.chips.front(), img.x + img.width, r.y + r.height - 17, 10, lighten(g.color, 0.55f), ui::Right, true);
        // text block
        float tx = r.x + 34, slideIn = (1 - k) * 24;
        float y = r.y + 40;
        if (!g.tag.empty()) {
            Color pc = tagColor(g.tag);
            float tw = ui::measure(g.tag, 13, true) + 22;
            DrawRectangleRounded({tx + slideIn, y - 12, tw, 24}, 1, 8, alpha(pc, a));
            ui::text(g.tag, tx + slideIn + tw / 2, y, 13, alpha(col::outline, a), ui::Center, true);
            y += 30;
        }
        float maxW = 364;
        float size = std::min(52.0f, 52 * maxW / std::max(1.0f, ui::measure(g.title, 52, true)));
        ui::textShadow(g.title, tx + slideIn, y + size / 2, size, alpha(WHITE, a), ui::Left, true, 3, alpha(darken(g.color, 0.7f), a));
        y += size + 14;
        float ph = ui::paragraph(g.blurb, {tx + slideIn * 1.5f, y, 364, 0}, 16, alpha(col::text, 0.88f * a));
        y += ph + 20;
        float cx = tx + slideIn * 2;
        for (auto& c : g.chips) {
            float w = ui::measure(c, 12, true) + 20;
            DrawRectangleRounded({cx, y - 11, w, 22}, 1, 8, alpha(WHITE, 0.12f * a));
            DrawRectangleRoundedLinesEx({cx, y - 11, w, 22}, 1, 8, 1, alpha(WHITE, 0.2f * a));
            ui::text(c, cx + w / 2, y, 12, alpha(WHITE, 0.9f * a), ui::Center, true);
            cx += w + 8;
        }
        // personal best, with a little trophy
        if (g.best) {
            Vector2 tr{tx + 9, r.y + r.height - 104};
            Color gc = alpha(col::gold, a);
            DrawCircleSector({tr.x, tr.y - 2}, 8, 0, 180, 12, gc);
            DrawRectangleRec({tr.x - 8, tr.y - 6, 16, 4}, gc);
            DrawRectangleRec({tr.x - 2, tr.y + 5, 4, 5}, gc);
            DrawRectangleRec({tr.x - 6, tr.y + 10, 12, 3}, gc);
            ui::text(g.best(), tx + 28, tr.y + 2, 15, gc, ui::Left, true);
        }
        // PLAY
        Rectangle play{tx, r.y + r.height - 76, 210, 54};
        bool over = ui::hovered(play) && !confirmQuit_;
        playHover_ = damp(playHover_, over ? 1.0f : 0.0f, 14, GetFrameTime());
        float pulse = 0.5f + 0.5f * std::sin(t_ * 3);
        fx::beginAdd();
        fx::glow(center(play), 150, alpha(lighten(g.color, 0.3f), 0.18f + 0.14f * pulse + 0.2f * playHover_));
        fx::endAdd();
        Rectangle pr{play.x, play.y - playHover_ * 3, play.width, play.height};
        DrawRectangleRounded({pr.x, pr.y + 5, pr.width, pr.height}, 1, 12, darken(g.color, 0.55f));
        fx::gradRoundRect(pr, 1, lighten(g.color, 0.35f + 0.1f * playHover_), g.color);
        DrawRectangleRounded({pr.x + 6, pr.y + 4, pr.width - 12, pr.height * 0.4f}, 1, 12, alpha(WHITE, 0.18f));
        Vector2 pc{pr.x + 40, pr.y + pr.height / 2};
        DrawCircleV(pc, 15, alpha(WHITE, 0.95f));
        DrawTriangle({pc.x - 4, pc.y - 7}, {pc.x - 4, pc.y + 7}, {pc.x + 7, pc.y}, g.color);
        ui::text("PLAY", pr.x + 66, pr.y + pr.height / 2, 26, WHITE, ui::Left, true);
        ui::text("ENTER", play.x + play.width + 16, play.y + play.height / 2, 13, alpha(col::muted, 0.8f), ui::Left, true);
        if (over && E().mousePressed()) {
            E().consumeClick();
            launch(g);
        }
        E().endClip();
        DrawRectangleRoundedLinesEx(r, 0.06f, 12, 2, alpha(lighten(g.color, 0.3f), 0.5f));
    }

    // ============================================================ shelf
    void drawShelf() {
        float x0 = kHero.x, y = kShelfY;
        ui::text(kCatName[g_cat], x0 + 2, y - 24, 18, WHITE, ui::Left, true);
        ui::text(plural((int)list_.size(), "game", "games"), x0 + 12 + ui::measure(kCatName[g_cat], 18, true), y - 24, 14, col::muted, ui::Left, true);
        ui::text("A / D  browse      W / S  category      ENTER  play", VW - 18, y - 24, 12, alpha(col::muted, 0.6f), ui::Right, true);
        Rectangle area{x0 - 10, y - 12, shelfW() + 28, kCardH + 30};
        Vector2 m = E().mouse();
        int clicked = -1;
        for (int i = 0; i < (int)list_.size(); i++) {
            float appear = easeOutBack(clamp01((shelfT_ - i * 0.035f) / 0.35f));
            float x = x0 + i * (kCardW + kCardGap) - scroll_;
            if (x > VW + 20 || x + kCardW < x0 - 40) continue;
            Rectangle r{x, y + (1 - appear) * 40, kCardW, kCardH};
            const Game& g = games()[list_[i]];
            bool over = CheckCollisionPointRec(m, r) && CheckCollisionPointRec(m, area) && !confirmQuit_;
            if (over && (Vector2Length(GetMouseDelta()) > 0 || E().mousePressed())) setFocus(i);
            bool foc = i == focus_;
            float& h = cardHover_[list_[i]];
            h = damp(h, foc ? 1.0f : 0.0f, 14, GetFrameTime());
            r.y -= h * 8;
            E().beginClip(area);
            if (h > 0.01f) {
                fx::beginAdd();
                fx::glow(center(r), kCardW * 0.9f, alpha(g.color, 0.3f * h));
                fx::endAdd();
            }
            fx::softShadow({r.x + r.width / 2, r.y + r.height + 4}, r.width * 0.5f, 14, 0.5f);
            Rectangle art{r.x, r.y, r.width, 144};
            DrawRectangleRounded(r, 0.1f, 10, rgba(0x150e26ff));
            E().endClip();
            float cx0 = std::max(art.x, area.x), cx1 = std::min(art.x + art.width, area.x + area.width);
            if (cx1 > cx0) {
                E().beginClip({cx0, art.y, cx1 - cx0, art.height});
                drawPreview(g, art, 1);
                E().endClip();
            }
            E().beginClip(area);
            // Cover typography is part of the artwork. Keep UI badges in the
            // footer so they never cover a title or a character's face.
            ui::text(g.chips.empty() ? "" : g.chips[0], r.x + 12, r.y + 169, 11, mix(col::muted, lighten(g.color, 0.5f), h), ui::Left, true);
            if (!g.tag.empty()) {
                float tw = ui::measure(g.tag, 10, true) + 14;
                Rectangle badge{r.x + r.width - tw - 12, r.y + 159, tw, 20};
                DrawRectangleRounded(badge, 1, 8, tagColor(g.tag));
                ui::text(g.tag, badge.x + tw / 2, badge.y + 10, 10, col::outline, ui::Center, true);
            } else {
                ui::text(foc ? "PLAY >" : ">", r.x + r.width - 14, r.y + 169, 11, alpha(WHITE, 0.45f + h * 0.55f), ui::Right, true);
            }
            DrawRectangleRoundedLinesEx(r, 0.1f, 10, foc ? 2.5f : 1.2f, foc ? alpha(WHITE, 0.15f + 0.75f * h) : alpha(WHITE, 0.1f));
            E().endClip();
            if (over && E().mousePressed()) clicked = i;
        }
        if (clicked >= 0) {
            E().consumeClick();
            launch(games()[list_[clicked]]);
        }
        // scroll arrows
        float ms = maxScroll();
        for (int side : {-1, 1}) {
            bool can = side < 0 ? scrollTarget_ > 1 : scrollTarget_ < ms - 1;
            if (!can) continue;
            Vector2 p{side < 0 ? x0 + 6.0f : VW - 24.0f, y + kCardH / 2};
            bool over = Vector2Distance(m, p) < 20 && !confirmQuit_;
            DrawCircleV({p.x, p.y + 3}, 20, alpha(BLACK, 0.4f));
            DrawCircleV(p, 20, over ? WHITE : alpha(WHITE, 0.85f));
            DrawLineEx({p.x - side * 4, p.y - 8}, {p.x + side * 4, p.y}, 3.5f, col::bgDeep);
            DrawLineEx({p.x + side * 4, p.y}, {p.x - side * 4, p.y + 8}, 3.5f, col::bgDeep);
            if (over && E().mousePressed()) {
                E().consumeClick();
                E().sfx.play(Sfx::Click);
                scrollTarget_ = std::clamp(scrollTarget_ + side * (kCardW + kCardGap) * 3, 0.0f, ms);
            }
        }
    }

    void draw() override {
        drawBackdrop();
        drawSidebar();
        drawTopBar();
        drawHero();
        drawShelf();
        if (confirmQuit_) drawQuitConfirm();
    }

private:
    void secretClick(const char* who) {
        if (t_ - lastSecret_ > 4) secret_.clear();
        lastSecret_ = t_;
        secret_.push_back(who);
        if (secret_.size() > 5) secret_.erase(secret_.begin());
        static const std::vector<std::string> code = {"pieter", "pieter", "kira", "pieter", "davi"};
        if (secret_ == code) {
            secret_.clear();
            E().sfx.play(Sfx::Win);
            E().flash(col::green, 0.4f);
            E().toast("ADMIN PANEL UNLOCKED", col::green);
            E().go(makeAdmin);
        }
    }

    void drawQuitConfirm() {
        DrawRectangle(0, 0, VW, VH, alpha(BLACK, 0.55f));
        Rectangle r{VW / 2.0f - 220, VH / 2.0f - 100, 440, 210};
        ui::panel(r);
        art::pieter({r.x + r.width / 2, r.y}, 34, P().hat);
        ui::text("Leave Pieter alone?", r.x + r.width / 2, r.y + 62, 30, col::text, ui::Center, true);
        ui::text("He will miss you. Probably.", r.x + r.width / 2, r.y + 98, 18, col::muted, ui::Center);
        if (ui::button({r.x + 30, r.y + 135, 180, 52}, "STAY", col::purple)) confirmQuit_ = false;
        if (ui::button({r.x + 230, r.y + 135, 180, 52}, "QUIT", col::red)) E().quit();
    }

    std::vector<int> list_;
    int focus_ = 0, heroFrom_ = -1;
    float t_ = 0, spawn_ = 0, lastSecret_ = -10, heroT_ = 1, shelfT_ = 0, scroll_ = 0, scrollTarget_ = 0, catY_ = 0, playHover_ = 0, shownCoins_ = -1;
    float crewHover_[4]{}, dockHover_[4]{}, catHover_[CatCount]{}, cardHover_[64]{};
    std::vector<std::string> secret_;
    Vector2 parallax_{};
    bool confirmQuit_ = false, launching_ = false;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeMenu() { return std::make_unique<MenuScene>(); }

}  // namespace pg
