#pragma once
#include "car_design.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace ambaretto {
class Environment;
class Character;
class Plane;
class Car;
struct ShotHit;
using Vec3 = JPH::Vec3;
using Quat = JPH::Quat;
struct VehicleDamage {
    static constexpr float max_health = 100;
    static constexpr float gunfire_multiplier = .35f;
    float health = max_health, crash_cooldown = 0, explosion_time = 10;
    std::atomic<float> impact_speed{0};
    Vec3 explosion_position{0, 0, 0};
    bool explosion_pending = false, player_caused = false;
    void take_damage(float amount, Vec3 position, bool by_player = false);
    void repair();
};
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
    Car* car = nullptr;
    Plane* plane = nullptr;
    JPH::BodyID body;
};
enum class SoundEffect { Pistol, SMG, AK47, Explosion, Count };
struct SoundEvent { SoundEffect effect; Vec3 position; };

class PhysicsWorld {
public:
    explicit PhysicsWorld(bool with_ridges = true);
    explicit PhysicsWorld(const Environment& environment);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    bool cast_ground(const Vec3& origin, const Vec3& direction,
                     float distance, GroundHit& hit) const;
    bool cast_ray(const Vec3& origin, const Vec3& direction, float distance, GroundHit& hit, JPH::BodyID ignore = {}) const;
    void raycast_characters(Vec3 origin, Vec3 direction, ShotHit& hit, const Character* ignore = nullptr) const;
    void step(float dt = fixed_step);
    void emit_sound(SoundEffect effect, Vec3 position);
    std::vector<SoundEvent> take_sound_events();
    void notify_player_kill();
    bool take_player_kill();
    float camera_fraction(const Vec3& origin, const Vec3& offset, JPH::BodyID ignore = {}) const;
private:
    friend class Car;
    friend class Character;
    friend class Plane;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JPH::BodyID create_chassis();
};

class Car {
public:
    explicit Car(PhysicsWorld& world, CarType type = CarType::Civilian);
    Car(PhysicsWorld& world, const CarDesign& design);
    ~Car();
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
    void set_design(const CarDesign& design);
    const std::optional<CarDesign>& design() const { return design_; }
    Vec3 body_size() const { return design_ ? Vec3(design_->width,design_->height,design_->length) : Vec3(1.86f,.44f,3.7f); }
    Vec3 body_offset() const { return design_ ? Vec3(design_->offset[0],design_->height/2-.15f+design_->offset[1],design_->offset[2]) : Vec3(0,chassis_offset,0); }
    float ride_height() const { return design_ ? tuning_.rest_length+tuning_.wheel_radius-tuning_.mount_height : .56f; }
    void set_simulated(bool simulated);
    bool simulated() const { return simulated_; }
    static constexpr float max_health = VehicleDamage::max_health;
    float health() const { return damage_.health; }
    bool destroyed() const { return health() <= 0; }
    void take_damage(float amount, bool by_player = false);
    bool player_destroyed() const { return destroyed() && damage_.player_caused; }
    bool player_controlled() const { return player_controlled_; }
    void repair();
    float explosion_time() const { return damage_.explosion_time; }
    Vec3 explosion_position() const { return damage_.explosion_position; }
private:
    friend class PhysicsWorld;
    void update_wheel_mounts();
    void refresh_wheel_contacts();
    PhysicsWorld& world_;
    JPH::BodyID body_;
    std::array<Wheel, 4> wheels_{};
    float steer_ = 0;
    int drive_direction_ = 0;
    float direction_change_time_ = 0;
    CarTuning tuning_{};
    std::optional<CarDesign> design_;
    CarType type_;
    bool simulated_ = true;
    bool player_controlled_ = false;
    VehicleDamage damage_;
};
} // namespace ambaretto
