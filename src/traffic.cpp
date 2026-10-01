#include "traffic.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace forza {
namespace {
constexpr float lane_offset = 4;
Vec3 flat(Vec3 value) { value.SetY(0); return value; }
Vec3 right(Vec3 direction) { return direction.Cross(Vec3::sAxisY()); }
float yaw(Vec3 direction) { return std::atan2(-direction.GetX(), -direction.GetZ()); }

std::vector<Vec3> lane_route(std::vector<Vec3> corners, bool reverse = false) {
    if (reverse) std::reverse(corners.begin(), corners.end());
    std::vector<Vec3> lane, result;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec3 incoming = (corners[i] - corners[(i + corners.size() - 1) % corners.size()]).Normalized();
        const Vec3 outgoing = (corners[(i + 1) % corners.size()] - corners[i]).Normalized();
        lane.push_back(corners[i] + (right(incoming) + right(outgoing)) *
            (lane_offset / std::max(.5f, 1 + incoming.Dot(outgoing))));
    }
    for (std::size_t i = 0; i < lane.size(); ++i) {
        const Vec3 incoming = (lane[i] - lane[(i + lane.size() - 1) % lane.size()]).Normalized();
        const Vec3 outgoing = (lane[(i + 1) % lane.size()] - lane[i]).Normalized();
        const Vec3 start = lane[i] - incoming * 8, end = lane[i] + outgoing * 8;
        for (int j = 0; j < 8; ++j) {
            const float t = j / 8.0f;
            result.push_back(start * ((1 - t) * (1 - t)) + lane[i] * (2 * t * (1 - t)) + end * (t * t));
        }
        const Vec3 next = lane[(i + 1) % lane.size()] - outgoing * 8;
        const int steps = std::max(1, int((next - end).Length() / 8));
        for (int j = 0; j < steps; ++j) result.push_back(end + (next - end) * (float(j) / steps));
    }
    return result;
}
std::vector<Vec3> rectangle(float west, float north, float east, float south, bool reverse) {
    return lane_route({{west, 0, north}, {east, 0, north}, {east, 0, south}, {west, 0, south}}, reverse);
}
} // namespace

Traffic::Traffic(PhysicsWorld& world, const Environment& environment) : world_(world), environment_(environment) {
    for (bool reverse : {false, true}) {
        routes_.push_back(rectangle(0, -200, 200, 200, reverse));
        for (float x : {-1000.0f, -600.0f, -200.0f})
            for (float z : {-1000.0f, -600.0f, -200.0f, 200.0f, 600.0f})
                routes_.push_back(rectangle(x, z, x + 400, z + 400, reverse));
        for (float x : {1900.0f, 2100.0f, 2300.0f})
            for (float z : {-800.0f, -400.0f, 0.0f, 400.0f})
                routes_.push_back(rectangle(x, z, x == 2300 ? 2450 : x + 200, z + 400, reverse));
        for (std::size_t i = 6; i < environment.islands().size(); ++i) {
            const auto c = environment.islands()[i].center;
            routes_.push_back(rectangle(c.GetX() - 200, c.GetZ() - 200, c.GetX() + 200, c.GetZ() + 200, reverse));
        }
    }
    routes_.push_back(lane_route({{0, 0, 400}, {1100, 0, 400}, {2100, 0, 400}, {2100, 0, -400}, {0, 0, -400}}));
    routes_.push_back(lane_route({{0, 0, 400}, {1100, 0, 400}, {2100, 0, 400}, {2100, 0, -400}, {0, 0, -400}}, true));
    // The Overseas Highway goes through every Key and every connecting deck.
    // Both ends turn around on connected village/city blocks.
    std::vector<Vec3> spine{{-600, 0, 1000}, {-600, 0, 1500}, {-600, 0, 2100}, {-600, 0, 2500},
        {-600, 0, 2900}, {-900, 0, 3400}, {-1300, 0, 3700}, {-1400, 0, 3900},
        {-1800, 0, 4650}, {-2200, 0, 4900}, {-2400, 0, 5150}, {-2650, 0, 5900},
        {-2900, 0, 6200}, {-3150, 0, 6450}, {-3300, 0, 6950}, {-3500, 0, 7300}};
    auto highway = spine;
    highway.insert(highway.end(), {{-3500, 0, 7500}, {-3300, 0, 7500}, {-3300, 0, 7300}, {-3500, 0, 7300}});
    for (std::size_t i = spine.size() - 1; i-- > 0;) highway.push_back(spine[i]);
    highway.insert(highway.end(), {{-800, 0, 1000}, {-800, 0, 800}, {-600, 0, 800}});
    routes_.push_back(lane_route(highway));
    for (std::size_t r = 0; r < routes_.size(); ++r) {
        const auto& route = routes_[r];
        const int count = r == routes_.size() - 1 ? 72 : r >= routes_.size() - 3 ? 12 : 3;
        for (int i = 0; i < count; ++i) {
            std::size_t start = (route.size() * (i * 2 + 1) / (count * 2) + r * 17) % route.size();
            if (r == 0 && i == 0) start = locate(route, Vec3(lane_offset, 0, 88)).segment;
            auto car = std::make_unique<Car>(world);
            car->set_simulated(false);
            const Vec3 p = route[start];
            car->reset(Vec3(p.GetX(), environment.height(p.GetX(), p.GetZ()) + .56f, p.GetZ()),
                yaw(route[(start + 1) % route.size()] - p));
            TrafficCar vehicle;
            vehicle.car = std::move(car); vehicle.route = r; vehicle.segment = start; vehicle.point = p;
            cars_.push_back(std::move(vehicle));
        }
    }
    // Make nearby cars physical before the first interaction / preview.
    stream(environment.spawn(), nullptr, nullptr);
}
bool Traffic::is_npc(const Car* car) const {
    for (const auto& vehicle : cars_) if (vehicle.car.get() == car) return vehicle.npc;
    return false;
}
void Traffic::steal(Car& car) {
    car.set_simulated(true);
    for (auto& vehicle : cars_) if (vehicle.car.get() == &car) vehicle.npc = false;
}
Traffic::RouteLocation Traffic::locate(const std::vector<Vec3>& route, Vec3 position) const {
    RouteLocation nearest{0, route.front(), std::numeric_limits<float>::max()};
    position = flat(position);
    for (std::size_t i = 0; i < route.size(); ++i) {
        const Vec3 segment = route[(i + 1) % route.size()] - route[i];
        const float t = std::clamp((position - route[i]).Dot(segment) / std::max(segment.LengthSq(), .001f), 0.0f, 1.0f);
        const Vec3 point = route[i] + segment * t;
        const float distance = (point - position).LengthSq();
        if (distance < nearest.distance) nearest = {i, point, distance};
    }
    nearest.distance = std::sqrt(nearest.distance);
    return nearest;
}
Traffic::RouteLocation Traffic::advance(const std::vector<Vec3>& route, RouteLocation location, float distance) const {
    for (std::size_t count = 0; count < route.size(); ++count) {
        const std::size_t next = (location.segment + 1) % route.size();
        const Vec3 segment = route[next] - location.point;
        const float length = segment.Length();
        if (length >= distance && length > .001f) {
            location.point += segment * (distance / length); return location;
        }
        distance -= length; location.point = route[next]; location.segment = next;
    }
    return location;
}
Vec3 Traffic::ahead(const std::vector<Vec3>& route, RouteLocation location, float distance) const {
    return advance(route, location, distance).point;
}
void Traffic::stream(Vec3 player_position, const Car* starter, const Plane* plane) {
    std::vector<std::pair<float, std::size_t>> nearby;
    for (std::size_t i = 0; i < cars_.size(); ++i) if (cars_[i].npc) {
        const float distance = flat(cars_[i].car->position() - player_position).LengthSq();
        if (distance < 700 * 700) nearby.push_back({distance, i});
    }
    std::sort(nearby.begin(), nearby.end());
    std::vector<bool> selected(cars_.size(), false);
    for (std::size_t i = 0; i < std::min<std::size_t>(48, nearby.size()); ++i) selected[nearby[i].second] = true;
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        auto& vehicle = cars_[i]; Car& car = *vehicle.car;
        if (!vehicle.npc || selected[i] == car.simulated()) continue;
        if (selected[i]) {
            const Vec3 position = car.position();
            // Never create a chassis inside an occupied lane or pedestrian.
            if (flat(position - player_position).LengthSq() < 3 * 3) continue;
            if (starter && flat(position - starter->position()).LengthSq() < 6 * 6) continue;
            if (plane && (position - plane->position()).LengthSq() < 12 * 12) continue;
            bool occupied = false;
            for (const auto& other : cars_) if (other.car.get() != &car && other.car->simulated()
                && flat(position - other.car->position()).LengthSq() < 6 * 6) { occupied = true; break; }
            if (occupied) continue;
            car.set_simulated(true); vehicle.plan_time = 0; vehicle.stuck_time = 0;
        } else {
            const auto location = locate(routes_[vehicle.route], car.position());
            vehicle.point = location.point; vehicle.segment = location.segment;
            car.set_simulated(false);
        }
    }
}
bool Traffic::space_available(Vec3 position, const Car& ignore, const Car& starter_car,
                              const Plane* plane, Vec3 player_position) const {
    if (flat(position - player_position).LengthSq() < 35 * 35 || flat(position - starter_car.position()).LengthSq() < 10 * 10) return false;
    if (plane && (position - plane->position()).LengthSq() < 12 * 12) return false;
    for (const auto& vehicle : cars_)
        if (vehicle.car.get() != &ignore && vehicle.car->simulated() && flat(position - vehicle.car->position()).LengthSq() < 10 * 10) return false;
    return true;
}
void Traffic::step(Car* controlled, const Car& starter_car, const Plane* plane,
                   const Vec3* pedestrian, Vec3 player_position, float dt) {
    stream_time_ -= dt;
    const bool sync = stream_time_ <= 0;
    if (sync) { stream(player_position, &starter_car, plane); stream_time_ = .5f; }
    for (auto& vehicle : cars_) {
        Car& car = *vehicle.car;
        if (&car == controlled) continue;
        if (!vehicle.npc) { car.step({0, 0, false, true}, dt); continue; }
        const auto& route = routes_[vehicle.route];
        if (!car.simulated()) {
            const auto location = advance(route, {vehicle.segment, vehicle.point, 0}, dt * 9);
            vehicle.segment = location.segment; vehicle.point = location.point;
            if (sync) car.reset(Vec3(location.point.GetX(), environment_.height(location.point.GetX(), location.point.GetZ()) + .56f,
                location.point.GetZ()), yaw(ahead(route, location, 2) - location.point));
            continue;
        }
        vehicle.plan_time -= dt;
        if (vehicle.plan_time > 0) { car.step(vehicle.input, dt); continue; }
        constexpr float plan_dt = .05f;
        vehicle.plan_time = plan_dt;
        const Vec3 position = car.position(), direction = flat(car.forward()).NormalizedOr(Vec3(0, 0, -1));
        const float speed = flat(car.velocity()).Length();
        const auto location = locate(route, position);
        const Vec3 delta = flat(ahead(route, location, 3.5f + speed * .55f) - position);
        const float curvature = 2 * delta.Dot(-right(direction)) / std::max(delta.LengthSq(), 1.0f);
        const float angle = std::atan(car.tuning().wheelbase * curvature);
        const float steer_limit = std::min(car.tuning().max_steer, std::atan(car.tuning().wheelbase * 16 / std::max(speed * speed, 1.0f)));
        float desired_speed = std::min(vehicle.route >= routes_.size() - 3 ? 11.0f : 8.0f,
            std::sqrt(2.8f / std::max(std::abs(curvature), .001f)));
        const auto avoid = [&](Vec3 obstacle, float clearance) {
            const Vec3 difference = flat(obstacle - position);
            if (std::abs(obstacle.GetY() - position.GetY()) > 3) return;
            const float along = difference.Dot(direction), side = std::abs(difference.Dot(right(direction)));
            if (along > -1 && side < clearance)
                desired_speed = std::min(desired_speed, std::max(0.0f, (along - 6) * .6f));
        };
        avoid(starter_car.position(), 2.6f);
        for (const auto& other : cars_) if (other.car.get() != &car && other.car->simulated()) avoid(other.car->position(), 2.6f);
        if (plane) avoid(plane->position(), 7);
        const bool yielding = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).LengthSq() < 9 * 9;
        if (yielding) desired_speed = 0;
        const float obstacle_distance = 4 + speed * .8f;
        const float clear = world_.camera_fraction(position + Vec3(0, 1, 0), direction * obstacle_distance, car.body_id());
        if (clear < .98f) desired_speed = std::min(desired_speed, std::max(0.0f, (clear * obstacle_distance - 3) * .6f));
        const bool brake = desired_speed < .1f || speed > desired_speed + .5f;
        const float throttle = brake ? 0 : std::clamp((desired_speed + (desired_speed - speed) * .8f) / car.tuning().top_speed, 0.0f, 1.0f);
        vehicle.input = {throttle, std::clamp(angle / steer_limit, -1.0f, 1.0f), false, brake};
        car.step(vehicle.input, dt);
        const bool lost = location.distance > 8 || car.rotate(Vec3::sAxisY()).GetY() < .35f || environment_.submerged(position);
        vehicle.stuck_time = lost || (speed < .3f && !yielding) ? vehicle.stuck_time + plan_dt : 0;
        if (vehicle.stuck_time > 8 && flat(position - player_position).LengthSq() > 35 * 35) {
            const Vec3 recovery = ahead(route, location, 18);
            if (space_available(recovery, car, starter_car, plane, player_position)) {
                car.reset(Vec3(recovery.GetX(), environment_.height(recovery.GetX(), recovery.GetZ()) + .56f, recovery.GetZ()),
                    yaw(ahead(route, locate(route, recovery), 2) - recovery));
                vehicle.stuck_time = 0;
            }
        }
    }
}
} // namespace forza
