// PieterGame 3 - entry point.
#include "engine/Engine.hpp"
#include "game/BeatChart.hpp"
#include "game/DiscordConfig.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

namespace {

// Dev/testing hooks (all optional):
//   PG_SCENE=classic|room|beats|fnap|horses|stroop|casino|roulette|blackjack|mines|slots|shop|inventory|settings
//   PG_SHOT=out.png PG_SHOT_FRAMES=120   save a screenshot after N frames and quit
//   PG_SHOT_TIME=6.5                     ...or after N seconds of game time
//   PG_GOD=1 PG_WAVE=10                  Classic: invincible / start at a wave
//   PG_RENDER_INTRO=1                    re-render assets/video/intro.{mpg,ogg} (+ media/*.mp4), needs ffmpeg
//   PG_HOUR=21.5                         Pieter Room: pretend it's this time of day
//   PG_ROOM=sleep|shower|feed|chat|play|sad   Pieter Room: start in that state
//   PG_MUTE=1                            silence all audio
//   PG_BEATS=3:2 PG_BEATS_AUTO=1         Pieter Beats: play song 3 on HARD (and let a bot play it)
//   PG_FNAP=3 PG_FNAP_VIEW=left PG_FNAP_STAGE=2,0,3,1,1 PG_FNAP_LIGHT=1 PG_FNAP_HOLD=1 PG_FNAP_SCARE=0
//                                        Five Nights at Pieter's: jump into a night with a frozen setup (screenshots)
//   PG_BEATS_TEST=1                      analyze every song for Pieter Beats, print tempo + note counts, quit
//   PG_DUMP_SFX=dir                      write every synthesized sound effect to dir/*.wav and quit
std::unique_ptr<pg::Scene> firstScene() {
    using namespace pg;
    const std::map<std::string, std::unique_ptr<Scene> (*)()> scenes = {
        {"menu", makeMenu},         {"classic", makeClassic}, {"room", makePieterRoom}, {"horses", makeHorses},
        {"stroop", makeStroopwafel}, {"casino", makeCasino},  {"roulette", makeRoulette}, {"blackjack", makeBlackjack},
        {"mines", makeMines},       {"slots", makeSlots}, {"tower", makeDragonTower}, {"crash", makeCrash}, {"plinko", makePlinko}, {"job", makeJob}, {"fiets", makeFiets}, {"kart", makeKart}, {"dungeon", makeDungeon},     {"shop", makeShop},        {"inventory", makeInventory},
        {"settings", makeSettings}, {"admin", makeAdmin},  {"beats", makeBeats}, {"fnap", makeFnap}, {"artlab", makeArtLab},
    };
    if (const char* s = std::getenv("PG_SCENE")) {
        if (std::string(s) == "intro") return makeIntroVideo();
        if (std::string(s) == "introanim") return makeIntroAnimation();
        auto it = scenes.find(s);
        if (it != scenes.end()) return it->second();
    }
    // first launch: the intro, then the menu
    return P().introSeen ? makeMenu() : makeIntroVideo();
}

// Renders the intro animation frame by frame and pipes it into ffmpeg together
// with the theme song (the animation is keyed to its beats).
int renderIntroVideo() {
    using namespace pg;
    const int w = 1280, h = 720;
    const float fps = 30, dur = introDuration();
    std::string song = E().assetPath("music/Stroopwafel_Rush.mp3");
    std::string outDir = E().assetPath("video");
    std::filesystem::create_directories("media");
    char fade[64];
    std::snprintf(fade, sizeof fade, "afade=t=out:st=%.2f:d=1.5", dur - 1.5f);
    std::string cmd = "ffmpeg -y -v error -f rawvideo -pix_fmt rgba -s " + std::to_string(w) + "x" + std::to_string(h) +
                      " -r 30 -i - -t " + std::to_string(dur) + " -i \"" + song + "\" -filter_complex \"[1:a]" + fade +
                      ",asplit=3[a1][a2][a3]\"" +
                      // the game plays this (MPEG-1 + MP2, decoded by pl_mpeg)
                      " -map 0:v -map [a1] -c:v mpeg1video -r 30 -b:v 6M -maxrate 9M -bufsize 6M -c:a mp2 -b:a 224k \"" + outDir + "/intro.mpg\"" +
                      // its soundtrack as a separate stream for playback
                      " -map [a2] -c:a libvorbis -q:a 6 \"" + outDir + "/intro.ogg\"" +
                      // a shareable copy
                      " -map 0:v -map [a3] -c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p -c:a aac -b:a 256k -movflags +faststart"
                      " media/PieterGame3_intro.mp4";
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) return 1;
    int frames = (int)(dur * fps);
    int written = 0;
    E().renderOffline(makeIntroAnimation(), w, h, fps, frames, [&](const Image& img) {
        fwrite(img.data, 1, (size_t)w * h * 4, pipe);
        if (++written % 30 == 0) std::fprintf(stderr, "\rrendering intro: %d / %d frames", written, frames);
    });
    std::fprintf(stderr, "\n");
    return pclose(pipe) == 0 ? 0 : 1;
}

int dumpSfx(const std::string& dir) {
    using namespace pg;
    std::filesystem::create_directories(dir);
    for (int i = 0; i < (int)Sfx::Count; i++)
        for (int v = 0; v < synth::kVariants; v++) {
            synth::Pcm pcm = synth::render((Sfx)i, v);
            std::vector<short> s16(pcm.data.size());
            for (size_t k = 0; k < s16.size(); k++) s16[k] = (short)(std::clamp(pcm.data[k], -1.0f, 1.0f) * 32000);
            Wave w{(unsigned)pcm.frames(), synth::kRate, 16, 2, s16.data()};
            ExportWave(w, TextFormat("%s/%s_%d.wav", dir.c_str(), synth::name((Sfx)i), v));
        }
    return 0;
}

}  // namespace

int beatsTest() {
    using namespace pg;
    for (int i = 0; i < E().music.trackCount(); i++) {
        double t0 = GetTime();
        beats::Analysis a = beats::analyze(E().music.trackPath(i));
        std::printf("%-40s %6.1fs  %5.1f bpm  easy %4zu  normal %4zu  hard %4zu  (%.1fs)\n", E().music.trackName(i).c_str(), a.duration, a.bpm,
                    beats::buildChart(a, beats::Difficulty::Easy).size(), beats::buildChart(a, beats::Difficulty::Normal).size(),
                    beats::buildChart(a, beats::Difficulty::Hard).size(), GetTime() - t0);
    }
    return 0;
}

int main() {
    using namespace pg;
    SetTraceLogLevel(LOG_WARNING);
    if (const char* dir = std::getenv("PG_DUMP_SFX")) return dumpSfx(dir);
    P().load();
    if (!E().init("PieterGame 3", P().fullscreen, P().resW, P().resH, P().monitor)) return 1;
    E().setSmooth(P().smoothGraphics);

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
    if (std::getenv("PG_BEATS_TEST")) {
        int rc = beatsTest();
        E().shutdown();
        return rc;
    }
    if (P().discordRpc) E().discord.start(discordAppId());

    if (std::getenv("PG_RENDER_INTRO")) {
        int rc = renderIntroVideo();
        E().shutdown();
        return rc;
    }

    E().run(firstScene());

    P().fullscreen = E().fullscreen();
    P().monitor = E().monitorName(E().monitor());
    P().resW = E().resWidth();
    P().resH = E().resHeight();
    P().save();
    E().shutdown();
    return 0;
}
