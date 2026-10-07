#include "menu_bar.hpp"
#include "ui_font.hpp"
#include <algorithm>
#include <cmath>

namespace ambaretto {
namespace {
constexpr Color blue = ui::dos_blue, gray = ui::dos_white, ink = ui::dos_blue, selected = ui::dos_light_blue;
struct Item { const char* label; MenuCommand command; const char* shortcut = ""; };
constexpr Item files[] = {{"Resume", MenuCommand::Resume, "Esc"}, {"End Game / Main menu", MenuCommand::Cities}};
constexpr Item edits[] = {{"Recover vehicle", MenuCommand::Recover}, {"Restore car tuning", MenuCommand::CarDefaults}};
constexpr Item settings[] = {{"World map", MenuCommand::Map}, {"Car tuning", MenuCommand::Tuning}, {"Aim mode", MenuCommand::AimMode}};
constexpr Item helps[] = {{"Controls...", MenuCommand::Controls}, {"About...", MenuCommand::About}};
constexpr Item home_files[] = {{"Play...", MenuCommand::PlayCity}, {"Create / edit...", MenuCommand::Editors}, {"Quit", MenuCommand::Quit}};
constexpr Item home_settings[] = {{"Graphics...", MenuCommand::Graphics}, {"Controller mapping...", MenuCommand::Controllers}};
constexpr Item editor_files[] = {{"Save city", MenuCommand::SaveCity, "Ctrl+S"}, {"Start time...", MenuCommand::StartTime}, {"Play city", MenuCommand::PlayCity}, {"Cities...", MenuCommand::Cities, "Esc"}, {"Close editor", MenuCommand::Quit}, {"Refresh saved designs", MenuCommand::RefreshDesigns}};
constexpr Item editor_edits[] = {{"Undo", MenuCommand::Undo, "Ctrl+Z"}, {"Redo", MenuCommand::Redo, "Ctrl+Y"}, {"Delete selection", MenuCommand::DeleteSelection, "Del"}, {"Select / edit", MenuCommand::SelectTool, "1"}, {"Bulldoze", MenuCommand::BulldozeTool, "7"}};
constexpr Item tiles[] = {{"Island / expand",MenuCommand::LandTool,"2"}, {"Road: L-shaped",MenuCommand::RoadBend,"3"}, {"Road: diagonal",MenuCommand::RoadDiagonal},
    {"Ground texture...",MenuCommand::GroundTool,"9"},
    {"Raise ground +2 m",MenuCommand::RaiseGround,"0"}, {"Lower ground -2 m",MenuCommand::LowerGround}};
constexpr Item buildings[] = {{"Place selected building",MenuCommand::BuildingTool,"4"}, {"Building Creator...",MenuCommand::BuildingCreator}, {"Edit selected building...",MenuCommand::EditBuilding}, {"Rotate building +90",MenuCommand::RotateBuilding,"R"}, {"Choose saved building...",MenuCommand::SavedBuildings}};
constexpr Item objects[] = {{"Player spawn",MenuCommand::SpawnTool,"5"}, {"Choose car...",MenuCommand::PlaceCar,"6"}, {"Trainer plane",MenuCommand::PlaceTrainer},
    {"F-18",MenuCommand::PlaceF18}, {"Boeing 747",MenuCommand::PlaceBoeing}, {"Rotate vehicle +90",MenuCommand::RotateObject},
    {"Trees: sparse",MenuCommand::TreesSparse,"8"}, {"Trees: medium",MenuCommand::TreesMedium}, {"Trees: dense",MenuCommand::TreesDense},
    {"Smaller tree brush",MenuCommand::BrushSmaller}, {"Larger tree brush",MenuCommand::BrushLarger},
    {"Car editor...",MenuCommand::CarEditor}, {"Edit selected car...",MenuCommand::EditCar},
    {"Character creator...",MenuCommand::CharacterCreator}, {"Choose player character...",MenuCommand::ChoosePlayerCharacter}};
constexpr Item views[] = {{"Top view", MenuCommand::TopView, "V"}, {"Show grid", MenuCommand::Grid, "G"}, {"Rotate left", MenuCommand::RotateLeft, "Q"}, {"Rotate right", MenuCommand::RotateRight, "E"}, {"Zoom in", MenuCommand::ZoomIn}, {"Zoom out", MenuCommand::ZoomOut}};
constexpr Item city_files[] = {{"Create new city...", MenuCommand::NewCity, "Ins"}, {"Open / edit selected", MenuCommand::EditCity, "Enter"}, {"Duplicate selected", MenuCommand::DuplicateCity, "Ctrl+D"}, {"Back", MenuCommand::Quit, "Esc"}};
struct Menu { const char* title; const Item* items; int count; };
constexpr Menu home_menus[] = {{"File", home_files, int(std::size(home_files))}, {"Settings", home_settings, int(std::size(home_settings))}, {"Help", helps, int(std::size(helps))}};
constexpr Menu game_menus[] = {{"File", files, int(std::size(files))}, {"Edit", edits, 2}, {"Settings", settings, 3}, {"Help", helps, 2}};
constexpr Menu editor_menus[] = {{"File", editor_files, int(std::size(editor_files))}, {"Edit", editor_edits, int(std::size(editor_edits))}, {"Tiles", tiles, int(std::size(tiles))},
    {"Buildings", buildings, int(std::size(buildings))}, {"Objects", objects, int(std::size(objects))}, {"View", views, 6}, {"Help", helps, 2}};
constexpr Menu city_menus[] = {{"File", city_files, int(std::size(city_files))}};
constexpr Item creator_files[] = {{"New building",MenuCommand::NewBuilding}, {"Save building",MenuCommand::SaveBuilding,"Ctrl+S"}, {"Open Saved Building",MenuCommand::SavedBuildings},
    {"Save and close",MenuCommand::UseBuilding}, {"Close creator",MenuCommand::CloseCreator,"Esc"}};
constexpr Menu creator_menus[] = {{"File",creator_files,int(std::size(creator_files))}, {"Edit",editor_edits,2}};
constexpr Item car_files[] = {{"New car",MenuCommand::NewCar}, {"Save car",MenuCommand::SaveCar,"Ctrl+S"}, {"Open Saved Car",MenuCommand::SavedCars}, {"Save and close",MenuCommand::UseCar}, {"Close editor",MenuCommand::CloseCreator,"Esc"}};
constexpr Menu car_menus[] = {{"File",car_files,int(std::size(car_files))}};
constexpr Item character_files[] = {{"New character",MenuCommand::NewCharacter}, {"Save character",MenuCommand::SaveCharacter,"Ctrl+S"}, {"Open Saved Character",MenuCommand::SavedCharacters}, {"Save and close",MenuCommand::UseCharacter}, {"Close creator",MenuCommand::CloseCreator,"Esc"}};
constexpr Menu character_menus[] = {{"File",character_files,int(std::size(character_files))}};
const Menu* menus(MenuMode mode) { return mode==MenuMode::Main ? home_menus : mode == MenuMode::Editor ? editor_menus : mode == MenuMode::Cities ? city_menus : mode==MenuMode::Creator ? creator_menus : mode==MenuMode::CarCreator ? car_menus : mode==MenuMode::CharacterCreator ? character_menus : game_menus; }
int menu_count(MenuMode mode) { return mode==MenuMode::Main ? int(std::size(home_menus)) : mode == MenuMode::Editor ? int(std::size(editor_menus)) : mode==MenuMode::Creator ? int(std::size(creator_menus)) : mode==MenuMode::Cities || mode==MenuMode::CarCreator || mode==MenuMode::CharacterCreator ? 1 : 4; }
bool enabled(MenuCommand command, const MenuState& state) {
    switch (command) {
        case MenuCommand::Undo: return state.undo;
        case MenuCommand::Redo: return state.redo;
        case MenuCommand::PlayCity: return state.play;
        case MenuCommand::EditBuilding: return state.building_selection;
        case MenuCommand::EditCar: return state.car_selection;
        case MenuCommand::ChoosePlayerCharacter: return state.choose_character;
        case MenuCommand::RotateBuilding: return state.building_selection || state.tool==3;
        case MenuCommand::DeleteSelection: case MenuCommand::EditCity: case MenuCommand::RenameCity: case MenuCommand::DeleteCity: case MenuCommand::DuplicateCity: return state.selection;
        default: return true;
    }
}
bool checked(MenuCommand command, const MenuState& state) {
    if (command == MenuCommand::TopView) return state.top;
    if (command == MenuCommand::Grid) return state.grid;
    if (command==MenuCommand::RoadBend || command==MenuCommand::RoadDiagonal) return state.tool==2 && state.diagonal==(command==MenuCommand::RoadDiagonal);
    if (command==MenuCommand::RaiseGround || command==MenuCommand::LowerGround) return state.tool==9 && state.elevation==(command==MenuCommand::RaiseGround ? 1 : -1);
    if (command>=MenuCommand::PlaceCar && command<=MenuCommand::PlaceBoeing) return state.tool==5 && state.vehicle==int(command)-int(MenuCommand::PlaceCar);
    if (command>=MenuCommand::TreesSparse && command<=MenuCommand::TreesDense) return state.tool==7 && state.density==int(command)-int(MenuCommand::TreesSparse)+1;
    return state.tool >= 0 && int(command) == int(MenuCommand::SelectTool) + state.tool;
}
Rectangle title_rect(MenuMode mode, int menu) {
    float x = 0;
    for (int i = 0; i < menu; ++i) x += ui::measure_text(menus(mode)[i].title, 19) + 32;
    return {x, 0, float(ui::measure_text(menus(mode)[menu].title, 19) + 32), menu_height};
}
Rectangle dropdown_rect(MenuMode mode, int menu) { return {title_rect(mode, menu).x, menu_height, mode == MenuMode::Game ? 320.f : 360.f, float(menus(mode)[menu].count * 32 + 8)}; }
Rectangle item_rect(MenuMode mode, int menu, int item) { auto r = dropdown_rect(mode, menu); return {r.x + 4, r.y + 4 + item * 32, r.width - 8, 32}; }
Rectangle panel_rect() { return {float((GetScreenWidth() - 944) / 2), 64, 944, float(GetScreenHeight() - 96)}; }
int visible_rows() { return std::max(1, int((panel_rect().height - 264) / 28)); }
int first_row(int selection, int total) { return std::clamp(selection - visible_rows() / 2, 0, std::max(0, total - visible_rows())); }
Rectangle group_rect(int group) { auto r = panel_rect(); return {r.x + 20 + group * 226, r.y + 98, 216, 28}; }
Rectangle row_rect(int row, bool binding) { auto r = panel_rect(); return {r.x + (binding ? 456 : 20), r.y + 154 + row * 28, binding ? 468.0f : 416.0f, 28}; }
Rectangle button_rect(int index) { auto r = panel_rect(); return {r.x + 20 + index * 182, r.y + r.height - 76, 172, 30}; }
bool hit(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
bool repeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
void box(Rectangle r, Color color = gray) {
    DrawRectangleRec({r.x + 5, r.y + 5, r.width, r.height}, {0, 0, 85, 255});
    DrawRectangleRec(r, color);
    DrawRectangleLinesEx(r, 2, blue);
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
    popup_ = command; dropdown_ = -1; capturing_ = false; selected_ = binding_ = group_ = 0; status_.clear();
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
    if (devices_.empty()) devices_ = "No controller detected";
    if (!IsWindowFocused()) { if (capturing_) status_ = "Binding cancelled: window lost focus"; capturing_ = false; return MenuCommand::None; }
    const bool click = mouse_enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (popup_ != MenuCommand::None) {
        interacted_ = true;
        if (click && hit(ui::window_close(panel_rect()))) {
            if (popup_==MenuCommand::Controllers) close_mapping(mapping,path);
            else popup_ = MenuCommand::None;
            capturing_ = false; return MenuCommand::None;
        }
        if (popup_ != MenuCommand::Controllers) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || (click && hit(button_rect(4)))) popup_ = MenuCommand::None;
            return MenuCommand::None;
        }
        if (!capturing_) {
            int next_group = group_;
            if (IsKeyPressed(KEY_TAB)) next_group = (group_ + (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? 3 : 1)) % int(action_groups.size());
            if (click) for (int i = 0; i < int(action_groups.size()); ++i) if (hit(group_rect(i))) next_group = i;
            if (next_group != group_) {
                group_ = next_group; selected_ = int(action_groups[group_].first); binding_ = 0; status_.clear();
                return MenuCommand::None;
            }
        }
        const int group_first = int(action_groups[group_].first), group_end = int(action_groups[group_].end);
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
            selected_ = std::clamp(selected_ + direction - wheel, group_first, group_end - 1); binding_ = 0;
            return MenuCommand::None;
        } else if (wheel) binding_ = std::clamp(binding_ - wheel, 0, std::max(0, int(list.size()) - 1));
        binding_ = std::clamp(binding_ + int(repeat(KEY_RIGHT)) - int(repeat(KEY_LEFT)), 0, std::max(0, int(list.size()) - 1));
        const int first = group_first + first_row(selected_ - group_first, group_end - group_first);
        const int binding_first = first_row(binding_, int(list.size()));
        if (click) for (int i = 0; i < visible_rows(); ++i) {
            if (first + i < group_end && hit(row_rect(i, false))) { selected_ = first + i; binding_ = 0; return MenuCommand::None; }
            if (binding_first + i < int(list.size()) && hit(row_rect(i, true))) binding_ = binding_first + i;
        }
        if (IsKeyPressed(KEY_INSERT) || (click && hit(button_rect(0)))) {
            capturing_ = true; capture_.start(input); status_ = "Waiting for input...";
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
    const auto command = update({}, mouse_enabled);
    if (command == MenuCommand::Controllers || command == MenuCommand::Controls || command == MenuCommand::About) { show(command); return MenuCommand::None; }
    return command;
}
MenuCommand MenuBar::update(const MenuState& state, bool mouse_enabled) {
    MenuInput input;
    input.mouse_enabled = mouse_enabled;
    input.mouse = GetMousePosition(); input.click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    input.moved = GetMouseDelta().x != 0 || GetMouseDelta().y != 0;
    input.enter = IsKeyPressed(KEY_ENTER); input.escape = IsKeyPressed(KEY_ESCAPE); input.toggle = IsKeyPressed(KEY_F10);
    input.focused = IsWindowFocused();
    input.horizontal = int(repeat(KEY_RIGHT)) - int(repeat(KEY_LEFT));
    input.vertical = int(repeat(KEY_DOWN)) - int(repeat(KEY_UP));
    return update(state, input);
}
MenuCommand MenuBar::update(const MenuState& state, const MenuInput& input) {
    interacted_ = false;
    if (!input.focused) { interacted_ = blocking(); dropdown_ = -1; return MenuCommand::None; }
    const bool click = input.mouse_enabled && input.click;
    const auto over = [&](Rectangle r) { return CheckCollisionPointRec(input.mouse, r); };
    const int total = menu_count(mode_);
    if (input.toggle) {
        interacted_ = true;
        if (dropdown_ < 0) open(); else dropdown_ = -1;
        return MenuCommand::None;
    }
    if (click && input.mouse.y < menu_height) {
        interacted_ = true;
        for (int i = 0; i < total; ++i) if (over(title_rect(mode_, i))) { if (dropdown_ == i) dropdown_ = -1; else open(i); return MenuCommand::None; }
        dropdown_ = -1; return MenuCommand::None;
    }
    if (dropdown_ < 0) {
        if (mode_ == MenuMode::Game && input.escape) { interacted_ = true; return MenuCommand::Pause; }
        return MenuCommand::None;
    }
    interacted_ = true;
    if (input.escape) { dropdown_ = -1; return MenuCommand::None; }
    if (input.horizontal) { dropdown_ = (dropdown_ + input.horizontal + total) % total; item_ = 0; }
    const auto& menu = menus(mode_)[dropdown_];
    item_ = (item_ + input.vertical + menu.count) % menu.count;
    if (input.mouse_enabled && input.moved) {
        for (int i = 0; i < total; ++i) if (over(title_rect(mode_, i))) { if (dropdown_ != i) open(i); }
        for (int i = 0; i < menus(mode_)[dropdown_].count; ++i) if (over(item_rect(mode_, dropdown_, i))) item_ = i;
    }
    if (input.enter || (click && over(item_rect(mode_, dropdown_, item_)))) {
        const auto command = menus(mode_)[dropdown_].items[item_].command;
        if (!enabled(command, state)) return MenuCommand::None;
        dropdown_ = -1;
        return command;
    }
    if (click && !over(dropdown_rect(mode_, dropdown_))) dropdown_ = -1;
    return MenuCommand::None;
}
void MenuBar::draw(const MenuState& state) const {
    DrawRectangle(0, 0, GetScreenWidth(), menu_height, gray);
    DrawLine(0, menu_height - 1, GetScreenWidth(), menu_height - 1, blue);
    for (int i = 0; i < menu_count(mode_); ++i) {
        const auto r = title_rect(mode_, i);
        if (dropdown_ == i) DrawRectangleRec(r, blue);
        text(menus(mode_)[i].title, r.x + 16, 6, 19, dropdown_ == i ? ui::dos_yellow : ink);
    }
    if (dropdown_ >= 0) {
        box(dropdown_rect(mode_, dropdown_));
        const auto& menu = menus(mode_)[dropdown_];
        for (int i = 0; i < menu.count; ++i) {
            const auto r = item_rect(mode_, dropdown_, i);
            if (item_ == i) DrawRectangleRec(r, blue);
            const auto& item = menu.items[i];
            const Color color = !enabled(item.command, state) ? ui::dos_light_blue : item_ == i ? ui::dos_yellow : ink;
            if (checked(item.command, state)) text("*", r.x + 8, r.y + 7, 17, color);
            text(item.label, r.x + 28, r.y + 7, 17, color);
            text(item.shortcut, r.x + r.width - 12 - ui::measure_text(item.shortcut, 17), r.y + 7, 17, color);
        }
    }
}
void MenuBar::draw(const ControllerMapping& mapping, const std::filesystem::path&) const {
    draw();
    if (mode_ == MenuMode::Game && dropdown_ == 2) {
        const auto r = item_rect(mode_, dropdown_, 2);
        text(mapping.auto_lock ? "Aim mode: Auto lock" : "Aim mode: Free aim", r.x + 28, r.y + 7, 17, item_ == 2 ? ui::dos_yellow : ink);
    }
    if (popup_ == MenuCommand::None) return;
    DrawRectangle(0, menu_height, GetScreenWidth(), GetScreenHeight() - menu_height, {0, 0, 0, 150});
    const auto r = panel_rect();
    ui::draw_window(r, popup_ == MenuCommand::Controllers ? "SETTINGS / CONTROLLER MAPPING" : popup_ == MenuCommand::Controls ? "HELP / CONTROLS" : "HELP / ABOUT");
    ui::draw_window_close(r);
    if (popup_ == MenuCommand::Controllers) {
        BeginScissorMode(int(r.x + 20), int(r.y + 74), int(r.width - 40), 19);
        text(devices_.empty() ? "Controller detection runs during interactive play." : devices_.c_str(), r.x + 20, r.y + 74, 15, YELLOW);
        EndScissorMode();
        for (int i = 0; i < int(action_groups.size()); ++i) {
            const auto tab = group_rect(i);
            DrawRectangleRec(tab, i == group_ ? selected : gray);
            DrawRectangleLinesEx(tab, 1, RAYWHITE);
            text(action_groups[i].label, tab.x + 10, tab.y + 5, 16, i == group_ ? RAYWHITE : ink);
        }
        text("ACTION", r.x + 20, r.y + 132, 16, YELLOW);
        text(capturing_ ? "WAITING FOR INPUT..." : "BINDINGS", r.x + 456, r.y + 132, 16, YELLOW);
        const auto& list = mapping.bindings[selected_];
        const int group_first = int(action_groups[group_].first), group_end = int(action_groups[group_].end);
        const int first = group_first + first_row(selected_ - group_first, group_end - group_first), binding_first = first_row(binding_, int(list.size()));
        for (int i = 0; i < visible_rows(); ++i) {
            if (first + i < group_end) {
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
        if (list.empty()) text("Unbound", r.x + 462, r.y + 159, 16, GRAY);
        text(status_.c_str(), r.x + 20, r.y + r.height - 106, 15, YELLOW);
        button(0, capturing_ ? "Listening..." : "Add binding"); button(1, "Remove binding"); button(2, "Defaults");
        button(3, TextFormat("Deadzone: %d%%", int(std::round(mapping.deadzone * 100)))); button(4, "Save & close");
    } else if (popup_ == MenuCommand::Controls) {
        text("Keyboard and gamepad defaults. Edit bindings in the main menu's Settings.", r.x + 20, r.y + 58, 16, RAYWHITE);
        constexpr const char* lines[] = {
            "Car: W/S or left stick. Opposite direction brakes, then reverses. Space / B: handbrake.",
            "Car horn: H / right-stick click. E / Y: exit, or jump out while moving and tumble.",
            "On foot: WASD / left stick. Shift / L-stick: sprint. Space / A: jump. E / Y: enter.",
            "Cover: Q / B by a wall. Move along it slowly. Aim: peek. Sprint / jump: leave.",
            "Swim: WASD / left stick. Shift, Space / L-stick, A: faster. Surface only; no diving.",
            "Weapons: hold Tab / LB, select with mouse / right stick, release to equip.",
            "On foot: right mouse / LT aim, left mouse / RT fire, R / X reload. Dot marks aim.",
            "Auto lock: camera up/down adjusts aim; camera left/right switches or releases lock.",
            "Plane: W/S or left stick Y: pitch. A/D or left stick X: bank.",
            "Plane throttle: Shift/Ctrl or RT/LT. Rudder: arrows or LB/RB. Flaps: F / X.",
            "Camera: mouse / right stick. Zoom: wheel / D-pad down or left.",
            "Recover vehicle: R / D-pad up. Respawn on foot: F5 / D-pad up. Map: F2 / Back.",
            "Esc / Start: pause on map / resume. F10: menubar.",
            "Tuning: drag sliders; arrows adjust; Shift is fine adjustment.",
            "Tuning camera: right-drag outside panel; scroll to zoom. The map pauses physics."
        };
        const int spacing = std::min(30, int((r.height - 220) / std::size(lines)));
        for (int i = 0; i < int(std::size(lines)); ++i) text(lines[i], r.x + 20, r.y + 108 + i * spacing, 16, RAYWHITE);
        button(4, "Close");
    } else {
        text("AMBARETTO", r.x + 20, r.y + 90, 26, RAYWHITE);
        text("Build an island city, then explore by car, on foot, or by plane.", r.x + 20, r.y + 145, 18, RAYWHITE);
        text("Built with C++17, raylib and Jolt Physics.", r.x + 20, r.y + 180, 18, RAYWHITE);
        button(4, "Close");
    }
}
} // namespace ambaretto
