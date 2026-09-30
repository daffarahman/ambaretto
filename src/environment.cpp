#include "environment.hpp"
#include "airport.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace forza {
namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float inland(float x, float z) {
    const auto hill = [=](float cx, float cz, float width, float top) {
        const float dx = x - cx, dz = z - cz;
        return top * std::exp(-(dx * dx + dz * dz) / (width * width));
    };
    return 3.2f + hill(-95, -85, 105, 19) + hill(125, -115, 85, 10)
        + hill(-210, 80, 62, 24) + hill(90, 185, 55, 13);
}
}

float Environment::coast_radius(float x, float z) {
    const float angle = std::atan2(z / 280, x / 320);
    const float outline = 1 + 0.045f * std::sin(3 * angle + 0.4f) + 0.025f * std::cos(5 * angle);
    return std::hypot(x / 320, z / 280) / outline;
}

bool Environment::road(float x, float z) {
    const float r = coast_radius(x, z);
    const float dx = std::abs(x - std::round(x / 60) * 60);
    const float dz = std::abs(z - std::round(z / 60) * 60);
    const bool grid = std::abs(x) <= 146 && std::abs(z) <= 146 && (dx <= 6 || dz <= 6);
    const bool connectors = std::min(std::abs(x), std::abs(z)) <= 6 && r <= 0.74f;
    return grid || connectors || std::abs(r - 0.72f) < 0.024f || Airport::pavement(x, z);
}

float Environment::elevation(float x, float z) {
    const float r = coast_radius(x, z);
    float land = inland(x, z);
    // Level building plots, blending back into the climbing street grid.
    if (std::abs(x) < 146 && std::abs(z) < 146) {
        const float cx = std::floor(x / 60) * 60 + 30;
        const float cz = std::floor(z / 60) * 60 + 30;
        const float blend = 1 - smooth(17, 25, std::max(std::abs(x - cx), std::abs(z - cz)));
        land += (inland(cx, cz) - land) * blend;
    }
    land *= 1 - smooth(0.60f, 0.84f, r);
    land += 1.25f;
    // A long gentle beach slope continues below sea level to a seabed.
    const float coast = land * (1 - smooth(0.83f, 1.10f, r)) - 8 * smooth(0.92f, 1.15f, r);
    // A level airfield with a gradual grass/beach shoulder. The access road
    // blends into the existing avenue rather than introducing a terrain step.
    const float airport = (1 - smooth(182, 210, std::abs(x)))
        * smooth(164, 196, z) * (1 - smooth(316, 354, z));
    return coast + (Airport::elevation - coast) * airport;
}

Surface Environment::surface(float x, float z, float y) {
    if (y < -0.1f) return Surface::Seabed;
    if (road(x, z)) return Surface::Road;
    if (Airport::contains(x, z)) return Surface::Grass;
    if (coast_radius(x, z) > 0.79f) return Surface::Sand;
    if (y > 24) return Surface::Rock;
    return Surface::Grass;
}

Environment::Environment() {
    vertices_.reserve(samples * samples);
    for (int z = 0; z < samples; ++z)
        for (int x = 0; x < samples; ++x) {
            const float px = -extent + x * spacing, pz = -extent + z * spacing;
            vertices_.emplace_back(px, elevation(px, pz), pz);
        }
    triangles_.reserve((samples - 1) * (samples - 1) * 2);
    const auto add_triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const Vec3 center = (vertices_[a] + vertices_[b] + vertices_[c]) / 3;
        triangles_.push_back({a, b, c, surface(center.GetX(), center.GetZ(), center.GetY())});
    };
    for (int z = 0; z < samples - 1; ++z)
        for (int x = 0; x < samples - 1; ++x) {
            const std::uint32_t a = z * samples + x, b = a + 1, c = a + samples, d = c + 1;
            add_triangle(a, c, b);
            add_triangle(b, c, d);
        }
    std::mt19937 random(4317);
    for (int z = -2; z < 2; ++z)
        for (int x = -2; x < 2; ++x) {
            const float cx = x * 60.0f + 30, cz = z * 60.0f + 30;
            // Two blocks are public parks; the rest hold four buildings.
            if ((x == 0 && z == 0) || (x == -2 && z == 1)) continue;
            for (float ox : {-10.5f, 10.5f}) for (float oz : {-10.5f, 10.5f}) {
                const float h = 9 + random() % 22 + (std::abs(cx) < 80 && cz < 0 ? 12 : 0);
                const Vec3 size(16, h, 16);
                const float px = cx + ox, pz = cz + oz;
                buildings_.push_back({Vec3(px, height(px, pz) + h / 2, pz), size, int(random() % 5)});
            }
        }
    // Terminal, hangar and control tower use the same solid building shapes
    // as the city. Keep them north of the apron and clear of the coastal road.
    for (const auto& building : {Building{Vec3(36, 0, 216), Vec3(42, 8, 18), 5},
            Building{Vec3(-94, 0, 216), Vec3(36, 10, 24), 6},
            Building{Vec3(91, 0, 220), Vec3(9, 22, 9), 7}}) {
        auto b = building;
        b.center.SetY(height(b.center.GetX(), b.center.GetZ()) + b.size.GetY() / 2);
        buildings_.push_back(b);
    }
    for (int i = 0; i < 360; ++i) {
        const float x = float(int(random() % 620) - 310), z = float(int(random() % 540) - 270);
        const float r = coast_radius(x, z), y = height(x, z);
        if (road(x, z) || (std::abs(x) < 205 && z > 182) || y < 0.65f || r > 0.94f) continue;
        if (std::abs(x) < 143 && std::abs(z) < 143) {
            const float cx = std::floor(x / 60) * 60 + 30, cz = std::floor(z / 60) * 60 + 30;
            if (!((cx == 30 && cz == 30) || (cx == -90 && cz == 90))) continue;
        }
        const float yaw = std::remainder(x * 13 + z * 17, 360.0f) * .017453293f;
        trees_.push_back({Vec3(x, y, z), 4.5f + (random() % 40) * 0.1f, yaw});
    }
}

float Environment::height(float x, float z) const {
    const float gx = std::clamp((x + extent) / spacing, 0.0f, float(samples - 1));
    const float gz = std::clamp((z + extent) / spacing, 0.0f, float(samples - 1));
    const int ix = std::min(int(gx), samples - 2), iz = std::min(int(gz), samples - 2);
    const float u = gx - ix, v = gz - iz;
    const int a = iz * samples + ix;
    const float ha = vertices_[a].GetY(), hb = vertices_[a + 1].GetY();
    const float hc = vertices_[a + samples].GetY(), hd = vertices_[a + samples + 1].GetY();
    // Barycentric interpolation follows the same diagonal as the mesh.
    return u + v <= 1 ? ha + u * (hb - ha) + v * (hc - ha)
        : hd + (1 - u) * (hc - hd) + (1 - v) * (hb - hd);
}

bool Environment::submerged(const Vec3& point) const {
    return point.GetY() < water_level - 0.6f || std::abs(point.GetX()) > extent - 10
        || std::abs(point.GetZ()) > extent - 10;
}
} // namespace forza
