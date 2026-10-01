#pragma once
#include <array>
#include <cmath>

namespace forza {
// Shared by terrain, pavement, rendering, aircraft parking and recovery.
struct Airport {
    float center_x, runway_z, departure, grounds_half_width;
    const char* name;
    static constexpr float elevation = 4;
    static constexpr float runway_half_length = 168;
    static constexpr float runway_half_width = 12;
    float plane_z() const { return runway_z - departure * 140; }
    float apron_x() const { return center_x + 42; }
    float apron_z() const { return runway_z - departure * 100; }
    float yaw() const { return departure < 0 ? 0 : 3.14159265359f; }
    bool contains(float x, float z) const {
        return std::abs(x - center_x) <= grounds_half_width && std::abs(z - runway_z) <= 215;
    }
    bool pavement(float x, float z) const {
        const float along = departure * (z - runway_z);
        return (std::abs(x - center_x) <= runway_half_width && std::abs(z - runway_z) <= runway_half_length)
            || (std::abs(x - apron_x()) <= 15 && along >= -140 && along <= 156)
            || (x >= center_x && x <= apron_x() && std::abs(z - plane_z()) <= 5);
    }
    bool flight_path(float x, float z) const {
        const float along = departure * (z - runway_z);
        return std::abs(x - center_x) <= 30 && along >= -200 && along <= 600;
    }
};
inline constexpr std::array<Airport, 2> airports{{
    {-880, -420, -1, 115, "MIAMI AIRFIELD"},
    {-2280, 4380, 1, 75, "KEY WEST AIRFIELD"}
}};
} // namespace forza
