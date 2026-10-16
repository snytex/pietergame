// Rendering effects shared by every scene: additive glow (fake bloom), soft
// shadows, shaded "3D" shapes, blurred backdrops, shockwave rings.
#pragma once

#include "engine/Core.hpp"

#include <string>
#include <vector>

namespace pg::fx {

void init();
void shutdown();

// Additive light. Wrap many glow() calls in one begin/end pair (it flushes the batch).
void beginAdd();
void endAdd();
void glow(Vector2 c, float radius, Color color);                 // soft radial light
void glowLine(Vector2 a, Vector2 b, float width, Color color);    // light streak

// Normal-blend helpers
void softShadow(Vector2 c, float rx, float ry, float strength = 0.5f);
void shadedBall(Vector2 c, float r, Color base, float outline = 3);  // sphere with light from top-left
void shadedRoundRect(Rectangle r, float round, Color base, float outline = 3);
// Textures (loaded through Engine::tex, e.g. "tex/grass", "spr/tree").
// Fill `dest` with a repeating texture; `scale` = screen px per texel, `scroll` in texels.
void tiled(Texture2D& t, Rectangle dest, float scale = 1, Vector2 scroll = {0, 0}, Color tint = WHITE);
// Textured quad with explicit UVs (in texels; textures repeat). Points in order tl, tr, br, bl.
void texQuad(Texture2D& t, const Vector2 p[4], const Vector2 uv[4], Color tint = WHITE);
// Sprite centred on `c`, scaled and rotated (degrees).
void sprite(Texture2D& t, Vector2 c, float scale, float rotDeg = 0, Color tint = WHITE, bool flipX = false);
// Rounded rectangle filled with an exact vertical gradient (roundness as in DrawRectangleRounded).
void gradRoundRect(Rectangle r, float roundness, Color top, Color bottom);
void dashedRing(Vector2 c, float r, float thick, int dashes, float rot, Color color);
void gradientText(const std::string& s, float x, float y, float size, Color top, Color bottom, float outline);
// Blurred, darkened copy of an image from assets/images (cached).
Texture2D& blurred(const std::string& name);
void blurredBackdrop(const std::string& name, float dim, float t);  // with slow drift + bokeh
void bokeh(float t, int count, Color c, float seed = 0);

// Expanding ring shockwaves.
struct Shock {
    Vector2 pos;
    float r0, r1, t = 0, life = 0.5f, thick = 10;
    Color color;
};
class Shocks {
public:
    void add(Vector2 at, float r0, float r1, Color c, float life = 0.45f, float thick = 10) { list_.push_back({at, r0, r1, 0, life, thick, c}); }
    void update(float dt);
    void draw() const;  // additive inside
private:
    std::vector<Shock> list_;
};

}  // namespace pg::fx
