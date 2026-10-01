#pragma once
#include "environment.hpp"
#include "plane.hpp"
#include "day_night.hpp"
#include "world_map.hpp"
#include <raylib.h>

namespace forza {
class Traffic;
class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(const Environment& environment);
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&) = delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&) = delete;
    void draw_sky(const Camera3D& camera, const Daylight& light, float time);
    void draw(const Camera3D& camera, float time, const Daylight& light);
    Shader object_shader() const { return land_shader_; }
    void minimap(const Car& car, const Plane& plane, Vec3 player_position, Vec3 player_forward, const Camera3D& camera, const Traffic* traffic = nullptr) const;
    void world_map(const WorldMapView& view, Rectangle viewport, const Car& car, const Plane& plane, Vec3 player_position, Vec3 player_forward, const Traffic* traffic = nullptr) const;
private:
    void load_trees(const Environment& environment);
    void load_minimap(const Environment& environment);
    Model terrain_{}, grass_{}, sand_{}, roads_{}, city_{}, ocean_{}, trees_{}, signs_{}, lights_{}, glows_{};
    Texture2D grass_texture_{}, sand_texture_{}, asphalt_texture_{}, tree_texture_{}, sign_texture_{}, minimap_texture_{};
    Shader land_shader_{}, water_shader_{}, tree_shader_{}, sky_shader_{}, light_shader_{};
    int land_camera_ = -1, water_camera_ = -1, water_time_ = -1, tree_camera_ = -1, sign_emission_ = -1;
    bool trees_ready_ = false;
};
} // namespace forza
