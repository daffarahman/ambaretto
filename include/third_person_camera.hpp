#pragma once
#include "vehicle.hpp"

namespace forza {
class ThirdPersonCamera {
public:
    void reset(float yaw = 0) { yaw_ = yaw; pitch_ = 0.30f; idle_ = 0; }
    void look(float mouse_x, float mouse_y, float wheel, bool driving, Vec3 car_forward, float speed, float dt, bool flying = false);
    Vec3 forward() const;
    Vec3 move_direction(float forward_input, float right_input) const;
    Vec3 desired_position(const Vec3& focus, bool driving, bool flying = false) const;
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
private:
    float yaw_ = 0, pitch_ = 0.30f, idle_ = 0;
    float foot_distance_ = 4.5f, car_distance_ = 9, plane_distance_ = 20;
};
} // namespace forza
