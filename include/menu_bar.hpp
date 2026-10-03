#pragma once
#include "controller_mapping.hpp"
#include <raylib.h>

namespace forza {
constexpr int menu_height = 32;
enum class MenuCommand { None, Resume, Pause, Quit, Recover, CarDefaults, Map, Tuning, Graphics, Controllers, Controls, About };
class MenuBar {
public:
    void open(int menu = 0) { dropdown_ = menu; item_ = 0; }
    void show(MenuCommand command);
    bool save_pending(const ControllerMapping& mapping, const std::filesystem::path& path);
    bool blocking() const { return dropdown_ >= 0 || popup_ != MenuCommand::None; }
    bool interacted() const { return interacted_; }
    MenuCommand update(ControllerMapping& mapping, const std::filesystem::path& path, bool mouse_enabled, const ControllerState& input);
    void draw(const ControllerMapping& mapping, const std::filesystem::path& path) const;
private:
    bool close_mapping(const ControllerMapping& mapping, const std::filesystem::path& path, bool save = false);
    int dropdown_ = -1, item_ = 0, selected_ = 0, binding_ = 0, group_ = 0;
    MenuCommand popup_ = MenuCommand::None;
    bool capturing_ = false, interacted_ = false, dirty_ = false;
    BindingCapture capture_;
    std::string status_, devices_;
};
} // namespace forza
