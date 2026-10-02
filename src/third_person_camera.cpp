#include "third_person_camera.hpp"
#include "environment.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
constexpr float two_pi = 6.28318530718f;
void ThirdPersonCamera::look(float mouse_x, float mouse_y, float wheel, bool driving, Vec3 car_forward, float speed, float dt, bool flying) {
    yaw_ = std::remainder(yaw_ - mouse_x * 0.003f, two_pi);
    pitch_ = std::clamp(pitch_ + mouse_y * 0.003f, -0.12f, 1.12f);
    float& distance = flying ? plane_distance_ : driving ? car_distance_ : foot_distance_;
    distance = std::clamp(distance - wheel * (flying ? 1.5f : .7f), flying ? 12.0f : driving ? 5.0f : 2.5f,
        flying ? 35.0f : driving ? 14.0f : 7.0f);
    if (std::abs(mouse_x) + std::abs(mouse_y) > 0.1f) idle_ = 0;
    else idle_ += dt;
    if ((driving || flying) && speed > 3 && idle_ > 1.8f) {
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
Vec3 ThirdPersonCamera::desired_position(const Vec3& focus, bool driving, bool flying) const {
    const float distance = flying ? plane_distance_ : driving ? car_distance_ : foot_distance_;
    return above_water(focus - forward() * (distance * std::cos(pitch_)) + Vec3(0, distance * std::sin(pitch_), 0));
}
Vec3 ThirdPersonCamera::above_water(Vec3 position) {
    position.SetY(std::max(position.GetY(), Environment::water_level + .3f));
    return position;
}
} // namespace forza
