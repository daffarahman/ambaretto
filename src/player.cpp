#include "player.hpp"
#include "environment.hpp"
#include "airport.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include <cmath>

namespace forza {
Player::Player(PhysicsWorld& world, Car& car, const Environment& environment, Plane* plane, Traffic* traffic, Pedestrians* pedestrians)
    : world_(world), starter_car_(car), car_(&car), environment_(environment), character_(world), plane_(plane), traffic_(traffic),
      pedestrians_(pedestrians) { reset(); }

void Player::reset() {
    Vec3 spawn = environment_.spawn();
    // A stolen car must not recover on top of the starter or another car
    // still parked downtown. Search along the paved main avenue.
    const auto occupied = [&](Vec3 position) {
        const auto overlaps = [&](const Car& other) {
            if (&other == car_) return false;
            const Vec3 delta = other.position() - position;
            return std::abs(delta.GetY()) < 3 && std::hypot(delta.GetX(), delta.GetZ()) < 5;
        };
        if (overlaps(starter_car_)) return true;
        if (traffic_) for (const auto& vehicle : traffic_->cars()) if (overlaps(*vehicle.car)) return true;
        return false;
    };
    for (int i = 0; i < 30 && occupied(spawn); ++i) {
        const float z = environment_.spawn().GetZ() + (i % 2 == 0 ? 1 : -1) * (i / 2 + 1) * 8;
        spawn = Vec3(0, environment_.height(0, z) + .56f, z);
    }
    car_->set_simulated(true);
    car_->reset(spawn);
    character_.reset(spawn - Vec3(0, 0.5f, 0));
    character_.set_enabled(false);
    driving_ = true;
    flying_ = false;
    coasting_ = false;
    recover_plane();
}

void Player::recover_plane() {
    if (!plane_) return;
    const Airport* nearest = &airports.front();
    for (const auto& airport : airports)
        if (std::hypot(plane_->position().GetX() - airport.center_x, plane_->position().GetZ() - airport.runway_z)
            < std::hypot(plane_->position().GetX() - nearest->center_x, plane_->position().GetZ() - nearest->runway_z)) nearest = &airport;
    plane_->reset(Vec3(nearest->plane_x(), environment_.height(nearest->plane_x(), nearest->plane_z())
        + Plane::parked_height, nearest->plane_z()), nearest->yaw());
}

Player::EntryTarget Player::entry_target() const {
    if (!on_foot() || character_.ragdolling()) return {};
    const Vec3 feet = character_.position(), eye = feet + Vec3(0, 1, 0);
    float nearest = 100;
    EntryTarget result;
    const auto consider = [&](EntryVehicle kind, Vec3 position, Vec3 velocity, JPH::BodyID body,
                              float range, bool allowed, Car* car = nullptr) {
        const Vec3 difference = feet - position;
        const float distance = difference.Length();
        if (!allowed || velocity.Length() > 2.5f || distance > range || distance >= nearest
            || std::abs(difference.GetY()) > 1.8f) return;
        if (world_.camera_fraction(eye, position + Vec3(0, .7f, 0) - eye, body) < .98f) return;
        result = {kind, car}; nearest = distance;
    };
    consider(EntryVehicle::Car, starter_car_.position(), starter_car_.velocity(), starter_car_.body_id(), 3.3f, true, &starter_car_);
    if (traffic_) for (const auto& vehicle : traffic_->cars()) {
        auto* car = vehicle.car.get();
        consider(EntryVehicle::Car, car->position(), car->velocity(), car->body_id(), 3.3f, car->simulated(), car);
    }
    if (plane_) consider(EntryVehicle::Plane, plane_->position(), plane_->velocity(), plane_->body_id(), 4.5f, plane_->grounded());
    return result;
}
EntryVehicle Player::entry_vehicle() const { return entry_target().kind; }
Car* Player::entry_car() const { return entry_target().car; }
bool Player::can_steal() const { return traffic_ && traffic_->is_npc(entry_car()); }
bool Player::can_enter() const { return entry_vehicle() != EntryVehicle::None; }

Interaction Player::interact() {
    if (on_foot()) {
        const auto vehicle = entry_target();
        if (vehicle.kind == EntryVehicle::None) return Interaction::TooFar;
        if (vehicle.car) {
            car_ = vehicle.car;
            if (traffic_) traffic_->steal(*car_);
        }
        driving_ = vehicle.kind == EntryVehicle::Car;
        flying_ = vehicle.kind == EntryVehicle::Plane;
        coasting_ = false;
        character_.set_enabled(false);
        return Interaction::Entered;
    }
    const Vec3 inherited_velocity = flying_ ? plane_->velocity() : car_->velocity();
    const bool bailout = inherited_velocity.Length() > 2.5f || (flying_ && !plane_->grounded());
    // Try both doors, then farther alongside and behind the car. Each exit
    // needs ground, free capsule space, and a clear path from the vehicle.
    const Vec3 car_exits[] = {Vec3(-2, 0, 0.35f), Vec3(2, 0, 0.35f),
        Vec3(-2.8f, 0, 0.35f), Vec3(2.8f, 0, 0.35f), Vec3(0, 0, 3.4f)};
    const Vec3 plane_exits[] = {Vec3(-1.9f, 0, -1.8f), Vec3(1.9f, 0, -1.8f),
        Vec3(-6.3f, 0, 0), Vec3(6.3f, 0, 0), Vec3(0, 0, 4.2f)};
    const auto& exits = flying_ ? plane_exits : car_exits;
    const Vec3 vehicle_position = position();
    const Quat rotation = flying_ ? plane_->rotation() : car_->rotation();
    const JPH::BodyID body = flying_ ? plane_->body_id() : car_->body_id();
    for (const auto& local : exits) {
        const Vec3 candidate = vehicle_position + rotation * local;
        Vec3 feet = candidate - Vec3(0, .25f, 0);
        if (!bailout) {
            GroundHit hit;
            if (!world_.cast_ground(candidate + Vec3(0, 3, 0), Vec3(0, -1, 0), 7, hit)
                || hit.normal.GetY() < 0.65f || hit.point.GetY() < Environment::water_level + 0.1f) continue;
            feet = hit.point + Vec3(0, 0.08f, 0);
        }
        if (!character_.can_stand_at(feet)) continue;
        const Vec3 origin = vehicle_position + Vec3(0, 1, 0);
        if (world_.camera_fraction(origin, feet + Vec3(0, 1, 0) - origin, body) < 0.98f) continue;
        const Vec3 heading = forward();
        character_.reset(feet, std::atan2(-heading.GetX(), -heading.GetZ()));
        character_.set_enabled(true);
        if (bailout) character_.ragdoll(inherited_velocity + (rotation * local).Normalized() * 2 + Vec3(0, 1.2f, 0));
        coasting_ = !flying_ && bailout;
        driving_ = false;
        flying_ = false;
        return Interaction::Exited;
    }
    return Interaction::Blocked;
}

void Player::step(Input driving, FootInput walking, float dt, FlightInput flight) {
    if (coasting_ && car_->velocity().Length() < .5f) coasting_ = false;
    if (!driving_) driving = {0, 0, false, !coasting_};
    driving.player_controlled = driving_ || coasting_;
    starter_car_.step(car_ == &starter_car_ ? driving : Input{0, 0, false, true}, dt);
    if (car_ != &starter_car_ && (driving_ || coasting_)) car_->step(driving, dt);
    if (plane_) {
        if (!flying_) flight = {0, 0, 0, 0, false, false, true};
        plane_->step(flight, dt);
    }
    if (traffic_) {
        const Vec3 feet = character_.position();
        traffic_->step(driving_ || coasting_ ? car_ : nullptr, starter_car_, plane_, on_foot() ? &feet : nullptr, position(), dt);
    }
    if (on_foot()) {
        character_.hit_by(starter_car_, dt);
        if (traffic_) for (const auto& vehicle : traffic_->cars()) character_.hit_by(*vehicle.car, dt);
    }
    if (pedestrians_) pedestrians_->prepare(starter_car_, traffic_, position(), dt);
    world_.step(dt);
    if (on_foot()) character_.step(walking, dt);
    if (pedestrians_) pedestrians_->step(starter_car_, traffic_, position(), dt);
}
} // namespace forza
