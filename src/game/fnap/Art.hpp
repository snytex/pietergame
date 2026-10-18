// Five Nights at Pieter's: everything that gets drawn. Procedural like the rest
// of the game, plus a lighting shader for the flashlight.
#pragma once

#include "engine/Core.hpp"

namespace pg::fnap::gfx {

// ---- primitives
void tri(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc);  // either winding
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color top, Color bottom);
void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c);
void limb(Vector2 a, Vector2 b, float w, Color c);  // thick line with round ends
float hash(float n);

// ---- the flashlight: darkness everywhere except the beam, plus haze in the air
struct Light {
    Vector2 pos{VW / 2.0f, VH / 2.0f};  // where the beam lands (virtual px)
    float radius = 300;                  // virtual px
    float power = 0;                     // 0 off .. 1 full (flicker and battery go in here)
    float dark = 0.94f;                  // how black the dark is
    Color tint = {255, 236, 200, 255};   // warm white, orange when the battery dies
    Vector2 hand{1010, 800};             // where the beam starts (shaft)
    struct Pt { Vector2 pos; float radius, strength; };
    Pt pts[6]{};                         // extra lights: lightning through a window, the PC...
    int count = 0;
    void add(Vector2 p, float r, float s) { if (count < 6) pts[count++] = {p, r, s}; }
};
void initLight();
void shutdownLight();
void darkness(const Light& l, float t);   // alpha-blended black with holes
void haze(const Light& l, float t);       // additive: warm beam, the light shaft, dust

// ---- the cast
// A portrait turned into a nightmare (0 Davi, 1 Kira, 2 the horse): hollow glowing eyes, a mouth full of teeth.
// jaw: 0 shut .. 1 wide open (jumpscares)
void nightmare(int who, Vector2 c, float r, float rot, float eyeGlow, float t, float jaw = 0);
void eyes(int who, Vector2 c, float r, float strength, float t);  // just the eyes, for the dark
// Davi (0) / Kira (1) standing in a hall. s = 1 is about 560 px tall.
// flinch 0..1: shielding their face from the light. reach 0..1: arms coming at you.
void stalker(int who, Vector2 feet, float s, float t, float flinch, float reach = 0);
Vector2 stalkerHead(Vector2 feet, float s);  // where stalker() puts the head
void stalkerShadow(int who, Vector2 feet, float s, Vector2 lightFrom, float strength);
// Plush horse, sitting. creep 0..1 leans it towards you; scared 0..1 makes it shudder.
void plush(Vector2 c, float s, float t, int i, float creep, float scared);
Vector2 plushEye(Vector2 c, float s, int i, float creep);
void monkeyMan(Vector2 c, float r, float t, float angry);
void toyMonkey(Vector2 feet, float s, float clash);
void windmill(Vector2 base, float s, float t, Color c);
void jackOLantern(Vector2 c, float r, float t, float flicker);
void horseSilhouette(Vector2 feet, float s, Color c, float facing = 1);

// The other one: a far too tall, perfectly still Pieter with holes for eyes.
// s = 1 is about 720 px tall; look -1..1 turns his head; a fades him.
void other(Vector2 feet, float s, float t, float look, float a = 1);
Vector2 otherHead(Vector2 feet, float s, float look);
// his face alone (jumpscare, subliminals, the phone). jaw 0..1 opens the smile.
void otherFace(Vector2 c, float r, float t, float jaw = 0, float rot = 0, float a = 1);

// ---- first person: Pieter's hands
void flashlightHand(Vector2 aim, float t, float on, float bob);
void holdingHands(float push, float t, bool closet);  // pressing on a door, push 0..1
void thirdHand(float k, float t);     // a hand that isn't yours, between yours on the door (k 0..1 over its visit)
void shoulderHand(float k, float t);  // something resting on your shoulder from behind (k 0..1 how far in)

// ---- the house from outside: a row of canal houses on a rainy night (or at dawn)
struct Street {
    float t = 0;
    float zoom = 1;          // camera push towards Pieter's window (1 = whole street)
    Vector2 focus{640, 360}; // zoom centre in street coords (default: Pieter's window)
    float lightning = 0;     // 0..1 flash
    float dawn = 0;          // 0 night .. 1 sunrise
    float rain = 1;
    int night = 0;           // more wrong things on later nights
    bool figures = true;     // silhouettes that only show in the lightning
};
inline constexpr Rectangle kPieterWindow{668, 349, 31, 45};  // top floor of the fourth house
void street(const Street& s);

}  // namespace pg::fnap::gfx
