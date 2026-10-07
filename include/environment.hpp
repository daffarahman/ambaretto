#pragma once

#include "vehicle.hpp"
#include "city.hpp"
#include <cstdint>
#include <vector>

namespace ambaretto {
enum class Surface { Grass, Sand, Rock, Road, Seabed, Soil };
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
struct StreetLoop {
    std::vector<Vec3> corners; float width; const char* name;
    bool closed = true;
    float cruise_speed = 8;
    int traffic_count = 3;
    float lane_offset = 2.2f;
};
struct Bridge {
    Vec3 a, b;
    float width, clearance;
    const char* name;
    Vec3 start_side = Vec3::sZero(), end_side = Vec3::sZero();
    bool open_a = true, open_b = true;
    Vec3 control_a = Vec3::sZero(), control_b = Vec3::sZero();
    bool has_curve() const { return control_a.LengthSq() > 0; }
    Vec3 point(float t) const;
    Vec3 side(float t) const;
};
struct Barrier {
    Vec3 center, size;
    float yaw, pitch = 0;
    Quat rotation() const { return Quat::sRotation(Vec3::sAxisY(), yaw) * Quat::sRotation(Vec3::sAxisX(), pitch); }
};
struct Port {
    Vec3 center; bool east; const char* name;
    float length = 220, width = 8;
    Vec3 solid_size() const { return east ? Vec3(length + 2, 1.2f, width) : Vec3(width, 1.2f, length + 2); }
    Vec3 solid_center() const { return center + (east ? Vec3::sAxisX() : Vec3::sAxisZ()) * (length / 2 - 1) - Vec3(0, .6f, 0); }
};

// Shared map data: rendering and Jolt use these exact terrain triangles.
class Environment {
public:
    static constexpr float extent = 5120;
    static constexpr float spacing = 20;
    static constexpr float highway_level = 10;
    static constexpr float highway_width = 24;
    static constexpr int samples = 513;
    static constexpr float road_level = 3.2f;
    static constexpr float water_level = 0;
    Environment();
    explicit Environment(const City& city);
    const City* city() const { return city_.get(); }
    const std::vector<Road>& road_segments() const { return city_ ? city_roads_ : roads(); }
    const std::vector<StreetLoop>& street_routes() const { return city_ ? city_streets_ : street_loops(); }
    const std::vector<StreetLoop>& highway_routes() const { return city_ ? city_streets_ : highways(); }
    const std::vector<Island>& land_islands() const { return city_ ? city_islands_ : islands(); }
    const std::vector<Bridge>& bridge_segments() const { return city_ ? city_bridges_ : bridges(); }
    float height(float x, float z) const;
    float ground_height(float x, float z) const;
    float road_height(const Road& road, float x, float z) const;
    float surface_height(Vec3 reference) const;
    static float coast_radius(float x, float z);
    static bool road(float x, float z);
    static const std::vector<Island>& islands();
    static const std::vector<Road>& roads();
    static const std::vector<StreetLoop>& street_loops();
    static const std::vector<StreetLoop>& highways();
    static const std::vector<Bridge>& bridges();
    static const char* district(float x, float z);
    Vec3 spawn() const {
        if (city_ && city_->spawn) { auto p = *city_->spawn; p.SetY(height(p.GetX(),p.GetZ())+.56f); return p; }
        return Vec3(0, height(0, 105) + .56f, 105);
    }
    bool submerged(const Vec3& point) const;
    const std::vector<Vec3>& vertices() const { return vertices_; }
    const std::vector<TerrainTriangle>& triangles() const { return triangles_; }
    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Tree>& trees() const { return trees_; }
    const std::vector<Barrier>& barriers() const { return barriers_; }
    const std::vector<Port>& ports() const { return ports_; }
    float terrain_height(float x, float z) const;
private:
    std::shared_ptr<const City> city_;
    std::vector<Road> city_roads_;
    std::vector<StreetLoop> city_streets_;
    std::vector<Island> city_islands_;
    std::vector<Bridge> city_bridges_;
    std::array<int,City::width*City::width> city_diagonal_bridge_owner_{};
    std::vector<std::array<Vec3,4>> city_diagonal_bridge_decks_;
    static constexpr float city_mesh_spacing = City::block / 4, city_mesh_extent = City::extent + city_mesh_spacing * 12;
    static constexpr int city_mesh_width = City::width * 4 + 25;
    std::vector<float> city_heights_;
    static float elevation(float x, float z);
    static Surface surface(float x, float z, float y);
    std::vector<Vec3> vertices_;
    std::vector<TerrainTriangle> triangles_;
    std::vector<Building> buildings_;
    std::vector<Tree> trees_;
    std::vector<Barrier> barriers_;
    std::vector<Port> ports_;
};
} // namespace ambaretto
