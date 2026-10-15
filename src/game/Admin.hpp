// Secret admin / practice settings. Unlocked by clicking the title screen
// characters in the order Pieter, Pieter, Kira, Pieter, Davi. Session only:
// nothing here is saved.
#pragma once

namespace pg {

struct AdminSettings {
    bool god = false;          // Classic: no damage
    bool hitboxes = false;     // Classic: draw collision circles
    float speed = 1.0f;        // Classic: game speed for practice (0.5 = half speed)
    // Practice run, consumed by the next Classic start from the admin panel
    bool practicePending = false;
    int startWave = 1;
    int hearts = 3;
    bool enragedBoss = false;  // start the boss already in its second phase
};

inline AdminSettings& Admin() {
    static AdminSettings a;
    return a;
}

}  // namespace pg
