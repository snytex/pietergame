// BLACKJACK vs Davi the dealer (pro dealer since 2010, he claims).
// 6-deck shoe, dealer stands on 17, blackjack pays 3:2, double on first two cards.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <deque>

namespace pg {
namespace {

constexpr Vector2 kShoe{1130, 150};
constexpr float kCardW = 110, kCardH = 154;

struct Card {
    int rank, suit;
    Vector2 pos{kShoe};
    float flip = 0;  // 0 = back, 1 = face
    bool faceUp = true;
};

struct Deal {
    bool toPlayer;
    bool faceUp;
};

enum class State { Betting, Dealing, Player, Dealer, Done };

int handValue(const std::vector<Card>& h, bool countHidden = true, bool* soft = nullptr) {
    int v = 0, aces = 0;
    for (auto& c : h) {
        if (!countHidden && !c.faceUp) continue;
        int r = std::min(c.rank, 10);
        if (c.rank == 1) { aces++; r = 11; }
        v += r;
    }
    while (v > 21 && aces > 0) { v -= 10; aces--; }
    if (soft) *soft = aces > 0;
    return v;
}

class BlackjackScene : public Scene {
public:
    Presence presence() const override { return {"Casino - Blackjack vs Davi", "", "casino", "Pieter Casino", "logo"}; }

    void enter() override {
        newShoe();
        bet_ = std::clamp<long long>(25, 1, std::max<long long>(1, P().coins));
        say("Place your bet. I'm a pro dealer btw.");
    }

    void newShoe() {
        shoe_.clear();
        for (int d = 0; d < 6; d++)
            for (int s = 0; s < 4; s++)
                for (int r = 1; r <= 13; r++) shoe_.push_back({r, s});
        std::shuffle(shoe_.begin(), shoe_.end(), rng());
    }

    void say(const std::string& s) {
        line_ = s;
        lineT_ = 3.0f;
    }

    void queue(bool toPlayer, bool faceUp = true) { deals_.push_back({toPlayer, faceUp}); }

    void deal() {
        if (!P().spend(bet_)) { E().sfx.play(Sfx::Error); return; }
        P().wager(bet_);
        stake_ = bet_;
        if (shoe_.size() < 60) { newShoe(); E().toast("Davi shuffles the shoe", col::cyan); }
        player_.clear();
        dealer_.clear();
        message_.clear();
        queue(true);
        queue(false);
        queue(true);
        queue(false, false);
        state_ = State::Dealing;
        dealT_ = 0.1f;
    }

    void layout(float dt) {
        auto place = [&](std::vector<Card>& h, float y) {
            float total = (h.size() - 1) * 72.0f + kCardW;
            for (size_t i = 0; i < h.size(); i++) {
                Vector2 target{VW / 2.0f - total / 2 + i * 72.0f, y};
                h[i].pos = damp(h[i].pos, target, 12, dt);
                h[i].flip = approach(h[i].flip, h[i].faceUp ? 1.0f : 0.0f, dt * 5);
            }
        };
        place(dealer_, 150);
        place(player_, 420);
    }

    bool settled() const {
        for (auto* h : {&player_, &dealer_})
            for (auto& c : *h)
                if (std::abs(c.flip - (c.faceUp ? 1 : 0)) > 0.01f) return false;
        return deals_.empty();
    }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        lineT_ -= dt;
        layout(dt);

        // deal queued cards one by one
        if (!deals_.empty()) {
            dealT_ -= dt;
            if (dealT_ <= 0) {
                Deal d = deals_.front();
                deals_.pop_front();
                Card c = shoe_.back();
                shoe_.pop_back();
                c.faceUp = d.faceUp;
                c.flip = 0;
                (d.toPlayer ? player_ : dealer_).push_back(c);
                E().sfx.play(Sfx::Card);
                dealT_ = 0.32f;
            }
            return;
        }
        waitT_ -= dt;
        if (waitT_ > 0) return;

        switch (state_) {
            case State::Dealing:
                if (!settled()) break;
                if (handValue(player_) == 21 || handValue(dealer_) == 21) {
                    revealHole();
                    finish();
                } else {
                    state_ = State::Player;
                }
                break;
            case State::Player:
                if (!settled()) break;
                if (handValue(player_) > 21) { revealHole(); finish(); }
                else if (handValue(player_) == 21 || doubled_) stand();
                break;
            case State::Dealer:
                if (!settled()) break;
                if (handValue(dealer_) < 17) { queue(false); waitT_ = 0.35f; }
                else finish();
                break;
            default: break;
        }
        if (E().backPressed() && (state_ == State::Betting || state_ == State::Done)) E().go(makeCasino);
    }

    void revealHole() {
        for (auto& c : dealer_) c.faceUp = true;
    }

    void stand() {
        state_ = State::Dealer;
        revealHole();
        waitT_ = 0.5f;
        E().sfx.play(Sfx::Card, 0.8f, 0.8f);
    }

    void doubleDown() {
        if (!P().spend(stake_)) { E().sfx.play(Sfx::Error); return; }
        P().wager(stake_);
        stake_ *= 2;
        doubled_ = true;
        queue(true);
        say("Doubling? Bold. Wrong, but bold.");
    }

    void finish() {
        state_ = State::Done;
        doubled_ = false;
        int p = handValue(player_), d = handValue(dealer_);
        bool pBJ = p == 21 && player_.size() == 2, dBJ = d == 21 && dealer_.size() == 2;
        long long back = 0;
        if (p > 21) { message_ = "BUST"; say("EZ. Pro since 2010."); }
        else if (pBJ && !dBJ) { back = stake_ + stake_ * 3 / 2; message_ = "BLACKJACK!"; say("HOW?! that's lag"); }
        else if (dBJ && !pBJ) { message_ = "DEALER BLACKJACK"; say("Told you. PRO."); }
        else if (d > 21) { back = stake_ * 2; message_ = "DEALER BUSTS - YOU WIN"; say("my controller is broken"); }
        else if (p > d) { back = stake_ * 2; message_ = "YOU WIN"; say("rigged. totally rigged."); }
        else if (p < d) { message_ = "DEALER WINS"; say("Get good. lol."); }
        else { back = stake_; message_ = "PUSH"; say("A tie? Boring."); }
        win_ = back > stake_;
        net_ = back - stake_;
        if (back > 0) P().earn(back, back > stake_);
        if (back > stake_) {
            E().sfx.play(Sfx::Win);
            fx_.confetti({VW / 2.0f, 400}, pBJ ? 120 : 60);
        } else if (back < stake_) {
            E().sfx.play(Sfx::Lose, 0.6f);
        } else {
            E().sfx.play(Sfx::Bell, 0.5f);
        }
        P().save();
    }

    void draw() override {
        casino::felt(t_);
        // table arc
        DrawRing({VW / 2.0f, -260}, 820, 830, 20, 160, 80, alpha(col::gold, 0.4f));
        ui::text("BLACKJACK PAYS 3 TO 2  -  DEALER STANDS ON 17", VW / 2.0f, 370, 18, alpha(col::gold, 0.6f), ui::Center, true);

        // dealer Davi
        art::portrait("davi", {150, 200}, 80, col::cyan, std::sin(t_ * 1.5f) * 4);
        ui::text("DAVI", 150, 300, 24, col::cyan, ui::Center, true);
        ui::text("the dealer", 150, 324, 16, col::muted, ui::Center);
        if (lineT_ > 0) {
            float w = std::min(300.0f, ui::measure(line_, 18) + 30);
            Rectangle b{240, 100, w, 0};
            b.height = ui::paragraph(line_, {0, 0, w - 30, 0}, 18, WHITE, false) + 20;
            DrawRectangleRounded(b, 0.4f, 10, WHITE);
            DrawTriangle({b.x, b.y + b.height - 14}, {b.x - 14, b.y + b.height + 2}, {b.x + 14, b.y + b.height - 2}, WHITE);
            ui::paragraph(line_, {b.x + 15, b.y + 9, w - 30, 0}, 18, col::outline);
        }

        // shoe
        DrawRectangleRounded({kShoe.x - 10, kShoe.y - 10, kCardW + 40, kCardH + 20}, 0.1f, 6, rgba(0x2a1a10ff));
        art::card({kShoe.x, kShoe.y, kCardW, kCardH}, 1, 0, false);

        auto drawHand = [&](const std::vector<Card>& h) {
            for (auto& c : h) {
                // flip animation: squash horizontally through the middle
                float k = std::abs(c.flip * 2 - 1);
                Rectangle r{c.pos.x + kCardW * (1 - k) / 2, c.pos.y, kCardW * k, kCardH};
                art::card(r, c.rank, c.suit, c.flip > 0.5f);
            }
        };
        drawHand(dealer_);
        drawHand(player_);

        auto badge = [&](const std::vector<Card>& h, float y, bool hideHole) {
            if (h.empty()) return;
            int v = handValue(h, !hideHole);
            Rectangle r{VW / 2.0f + ((h.size() - 1) * 72.0f + kCardW) / 2 + 16, y + kCardH / 2 - 22, 64, 44};
            DrawRectangleRounded(r, 0.5f, 8, v > 21 ? col::red : col::bgDeep);
            ui::text(std::to_string(v), r.x + 32, r.y + 22, 26, WHITE, ui::Center, true);
        };
        bool holeHidden = std::any_of(dealer_.begin(), dealer_.end(), [](auto& c) { return !c.faceUp; });
        badge(dealer_, 150, holeHidden);
        badge(player_, 420, false);

        // controls
        Rectangle bar{VW / 2.0f - 380, 600, 760, 100};
        if (state_ == State::Betting || state_ == State::Done) {
            ui::betSelector({bar.x, bar.y, 470, 100}, bet_, P().coins);
            if (ui::button({bar.x + 490, bar.y + 10, 270, 80}, state_ == State::Done ? "DEAL AGAIN" : "DEAL", col::green, 32, P().coins >= bet_ && P().coins > 0))
                deal();
        } else if (state_ == State::Player && settled()) {
            if (ui::button({bar.x, bar.y + 10, 240, 76}, "HIT", col::purple, 32) || IsKeyPressed(KEY_H)) {
                queue(true);
            }
            if (ui::button({bar.x + 260, bar.y + 10, 240, 76}, "STAND", col::red, 32) || IsKeyPressed(KEY_S)) stand();
            bool canDouble = player_.size() == 2 && P().coins >= stake_;
            if (ui::button({bar.x + 520, bar.y + 10, 240, 76}, "DOUBLE", col::gold, 32, canDouble) || (canDouble && IsKeyPressed(KEY_D))) doubleDown();
        }
        if (stake_ > 0 && state_ != State::Betting) {
            ui::coin({110, 470}, 16);
            ui::text("stake " + fmtNum(stake_), 135, 470, 24, col::gold);
        }

        if (state_ == State::Done && !message_.empty()) {
            Color c = win_ ? col::gold : (net_ == 0 ? col::text : col::red);
            ui::textOutline(message_, VW / 2.0f, 360, 46, c, col::outline, 4);
            if (net_ != 0)
                ui::text((net_ > 0 ? "+" : "") + fmtNum(net_), VW / 2.0f, 590 - 10, 26, c, ui::Center, true);
        }
        fx_.draw();
        if (casino::header("BLACKJACK") && (state_ == State::Betting || state_ == State::Done)) E().go(makeCasino);
        if (state_ == State::Player) ui::text("H hit   S stand   D double", 30, VH - 20, 16, alpha(WHITE, 0.5f));
    }

private:
    State state_ = State::Betting;
    std::vector<Card> shoe_, player_, dealer_;
    std::deque<Deal> deals_;
    float t_ = 0, dealT_ = 0, waitT_ = 0, lineT_ = 0;
    long long bet_ = 25, stake_ = 0, net_ = 0;
    bool doubled_ = false, win_ = false;
    std::string message_, line_;
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makeBlackjack() { return std::make_unique<BlackjackScene>(); }

}  // namespace pg
