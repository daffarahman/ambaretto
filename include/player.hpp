#pragma once
#include "vehicle.hpp"
#include "plane.hpp"

namespace forza {
class Traffic;
class Pedestrians;
struct FootInput {
    Vec3 direction{0, 0, 0};
    bool sprint = false;
    bool jump = false;
};

enum class BodyPart {
    Pelvis, Torso, Head,
    LeftUpperArm, LeftForearm, LeftHand,
    RightUpperArm, RightForearm, RightHand,
    LeftThigh, LeftShin, LeftFoot,
    RightThigh, RightShin, RightFoot, Count
};
struct BodyPartPose {
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::sIdentity();
    Vec3 size{0, 0, 0}; // Full dimensions, with limbs along local Y.
};

class Character {
public:
    explicit Character(PhysicsWorld& world, const Environment* environment = nullptr);
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;
    void reset(const Vec3& feet, float yaw = 0);
    void step(FootInput input, float dt = fixed_step);
    void set_enabled(bool enabled);
    bool ragdolling() const;
    bool swimming() const;
    void start_swimming(const Vec3& surface, float yaw = 0);
    // Velocity is inherited by every body; impulse is applied to the torso in N s.
    void ragdoll(const Vec3& inherited_velocity, const Vec3& impulse = Vec3::sZero());
    void hit_by(const Car& car, float dt = fixed_step);
    std::array<BodyPartPose, static_cast<std::size_t>(BodyPart::Count)> body_parts() const;
    bool can_stand_at(const Vec3& feet) const;
    Vec3 position() const;
    Vec3 velocity() const;
    bool grounded() const;
    float yaw() const { return yaw_; }
    float gait() const { return gait_; }
    Vec3 forward() const { return Quat::sRotation(Vec3::sAxisY(), yaw_) * Vec3(0, 0, -1); }
private:
    struct Impl;
    PhysicsWorld& world_;
    std::unique_ptr<Impl> impl_;
    float yaw_ = 0, gait_ = 0;
};

enum class Interaction { Entered, Exited, TooFast, TooFar, Blocked };
enum class EntryVehicle { None, Car, Plane };
class Player {
public:
    Player(PhysicsWorld& world, Car& car, const Environment& environment, Plane* plane = nullptr, Traffic* traffic = nullptr,
        Pedestrians* pedestrians = nullptr);
    void reset();
    void recover_plane();
    Interaction interact();
    void step(Input driving, FootInput walking, float dt = fixed_step, FlightInput flight = {});
    bool driving() const { return driving_; }
    bool flying() const { return flying_; }
    bool on_foot() const { return !driving_ && !flying_; }
    EntryVehicle entry_vehicle() const;
    Car* entry_car() const;
    bool can_steal() const;
    bool can_enter() const;
    Car& car() { return *car_; }
    const Car& car() const { return *car_; }
    Vec3 position() const { return driving_ ? car_->position() : flying_ ? plane_->position() : character_.position(); }
    Vec3 forward() const { return driving_ ? car_->forward() : flying_ ? plane_->forward() : character_.forward(); }
    Character& character() { return character_; }
    const Character& character() const { return character_; }
private:
    PhysicsWorld& world_;
    struct EntryTarget { EntryVehicle kind = EntryVehicle::None; Car* car = nullptr; };
    EntryTarget entry_target() const;
    Car& starter_car_;
    Car* car_;
    const Environment& environment_;
    Character character_;
    Plane* plane_ = nullptr;
    Traffic* traffic_ = nullptr;
    Pedestrians* pedestrians_ = nullptr;
    bool driving_ = true;
    bool flying_ = false;
    bool coasting_ = false;
};
} // namespace forza
