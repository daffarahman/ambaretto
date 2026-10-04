#pragma once
#include "vehicle.hpp"

namespace forza {
class ThirdPersonCamera {
public:
    void reset(float yaw = 0) { yaw_ = movement_yaw_ = yaw; pitch_ = 0.30f; idle_ = 0; movement_input_ = Vec3::sZero(); follow_foot_ = true; }
    void look(float mouse_x, float mouse_y, float wheel, bool driving, Vec3 actor_forward, float speed, float dt, bool flying = false, bool auto_follow = true);
    Vec3 forward() const;
    Vec3 aim_direction() const;
    void aim_at(Vec3 origin, Vec3 point);
    Vec3 move_direction(float forward_input, float right_input);
    Vec3 shoulder_focus(Vec3 center, float width, Vec3 cover_side = Vec3::sZero()) const;
    Vec3 desired_position(const Vec3& focus, bool driving, bool flying = false, float plane_scale = 1, bool aiming = false) const;
    static Vec3 above_water(Vec3 position);
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    void recoil(float amount);
private:
    float yaw_ = 0, pitch_ = 0.30f, idle_ = 0;
    float movement_yaw_ = 0;
    Vec3 movement_input_{0, 0, 0};
    bool follow_foot_ = true;
    float foot_distance_ = 4.5f, car_distance_ = 9, plane_distance_ = 20;
};
} // namespace forza
