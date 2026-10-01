#pragma once
#include <cmath>

namespace forza {
// All airport geometry, road queries, terrain leveling, and aircraft spawn
// share this layout. The runway runs east/west (09/27).
struct Airport {
    static constexpr float elevation = 4;
    static constexpr float center_x = -1400;
    static constexpr float runway_z = -600;
    static constexpr float runway_half_length = 168;
    static constexpr float runway_half_width = 12;
    static constexpr float plane_x = center_x - 140;
    static constexpr float apron_z = runway_z - 34;
    static bool contains(float x, float z) {
        return std::abs(x - center_x) <= 205 && z >= runway_z - 110 && z <= runway_z + 65;
    }
    static bool pavement(float x, float z) {
        return (std::abs(x - center_x) <= runway_half_length && std::abs(z - runway_z) <= runway_half_width)
            || (x >= center_x - 156 && x <= center_x + 62 && z >= apron_z - 12 && z <= apron_z + 12)
            || (std::abs(x - plane_x) <= 5 && z >= apron_z && z <= runway_z)
            || (std::abs(x - center_x) <= 12 && z >= apron_z && z <= 400);
    }
};
} // namespace forza
