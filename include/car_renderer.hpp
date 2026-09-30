#pragma once
#include "vehicle.hpp"
#include <raylib.h>

namespace forza {
class CarRenderer {
public:
    CarRenderer();
    ~CarRenderer();
    CarRenderer(const CarRenderer&) = delete;
    CarRenderer& operator=(const CarRenderer&) = delete;
    bool draw_body(const Car& car, const Camera3D& camera) const;
    bool draw_wheel(const Car& car, const Wheel& wheel, const Camera3D& camera) const;
private:
    void load_body();
    void load_wheel();
    void draw_model(const Model& model, const Matrix& transform, const Camera3D& camera) const;
    Model body_{}, wheel_{};
    Shader shader_{};
    int camera_location_ = -1, emission_location_ = -1;
    bool ready_ = false, wheel_ready_ = false;
};
} // namespace forza
