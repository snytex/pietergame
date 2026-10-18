#include "game/fnap/Brain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pg::fnap {

const NightCfg kNight[kNights] = {
    // davi kira horse plush pc  crit feint  monkey
    {4, 3, 0, 2, 0, 1, 0.00f, false, "Welcome home"},
    {6, 6, 3, 4, 4, 1, 0.15f, true, "Kira is awake"},
    {9, 9, 7, 7, 6, 2, 0.25f, true, "The horse awakens"},
    {11, 11, 9, 8, 8, 3, 0.35f, true, "Nobody sleeps"},
    {13, 12, 11, 10, 9, 3, 0.45f, true, "The last night"},
    {15, 14, 13, 12, 11, 3, 0.55f, true, "JUHUH NIGHT"},
};

namespace {
constexpr float kDoorPatience = 9.0f;  // seconds at the door before coming in (minus 0.7 per night)
}

int Brain::hour() const { return std::min(6, (int)(t / kHourLen)); }
float Brain::speed() const { return frozen || mood == Mood::Hush ? 0.0f : pc ? 1.8f : 1.0f; }

float Brain::danger() const {
    float d = 0;
    for (auto& h : hall) d = std::max(d, h.pos == 4 ? 1.0f : h.pos * 0.22f);
    d = std::max(d, horse / 4.0f);
    d = std::max(d, plush / 3.0f);
    return d;
}

int Brain::critical() const {
    int n = (hall[0].pos == 4) + (hall[1].pos == 4) + (horse >= 3) + (plush >= 3);
    return n;
}
bool Brain::mayGoCritical() const { return critical() < kNight[night].maxCritical && mood != Mood::Relax; }

float Brain::moodScale() const {
    switch (mood) {
        case Mood::Calm: return 0;
        case Mood::Build: return 1;
        case Mood::Peak: return 1.35f;
        case Mood::Relax: return 0.45f;
        case Mood::Hush: return 0;
    }
    return 1;
}

void Brain::start(int n) {
    night = n;
    t = 0;
    for (int s = 0; s < 2; s++) {
        hall[s] = Stalker{};
        hall[s].think = s == 0 ? 6 : 8;
    }
    horse = 0;
    horseOpenT = horseHold = horseLook = 0;
    horseAnim = 9;
    plush = 0;
    plushCall = plushLit = 0;
    pc = false;
    monkey = monkeyRiser = false;
    monkeyT = monkeyStare = 0;
    monkeyBangT = 9;
    battery = 1;
    noise = intensity = 0;
    mood = Mood::Calm;
    moodT = 0;
    moodLen_ = 14 - n;
    over = won = false;
    closetNeglect_ = bedNeglect_ = 0;
    horseThink_ = 8;
    plushThink_ = 10;
    pcThink_ = 20;
    pcSong_ = 0;
    monkeyNext_ = rnd(35, 60);
    knockT_ = rnd(30, 55);
    whisperT_ = rnd(12, 25);
    peakT_ = strainT_ = 0;
    snortT_ = 6;
    lastHour_ = 0;
    wasMoving_ = wasHold_ = lowWarned_ = false;
    hushDone_[0] = hushDone_[1] = false;
}

void Brain::scare(Who w, std::vector<Event>& out, const char* why) {
    if (over || frozen) return;
    over = true;
    cause = why;
    killer = w;
    out.push_back({Ev::Jumpscare, (int)w});
}

// ------------------------------------------------------------------ update

void Brain::update(float dt, const Input& in, std::vector<Event>& out) {
    if (over || won) return;
    t += dt;
    if (hour() != lastHour_) {
        lastHour_ = hour();
        out.push_back({Ev::Hour, lastHour_});
    }
    if (t >= 6 * kHourLen) {
        won = true;
        out.push_back({Ev::Win});
        return;
    }
    const bool moving = in.moving;
    const bool away = in.view != View::Center;
    // the flashlight: drains away from the desk, charges in its dock on the desk
    bool lightOn = in.light && away && !moving && battery > 0;
    if (lightOn) battery = std::max(0.0f, battery - dt / (68 - night * 3.0f));
    else if (in.view == View::Center && !moving) battery = std::min(1.0f, battery + dt / 22);
    if (battery < 0.2f && !lowWarned_) {
        lowWarned_ = true;
        out.push_back({Ev::BatteryLow});
    }
    if (battery > 0.3f) lowWarned_ = false;
    if (lightOn && battery <= 0) out.push_back({Ev::BatteryDead});
    bool canHold = in.view == View::Left || in.view == View::Right || in.view == View::Closet;
    bool hold = in.hold && canHold && !moving;

    // noise: running around and slamming doors carries through the house
    noise = std::max(0.0f, noise - dt * 0.07f);
    if (moving && !wasMoving_) noise += 0.08f;
    if (hold && !wasHold_) noise += 0.05f;
    if (pc) noise += dt * 0.2f;
    noise = std::min(noise, 1.0f);
    wasMoving_ = moving;
    wasHold_ = hold;

    director(dt, out);
    for (int s = 0; s < 2 && !over; s++) stalker(s, dt, in, lightOn, hold, out);
    if (!over) closet(dt, in, hold, out);
    if (!over) bed(dt, in, lightOn, out);
    if (!over) window(dt, in, out);
    if (over) return;

    // Kira's prank: the PC plays a music box and everything in the house speeds up
    if (night >= 1 && !pc && !frozen) {
        pcThink_ -= dt;
        if (pcThink_ <= 0) {
            pcThink_ = 18;
            if (roll(level(kNight[night].pc) / 20.0f * std::max(0.5f, moodScale()))) {
                pc = true;
                pcSong_ = 0;
                out.push_back({Ev::PcHack});
            }
        }
    }
    if (pc) {
        pcSong_ -= dt;
        if (pcSong_ <= 0) {
            pcSong_ = 7.2f;
            out.push_back({Ev::PcSong});
        }
        if (in.pcOff && in.view == View::Center && !moving) {
            pc = false;
            pcThink_ = 18;
            out.push_back({Ev::PcOff});
        }
    }

    // the house settling, and things that aren't there
    if (mood != Mood::Hush && !frozen) {
        if (night >= 1 && (whisperT_ -= dt) <= 0) {
            whisperT_ = rnd(14, 32) - night * 1.5f;
            out.push_back({Ev::Whisper});
        }
        if (night >= 2 && (knockT_ -= dt) <= 0) {
            knockT_ = rnd(28, 55);
            out.push_back({Ev::Knock, 0, roll(0.5f) ? -1.0f : 1.0f});
        }
    }
}

// The director: paces the night like a film.
void Brain::director(float dt, std::vector<Event>& out) {
    intensity += (danger() - intensity) * (1 - std::exp(-dt * 0.8f));
    moodT += dt;
    if (frozen) return;
    switch (mood) {
        case Mood::Calm:
            if (moodT > moodLen_) mood = Mood::Build, moodT = 0;
            break;
        case Mood::Build:
            peakT_ = intensity > 0.6f ? peakT_ + dt : std::max(0.0f, peakT_ - dt);
            if (peakT_ > 3) {
                mood = Mood::Peak;
                moodT = peakT_ = 0;
                moodLen_ = rnd(9, 13) + night * 1.2f;
            }
            break;
        case Mood::Peak:
            if (moodT > moodLen_) {
                mood = Mood::Relax;
                moodT = 0;
                moodLen_ = std::max(4.0f, rnd(11, 15) - night * 1.6f);
            }
            break;
        case Mood::Relax:
            if (moodT > moodLen_) mood = Mood::Build, moodT = 0;
            break;
        case Mood::Hush:
            if (moodT > 7) {
                // ...and here they come, both at once
                mood = Mood::Peak;
                moodT = 0;
                moodLen_ = 14;
                for (int s = 0; s < 2; s++) {
                    Stalker& h = hall[s];
                    if (h.pos < 3) {
                        h.pos = 3;
                        h.stun = 0;
                        h.hidden = false;
                        h.seen = false;
                        h.moveAnim = 0;
                        h.think = rnd(2.5f, 4.0f);
                    }
                }
                noise = 1;
                out.push_back({Ev::Rush});
            }
            return;
    }
    // set pieces: from night 4 the house goes silent before they rush you
    static const float kHushAt[kNights][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {3.5f, -1}, {2.5f, 4.3f}, {1.8f, 4.0f}};
    for (int i = 0; i < 2; i++) {
        float at = kHushAt[night][i];
        if (at < 0 || hushDone_[i] || t < at * kHourLen) continue;
        if (critical() > 0 || hall[0].pos >= 3 || hall[1].pos >= 3) continue;  // wait for a quiet moment
        hushDone_[i] = true;
        mood = Mood::Hush;
        moodT = 0;
        out.push_back({Ev::Hush});
        return;
    }
}

void Brain::stalker(int s, float dt, const Input& in, bool lightOn, bool hold, std::vector<Event>& out) {
    Stalker& h = hall[s];
    const NightCfg& cfg = kNight[night];
    bool here = in.view == (s == 0 ? View::Left : View::Right) && !in.moving;
    bool lit = here && lightOn && !hold;  // a held door blocks the light
    h.neglect = here ? 0 : h.neglect + dt;
    bool arrived = here && !h.wasHere;
    h.wasHere = here;
    h.watchT = here ? h.watchT + dt : 0;
    h.moveAnim += dt;
    if (h.fleeAnim >= 0 && (h.fleeAnim += dt) > 1.2f) h.fleeAnim = -1;
    if (h.lureAnim >= 0 && (h.lureAnim += dt) > 1.2f) h.lureAnim = -1;
    if (frozen) return;
    float spd = speed();

    // ---- at the door
    if (h.pos == 4) {
        if (arrived && !h.feint) h.cueT = std::min(h.cueT, 0.15f);  // walk up to the door: you hear them straight away
        if (lit && (h.lightT += dt) > 0.12f) return scare(s == 0 ? Who::Davi : Who::Kira, out, "lit at door");
        if (!lit) h.lightT = 0;
        if (h.feint) {
            // footsteps walking off... but nobody left. Breathing comes back.
            if ((h.feintT -= dt) <= 0) {
                h.feint = false;
                h.cueT = 0;
            }
            return;
        }
        if (here && hold) {
            h.pushT += dt;
            if ((h.pushCue -= dt) <= 0) {
                h.pushCue = rnd(0.6f, 1.1f);
                out.push_back({Ev::Push, s});
            }
            if (h.pushT >= h.needPush) {
                if (!h.feinted && roll(cfg.feint)) {
                    h.feint = h.feinted = true;
                    h.feintT = rnd(1.4f, 2.2f);
                    h.pushT = 0;
                    h.needPush = rnd(1.2f, 2.0f);
                    h.doorT = std::max(h.doorT, 4.0f);
                    out.push_back({Ev::Feint, s});
                } else {
                    h.pos = 0;
                    h.stun = rnd(3, 5);
                    h.think = rnd(3, 5);
                    out.push_back({Ev::Leave, s});
                }
                return;
            }
        } else {
            h.pushT = std::max(0.0f, h.pushT - dt * 0.5f);
            // fair play: while you're busy holding something else shut, they lose patience slower
            float busy = hold ? 0.55f : 1.0f;
            if ((h.doorT -= dt * busy) <= 0) return scare(s == 0 ? Who::Davi : Who::Kira, out, here ? "door not held" : "door unattended");
        }
        // breathing / giggling: loud when you're at the door, faint from elsewhere
        if ((h.cueT -= dt) <= 0) {
            h.cueT = here ? 2.3f : 4.0f;
            out.push_back({Ev::Cue, s, here ? 1.0f : 0.0f});
        }
        return;
    }

    // ---- in the hall: the flashlight
    if (h.lureAnim >= 0) return;
    if (h.hidden) {
        if ((h.hideT -= dt) <= 0) {
            h.hidden = false;
            h.seen = false;
            // sometimes she comes back out a step closer
            if (h.pos < 3 && roll(0.5f)) {
                h.pos++;
                h.moveAnim = 0;
            }
            out.push_back({Ev::Unhide, s});
        }
    } else if (lit && h.pos >= 1) {
        if (!h.seen) {
            h.seen = true;
            out.push_back({Ev::Seen, s, (float)h.pos});
            // Kira learns: the more you light her, the more she hides instead of running
            if (s == 1 && night >= 1 && h.pos >= 2 && roll(std::min(0.6f, 0.12f + h.flashed * 0.1f))) {
                h.hidden = true;
                h.hideT = rnd(3, 5);
                h.flashed++;
                out.push_back({Ev::Hide, s});
                return;
            }
        }
        h.lightT += dt;
        float need = h.pos == 1 ? 0.7f : h.pos == 2 ? 0.5f : 0.3f;
        if (h.lightT >= need) {
            h.fleeFrom = h.pos;
            h.fleeAnim = 0;
            h.pos = 0;
            h.lightT = 0;
            h.stun = rnd(3.5f, 5.5f);
            h.repelled++;
            if (s == 0) h.rage = std::min(5.0f, h.rage + 1.2f);
            else h.flashed++;
            noise = std::min(1.0f, noise + 0.04f);
            out.push_back({Ev::Flee, s});
            return;
        }
    } else {
        h.lightT = std::max(0.0f, h.lightT - dt * 2);
    }

    // ---- lure: a stroopwafel / boba down the hall keeps them busy
    if (here && in.lure) {
        if (h.lureUsed || !in.hasLure[s]) {
            out.push_back({Ev::LureFail, s});
        } else {
            h.lureUsed = true;
            h.lureAnim = 0;
            h.fleeFrom = h.pos;
            h.pos = 0;
            h.hidden = false;
            h.stun = 15;
            noise = std::max(0.0f, noise - 0.3f);
            out.push_back({Ev::Lure, s});
        }
        return;
    }

    // ---- deciding to move
    if (h.stun > 0) {
        h.stun -= dt * std::max(spd, 0.5f);
        return;
    }
    bool watched = lit;  // they never move while you're shining at them
    h.think -= dt * spd * (1 + noise * 0.8f);
    if (h.think > 0 || h.hidden || watched) return;
    float base = s == 0 ? 5.0f - h.rage * 0.35f : 3.7f;
    h.think = base * rnd(0.75f, 1.25f);
    int lvl = level(s == 0 ? cfg.davi : cfg.kira);
    float p = (lvl + h.rage + std::min(4.0f, h.neglect / 12) + noise * 4) / 20 * moodScale();
    if (h.pos == 3 && !mayGoCritical()) p = 0;
    if (h.pos == 3 && here && h.watchT < 1.2f) p = 0;  // never step up to the door the moment you get there
    if (roll(p)) {
        h.pos++;
        h.seen = false;
        h.moveAnim = 0;
        if (h.pos == 4) {
            h.doorT = std::max(4.5f, kDoorPatience - night * 0.7f);
            h.pushT = 0;
            h.needPush = rnd(2.0f, 3.0f) + h.rage * 0.15f;
            h.cueT = here ? 0.2f : 0.7f;
            h.feinted = false;
            out.push_back({Ev::AtDoor, s});
        } else {
            out.push_back({Ev::Advance, s, (float)h.pos});
        }
    } else if (h.pos > 0 && mood == Mood::Relax && roll(0.35f)) {
        h.pos--;
        h.moveAnim = 0;
        out.push_back({Ev::Back, s});
    } else if (h.pos == 0 && roll(0.35f)) {
        out.push_back({Ev::Roam, s});
    }
}

void Brain::closet(float dt, const Input& in, bool hold, std::vector<Event>& out) {
    bool at = in.view == View::Closet && !in.moving;
    closetNeglect_ = at ? 0 : closetNeglect_ + dt;
    horseAnim += dt;
    if (frozen) return;
    if (horse >= 2 && (snortT_ -= dt) <= 0) {
        snortT_ = rnd(7, 14) - horse * 1.5f;
        out.push_back({Ev::Snort});
    }
    auto strain = [&] {
        if ((strainT_ -= dt) <= 0) {
            strainT_ = 0.5f;
            out.push_back({Ev::HorseStrain});
        }
    };
    if (horse == 4) {
        // the doors are open. Get there and hold them shut, now.
        horseOpenT += dt;
        if (at && hold) {
            horseLook = 0;
            horseHold += dt;
            strain();
            if (horseHold >= 2.2f) {
                horse = 1;
                horseHold = 0;
                horseAnim = 0;
                horseThink_ = 7;
                out.push_back({Ev::HorseBack});
            }
            return;
        }
        horseHold = 0;
        if (at && (horseLook += dt) > 1.2f) return scare(Who::Horse, out, "stared");
        if (!at && horseOpenT > 7.5f - night * 0.4f) return scare(Who::Horse, out, "too late");
        return;
    }
    if (at && hold && horse > 0) {
        horseHold += dt;
        strain();
        if (horseHold >= 1.2f + horse * 0.35f) {
            horse = 0;
            horseHold = 0;
            horseAnim = 0;
            horseThink_ = 7;
            out.push_back({Ev::HorseBack});
        }
        return;
    }
    if (!hold) horseHold = 0;
    if (at) return;  // he doesn't grow while you watch
    horseThink_ -= dt * speed() * (1 + noise * 0.5f);
    if (horseThink_ > 0) return;
    horseThink_ = 6 * rnd(0.8f, 1.2f);
    float p = (level(kNight[night].horse) + (plush >= 2 ? 3 : 0) + std::min(4.0f, closetNeglect_ / 15) + noise * 3) / 20 * moodScale();
    if (kNight[night].horse == 0) p = 0;
    if (horse == 3 && !mayGoCritical()) p = 0;
    if (!roll(p)) return;
    horse++;
    horseAnim = 0;
    if (horse == 4) {
        horseOpenT = horseLook = 0;
        out.push_back({Ev::HorseOpen});
    } else {
        out.push_back({Ev::HorseGrow, 0, (float)horse});
    }
}

void Brain::bed(float dt, const Input& in, bool lightOn, std::vector<Event>& out) {
    bool at = in.view == View::Bed && !in.moving;
    bedNeglect_ = at ? 0 : bedNeglect_ + dt;
    for (auto& a : plushAge) a += dt;
    if (frozen) return;
    if (at && lightOn && plush > 0) {
        if ((plushLit += dt) >= 0.45f) {
            plushLit = 0;
            plush--;
            out.push_back({Ev::PlushOut, 0, (float)plush});
        }
    } else {
        plushLit = 0;
    }
    if (plush >= 3) {
        if (plushCall == 0) out.push_back({Ev::PlushCall});
        if ((plushCall += dt) > 6) return scare(Who::Paardjes, out);
        return;
    }
    plushCall = 0;
    if (at) return;
    plushThink_ -= dt * speed();
    if (plushThink_ > 0) return;
    plushThink_ = 7 * rnd(0.8f, 1.2f);
    float p = (level(kNight[night].plush) + std::min(3.0f, bedNeglect_ / 15)) / 20 * moodScale();
    if (plush == 2 && !mayGoCritical()) p = 0;
    if (!roll(p)) return;
    plushAge[plush] = 0;
    plush++;
    out.push_back({Ev::PlushIn, 0, (float)plush});
}

// The Monkey Man at the window: harmless, unless you keep looking at him.
void Brain::window(float dt, const Input& in, std::vector<Event>& out) {
    monkeyBangT += dt;
    if (!kNight[night].monkey || frozen) return;
    bool watching = in.view == View::Center && !in.moving;
    if (!monkey) {
        if (mood == Mood::Hush) return;
        if ((monkeyNext_ -= dt) <= 0) {
            monkey = true;
            monkeyT = monkeyStare = 0;
            monkeyRiser = false;
            out.push_back({Ev::MonkeyTap});
        }
        return;
    }
    monkeyT += dt;
    if (std::fmod(monkeyT, 1.3f) < dt) out.push_back({Ev::Cymbal});
    if (watching) {
        monkeyStare += dt;
        if (monkeyStare > 1.6f && !monkeyRiser) {
            monkeyRiser = true;
            out.push_back({Ev::MonkeyStare});
        }
    } else if (monkeyRiser) {
        monkeyRiser = false;
        monkeyStare = std::min(monkeyStare, 1.0f);
        out.push_back({Ev::MonkeyCalm});
    }
    if (monkeyStare >= 4.6f) {
        // BANG BANG BANG: the whole house is awake now
        monkey = monkeyRiser = false;
        monkeyBangT = 0;
        monkeyNext_ = rnd(40, 70) - night * 4;
        noise = 1;
        for (auto& h : hall)
            if (h.pos < 2 && h.stun <= 0 && h.lureAnim < 0) {
                h.pos++;
                h.seen = false;
                h.moveAnim = 0;
            }
        out.push_back({Ev::MonkeyBang});
    } else if (monkeyT > 11) {
        monkey = false;
        monkeyNext_ = rnd(40, 70) - night * 4;
        if (monkeyRiser) out.push_back({Ev::MonkeyCalm});
        monkeyRiser = false;
    }
}

// ================================================================== balance bot
//
// Plays like a careful human who knows the rules: it only acts on what a player
// could hear or see (no peeking at hidden state), with a reaction delay.

namespace {

struct Bot {
    View view = View::Center, target = View::Center;
    float moveT = 0, dwell = 0, react = 0, listenT = 0;
    int listenSide = -1;
    bool doorKnown[2]{}, seenClose[2]{}, horseOpen = false, pcOn = false;
    int plushHeard = 0, horseHeard = 0;
    float sinceChecked[5]{};
    std::mt19937 rng{7};

    float r(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

    void hear(const Event& e) {
        switch (e.ev) {
            case Ev::Cue: doorKnown[e.a] = true; break;
            case Ev::AtDoor: break;  // silent: you only know once the breathing starts
            case Ev::Leave: doorKnown[e.a] = false; listenSide = e.a; listenT = 2.5f; break;
            case Ev::Feint: doorKnown[e.a] = false; listenSide = e.a; listenT = 2.5f; break;
            case Ev::HorseOpen: horseOpen = true; break;
            case Ev::HorseGrow: horseHeard = (int)e.x; break;
            case Ev::Snort: horseHeard = std::max(horseHeard, 2); break;
            case Ev::HorseBack: horseOpen = false; horseHeard = 0; break;
            case Ev::PlushIn: plushHeard = (int)e.x; break;
            case Ev::PlushOut: plushHeard = (int)e.x; break;
            case Ev::PcHack: pcOn = true; break;
            case Ev::PcOff: pcOn = false; break;
            default: break;
        }
    }

    Input act(float dt, const Brain& b) {
        Input in;
        for (auto& s : sinceChecked) s += dt;
        if (moveT > 0) {
            moveT -= dt;
            in.view = view;
            in.moving = true;
            if (moveT <= 0) view = target, dwell = 0, react = r(0.25f, 0.45f);
            return in;
        }
        in.view = view;
        dwell += dt;
        sinceChecked[(int)view] = 0;
        auto go = [&](View v) {
            if (v == view) return;
            target = v;
            moveT = 0.3f;
            in.moving = true;
        };
        int door = view == View::Left ? 0 : view == View::Right ? 1 : -1;
        // 1. someone breathing at this door: hold until they leave, then keep listening
        if (door >= 0 && dwell > 0.5f && b.hall[door].pos == 4 && seenClose[door]) doorKnown[door] = true;  // she was right there, and now she's gone
        if (door >= 0) seenClose[door] = b.hall[door].pos == 3 || (seenClose[door] && b.hall[door].pos == 4);
        if (door >= 0 && doorKnown[door]) {
            in.hold = true;
            return in;
        }
        if (listenT > 0) {
            listenT -= dt;
            bool otherDoor = doorKnown[1 - std::max(0, listenSide)];
            if (door == listenSide && !otherDoor && !horseOpen) return in;  // dark and quiet: wait, don't shine
        }
        if (react > 0) {
            react -= dt;
            return in;
        }
        // 2. the closet is open: run
        if (horseOpen) {
            if (view == View::Closet) in.hold = true;
            else go(View::Closet);
            return in;
        }
        // the plushies are calling the horse: everything else can wait a second
        if (view == View::Bed && b.plush > 0 && b.battery > 0) {
            in.light = true;
            return in;
        }
        if (plushHeard >= 2 && view != View::Bed) {
            go(View::Bed);
            return in;
        }
        // 3. someone at another door
        for (int s = 0; s < 2; s++)
            if (doorKnown[s]) {
                go(s == 0 ? View::Left : View::Right);
                return in;
            }
        // 4. the PC
        if (pcOn) {
            if (view == View::Center) in.pcOff = true;
            else go(View::Center);
            return in;
        }
        // 5. work the current spot
        if (view == View::Closet && horseHeard > 0 && b.horse > 0) {  // the doors bulge: you can see it
            in.hold = true;
            if (dwell < 4) return in;
            horseHeard = 0;
        }
        if (view == View::Closet && b.horse > 0 && dwell < 3.5f) {
            in.hold = true;
            return in;
        }
        if (view == View::Bed && b.plush > 0) {
            in.light = true;
            return in;
        }
        if (door >= 0 && dwell < 1.6f) {
            Stalker h = b.hall[door];
            // light only if something might be in the hall; looking is free, the battery isn't
            in.light = dwell > 0.3f && b.battery > 0.15f;
            (void)h;
            return in;
        }
        if (view == View::Center && b.battery < 0.6f && dwell < 5 && !b.monkey) return in;  // charging
        if (view == View::Center && b.monkey) {
            // don't stare at the window
            go(View::Bed);
            return in;
        }
        // 6. patrol: whatever has gone unchecked longest
        View best = View::Left;
        float score = -1;
        const float w[5] = {0.3f, 1.0f, 1.0f, 0.55f + plushHeard * 0.4f + horseHeard * 0.3f, 0.45f + plushHeard * 0.5f};
        for (int v = 0; v < 5; v++) {
            if ((View)v == view) continue;
            float sc = sinceChecked[v] * w[v];
            if ((View)v == View::Center && b.battery < 0.45f) sc += 20;
            if (sc > score) score = sc, best = (View)v;
        }
        // you have to pass through the room to get anywhere
        if (view != View::Center && best != View::Center) best = View::Center;
        go(best);
        return in;
    }
};

}  // namespace

int simulate(int runs) {
    const char* names[] = {"Davi", "Kira", "Horse", "Plush"};
    for (int n = 0; n < kNights; n++) {
        int wins = 0, deaths[4]{};
        float hourSum = 0;
        int trace = -1;
        if (const char* tr = std::getenv("PG_FNAP_SIM_TRACE")) {
            int tn = 0, ti = 0;
            std::sscanf(tr, "%d,%d", &tn, &ti);
            if (tn == n + 1) trace = ti;
        }
        for (int i = 0; i < runs; i++) {
            Brain b(1000 + i * 31 + n);
            b.start(n);
            Bot bot;
            bot.rng.seed(i * 7 + 3);
            std::vector<Event> ev;
            const float dt = 1 / 60.0f;
            while (!b.over && !b.won) {
                Input in = bot.act(dt, b);
                ev.clear();
                b.update(dt, in, ev);
                for (auto& e : ev) {
                    bot.hear(e);
                    if (trace == i) std::printf("    %6.2f  ev %2d a %d x %.0f   bot view %d moving %d hold %d light %d  pos %d/%d\n", b.t, (int)e.ev, e.a, e.x, (int)in.view, in.moving,
                                                in.hold, in.light, b.hall[0].pos, b.hall[1].pos);
                }
            }
            if (b.won) wins++;
            else {
                deaths[(int)b.killer]++, hourSum += b.t / kHourLen;
                if (std::getenv("PG_FNAP_SIM_WHY"))
                    std::printf("  run %d n%d %s: %s at %.0fs  bot at %d known %d%d horse %d plush %d pc %d mood %d\n", i, n + 1, names[(int)b.killer], b.cause, b.t, (int)bot.view,
                                bot.doorKnown[0], bot.doorKnown[1], b.horse, b.plush, b.pc, (int)b.mood);
            }
        }
        int lost = runs - wins;
        (void)0;
        std::printf("night %d  %-18s  win %5.1f%%   deaths: Davi %3d  Kira %3d  Horse %3d  Plush %3d   avg death at %.1f AM\n", n + 1, kNight[n].tag,
                    100.0 * wins / runs, deaths[0], deaths[1], deaths[2], deaths[3], lost ? hourSum / lost : 0.0);
        (void)names;
    }
    return 0;
}

}  // namespace pg::fnap
