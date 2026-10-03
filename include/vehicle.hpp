#pragma once
#include "car_tuning.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <array>
#include <memory>

namespace forza {
class Environment;
class Character;
class Plane;
using Vec3 = JPH::Vec3;
using Quat = JPH::Quat;
inline constexpr float fixed_step = 1.0f / 120.0f;
inline constexpr float wheel_radius = CarTuning{}.wheel_radius;
inline constexpr float chassis_offset = 0.5f;

struct Input {
    float throttle = 0;
    float steer = 0; // Positive turns left; the car faces local -Z.
    bool handbrake = false;
    bool parking_brake = false;
    bool player_controlled = false;
};
struct Wheel {
    Vec3 mount{0, 0, 0};
    bool front = false;
    Vec3 center{0, 0, 0};
    Vec3 ground_point{0, 0, 0};
    Vec3 ground_normal{0, 1, 0};
    bool grounded = false;
    bool skidding = false;
    float compression = 0;
    float spin = 0;
    float normal_force = 0;
};
struct GroundHit {
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};
    float distance = 0;
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(bool with_ridges = true);
    explicit PhysicsWorld(const Environment& environment);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    bool cast_ground(const Vec3& origin, const Vec3& direction,
                     float distance, GroundHit& hit) const;
    bool cast_ray(const Vec3& origin, const Vec3& direction, float distance, GroundHit& hit) const;
    void step(float dt = fixed_step);
    float camera_fraction(const Vec3& origin, const Vec3& offset, JPH::BodyID ignore = {}) const;
private:
    friend class Car;
    friend class Character;
    friend class Plane;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JPH::BodyID create_chassis();
};

enum class CarType { Civilian, Police };
class Car {
public:
    explicit Car(PhysicsWorld& world, CarType type = CarType::Civilian);
    CarType type() const { return type_; }
    void step(Input input, float dt = fixed_step);
    void reset(const Vec3& center_of_mass, float yaw = 0);
    Vec3 position() const;
    Vec3 velocity() const;
    Quat rotation() const;
    Vec3 rotate(const Vec3& local) const { return rotation() * local; }
    Vec3 forward() const { return rotate(Vec3(0, 0, -1)); }
    const std::array<Wheel, 4>& wheels() const { return wheels_; }
    // Contact samples precede world integration; draw tires with the body's current pose.
    Vec3 wheel_center(const Wheel& wheel) const {
        return position() + rotate(wheel.mount - Vec3(0, tuning_.rest_length - wheel.compression, 0));
    }
    float steering() const { return steer_; }
    JPH::BodyID body_id() const { return body_; }
    const CarTuning& tuning() const { return tuning_; }
    void set_tuning(CarTuning tuning);
    void set_simulated(bool simulated);
    bool simulated() const { return simulated_; }
private:
    void update_wheel_mounts();
    void refresh_wheel_contacts();
    PhysicsWorld& world_;
    JPH::BodyID body_;
    std::array<Wheel, 4> wheels_{};
    float steer_ = 0;
    int drive_direction_ = 0;
    float direction_change_time_ = 0;
    CarTuning tuning_{};
    CarType type_;
    bool simulated_ = true;
};
} // namespace forza
