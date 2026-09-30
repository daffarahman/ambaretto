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
private:
    Model body_{};
    Shader shader_{};
    int camera_location_ = -1, emission_location_ = -1;
    bool ready_ = false;
};
} // namespace forza
