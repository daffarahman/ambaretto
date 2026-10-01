#pragma once
#include "environment.hpp"
#include "plane.hpp"
#include <raylib.h>

namespace forza {
class Traffic;
class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(const Environment& environment);
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&) = delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&) = delete;
    void draw(const Camera3D& camera, float time);
    void minimap(const Environment& environment, const Car& car, const Plane& plane, Vec3 player_position, Vec3 player_forward, int screen_width, const Traffic* traffic = nullptr) const;
private:
    void load_trees(const Environment& environment);
    void load_minimap(const Environment& environment);
    Model terrain_{}, grass_{}, sand_{}, roads_{}, city_{}, ocean_{}, trees_{}, signs_{};
    Texture2D grass_texture_{}, sand_texture_{}, asphalt_texture_{}, tree_texture_{}, sign_texture_{}, minimap_texture_{};
    Shader land_shader_{}, water_shader_{}, tree_shader_{};
    int land_camera_ = -1, water_camera_ = -1, water_time_ = -1, tree_camera_ = -1;
    bool trees_ready_ = false;
};
} // namespace forza
