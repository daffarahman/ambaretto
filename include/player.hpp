#pragma once
#include "vehicle.hpp"
#include "plane.hpp"
#include "weapons.hpp"

namespace forza {
class Traffic;
class Pedestrians;
class Police;
struct FootInput {
    Vec3 direction{0, 0, 0};
    bool sprint = false;
    bool jump = false;
    Vec3 aim_direction{0, 0, 0};
    WeaponType weapon = WeaponType::Unarmed;
};

enum class BodyPart {
    Pelvis, Torso, Head,
    LeftUpperArm, LeftForearm, LeftHand,
    RightUpperArm, RightForearm, RightHand,
    LeftThigh, LeftShin, LeftFoot,
    RightThigh, RightShin, RightFoot, Count
};
enum class DamageSource { Impact, Bullet };
enum class CoverStance { None, Standing, Crouched, Crawling };
struct BodyPartPose {
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::sIdentity();
    Vec3 size{0, 0, 0}; // Full dimensions, with limbs along local Y.
};

class Character {
public:
    explicit Character(PhysicsWorld& world, const Environment* environment = nullptr, bool player_controlled = false);
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;
    void reset(const Vec3& feet, float yaw = 0);
    void step(FootInput input, float dt = fixed_step);
    void set_enabled(bool enabled);
    bool enabled() const;
    bool pull_from(const Car& car, float side = -1);
    bool ragdolling() const;
    bool swimming() const;
    bool toggle_cover();
    bool covering() const;
    CoverStance cover_stance() const;
    bool cover_peeking() const;
    bool cover_aim_ready() const;
    Vec3 cover_aim_side() const;
    void start_swimming(const Vec3& surface, float yaw = 0);
    // Velocity is inherited by every body; impulse is applied to the torso in N s.
    void ragdoll(const Vec3& inherited_velocity, const Vec3& impulse = Vec3::sZero());
    void hit_by(const Car& car, float dt = fixed_step);
    const Car* last_vehicle_hit() const;
    bool touching(const Car& car) const;
    std::array<BodyPartPose, static_cast<std::size_t>(BodyPart::Count)> body_parts() const;
    BodyPartPose held_weapon() const;
    bool can_stand_at(const Vec3& feet) const;
    Vec3 position() const;
    Vec3 velocity() const;
    bool grounded() const;
    float health() const { return health_; }
    bool alive() const { return health_ > 0; }
    void revive() { health_ = 100; }
    void take_damage(float amount, BodyPart part = BodyPart::Torso, Vec3 impulse = Vec3::sZero(), DamageSource source = DamageSource::Impact);
    bool raycast(Vec3 origin, Vec3 direction, float& distance, BodyPart& part) const;
    float yaw() const { return yaw_; }
    float gait() const { return gait_; }
    Vec3 forward() const { return Quat::sRotation(Vec3::sAxisY(), yaw_) * Vec3(0, 0, -1); }
private:
    bool cover_wall(Vec3 feet, Vec3 direction, float range, GroundHit& wall, float& height) const;
    bool set_posture(float height, bool force = false);
    void leave_cover();
    void begin_cover_transition(float duration);
    struct Impl;
    PhysicsWorld& world_;
    std::unique_ptr<Impl> impl_;
    float yaw_ = 0, gait_ = 0;
    float health_ = 100;
};

enum class Interaction { Entered, Exited, TooFast, TooFar, Blocked };
enum class EntryVehicle { None, Car, Plane };
class Player {
public:
    Player(PhysicsWorld& world, Car& car, const Environment& environment, Plane* plane = nullptr, Traffic* traffic = nullptr,
        Pedestrians* pedestrians = nullptr, const std::vector<std::unique_ptr<Plane>>* aircraft = nullptr, Police* police = nullptr);
    void reset();
    void respawn_on_foot(Vec3 feet, float yaw = 0);
    void respawn_near(Vec3 origin);
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
    Plane& plane() { return *plane_; }
    const Plane& plane() const { return *plane_; }
    Vec3 position() const { return driving_ ? car_->position() : flying_ ? plane_->position() : character_.position(); }
    Vec3 forward() const { return driving_ ? car_->forward() : flying_ ? plane_->forward() : character_.forward(); }
    Character& character() { return character_; }
    const Character& character() const { return character_; }
    Weapons& weapons() { return weapons_; }
    const Weapons& weapons() const { return weapons_; }
    bool can_shoot() const;
    Shot shoot(Vec3 origin, Vec3 direction, bool held, bool pressed, bool aiming);
private:
    PhysicsWorld& world_;
    struct EntryTarget { EntryVehicle kind = EntryVehicle::None; Car* car = nullptr; Plane* plane = nullptr; };
    EntryTarget entry_target() const;
    Car& starter_car_;
    Car* car_;
    const Environment& environment_;
    Character character_;
    Weapons weapons_;
    Plane* plane_ = nullptr;
    Plane* starter_plane_ = nullptr;
    const std::vector<std::unique_ptr<Plane>>* aircraft_ = nullptr;
    Traffic* traffic_ = nullptr;
    Pedestrians* pedestrians_ = nullptr;
    Police* police_ = nullptr;
    bool driving_ = true;
    bool flying_ = false;
    bool coasting_ = false;
};
} // namespace forza
