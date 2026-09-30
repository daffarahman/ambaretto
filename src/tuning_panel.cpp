#include "tuning_panel.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace forza {
namespace {
constexpr Color background{18, 27, 35, 248}, muted{154, 172, 183, 255};
constexpr Color accent{80, 208, 181, 255}, track_color{53, 67, 78, 255};
struct Layout {
    Rectangle panel;
    float row_height;
    explicit Layout(Rectangle rect) : panel(rect), row_height(std::min(54.0f, (rect.height - 298) / 7)) {}
    Rectangle tab(int index) const { return {panel.x + 18 + index * 178, panel.y + 66, 170, 30}; }
    Rectangle row(int index) const { return {panel.x + 18, panel.y + 111 + index * row_height, 348, row_height}; }
    Rectangle slider(int index) const {
        const auto r = row(index);
        return {r.x, r.y + 23, r.width, 10};
    }
    Rectangle close() const { return {panel.x + panel.width - 42, panel.y + 12, 26, 26}; }
    Rectangle button(int index) const { return {panel.x + 18 + index * 118, panel.y + panel.height - 58, 110, 30}; }
};
void button(Rectangle r, const char* text, bool selected = false) {
    const bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, selected ? Color{35, 89, 82, 255} : hover ? Color{60, 76, 88, 255} : track_color);
    DrawText(text, int(r.x + (r.width - MeasureText(text, 16)) / 2), int(r.y + 7), 16, selected ? accent : RAYWHITE);
}
void wrapped(const char* text, int x, int y, int width) {
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        const auto next = line.empty() ? word : line + " " + word;
        if (MeasureText(next.c_str(), 14) > width && !line.empty()) {
            DrawText(line.c_str(), x, y, 14, muted);
            y += 18;
            line = word;
        } else line = next;
    }
    DrawText(line.c_str(), x, y, 14, muted);
}
} // namespace

Rectangle TuningPanel::bounds(int width, int height) const {
    return {float(width - 400), 16, 384, float(height - 32)};
}
bool TuningPanel::contains(Vector2 point, int width, int height) const {
    return CheckCollisionPointRec(point, bounds(width, height));
}
TuningPanelAction TuningPanel::update(Car& car, int width, int height, const TuningPanelInput& input) {
    TuningPanelAction action;
    if (!input.focused) { cancel_drag(); return action; }
    const Layout layout(bounds(width, height));
    if (!input.down) cancel_drag();
    if (input.pressed) {
        if (CheckCollisionPointRec(input.mouse, layout.close()) || CheckCollisionPointRec(input.mouse, layout.button(2))) {
            cancel_drag(); action.close = true; return action;
        }
        if (CheckCollisionPointRec(input.mouse, layout.button(0))) {
            cancel_drag(); car.set_tuning({}); return action;
        }
        if (CheckCollisionPointRec(input.mouse, layout.button(1))) {
            cancel_drag(); action.reset_car = true; return action;
        }
        for (int tab = 0; tab < 2; ++tab) if (CheckCollisionPointRec(input.mouse, layout.tab(tab))) {
            tab_ = tab; selected_ = tab == 0 ? 0 : suspension_controls;
            cancel_drag(); return action;
        }
    }
    const int first = tab_ == 0 ? 0 : suspension_controls;
    const int count = tab_ == 0 ? suspension_controls : int(tuning_controls.size()) - first;
    if (input.vertical) selected_ = first + (selected_ - first + input.vertical + count) % count;
    CarTuning tuning = car.tuning();
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const int id = first + i;
        const auto& control = tuning_controls[id];
        const auto slider = layout.slider(i);
        const Rectangle grab{slider.x - 5, slider.y - 7, slider.width + 10, slider.height + 14};
        const bool hovered = CheckCollisionPointRec(input.mouse, layout.row(i));
        if (hovered && input.pressed) selected_ = id;
        if (input.pressed && CheckCollisionPointRec(input.mouse, grab)) { dragging_ = id; selected_ = id; }
        auto& value = tuning.*(control.value);
        const float upper = control.value == &CarTuning::travel ? std::min(control.max, tuning.rest_length - .05f) : control.max;
        if (dragging_ == id && input.down) {
            const float fraction = std::clamp((input.mouse.x - slider.x) / slider.width, 0.0f, 1.0f);
            const float step = control.step * (input.fine ? .1f : 1);
            value = std::clamp(std::round((control.min + fraction * (upper - control.min)) / step) * step, control.min, upper);
            changed = true;
        } else if ((selected_ == id && input.horizontal) || (hovered && input.scroll != 0)) {
            selected_ = id;
            value += (input.horizontal + (hovered ? input.scroll : 0)) * control.step * (input.fine ? .1f : 1);
            value = std::clamp(value, control.min, upper);
            changed = true;
        }
    }
    if (changed) car.set_tuning(tuning);
    return action;
}

void TuningPanel::draw(const Car& car, int width, int height) const {
    const Layout layout(bounds(width, height));
    const auto r = layout.panel;
    DrawRectangleRec({r.x + 5, r.y + 5, r.width, r.height}, {0, 0, 0, 80});
    DrawRectangleRec(r, background);
    DrawRectangle(int(r.x), int(r.y), int(r.width), 3, accent);
    DrawText("CAR TUNING", int(r.x + 18), int(r.y + 15), 23, RAYWHITE);
    DrawText("LIVE / parking brake   F3 or Esc closes", int(r.x + 18), int(r.y + 44), 13, accent);
    button(layout.close(), "X");
    button(layout.tab(0), "Suspension", tab_ == 0);
    button(layout.tab(1), "Handling", tab_ == 1);
    const int first = tab_ == 0 ? 0 : suspension_controls;
    const int count = tab_ == 0 ? suspension_controls : int(tuning_controls.size()) - first;
    for (int i = 0; i < count; ++i) {
        const int id = first + i;
        const auto& control = tuning_controls[id];
        const auto row = layout.row(i), slider = layout.slider(i);
        const float value = car.tuning().*(control.value);
        const float upper = control.value == &CarTuning::travel ? std::min(control.max, car.tuning().rest_length - .05f) : control.max;
        const float fraction = std::clamp((value - control.min) / (upper - control.min), 0.0f, 1.0f);
        DrawText(control.label, int(row.x), int(row.y), 16, selected_ == id ? accent : RAYWHITE);
        const std::string text = TextFormat(control.format, double(value * control.display_scale));
        DrawText(text.c_str(), int(row.x + row.width - MeasureText(text.c_str(), 15)), int(row.y), 15, muted);
        DrawRectangleRec({slider.x, slider.y + 3, slider.width, 4}, track_color);
        DrawRectangleRec({slider.x, slider.y + 3, slider.width * fraction, 4}, accent);
        DrawCircle(int(slider.x + slider.width * fraction), int(slider.y + 5), dragging_ == id ? 7 : 5, RAYWHITE);
    }
    wrapped(tuning_controls[selected_].hint, int(r.x + 18), int(r.y + r.height - 190), 348);
    DrawText("WHEEL CONTACT / compression / load", int(r.x + 18), int(r.y + r.height - 141), 13, muted);
    constexpr const char* names[] = {"FL", "FR", "RL", "RR"};
    for (int i = 0; i < 4; ++i) {
        const auto& wheel = car.wheels()[i];
        const int x = int(r.x + 18 + (i % 2) * 178), y = int(r.y + r.height - 119 + (i / 2) * 26);
        DrawCircle(x + 3, y + 7, 3, wheel.grounded ? accent : ORANGE);
        DrawText(TextFormat("%s %+.2fm %.1fkN", names[i], double(wheel.compression), double(wheel.normal_force / 1000)),
                 x + 13, y, 13, RAYWHITE);
    }
    button(layout.button(0), "Defaults");
    button(layout.button(1), "Reset car");
    button(layout.button(2), "Close / F3");
    DrawText("Arrows: select / adjust   Shift: fine", int(r.x + 18), int(r.y + r.height - 18), 12, muted);
    DrawText("RMB drag outside panel: orbit   Scroll: zoom", 26, height - 82, 16, RAYWHITE);
}
} // namespace forza
