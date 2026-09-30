#pragma once

#include "vehicle.hpp"
#include <cstdint>
#include <vector>

namespace forza {
enum class Surface { Grass, Sand, Rock, Road, Seabed };
struct TerrainTriangle { std::uint32_t a, b, c; Surface surface; };
struct Building { Vec3 center; Vec3 size; int style; };
struct Tree { Vec3 base; float height; bool palm; };

// Shared map data: rendering and Jolt use these exact terrain triangles.
class Environment {
public:
    static constexpr float extent = 400;
    static constexpr float spacing = 4;
    static constexpr int samples = 201;
    static constexpr float water_level = 0;
    Environment();
    float height(float x, float z) const;
    static float coast_radius(float x, float z);
    static bool road(float x, float z);
    Vec3 spawn() const { return Vec3(0, height(0, 105) + 0.56f, 105); }
    bool submerged(const Vec3& point) const;
    const std::vector<Vec3>& vertices() const { return vertices_; }
    const std::vector<TerrainTriangle>& triangles() const { return triangles_; }
    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Tree>& trees() const { return trees_; }
private:
    static float elevation(float x, float z);
    static Surface surface(float x, float z, float y);
    std::vector<Vec3> vertices_;
    std::vector<TerrainTriangle> triangles_;
    std::vector<Building> buildings_;
    std::vector<Tree> trees_;
};
} // namespace forza
