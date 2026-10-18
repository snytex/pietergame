#include "engine/UpdateStatus.hpp"
#include "engine/Ui.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

namespace pg {
namespace {
std::string status;
float poll = 0;
float age = 0;
}

void pollUpdateStatus(float dt) {
    age += dt;
    poll -= dt;
    if (poll > 0) return;
    poll = 0.5f;
    const char* path = std::getenv("PG_UPDATE_STATUS");
    if (!path) return;
    std::ifstream input(path);
    std::string next;
    std::getline(input, next);
    if (!next.empty() && next.size() <= 160 && next != status) {
        status = next;
        age = 0;
    }
}

void drawUpdateStatus() {
    if (status.empty()) return;
    const bool installed = status.find("installed!") != std::string::npos;
    const bool downloading = status.find("downloading") != std::string::npos;
    if (age > 10 && !installed && !downloading) return;
    const float width = std::min(1200.0f, ui::measure(status, 18) + 36);
    ui::panel({(VW - width) / 2, VH - 42, width, 32}, col::bgDeep);
    ui::text(status, VW / 2, VH - 26, 18, installed ? col::green : col::cyan, ui::Center);
}
}
