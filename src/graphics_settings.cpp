#include "graphics_settings.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

namespace forza {
bool GraphicsSettings::operator==(const GraphicsSettings& b) const {
    return shadows == b.shadows && soft_shadows == b.soft_shadows && shadow_distance == b.shadow_distance &&
        local_lights == b.local_lights && view_distance == b.view_distance && brightness == b.brightness &&
        vsync == b.vsync && fps_limit == b.fps_limit;
}
void GraphicsSettings::apply_preset(GraphicsPreset value) {
    if (value == GraphicsPreset::Custom) return;
    *this = GraphicsSettings{};
    if (value == GraphicsPreset::Low) {
        shadows = 0; local_lights = false; view_distance = 1000; vsync = false;
    } else if (value == GraphicsPreset::High) {
        shadows = 3; shadow_distance = 180; view_distance = 4000;
    }
}
GraphicsPreset GraphicsSettings::preset() const {
    for (const auto value : {GraphicsPreset::Low, GraphicsPreset::Balanced, GraphicsPreset::High}) {
        GraphicsSettings candidate; candidate.apply_preset(value);
        if (*this == candidate) return value;
    }
    return GraphicsPreset::Custom;
}
const char* GraphicsSettings::preset_name() const {
    constexpr const char* names[] = {"Low", "Balanced", "High", "Custom"};
    return names[int(preset())];
}
bool GraphicsSettings::valid(std::string& error) const {
    error.clear();
    if (shadows < 0 || shadows > 3) error = "Shadow quality must be Off, Low, Medium or High";
    else if (!std::isfinite(shadow_distance) || shadow_distance < 30 || shadow_distance > 180)
        error = "Shadow distance must be 30-180 m";
    else if (!std::isfinite(view_distance) || view_distance < 500 || view_distance > 6000)
        error = "View distance must be 500-6000 m";
    else if (!std::isfinite(brightness) || brightness < .6f || brightness > 1.5f)
        error = "Brightness must be 60-150%";
    else if (std::find(graphics_fps_limits.begin(), graphics_fps_limits.end(), fps_limit) == graphics_fps_limits.end())
        error = "Frame limit must be 30, 60, 120, 144, 240 or 0 (unlimited)";
    return error.empty();
}
bool GraphicsSettings::load(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::ifstream file(path);
    if (!file) { error = "Cannot open graphics settings"; return false; }
    constexpr std::array<const char*, 9> keys{{"version", "shadows", "soft_shadows", "shadow_distance",
        "local_lights", "view_distance", "brightness", "vsync", "fps_limit"}};
    GraphicsSettings candidate;
    std::array<bool, 9> seen{};
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line_number > 128 || line.size() > 512) { error = "Graphics settings file is too large"; return false; }
        if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = line.substr(0, line.find_first_of(";#"));
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const auto equals = line.find('=');
        bool ok = equals != std::string::npos;
        const auto trim = [](std::string text) {
            const auto first = text.find_first_not_of(" \t\r");
            return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
        };
        const auto key = ok ? trim(line.substr(0, equals)) : std::string{};
        int id = -1;
        for (int i = 0; i < int(keys.size()); ++i) if (key == keys[i]) id = i;
        ok = ok && id >= 0 && !seen[id] && (id == 0 || seen[0]);
        if (ok) {
            std::istringstream row(line.substr(equals + 1)); row.imbue(std::locale::classic());
            int integer = -1;
            switch (id) {
                case 0: ok = bool(row >> integer) && integer == 1; break;
                case 1: ok = bool(row >> candidate.shadows); break;
                case 2: case 4: case 7:
                    ok = bool(row >> integer) && (integer == 0 || integer == 1);
                    if (id == 2) candidate.soft_shadows = integer == 1;
                    if (id == 4) candidate.local_lights = integer == 1;
                    if (id == 7) candidate.vsync = integer == 1;
                    break;
                case 3: ok = bool(row >> candidate.shadow_distance); break;
                case 5: ok = bool(row >> candidate.view_distance); break;
                case 6: ok = bool(row >> candidate.brightness); break;
                case 8: ok = bool(row >> candidate.fps_limit); break;
            }
            std::string extra;
            if (row >> extra) ok = false;
            if (ok) seen[id] = true;
        }
        if (!ok) { error = "Invalid graphics setting at line " + std::to_string(line_number); return false; }
    }
    if (!seen[0] || file.bad()) { error = "Incomplete graphics settings file"; return false; }
    if (!candidate.valid(error)) return false;
    *this = candidate;
    return true;
}
bool GraphicsSettings::save(const std::filesystem::path& path, std::string& error) const {
    if (!valid(error)) return false;
    auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) { error = "Cannot write graphics settings; previous file kept"; return false; }
    file.imbue(std::locale::classic());
    file << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "; Forza Ambazon graphics. Shadow quality: 0=Off, 1=512, 2=1024, 3=2048.\n"
        << "; Distances are metres; brightness is 0.6-1.5; frame limit 0 is unlimited.\n"
        << "version=1\nshadows=" << shadows << "\nsoft_shadows=" << int(soft_shadows)
        << "\nshadow_distance=" << shadow_distance << "\nlocal_lights=" << int(local_lights)
        << "\nview_distance=" << view_distance << "\nbrightness=" << brightness
        << "\nvsync=" << int(vsync) << "\nfps_limit=" << fps_limit << '\n';
    file.close();
    if (!file) { error = "Cannot finish saving graphics settings; previous file kept"; return false; }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    const bool replaced = !ec;
#endif
    if (!replaced) { error = "Cannot replace graphics settings; previous file kept"; return false; }
    return true;
}
} // namespace forza
