#pragma once
#include <filesystem>
#include <string>

namespace ambaretto {
struct City;
class ControllerMapping;
struct GraphicsSettings;
// Each app runs in its own native window/process; only saved files are shared.
enum class DesktopApp { Cities, Buildings, Cars, Characters, Settings };
bool launch_app(DesktopApp app, std::string& error, const std::filesystem::path& draft = {});
void set_app_session(const std::filesystem::path& session);
bool app_should_close();
void cancel_app_close();
bool close_apps();
bool request_play(const City& city, std::string& error);
bool desktop_menu(City& city, const std::filesystem::path& cities, const std::string& screenshot = {});
void design_app(DesktopApp app, const std::filesystem::path& root, const std::string& screenshot = {},
                bool create_new = false, const std::filesystem::path& draft = {});
void settings_app(ControllerMapping& controls, GraphicsSettings& graphics);
}
