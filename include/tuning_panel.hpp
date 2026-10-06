#pragma once
#include "vehicle.hpp"
#include <raylib.h>

namespace ambaretto {
struct TuningPanelInput {
    Vector2 mouse{};
    bool pressed = false, down = false, focused = true, fine = false;
    int horizontal = 0, vertical = 0;
    float scroll = 0;
};
struct TuningPanelAction { bool close = false, reset_car = false; };
class TuningPanel {
public:
    Rectangle bounds(int width, int height) const;
    bool contains(Vector2 point, int width, int height) const;
    TuningPanelAction update(Car& car, int width, int height, const TuningPanelInput& input);
    void draw(const Car& car, int width, int height) const;
    void select_tab(int tab);
    void cancel_drag() { dragging_ = -1; }
private:
    int tab_ = 0, selected_ = 0, dragging_ = -1;
};
} // namespace ambaretto
