// SHOP: Kira sells powerups, food for Pieter, and cosmetics.
#include "engine/Particles.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"
#include "game/Scenes.hpp"

namespace pg {
namespace {

class ShopScene : public Scene {
public:
    Presence presence() const override { return {"Shopping at Kira's", "", "kira", "Kira's Shop", "logo"}; }

    MusicMode music() const override { return MusicMode::Title; }

    void enter() override { line_ = "Welcome to Kira's shop! No refunds. Ever."; }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        if (E().backPressed()) E().go(makeMenu);
    }

    bool consumable(const ItemDef& it) const { return it.kind == ItemKind::Powerup || it.kind == ItemKind::Food; }

    void buy(const ItemDef& it, int n, Vector2 at) {
        long long cost = it.price * n;
        if (!consumable(it) && P().owns(it.id)) return;
        if (!P().spend(cost)) {
            E().sfx.play(Sfx::Error);
            line_ = "You're broke lol. Go win some coins.";
            return;
        }
        P().give(it.id, n);
        P().save();
        E().sfx.play(Sfx::Coin);
        fx_.burst(at, 20, col::gold, 280, 6, 0.6f, PShape::Star);
        fx_.floatText("-" + fmtNum(cost), at, col::gold, 26);
        static const char* lines[] = {"Pleasure doing business!", "Pieter will love that.", "Good choice. Probably.",
                                      "Davi bought that once. He's still bad.", "Ka-ching!"};
        line_ = lines[rndi(0, 4)];
        if (!consumable(it)) {
            if (it.kind == ItemKind::Hat) P().hat = it.id;
            else P().trail = it.id;
            line_ = "Equipped! Looking fresh.";
            P().save();
        }
    }

    void draw() override {
        art::purpleBackdrop(t_);
        ui::titleBar("KIRA'S SHOP", col::pink);
        if (ui::backButton()) E().go(makeMenu);
        ui::coinCounter(VW - 20, 38, P().coins);

        // tabs
        const char* tabs[] = {"POWERUPS", "FOOD", "COSMETICS"};
        for (int i = 0; i < 3; i++)
            if (ui::button({330 + i * 210.0f, 92, 196, 50}, tabs[i], tab_ == i ? col::pink : col::panelHi, 22)) tab_ = i;

        // Kira the shopkeeper
        art::portrait("kira", {150, 250}, 95, col::pink, std::sin(t_ * 1.7f) * 5);
        Rectangle b{30, 365, 245, 0};
        b.height = ui::paragraph(line_, {0, 0, 215, 0}, 19, WHITE, false) + 26;
        DrawRectangleRounded(b, 0.3f, 10, WHITE);
        DrawTriangle({140, b.y}, {150, b.y - 16}, {162, b.y}, WHITE);
        ui::paragraph(line_, {b.x + 15, b.y + 12, 215, 0}, 19, col::outline);

        std::vector<const ItemDef*> items;
        for (auto& it : itemDb()) {
            bool show = (tab_ == 0 && it.kind == ItemKind::Powerup) || (tab_ == 1 && it.kind == ItemKind::Food) ||
                        (tab_ == 2 && (it.kind == ItemKind::Hat || it.kind == ItemKind::Trail));
            if (show) items.push_back(&it);
        }
        float cw = 230, ch = tab_ == 2 ? 178 : 248, gap = 14, x0 = 310, y0 = 158;
        int cols = 4;
        for (size_t i = 0; i < items.size(); i++) {
            const ItemDef& it = *items[i];
            Rectangle c{x0 + (i % cols) * (cw + gap), y0 + (i / cols) * (ch + gap), cw, ch};
            bool owned = !consumable(it) && P().owns(it.id);
            bool hot = ui::hovered(c);
            ui::panel(c, hot ? lighten(col::panel, 0.05f) : col::panel, 0.1f, hot ? it.color : col::outline);
            float iconY = c.y + (tab_ == 2 ? 50 : 60);
            DrawCircleGradient({c.x + c.width / 2, iconY}, 60, alpha(it.color, 0.3f), alpha(it.color, 0));
            art::itemIcon(it.id, {c.x + c.width / 2, iconY}, tab_ == 2 ? 64 : 84);
            float ty = c.y + (tab_ == 2 ? 104 : 122);
            ui::text(it.name, c.x + c.width / 2, ty, 21, col::text, ui::Center, true);
            if (tab_ != 2) ui::paragraph(it.desc, {c.x + 14, ty + 18, c.width - 28, 60}, 15, col::muted);
            int cnt = P().count(it.id);
            if (consumable(it) && cnt > 0) ui::text("owned: " + std::to_string(cnt), c.x + 12, c.y + 16, 15, col::green);
            Rectangle buyR{c.x + 12, c.y + c.height - 50, consumable(it) ? c.width - 84 : c.width - 24, 40};
            if (owned) {
                bool equipped = P().hat == it.id || P().trail == it.id;
                if (ui::button(buyR, equipped ? "EQUIPPED" : "EQUIP", equipped ? col::green : col::panelHi, 18)) {
                    std::string& slot = it.kind == ItemKind::Hat ? P().hat : P().trail;
                    slot = equipped ? "" : it.id;
                    P().save();
                }
            } else {
                if (ui::button(buyR, fmtNum(it.price), col::purple, 20, P().coins >= it.price)) buy(it, 1, center(c));
                ui::coin({buyR.x + 22, buyR.y + 20}, 11);
                if (consumable(it) && ui::button({c.x + c.width - 66, buyR.y, 54, 40}, "x5", col::panelHi, 18, P().coins >= it.price * 5))
                    buy(it, 5, center(c));
            }
        }
        if (tab_ == 0) ui::text("Equip powerups in your INVENTORY, then press 1/2/3 in Classic.", 310, VH - 22, 17, col::muted);
        if (tab_ == 1) ui::text("Feed food to Pieter in the PIETER ROOM.", 310, VH - 22, 17, col::muted);
        if (tab_ == 2) ui::text("Hats show up on Pieter everywhere. Trails follow you in Classic.", 310, VH - 22, 17, col::muted);
        fx_.draw();
    }

private:
    int tab_ = 0;
    float t_ = 0;
    std::string line_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeShop() { return std::make_unique<ShopScene>(); }

}  // namespace pg
