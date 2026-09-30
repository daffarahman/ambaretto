#include "player.hpp"
#include "environment.hpp"
#include "airport.hpp"
#include <cmath>

namespace forza {
Player::Player(PhysicsWorld& world, Car& car, const Environment& environment, Plane* plane)
    : world_(world), car_(car), environment_(environment), character_(world), plane_(plane) { reset(); }

void Player::reset() {
    car_.reset(environment_.spawn());
    character_.reset(environment_.spawn() - Vec3(0, 0.5f, 0));
    driving_ = true;
    flying_ = false;
    recover_plane();
}

void Player::recover_plane() {
    if (plane_) plane_->reset(Vec3(Airport::plane_x, environment_.height(Airport::plane_x, Airport::runway_z)
        + Plane::parked_height, Airport::runway_z));
}

EntryVehicle Player::entry_vehicle() const {
    if (!on_foot()) return EntryVehicle::None;
    const Vec3 feet = character_.position(), eye = feet + Vec3(0, 1, 0);
    float nearest = 100;
    EntryVehicle result = EntryVehicle::None;
    const auto consider = [&](EntryVehicle kind, Vec3 position, Vec3 velocity, JPH::BodyID body,
                              float range, bool allowed) {
        const Vec3 difference = feet - position;
        const float distance = difference.Length();
        if (!allowed || velocity.Length() > 2.5f || distance > range || distance >= nearest
            || std::abs(difference.GetY()) > 1.8f) return;
        if (world_.camera_fraction(eye, position + Vec3(0, .7f, 0) - eye, body) < .98f) return;
        result = kind; nearest = distance;
    };
    consider(EntryVehicle::Car, car_.position(), car_.velocity(), car_.body_id(), 3.3f, true);
    if (plane_) consider(EntryVehicle::Plane, plane_->position(), plane_->velocity(), plane_->body_id(), 4.5f, plane_->grounded());
    return result;
}
bool Player::can_enter() const { return entry_vehicle() != EntryVehicle::None; }

Interaction Player::interact() {
    if (on_foot()) {
        const auto vehicle = entry_vehicle();
        if (vehicle == EntryVehicle::None) return Interaction::TooFar;
        driving_ = vehicle == EntryVehicle::Car;
        flying_ = vehicle == EntryVehicle::Plane;
        return Interaction::Entered;
    }
    if ((flying_ ? plane_->velocity() : car_.velocity()).Length() > 2.5f
        || (flying_ && !plane_->grounded())) return Interaction::TooFast;
    // Try both doors, then farther alongside and behind the car. Each exit
    // needs ground, free capsule space, and a clear path from the vehicle.
    const Vec3 car_exits[] = {Vec3(-2, 0, 0.35f), Vec3(2, 0, 0.35f),
        Vec3(-2.8f, 0, 0.35f), Vec3(2.8f, 0, 0.35f), Vec3(0, 0, 3.4f)};
    const Vec3 plane_exits[] = {Vec3(-1.9f, 0, -1.8f), Vec3(1.9f, 0, -1.8f),
        Vec3(-6.3f, 0, 0), Vec3(6.3f, 0, 0), Vec3(0, 0, 4.2f)};
    const auto& exits = flying_ ? plane_exits : car_exits;
    const Vec3 vehicle_position = position();
    const Quat rotation = flying_ ? plane_->rotation() : car_.rotation();
    const JPH::BodyID body = flying_ ? plane_->body_id() : car_.body_id();
    for (const auto& local : exits) {
        const Vec3 candidate = vehicle_position + rotation * local;
        GroundHit hit;
        if (!world_.cast_ground(candidate + Vec3(0, 3, 0), Vec3(0, -1, 0), 7, hit)
            || hit.normal.GetY() < 0.65f || hit.point.GetY() < Environment::water_level + 0.1f) continue;
        const Vec3 feet = hit.point + Vec3(0, 0.08f, 0);
        if (!character_.can_stand_at(feet)) continue;
        const Vec3 origin = vehicle_position + Vec3(0, 1, 0);
        if (world_.camera_fraction(origin, feet + Vec3(0, 1, 0) - origin, body) < 0.98f) continue;
        const Vec3 heading = forward();
        character_.reset(feet, std::atan2(-heading.GetX(), -heading.GetZ()));
        driving_ = false;
        flying_ = false;
        return Interaction::Exited;
    }
    return Interaction::Blocked;
}

void Player::step(Input driving, FootInput walking, float dt, FlightInput flight) {
    if (!driving_) driving = {0, 0, false, true};
    car_.step(driving, dt);
    if (plane_) {
        if (!flying_) flight = {0, 0, 0, 0, false, false, true};
        plane_->step(flight, dt);
    }
    world_.step(dt);
    if (on_foot()) character_.step(walking, dt);
}
} // namespace forza
