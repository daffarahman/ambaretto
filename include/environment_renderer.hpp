#pragma once
#include "environment.hpp"
#include <raylib.h>

namespace forza {
class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(const Environment& environment);
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&) = delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&) = delete;
    void draw(const Camera3D& camera, float time);
    void minimap(const Environment& environment, const Car& car, Vec3 player_position, Vec3 player_forward, int screen_width) const;
private:
    Model terrain_{}, city_{}, ocean_{};
    Shader land_shader_{}, water_shader_{};
    int land_camera_ = -1, water_camera_ = -1, water_time_ = -1;
};
} // namespace forza
