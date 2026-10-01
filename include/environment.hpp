#pragma once

#include "vehicle.hpp"
#include <cstdint>
#include <vector>

namespace forza {
enum class Surface { Grass, Sand, Rock, Road, Seabed };
struct TerrainTriangle { std::uint32_t a, b, c; Surface surface; bool deck = false; };
enum class BuildingKind { Tower, Mall, Hotel, Cafe, Club, House, GasStation, Warehouse, Terminal, Hangar, ControlTower, Apartment, Shop, Office };
struct Building {
    Vec3 center, size;
    int style;
    BuildingKind kind = BuildingKind::Tower;
    bool face_east = false;
    bool face_positive = true;
    Vec3 solid_size() const { return kind == BuildingKind::GasStation ? Vec3(size.GetX() * .42f, size.GetY(), size.GetZ()) : size; }
    Vec3 solid_center() const { return kind == BuildingKind::GasStation ? center - Vec3(size.GetX() * .29f, 0, 0) : center; }
};
struct Island { Vec3 center; float radius_x, radius_z; const char* name; };
struct Road { Vec3 a, b; float width; const char* name; int bridge = -1; };
// Street corners are shared by pavement generation and NPC routes.
struct StreetLoop { std::vector<Vec3> corners; float width; const char* name; };
struct Bridge {
    Vec3 a, b;
    float width, clearance;
    const char* name;
    Vec3 point(float t) const;
};
struct Barrier { Vec3 center, size; float yaw; };
struct Port { Vec3 center; bool east; const char* name; };
struct Tree {
    Vec3 base;
    float height;
    float yaw;
    // Native tree1.glb dimensions, shared by rendering and trunk collision.
    static constexpr float model_height = 3.9276662f;
    static constexpr float model_trunk_height = 2.5137062f;
    static constexpr float model_trunk_radius = .1256853f;
    float scale() const { return height / model_height; }
};

// Shared map data: rendering and Jolt use these exact terrain triangles.
class Environment {
public:
    static constexpr float extent = 5120;
    static constexpr float spacing = 20;
    static constexpr int samples = 513;
    static constexpr float road_level = 3.2f;
    static constexpr float water_level = 0;
    Environment();
    float height(float x, float z) const;
    static float coast_radius(float x, float z);
    static bool road(float x, float z);
    static const std::vector<Island>& islands();
    static const std::vector<Road>& roads();
    static const std::vector<StreetLoop>& street_loops();
    static const std::vector<Bridge>& bridges();
    static const char* district(float x, float z);
    Vec3 spawn() const { return Vec3(0, height(0, 105) + 0.56f, 105); }
    bool submerged(const Vec3& point) const;
    const std::vector<Vec3>& vertices() const { return vertices_; }
    const std::vector<TerrainTriangle>& triangles() const { return triangles_; }
    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Tree>& trees() const { return trees_; }
    const std::vector<Barrier>& barriers() const { return barriers_; }
    const std::vector<Port>& ports() const { return ports_; }
    float terrain_height(float x, float z) const;
private:
    static float elevation(float x, float z);
    static Surface surface(float x, float z, float y);
    std::vector<Vec3> vertices_;
    std::vector<TerrainTriangle> triangles_;
    std::vector<Building> buildings_;
    std::vector<Tree> trees_;
    std::vector<Barrier> barriers_;
    std::vector<Port> ports_;
};
} // namespace forza
