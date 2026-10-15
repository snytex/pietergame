#include "engine/Particles.hpp"

#include "engine/Ui.hpp"

#include <algorithm>

namespace pg {

void Particles::update(float dt) {
    for (auto& p : ps_) {
        p.life -= dt;
        p.vel.y += p.gravity * dt;
        if (p.drag > 0) p.vel = Vector2Scale(p.vel, std::exp(-p.drag * dt));
        p.pos = Vector2Add(p.pos, Vector2Scale(p.vel, dt));
        p.rot += p.rotVel * dt;
    }
    std::erase_if(ps_, [](const Particle& p) { return p.life <= 0; });
    for (auto& t : texts_) t.t += dt;
    std::erase_if(texts_, [](const FloatText& t) { return t.t >= t.life; });
}

static void drawStar(Vector2 c, float r, float rot, Color col) {
    for (int i = 0; i < 5; i++) {
        float a0 = rot + i * 2 * PI / 5 - PI / 2;
        Vector2 tip = Vector2Add(c, fromAngle(a0, r));
        Vector2 l = Vector2Add(c, fromAngle(a0 - PI / 5, r * 0.42f));
        Vector2 rr = Vector2Add(c, fromAngle(a0 + PI / 5, r * 0.42f));
        DrawTriangle(tip, l, c, col);
        DrawTriangle(tip, c, rr, col);
    }
}

void Particles::draw() const {
    for (auto& p : ps_) {
        float t = 1 - p.life / p.maxLife;
        float s = lerpf(p.size, p.endSize, t);
        Color c = mix(p.color, p.endColor.a == 0 && p.endColor.r == 255 ? alpha(p.color, 0) : p.endColor, t * t);
        switch (p.shape) {
            case PShape::Circle: DrawCircleV(p.pos, s, c); break;
            case PShape::Square: DrawRectanglePro({p.pos.x, p.pos.y, s * 2, s * 2}, {s, s}, p.rot * RAD2DEG, c); break;
            case PShape::Star: drawStar(p.pos, s, p.rot, c); break;
            case PShape::Heart: ui::heart(p.pos, s * 2, c); break;
            case PShape::Bubble:
                DrawCircleLinesV(p.pos, s, c);
                DrawCircleV({p.pos.x - s * 0.35f, p.pos.y - s * 0.35f}, s * 0.25f, c);
                break;
            case PShape::Spark: {
                Vector2 d = Vector2Normalize(p.vel);
                DrawLineEx(Vector2Subtract(p.pos, Vector2Scale(d, s * 2.5f)), p.pos, std::max(1.5f, s * 0.5f), c);
                break;
            }
        }
    }
    for (auto& t : texts_) {
        float k = t.t / t.life;
        float pop = easeOutBack(std::min(1.0f, t.t / 0.2f));
        float a = 1 - std::max(0.0f, (k - 0.6f) / 0.4f);
        ui::textOutline(t.text, t.pos.x, t.pos.y - easeOutCubic(k) * 50, t.size * pop, alpha(t.color, a),
                        alpha(col::outline, a), 2, ui::Center, true);
    }
}

Particle& Particles::add(const Particle& p) {
    if (ps_.size() > 3000) ps_.erase(ps_.begin());
    ps_.push_back(p);
    return ps_.back();
}

void Particles::burst(Vector2 at, int n, Color c, float speed, float size, float life, PShape shape, float gravity) {
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = at;
        p.vel = Vector2Scale(rndDir(), rnd(0.3f, 1.0f) * speed);
        p.life = p.maxLife = life * rnd(0.6f, 1.2f);
        p.size = size * rnd(0.6f, 1.3f);
        p.endSize = 0;
        p.color = c;
        p.drag = 3;
        p.gravity = gravity;
        p.shape = shape;
        p.rot = rnd(0, 6.28f);
        p.rotVel = rnd(-8, 8);
        add(p);
    }
}

void Particles::confetti(Vector2 at, int n, float speed) {
    const Color cs[] = {col::gold, col::pink, col::cyan, col::green, col::violet, col::orange};
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = at;
        p.vel = {rnd(-1, 1) * speed * 0.6f, -rnd(0.4f, 1.0f) * speed};
        p.life = p.maxLife = rnd(1.2f, 2.2f);
        p.size = rnd(4, 8);
        p.endSize = p.size;
        p.color = cs[rndi(0, 5)];
        p.endColor = alpha(p.color, 0);
        p.gravity = 700;
        p.drag = 1.5f;
        p.shape = PShape::Square;
        p.rotVel = rnd(-12, 12);
        add(p);
    }
}

void Particles::floatText(const std::string& s, Vector2 at, Color c, float size) {
    texts_.push_back({s, at, c, size, 0, 1.1f});
}

}  // namespace pg
