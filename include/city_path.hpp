#pragma once
#include "vehicle.hpp"
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace ambaretto {
struct PathPoint {
    Vec3 position = Vec3::sZero(), tangent = Vec3::sZero();
    bool operator==(const PathPoint& other) const { return position==other.position && tangent==other.tangent; }
};
struct CityPath {
    std::vector<PathPoint> nodes;
    bool closed = false;
    bool operator==(const CityPath& other) const { return closed==other.closed && nodes==other.nodes; }
    std::vector<Vec3> sample(float spacing = 2.f) const;
    bool validate(std::string& error, bool area) const;
};
enum class AreaKind { Land, Water, Texture };
struct CityArea {
    CityPath path;
    AreaKind kind = AreaKind::Land;
    std::string texture = "soil.png";
    std::vector<Vec3> boundary;
    Vec3 low = Vec3::sZero(), high = Vec3::sZero();
};
struct PathRoad {
    CityPath path;
    float width = 9.8f;
    std::vector<Vec3> centerline;
    std::vector<std::vector<Vec3>> pavement;
};
struct PathRoadSegment { Vec3 a, b; float width; };
float path_distance(Vec3 p, Vec3 a, Vec3 b);
bool polygon_contains(const std::vector<Vec3>& polygon, Vec3 p);
std::vector<std::array<Vec3,3>> polygon_triangles(std::vector<Vec3> polygon);
std::vector<Vec3> offset_path(const std::vector<Vec3>& points, float offset);
std::pair<std::vector<Vec3>,std::vector<Vec3>> split_path_polygon(const std::vector<Vec3>& polygon, Vec3 a, Vec3 b);
} // namespace ambaretto
