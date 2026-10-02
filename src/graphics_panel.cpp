#include "graphics_panel.hpp"
#include "ui_font.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace forza {
namespace {
constexpr Color blue{0, 0, 128, 255}, gray{192, 192, 192, 255}, teal{0, 128, 128, 255}, ink{0, 0, 0, 255};
constexpr const char* labels[] = {"Preset", "Sun shadows", "Soft shadow edges", "Shadow distance", "City / vehicle lights",
    "View distance", "Brightness", "Vertical sync", "Frame limit"};
constexpr const char* hints[] = {
    "Low saves GPU work. Balanced keeps nearby shadows and lights. High increases shadow resolution and distance. Any individual change becomes Custom.",
    "Buildings, trees and cars cast sunlight shadows. Higher quality improves detail and uses more GPU memory. Off is fastest.",
    "Smooths the edges of sun shadows. Slight extra GPU cost. Turn it off for crisp edges or faster graphics. Requires sun shadows.",
    "How far nearby objects cast sun shadows. Shorter distances improve detail and performance. Longer distances show more shadows. Requires sun shadows.",
    "Streetlights and vehicle lamps illuminate nearby cars and objects at night. Turn off to reduce GPU work. City signs and windows remain visible.",
    "How far scenery is visible. Shorter distances improve performance; farther distances suit flying and skyline views. Fog blends distant scenery.",
    "Adjusts scene brightness without changing the clock or UI. Raise this if night scenes are too dark on your display. Negligible performance cost.",
    "Matches frames to your monitor to prevent tearing. VSync controls pacing while enabled; your frame limit can still lower the cap. Off can reduce input delay.",
    "Lower limits reduce GPU usage and heat. Higher limits can feel smoother on fast monitors. Unlimited uses all available rendering capacity. VSync controls pacing while enabled."
};
struct Layout {
    Rectangle panel;
    float left_width, row_height;
    explicit Layout(Rectangle r) : panel(r), left_width(r.width * .56f - 24), row_height(std::min(42.0f, (r.height - 216) / 8)) {}
    Rectangle preset(int i) const { return {panel.x + 18 + i * 102, panel.y + 70, 94, 28}; }
    Rectangle row(int id) const { return {panel.x + 18, panel.y + 108 + (id - 1) * row_height, left_width, row_height - 2}; }
    Rectangle slider(int id) const { auto r = row(id); return {r.x + 8, r.y + 26, r.width - 16, 7}; }
    Rectangle value(int id) const { auto r = row(id); return {r.x + r.width - 184, r.y + 3, 178, 26}; }
    Rectangle close() const { return {panel.x + panel.width - 42, panel.y + 12, 26, 26}; }
    Rectangle button(int i) const { return {panel.x + 18 + i * 158, panel.y + panel.height - 68, 146, 30}; }
    Rectangle help() const { return {panel.x + panel.width * .56f + 12, panel.y + 110, panel.width * .44f - 30, panel.height - 225}; }
};
void text(const char* value, float x, float y, int size = 15, Color color = RAYWHITE) {
    ui::draw_text(value, int(x), int(y), size, color);
}
void button(Rectangle r, const char* value, bool selected, bool enabled = true) {
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, selected || hover ? teal : gray);
    DrawRectangleLinesEx(r, selected ? 2 : 1, selected ? YELLOW : RAYWHITE);
    int size = 15;
    while (size > 11 && ui::measure_text(value, size) > r.width - 12) --size;
    text(value, r.x + (r.width - ui::measure_text(value, size)) / 2, r.y + (r.height - size) / 2, size,
        !enabled ? DARKGRAY : selected || hover ? RAYWHITE : ink);
}
int wrapped(const char* value, float x, float y, int width, int size = 14, Color color = gray) {
    std::istringstream words(value);
    std::string word, line;
    const float first = y;
    while (words >> word) {
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measure_text(next.c_str(), size) > width) {
            text(line.c_str(), x, y, size, color); y += size + 5; line = word;
        } else line = next;
    }
    text(line.c_str(), x, y, size, color);
    return int(y - first + size + 5);
}
bool slider_row(int id) { return id == 3 || id == 5 || id == 6; }
bool enabled(int id, const GraphicsSettings& settings) { return settings.shadows != 0 || (id != 2 && id != 3); }
float& slider_value(int id, GraphicsSettings& s) { return id == 3 ? s.shadow_distance : id == 5 ? s.view_distance : s.brightness; }
float slider_value(int id, const GraphicsSettings& s) { return id == 3 ? s.shadow_distance : id == 5 ? s.view_distance : s.brightness; }
float minimum(int id) { return id == 3 ? 30 : id == 5 ? 500 : .6f; }
float maximum(int id) { return id == 3 ? 180 : id == 5 ? 6000 : 1.5f; }
float step(int id, bool fine) { return id == 3 ? (fine ? 1 : 10) : id == 5 ? (fine ? 25 : 250) : (fine ? .01f : .05f); }
void adjust(int id, int direction, GraphicsSettings& s, bool fine) {
    if (!direction || !enabled(id, s)) return;
    if (id == 0) {
        const int current = s.preset() == GraphicsPreset::Custom ? 1 : int(s.preset());
        s.apply_preset(GraphicsPreset(std::clamp(current + direction, 0, 2)));
    } else if (id == 1) s.shadows = std::clamp(s.shadows + direction, 0, 3);
    else if (id == 2) s.soft_shadows = !s.soft_shadows;
    else if (id == 4) s.local_lights = !s.local_lights;
    else if (id == 7) s.vsync = !s.vsync;
    else if (id == 8) {
        const auto current = std::find(graphics_fps_limits.begin(), graphics_fps_limits.end(), s.fps_limit);
        const int index = current == graphics_fps_limits.end() ? 1 : int(current - graphics_fps_limits.begin());
        s.fps_limit = graphics_fps_limits[std::clamp(index + direction, 0, int(graphics_fps_limits.size()) - 1)];
    } else if (slider_row(id)) {
        auto& value = slider_value(id, s);
        value = std::clamp(std::round((value + direction * step(id, fine)) * 100) / 100, minimum(id), maximum(id));
    }
}
std::string value_label(int id, const GraphicsSettings& s) {
    if (id == 1) { constexpr const char* names[] = {"Off", "Low / 512", "Medium / 1024", "High / 2048"}; return names[s.shadows]; }
    if (id == 2) return s.soft_shadows ? "On" : "Off";
    if (id == 4) return s.local_lights ? "On" : "Off";
    if (id == 7) return s.vsync ? "On" : "Off";
    if (id == 8) return s.fps_limit == 0 ? "Unlimited" : std::to_string(s.fps_limit) + " FPS";
    if (id == 6) return std::to_string(int(std::round(s.brightness * 100))) + "%";
    return std::to_string(int(std::round(slider_value(id, s)))) + " m";
}
bool repeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
} // namespace

void GraphicsPanel::open(const GraphicsSettings& settings) {
    original_ = pending_ = settings; visible_ = true; selected_ = 0; dragging_ = -1;
}
Rectangle GraphicsPanel::bounds(int width, int height) const {
    const float w = float(std::min(940, width - 48)), h = float(std::min(640, height - 80));
    return {(width - w) / 2, (height - h) / 2 + 12, w, h};
}
GraphicsPanelAction GraphicsPanel::update() {
    GraphicsPanelInput input;
    input.mouse = GetMousePosition(); input.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT); input.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    input.focused = IsWindowFocused(); input.fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    input.horizontal = int(repeat(KEY_RIGHT)) - int(repeat(KEY_LEFT)); input.vertical = int(repeat(KEY_DOWN)) - int(repeat(KEY_UP));
    input.tab = repeat(KEY_TAB) ? (input.fine ? -1 : 1) : 0;
    input.scroll = GetMouseWheelMove(); input.apply = IsKeyPressed(KEY_ENTER); input.cancel = IsKeyPressed(KEY_ESCAPE);
    input.defaults = IsKeyPressed(KEY_D) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    return update(GetScreenWidth(), GetScreenHeight(), input);
}
GraphicsPanelAction GraphicsPanel::update(int width, int height, const GraphicsPanelInput& input) {
    if (!visible_) return GraphicsPanelAction::None;
    if (!input.focused) { dragging_ = -1; return GraphicsPanelAction::None; }
    const Layout layout(bounds(width, height));
    const auto hit = [&](Rectangle r) { return CheckCollisionPointRec(input.mouse, r); };
    if (!input.down) dragging_ = -1;
    if (input.cancel || (input.pressed && (hit(layout.close()) || hit(layout.button(1))))) {
        dragging_ = -1; return GraphicsPanelAction::Cancel;
    }
    if (input.tab) { dragging_ = -1; selected_ = (selected_ + input.tab + 12) % 12; }
    if (input.vertical) { dragging_ = -1; selected_ = (std::min(selected_, 8) + input.vertical + 9) % 9; }
    const auto before = pending_;
    if (input.defaults || (input.pressed && hit(layout.button(0))) || (input.apply && selected_ == 9)) {
        dragging_ = -1; pending_.apply_preset(GraphicsPreset::Balanced); selected_ = 0;
    } else if (input.apply || (input.pressed && hit(layout.button(2)))) {
        dragging_ = -1; return selected_ == 10 && input.apply ? GraphicsPanelAction::Cancel : GraphicsPanelAction::Apply;
    } else {
        if (input.pressed) for (int i = 0; i < 3; ++i) if (hit(layout.preset(i))) {
            dragging_ = -1; selected_ = 0; pending_.apply_preset(GraphicsPreset(i));
        }
        if (input.horizontal) adjust(selected_, input.horizontal, pending_, input.fine);
        for (int id = 1; id <= 8; ++id) {
            const bool hovered = hit(layout.row(id));
            if (input.pressed && hovered) selected_ = id;
            if (!enabled(id, pending_)) continue;
            if (slider_row(id)) {
                const auto slider = layout.slider(id);
                const Rectangle grab{slider.x - 4, slider.y - 7, slider.width + 8, slider.height + 14};
                if (input.pressed && hit(grab)) { dragging_ = id; selected_ = id; }
                if (dragging_ == id && input.down) {
                    auto& value = slider_value(id, pending_);
                    const float fraction = std::clamp((input.mouse.x - slider.x) / slider.width, 0.0f, 1.0f);
                    const float increment = step(id, input.fine);
                    value = std::clamp(minimum(id) + std::round(fraction * (maximum(id) - minimum(id)) / increment) * increment,
                        minimum(id), maximum(id));
                }
            } else if (input.pressed && hit(layout.value(id))) {
                const auto r = layout.value(id);
                const bool toggle = id == 2 || id == 4 || id == 7;
                adjust(id, toggle || input.mouse.x >= r.x + r.width / 2 ? 1 : -1, pending_, input.fine);
            }
            if (hovered && input.scroll != 0) {
                selected_ = id; adjust(id, input.scroll > 0 ? 1 : -1, pending_, input.fine);
            }
        }
    }
    return pending_ != before ? GraphicsPanelAction::Preview : GraphicsPanelAction::None;
}
void GraphicsPanel::draw(float fps, const std::string& status) const {
    if (!visible_) return;
    const Layout layout(bounds(GetScreenWidth(), GetScreenHeight()));
    const auto r = layout.panel;
    DrawRectangle(0, 32, GetScreenWidth(), GetScreenHeight() - 32, {0, 0, 0, 155});
    DrawRectangleRec({r.x + 6, r.y + 6, r.width, r.height}, {0, 0, 0, 180});
    DrawRectangleRec(r, blue); DrawRectangleLinesEx(r, 2, RAYWHITE);
    text("SETTINGS / GRAPHICS", r.x + 18, r.y + 15, 23, YELLOW);
    text("Live preview - Apply saves; Cancel restores your previous settings.", r.x + 18, r.y + 46, 14);
    button(layout.close(), "X", false);
    for (int i = 0; i < 3; ++i) {
        constexpr const char* names[] = {"Low", "Balanced", "High"};
        button(layout.preset(i), names[i], int(pending_.preset()) == i);
    }
    text(pending_.preset_name(), r.x + 336, r.y + 77, 15, pending_.preset() == GraphicsPreset::Custom ? YELLOW : gray);
    if (selected_ == 0) DrawRectangleLinesEx({r.x + 14, r.y + 65, layout.left_width + 8, 37}, 1, YELLOW);
    for (int id = 1; id <= 8; ++id) {
        const auto row = layout.row(id);
        const bool active = enabled(id, pending_);
        if (selected_ == id) DrawRectangleRec(row, teal);
        text(labels[id], row.x + 8, row.y + 5, 15, !active ? gray : selected_ == id ? YELLOW : RAYWHITE);
        const auto label = value_label(id, pending_);
        if (slider_row(id)) {
            text(label.c_str(), row.x + row.width - ui::measure_text(label.c_str(), 15) - 8, row.y + 5, 15, active ? RAYWHITE : gray);
            const auto slider = layout.slider(id);
            const float fraction = (slider_value(id, pending_) - minimum(id)) / (maximum(id) - minimum(id));
            DrawRectangleRec({slider.x, slider.y + 2, slider.width, 3}, gray);
            DrawRectangleRec({slider.x, slider.y + 2, slider.width * fraction, 3}, active ? YELLOW : DARKGRAY);
            DrawRectangleRec({slider.x + slider.width * fraction - 5, slider.y - 2, 10, 11}, active ? RAYWHITE : gray);
        } else {
            const auto value = layout.value(id);
            button(value, label.c_str(), selected_ == id, active);
            if (id == 1 || id == 8) { text("<", value.x + 8, value.y + 6, 14, ink); text(">", value.x + value.width - 16, value.y + 6, 14, ink); }
        }
    }
    const auto help = layout.help();
    DrawLine(int(help.x - 12), int(r.y + 108), int(help.x - 12), int(r.y + r.height - 113), gray);
    const int selected = std::min(selected_, 8);
    text(labels[selected], help.x, help.y, 18, YELLOW);
    const int used = wrapped(hints[selected], help.x, help.y + 33, int(help.width));
    float y = std::max(help.y + 33 + used + 18, r.y + r.height - 234);
    text(TextFormat("CURRENT: %.0f FPS", double(std::isfinite(fps) ? fps : 0)), help.x, y, 21, YELLOW);
    y += 33;
    wrapped(pending_.vsync ? "VSync controls pacing while enabled." : "VSync is off. Your frame limit controls pacing.", help.x, y, int(help.width), 13, RAYWHITE);
    y += 43;
    wrapped("FPS is a preview of this view. Heavy traffic and flying can cost more.", help.x, y, int(help.width), 12);
    BeginScissorMode(int(r.x + 18), int(r.y + r.height - 105), int(r.width - 36), 28);
    text(status.empty() ? (dirty() ? "Preview active / unsaved changes" : "Current settings / no changes") : status.c_str(),
        r.x + 18, r.y + r.height - 99, 14, status.empty() ? gray : YELLOW);
    EndScissorMode();
    button(layout.button(0), "Defaults", selected_ == 9);
    button(layout.button(1), "Cancel / Esc", selected_ == 10);
    button(layout.button(2), dirty() ? "Apply & save *" : "Apply & save", selected_ == 11);
    text("Tab: focus   Arrows: adjust   Shift: fine   Enter: apply   Ctrl+D: defaults", r.x + 18, r.y + r.height - 24, 13, gray);
}
} // namespace forza
