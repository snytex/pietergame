// PieterGame 3 - entry point.
#include "engine/Engine.hpp"
#include "game/DiscordConfig.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace {

// Dev/testing hooks (all optional):
//   PG_SCENE=classic|room|horses|stroop|casino|roulette|blackjack|mines|slots|shop|inventory|settings
//   PG_SHOT=out.png PG_SHOT_FRAMES=120   save a screenshot after N frames and quit
//   PG_SHOT_TIME=6.5                     ...or after N seconds of game time
//   PG_GOD=1 PG_WAVE=10                  Classic: invincible / start at a wave
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
    if (!E().init("PieterGame 3", P().fullscreen, P().resW, P().resH, P().monitor)) return 1;

    bool mute = std::getenv("PG_MUTE") != nullptr;
    E().music.setVolume(mute ? 0 : P().musicVolume);
    E().sfx.setVolume(mute ? 0 : P().sfxVolume);
    E().screenShake = P().screenShake;
    if (const char* shot = std::getenv("PG_SHOT")) {
        const char* frames = std::getenv("PG_SHOT_FRAMES");
        if (const char* secs = std::getenv("PG_SHOT_TIME")) E().autoScreenshotAt(shot, (float)std::atof(secs));
        else E().autoScreenshot(shot, frames ? std::atoi(frames) : 120);
    }

    // Discord shows the coin balance when a scene has nothing more specific to say.
    E().presenceHook = [](Presence& p) {
        if (p.state.empty()) p.state = fmtNum(P().coins) + " Pietercoins";
    };
    if (P().discordRpc) E().discord.start(discordAppId());

    E().run(firstScene());

    P().fullscreen = E().fullscreen();
    P().monitor = E().monitorName(E().monitor());
    P().resW = E().resWidth();
    P().resH = E().resHeight();
    P().save();
    E().shutdown();
    return 0;
}
