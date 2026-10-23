#pragma once
#include "building_mesh.hpp"

namespace ambaretto {
struct City;
struct CityBuilding;
struct InteriorObject {
    int design = 0;
    Vec3 position = Vec3::sZero();
    float yaw = 0;
};
struct Interior {
    std::string id, name = "New interior";
    Vec3 size{16, 4, 16};
    std::optional<BuildingMesh> shell;
    std::optional<Vec3> exit_point;
    std::vector<BuildingMesh> designs;
    std::vector<InteriorObject> objects;
    Vec3 exit() const { return exit_point.value_or(Vec3(0, .08f, size.GetZ()/2-1.6f)); }
    Vec3 spawn() const { return exit()-Vec3(0, 0, 2.4f); }
    bool validate(std::string& error) const;
    bool can_place(const InteriorObject& object, std::string& error) const;
    bool place(InteriorObject object, std::string& error, int replace = -1);
    BuildingMesh surface_mesh() const;
    void write(std::ostream& file, int version = 2) const;
    static bool read(std::istream& file, Interior& room, std::string& error, int version = 2);
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, Interior& room, std::string& error);
    std::vector<BuildingTriangle> triangles(bool ceiling = true) const;
    std::vector<BuildingTriangle> object_triangles(const InteriorObject& object) const;
};
std::vector<Interior> saved_interiors(const std::filesystem::path& directory, std::string& error);
const BuildingEntrance* building_entrance(const CityBuilding& building);
bool update_city_interiors(City& city, const std::vector<Interior>& library, std::string& error, const std::string& required = {});
Vec3 entrance_position(const City& city, const CityBuilding& building);
bool validate_interiors(const City& city, std::string& error);
void write_interiors(std::ostream& file, const City& city, int version = 2);
bool read_interiors(std::istream& file, City& city, std::string& error, int version = 2);
bool near_interior_marker(Vec3 feet, Vec3 marker, float radius = 1.15f);
} // namespace ambaretto
