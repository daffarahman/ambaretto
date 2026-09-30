#pragma once
#include "vehicle.hpp"

namespace forza {
struct FlightInput {
    float throttle = 0; // Rate: positive increases the retained throttle.
    float pitch = 0;    // Positive pulls the nose up.
    float roll = 0;     // Positive banks left.
    float yaw = 0;      // Positive turns left (also nose-wheel steering).
    bool brake = false;
    bool flaps = false;
    bool parking_brake = false;
};

// A dynamic Jolt rigid body with aerodynamic forces and three raycast struts.
class Plane {
public:
    explicit Plane(PhysicsWorld& world);
    void reset(const Vec3& center, float yaw = -1.57079632679f,
               const Vec3& velocity = Vec3::sZero(), float throttle = 0);
    void step(FlightInput input, float dt = fixed_step);
    Vec3 position() const;
    Vec3 velocity() const;
    Quat rotation() const;
    Vec3 rotate(const Vec3& local) const { return rotation() * local; }
    Vec3 forward() const { return rotate(Vec3(0, 0, -1)); }
    JPH::BodyID body_id() const { return body_; }
    float throttle() const { return throttle_; }
    float airspeed() const { return airspeed_; }
    float angle_of_attack() const { return alpha_; }
    float propeller_angle() const { return propeller_angle_; }
    bool stalled() const { return stalled_; }
    bool grounded() const;
    bool damaged() const { return damaged_; }
    const std::array<Wheel, 3>& wheels() const { return wheels_; }
    static constexpr float parked_height = 1.16f;
    static constexpr float tire_radius = .26f;
private:
    void refresh_gear();
    PhysicsWorld& world_;
    JPH::BodyID body_;
    std::array<Wheel, 3> wheels_{};
    Vec3 previous_velocity_{0, 0, 0};
    float throttle_ = 0, airspeed_ = 0, alpha_ = 0, propeller_angle_ = 0;
    bool stalled_ = false, damaged_ = false;
};
} // namespace forza
