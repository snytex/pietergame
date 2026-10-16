// Dev-only sprite sheet for checking art (PG_SCENE=artlab, PG_ARTLAB=horse|items).
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

class ArtLabScene : public Scene {
public:
    void update(float dt) override { t_ += dt; }
    void draw() override {
        DrawRectangleGradientV(0, 0, VW, VH, rgba(0x6a8a5aff), rgba(0x3a5a3aff));
        std::string page = std::getenv("PG_ARTLAB") ? std::getenv("PG_ARTLAB") : "horse";
        if (page == "horse") {
            for (int i = 0; i < 4; i++) {
                float ph = i * PI / 2;
                art::horse({160.0f + i * 300, 330}, 1.15f, ph, rgba(0x9a5a2aff), rgba(0x2a1a10ff), col::violet, i == 0 ? "pieter" : i == 1 ? "" : "none");
            }
            art::horse({330, 690}, 1.9f, 0.8f, rgba(0xe8e0d8ff), rgba(0x8a7a6aff), col::red, "davi");
            art::horse({900, 690}, 1.9f, 2.4f, rgba(0x3a2a24ff), rgba(0x101010ff), col::gold, "", -1);
        } else {
            int i = 0;
            for (auto& it : itemDb()) {
                Vector2 c{90.0f + (i % 9) * 135, 110.0f + (i / 9) * 150};
                DrawRectangleRounded({c.x - 60, c.y - 60, 120, 120}, 0.2f, 8, rgba(0x24163fff));
                art::itemIcon(it.id, c, 96);
                ui::text(it.id, c.x, c.y + 72, 13, WHITE, ui::Center);
                i++;
            }
        }
    }

private:
    float t_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeArtLab() { return std::make_unique<ArtLabScene>(); }

}  // namespace pg
