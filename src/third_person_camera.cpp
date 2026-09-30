#include "third_person_camera.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
constexpr float two_pi = 6.28318530718f;
void ThirdPersonCamera::look(float mouse_x, float mouse_y, float wheel, bool driving, Vec3 car_forward, float speed, float dt) {
    yaw_ = std::remainder(yaw_ - mouse_x * 0.003f, two_pi);
    pitch_ = std::clamp(pitch_ + mouse_y * 0.003f, -0.12f, 1.12f);
    float& distance = driving ? car_distance_ : foot_distance_;
    distance = std::clamp(distance - wheel * 0.7f, driving ? 5.0f : 2.5f, driving ? 14.0f : 7.0f);
    if (std::abs(mouse_x) + std::abs(mouse_y) > 0.1f) idle_ = 0;
    else idle_ += dt;
    if (driving && speed > 3 && idle_ > 1.8f) {
        const float heading = std::atan2(-car_forward.GetX(), -car_forward.GetZ());
        yaw_ += std::remainder(heading - yaw_, two_pi) * (1 - std::exp(-2.2f * dt));
    }
}
Vec3 ThirdPersonCamera::forward() const { return Vec3(-std::sin(yaw_), 0, -std::cos(yaw_)); }
Vec3 ThirdPersonCamera::move_direction(float forward_input, float right_input) const {
    const Vec3 f = forward();
    Vec3 direction = f * forward_input + f.Cross(Vec3::sAxisY()) * right_input;
    return direction.LengthSq() > 1 ? direction.Normalized() : direction;
}
Vec3 ThirdPersonCamera::desired_position(const Vec3& focus, bool driving) const {
    const float distance = driving ? car_distance_ : foot_distance_;
    return focus - forward() * (distance * std::cos(pitch_)) + Vec3(0, distance * std::sin(pitch_), 0);
}
} // namespace forza
