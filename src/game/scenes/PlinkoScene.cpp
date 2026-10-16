// PLINKO: drop little Pieters through a triangle of pegs into multiplier
// buckets. Pick 8 / 12 / 16 rows and low / medium / high risk.
#include "engine/Particles.hpp"
#include "game/Art.hpp"
#include "game/scenes/CasinoCommon.hpp"

#include <algorithm>
#include <deque>

namespace pg {
namespace {

// payout tables per rows and risk (edge buckets first)
const std::vector<float>& table(int rows, int risk) {
    static const std::vector<float> t[3][3] = {
        {{5.6f, 2.1f, 1.1f, 1, 0.5f, 1, 1.1f, 2.1f, 5.6f},
         {13, 3, 1.3f, 0.7f, 0.4f, 0.7f, 1.3f, 3, 13},
         {29, 4, 1.5f, 0.3f, 0.2f, 0.3f, 1.5f, 4, 29}},
        {{10, 3, 1.6f, 1.4f, 1.1f, 1, 0.5f, 1, 1.1f, 1.4f, 1.6f, 3, 10},
         {33, 11, 4, 2, 1.1f, 0.6f, 0.3f, 0.6f, 1.1f, 2, 4, 11, 33},
         {170, 24, 8.1f, 2, 0.7f, 0.2f, 0.2f, 0.2f, 0.7f, 2, 8.1f, 24, 170}},
        {{16, 9, 2, 1.4f, 1.4f, 1.2f, 1.1f, 1, 0.5f, 1, 1.1f, 1.2f, 1.4f, 1.4f, 2, 9, 16},
         {110, 41, 10, 5, 3, 1.5f, 1, 0.5f, 0.3f, 0.5f, 1, 1.5f, 3, 5, 10, 41, 110},
         {1000, 130, 26, 9, 4, 2, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 2, 4, 9, 26, 130, 1000}}};
    int ri = rows == 8 ? 0 : rows == 12 ? 1 : 2;
    return t[ri][risk];
}

struct Ball {
    std::vector<int> dirs;  // 0 = left, 1 = right, per row
    int row = -1;           // segment being travelled (-1 = falling in)
    float u = 0;            // progress through the segment
    long long stake = 0;
    float hue = 0;
};

class PlinkoScene : public Scene {
public:
    Presence presence() const override { return {"Casino - Plinko", "", "casino", "Pieter Casino", "logo"}; }

    void enter() override {
        bet_ = std::clamp<long long>(10, 1, std::max<long long>(1, P().coins));
        autoOn_ = std::getenv("PG_PLINKO_AUTO") != nullptr;  // dev hook
    }

    // ------------------------------------------------------------ geometry
    float dx() const { return std::min(48.0f, 760.0f / (rows_ + 3)); }
    float dy() const { return std::min(42.0f, 470.0f / (rows_ + 1)); }
    float cx() const { return 450; }
    float top() const { return 150; }
    Vector2 peg(int r, int j) const { return {cx() + (j - (r + 2) / 2.0f) * dx(), top() + r * dy()}; }
    // where the ball touches row r after `rights` right-bounces
    Vector2 hit(int r, int rights) const { return {cx() + (rights - r / 2.0f) * dx(), top() + r * dy() - dx() * 0.22f}; }
    Rectangle bucket(int k) const {
        float w = dx() - 4;
        return {cx() + (k - rows_ / 2.0f) * dx() - w / 2, top() + rows_ * dy() + 4, w, 30};
    }

    void drop() {
        if (!P().spend(bet_)) { E().sfx.play(Sfx::Error); autoOn_ = false; return; }
        P().wager(bet_);
        Ball b;
        b.stake = bet_;
        for (int i = 0; i < rows_; i++) b.dirs.push_back(rndi(0, 1));
        b.hue = rnd(0, 360);
        balls_.push_back(b);
        E().sfx.play(Sfx::Pop, 0.5f);
    }

    Vector2 ballPos(const Ball& b) const {
        int rights = 0;
        for (int i = 0; i < std::max(0, b.row); i++) rights += b.dirs[i];
        if (b.row < 0) {
            Vector2 a{cx(), top() - 70}, z = hit(0, 0);
            return Vector2Lerp(a, z, easeInCubic(b.u));
        }
        Vector2 a = hit(b.row, rights);
        Vector2 z = b.row + 1 < rows_ ? hit(b.row + 1, rights + b.dirs[b.row]) : center(bucket(rights + b.dirs[b.row]));
        Vector2 p = Vector2Lerp(a, z, b.u);
        p.y -= std::sin(b.u * PI) * dy() * 0.45f;  // little hop off the peg
        return p;
    }

    void land(const Ball& b) {
        int k = 0;
        for (int d : b.dirs) k += d;
        float m = table(rows_, risk_)[k];
        long long win = (long long)std::floor(b.stake * m);
        if (win > 0) P().earn(win, true);
        bucketPop_[k] = 1;
        Rectangle bk = bucket(k);
        fx_.floatText(TextFormat("%gx", m), {bk.x + bk.width / 2, bk.y - 10}, m >= 1 ? col::gold : col::muted, m >= 10 ? 30 : 20);
        if (m >= 10) {
            fx_.confetti(center(bk), 60);
            E().sfx.play(Sfx::Win, 0.6f);
        } else {
            E().sfx.play(m >= 1 ? Sfx::Coin : Sfx::Drop, m >= 1 ? 0.5f : 0.4f, m >= 1 ? 1 + std::min(1.0f, m * 0.1f) : 1);
        }
        recent_.push_front(m);
        if (recent_.size() > 9) recent_.pop_back();
        P().save();
    }

    void update(float dt) override {
        t_ += dt;
        fx_.update(dt);
        for (auto& p : pegFlash_) p = std::max(0.0f, p - dt * 4);
        for (auto& p : bucketPop_) p = std::max(0.0f, p - dt * 3);
        const float seg = 0.11f;
        for (auto& b : balls_) {
            b.u += dt / seg * (b.row < 0 ? 0.6f : 1.0f);
            while (b.u >= 1 && b.row < rows_) {
                b.u -= 1;
                b.row++;
                if (b.row < rows_) {
                    int rights = 0;
                    for (int i = 0; i < b.row; i++) rights += b.dirs[i];
                    pegFlash_[std::min(399, b.row * 20 + rights + 1)] = 1;
                    E().sfx.play(Sfx::Tick, 0.18f, 0.8f + b.row * 0.04f);
                } else {
                    land(b);
                }
            }
        }
        std::erase_if(balls_, [&](const Ball& b) { return b.row >= rows_; });
        if (autoOn_) {
            autoT_ -= dt;
            if (autoT_ <= 0) {
                autoT_ = 0.35f;
                drop();
            }
        }
        if (IsKeyPressed(KEY_SPACE)) drop();
        if (E().backPressed() && balls_.empty()) E().go(makeCasino);
    }

    void drawBoard() {
        Rectangle board{40, 100, 820, 600};
        ui::panel(board, alpha(rgba(0x120c22ff), 0.9f), 0.04f, alpha(col::pink, 0.35f));
        fx::beginAdd();
        fx::glow({cx(), top() + rows_ * dy() * 0.5f}, 420, alpha(col::violet, 0.12f));
        fx::endAdd();
        // dropper
        DrawRectangleRounded({cx() - 26, top() - 92, 52, 28}, 0.5f, 8, col::panelHi);
        DrawTriangle({cx() - 10, top() - 64}, {cx(), top() - 54}, {cx() + 10, top() - 64}, col::panelHi);
        // pegs
        float pr = std::max(3.0f, dx() * 0.12f);
        for (int r = 0; r < rows_; r++)
            for (int j = 0; j < r + 3; j++) {
                Vector2 p = peg(r, j);
                float f = pegFlash_[std::min(399, r * 20 + j)];
                if (f > 0) {
                    fx::beginAdd();
                    fx::glow(p, pr * 6, alpha(col::pink, 0.6f * f));
                    fx::endAdd();
                }
                DrawCircleV({p.x, p.y + 1.5f}, pr, alpha(BLACK, 0.4f));
                DrawCircleV(p, pr, mix(rgba(0xe8e2f0ff), col::pink, f));
                DrawCircleV({p.x - pr * 0.3f, p.y - pr * 0.3f}, pr * 0.35f, alpha(WHITE, 0.8f));
            }
        // buckets, coloured from red edges to gold centre
        const auto& tb = table(rows_, risk_);
        for (int k = 0; k <= rows_; k++) {
            Rectangle b = bucket(k);
            float edge = std::abs(k - rows_ / 2.0f) / (rows_ / 2.0f);
            Color c = mix(col::gold, col::red, edge);
            float pop = bucketPop_[k];
            b.y += pop * 6;
            DrawRectangleRounded({b.x, b.y + 4, b.width, b.height}, 0.3f, 6, darken(c, 0.5f));
            fx::gradRoundRect(b, 0.3f, lighten(c, 0.2f + pop * 0.3f), darken(c, 0.1f));
            float m = tb[k];
            std::string s = m >= 100 ? TextFormat("%.0f", m) : TextFormat("%g", m);
            ui::text(s, b.x + b.width / 2, b.y + b.height / 2, std::min(15.0f, b.width * 0.42f), col::outline, ui::Center, true);
        }
        // balls: little Pieters with a glow
        float br = std::max(7.0f, dx() * 0.26f);
        for (auto& b : balls_) {
            Vector2 p = ballPos(b);
            fx::beginAdd();
            fx::glow(p, br * 3, alpha(ColorFromHSV(b.hue, 0.6f, 1), 0.45f));
            fx::endAdd();
            art::portrait("pieter", p, br, col::outline, b.u * 120);
        }
        // recent results down the right edge
        float y = board.y + 30;
        for (float m : recent_) {
            Color c = m >= 10 ? col::gold : m >= 1 ? col::green : col::red;
            Rectangle pill{board.x + board.width - 74, y, 60, 26};
            DrawRectangleRounded(pill, 1, 8, alpha(c, 0.2f));
            DrawRectangleRoundedLinesEx(pill, 1, 8, 1.5f, c);
            ui::text(TextFormat("%gx", m), pill.x + 30, pill.y + 13, 13, c, ui::Center, true);
            y += 32;
        }
    }

    void drawSide() {
        Rectangle side{890, 100, 360, 600};
        ui::panel(side);
        ui::text("PLINKO", side.x + 22, side.y + 32, 30, col::text, ui::Left, true);
        bool idle = balls_.empty();
        ui::betSelector({side.x + 14, side.y + 58, side.width - 28, 100}, bet_, P().coins, !autoOn_);
        ui::text("RISK", side.x + 22, side.y + 190, 15, col::muted, ui::Left, true);
        const char* risks[3] = {"LOW", "MEDIUM", "HIGH"};
        const Color rc[3] = {col::green, col::gold, col::red};
        for (int i = 0; i < 3; i++)
            if (ui::button({side.x + 20 + i * 108.0f, side.y + 206, 100, 42}, risks[i], risk_ == i ? rc[i] : col::panelHi, 17, idle)) risk_ = i;
        ui::text("ROWS", side.x + 22, side.y + 274, 15, col::muted, ui::Left, true);
        const int rowsOpt[3] = {8, 12, 16};
        for (int i = 0; i < 3; i++)
            if (ui::button({side.x + 20 + i * 108.0f, side.y + 290, 100, 42}, std::to_string(rowsOpt[i]), rows_ == rowsOpt[i] ? col::violet : col::panelHi, 18, idle))
                rows_ = rowsOpt[i];
        const auto& tb = table(rows_, risk_);
        ui::text(TextFormat("max %gx", tb.front()), side.x + side.width - 22, side.y + 274, 15, col::gold, ui::Right, true);
        if (ui::button({side.x + 20, side.y + 360, side.width - 40, 70}, "DROP", col::pink, 32, P().coins >= bet_)) drop();
        ui::toggle({side.x + 24, side.y + 446, side.width - 48, 44}, "Auto drop", autoOn_);
        ui::text("SPACE drops one", side.x + side.width / 2, side.y + 512, 14, alpha(col::muted, 0.8f), ui::Center, true);
        ui::text(TextFormat("balls in play: %d", (int)balls_.size()), side.x + side.width / 2, side.y + 550, 16, col::muted, ui::Center);
    }

    void draw() override {
        casino::felt(t_);
        drawBoard();
        drawSide();
        fx_.draw();
        if (casino::header("PLINKO") && balls_.empty()) E().go(makeCasino);
    }

private:
    int rows_ = 12, risk_ = 1;
    long long bet_ = 10;
    bool autoOn_ = false;
    float t_ = 0, autoT_ = 0;
    std::vector<Ball> balls_;
    std::deque<float> recent_;
    float pegFlash_[400]{}, bucketPop_[17]{};
    Particles fx_;
};

}  // namespace

std::unique_ptr<Scene> makePlinko() { return std::make_unique<PlinkoScene>(); }

}  // namespace pg
