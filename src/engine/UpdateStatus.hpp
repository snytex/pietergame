#pragma once

namespace pg {
// The launcher does all network/disk installation work outside the game process.
void pollUpdateStatus(float dt);
void drawUpdateStatus();
}
