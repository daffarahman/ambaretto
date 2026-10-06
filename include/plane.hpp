#pragma once
#include "vehicle.hpp"
#include <algorithm>
#include <vector>

namespace ambaretto {
enum class PlaneType { Trainer, F18, Boeing747 };
struct PlaneSpecs {
    const char* name;
    float span, length, mass, wing_area, thrust, reference_speed;
    float body_radius, gear_mount, gear_rest, wheel_radius;
};
const PlaneSpecs& plane_specs(PlaneType type);
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
    explicit Plane(PhysicsWorld& world, PlaneType type = PlaneType::Trainer);
    ~Plane();
    void reset(const Vec3& center, float yaw = 0,
               const Vec3& velocity = Vec3::sZero(), float throttle = 0);
    void step(FlightInput input, float dt = fixed_step);
    void set_simulated(bool simulated);
    bool simulated() const { return simulated_; }
    Vec3 position() const;
    Vec3 velocity() const;
    Quat rotation() const;
    Vec3 rotate(const Vec3& local) const { return rotation() * local; }
    Vec3 forward() const { return rotate(Vec3(0, 0, -1)); }
    JPH::BodyID body_id() const { return body_; }
    PlaneType type() const { return type_; }
    const PlaneSpecs& specs() const { return plane_specs(type_); }
    float parking_height() const { return specs().gear_mount + specs().gear_rest + specs().wheel_radius - .08f; }
    float camera_scale() const { return std::max(1.0f, specs().length / 10); }
    Vec3 boarding_position() const;
    std::array<Vec3, 5> exit_offsets() const;
    float throttle() const { return throttle_; }
    float airspeed() const { return airspeed_; }
    float angle_of_attack() const { return alpha_; }
    float propeller_angle() const { return propeller_angle_; }
    bool stalled() const { return stalled_; }
    bool grounded() const;
    bool damaged() const { return destroyed(); }
    static constexpr float max_health = VehicleDamage::max_health;
    float health() const { return damage_.health; }
    bool destroyed() const { return health() <= 0; }
    void take_damage(float amount, bool by_player = false);
    void repair();
    float explosion_time() const { return damage_.explosion_time; }
    Vec3 explosion_position() const { return damage_.explosion_position; }
    float explosion_radius() const { return std::max(10.f, specs().length * .3f); }
    const std::array<Wheel, 3>& wheels() const { return wheels_; }
    static constexpr float parked_height = 1.16f;
    static constexpr float tire_radius = .26f;
private:
    friend class PhysicsWorld;
    void refresh_gear();
    PhysicsWorld& world_;
    PlaneType type_;
    JPH::BodyID body_;
    std::array<Wheel, 3> wheels_{};
    float throttle_ = 0, airspeed_ = 0, alpha_ = 0, propeller_angle_ = 0;
    bool stalled_ = false;
    bool simulated_ = true;
    VehicleDamage damage_;
};
std::vector<std::unique_ptr<Plane>> parked_aircraft(PhysicsWorld& world, const Environment& environment);
} // namespace ambaretto
