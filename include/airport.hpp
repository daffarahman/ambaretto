#pragma once
#include <array>
#include <cmath>

namespace forza {
struct AirportPoint { float x, z; };
// Shared by terrain, pavement, rendering, aircraft parking and recovery.
struct Airport {
    float center_x, runway_z, departure, grounds_half_width;
    const char* name;
    bool international = false;
    static constexpr float elevation = 4;
    static constexpr float runway_half_length = 168;
    static constexpr float runway_half_width = 12;
    float grounds_half_length() const { return international ? 575 : 385; }
    float runway_length() const { return international ? 1100 : 720; }
    float runway_width() const { return international ? 44 : runway_half_width * 2; }
    int runway_count() const { return 1; }
    AirportPoint direction(int = 0) const { return {0, departure}; }
    AirportPoint point(float along, float across = 0, int runway = 0) const {
        const auto d = direction(runway);
        return {center_x + d.x * along - d.z * across, runway_z + d.z * along + d.x * across};
    }
    float plane_along() const { return -runway_length() / 2 + 40; }
    float plane_x() const { return point(plane_along()).x; }
    float plane_z() const { return point(plane_along()).z; }
    float apron_x() const { return center_x + 42; }
    float apron_z() const { return runway_z - departure * 100; }
    float gate_x() const { return center_x + (international ? grounds_half_width + 35 : -grounds_half_width); }
    float gate_z() const { return runway_z; }
    float gate_width() const { return 7; }
    std::array<AirportPoint, 2> access_points() const {
        return {{{gate_x(), gate_z()}, {apron_x(), apron_z()}}};
    }
    float yaw(int runway = 0) const { const auto d = direction(runway); return std::atan2(-d.x, -d.z); }
    bool contains(float x, float z) const {
        return (std::abs(x - center_x) <= grounds_half_width && std::abs(z - runway_z) <= grounds_half_length())
            || (x >= center_x - 115 && x <= center_x - 38 && std::abs(z - runway_z + 30) <= 190);
    }
    bool pavement(float x, float z) const {
        // Aircraft stands on the west apron leave the runway and public access clear.
        if (x >= center_x - 115 && x <= center_x - 38 && std::abs(z - runway_z + 30) <= 190) return true;
        if (x >= center_x - 78 && x <= center_x && std::abs(z - runway_z + 200) <= 8) return true;
        if (international) {
            if (std::abs(x - apron_x()) <= 15 && std::abs(z - apron_z()) <= 60) return true;
            const auto near_segment = [&](AirportPoint a, AirportPoint b, float width) {
                const float dx = b.x - a.x, dz = b.z - a.z;
                const float t = std::fmax(0.0f, std::fmin(1.0f, ((x - a.x) * dx + (z - a.z) * dz) / (dx * dx + dz * dz)));
                return std::hypot(x - a.x - dx * t, z - a.z - dz * t) <= width / 2;
            };
            const auto access = access_points();
            for (int i = 1; i < int(access.size()); ++i) if (near_segment(access[i - 1], access[i], gate_width())) return true;
            for (int i = 0; i < runway_count(); ++i) {
                const auto d = direction(i);
                const float dx = x - center_x, dz = z - runway_z;
                if (std::abs(dx * d.x + dz * d.z) <= runway_length() / 2
                    && std::abs(-dx * d.z + dz * d.x) <= runway_width() / 2) return true;
                if (near_segment({apron_x(), apron_z()}, {apron_x(), plane_z()}, 12)
                    || near_segment({apron_x(), plane_z()}, point(plane_along(), 0, i), 12)) return true;
            }
            return false;
        }
        const float along = departure * (z - runway_z);
        return (std::abs(x - center_x) <= runway_half_width && std::abs(z - runway_z) <= runway_length() / 2)
            || (std::abs(x - apron_x()) <= 15 && along >= plane_along() && along <= runway_length() / 2 - 12)
            || (x >= center_x && x <= apron_x() && std::abs(z - plane_z()) <= 5);
    }
    bool flight_path(float x, float z) const {
        if (international) {
            for (int i = 0; i < runway_count(); ++i) {
                const auto d = direction(i);
                const float dx = x - center_x, dz = z - runway_z;
                const float along = dx * d.x + dz * d.z;
                if (std::abs(-dx * d.z + dz * d.x) <= 45 && along >= -runway_length() / 2 - 80
                    && along <= runway_length() / 2 + 600) return true;
            }
            return false;
        }
        const float along = departure * (z - runway_z);
        return std::abs(x - center_x) <= 38 && along >= -runway_length() / 2 - 80 && along <= runway_length() / 2 + 600;
    }
};
inline constexpr std::array<Airport, 2> airports{{
    {-1040, 0, -1, 75, "MIAMI INTERNATIONAL AIRPORT", true},
    {-2160, 4380, 1, 75, "KEY WEST AIRFIELD"}
}};
} // namespace forza
