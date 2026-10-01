#include "ui_font.hpp"
#include "tuning_panel.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace forza {
namespace {
constexpr Color background{0, 0, 128, 255}, muted{192, 192, 192, 255};
constexpr Color selected_color{0, 128, 128, 255}, ink{0, 0, 0, 255};
constexpr Color accent = YELLOW, track_color = muted;
constexpr std::array<int, 3> tab_starts{{0, suspension_controls, suspension_controls + handling_controls}};
constexpr std::array<int, 3> tab_counts{{suspension_controls, handling_controls,
    int(tuning_controls.size()) - suspension_controls - handling_controls}};
struct Layout {
    Rectangle panel;
    float row_height;
    explicit Layout(Rectangle rect) : panel(rect), row_height(std::min(54.0f, (rect.height - 298) / 7)) {}
    Rectangle tab(int index) const { return {panel.x + 18 + index * 118, panel.y + 66, 110, 30}; }
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
    DrawRectangleRec(r, selected || hover ? selected_color : track_color);
    DrawRectangleLinesEx(r, 1, RAYWHITE);
    int size = 16;
    while (size > 10 && forza::ui::measure_text(text, size) > r.width - 12) --size;
    forza::ui::draw_text(text, int(r.x + (r.width - forza::ui::measure_text(text, size)) / 2),
        int(r.y + (r.height - size) / 2), size, selected || hover ? RAYWHITE : ink);
}
void wrapped(const char* text, int x, int y, int width) {
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        const auto next = line.empty() ? word : line + " " + word;
        if (forza::ui::measure_text(next.c_str(), 14) > width && !line.empty()) {
            forza::ui::draw_text(line.c_str(), x, y, 14, muted);
            y += 18;
            line = word;
        } else line = next;
    }
    forza::ui::draw_text(line.c_str(), x, y, 14, muted);
}
} // namespace

Rectangle TuningPanel::bounds(int width, int height) const {
    return {float(width - 400), 48, 384, float(height - 64)};
}
bool TuningPanel::contains(Vector2 point, int width, int height) const {
    return CheckCollisionPointRec(point, bounds(width, height));
}
void TuningPanel::select_tab(int tab) {
    tab_ = std::clamp(tab, 0, 2);
    selected_ = tab_starts[tab_];
    cancel_drag();
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
        for (int tab = 0; tab < 3; ++tab) if (CheckCollisionPointRec(input.mouse, layout.tab(tab))) {
            select_tab(tab); return action;
        }
    }
    const int first = tab_starts[tab_], count = tab_counts[tab_];
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
    DrawRectangleRec({r.x + 5, r.y + 5, r.width, r.height}, {0, 0, 0, 160});
    DrawRectangleRec(r, background);
    DrawRectangleLinesEx(r, 2, RAYWHITE);
    forza::ui::draw_text("CAR TUNING", int(r.x + 18), int(r.y + 15), 23, YELLOW);
    forza::ui::draw_text("LIVE / parking brake   Esc closes", int(r.x + 18), int(r.y + 44), 13, RAYWHITE);
    button(layout.close(), "X");
    button(layout.tab(0), "Suspension", tab_ == 0);
    button(layout.tab(1), "Handling", tab_ == 1);
    button(layout.tab(2), "Performance", tab_ == 2);
    const int first = tab_starts[tab_], count = tab_counts[tab_];
    for (int i = 0; i < count; ++i) {
        const int id = first + i;
        const auto& control = tuning_controls[id];
        const auto row = layout.row(i), slider = layout.slider(i);
        const float value = car.tuning().*(control.value);
        const float upper = control.value == &CarTuning::travel ? std::min(control.max, car.tuning().rest_length - .05f) : control.max;
        const float fraction = std::clamp((value - control.min) / (upper - control.min), 0.0f, 1.0f);
        if (selected_ == id) DrawRectangleRec({row.x - 6, row.y - 3, row.width + 12, 22}, selected_color);
        forza::ui::draw_text(control.label, int(row.x), int(row.y), 16, selected_ == id ? accent : RAYWHITE);
        const std::string text = TextFormat(control.format, double(value * control.display_scale));
        forza::ui::draw_text(text.c_str(), int(row.x + row.width - forza::ui::measure_text(text.c_str(), 15)), int(row.y), 15, RAYWHITE);
        DrawRectangleRec({slider.x, slider.y + 3, slider.width, 4}, track_color);
        DrawRectangleRec({slider.x, slider.y + 3, slider.width * fraction, 4}, accent);
        const float thumb_width = dragging_ == id ? 14.0f : 10.0f;
        DrawRectangleRec({slider.x + slider.width * fraction - thumb_width / 2, slider.y - 2, thumb_width, 14}, RAYWHITE);
    }
    wrapped(tuning_controls[selected_].hint, int(r.x + 18), int(r.y + r.height - 190), 348);
    forza::ui::draw_text("WHEEL CONTACT / compression / load", int(r.x + 18), int(r.y + r.height - 141), 13, muted);
    constexpr const char* names[] = {"FL", "FR", "RL", "RR"};
    for (int i = 0; i < 4; ++i) {
        const auto& wheel = car.wheels()[i];
        const int x = int(r.x + 18 + (i % 2) * 178), y = int(r.y + r.height - 119 + (i / 2) * 26);
        DrawRectangle(x, y + 4, 6, 6, wheel.grounded ? accent : ORANGE);
        forza::ui::draw_text(TextFormat("%s %+.2fm %.1fkN", names[i], double(wheel.compression), double(wheel.normal_force / 1000)),
                 x + 13, y, 13, RAYWHITE);
    }
    button(layout.button(0), "Defaults");
    button(layout.button(1), "Reset car");
    button(layout.button(2), "Close / Esc");
    forza::ui::draw_text("Arrows: select / adjust   Shift: fine", int(r.x + 18), int(r.y + r.height - 18), 12, muted);
    forza::ui::draw_text("RMB drag outside panel: orbit   Scroll: zoom", 26, height - 82, 16, RAYWHITE);
}
} // namespace forza
