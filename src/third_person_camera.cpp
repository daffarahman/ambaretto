#include "third_person_camera.hpp"
#include "environment.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
constexpr float two_pi = 6.28318530718f;
void ThirdPersonCamera::recoil(float amount) {
    pitch_ = std::clamp(pitch_ - amount, -.95f, 1.12f);
}
void ThirdPersonCamera::look(float mouse_x, float mouse_y, float wheel, bool driving, Vec3 actor_forward, float speed, float dt, bool flying, bool auto_follow) {
    yaw_ = std::remainder(yaw_ - mouse_x * 0.003f, two_pi);
    movement_yaw_ = std::remainder(movement_yaw_ - mouse_x * 0.003f, two_pi);
    const bool vehicle = driving || flying;
    follow_foot_ = !vehicle && auto_follow;
    pitch_ = std::clamp(pitch_ + mouse_y * 0.003f, driving || flying ? -0.12f : -.95f, 1.12f);
    float& distance = flying ? plane_distance_ : driving ? car_distance_ : foot_distance_;
    distance = std::clamp(distance - wheel * (flying ? 1.5f : .7f), flying ? 12.0f : driving ? 5.0f : 2.5f,
        flying ? 35.0f : driving ? 14.0f : 7.0f);
    if (std::abs(mouse_x) + std::abs(mouse_y) > 0.1f || !auto_follow || (!vehicle && std::abs(wheel) > .001f)) idle_ = 0;
    else idle_ += dt;
    if (auto_follow && speed > (vehicle ? 3.f : .35f) && (vehicle ? idle_ > 1.8f : idle_ >= .1f)) {
        const float heading = std::atan2(-actor_forward.GetX(), -actor_forward.GetZ());
        const float rate = vehicle ? 2.2f : 2.f + std::min(speed, 7.3f) / 7.3f;
        float turn = std::remainder(heading - yaw_, two_pi) * (1 - std::exp(-rate * dt));
        if (!vehicle) turn = std::clamp(turn, -2.4f * dt, 2.4f * dt);
        yaw_ = std::remainder(yaw_ + turn, two_pi);
    }
}
Vec3 ThirdPersonCamera::forward() const { return Vec3(-std::sin(yaw_), 0, -std::cos(yaw_)); }
Vec3 ThirdPersonCamera::aim_direction() const { return forward() * std::cos(pitch_) - Vec3::sAxisY() * std::sin(pitch_); }
void ThirdPersonCamera::aim_at(Vec3 origin, Vec3 point) {
    const Vec3 direction = point - origin;
    if (direction.LengthSq() < .00001f) return;
    yaw_ = std::atan2(-direction.GetX(), -direction.GetZ());
    pitch_ = std::clamp(-std::atan2(direction.GetY(), std::hypot(direction.GetX(), direction.GetZ())), -.95f, 1.12f);
    movement_yaw_ = yaw_;
    idle_ = 0;
}
Vec3 ThirdPersonCamera::move_direction(float forward_input, float right_input) {
    const auto input = Vec3(right_input, 0, -forward_input).NormalizedOr(Vec3::sZero());
    // Keep held travel straight while the camera turns behind it. New input uses the current view.
    if (!follow_foot_ || input.LengthSq() == 0 || input.Dot(movement_input_) < .999f) movement_yaw_ = yaw_;
    movement_input_ = input;
    const Vec3 f(-std::sin(movement_yaw_), 0, -std::cos(movement_yaw_));
    Vec3 direction = f * forward_input + f.Cross(Vec3::sAxisY()) * right_input;
    return direction.LengthSq() > 1 ? direction.Normalized() : direction;
}
Vec3 ThirdPersonCamera::shoulder_focus(Vec3 center, float width, Vec3 cover_side) const {
    const Vec3 right = forward().Cross(Vec3::sAxisY());
    return center + right * (cover_side.Dot(right) < -.05f ? -width : width);
}
Vec3 ThirdPersonCamera::desired_position(const Vec3& focus, bool driving, bool flying, float plane_scale, bool aiming) const {
    const float distance = aiming && !driving && !flying ? .85f : flying ? plane_distance_ * plane_scale : driving ? car_distance_ : foot_distance_;
    return above_water(focus - forward() * (distance * std::cos(pitch_)) + Vec3(0, distance * std::sin(pitch_), 0));
}
Vec3 ThirdPersonCamera::above_water(Vec3 position) {
    position.SetY(std::max(position.GetY(), Environment::water_level + .3f));
    return position;
}
} // namespace forza
