#include "menu_bar.hpp"
#include "ui_font.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
namespace {
constexpr Color blue{0, 0, 128, 255}, gray{192, 192, 192, 255}, ink{0, 0, 0, 255}, selected{0, 128, 128, 255};
struct Item { const char* label; MenuCommand command; };
constexpr Item files[] = {{"Resume game", MenuCommand::Resume}, {"Pause game", MenuCommand::Pause}, {"Quit", MenuCommand::Quit}};
constexpr Item edits[] = {{"Recover vehicle", MenuCommand::Recover}, {"Restore car tuning", MenuCommand::CarDefaults}};
constexpr Item settings[] = {{"World map", MenuCommand::Map}, {"Car tuning", MenuCommand::Tuning}, {"Graphics...", MenuCommand::Graphics}, {"Controller mapping...", MenuCommand::Controllers}};
constexpr Item helps[] = {{"Controls...", MenuCommand::Controls}, {"About...", MenuCommand::About}};
constexpr const char* titles[] = {"File", "Edit", "Settings", "Help"};
constexpr const Item* menus[] = {files, edits, settings, helps};
constexpr int counts[] = {3, 2, 4, 2};
Rectangle title_rect(int menu) {
    constexpr float gap = 48;
    float x = 8;
    for (int i = 0; i < menu; ++i) x += ui::measure_text(titles[i], 19) + gap;
    return {x, 0, float(ui::measure_text(titles[menu], 19) + 24), menu_height};
}
Rectangle dropdown_rect(int menu) { return {title_rect(menu).x, menu_height, 302, float(counts[menu] * 32 + 8)}; }
Rectangle item_rect(int menu, int item) { auto r = dropdown_rect(menu); return {r.x + 4, r.y + 4 + item * 32, r.width - 8, 32}; }
Rectangle panel_rect() { return {float((GetScreenWidth() - 944) / 2), 64, 944, float(GetScreenHeight() - 96)}; }
int visible_rows() { return std::max(1, int((panel_rect().height - 230) / 28)); }
int first_row(int selection, int total) { return std::clamp(selection - visible_rows() / 2, 0, std::max(0, total - visible_rows())); }
Rectangle row_rect(int row, bool binding) { auto r = panel_rect(); return {r.x + (binding ? 456 : 20), r.y + 120 + row * 28, binding ? 468.0f : 416.0f, 28}; }
Rectangle button_rect(int index) { auto r = panel_rect(); return {r.x + 20 + index * 182, r.y + r.height - 76, 172, 30}; }
bool hit(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
bool repeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
void box(Rectangle r, Color color = gray) {
    DrawRectangleRec({r.x + 5, r.y + 5, r.width, r.height}, {0, 0, 0, 160});
    DrawRectangleRec(r, color);
    DrawRectangleLinesEx(r, 2, RAYWHITE);
}
void text(const char* value, float x, float y, int size = 17, Color color = ink) { ui::draw_text(value, int(x), int(y), size, color); }
void button(int index, const char* label) {
    const auto r = button_rect(index);
    DrawRectangleRec(r, hit(r) ? selected : gray);
    DrawRectangleLinesEx(r, 1, RAYWHITE);
    text(label, r.x + 10, r.y + 6, 16, hit(r) ? RAYWHITE : ink);
}
}
void MenuBar::show(MenuCommand command) {
    popup_ = command; dropdown_ = -1; capturing_ = false; selected_ = binding_ = 0; status_.clear();
}
bool MenuBar::save_pending(const ControllerMapping& mapping, const std::filesystem::path& path) {
    if (!dirty_) return true;
    std::string error;
    if (!mapping.save(path, error)) { status_ = error; return false; }
    dirty_ = false; return true;
}
bool MenuBar::close_mapping(const ControllerMapping& mapping, const std::filesystem::path& path, bool save) {
    dirty_ |= save;
    if (!save_pending(mapping, path)) return false;
    popup_ = MenuCommand::None; capturing_ = false; return true;
}
MenuCommand MenuBar::update(ControllerMapping& mapping, const std::filesystem::path& path, bool mouse_enabled, const ControllerState& input) {
    interacted_ = false;
    devices_.clear();
    for (const auto& pad : input.pads) if (pad.connected) {
        if (!devices_.empty()) devices_ += " / ";
        devices_ += (pad.raw ? "USB joystick: " : "Gamepad: ") + pad.name;
    }
    if (devices_.empty()) devices_ = "No controller detected - plug in a gamepad, then choose Add binding.";
    if (!IsWindowFocused()) { if (capturing_) status_ = "Binding cancelled: window lost focus"; capturing_ = false; return MenuCommand::None; }
    const bool click = mouse_enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (popup_ != MenuCommand::None) {
        interacted_ = true;
        if (popup_ != MenuCommand::Controllers) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || (click && hit(button_rect(4)))) popup_ = MenuCommand::None;
            return MenuCommand::None;
        }
        auto& list = mapping.bindings[selected_];
        if (capturing_) {
            if (IsKeyPressed(KEY_ESCAPE)) { capturing_ = false; status_ = "Binding cancelled"; return MenuCommand::None; }
            Binding b{BindingKind::Key, GetKeyPressed()};
            bool found = b.code != 0;
            if (!found) if (const auto captured = capture_.poll(input)) { b = *captured; found = true; }
            if (found) {
                if (b.kind == BindingKind::Key && b.code == KEY_F10) {
                    status_ = "F10 is reserved for the menubar"; capturing_ = false; return MenuCommand::None;
                }
                const auto old_size = list.size();
                if (!mapping.add(Action(selected_), b)) status_ = "Cannot add binding (limit: 64 per action)";
                else { dirty_ |= list.size() != old_size; binding_ = int(std::find(list.begin(), list.end(), b) - list.begin()); status_ = (list.size() == old_size ? "Already mapped: " : "Added ") + binding_label(b); save_pending(mapping, path); }
                capturing_ = false;
            }
            return MenuCommand::None;
        }
        if (IsKeyPressed(KEY_ESCAPE)) { close_mapping(mapping, path); return MenuCommand::None; }
        const int direction = int(repeat(KEY_DOWN)) - int(repeat(KEY_UP));
        const int wheel = int(GetMouseWheelMove());
        if (direction || (wheel && GetMousePosition().x < panel_rect().x + 456)) {
            selected_ = std::clamp(selected_ + direction - wheel, 0, action_count - 1); binding_ = 0;
            return MenuCommand::None;
        } else if (wheel) binding_ = std::clamp(binding_ - wheel, 0, std::max(0, int(list.size()) - 1));
        binding_ = std::clamp(binding_ + int(repeat(KEY_RIGHT)) - int(repeat(KEY_LEFT)), 0, std::max(0, int(list.size()) - 1));
        const int first = first_row(selected_, action_count);
        const int binding_first = first_row(binding_, int(list.size()));
        if (click) for (int i = 0; i < visible_rows(); ++i) {
            if (first + i < action_count && hit(row_rect(i, false))) { selected_ = first + i; binding_ = 0; return MenuCommand::None; }
            if (binding_first + i < int(list.size()) && hit(row_rect(i, true))) binding_ = binding_first + i;
        }
        if (IsKeyPressed(KEY_INSERT) || (click && hit(button_rect(0)))) {
            capturing_ = true; capture_.start(input); status_ = "Press a key, gamepad button, or move a stick/trigger. Esc cancels.";
            while (GetKeyPressed()) {}
        } else if (IsKeyPressed(KEY_DELETE) || (click && hit(button_rect(1)))) {
            if (!list.empty()) { list.erase(list.begin() + std::clamp(binding_, 0, int(list.size()) - 1)); binding_ = std::max(0, binding_ - 1); dirty_ = true; status_ = "Removed binding"; save_pending(mapping, path); }
        } else if (click && hit(button_rect(2))) {
            mapping.defaults(); dirty_ = true; binding_ = 0; status_ = "Restored keyboard and gamepad defaults"; save_pending(mapping, path);
        } else if (click && hit(button_rect(3))) {
            mapping.deadzone = mapping.deadzone >= .49f ? .05f : mapping.deadzone + .05f; dirty_ = true; save_pending(mapping, path);
        } else if (IsKeyPressed(KEY_ENTER) || (click && hit(button_rect(4)))) close_mapping(mapping, path, true);
        return MenuCommand::None;
    }
    if (IsKeyPressed(KEY_F10)) {
        interacted_ = true;
        if (dropdown_ < 0) open(); else dropdown_ = -1;
        return MenuCommand::None;
    }
    if (click && GetMousePosition().y < menu_height) {
        interacted_ = true;
        for (int i = 0; i < 4; ++i) if (hit(title_rect(i))) { if (dropdown_ == i) dropdown_ = -1; else open(i); return MenuCommand::None; }
        dropdown_ = -1; return MenuCommand::None;
    }
    if (dropdown_ < 0) return MenuCommand::None;
    interacted_ = true;
    if (IsKeyPressed(KEY_ESCAPE)) { dropdown_ = -1; return MenuCommand::None; }
    const int horizontal = int(repeat(KEY_RIGHT)) - int(repeat(KEY_LEFT));
    if (horizontal) { dropdown_ = (dropdown_ + horizontal + 4) % 4; item_ = 0; }
    const int vertical = int(repeat(KEY_DOWN)) - int(repeat(KEY_UP));
    item_ = (item_ + vertical + counts[dropdown_]) % counts[dropdown_];
    if (mouse_enabled && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) {
        for (int i = 0; i < 4; ++i) if (hit(title_rect(i))) { if (dropdown_ != i) open(i); }
        for (int i = 0; i < counts[dropdown_]; ++i) if (hit(item_rect(dropdown_, i))) item_ = i;
    }
    if (IsKeyPressed(KEY_ENTER) || (click && hit(item_rect(dropdown_, item_)))) {
        const auto command = menus[dropdown_][item_].command;
        dropdown_ = -1;
        if (command == MenuCommand::Controllers || command == MenuCommand::Controls || command == MenuCommand::About) { show(command); return MenuCommand::None; }
        return command;
    }
    if (click && !hit(dropdown_rect(dropdown_))) dropdown_ = -1;
    return MenuCommand::None;
}
void MenuBar::draw(const ControllerMapping& mapping, const std::filesystem::path& path, bool captured) const {
    DrawRectangle(0, 0, GetScreenWidth(), menu_height, gray);
    DrawLine(0, menu_height - 1, GetScreenWidth(), menu_height - 1, WHITE);
    for (int i = 0; i < 4; ++i) {
        const auto r = title_rect(i);
        if (dropdown_ == i) DrawRectangleRec(r, blue);
        text(titles[i], r.x + 12, 6, 19, dropdown_ == i ? RAYWHITE : ink);
    }
    text(captured ? "F10: menu   Esc: map" : "F10 / arrows / Enter: menu", float(GetScreenWidth() - 365), 8, 15);
    if (dropdown_ >= 0) {
        box(dropdown_rect(dropdown_));
        for (int i = 0; i < counts[dropdown_]; ++i) {
            const auto r = item_rect(dropdown_, i);
            if (item_ == i) DrawRectangleRec(r, blue);
            text(menus[dropdown_][i].label, r.x + 10, r.y + 7, 17, item_ == i ? RAYWHITE : ink);
        }
    }
    if (popup_ == MenuCommand::None) return;
    DrawRectangle(0, menu_height, GetScreenWidth(), GetScreenHeight() - menu_height, {0, 0, 0, 150});
    const auto r = panel_rect(); box(r, blue);
    text(popup_ == MenuCommand::Controllers ? "SETTINGS / CONTROLLER MAPPING" : popup_ == MenuCommand::Controls ? "HELP / CONTROLS" : "HELP / ABOUT", r.x + 20, r.y + 16, 22, YELLOW);
    if (popup_ == MenuCommand::Controllers) {
        text("Up/Down: action. Left/Right: binding. Scroll either list. Any connected gamepad.", r.x + 20, r.y + 50, 15, RAYWHITE);
        BeginScissorMode(int(r.x + 20), int(r.y + 74), int(r.width - 40), 19);
        text(devices_.empty() ? "Controller detection runs during interactive play." : devices_.c_str(), r.x + 20, r.y + 74, 15, YELLOW);
        EndScissorMode();
        text("ACTION", r.x + 20, r.y + 98, 16, YELLOW);
        text(capturing_ ? "WAITING FOR INPUT..." : "BINDINGS (click to select)", r.x + 456, r.y + 98, 16, YELLOW);
        const auto& list = mapping.bindings[selected_];
        const int first = first_row(selected_, action_count), binding_first = first_row(binding_, int(list.size()));
        for (int i = 0; i < visible_rows(); ++i) {
            if (first + i < action_count) {
                auto row = row_rect(i, false);
                if (first + i == selected_) DrawRectangleRec(row, selected);
                text(action_labels[first + i], row.x + 6, row.y + 5, 16, RAYWHITE);
            }
            if (binding_first + i < int(list.size())) {
                auto row = row_rect(i, true);
                if (binding_first + i == binding_) DrawRectangleRec(row, selected);
                text(binding_label(list[binding_first + i]).c_str(), row.x + 6, row.y + 5, 16, RAYWHITE);
            }
        }
        if (list.empty()) text("Unbound - choose Add binding", r.x + 462, r.y + 125, 16, GRAY);
        text(status_.empty() ? "Insert: add   Delete: remove   Enter/Esc: save and close" : status_.c_str(), r.x + 20, r.y + r.height - 106, 15, YELLOW);
        button(0, capturing_ ? "Listening..." : "Add binding"); button(1, "Remove binding"); button(2, "Defaults");
        button(3, TextFormat("Deadzone: %d%%", int(std::round(mapping.deadzone * 100)))); button(4, "Save & close");
        const std::string filename = path.filename().string();
        text(("Saved beside the executable: " + filename + (dirty_ ? "  (unsaved changes)" : "")).c_str(), r.x + 20, r.y + r.height - 32, 14, RAYWHITE);
    } else if (popup_ == MenuCommand::Controls) {
        text("Keyboard and gamepad defaults. Edit bindings in Settings > Controller mapping.", r.x + 20, r.y + 58, 16, RAYWHITE);
        constexpr const char* lines[] = {
            "Car: W/S or left stick. Opposite direction brakes, then reverses. Space / B: handbrake.",
            "Car horn: H / right-stick click. E / Y: exit, or jump out while moving and tumble.",
            "On foot: WASD / left stick. Shift / L-stick: sprint. Space / A: jump. E / Y: enter/exit.",
            "Plane: W/S or left stick Y: pitch. A/D or left stick X: bank.",
            "Plane throttle: Shift/Ctrl or RT/LT. Rudder: arrows or LB/RB. Flaps: F / X.",
            "Camera: mouse / right stick. Zoom: wheel / D-pad down or left.",
            "Recover: R / D-pad up. Map: F2 / Back. Tuning: F3 / D-pad right.",
            "Esc: map / close. Start: pause/resume. F10: menubar; arrows and Enter navigate.",
            "Tuning: drag sliders; arrows adjust; Shift is fine adjustment.",
            "Tuning camera: right-drag outside panel; scroll to zoom. Menus pause physics."
        };
        for (int i = 0; i < int(std::size(lines)); ++i) text(lines[i], r.x + 20, r.y + 108 + i * 30, 16, RAYWHITE);
        button(4, "Close");
    } else {
        text("FORZA AMBAZON", r.x + 20, r.y + 90, 26, RAYWHITE);
        text("Explore Miami and the Florida Keys by car, on foot, or by plane.", r.x + 20, r.y + 145, 18, RAYWHITE);
        text("Built with C++17, raylib and Jolt Physics.", r.x + 20, r.y + 180, 18, RAYWHITE);
        button(4, "Close");
    }
}
} // namespace forza
