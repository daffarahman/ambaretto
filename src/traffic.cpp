#include "traffic.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace forza {
namespace {
constexpr float pi = 3.14159265f;
constexpr float lane_offset = 2.5f;
Vec3 flat(Vec3 value) { value.SetY(0); return value; }
Vec3 right(Vec3 direction) { return direction.Cross(Vec3::sAxisY()); }
float yaw(Vec3 direction) { return std::atan2(-direction.GetX(), -direction.GetZ()); }

std::vector<Vec3> city_route(float west, float north, float east, float south, bool reverse) {
    std::vector<Vec3> corners{{west, 0, north}, {east, 0, north}, {east, 0, south}, {west, 0, south}};
    if (reverse) std::reverse(corners.begin(), corners.end());
    // Offset each street into its right-hand lane, then round the intersection
    // corners. The whole turn fits inside the paved 12-meter road footprint.
    std::vector<Vec3> lane;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec3 incoming = (corners[i] - corners[(i + 3) % 4]).Normalized();
        const Vec3 outgoing = (corners[(i + 1) % 4] - corners[i]).Normalized();
        lane.push_back(corners[i] + (right(incoming) + right(outgoing)) * lane_offset);
    }
    std::vector<Vec3> result;
    constexpr float radius = 4;
    for (std::size_t i = 0; i < lane.size(); ++i) {
        const Vec3 incoming = (lane[i] - lane[(i + 3) % 4]).Normalized();
        const Vec3 outgoing = (lane[(i + 1) % 4] - lane[i]).Normalized();
        const Vec3 start = lane[i] - incoming * radius;
        const Vec3 center = start + outgoing * radius;
        for (int j = 0; j <= 8; ++j) {
            const float angle = j * pi / 16;
            result.push_back(center - outgoing * (radius * std::cos(angle)) + incoming * (radius * std::sin(angle)));
        }
        const Vec3 end = lane[i] + outgoing * radius;
        const Vec3 next = lane[(i + 1) % 4] - outgoing * radius;
        const int steps = int((next - end).Length() / 2) + 1;
        for (int j = 1; j < steps; ++j) result.push_back(end + (next - end) * (float(j) / steps));
    }
    return result;
}

std::vector<Vec3> coastal_route(bool reverse) {
    const auto center = [](float angle) {
        const float outline = 1 + .045f * std::sin(3 * angle + .4f) + .025f * std::cos(5 * angle);
        return Vec3(320 * .72f * outline * std::cos(angle), 0, 280 * .72f * outline * std::sin(angle));
    };
    std::vector<Vec3> result;
    for (int i = 0; i < 480; ++i) {
        const float a = (reverse ? -1 : 1) * i * 2 * pi / 480;
        const Vec3 direction = (center(a + (reverse ? -.001f : .001f)) - center(a)).Normalized();
        result.push_back(center(a) + right(direction) * lane_offset);
    }
    return result;
}
} // namespace

Traffic::Traffic(PhysicsWorld& world, const Environment& environment) : world_(world), environment_(environment) {
    for (bool reverse : {false, true}) {
        routes_.push_back(city_route(0, -60, 60, 120, reverse));
        routes_.push_back(city_route(-120, -120, 0, 60, reverse));
        routes_.push_back(city_route(-60, 0, 120, 120, reverse));
        routes_.push_back(city_route(0, -120, 120, 0, reverse));
        routes_.push_back(coastal_route(reverse));
    }
    for (std::size_t r = 0; r < routes_.size(); ++r) {
        const auto& route = routes_[r];
        const int count = r % 5 == 4 ? 3 : 1;
        for (int i = 0; i < count; ++i) {
            std::size_t start = (route.size() * (i * 2 + 1) / (count * 2) + r * 17) % route.size();
            // Keep the initial downtown car in view of the player spawn.
            if (r == 0) start = locate(route, Vec3(-lane_offset, 0, 88)).segment;
            const Vec3 p = route[start];
            auto car = std::make_unique<Car>(world);
            car->reset(Vec3(p.GetX(), environment.height(p.GetX(), p.GetZ()) + .56f, p.GetZ()),
                       yaw(route[(start + 1) % route.size()] - p));
            cars_.push_back({std::move(car), true, r, 0});
        }
    }
}

bool Traffic::is_npc(const Car* car) const {
    for (const auto& vehicle : cars_) if (vehicle.car.get() == car) return vehicle.npc;
    return false;
}
void Traffic::steal(Car& car) {
    for (auto& vehicle : cars_) if (vehicle.car.get() == &car) vehicle.npc = false;
}

Traffic::RouteLocation Traffic::locate(const std::vector<Vec3>& route, Vec3 position) const {
    RouteLocation nearest{0, route.front(), std::numeric_limits<float>::max()};
    position = flat(position);
    for (std::size_t i = 0; i < route.size(); ++i) {
        const Vec3 segment = route[(i + 1) % route.size()] - route[i];
        const float t = std::clamp((position - route[i]).Dot(segment) / segment.LengthSq(), 0.0f, 1.0f);
        const Vec3 point = route[i] + segment * t;
        const float distance = (point - position).Length();
        if (distance < nearest.distance) nearest = {i, point, distance};
    }
    return nearest;
}
Vec3 Traffic::ahead(const std::vector<Vec3>& route, RouteLocation location, float distance) const {
    Vec3 start = location.point;
    for (std::size_t count = 0; count < route.size(); ++count) {
        const std::size_t next = (location.segment + 1) % route.size();
        const Vec3 segment = route[next] - start;
        const float length = segment.Length();
        if (length >= distance && length > .001f) return start + segment * (distance / length);
        distance -= length;
        start = route[next]; location.segment = next;
    }
    return start;
}

bool Traffic::space_available(Vec3 position, const Car& ignore, const Car& starter_car,
                              const Plane* plane, Vec3 player_position) const {
    if ((flat(position - player_position)).Length() < 35 || (flat(position - starter_car.position())).Length() < 10) return false;
    if (plane && (position - plane->position()).Length() < 12) return false;
    for (const auto& vehicle : cars_)
        if (vehicle.car.get() != &ignore && flat(position - vehicle.car->position()).Length() < 10) return false;
    return true;
}

void Traffic::step(Car* controlled, const Car& starter_car, const Plane* plane,
                   const Vec3* pedestrian, Vec3 player_position, float dt) {
    for (auto& vehicle : cars_) {
        Car& car = *vehicle.car;
        if (&car == controlled) continue;
        if (!vehicle.npc) { car.step({0, 0, false, true}, dt); continue; }
        const auto& route = routes_[vehicle.route];
        const Vec3 position = car.position(), direction = flat(car.forward()).NormalizedOr(Vec3(0, 0, -1));
        const float speed = flat(car.velocity()).Length();
        const auto location = locate(route, position);
        const float lookahead = 3.5f + speed * .55f;
        const Vec3 target = ahead(route, location, lookahead);
        const Vec3 delta = flat(target - position);
        const float curvature = 2 * delta.Dot(-right(direction)) / std::max(delta.LengthSq(), 1.0f);
        const float angle = std::atan(car.tuning().wheelbase * curvature);
        const float steer_limit = std::min(car.tuning().max_steer,
            std::atan(car.tuning().wheelbase * 16 / std::max(speed * speed, 1.0f)));
        float desired_speed = std::min(vehicle.route % 5 == 4 ? 9.0f : 7.0f,
            std::sqrt(2.8f / std::max(std::abs(curvature), .001f)));

        // Leave braking distance behind cars, and yield at crossing streets.
        // Faster traffic approaches a stopped obstruction gently rather than
        // accelerating all the way into its physical collider.
        const auto avoid = [&](Vec3 obstacle, float clearance) {
            const Vec3 difference = flat(obstacle - position);
            if (std::abs(obstacle.GetY() - position.GetY()) > 3) return;
            const float along = difference.Dot(direction);
            const float side = std::abs(difference.Dot(right(direction)));
            if (along > -1 && side < clearance)
                desired_speed = std::min(desired_speed, std::max(0.0f, (along - 6) * .6f));
        };
        avoid(starter_car.position(), 2.6f);
        for (const auto& other : cars_) if (other.car.get() != &car) avoid(other.car->position(), 2.6f);
        if (plane) avoid(plane->position(), 7);
        // A pedestrian beside either door stops traffic, making theft possible
        // without needing to chase a car at full road speed.
        const bool yielding_to_pedestrian = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).Length() < 9;
        if (yielding_to_pedestrian) desired_speed = 0;
        const Vec3 origin = position + Vec3(0, 1, 0);
        const float obstacle_distance = 4 + speed * .8f;
        const float clear = world_.camera_fraction(origin, direction * obstacle_distance, car.body_id());
        if (clear < .98f) desired_speed = std::min(desired_speed, std::max(0.0f, (clear * obstacle_distance - 3) * .6f));

        const bool brake = desired_speed < .1f || speed > desired_speed + .5f;
        const float throttle = brake ? 0 : std::clamp((desired_speed + (desired_speed - speed) * .8f) / 24, 0.0f, 1.0f);
        car.step({throttle, std::clamp(angle / steer_limit, -1.0f, 1.0f), false, brake}, dt);
        const bool lost = location.distance > 8 || car.rotate(Vec3::sAxisY()).GetY() < .35f || environment_.submerged(position);
        vehicle.stuck_time = lost || (speed < .3f && !yielding_to_pedestrian) ? vehicle.stuck_time + dt : 0;
        // Recover displaced/stuck NPCs away from the player into an empty lane.
        // Claimed cars never respawn or return to AI control.
        if (vehicle.stuck_time > 8 && flat(position - player_position).Length() > 35) {
            const Vec3 recovery = ahead(route, location, 18);
            if (space_available(recovery, car, starter_car, plane, player_position)) {
                const Vec3 forward = ahead(route, locate(route, recovery), 2) - recovery;
                car.reset(Vec3(recovery.GetX(), environment_.height(recovery.GetX(), recovery.GetZ()) + .56f, recovery.GetZ()), yaw(forward));
                vehicle.stuck_time = 0;
            }
        }
    }
}
} // namespace forza
