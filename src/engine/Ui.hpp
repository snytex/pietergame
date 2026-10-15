// Immediate-mode UI toolkit. Call widgets from Scene::draw(); they return
// true on the frame they are clicked.
#pragma once

#include "engine/Core.hpp"

#include <string>

namespace pg::ui {

enum Align { Left, Center, Right };

// Text (y is the vertical center of the line for Center/Left/Right alike)
float measure(const std::string& s, float size, bool big = false);
void text(const std::string& s, float x, float y, float size, Color c, Align a = Left, bool big = false);
void textShadow(const std::string& s, float x, float y, float size, Color c, Align a = Left, bool big = false,
                float off = 3, Color shadow = col::outline);
void textOutline(const std::string& s, float x, float y, float size, Color c, Color outline, float thick,
                 Align a = Center, bool big = true);
void textRotated(const std::string& s, Vector2 pos, float size, float deg, Color c, bool big = false);
// Word-wrapped paragraph, returns height used.
float paragraph(const std::string& s, Rectangle box, float size, Color c, bool draw = true);

// Widgets
bool hovered(Rectangle r);
bool button(Rectangle r, const std::string& label, Color base = col::purple, float fontSize = 26, bool enabled = true);
bool bigButton(Rectangle r, const std::string& label, const std::string& sub, Color base, float fontSize = 34);
bool backButton();  // top-left "< BACK" (also triggered by Esc)
bool slider(Rectangle r, float& value, Color c = col::violet);  // value in 0..1
bool toggle(Rectangle r, const std::string& label, bool& value);

void panel(Rectangle r, Color c = col::panel, float round = 0.12f, Color border = col::outline);
void bar(Rectangle r, float frac, Color fill, Color back = col::bgDeep);
void coin(Vector2 c, float radius);
void heart(Vector2 c, float size, Color color);
void coinCounter(float rightX, float y, long long amount);  // top-right wallet
void titleBar(const std::string& title, Color accent = col::violet);

// Bet chooser: [-] amount [+] plus quick buttons. Returns true if changed.
bool betSelector(Rectangle r, long long& bet, long long maxBet, bool enabled = true);

void resetHover();

}  // namespace pg::ui
