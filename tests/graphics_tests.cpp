#include "graphics_panel.hpp"
#include "ui_font.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    using namespace ambaretto;
    const auto folder = std::filesystem::temp_directory_path() / "ambaretto-graphics-check";
    const auto path = folder / "graphics-settings.ini";
    try {
        std::filesystem::create_directories(folder);
        GraphicsSettings settings;
        require(settings.preset() == GraphicsPreset::Balanced && settings.shadow_resolution() == 1024 && settings.msaa && settings.view_distance == 100, "default preset changed");
        settings.apply_preset(GraphicsPreset::Low);
        require(settings.preset() == GraphicsPreset::Low && settings.shadows == 0 && !settings.local_lights &&
            settings.view_distance == 1000 && !settings.vsync && !settings.msaa, "Low preset does not reduce GPU work");
        settings.apply_preset(GraphicsPreset::High);
        require(settings.preset() == GraphicsPreset::High && settings.shadow_resolution() == 2048 &&
            settings.shadow_distance == 180 && settings.view_distance == 4000 && settings.msaa, "High preset is incomplete");
        settings.brightness = 1.23f; settings.fps_limit = 144; settings.view_distance = 100; settings.msaa = false;
        require(settings.preset() == GraphicsPreset::Custom, "individual edits did not become Custom");
        std::string error;
        require(settings.save(path, error), error.c_str());
        GraphicsSettings loaded;
        require(loaded.load(path, error) && loaded == settings, "settings did not round-trip exactly");
        loaded.fps_limit = 0;
        require(loaded.save(path, error) && settings.load(path, error) && settings == loaded, "replacement did not persist unlimited frame cap");
        const auto before = loaded;
        for (const char* invalid : {"version=2\n", "version=1\nshadows=4\n", "version=1\nsoft_shadows=2\n",
            "version=1\nlocal_lights=true\n", "version=1\nbrightness=nan\n", "version=1\nbrightness=0.59\n",
            "version=1\nview_distance=6001\n", "version=1\nview_distance=99\n", "version=1\nshadow_distance=29\n", "version=1\nfps_limit=59\n",
            "version=1\nunknown=1\n", "version=1\nshadows=1\nshadows=2\n", "shadows=1\nversion=1\n",
            "version=1 extra\n", "version=1\nvsync=1 garbage\n", "version=1\nmsaa=2\n", "version=1\nmsaa=true\n",
            "version=1\nmsaa=0\nmsaa=1\n", "; empty file\n"}) {
            std::ofstream(path) << invalid;
            require(!loaded.load(path, error) && !error.empty() && loaded == before, "invalid file changed working settings");
        }
        std::ofstream(path) << "\xEF\xBB\xBF; Partial file retains Balanced defaults\nversion = 1\n brightness = 1.2 # comment\n";
        require(loaded.load(path, error) && loaded.brightness == 1.2f && loaded.shadows == 2 && loaded.msaa && loaded.view_distance == 100,
            "legacy partial settings or BOM/comments rejected");
        require(loaded.save(path, error), "cannot restore valid settings file");
        auto invalid = loaded; invalid.brightness = std::numeric_limits<float>::infinity();
        require(!invalid.save(path, error), "non-finite settings were saved");
        GraphicsSettings preserved;
        require(preserved.load(path, error) && preserved == loaded, "failed validation overwrote previous save");
        const auto locked = folder / "not-a-file";
        std::filesystem::create_directories(locked);
        std::ofstream(locked / "keep.txt") << "keep";
        require(!loaded.save(locked, error) && std::filesystem::exists(locked / "keep.txt"), "replacement failure deleted prior data");
        require(!loaded.save(folder / "missing" / "settings.ini", error), "missing destination directory reported success");

        GraphicsPanel panel;
        constexpr int width = 1024, height = 600;
        panel.open({});
        const auto rect = panel.bounds(width, height);
        require(rect.x >= 0 && rect.y >= 32 && rect.x + rect.width <= width && rect.y + rect.height <= height,
            "panel does not fit minimum window size");
        GraphicsPanelInput input;
        input.horizontal = -1;
        require(panel.update(width, height, input) == GraphicsPanelAction::Preview && panel.pending().preset() == GraphicsPreset::Low,
            "preset keyboard preview failed");
        require(panel.update(width, height, input) == GraphicsPanelAction::None, "unchanged preview repeats renderer allocation");
        input = {}; input.defaults = true;
        require(panel.update(width, height, input) == GraphicsPanelAction::Preview && !panel.dirty(), "Defaults did not reset pending only");
        input = {}; input.vertical = 1;
        panel.update(width, height, input);
        input = {}; input.horizontal = 1;
        require(panel.update(width, height, input) == GraphicsPanelAction::Preview && panel.pending().shadows == 3 && panel.dirty(),
            "custom shadow quality did not preview");
        input.horizontal = -1; panel.update(width, height, input);
        require(!panel.dirty(), "restoring previous values did not clear unsaved marker");
        input = {}; input.cancel = true;
        require(panel.update(width, height, input) == GraphicsPanelAction::Cancel && panel.visible(), "Cancel must wait for caller to restore and close");
        input = {}; const auto close = ui::window_close(rect);
        input.mouse = {close.x+close.width/2,close.y+close.height/2}; input.pressed = true;
        require(panel.update(width,height,input)==GraphicsPanelAction::Cancel,"title-bar close button did not cancel graphics preview");
        input = {}; input.apply = true;
        require(panel.update(width,height,input)==GraphicsPanelAction::Preview && panel.pending().shadows==3,"Enter on a setting saved instead of changing its value");
        panel.open({}); input = {}; input.tab = -1; panel.update(width,height,input);
        input = {}; input.apply = true;
        require(panel.update(width, height, input) == GraphicsPanelAction::Apply && panel.visible(), "Apply must remain open if saving fails");
        // Drag view distance beyond the right edge, release and cancel on focus loss.
        const float row_height = std::min(42.f,(rect.height - 252) / 9), left_width = rect.width - 36;
        const Vector2 slider{rect.x + 18 + left_width - 144, rect.y + 108 + 4 * row_height + 13};
        input = {}; input.mouse = slider; input.pressed = input.down = true;
        require(panel.update(width, height, input) == GraphicsPanelAction::Preview, "mouse slider did not preview");
        const float middle = panel.pending().view_distance;
        panel.cancel_drag();
        input.pressed = false; input.mouse.x = width + 200; panel.update(width, height, input);
        require(panel.pending().view_distance == middle, "opening another menu did not cancel slider drag");
        input.mouse = slider; input.pressed = true; panel.update(width, height, input);
        input.pressed = false; input.focused = false; panel.update(width, height, input);
        input.focused = true; input.mouse.x = width + 200; panel.update(width, height, input);
        require(panel.pending().view_distance == middle, "focus loss did not cancel slider drag");
        input = {}; input.mouse = slider; input.pressed = input.down = true; panel.update(width, height, input);
        input.pressed = false; input.mouse.x = width + 200; panel.update(width, height, input);
        require(panel.pending().view_distance == 6000, "slider did not clamp beyond window edge");
        input = {}; panel.update(width, height, input);
        input.mouse = {100, 100}; input.pressed = input.down = true; panel.update(width, height, input);
        require(panel.pending().view_distance == 6000, "release retained stale slider drag");
        input = {}; input.horizontal = -1; input.fine = true; panel.update(width, height, input);
        require(panel.pending().view_distance == 5975, "fine adjustment did not use small distance step");
        input = {}; input.mouse = {slider.x-60,slider.y}; input.pressed = input.down = true;
        panel.update(width,height,input);
        require(panel.pending().view_distance==100,"view-distance slider cannot select 100 m");
        input = {}; input.horizontal = -1; panel.update(width,height,input);
        require(panel.pending().view_distance==100,"view distance fell below the 100 m minimum");
        input = {}; input.mouse = {rect.x + rect.width - 80, rect.y + 108 + 8 * row_height + 12}; input.pressed = true;
        require(panel.update(width,height,input)==GraphicsPanelAction::Preview && !panel.pending().msaa,
            "MSAA row did not toggle the restart setting");
        input.pressed = false; input.scroll = 1;
        require(panel.update(width,height,input)==GraphicsPanelAction::None && !panel.pending().msaa,"scrolling accidentally toggled MSAA");
        GraphicsSettings low; low.apply_preset(GraphicsPreset::Low); panel.open(low);
        input = {}; input.vertical = 1; panel.update(width,height,input); panel.update(width,height,input);
        input = {}; input.apply = true;
        require(panel.update(width,height,input)==GraphicsPanelAction::Preview && panel.pending().local_lights && panel.pending().shadows==0,
            "keyboard did not skip disabled shadow settings");
        panel.open({}); input = {}; input.tab = -1; panel.update(width,height,input);
        input = {}; input.horizontal = -1; panel.update(width,height,input);
        input = {}; input.apply = true;
        require(panel.update(width,height,input)==GraphicsPanelAction::Cancel,"footer keyboard focus did not activate Cancel");
        panel.close();
        require(!panel.visible() && panel.update(width, height, input) == GraphicsPanelAction::None, "closed panel consumes input");
        std::filesystem::remove_all(folder);
        std::cout << "Graphics presets, atomic persistence, validation, reversible preview, keyboard and slider checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Graphics check failed: " << error.what() << '\n'; return 1; }
}
