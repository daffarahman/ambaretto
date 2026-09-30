#include "player.hpp"
#include "environment.hpp"
#include <cmath>

namespace forza {
Player::Player(PhysicsWorld& world, Car& car, const Environment& environment)
    : world_(world), car_(car), environment_(environment), character_(world) { reset(); }

void Player::reset() {
    car_.reset(environment_.spawn());
    character_.reset(environment_.spawn() - Vec3(0, 0.5f, 0));
    driving_ = true;
}

bool Player::can_enter() const {
    if (driving_ || car_.velocity().Length() > 2.5f) return false;
    const Vec3 difference = character_.position() - car_.position();
    if (difference.Length() > 3.3f || std::abs(difference.GetY()) > 1.8f) return false;
    const Vec3 eye = character_.position() + Vec3(0, 1, 0);
    return world_.camera_fraction(eye, car_.position() + Vec3(0, 0.7f, 0) - eye, car_.body_id()) > 0.98f;
}

Interaction Player::interact() {
    if (car_.velocity().Length() > 2.5f) return Interaction::TooFast;
    if (!driving_) {
        if (!can_enter()) return Interaction::TooFar;
        driving_ = true;
        return Interaction::Entered;
    }
    // Try both doors, then farther alongside and behind the car. Each exit
    // needs ground, free capsule space, and a clear path from the vehicle.
    const Vec3 exits[] = {Vec3(-2, 0, 0.35f), Vec3(2, 0, 0.35f),
        Vec3(-2.8f, 0, 0.35f), Vec3(2.8f, 0, 0.35f), Vec3(0, 0, 3.4f)};
    for (const auto& local : exits) {
        const Vec3 candidate = car_.position() + car_.rotate(local);
        GroundHit hit;
        if (!world_.cast_ground(candidate + Vec3(0, 3, 0), Vec3(0, -1, 0), 7, hit)
            || hit.normal.GetY() < 0.65f || hit.point.GetY() < Environment::water_level + 0.1f) continue;
        const Vec3 feet = hit.point + Vec3(0, 0.08f, 0);
        if (!character_.can_stand_at(feet)) continue;
        const Vec3 origin = car_.position() + Vec3(0, 1, 0);
        if (world_.camera_fraction(origin, feet + Vec3(0, 1, 0) - origin, car_.body_id()) < 0.98f) continue;
        const Vec3 forward = car_.forward();
        character_.reset(feet, std::atan2(-forward.GetX(), -forward.GetZ()));
        driving_ = false;
        return Interaction::Exited;
    }
    return Interaction::Blocked;
}

void Player::step(Input driving, FootInput walking, float dt) {
    if (!driving_) driving = {0, 0, false, true};
    car_.step(driving, dt);
    world_.step(dt);
    if (!driving_) character_.step(walking, dt);
}
} // namespace forza
