// Horizontally scrolling row of big mode tiles (main menu, casino lobby).
// Mouse wheel, drag, arrow keys or the side arrows scroll it; click or ENTER opens.
#pragma once

#include "engine/Engine.hpp"

#include <string>
#include <vector>

namespace pg {

struct Tile {
    std::string id;     // art id (see art::modeArt)
    std::string title, sub;
    std::string tag;    // "NEW", "HOT", ... (empty = none)
    Color color;
    SceneFactory go;
};

class Carousel {
public:
    void setTiles(std::vector<Tile> tiles);
    // Draws inside `area` and handles input. Returns the tile opened this frame, or nullptr.
    const Tile* run(Rectangle area, float t, bool keys = true);
    void setFocus(int i) { focus_ = i; }
    int focus() const { return focus_; }

    float tileW = 238, gap = 22;

private:
    float maxScroll(Rectangle area) const;
    void ensureVisible(Rectangle area);

    std::vector<Tile> tiles_;
    std::vector<float> hover_;
    float scroll_ = 0, target_ = 0;
    int focus_ = 0;
    bool pressed_ = false, dragging_ = false;
    float pressX_ = 0, pressScroll_ = 0;
    bool mouseFocus_ = true;
};

}  // namespace pg
