#pragma once
#include "vehicle.hpp"

namespace forza {
struct FootInput {
    Vec3 direction{0, 0, 0};
    bool sprint = false;
    bool jump = false;
};

class Character {
public:
    explicit Character(PhysicsWorld& world);
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;
    void reset(const Vec3& feet, float yaw = 0);
    void step(FootInput input, float dt = fixed_step);
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
class Player {
public:
    Player(PhysicsWorld& world, Car& car, const Environment& environment);
    void reset();
    Interaction interact();
    void step(Input driving, FootInput walking, float dt = fixed_step);
    bool driving() const { return driving_; }
    bool can_enter() const;
    Vec3 position() const { return driving_ ? car_.position() : character_.position(); }
    Vec3 forward() const { return driving_ ? car_.forward() : character_.forward(); }
    Character& character() { return character_; }
    const Character& character() const { return character_; }
private:
    PhysicsWorld& world_;
    Car& car_;
    const Environment& environment_;
    Character character_;
    bool driving_ = true;
};
} // namespace forza
