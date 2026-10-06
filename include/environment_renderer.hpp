#pragma once
#include "environment.hpp"
#include "day_night.hpp"
#include "world_map.hpp"
#include "scene_lighting.hpp"
#include <raylib.h>
#include <map>

namespace ambaretto {
class Police;
std::filesystem::path building_texture_directory();
Texture2D load_building_texture(const std::string& filename);
class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(const Environment& environment);
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&) = delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&) = delete;
    void draw_sky(const Camera3D& camera, const Daylight& light, float time, const GraphicsSettings& settings);
    void draw(const Camera3D& camera, float time, const Daylight& light, const SceneLighting& lighting, const GraphicsSettings& settings);
    void draw_shadow(Shader shader, const Vector3& focus, float distance);
    const std::vector<SceneLight>& lights() const { return local_lights_; }
    Shader object_shader() const { return land_shader_; }
    void minimap(Vec3 player_position, Vec3 player_forward, const Camera3D& camera, const Police* police = nullptr, bool in_vehicle = false) const;
    void world_map(const WorldMapView& view, Rectangle viewport, Vec3 player_position, Vec3 player_forward, const Police* police = nullptr) const;
private:
    void load_city(const Environment& environment);
    const Environment* environment_ = nullptr;
    void load_trees(const Environment& environment);
    void load_map(const Environment& environment);
    void draw_map(Matrix transform, Shader shader = {}) const;
    struct Chunk { Model model{}; Vector3 center{}; float radius = 0; };
    static Chunk chunk(Model model);
    static bool nearby(const Chunk& chunk, const Vector3& focus, float distance);
    std::vector<Chunk> city_chunks_, tree_chunks_;
    std::vector<SceneLight> local_lights_;
    Model terrain_{}, grass_{}, sand_{}, roads_{}, ocean_{}, trees_{}, signs_{}, lights_{}, glows_{}, map_{};
    Texture2D soil_texture_{}, grass_texture_{}, sand_texture_{}, asphalt_texture_{}, tree_texture_{}, sign_texture_{};
    std::map<std::string,Texture2D> building_textures_;
    Shader land_shader_{}, water_shader_{}, tree_shader_{}, sky_shader_{}, light_shader_{}, minimap_shader_{};
    int land_camera_ = -1, water_camera_ = -1, water_time_ = -1, tree_camera_ = -1, sign_emission_ = -1;
    bool trees_ready_ = false;
};
} // namespace ambaretto
