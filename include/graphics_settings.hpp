#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace forza {
enum class GraphicsPreset { Low, Balanced, High, Custom };
inline constexpr std::array<int, 6> graphics_fps_limits{{30, 60, 120, 144, 240, 0}};
struct GraphicsSettings {
    int shadows = 2;
    bool soft_shadows = true;
    float shadow_distance = 90;
    bool local_lights = true;
    float view_distance = 2000;
    float brightness = 1;
    bool vsync = true;
    int fps_limit = 60;

    bool operator==(const GraphicsSettings& other) const;
    bool operator!=(const GraphicsSettings& other) const { return !(*this == other); }
    void apply_preset(GraphicsPreset preset);
    GraphicsPreset preset() const;
    const char* preset_name() const;
    int shadow_resolution() const { return shadows > 0 && shadows <= 3 ? 256 << shadows : 0; }
    bool valid(std::string& error) const;
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(const std::filesystem::path& path, std::string& error) const;
};
} // namespace forza
