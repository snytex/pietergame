// Procedural art shared between game modes: portraits, hats, food, horses,
// playing cards, item icons, backgrounds.
#pragma once

#include "engine/Core.hpp"

#include <string>

namespace pg::art {

// Circular character portrait cut from the menu artwork ("pieter", "horse", "davi", "kira").
void portrait(const std::string& who, Vector2 c, float radius, Color ring = col::outline, float rotDeg = 0,
              bool flip = false, Color tint = WHITE);
// Pieter portrait with the player's equipped hat.
void pieter(Vector2 c, float radius, const std::string& hat, float rotDeg = 0, Color tint = WHITE);
void hat(const std::string& id, Vector2 headCenter, float radius, float rotDeg = 0);

void boba(Vector2 c, float size, Color tea = col::boba);
void stroopwafel(Vector2 c, float w, float h, float rotDeg = 0, Color tint = WHITE);
void stroopwafelTop(Vector2 c, float radius);  // round waffle seen from above
void bomb(Vector2 c, float r, float t);
void gem(Vector2 c, float r, Color color);

// Side-view galloping horse. `phase` advances the leg cycle, `facing` = +1 right.
// `jockey` is a portrait name ("pieter", "davi", ...) or empty for a generic rider.
void horse(Vector2 feet, float scale, float phase, Color body, Color mane, Color silk, const std::string& jockey = "",
           float facing = 1);

// Playing card: rank 1..13, suit 0..3 (spades, hearts, diamonds, clubs).
void card(Rectangle r, int rank, int suit, bool faceUp);
void suit(int s, Vector2 c, float size);

// Icon for any shop item id.
void itemIcon(const std::string& id, Vector2 c, float size);

// Backgrounds
void menuBackground(float parallaxX = 0, float parallaxY = 0, float dim = 0);
void purpleBackdrop(float t, bool grid = true);
void vignette(float strength = 0.6f);

}  // namespace pg::art
