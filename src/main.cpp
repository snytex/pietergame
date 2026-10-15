// PieterGame 3 - entry point.
#include "engine/Engine.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace {

// Dev/testing hooks (all optional):
//   PG_SCENE=classic|room|horses|stroop|casino|roulette|blackjack|mines|slots|shop|inventory|settings
//   PG_SHOT=out.png PG_SHOT_FRAMES=120   save a screenshot after N frames and quit
//   PG_MUTE=1                            silence all audio
std::unique_ptr<pg::Scene> firstScene() {
    using namespace pg;
    const std::map<std::string, std::unique_ptr<Scene> (*)()> scenes = {
        {"menu", makeMenu},         {"classic", makeClassic}, {"room", makePieterRoom}, {"horses", makeHorses},
        {"stroop", makeStroopwafel}, {"casino", makeCasino},  {"roulette", makeRoulette}, {"blackjack", makeBlackjack},
        {"mines", makeMines},       {"slots", makeSlots},     {"shop", makeShop},        {"inventory", makeInventory},
        {"settings", makeSettings},
    };
    if (const char* s = std::getenv("PG_SCENE")) {
        auto it = scenes.find(s);
        if (it != scenes.end()) return it->second();
    }
    return makeMenu();
}

}  // namespace

int main() {
    using namespace pg;
    SetTraceLogLevel(LOG_WARNING);
    P().load();
    if (!E().init("PieterGame 3", P().fullscreen, P().resW, P().resH)) return 1;

    bool mute = std::getenv("PG_MUTE") != nullptr;
    E().music.setVolume(mute ? 0 : P().musicVolume);
    E().sfx.setVolume(mute ? 0 : P().sfxVolume);
    E().screenShake = P().screenShake;
    if (const char* shot = std::getenv("PG_SHOT")) {
        const char* frames = std::getenv("PG_SHOT_FRAMES");
        E().autoScreenshot(shot, frames ? std::atoi(frames) : 120);
    }

    E().run(firstScene());

    P().fullscreen = E().fullscreen();
    P().resW = E().resWidth();
    P().resH = E().resHeight();
    P().save();
    E().shutdown();
    return 0;
}
