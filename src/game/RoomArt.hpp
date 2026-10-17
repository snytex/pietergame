// The Bec IT Room: Pieter's bedroom, drawn procedurally for the Pieter Room
// scene. Lighting follows the real local time (sunrise, day, sunset, night)
// and the room goes dark when Pieter sleeps.
#pragma once

#include "engine/Core.hpp"

#include <functional>

namespace pg::room {

struct Env {
    float t = 0;          // seconds since the scene started
    float hour = 12;      // local time of day, fractional hours
    float lights = 1;     // 1 = awake, room lights on; 0 = lights out (Pieter sleeps)
    float gaming = 0;     // 0..1, how hard the PC is working (PLAY)
    float steam = 0;      // 0..1 shower steam
    bool horror = false;  // Five Nights at Pieter's: no lamp, LEDs or PC; doors need the side walls clear
    std::function<void()> outside;  // extra drawing behind the window glass (clipped to it), in front of the skyline
};

inline constexpr Rectangle kWindowGlass{872, 102, 206, 166};

float daylight(float hour);  // 0 = night .. 1 = full day
Color ambient(const Env& e);  // light-map clear colour

// Back to front.
void drawWalls(const Env& e);
void drawWindow(const Env& e);
void drawWallDecor(const Env& e);  // neon sign, shelf, clock
void drawFloorLight(const Env& e); // sun patch through the window
void drawBed(const Env& e);
void drawDesk(const Env& e);
void drawRug(const Env& e);

// Gaming chair Pieter sits in. Back part goes behind him, armrests in front.
void drawChairBack(Vector2 seat, const Env& e);
void drawChairFront(Vector2 seat, const Env& e);

// Light map contents (drawn additively into the light map, virtual coords).
void addLights(const Env& e);
// Additive bloom + god rays on top of the lit scene.
void drawBloom(const Env& e);

}  // namespace pg::room
