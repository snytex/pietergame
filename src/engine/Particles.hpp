// Lightweight particle system + floating combat text.
#pragma once

#include "engine/Core.hpp"

#include <string>
#include <vector>

namespace pg {

enum class PShape { Circle, Square, Star, Heart, Bubble, Spark };

struct Particle {
    Vector2 pos{}, vel{};
    float life = 1, maxLife = 1;
    float size = 6, endSize = 0;
    float gravity = 0, drag = 0;
    float rot = 0, rotVel = 0;
    Color color = WHITE, endColor = {255, 255, 255, 0};
    PShape shape = PShape::Circle;
};

struct FloatText {
    std::string text;
    Vector2 pos{};
    Color color = WHITE;
    float size = 26;
    float t = 0, life = 1.0f;
};

class Particles {
public:
    void update(float dt);
    void draw() const;
    void clear() { ps_.clear(); texts_.clear(); }

    Particle& add(const Particle& p);
    // Radial burst of `n` particles.
    void burst(Vector2 at, int n, Color c, float speed = 250, float size = 6, float life = 0.6f,
               PShape shape = PShape::Circle, float gravity = 0);
    void confetti(Vector2 at, int n, float speed = 500);
    void floatText(const std::string& s, Vector2 at, Color c, float size = 26);

    size_t count() const { return ps_.size(); }

private:
    std::vector<Particle> ps_;
    std::vector<FloatText> texts_;
};

}  // namespace pg
