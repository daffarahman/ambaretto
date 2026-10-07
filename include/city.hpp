#pragma once
#include "vehicle.hpp"
#include "building_mesh.hpp"
#include "character_design.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace ambaretto {
class ControllerMapping;
struct GraphicsSettings;
enum class MenuCommand;
struct CityCell {
    int x = 0, z = 0;
    bool operator==(CityCell b) const { return x == b.x && z == b.z; }
    bool operator!=(CityCell b) const { return !(*this == b); }
};
enum class CityTile : unsigned char { Water, Land, Road, Bridge };
enum class CityGround : unsigned char { Soil, Grass, Sand, Asphalt };
struct CityBuilding {
    CityCell cell; int size = 1, height = 12;
    std::optional<BuildingMesh> mesh{};
    int rotation = 0;
    CityCell footprint() const { if (!mesh) return {size,size}; const auto tiles = mesh->footprint(); return rotation%2 ? CityCell{tiles[1],tiles[0]} : CityCell{tiles[0],tiles[1]}; }
    BuildingMesh shape() const { BuildingMesh result = mesh.value_or(BuildingMesh{}); if (!mesh) result.size = Vec3(size*11.2f,float(height),size*11.2f); return result; }
    Vec3 base(float ground) const;
    std::vector<Vec3> points(float ground) const;
    std::vector<BuildingTriangle> triangles(float ground) const;
};
enum class CityVehicleKind { Car, Trainer, F18, Boeing747 };
struct CityVehicle {
    CityVehicleKind kind = CityVehicleKind::Car;
    Vec3 position = Vec3::sZero();
    int rotation = 0;
    CityVehicle() = default;
    CityVehicle(CityVehicleKind kind, Vec3 position, int rotation = 0) : kind(kind), position(position), rotation(rotation) {}
    CityVehicle(CityVehicleKind kind, CityCell cell, int rotation = 0);
    Vec3 half_size() const;
    std::optional<CarDesign> car;
};
struct CityRoadPort {
    Vec3 center = Vec3::sZero();
    float width = 0;
};
struct CityRoadNode {
    CityCell first, last;
    std::vector<int> edges;
    unsigned mask = 0;
    std::array<CityRoadPort,4> ports{};
    float height = 3.2f;
    Vec3 center() const;
    Vec3 half_size() const;
    // Quarter-ellipse across the whole turn footprint; fraction selects a lane radius.
    std::vector<Vec3> bend(float fraction = .5f, float inset = 0) const;
    CityRoadPort port(int direction) const;
    std::vector<Vec3> path(float fraction = .5f, float inset = 0) const;
    std::vector<Vec3> outline() const;
};
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

// Terraced islands on an 11.2 m grid, with plain blocks or saved building meshes.
struct City {
    static constexpr int width = 128;
    static constexpr int corner_width = width + 1;
    static constexpr float block = 11.2f, level = 3.2f, half_block = block / 2, extent = width * block / 2;
    static constexpr float elevation_step = 2.f;
    static constexpr int max_elevation = 8;
    std::string id, name = "New city";
    std::array<CityTile, width * width> tiles{};
    std::array<CityGround, width * width> ground{};
    std::map<int,std::string> ground_textures;
    std::array<unsigned char, corner_width * corner_width> elevation{};
    // Stroke axes: 1 = north/south, 2 = east/west, 3 = crossing or bend.
    std::array<unsigned char, width * width> road_axes{};
    std::vector<CityBuilding> buildings;
    std::vector<CityVehicle> vehicles;
    std::vector<Tree> trees;
    int start_minutes = 12 * 60;
    std::optional<CityCell> spawn;
    std::optional<CharacterDesign> player_character;
    bool playable() const { std::string error; return spawn && player_character && player_character->type==CharacterType::Player && player_character->validate(error); }
    bool update_player_character(const std::vector<CharacterDesign>& designs);
    static bool contains(CityCell p) { return p.x >= 0 && p.z >= 0 && p.x < width && p.z < width; }
    static int index(CityCell p) { return p.z * width + p.x; }
    static int corner_index(CityCell p) { return p.z * corner_width + p.x; }
    static CityCell cell(float x, float z);
    static Vec3 center(CityCell p, float y = level);
    CityTile tile(CityCell p) const { return contains(p) ? tiles[index(p)] : CityTile::Water; }
    int building_at(CityCell p) const;
    int vehicle_at(Vec3 p) const;
    bool road(CityCell p) const { return tile(p) == CityTile::Road || tile(p) == CityTile::Bridge; }
    bool land(CityCell p) const { return tile(p) == CityTile::Land || tile(p) == CityTile::Road; }
    float tile_height(CityCell p) const;
    std::array<Vec3,4> ground_patch(CityCell p) const;
    std::array<std::array<Vec3,3>,2> ground_triangles(CityCell p) const;
    // 0 = flat, 1 = north/south ramp, 2 = east/west ramp, 3 = corner or ridge.
    unsigned ramp_axis(CityCell p) const;
    float height(float x, float z) const;
    std::vector<std::array<Vec3,3>> drape_triangle(const std::array<Vec3,3>& triangle) const;
    unsigned road_axis(CityCell p) const;
    unsigned road_mask(CityCell p) const;
    // Quarter-circle points for a simple two-way bend; other road shapes return no curve.
    std::vector<Vec3> road_bend(CityCell p, float radius = half_block) const;
    std::vector<CityRoadNode> road_network() const;
    std::vector<CityCell> neighbors(CityCell p) const;
    int land_count() const;
    bool connected() const;
    bool add_land(CityCell a, CityCell b, std::string& error);
    static std::vector<CityCell> road_stroke(CityCell a, CityCell b, bool z_first = false, bool diagonal = false);
    bool add_road(const std::vector<CityCell>& cells, std::string& error);
    bool add_building(CityBuilding building, std::string& error, int replace = -1);
    // Returns updated instances, 0 for no change, or -1 if the whole design update is rejected.
    int update_building_design(const BuildingMesh& mesh, std::string& error, int replace = -1);
    int update_car_design(const CarDesign& design, std::string& error, int replace = -1);
    // Returns updated instances; error lists designs rejected by the map's placement checks.
    int update_car_designs(const std::vector<CarDesign>& designs, std::string& error);
    bool add_vehicle(CityVehicle vehicle, std::string& error, int replace = -1);
    bool set_spawn(CityCell p, std::string& error);
    bool paint_ground(CityCell a, CityCell b, CityGround texture, std::string& error);
    bool paint_ground(CityCell a, CityCell b, const std::string& texture, std::string& error);
    std::string ground_texture(CityCell p) const;
    bool change_elevation(CityCell a, CityCell b, int direction, std::string& error);
    bool tree_clear(Vec3 p) const;
    void clear_trees();
    int brush_trees(Vec3 point, float radius, int density, bool remove, std::string& error);
    bool erase(CityCell p, std::string& error);
    bool erase(Vec3 p, std::string& error);
    bool validate(std::string& error) const;
    // Directed circuits cover each street both ways, turning back only at dead ends.
    // A smaller curb inset follows the same connections along the walking verge.
    std::vector<std::vector<Vec3>> traffic_routes(float curb_inset = 2.38f) const;
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, City& city, std::string& error);
    static City create(std::string name);
    bool generate_terrain(int tiles_x, int tiles_z, std::uint32_t seed, std::string& error);
};
enum class DesignKind { Building, Car, Character };
bool main_menu(City& selected, const std::filesystem::path& directory, ControllerMapping& controls,
               GraphicsSettings& graphics, const std::string& screenshot = {});
void design_menu(DesignKind kind, const std::filesystem::path& root, const std::string& screenshot = {}, bool create_new = false);
void settings_menu(ControllerMapping& controls, GraphicsSettings& graphics, const std::string& screenshot = {}, MenuCommand initial_settings = {});
// Returns a saved playable city on Play; otherwise returns to the main menu.
bool city_menu(City& selected, const std::filesystem::path& directory, ControllerMapping& controls,
               GraphicsSettings& graphics, bool edit_selected = false, const std::string& screenshot = {},
               bool preview_editor = false, MenuCommand initial_settings = {});
std::optional<BuildingMesh> building_builder(BuildingMesh mesh, const std::filesystem::path& directory,
                                           const std::string& screenshot = {}, int initial_tab = 0,
                                           std::vector<BuildingMesh>* saved_designs = nullptr);
} // namespace ambaretto
