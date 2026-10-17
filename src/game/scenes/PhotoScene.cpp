// NATUURFOTOGRAAF - a photo safari through Dutch nature.
// You glide along a route (De Veluwe, De Polder, De Wadden) and take real
// photos: the viewfinder follows the mouse, the scroll wheel zooms, click to
// shoot. SPACE whistles (animals look up, some run), Q throws bread (hungry
// animals come closer). Afterwards every photo is judged on size, framing,
// centering and pose, and the best shot of each species counts. New species go
// into the Natuurgids.
#include "engine/Fx.hpp"
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <rlgl.h>

#include <algorithm>
#include <vector>

namespace pg {
namespace {

enum class Pose { Idle, Walk, Leap, Dip, Rear, Roll, Hop, Jump, Fly, Run };

struct Species {
    const char* name;
    const char* tex;  // nullptr: drawn in code (the horse)
    float h;          // height in px at the nearest layer
    int value;
    int route;        // 0 Veluwe, 1 Polder, 2 Wadden, -1 rare everywhere
    Pose special;
    bool shy;         // runs off when you whistle
    bool hungry;      // comes for bread
    bool flies;
};

const Species kSp[] = {
    {"Ree", "spr/ani_deer", 170, 250, 0, Pose::Leap, true, false, false},
    {"Wild zwijn", "spr/ani_boar", 100, 220, 0, Pose::Dip, false, true, false},
    {"Vos", "spr/ani_fox", 92, 320, 0, Pose::Leap, true, false, false},
    {"Konijn", "spr/ani_rabbit", 62, 120, 0, Pose::Hop, true, true, false},
    {"Eekhoorn", "spr/ani_squirrel", 64, 200, 0, Pose::Hop, true, true, false},
    {"Grote bonte specht", "spr/ani_woodpecker", 70, 450, 0, Pose::Dip, false, false, false},
    {"Het Paard", nullptr, 160, 777, -1, Pose::Rear, false, false, false},
    {"Koe", "spr/ani_cow", 150, 100, 1, Pose::Dip, false, true, false},
    {"Schaap", "spr/ani_sheep", 92, 110, 1, Pose::Hop, false, true, false},
    {"Ooievaar", "spr/ani_stork", 160, 280, 1, Pose::Dip, false, false, false},
    {"Blauwe reiger", "spr/ani_heron", 170, 260, 1, Pose::Dip, true, false, false},
    {"Haas", "spr/ani_hare", 74, 240, 1, Pose::Leap, true, false, false},
    {"Wilde eend", "spr/duck", 44, 70, 1, Pose::Dip, false, true, false},
    {"Zeehond", "spr/ani_seal", 74, 300, 2, Pose::Roll, false, false, false},
    {"Lepelaar", "spr/ani_spoonbill", 150, 300, 2, Pose::Dip, true, false, false},
    {"Scholekster", "spr/ani_oyster", 66, 150, 2, Pose::Hop, true, true, false},
    {"Strandkrab", "spr/ani_crab", 34, 90, 2, Pose::Hop, false, true, false},
    {"Bruinvis", "spr/ani_porpoise", 70, 600, 2, Pose::Jump, false, false, false},
};
constexpr int kSpecies = sizeof(kSp) / sizeof(kSp[0]);

struct Route {
    const char* name;
    const char* sub;
    Color skyTop, skyBot, far, mid, near;
    std::vector<int> common, rare;
};
const Route kRoutes[3] = {
    {"De Veluwe", "Bos en heide. Herten, zwijnen en wie weet...", rgba(0x7ab8e8ff), rgba(0xe8f0d8ff), rgba(0x4a6a4aff), rgba(0x8a5a8aff), rgba(0x6a8a3aff),
     {0, 1, 2, 3, 4, 0, 1, 3}, {5, 6}},
    {"De Polder", "Molens, sloten en heel veel koeien.", rgba(0x5a9ae8ff), rgba(0xd8ecf8ff), rgba(0x6a9a5aff), rgba(0x5a9a3aff), rgba(0x4a8a2aff),
     {7, 8, 9, 10, 11, 12, 7, 8, 12}, {6, 9}},
    {"De Wadden", "Wad, zand en zon die ondergaat.", rgba(0x5a4a9aff), rgba(0xffb070ff), rgba(0x3a5a8aff), rgba(0xd8b888ff), rgba(0xe8c898ff),
     {13, 14, 15, 16, 13, 15, 16}, {17}},
};

constexpr float kScale[3] = {0.5f, 0.74f, 1.0f};
constexpr float kGround[3] = {455, 545, 668};
constexpr float kSpeed = 82;       // rail speed (near layer, px/s)
constexpr float kRouteTime = 105;
constexpr int kFilm = 24;
constexpr int kPhotoW = 480, kPhotoH = 270;

struct Animal {
    int sp, layer;
    float x;           // world x in its layer
    float yOff = 0;    // e.g. a woodpecker up a tree trunk
    float vx = 0;
    bool flip = false; // facing left
    Pose pose = Pose::Idle;
    float poseT = 0, poseLen = 1, next = 3, alert = 0, flee = 0, t = 0;
    float targetX = -1;  // walking to bread
};

struct Subject { int sp; float score; Pose pose; bool alert; float size, center, visible; };
struct Photo {
    RenderTexture2D rt{};
    std::vector<Subject> subjects;
    int main = -1;  // index into subjects
    float score = 0;
    int stars = 0;
    std::vector<std::string> notes;
};

float hashf(float n) { return std::fmod(std::abs(std::sin(n * 12.9898f) * 43758.5453f), 1.0f); }

enum class Phase { Title, Guide, Ride, Album, Detail };

class PhotoScene : public Scene {
public:
    ~PhotoScene() override {
        clearPhotos();
        if (world_.id) UnloadRenderTexture(world_);
    }

    Presence presence() const override {
        if (phase_ == Phase::Ride) return {"Natuurfotograaf", std::string(kRoutes[route_].name) + " - " + std::to_string(photos_.size()) + "/24 photos"};
        return {"Natuurfotograaf", "Looking at the photo album"};
    }

    void enter() override {
        q_ = E().smooth() ? 2 : 1;
        world_ = LoadRenderTexture(VW * q_, VH * q_);
        SetTextureFilter(world_.texture, TEXTURE_FILTER_BILINEAR);
        if (const char* r = std::getenv("PG_PHOTO")) {  // dev hook: PG_PHOTO=<route 1-3> starts riding
            startRide(std::clamp(std::atoi(r), 1, 3) - 1);
            autoShoot_ = std::getenv("PG_PHOTO_AUTO") != nullptr;
            if (const char* t = std::getenv("PG_PHOTO_T")) skipTo(std::atof(t));
        }
    }

    void clearPhotos() {
        for (auto& p : photos_)
            if (p.rt.id) UnloadRenderTexture(p.rt);
        photos_.clear();
    }

    // ============================================================ ride
    void startRide(int r) {
        route_ = r;
        clearPhotos();
        animals_.clear();
        fx_.clear();
        cam_ = 0;
        rideT_ = 0;
        spawnAt_ = 300;
        bread_ = 8;
        whistleT_ = 0;
        breads_.clear();
        flash_ = 0;
        frameW_ = 360;
        phase_ = Phase::Ride;
        rareAt_ = rnd(0.25f, 0.75f) * kRouteTime;
        rareDone_ = false;
        // a few animals already in view so the first seconds aren't empty
        for (float x = 200; x < VW + 200; x += rnd(260, 420)) spawnGroup(x);
        E().sfx.play(Sfx::Whoosh, 0.5f, 0.7f);
    }

    void skipTo(double t) {
        while (rideT_ < t) updateRide(1 / 30.0f, false);
    }

    void spawnGroup(float nearX) {
        const Route& R = kRoutes[route_];
        int sp = R.common[rndi(0, (int)R.common.size() - 1)];
        spawnSpecies(sp, nearX);
    }

    void spawnSpecies(int sp, float nearX) {
        int layer = rndi(0, 2);
        if (sp == 5) layer = rndi(1, 2);
        if (sp == 17) layer = 0;  // out at sea
        if (sp == 12 || sp == 10) layer = route_ == 1 ? 1 : layer;  // at the ditch
        int n = 1;
        if (sp == 7) n = rndi(1, 3);
        if (sp == 8) n = rndi(2, 4);
        if (sp == 12) n = rndi(2, 4);
        if (sp == 1 && chance(0.4f)) n = 2;
        if (sp == 16 || sp == 15) n = rndi(1, 3);
        for (int i = 0; i < n; i++) {
            Animal a;
            a.sp = sp;
            a.layer = layer;
            a.x = nearX + i * rnd(60, 110) * kScale[layer] + camLayer(layer);  // nearX is a screen x
            a.flip = chance(0.5f);
            a.next = rnd(1.5f, 5);
            a.t = rnd(0, 10);
            if (sp == 5) a.yOff = -rnd(90, 150) * kScale[layer];  // up a trunk
            if (sp == 6) {                                         // the horse gallops across
                a.flip = false;
                a.vx = 260;
                a.pose = Pose::Run;
            }
            if (sp == 17) {
                a.pose = Pose::Idle;
                a.yOff = 40;  // under water until it jumps
                a.next = rnd(0.5f, 2);
            }
            animals_.push_back(a);
        }
    }

    // how far a layer has scrolled (world x at the left edge of the screen)
    float camLayer(int l) const { return cam_ * kScale[l]; }
    float screenX(const Animal& a) const { return a.x - camLayer(a.layer); }

    void setPose(Animal& a, Pose p, float len) {
        a.pose = p;
        a.poseT = 0;
        a.poseLen = len;
    }

    void updateAnimal(Animal& a, float dt) {
        const Species& s = kSp[a.sp];
        a.t += dt;
        a.alert = std::max(0.0f, a.alert - dt);
        a.poseT += dt;
        if (a.flee > 0) {  // running away
            a.x += (a.flip ? -1 : 1) * 380 * kScale[a.layer] * dt;
            a.pose = Pose::Run;
            return;
        }
        if (a.sp == 6) {  // the horse: gallop, rear once mid-screen
            a.x += a.vx * kScale[a.layer] * dt;
            if (a.pose == Pose::Run && screenX(a) > VW * 0.45f && a.poseT > 1 && a.next > 0) {
                setPose(a, Pose::Rear, 1.4f);
                a.next = -1;
                E().sfx.playAt(Sfx::Neigh, screenX(a), 0.6f);
            }
            if (a.pose == Pose::Rear && a.poseT > a.poseLen) setPose(a, Pose::Run, 99);
            return;
        }
        // walk towards bread
        if (a.targetX >= 0) {
            float d = a.targetX - a.x;
            if (std::abs(d) > 8 * kScale[a.layer]) {
                a.flip = d < 0;
                a.x += (d > 0 ? 1 : -1) * 70 * kScale[a.layer] * dt;
                if (a.pose != Pose::Walk) setPose(a, Pose::Walk, 99);
                return;
            }
            a.targetX = -1;
            setPose(a, Pose::Dip, 2.2f);
            return;
        }
        if (a.pose == Pose::Walk) a.x += (a.flip ? -1 : 1) * 30 * kScale[a.layer] * dt;
        if (a.pose == Pose::Leap || a.pose == Pose::Hop) a.x += (a.flip ? -1 : 1) * (a.pose == Pose::Leap ? 160 : 60) * kScale[a.layer] * dt;
        if (a.pose != Pose::Idle && a.pose != Pose::Walk && a.poseT >= a.poseLen) setPose(a, Pose::Idle, 99);
        a.next -= dt;
        if (a.next <= 0) {
            a.next = rnd(2.5f, 6.5f);
            float r = rnd();
            if (a.sp == 17) {
                setPose(a, Pose::Jump, 1.3f);
                E().sfx.playAt(Sfx::Splash, screenX(a), 0.3f, 1.3f);
            } else if (a.sp == 5) {
                setPose(a, Pose::Dip, 1.4f);  // pecking
                E().sfx.playAt(Sfx::Key, screenX(a), 0.3f, 0.6f);
            } else if (r < 0.35f) {
                setPose(a, s.special, s.special == Pose::Leap ? 0.9f : s.special == Pose::Roll ? 1.6f : 1.2f);
            } else if (r < 0.7f && a.sp != 13 && a.sp != 9) {
                setPose(a, Pose::Walk, 99);
                a.flip = chance(0.5f);
                a.next = rnd(1.5f, 3.5f);
            } else {
                setPose(a, Pose::Idle, 99);
            }
        }
    }

    void whistle() {
        if (whistleT_ > 0) return;
        whistleT_ = 1.6f;
        E().sfx.play(Sfx::Whistle, 0.6f);
        for (auto& a : animals_) {
            float x = screenX(a);
            if (x < -100 || x > VW + 100 || a.sp == 17 || a.sp == 6) continue;
            a.alert = 2.2f;
            if (a.pose == Pose::Idle || a.pose == Pose::Walk) setPose(a, Pose::Rear, 0.8f);
            if (kSp[a.sp].shy && chance(0.45f)) {
                a.flee = 1;
                a.flip = chance(0.5f);
            }
        }
        fx_.floatText("fwiet!", E().mouse(), WHITE, 22);
    }

    struct Bread { Vector2 p, v; int layer; float targetY; };

    void throwBread() {
        if (bread_ <= 0) {
            E().sfx.play(Sfx::Error, 0.3f);
            return;
        }
        bread_--;
        Vector2 m = E().mouse();
        int layer = 0;
        float best = 1e9f;
        for (int l = 0; l < 3; l++)
            if (std::abs(kGround[l] - m.y) < best) best = std::abs(kGround[l] - m.y), layer = l;
        Vector2 from{VW / 2.0f, (float)VH + 20};
        float T = 0.8f;
        Vector2 v{(m.x - from.x) / T, (kGround[layer] - from.y - 0.5f * 900 * T * T) / T};
        breads_.push_back({from, v, layer, kGround[layer]});
        E().sfx.play(Sfx::Whoosh, 0.4f, 1.5f);
    }

    void landBread(const Bread& b) {
        float wx = b.p.x + camLayer(b.layer);
        fx_.burst(b.p, 8, rgba(0xe8c890ff), 120, 3, 0.4f, PShape::Square, 400);
        for (auto& a : animals_) {
            if (a.layer != b.layer || !kSp[a.sp].hungry || a.flee > 0) continue;
            if (std::abs(a.x - wx) < 420 * kScale[a.layer]) a.targetX = wx + rnd(-30, 30) * kScale[a.layer];
        }
    }

    void updateRide(float dt, bool input) {
        rideT_ += dt;
        float spd = kSpeed * (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 0.45f : IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1.7f : 1.0f);
        if (!input) spd = kSpeed;
        cam_ += spd * dt;
        whistleT_ = std::max(0.0f, whistleT_ - dt);
        flash_ = std::max(0.0f, flash_ - dt * 5);
        // spawning ahead
        while (spawnAt_ < cam_ + VW + 300) {
            spawnGroup(spawnAt_ - cam_ + 0);
            spawnAt_ += rnd(320, 560);
        }
        if (!rareDone_ && rideT_ > rareAt_) {
            rareDone_ = true;
            const auto& rare = kRoutes[route_].rare;
            int sp = rare[rndi(0, (int)rare.size() - 1)];
            if (sp == 6) spawnSpecies(6, -150);
            else spawnSpecies(sp, VW + 120);
        }
        // the occasional stork flying over the polder
        if (route_ == 1 && chance(dt * 0.05f)) flyers_.push_back({VW + 100.0f, rnd(90, 220), rnd(0.6f, 1.0f)});
        for (auto& f : flyers_) f.x -= (60 + kSpeed * 0.3f) * dt;
        std::erase_if(flyers_, [](const Flyer& f) { return f.x < -200; });
        for (auto& a : animals_) updateAnimal(a, dt);
        std::erase_if(animals_, [&](const Animal& a) { float x = screenX(a); return x < -350 || (a.flee > 0 && x > VW + 400) || (a.sp == 6 && x > VW + 400); });
        for (auto& b : breads_) {
            b.v.y += 900 * dt;
            b.p = b.p + b.v * dt;
        }
        for (auto& b : breads_)
            if (b.p.y >= b.targetY && b.v.y > 0) landBread(b);
        std::erase_if(breads_, [](const Bread& b) { return b.p.y >= b.targetY && b.v.y > 0; });
    }

    // ============================================================ photos
    Rectangle frame() const {
        float w = frameW_, h = frameW_ * 9 / 16;
        Vector2 m = autoShoot_ ? autoAim_ : E().mouse();
        float x = std::clamp(m.x - w / 2, 8.0f, VW - w - 8), y = std::clamp(m.y - h / 2, 8.0f, VH - h - 8);
        return {x, y, w, h};
    }

    // where an animal is on screen (for drawing and judging)
    Rectangle bounds(const Animal& a, float* rot = nullptr, Vector2* centerOut = nullptr) const {
        const Species& s = kSp[a.sp];
        float sc = kScale[a.layer];
        float h = s.h * sc, w = h;
        if (s.tex) {
            Texture2D& t = E().tex(s.tex);
            w = h * t.width / (float)t.height;
        } else {
            w = h * 1.35f;
        }
        float x = screenX(a), y = kGround[a.layer] + a.yOff;
        float lift = 0, r = 0;
        float k = clamp01(a.poseT / a.poseLen);
        switch (a.pose) {
            case Pose::Leap: lift = std::sin(k * PI) * h * 0.6f; r = (k - 0.5f) * 30; break;
            case Pose::Hop: lift = std::abs(std::sin(k * PI * 2)) * h * 0.25f; break;
            case Pose::Dip: r = std::sin(k * PI) * 22; break;
            case Pose::Rear: r = -std::sin(k * PI) * 18; lift = std::sin(k * PI) * h * 0.06f; break;
            case Pose::Roll: r = std::sin(k * PI * 2) * 25; break;
            case Pose::Jump: lift = std::sin(k * PI) * h * 2.2f + (1 - std::sin(k * PI)) * -h * 0.6f; r = (0.5f - k) * 70; break;
            case Pose::Walk: lift = std::abs(std::sin(a.t * 9)) * 3 * sc; break;
            case Pose::Run: lift = std::abs(std::sin(a.t * 14)) * 10 * sc; break;
            default: break;
        }
        if (a.sp == 17 && a.pose != Pose::Jump) lift = -h;  // under water
        if (a.flip) r = -r;
        if (rot) *rot = r;
        Vector2 c{x, y - h / 2 - lift};
        if (centerOut) *centerOut = c;
        return {c.x - w / 2, c.y - h / 2, w, h};
    }

    static float overlap(Rectangle a, Rectangle b) {
        float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.width, b.x + b.width), y1 = std::min(a.y + a.height, b.y + b.height);
        return std::max(0.0f, x1 - x0) * std::max(0.0f, y1 - y0);
    }

    void judge(Photo& ph, Rectangle f) {
        float farea = f.width * f.height;
        Vector2 fc = center(f);
        float half = std::sqrt(f.width * f.width + f.height * f.height) / 2;
        int counts[kSpecies]{};
        for (auto& a : animals_) {
            Rectangle b = bounds(a);
            float area = b.width * b.height;
            float vis = overlap(b, f) / std::max(1.0f, area);
            if (vis < 0.45f || (a.sp == 17 && a.pose != Pose::Jump)) continue;
            const Species& s = kSp[a.sp];
            float size = overlap(b, f) / farea;
            float sizeK = size < 0.18f ? 0.25f + 0.75f * size / 0.18f : size > 0.75f ? std::max(0.5f, 1 - (size - 0.75f) * 2) : 1;
            float cen = 1 - Vector2Distance(center(b), fc) / half;
            float cenK = 0.6f + 0.4f * clamp01(cen);
            float visK = vis > 0.9f ? 1 : 0.55f + 0.45f * (vis - 0.45f) / 0.45f;
            bool special = a.pose == s.special || a.pose == Pose::Jump || (a.sp == 6 && a.pose == Pose::Rear);
            float poseK = special ? 2.0f : a.alert > 0 ? 1.35f : 1;
            float score = s.value * sizeK * cenK * visK * poseK;
            counts[a.sp]++;
            ph.subjects.push_back({a.sp, score, a.pose, a.alert > 0, size, cen, vis});
        }
        if (ph.subjects.empty()) {
            ph.notes.push_back("Geen dier te zien...");
            return;
        }
        int best = 0;
        for (size_t i = 0; i < ph.subjects.size(); i++)
            if (ph.subjects[i].score > ph.subjects[best].score) best = (int)i;
        ph.main = best;
        const Subject& m = ph.subjects[best];
        float extra = 0;
        for (size_t i = 0; i < ph.subjects.size(); i++)
            if ((int)i != best) extra += ph.subjects[i].score * (ph.subjects[i].sp == m.sp ? 0.35f : 0.2f);
        ph.score = m.score + extra;
        float rel = ph.score / kSp[m.sp].value;
        ph.stars = rel > 2.0f ? 5 : rel > 1.3f ? 4 : rel > 0.8f ? 3 : rel > 0.45f ? 2 : 1;
        // what the judge says
        bool special = m.pose == kSp[m.sp].special || m.pose == Pose::Jump || (m.sp == 6 && m.pose == Pose::Rear);
        if (special) ph.notes.push_back(m.pose == Pose::Leap ? "Sprong! x2" : m.pose == Pose::Dip ? "Aan het eten! x2" : m.pose == Pose::Roll ? "Rolt rond! x2" : m.pose == Pose::Jump ? "Uit het water! x2" : m.pose == Pose::Rear ? "Steigert! x2" : "Actie! x2");
        else if (m.alert) ph.notes.push_back("Kijkt op! x1.35");
        if (m.size < 0.06f) ph.notes.push_back("Te ver weg");
        else if (m.size > 0.18f && m.size < 0.75f) ph.notes.push_back("Mooi in beeld");
        else if (m.size >= 0.75f) ph.notes.push_back("Te dichtbij");
        if (m.center > 0.8f) ph.notes.push_back("Perfect gecentreerd");
        if (m.visible < 0.9f) ph.notes.push_back("Afgesneden");
        if (counts[m.sp] >= 3) ph.notes.push_back("Groepsfoto!");
        if (kSp[m.sp].route < 0 || m.sp == 5 || m.sp == 17) ph.notes.push_back("ZELDZAAM!");
    }

    void shoot() {
        if ((int)photos_.size() >= kFilm || shotCd_ > 0) {
            if ((int)photos_.size() >= kFilm) E().sfx.play(Sfx::Error, 0.3f);
            return;
        }
        shotCd_ = 0.4f;
        Rectangle f = frame();
        Photo ph;
        ph.rt = LoadRenderTexture(kPhotoW, kPhotoH);
        SetTextureFilter(ph.rt.texture, TEXTURE_FILTER_BILINEAR);
        BeginTextureMode(ph.rt);
        ClearBackground(BLACK);
        // the world RT is stored upside down: take the frame's rows from the bottom
        Rectangle src{f.x * q_, (VH - f.y - f.height) * q_, f.width * q_, -f.height * q_};
        DrawTexturePro(world_.texture, src, {0, 0, (float)kPhotoW, (float)kPhotoH}, {0, 0}, 0, WHITE);
        EndTextureMode();
        judge(ph, f);
        photos_.push_back(std::move(ph));
        flash_ = 1;
        lastFrame_ = f;
        E().sfx.play(Sfx::Shutter, 0.8f);
    }

    // ============================================================ update
    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        shotCd_ = std::max(0.0f, shotCd_ - dt);
        if (phase_ == Phase::Ride) {
            if (E().backPressed()) {
                phase_ = Phase::Title;
                return;
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0) frameW_ = std::clamp(frameW_ - wheel * 40, 170.0f, 620.0f);
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) frameW_ = std::max(170.0f, frameW_ - 300 * dt);
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) frameW_ = std::min(620.0f, frameW_ + 300 * dt);
            if (IsKeyPressed(KEY_SPACE)) whistle();
            if (IsKeyPressed(KEY_Q) || E().rightPressed()) throwBread();
            updateRide(dt, true);
            renderWorld();
            if (autoShoot_) autoPlay(dt);
            else if (E().mousePressed()) {
                E().consumeClick();
                shoot();
            }
            if (rideT_ >= kRouteTime) endRide();
        } else if (E().backPressed()) {
            if (phase_ == Phase::Detail) phase_ = Phase::Album;
            else if (phase_ == Phase::Guide) phase_ = Phase::Title;
            else if (phase_ == Phase::Title) E().go(makeMenu);
            else phase_ = Phase::Title;
        }
    }

    // dev hook: aims at animals and shoots now and then
    void autoPlay(float dt) {
        autoT_ -= dt;
        Rectangle best{};
        float bs = -1;
        for (auto& a : animals_) {
            Rectangle b = bounds(a);
            if (b.x < 40 || b.x + b.width > VW - 40) continue;
            if (b.width * b.height > bs) bs = b.width * b.height, best = b;
        }
        if (bs > 0) {
            autoAim_ = damp(autoAim_, center(best), 6, dt);
            frameW_ = damp(frameW_, std::clamp(best.width * 2.2f, 170.0f, 620.0f), 4, dt);
        }
        if (autoT_ <= 0 && bs > 0) {
            autoT_ = rnd(2.5f, 4.5f);
            shoot();
        }
    }

    void endRide() {
        phase_ = Phase::Album;
        albumT_ = 0;
        // the best photo of each species counts
        float best[kSpecies]{};
        int got = 0;
        for (auto& p : photos_)
            if (p.main >= 0) {
                int sp = p.subjects[p.main].sp;
                best[sp] = std::max(best[sp], p.score);
            }
        total_ = 0;
        newSpecies_.clear();
        for (int i = 0; i < kSpecies; i++)
            if (best[i] > 0) {
                total_ += (int)best[i];
                got++;
                if (!(P().stats.fotoSpecies & (1 << i))) newSpecies_.push_back(i);
                P().stats.fotoSpecies |= 1 << i;
            }
        total_ += got * 50;  // variety bonus
        speciesGot_ = got;
        coins_ = total_ / 20;
        recordBest_ = total_ > P().stats.fotoBest;
        P().stats.fotoBest = std::max(P().stats.fotoBest, total_);
        unlocked_ = false;
        if (total_ >= 1400 && P().stats.fotoRoute < 3 && route_ + 1 >= P().stats.fotoRoute) {
            P().stats.fotoRoute = route_ + 2;
            unlocked_ = true;
        }
        if (coins_ > 0) P().earn(coins_);
        P().save();
        E().sfx.play(Sfx::Win, 0.6f);
    }

    // ============================================================ world rendering (into the RT)
    void renderWorld() {
        BeginTextureMode(world_);
        rlPushMatrix();
        rlScalef((float)q_, (float)q_, 1);
        drawWorld();
        rlPopMatrix();
        EndTextureMode();
    }

    void drawSky(const Route& R) {
        DrawRectangleGradientV(0, 0, VW, 470, R.skyTop, R.skyBot);
        Vector2 sun = route_ == 2 ? Vector2{VW * 0.62f, 380} : Vector2{VW * 0.78f, 110};
        fx::beginAdd();
        fx::glow(sun, route_ == 2 ? 420 : 220, alpha(route_ == 2 ? rgba(0xff9a50ff) : rgba(0xfff0c0ff), 0.6f));
        fx::endAdd();
        DrawCircleV(sun, route_ == 2 ? 46 : 34, route_ == 2 ? rgba(0xffd890ff) : rgba(0xfffbe8ff));
        // drifting clouds
        for (int i = 0; i < 7; i++) {
            float x = std::fmod(i * 260 - cam_ * 0.06f + 4000 * 10, (float)VW + 400) - 200;
            float y = 60 + hashf(i + 3.0f) * 160;
            float s = 0.6f + hashf(i) * 0.7f;
            Color c = route_ == 2 ? alpha(rgba(0xffc8b8ff), 0.8f) : alpha(WHITE, 0.85f);
            for (int k = 0; k < 4; k++) DrawCircleV({x + (k - 1.5f) * 34 * s, y - (k % 2) * 14 * s}, (26 + (k % 2) * 10) * s, c);
            DrawEllipse((int)x, (int)(y + 8 * s), 80 * s, 16 * s, c);
        }
    }

    // repeating decor: calls f(screenX, index) for tiles of width w scrolling at parallax p
    template <class F> void tiles(float w, float p, F f) {
        float off = cam_ * p;
        int i0 = (int)std::floor((off - 300) / w), i1 = (int)std::floor((off + VW + 300) / w);
        for (int i = i0; i <= i1; i++) f(i * w - off, i);
    }

    void pine(Vector2 base, float s, Color c) {
        DrawRectangleRec({base.x - 4 * s, base.y - 30 * s, 8 * s, 30 * s}, darken(c, 0.5f));
        for (int k = 0; k < 4; k++) {
            float y = base.y - 24 * s - k * 30 * s, w = (46 - k * 9) * s;
            DrawTriangle({base.x, y - 48 * s}, {base.x - w, y}, {base.x + w, y}, darken(c, 0.12f * (3 - k)));
            DrawTriangle({base.x, y - 48 * s}, {base.x + w, y}, {base.x + w * 0.2f, y}, alpha(BLACK, 0.12f));
        }
    }

    void farMill(Vector2 base, float s) {
        Color c = rgba(0x4a5a6aff);
        DrawTriangle({base.x - 16 * s, base.y}, {base.x + 16 * s, base.y}, {base.x + 8 * s, base.y - 70 * s}, c);
        DrawTriangle({base.x - 16 * s, base.y}, {base.x + 8 * s, base.y - 70 * s}, {base.x - 8 * s, base.y - 70 * s}, c);
        for (int k = 0; k < 4; k++) DrawLineEx({base.x, base.y - 68 * s}, Vector2{base.x, base.y - 68 * s} + fromAngle(t_ * 0.5f + k * PI / 2, 52 * s), 4 * s, c);
    }

    void drawLayerGround(int l, const Route& R) {
        Color g = l == 0 ? R.far : l == 1 ? R.mid : R.near;
        float y = kGround[l] - (l == 0 ? 20 : l == 1 ? 30 : 40);
        DrawRectangleGradientV(0, (int)y, VW, VH - (int)y, g, darken(g, 0.25f));
        if (route_ == 2 && l == 0) {  // the sea behind the far sandbank
            DrawRectangleGradientV(0, 400, VW, (int)(y - 400) + 30, rgba(0x4a6aa8ff), rgba(0x6a8ac0ff));
            fx::beginAdd();
            for (int i = 0; i < 20; i++) {
                float x = std::fmod(i * 97 - cam_ * 0.3f + 9000, (float)VW);
                fx::glowLine({x, 410 + hashf(i) * 30}, {x + 30, 410 + hashf(i) * 30}, 2, alpha(rgba(0xffd8a0ff), 0.4f));
            }
            fx::endAdd();
            DrawRectangle(0, (int)y + 10, VW, VH, g);
        }
        if (route_ == 0) fx::tiled(E().tex("tex/grass"), {0, y, (float)VW, VH - y}, 0.5f + l * 0.25f, {cam_ * kScale[l] / (0.5f + l * 0.25f), 0}, alpha(g, 0.5f));
        if (route_ == 1 && l == 1) {  // a ditch with water along the mid layer
            Rectangle d{0, kGround[1] + 8, (float)VW, 26};
            fx::tiled(E().tex("tex/water"), d, 0.5f, {cam_ * kScale[1] / 0.5f, t_ * 4}, rgba(0xa8d0ffff));
            DrawRectangleGradientV(0, (int)d.y, VW, 8, alpha(BLACK, 0.3f), alpha(BLACK, 0));
        }
        if (route_ == 0 && l == 1)  // heather
            tiles(90, kScale[1], [&](float x, int i) { DrawEllipse((int)(x + hashf(i) * 60), (int)(kGround[1] + 10), 50, 12, alpha(rgba(0xb06ab0ff), 0.6f)); });
    }

    void drawDecor(int l) {
        float p = kScale[l];
        if (route_ == 0) {
            if (l == 0) tiles(70, p * 0.9f, [&](float x, int i) { pine({x, kGround[0] - 14}, 0.7f + hashf(i) * 0.4f, rgba(0x2a4a34ff)); });
            if (l == 1) tiles(260, p, [&](float x, int i) {
                if (hashf(i * 1.7f) < 0.55f) pine({x + hashf(i) * 100, kGround[1] - 18}, 1.1f + hashf(i + 2.0f) * 0.4f, rgba(0x2e5a3aff));
                else fx::sprite(E().tex("spr/canaltree"), {x + 60, kGround[1] - 120}, 0.85f);
            });
        } else if (route_ == 1) {
            if (l == 0) tiles(520, p, [&](float x, int i) {
                if (hashf(i * 2.3f) < 0.6f) farMill({x + hashf(i) * 200, kGround[0] - 18}, 1.1f);
                else {  // farm
                    Rectangle h{x + 80, kGround[0] - 60, 90, 44};
                    DrawRectangleRec(h, rgba(0x8a4a3aff));
                    DrawTriangle({h.x - 6, h.y}, {h.x + h.width + 6, h.y}, {h.x + h.width / 2, h.y - 40}, rgba(0x3a3a4aff));
                    DrawTriangle({h.x - 6, h.y}, {h.x + h.width / 2, h.y - 40}, {h.x + h.width + 6, h.y}, rgba(0x3a3a4aff));
                }
            });
            if (l == 2) tiles(110, p, [&](float x, int i) {  // fence posts and wire
                DrawRectangleRec({x, kGround[2] - 64, 8, 64}, rgba(0x6a4a2aff));
                DrawLineEx({x, kGround[2] - 54}, {x + 110, kGround[2] - 54}, 1.5f, alpha(rgba(0x3a3a3aff), 0.7f));
                DrawLineEx({x, kGround[2] - 34}, {x + 110, kGround[2] - 34}, 1.5f, alpha(rgba(0x3a3a3aff), 0.7f));
                (void)i;
            });
        } else {
            if (l == 0) tiles(900, p * 0.8f, [&](float x, int i) {
                if (hashf(i * 3.1f) < 0.5f) {  // lighthouse
                    Vector2 b{x + 300, kGround[0] - 16};
                    DrawTriangle({b.x - 14, b.y}, {b.x + 14, b.y}, {b.x + 8, b.y - 110}, rgba(0xe8e0d8ff));
                    DrawTriangle({b.x - 14, b.y}, {b.x + 8, b.y - 110}, {b.x - 8, b.y - 110}, rgba(0xe8e0d8ff));
                    for (int k = 0; k < 3; k++) DrawRectangleRec({b.x - 12 + k * 2.0f, b.y - 30 - k * 30.0f, 24 - k * 4.0f, 12}, rgba(0xc8283aff));
                    fx::beginAdd();
                    fx::glow({b.x, b.y - 116}, 40, alpha(rgba(0xfff0a0ff), 0.6f + 0.4f * std::sin(t_ * 3)));
                    fx::endAdd();
                }
            });
            if (l == 1) tiles(160, p, [&](float x, int i) {  // dune grass
                for (int k = 0; k < 7; k++) {
                    float bx = x + hashf(i + k) * 120;
                    DrawLineEx({bx, kGround[1] - 20}, {bx + (hashf(i * k + 1.0f) - 0.5f) * 30, kGround[1] - 60 - hashf(k + i * 2.0f) * 30}, 2.5f, rgba(0x8a9a4aff));
                }
            });
            if (l == 2) tiles(220, p, [&](float x, int i) {  // shells and wet sand shine
                DrawEllipse((int)(x + hashf(i) * 150), (int)(kGround[2] + 10), 8, 4, rgba(0xf8f0e8ff));
                DrawEllipse((int)(x + 60), (int)(kGround[2] + 24), 90, 6, alpha(rgba(0xffd8a0ff), 0.25f));
            });
        }
    }

    void drawAnimal(const Animal& a) {
        float rot;
        Vector2 c;
        Rectangle b = bounds(a, &rot, &c);
        if (b.x > VW + 50 || b.x + b.width < -50) return;
        const Species& s = kSp[a.sp];
        float sc = kScale[a.layer];
        // shadow on the ground
        if (a.yOff == 0 && a.sp != 17) fx::softShadow({c.x, kGround[a.layer] + 2}, b.width * 0.45f, 8 * sc, 0.35f);
        if (a.sp == 17) {  // porpoise: clipped by the water surface
            if (a.pose != Pose::Jump) return;
            fx::sprite(E().tex(s.tex), c, b.height / E().tex(s.tex).height, rot, WHITE, a.flip);
            if (a.poseT < 0.15f || a.poseT > a.poseLen - 0.15f)
                for (int i = 0; i < 3; i++) DrawEllipseLines((int)c.x, (int)kGround[0], 20 + i * 12, 4 + i * 2, alpha(WHITE, 0.6f));
            return;
        }
        if (a.sp == 5) DrawRectangleRec({c.x - 26 * sc, kGround[a.layer] - 260 * sc, 34 * sc, 260 * sc}, rgba(0x6a4a34ff));  // tree trunk
        if (!s.tex) {
            float ph = a.pose == Pose::Run ? a.t * 14 : 0;
            rlPushMatrix();
            rlTranslatef(c.x, c.y + b.height / 2, 0);
            rlRotatef(rot, 0, 0, 1);
            art::horse({0, 0}, sc * 1.1f, ph, rgba(0xa0582aff), rgba(0x3a2010ff), col::violet, "none", a.flip ? -1 : 1);
            rlPopMatrix();
        } else {
            Texture2D& t = E().tex(s.tex);
            float k = b.height / t.height;
            float breathe = a.pose == Pose::Idle ? 1 + std::sin(a.t * 2.5f) * 0.012f : 1;
            Rectangle dst{c.x, c.y + b.height / 2, t.width * k, t.height * k * breathe};
            Rectangle src{0, 0, a.flip ? -(float)t.width : (float)t.width, (float)t.height};
            DrawTexturePro(t, src, dst, {dst.width / 2, dst.height}, rot, WHITE);
        }
        if (a.alert > 0 && a.alert > 1.6f) {
            float k = (a.alert - 1.6f) / 0.6f;
            ui::textOutline("!", c.x, b.y - 18 * sc, 34 * sc, alpha(col::gold, k), alpha(col::outline, k), 3);
        }
    }

    void drawWorld() {
        const Route& R = kRoutes[route_];
        drawSky(R);
        for (auto& f : flyers_) {
            Texture2D& t = E().tex("spr/ani_stork_fly");
            float flap = 1 + std::sin(t_ * 6 + f.x * 0.01f) * 0.15f;
            DrawTexturePro(t, {0, 0, -(float)t.width, (float)t.height}, {f.x, f.y, t.width * 0.6f * f.s, t.height * 0.6f * f.s * flap}, {t.width * 0.3f * f.s, t.height * 0.3f * f.s}, 0, WHITE);
        }
        // far hills
        tiles(400, 0.12f, [&](float x, int i) {
            float h = 40 + hashf(i) * 50;
            DrawEllipse((int)(x + 200), 440, 300, (int)h, alpha(darken(R.far, 0.1f), 0.7f));
        });
        for (int l = 0; l < 3; l++) {
            drawLayerGround(l, R);
            drawDecor(l);
            for (auto& a : animals_)
                if (a.layer == l) drawAnimal(a);
            for (auto& b : breads_)
                if (b.layer == l) DrawRectangleRounded({b.p.x - 8, b.p.y - 5, 16, 10}, 0.5f, 4, rgba(0xd8a860ff));
            // haze between layers
            if (l < 2) DrawRectangle(0, 0, VW, VH, alpha(R.skyBot, 0.08f));
        }
        // foreground grass / reeds sweeping past
        tiles(46, 1.45f, [&](float x, int i) {
            float h = 30 + hashf(i) * 40;
            Color c = route_ == 2 ? rgba(0x9a8a4aff) : rgba(0x2a5a1aff);
            DrawTriangle({x, VH + 10.0f}, {x + 14, VH + 10.0f}, {x + 4 + std::sin(t_ * 2 + i) * 6, VH - h}, c);
            DrawTriangle({x, VH + 10.0f}, {x + 4 + std::sin(t_ * 2 + i) * 6, VH - h}, {x + 14, VH + 10.0f}, c);
        });
        if (route_ == 2) {  // golden hour
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleGradientV(0, 0, VW, VH, alpha(rgba(0xff8a3aff), 0.0f), alpha(rgba(0xff8a3aff), 0.12f));
            EndBlendMode();
        }
    }

    // ============================================================ draw
    void drawViewfinder() {
        Rectangle f = frame();
        // darken outside the frame like a camera screen
        Color dim = alpha(BLACK, 0.35f);
        DrawRectangleRec({0, 0, (float)VW, f.y}, dim);
        DrawRectangleRec({0, f.y + f.height, (float)VW, VH - f.y - f.height}, dim);
        DrawRectangleRec({0, f.y, f.x, f.height}, dim);
        DrawRectangleRec({f.x + f.width, f.y, VW - f.x - f.width, f.height}, dim);
        // autofocus box on the most interesting subject in the frame
        float bestA = 0;
        Rectangle af{};
        for (auto& a : animals_) {
            Rectangle b = bounds(a);
            float ov = overlap(b, f);
            if (ov > bestA && ov / std::max(1.0f, b.width * b.height) > 0.45f && !(a.sp == 17 && a.pose != Pose::Jump)) bestA = ov, af = b;
        }
        if (bestA > 0) {
            Color ac = alpha(col::green, 0.9f);
            float L = 12;
            Vector2 cs[4] = {{af.x, af.y}, {af.x + af.width, af.y}, {af.x, af.y + af.height}, {af.x + af.width, af.y + af.height}};
            for (int i = 0; i < 4; i++) {
                float sx = i % 2 ? -1.0f : 1.0f, sy = i < 2 ? 1.0f : -1.0f;
                DrawLineEx(cs[i], cs[i] + Vector2{sx * L, 0}, 2, ac);
                DrawLineEx(cs[i], cs[i] + Vector2{0, sy * L}, 2, ac);
            }
        }
        // frame brackets, thirds and centre
        Color w = alpha(WHITE, 0.9f);
        float L = 26;
        Vector2 cs[4] = {{f.x, f.y}, {f.x + f.width, f.y}, {f.x, f.y + f.height}, {f.x + f.width, f.y + f.height}};
        for (int i = 0; i < 4; i++) {
            float sx = i % 2 ? -1.0f : 1.0f, sy = i < 2 ? 1.0f : -1.0f;
            DrawLineEx(cs[i], cs[i] + Vector2{sx * L, 0}, 3, w);
            DrawLineEx(cs[i], cs[i] + Vector2{0, sy * L}, 3, w);
        }
        for (int k = 1; k < 3; k++) {
            DrawLineEx({f.x + f.width * k / 3, f.y}, {f.x + f.width * k / 3, f.y + f.height}, 1, alpha(WHITE, 0.18f));
            DrawLineEx({f.x, f.y + f.height * k / 3}, {f.x + f.width, f.y + f.height * k / 3}, 1, alpha(WHITE, 0.18f));
        }
        Vector2 c = center(f);
        DrawLineEx({c.x - 10, c.y}, {c.x + 10, c.y}, 1.5f, w);
        DrawLineEx({c.x, c.y - 10}, {c.x, c.y + 10}, 1.5f, w);
        ui::text(TextFormat("%.1fx", 620 / frameW_), f.x + 8, f.y + f.height - 14, 14, w, ui::Left, true);
        DrawCircleV({f.x + f.width - 16, f.y + 16}, 5, std::fmod(t_, 1) < 0.5f ? col::red : darken(col::red, 0.4f));
        if (flash_ > 0) {
            Rectangle lf = lastFrame_;
            DrawRectangleRec(lf, alpha(WHITE, 0.85f * flash_));
        }
    }

    void drawRideHud() {
        // film strip: the photos you took
        int n = (int)photos_.size();
        Rectangle strip{16, 12, 330, 54};
        DrawRectangleRounded(strip, 0.3f, 8, alpha(col::bgDeep, 0.75f));
        ui::text(TextFormat("FILM %d/%d", kFilm - n, kFilm), strip.x + 14, strip.y + 18, 18, n >= kFilm ? col::red : WHITE, ui::Left, true);
        ui::text(TextFormat("BROOD %d   FLUITEN %s", bread_, whistleT_ > 0 ? "..." : "OK"), strip.x + 14, strip.y + 40, 13, col::muted, ui::Left, true);
        for (int i = 0; i < std::min(n, 4); i++) {
            const Photo& p = photos_[n - 1 - i];
            float slide = i == 0 ? (1 - clamp01((0.4f - shotCd_) / 0.3f)) : 0;
            Rectangle th{strip.x + strip.width + 10 + i * 78.0f, strip.y + 4 - slide * 80, 72, 40.5f};
            DrawRectangleRec(shrink(th, -3), WHITE);
            DrawTexturePro(p.rt.texture, {0, 0, (float)kPhotoW, -(float)kPhotoH}, th, {0, 0}, 0, WHITE);
        }
        // route progress
        Rectangle pr{VW - 336.0f, 18, 320, 40};
        DrawRectangleRounded(pr, 0.4f, 8, alpha(col::bgDeep, 0.75f));
        ui::text(kRoutes[route_].name, pr.x + 14, pr.y + 13, 15, WHITE, ui::Left, true);
        DrawRectangleRounded({pr.x + 14, pr.y + 26, pr.width - 28, 6}, 1, 4, alpha(WHITE, 0.2f));
        DrawRectangleRounded({pr.x + 14, pr.y + 26, (pr.width - 28) * clamp01(rideT_ / kRouteTime), 6}, 1, 4, col::green);
        if (rideT_ < 7) {
            float a = clamp01((7 - rideT_) / 1.5f);
            Rectangle hp{VW / 2.0f - 330, VH - 74.0f, 660, 50};
            DrawRectangleRounded(hp, 0.4f, 8, alpha(col::bgDeep, 0.7f * a));
            ui::text("CLICK = foto   SCROLL = zoom   SPACE = fluiten   Q / RECHTS = brood   A/D = trager/sneller", VW / 2.0f, hp.y + 25, 15, alpha(WHITE, a), ui::Center, true);
        }
    }

    void drawTitle() {
        art::purpleBackdrop(t_);
        fx::gradientText("NATUURFOTOGRAAF", VW / 2.0f, 70, 64, rgba(0xd8ffd0ff), col::green, 5);
        ui::text("Pak je camera. Kies een route.", VW / 2.0f, 120, 20, col::muted, ui::Center, true);
        int unlocked = std::clamp(P().stats.fotoRoute, 1, 3);
        for (int i = 0; i < 3; i++) {
            Rectangle r{110 + i * 360.0f, 170, 340, 360};
            bool open = i < unlocked;
            bool hov = open && ui::hovered(r);
            float lift = hov ? -6 : 0;
            r.y += lift;
            const Route& R = kRoutes[i];
            DrawRectangleRounded({r.x, r.y + 8, r.width, r.height}, 0.08f, 10, alpha(BLACK, 0.4f));
            DrawRectangleRounded(shrink(r, -3), 0.08f, 10, hov ? WHITE : col::outline);
            // little landscape preview
            Rectangle pv{r.x, r.y, r.width, 200};
            E().beginClip(pv);
            DrawRectangleGradientV((int)pv.x, (int)pv.y, (int)pv.width, (int)pv.height, R.skyTop, R.skyBot);
            DrawRectangleGradientV((int)pv.x, (int)(pv.y + 120), (int)pv.width, 80, R.mid, darken(R.near, 0.2f));
            const char* show[3] = {"spr/ani_deer", "spr/ani_cow", "spr/ani_seal"};
            fx::sprite(E().tex(show[i]), {pv.x + pv.width / 2, pv.y + 130}, i == 1 ? 0.55f : 0.6f);
            E().endClip();
            DrawRectangleRec({r.x, r.y + 200, r.width, r.height - 200}, col::panel);
            ui::text(R.name, r.x + 20, r.y + 232, 26, WHITE, ui::Left, true);
            ui::paragraph(R.sub, {r.x + 20, r.y + 256, r.width - 40, 0}, 15, col::muted);
            int found = 0, total = 0;
            for (int s = 0; s < kSpecies; s++)
                if (kSp[s].route == i) {
                    total++;
                    found += (P().stats.fotoSpecies >> s) & 1;
                }
            ui::text(TextFormat("%d/%d soorten gefotografeerd", found, total), r.x + 20, r.y + 320, 14, col::green, ui::Left, true);
            if (!open) {
                DrawRectangleRounded(r, 0.08f, 10, alpha(col::bgDeep, 0.75f));
                ui::text("LOCKED", r.x + r.width / 2, r.y + 150, 30, col::muted, ui::Center, true);
                ui::text("Score 1400+ op de vorige route", r.x + r.width / 2, r.y + 184, 14, col::muted, ui::Center, true);
            } else if (hov && E().mousePressed()) {
                E().consumeClick();
                startRide(i);
                return;
            }
        }
        ui::text(TextFormat("Beste rit: %d punten", P().stats.fotoBest), VW / 2.0f, 566, 18, col::gold, ui::Center, true);
        if (ui::button({VW / 2.0f - 150, 600, 300, 56}, "NATUURGIDS", rgba(0x2a8a4aff), 22)) phase_ = Phase::Guide;
        if (ui::backButton()) E().go(makeMenu);
    }

    void drawGuide() {
        art::purpleBackdrop(t_);
        fx::gradientText("NATUURGIDS", VW / 2.0f, 60, 56, rgba(0xd8ffd0ff), col::green, 4);
        int got = 0;
        for (int i = 0; i < kSpecies; i++) got += (P().stats.fotoSpecies >> i) & 1;
        ui::text(TextFormat("%d van de %d soorten", got, kSpecies), VW / 2.0f, 104, 18, col::muted, ui::Center, true);
        for (int i = 0; i < kSpecies; i++) {
            Rectangle r{70 + (i % 6) * 192.0f, 140 + (i / 6) * 176.0f, 180, 164};
            bool have = (P().stats.fotoSpecies >> i) & 1;
            ui::panel(r, have ? col::panelHi : alpha(col::panel, 0.8f), 0.1f, have ? alpha(col::green, 0.6f) : col::outline);
            Vector2 c{r.x + r.width / 2, r.y + 72};
            Color tint = have ? WHITE : rgba(0x0a0612ff);
            if (kSp[i].tex) {
                Texture2D& t = E().tex(kSp[i].tex);
                float k = std::min(150.0f / t.width, 100.0f / t.height);
                fx::sprite(t, c, k, 0, tint);
            } else {
                art::horse({c.x - 10, c.y + 46}, 0.55f, 0, have ? rgba(0xa0582aff) : rgba(0x0a0612ff), have ? rgba(0x3a2010ff) : rgba(0x0a0612ff), have ? col::violet : rgba(0x0a0612ff), "none");
            }
            ui::text(have ? kSp[i].name : "???", r.x + r.width / 2, r.y + 140, 15, have ? WHITE : col::muted, ui::Center, true);
            if (kSp[i].route < 0 || kSp[i].value >= 450) ui::text("zeldzaam", r.x + r.width / 2, r.y + 156, 11, col::gold, ui::Center, true);
        }
        if (ui::backButton()) phase_ = Phase::Title;
    }

    Rectangle thumbRect(int i) const { return {60 + (i % 6) * 196.0f, 150 + (i / 6) * 118.0f, 184, 103.5f}; }

    void drawAlbum(bool interactive = true) {
        albumT_ += GetFrameTime();
        art::purpleBackdrop(t_);
        fx::gradientText("FOTOBOEK", 260, 54, 52, rgba(0xd8ffd0ff), col::green, 4);
        ui::text(std::string(kRoutes[route_].name) + "  -  " + std::to_string(photos_.size()) + " foto's", 260, 98, 18, col::muted, ui::Center, true);
        // score summary
        Rectangle sm{560, 20, 690, 110};
        ui::panel(sm, alpha(col::panel, 0.95f), 0.15f, alpha(col::green, 0.5f));
        ui::textShadow(TextFormat("%d", (int)(total_ * clamp01(albumT_ / 1.2f))), sm.x + 30, sm.y + 46, 46, WHITE, ui::Left, true, 3);
        ui::text(TextFormat("punten  -  %d soorten (+%d)", speciesGot_, speciesGot_ * 50), sm.x + 30, sm.y + 84, 15, col::muted, ui::Left, true);
        ui::coin({sm.x + 420, sm.y + 40}, 14);
        ui::text("+" + fmtNum(coins_), sm.x + 442, sm.y + 40, 24, col::gold, ui::Left, true);
        if (recordBest_) ui::text("NIEUW RECORD!", sm.x + 420, sm.y + 74, 15, col::gold, ui::Left, true);
        if (unlocked_) ui::text("Nieuwe route vrijgespeeld!", sm.x + 420, sm.y + 94, 14, col::green, ui::Left, true);
        if (!newSpecies_.empty()) ui::text(TextFormat("%d nieuw in de Natuurgids", (int)newSpecies_.size()), sm.x + 600, sm.y + 74, 13, col::green, ui::Center, true);
        // the photos
        for (size_t i = 0; i < photos_.size(); i++) {
            float pop = easeOutBack(clamp01((albumT_ - i * 0.04f) / 0.3f));
            if (pop <= 0) continue;
            Rectangle r = thumbRect((int)i);
            Vector2 c = center(r);
            r = {c.x - r.width * pop / 2, c.y - r.height * pop / 2, r.width * pop, r.height * pop};
            bool hov = interactive && ui::hovered(r);
            float rot = (hashf(i * 1.3f) - 0.5f) * 4;
            Rectangle fr = shrink(r, -6);
            DrawRectanglePro({fr.x + fr.width / 2 + 3, fr.y + fr.height / 2 + 5, fr.width, fr.height}, {fr.width / 2, fr.height / 2}, rot, alpha(BLACK, 0.4f));
            DrawRectanglePro({fr.x + fr.width / 2, fr.y + fr.height / 2, fr.width, fr.height}, {fr.width / 2, fr.height / 2}, rot, hov ? col::gold : WHITE);
            DrawTexturePro(photos_[i].rt.texture, {0, 0, (float)kPhotoW, -(float)kPhotoH}, {r.x + r.width / 2, r.y + r.height / 2, r.width, r.height}, {r.width / 2, r.height / 2}, rot, WHITE);
            const Photo& p = photos_[i];
            for (int s = 0; s < p.stars; s++) DrawPoly({r.x + 12 + s * 15.0f, r.y + r.height - 10}, 5, 7, -90 + 36, col::gold);
            if (p.main >= 0) ui::textShadow(kSp[p.subjects[p.main].sp].name, r.x + 6, r.y + 12, 12, WHITE, ui::Left, true, 1);
            if (hov && E().mousePressed()) {
                E().consumeClick();
                detail_ = (int)i;
                phase_ = Phase::Detail;
                E().sfx.play(Sfx::Click, 0.4f);
            }
        }
        if (photos_.empty()) ui::text("Je hebt geen enkele foto gemaakt...", VW / 2.0f, 360, 24, col::muted, ui::Center, true);
        if (!interactive) return;
        if (ui::button({VW / 2.0f - 270, 650, 250, 56}, "NOG EEN RIT", rgba(0x2a8a4aff), 22)) startRide(route_);
        if (ui::button({VW / 2.0f + 20, 650, 250, 56}, "ROUTES", col::panelHi, 22)) phase_ = Phase::Title;
    }

    void drawDetail() {
        drawAlbum(false);
        DrawRectangle(0, 0, VW, VH, alpha(col::bgDeep, 0.85f));
        const Photo& p = photos_[detail_];
        Rectangle r{60, 90, 768, 432};
        DrawRectangleRec(shrink(r, -10), WHITE);
        DrawTexturePro(p.rt.texture, {0, 0, (float)kPhotoW, -(float)kPhotoH}, r, {0, 0}, 0, WHITE);
        Rectangle info{860, 90, 360, 432};
        ui::panel(info, alpha(col::panel, 0.97f), 0.06f, alpha(col::green, 0.5f));
        ui::text(p.main >= 0 ? kSp[p.subjects[p.main].sp].name : "Leeg landschap", info.x + 24, info.y + 36, 26, WHITE, ui::Left, true);
        for (int s = 0; s < 5; s++) DrawPoly({info.x + 34 + s * 30.0f, info.y + 78}, 5, 13, -90 + 36, s < p.stars ? col::gold : rgba(0x4a3a6aff));
        ui::textShadow(TextFormat("%d", (int)p.score), info.x + 24, info.y + 128, 40, col::gold, ui::Left, true, 2);
        ui::text("punten", info.x + 30 + ui::measure(TextFormat("%d", (int)p.score), 40, true), info.y + 134, 16, col::muted, ui::Left, true);
        float y = info.y + 180;
        for (auto& n : p.notes) {
            bool good = n.find("x") != std::string::npos || n == "Mooi in beeld" || n == "Perfect gecentreerd" || n == "Groepsfoto!" || n == "ZELDZAAM!";
            ui::text((good ? "+ " : "- ") + n, info.x + 24, y, 17, good ? col::green : col::orange, ui::Left, true);
            y += 30;
        }
        if (p.subjects.size() > 1) ui::text(TextFormat("ook in beeld: %d dier(en)", (int)p.subjects.size() - 1), info.x + 24, y + 10, 14, col::muted, ui::Left, true);
        ui::text(TextFormat("foto %d / %d  -  klik om terug te gaan", detail_ + 1, (int)photos_.size()), r.x, r.y + r.height + 34, 15, col::muted, ui::Left, true);
        if (IsKeyPressed(KEY_RIGHT)) detail_ = std::min((int)photos_.size() - 1, detail_ + 1);
        if (IsKeyPressed(KEY_LEFT)) detail_ = std::max(0, detail_ - 1);
        if (E().mousePressed()) {
            E().consumeClick();
            phase_ = Phase::Album;
        }
    }

    void draw() override {
        switch (phase_) {
            case Phase::Title: drawTitle(); break;
            case Phase::Guide: drawGuide(); break;
            case Phase::Album: drawAlbum(); break;
            case Phase::Detail: drawDetail(); break;
            case Phase::Ride:
                DrawTexturePro(world_.texture, {0, 0, (float)world_.texture.width, -(float)world_.texture.height}, {0, 0, (float)VW, (float)VH}, {0, 0}, 0, WHITE);
                drawViewfinder();
                fx_.draw();
                drawRideHud();
                if (rideT_ > kRouteTime - 5) {
                    float a = clamp01((rideT_ - (kRouteTime - 5)) / 0.5f);
                    ui::textOutline(TextFormat("Rit eindigt over %d...", (int)(kRouteTime - rideT_) + 1), VW / 2.0f, 110, 34, alpha(WHITE, a), alpha(col::outline, a), 3);
                }
                break;
        }
    }

private:
    struct Flyer { float x, y, s; };
    Phase phase_ = Phase::Title;
    int route_ = 0, q_ = 1, bread_ = 8, detail_ = 0, total_ = 0, speciesGot_ = 0, coins_ = 0;
    std::vector<Animal> animals_;
    std::vector<Photo> photos_;
    std::vector<Bread> breads_;
    std::vector<Flyer> flyers_;
    std::vector<int> newSpecies_;
    RenderTexture2D world_{};
    float t_ = 0, cam_ = 0, rideT_ = 0, spawnAt_ = 0, whistleT_ = 0, flash_ = 0, frameW_ = 360, shotCd_ = 0, albumT_ = 0, rareAt_ = 50, autoT_ = 1;
    bool rareDone_ = false, autoShoot_ = false, recordBest_ = false, unlocked_ = false;
    Rectangle lastFrame_{};
    Vector2 autoAim_{VW / 2.0f, VH / 2.0f};
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makePhoto() { return std::make_unique<PhotoScene>(); }

}  // namespace pg
