#include "player.hpp"
#include "environment.hpp"
#include "airport.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include "police.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
Player::Player(PhysicsWorld& world, Car& car, const Environment& environment, Plane* plane, Traffic* traffic, Pedestrians* pedestrians,
    const std::vector<std::unique_ptr<Plane>>* aircraft, Police* police)
    : world_(world), starter_car_(car), car_(&car), environment_(environment), character_(world, &environment, true), plane_(plane), traffic_(traffic),
      pedestrians_(pedestrians), police_(police) { starter_plane_ = plane; aircraft_ = aircraft; reset(); }

void Player::reset() {
    world_.take_player_kill();
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
    car_->repair();
    character_.reset(spawn - Vec3(0, 0.5f, 0));
    character_.revive();
    weapons_.reset();
    character_.set_enabled(false);
    driving_ = true;
    flying_ = false;
    coasting_ = false;
    recover_plane();
}

void Player::respawn_on_foot(Vec3 feet, float yaw) {
    world_.take_player_kill();
    driving_ = flying_ = coasting_ = false;
    character_.reset(feet, yaw);
    character_.revive();
    weapons_.reset();
}

void Player::respawn_near(Vec3 origin) {
    if (!std::isfinite(origin.GetX()) || !std::isfinite(origin.GetY()) || !std::isfinite(origin.GetZ())) origin = environment_.spawn();
    const auto try_ground = [&](Vec3 point) {
        if (std::abs(point.GetX()) > Environment::extent - 40 || std::abs(point.GetZ()) > Environment::extent - 40) return false;
        point.SetY(environment_.surface_height(point));
        if (point.GetY() <= Environment::water_level + .1f) return false;
        GroundHit ground;
        if (!world_.cast_ground(point + Vec3(0, 3, 0), Vec3(0, -1, 0), 6, ground)
            || ground.normal.GetY() < .65f || ground.point.GetY() <= Environment::water_level + .1f) return false;
        const Vec3 feet = ground.point + Vec3(0, .08f, 0);
        if (!character_.can_stand_at(feet)) return false;
        respawn_on_foot(feet, character_.yaw());
        return true;
    };
    for (float radius : {18.f, 28.f, 40.f, 60.f, 90.f, 140.f, 220.f}) for (int i = 0; i < 16; ++i) {
        const float angle = i * .39269908f;
        if (try_ground(origin + Vec3(std::cos(angle) * radius, 0, std::sin(angle) * radius))) return;
    }
    // Offshore deaths use the closest safe road, including bridge decks.
    std::vector<Vec3> roads;
    for (const auto& road : environment_.roads()) {
        Vec3 direction = road.b - road.a; direction.SetY(0);
        const float t = std::clamp((origin - road.a).Dot(direction) / std::max(.001f, direction.LengthSq()), 0.f, 1.f);
        Vec3 point = road.a + direction * t;
        point.SetY(environment_.road_height(road, point.GetX(), point.GetZ()));
        roads.push_back(point);
    }
    const auto distance = [&](Vec3 point) { return std::hypot(point.GetX() - origin.GetX(), point.GetZ() - origin.GetZ()); };
    std::sort(roads.begin(), roads.end(), [&](Vec3 a, Vec3 b) { return distance(a) < distance(b); });
    for (auto point : roads) if (try_ground(point)) return;
    respawn_on_foot(environment_.spawn() - Vec3(0, .48f, 0));
}

void Player::recover_plane() {
    if (!plane_) return;
    const Airport* nearest = &airports.front();
    for (const auto& airport : airports)
        if (std::hypot(plane_->position().GetX() - airport.center_x, plane_->position().GetZ() - airport.runway_z)
            < std::hypot(plane_->position().GetX() - nearest->center_x, plane_->position().GetZ() - nearest->runway_z)) nearest = &airport;
    for (int i = 0; i < 20; ++i) {
        const auto p = nearest->point(nearest->plane_along() + i * (plane_->specs().length + 15));
        const float radius = std::max(plane_->specs().length, plane_->specs().span) / 2 + 2;
        const auto occupied = [&](const Plane* other) {
            return other && other != plane_ && std::hypot(other->position().GetX() - p.x, other->position().GetZ() - p.z)
                < radius + std::max(other->specs().length, other->specs().span) / 2;
        };
        bool blocked = occupied(starter_plane_);
        if (aircraft_) for (const auto& other : *aircraft_) blocked |= occupied(other.get());
        if (blocked) continue;
        plane_->reset(Vec3(p.x, environment_.height(p.x, p.z) + plane_->parking_height(), p.z), nearest->yaw());
        plane_->repair();
        return;
    }
}

Player::EntryTarget Player::entry_target() const {
    if (!on_foot() || character_.ragdolling()) return {};
    const Vec3 feet = character_.position(), eye = feet + Vec3(0, 1, 0);
    float nearest = 100;
    EntryTarget result;
    const auto consider = [&](EntryVehicle kind, Vec3 position, Vec3 velocity, JPH::BodyID body,
                              float range, bool allowed, Car* car = nullptr, Plane* plane = nullptr) {
        const Vec3 difference = feet - position;
        const float distance = difference.Length();
        if (!allowed || (position.GetY() < Environment::water_level + .1f
            && environment_.terrain_height(position.GetX(), position.GetZ()) < Environment::water_level - 1)
            || velocity.Length() > 2.5f || distance > range || distance >= nearest
            || std::abs(difference.GetY()) > 1.8f) return;
        if (world_.camera_fraction(eye, position + Vec3(0, .7f, 0) - eye, body) < .98f) return;
        result = {kind, car, plane}; nearest = distance;
    };
    consider(EntryVehicle::Car, starter_car_.position(), starter_car_.velocity(), starter_car_.body_id(), 3.3f, !starter_car_.destroyed(), &starter_car_);
    if (traffic_) for (const auto& vehicle : traffic_->cars()) {
        auto* car = vehicle.car.get();
        consider(EntryVehicle::Car, car->position(), car->velocity(), car->body_id(), 3.3f, car->simulated() && !car->destroyed(), car);
    }
    if (police_) for (const auto& unit : police_->units()) if (unit.active) {
        consider(EntryVehicle::Car, unit.car->position(), unit.car->velocity(), unit.car->body_id(), 3.3f, !unit.car->destroyed(), unit.car.get());
    }
    const auto consider_plane = [&](Plane* plane) {
        if (plane) consider(EntryVehicle::Plane, plane->boarding_position(), plane->velocity(), plane->body_id(), 2.5f,
            !plane->destroyed() && plane->grounded() && plane->position().GetY() > Environment::water_level + .1f, nullptr, plane);
    };
    consider_plane(starter_plane_);
    if (aircraft_) for (const auto& plane : *aircraft_) consider_plane(plane.get());
    return result;
}
EntryVehicle Player::entry_vehicle() const { return entry_target().kind; }
Car* Player::entry_car() const { return entry_target().car; }
bool Player::can_steal() const { return traffic_ && traffic_->is_npc(entry_car()); }
bool Player::can_enter() const { return entry_vehicle() != EntryVehicle::None; }

Interaction Player::interact() {
    if (police_ && police_->arrested()) return Interaction::Blocked;
    if (!character_.alive() || (on_foot() && character_.ragdolling())) return Interaction::Blocked;
    if (on_foot()) {
        const auto vehicle = entry_target();
        if (vehicle.kind == EntryVehicle::None) return Interaction::TooFar;
        if (vehicle.car) {
            const bool stolen = traffic_ && traffic_->is_npc(vehicle.car);
            if (traffic_ && !traffic_->steal(*vehicle.car)) return Interaction::Blocked;
            if (police_) {
                if (vehicle.car->type() == CarType::Police) { if (!police_->steal(*vehicle.car)) return Interaction::Blocked; }
                else if (stolen) police_->crime(Crime::VehicleTheft, character_.position());
            }
            car_ = vehicle.car;
        }
        if (vehicle.plane) plane_ = vehicle.plane;
        driving_ = vehicle.kind == EntryVehicle::Car;
        flying_ = vehicle.kind == EntryVehicle::Plane;
        coasting_ = false;
        character_.set_enabled(false);
        return Interaction::Entered;
    }
    const Vec3 inherited_velocity = flying_ ? plane_->velocity() : car_->velocity();
    const bool water_exit = position().GetY() < Environment::water_level + .1f
        && environment_.terrain_height(position().GetX(), position().GetZ()) < Environment::water_level - 1;
    const bool bailout = inherited_velocity.Length() > 2.5f || (flying_ && !plane_->grounded());
    // Try both doors, then farther alongside and behind the vehicle.
    // Water exits also search around submerged wrecks for free surface space.
    const Vec3 car_exits[] = {Vec3(-2, 0, 0.35f), Vec3(2, 0, 0.35f),
        Vec3(-2.8f, 0, 0.35f), Vec3(2.8f, 0, 0.35f), Vec3(0, 0, 3.4f)};
    const auto plane_exits = plane_ ? plane_->exit_offsets() : std::array<Vec3, 5>{};
    const Vec3 vehicle_position = position();
    const Vec3 heading = forward();
    const float yaw = std::atan2(-heading.GetX(), -heading.GetZ());
    const Quat rotation = water_exit ? Quat::sRotation(Vec3::sAxisY(), yaw) : flying_ ? plane_->rotation() : car_->rotation();
    const JPH::BodyID body = flying_ ? plane_->body_id() : car_->body_id();
    for (int i = 0; i < (water_exit ? 37 : 5); ++i) {
        const float angle = (i - 5) * .78539816f, radius = 3 + ((i - 5) / 8) * 2;
        const Vec3 local = i < 5 ? (flying_ ? plane_exits[i] : car_exits[i]) : Vec3(std::cos(angle) * radius, 0, std::sin(angle) * radius);
        const Vec3 candidate = vehicle_position + rotation * local;
        Vec3 feet = candidate - Vec3(0, .25f, 0);
        if (water_exit) {
            if (environment_.terrain_height(candidate.GetX(), candidate.GetZ()) > Environment::water_level - 1) continue;
            feet.SetY(Environment::water_level - 1.25f);
        } else if (!bailout) {
            GroundHit hit;
            if (!world_.cast_ground(candidate + Vec3(0, 3, 0), Vec3(0, -1, 0), flying_ ? plane_->parking_height() + 4 : 7, hit)
                || hit.normal.GetY() < 0.65f || hit.point.GetY() < Environment::water_level + 0.1f) continue;
            feet = hit.point + Vec3(0, 0.08f, 0);
        }
        if (!character_.can_stand_at(feet)) continue;
        const Vec3 origin = vehicle_position + Vec3(0, 1, 0);
        if (!water_exit && world_.camera_fraction(origin, feet + Vec3(0, 1, 0) - origin, body) < 0.98f) continue;
        character_.reset(feet, yaw);
        character_.set_enabled(true);
        if (water_exit) character_.start_swimming(feet, yaw);
        else if (bailout) character_.ragdoll(inherited_velocity + (rotation * local).Normalized() * 2 + Vec3(0, 1.2f, 0));
        coasting_ = !flying_ && bailout;
        driving_ = false;
        flying_ = false;
        return Interaction::Exited;
    }
    return Interaction::Blocked;
}

void Player::step(Input driving, FootInput walking, float dt, FlightInput flight) {
    if (police_ && police_->arrested()) { walking = {}; driving = {0, 0, false, true}; }
    weapons_.step(dt);
    if (coasting_ && car_->velocity().Length() < .5f) coasting_ = false;
    if (!driving_) driving = {0, 0, false, !coasting_};
    driving.player_controlled = driving_ || coasting_;
    starter_car_.step(car_ == &starter_car_ ? driving : Input{0, 0, false, true}, dt);
    if (car_ != &starter_car_) car_->step(driving, dt);
    if (plane_) {
        if (!flying_) flight = {0, 0, 0, 0, false, false, true};
        plane_->step(flight, dt);
    }
    if (starter_plane_ && starter_plane_ != plane_) starter_plane_->step({0, 0, 0, 0, false, false, true}, dt);
    if (aircraft_) for (const auto& plane : *aircraft_) if (plane.get() != plane_)
        plane->step({0, 0, 0, 0, false, false, true}, dt);
    if (traffic_) {
        const Vec3 feet = character_.position();
        traffic_->step(car_, starter_car_, plane_, on_foot() ? &feet : nullptr, position(), dt, police_);
    }
    if (on_foot()) {
        character_.hit_by(starter_car_, dt);
        if (traffic_) for (const auto& vehicle : traffic_->cars()) character_.hit_by(*vehicle.car, dt);
    }
    if (pedestrians_) pedestrians_->prepare(starter_car_, traffic_, position(), dt);
    if (police_) {
        police_->prepare(*this, dt);
        if (on_foot()) for (const auto& unit : police_->units()) if (unit.active) character_.hit_by(*unit.car, dt);
    }
    world_.step(dt);
    if (driving_ && car_->destroyed()) {
        if (!character_.pull_from(*car_)) character_.reset(car_->position() + Vec3(0, 2, 0));
        driving_ = coasting_ = false;
        character_.take_damage(100);
    }
    if (flying_ && plane_->destroyed()) {
        character_.reset(plane_->position() + plane_->rotate(plane_->exit_offsets()[0]));
        character_.ragdoll(plane_->velocity());
        flying_ = false;
        character_.take_damage(100);
    }
    if (!on_foot() && position().GetY() < Environment::water_level + .1f
        && environment_.terrain_height(position().GetX(), position().GetZ()) < Environment::water_level - 1)
        interact();
    if (on_foot()) {
        walking.weapon = weapons_.selected();
        character_.step(walking, dt);
    }
    if (pedestrians_) pedestrians_->step(starter_car_, traffic_, position(), dt);
    if (traffic_) traffic_->finish(position(), dt);
    if (police_) police_->finish(*this, dt);
    const Vec3 velocity = driving_ ? car_->velocity() : flying_ ? plane_->velocity() : character_.velocity();
    const bool idle = on_foot() ? walking.direction.LengthSq() < .0001f && !walking.jump && character_.grounded()
        && !character_.ragdolling() && !character_.swimming()
        : driving_ ? std::abs(driving.throttle) < .01f : plane_->grounded() && std::abs(flight.throttle) < .01f;
    const float speed_squared = on_foot() ? velocity.GetX() * velocity.GetX() + velocity.GetZ() * velocity.GetZ() : velocity.LengthSq();
    if (idle && speed_squared < .04f && (!police_ || !police_->arrested())) character_.regenerate(dt);
}

bool Player::can_shoot() const {
    return on_foot() && character_.alive() && !character_.ragdolling() && !character_.swimming() && (!police_ || !police_->arrested());
}
Shot Player::shoot(Vec3 origin, Vec3 direction, bool held, bool pressed, bool aiming) {
    if (!can_shoot() || (character_.covering() && !character_.cover_aim_ready())) return {};
    const auto shot = weapons_.fire(world_, pedestrians_, origin, direction, held, pressed, aiming, police_, &character_);
    if (police_ && shot.fired) {
        police_->crime(Crime::Gunfire, position());
        if (shot.injured) police_->crime(police_->is_officer(shot.victim)
            ? (shot.killed ? Crime::OfficerHomicide : Crime::OfficerAssault)
            : (shot.killed ? Crime::Homicide : Crime::Assault), position(), shot.victim);
    }
    return shot;
}
} // namespace forza
