#pragma once
#include <cmath>

namespace forza {
// All airport geometry, road queries, terrain leveling, and aircraft spawn
// share this layout. The runway runs east/west (09/27).
struct Airport {
    static constexpr float elevation = 4;
    static constexpr float runway_z = 280;
    static constexpr float runway_half_length = 168;
    static constexpr float runway_half_width = 12;
    static constexpr float plane_x = -140;
    static constexpr float apron_z = 246;
    static bool contains(float x, float z) {
        return std::abs(x) <= 182 && z >= 196 && z <= 316;
    }
    static bool pavement(float x, float z) {
        return (std::abs(x) <= runway_half_length && std::abs(z - runway_z) <= runway_half_width)
            || (x >= -156 && x <= 62 && z >= 234 && z <= 258)
            || (std::abs(x - plane_x) <= 5 && z >= 246 && z <= runway_z)
            || (std::abs(x) <= 6 && z >= 146 && z <= apron_z);
    }
};
} // namespace forza
