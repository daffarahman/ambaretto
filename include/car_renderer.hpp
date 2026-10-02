#pragma once
#include "vehicle.hpp"
#include "day_night.hpp"
#include "scene_lighting.hpp"
#include <raylib.h>

namespace forza {
class CarRenderer {
public:
    CarRenderer();
    ~CarRenderer();
    CarRenderer(const CarRenderer&) = delete;
    CarRenderer& operator=(const CarRenderer&) = delete;
    void set_lighting(const SceneLighting& lighting, const Camera3D& camera, const Daylight& light, const GraphicsSettings& settings) const { lighting.apply(shader_, camera, light, settings); }
    bool draw_body(const Car& car, const Camera3D& camera, Color paint = {235, 235, 224, 255}, Shader override_shader = {}) const;
    bool draw_wheel(const Car& car, const Wheel& wheel, const Camera3D& camera, Shader override_shader = {}) const;
private:
    void load_body();
    void load_wheel();
    void draw_model(const Model& model, const Matrix& transform, const Camera3D& camera, Color paint = BLANK, Shader override_shader = {}) const;
    Model body_{}, wheel_{};
    Shader shader_{};
    int camera_location_ = -1, emission_location_ = -1;
    bool ready_ = false, wheel_ready_ = false;
};
} // namespace forza
