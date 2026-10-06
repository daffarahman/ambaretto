#pragma once
#include "controller_mapping.hpp"
#include <raylib.h>

namespace ambaretto {
constexpr int menu_height = 32;
enum class MenuCommand {
    None, Resume, Pause, Quit, Cities, Recover, CarDefaults, Map, Tuning, Graphics, AimMode, Controllers, Controls, About,
    SaveCity, StartTime, PlayCity, Undo, Redo, DeleteSelection,
    SelectTool, LandTool, RoadTool, BuildingTool, SpawnTool, VehicleTool, BulldozeTool, TreesTool, GroundTool, ElevationTool,
    TopView, Grid, RotateLeft, RotateRight, ZoomIn, ZoomOut, NewCity, EditCity, RenameCity, DeleteCity
};
enum class MenuMode { Game, Editor, Cities };
struct MenuState {
    bool undo = false, redo = false, play = true, selection = false, top = false, grid = true;
    int tool = -1;
};
struct MenuInput {
    Vector2 mouse{};
    bool mouse_enabled = true, click = false, moved = false, enter = false, escape = false, toggle = false, focused = true;
    int horizontal = 0, vertical = 0;
};
class MenuBar {
public:
    explicit MenuBar(MenuMode mode = MenuMode::Game) : mode_(mode) {}
    void open(int menu = 0) { dropdown_ = menu; item_ = 0; }
    void show(MenuCommand command);
    bool save_pending(const ControllerMapping& mapping, const std::filesystem::path& path);
    bool blocking() const { return dropdown_ >= 0 || popup_ != MenuCommand::None; }
    bool dialog_open() const { return popup_ != MenuCommand::None; }
    bool interacted() const { return interacted_; }
    MenuCommand update(const MenuState& state = {}, bool mouse_enabled = true);
    MenuCommand update(const MenuState& state, const MenuInput& input);
    void draw(const MenuState& state = {}) const;
    MenuCommand update(ControllerMapping& mapping, const std::filesystem::path& path, bool mouse_enabled, const ControllerState& input);
    void draw(const ControllerMapping& mapping, const std::filesystem::path& path) const;
private:
    MenuMode mode_;
    bool close_mapping(const ControllerMapping& mapping, const std::filesystem::path& path, bool save = false);
    int dropdown_ = -1, item_ = 0, selected_ = 0, binding_ = 0, group_ = 0;
    MenuCommand popup_ = MenuCommand::None;
    bool capturing_ = false, interacted_ = false, dirty_ = false;
    BindingCapture capture_;
    std::string status_, devices_;
};
} // namespace ambaretto
