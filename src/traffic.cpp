#include "traffic.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace forza {
namespace {
constexpr float lane_offset = 2.2f;
Vec3 flat(Vec3 value) { value.SetY(0); return value; }
Vec3 right(Vec3 direction) { return direction.Cross(Vec3::sAxisY()); }
float yaw(Vec3 direction) { return std::atan2(-direction.GetX(), -direction.GetZ()); }
float segment_distance(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 d = flat(b - a);
    return flat(p - a - d * std::clamp(flat(p - a).Dot(d) / std::max(d.LengthSq(), .001f), 0.0f, 1.0f)).LengthSq();
}
bool crossing(Vec3 a, Vec3 b, Vec3 c, Vec3 d, float radius) {
    const auto cross = [](Vec3 u, Vec3 v) { return u.GetX() * v.GetZ() - u.GetZ() * v.GetX(); };
    const Vec3 u = flat(b - a), v = flat(d - c), w = flat(c - a);
    const float determinant = cross(u, v);
    if (std::abs(determinant) > .001f) {
        const float t = cross(w, v) / determinant, s = cross(w, u) / determinant;
        if (t >= 0 && t <= 1 && s >= 0 && s <= 1) return true;
    }
    return std::min({segment_distance(a, c, d), segment_distance(b, c, d),
        segment_distance(c, a, b), segment_distance(d, a, b)}) < radius * radius;
}
void clear_driver(TrafficCar& vehicle) {
    vehicle.horn_time = vehicle.horn_cooldown = vehicle.blocked_time = vehicle.pass_retry = 0;
    vehicle.blocked = false; vehicle.pass_blocker = nullptr; vehicle.pass_road = nullptr;
}

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
        const Vec3 start = lane[i] - incoming * 4, end = lane[i] + outgoing * 4;
        for (int j = 0; j < 8; ++j) {
            const float t = j / 8.0f;
            result.push_back(start * ((1 - t) * (1 - t)) + lane[i] * (2 * t * (1 - t)) + end * (t * t));
        }
        const Vec3 next = lane[(i + 1) % lane.size()] - outgoing * 4;
        const int steps = std::max(1, int((next - end).Length() / 8));
        for (int j = 0; j < steps; ++j) result.push_back(end + (next - end) * (float(j) / steps));
    }
    return result;
}
} // namespace

Traffic::Traffic(PhysicsWorld& world, const Environment& environment) : world_(world), environment_(environment) {
    for (bool reverse : {false, true}) {
        for (const auto& loop : environment.street_loops()) routes_.push_back(lane_route(loop.corners, reverse));
    }
    routes_.push_back(lane_route({{0, 0, 240}, {660, 0, 240}, {1260, 0, 240}, {1260, 0, -240}, {0, 0, -240}}));
    routes_.push_back(lane_route({{0, 0, 240}, {660, 0, 240}, {1260, 0, 240}, {1260, 0, -240}, {0, 0, -240}}, true));
    // The Overseas Highway goes through every Key and every connecting deck.
    // Both ends turn around on connected village/city blocks.
    std::vector<Vec3> spine{{-360, 0, 600}};
    for (std::size_t i = 3; i < std::min(environment.bridges().size(), environment.islands().size() - 3); ++i) {
        const auto& bridge = environment.bridges()[i];
        spine.push_back(bridge.a); spine.push_back(bridge.b); spine.push_back(environment.islands()[i + 3].center);
    }
    auto highway = spine;
    highway.insert(highway.end(), {{-2100, 0, 4470}, {-2010, 0, 4470}, {-2010, 0, 4380}, {-2100, 0, 4380}});
    for (std::size_t i = spine.size() - 1; i-- > 0;) highway.push_back(spine[i]);
    highway.insert(highway.end(), {{-600, 0, 600}, {-580, 0, 340}, {-360, 0, 360}});
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
    for (auto& vehicle : cars_) if (vehicle.car.get() == &car) { vehicle.npc = false; clear_driver(vehicle); }
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
            clear_driver(vehicle);
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
        vehicle.horn_time = std::max(0.0f, vehicle.horn_time - dt);
        vehicle.horn_cooldown = std::max(0.0f, vehicle.horn_cooldown - dt);
        vehicle.pass_retry = std::max(0.0f, vehicle.pass_retry - dt);
        if (&car == controlled) continue;
        if (!vehicle.npc) { if (car.simulated()) car.step({0, 0, false, true}, dt); continue; }
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
        Vec3 target = ahead(route, location, 3.5f + speed * .55f);
        if (vehicle.pass_blocker) {
            const float remaining = flat(vehicle.pass_end - position).Dot(vehicle.pass_direction);
            if (remaining < 0 && location.distance < .8f) { vehicle.pass_blocker = nullptr; vehicle.pass_road = nullptr; }
            else if (remaining > 9) target += right(vehicle.pass_direction) * -4.4f;
        }
        const auto turn = [&](Vec3 aim) {
            const Vec3 delta = flat(aim - position);
            return 2 * delta.Dot(-right(direction)) / std::max(delta.LengthSq(), 1.0f);
        };
        float curvature = turn(target);
        const float steer_limit = std::min(car.tuning().max_steer, std::atan(car.tuning().wheelbase * 16 / std::max(speed * speed, 1.0f)));
        float desired_speed = std::min(vehicle.route >= routes_.size() - 3 ? 11.0f : 8.0f,
            std::sqrt(2.8f / std::max(std::abs(curvature), .001f)));
        const float cruise_speed = desired_speed;
        const Car* blocker = nullptr;
        float nearest = 25;
        const auto avoid_car = [&](const Car& other) {
            if (&other == &car || &other == vehicle.pass_blocker) return;
            const Vec3 difference = flat(other.position() - position);
            const float along = difference.Dot(direction);
            if (along > 3 && along < nearest && std::abs(other.position().GetY() - position.GetY()) < 3
                && std::abs(difference.Dot(right(direction))) < 2.6f && flat(other.velocity()).Length() < .6f) {
                nearest = along; blocker = &other;
            }
        };
        const auto avoid = [&](Vec3 obstacle, float clearance, float gap = 8) {
            const Vec3 difference = flat(obstacle - position);
            if (std::abs(obstacle.GetY() - position.GetY()) > 3) return;
            const float along = difference.Dot(direction), side = std::abs(difference.Dot(right(direction)));
            if (along > -1 && side < clearance)
                desired_speed = std::min(desired_speed, std::max(0.0f, (along - gap) * .6f));
        };
        if (&starter_car != vehicle.pass_blocker)
            avoid(starter_car.position(), 2.6f, flat(starter_car.velocity()).Length() < .6f ? 12 : 8);
        avoid_car(starter_car);
        for (const auto& other : cars_) if (other.car.get() != &car && other.car->simulated()) {
            if (other.car.get() != vehicle.pass_blocker)
                avoid(other.car->position(), 2.6f, flat(other.car->velocity()).Length() < .6f ? 12 : 8);
            avoid_car(*other.car);
        }
        if (plane) avoid(plane->position(), 7);
        const bool yielding = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).LengthSq() < 9 * 9;
        if (yielding) desired_speed = 0;
        const bool pedestrian_ahead = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).Dot(direction) > -2 && flat(*pedestrian - position).Dot(direction) < 25
            && std::abs(flat(*pedestrian - position).Dot(right(direction))) < 3;
        const float obstacle_distance = 4 + speed * .8f;
        if (!vehicle.pass_blocker) {
            const float clear = world_.camera_fraction(position + Vec3(0, 1, 0), direction * obstacle_distance, car.body_id());
            if (clear < .98f) desired_speed = std::min(desired_speed, std::max(0.0f, (clear * obstacle_distance - 3) * .6f));
        }
        const auto pass_clear = [&](const Road& road, Vec3 finish, Vec3 basis, bool static_check) {
            const float remaining = std::max(0.0f, flat(finish - location.point).Dot(basis));
            const Vec3 side = right(basis), offset = side * -4.4f;
            std::array<Vec3, 4> path{{position,
                location.point + basis * std::min(6.0f, std::max(0.0f, remaining - 10)) + offset,
                finish - basis * std::min(10.0f, remaining) + offset, finish}};
            if (remaining <= 9) path[1] = path[2] = finish;
            const float duration = remaining / 4.0f + 2;
            const auto occupied = [&](const Car& other) {
                if (&other == &car || std::abs(other.position().GetY() - position.GetY()) > 3) return false;
                const Vec3 future = other.position() + flat(other.velocity()) * duration;
                for (int i = 1; i < 4; ++i) if (crossing(path[i - 1], path[i], other.position(), future, 3.5f)) return true;
                return false;
            };
            if (occupied(starter_car)) return false;
            for (const auto& other : cars_) if (other.car->simulated() && occupied(*other.car)) return false;
            for (int i = 1; i < 4; ++i) {
                if (pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 3
                    && segment_distance(*pedestrian, path[i - 1], path[i]) < 5 * 5) return false;
                if (plane && std::abs(plane->position().GetY() - position.GetY()) < 8
                    && segment_distance(plane->position(), path[i - 1], path[i]) < 12 * 12) return false;
                if (!static_check) continue;
                const Vec3 road_delta = flat(road.b - road.a);
                const int samples = std::max(1, int(std::ceil(flat(path[i] - path[i - 1]).Length() / 3)));
                for (int sample = 0; sample <= samples; ++sample) {
                    const Vec3 p = path[i - 1] + (path[i] - path[i - 1]) * (float(sample) / samples);
                    const float t = flat(p - road.a).Dot(road_delta) / road_delta.LengthSq();
                    if (t < 0 || t > 1 || segment_distance(p, road.a, road.b) > std::pow(road.width / 2 - 1.3f, 2)) return false;
                }
                Vec3 a = path[i - 1], b = path[i];
                a.SetY(environment_.height(a.GetX(), a.GetZ()) + 1.1f);
                b.SetY(environment_.height(b.GetX(), b.GetZ()) + 1.1f);
                for (float edge : {-1.0f, 0.0f, 1.0f}) {
                    const Vec3 margin = side * edge + basis * 1.8f;
                    if (world_.camera_fraction(a + margin, b - a, car.body_id()) < .98f) return false;
                }
            }
            return true;
        };
        // ponytail: local passes only on straight, wide roads; route planning is needed for detours around whole streets.
        // Retry only while queued; straight-road checks and swept queries never run for normal cruising.
        if (!vehicle.pass_blocker && blocker && vehicle.blocked_time > 2 && speed < 2 && !pedestrian_ahead
            && !yielding && vehicle.pass_retry <= 0) {
            vehicle.pass_retry = 1;
            const Vec3 finish = location.point + direction * (nearest + 18);
            const Vec3 route_end = ahead(route, location, nearest + 18);
            if (flat(route_end - finish).Length() < 1.5f) for (const auto& road : environment_.roads()) {
                const Vec3 along = flat(road.b - road.a).Normalized();
                if (road.bridge >= 0 || road.width < 9 || std::abs(along.Dot(direction)) < .99f
                    || segment_distance(location.point, road.a, road.b) > std::pow(road.width / 2 - 1.3f, 2)) continue;
                bool junction = false;
                for (const auto& other : environment_.roads()) {
                    if (std::abs(flat(other.b - other.a).Normalized().Dot(direction)) > .98f) continue;
                    if (crossing(location.point, finish, other.a, other.b, other.width / 2 + 3)) { junction = true; break; }
                }
                if (junction || !pass_clear(road, finish, direction, true)) continue;
                vehicle.pass_blocker = blocker; vehicle.pass_road = &road;
                vehicle.pass_end = finish; vehicle.pass_direction = direction;
                target += right(direction) * -4.4f;
                curvature = turn(target);
                desired_speed = 4.5f;
                break;
            }
        }
        if (vehicle.pass_blocker) {
            desired_speed = std::min(desired_speed, 4.5f);
            if (!pass_clear(*vehicle.pass_road, vehicle.pass_end, vehicle.pass_direction, false)) desired_speed = 0;
        }
        const bool obstructed = desired_speed < cruise_speed - 1.5f;
        vehicle.blocked_time = obstructed && desired_speed < 1.5f && !yielding ? vehicle.blocked_time + plan_dt : 0;
        if (vehicle.horn_cooldown <= 0 && ((obstructed && !vehicle.blocked && speed > desired_speed + 2
            && (!yielding || pedestrian_ahead))
            || (vehicle.blocked_time > 6 && !pedestrian_ahead && !yielding))) {
            vehicle.horn_time = speed > 2 ? .25f : .4f;
            vehicle.horn_cooldown = 8 + float(vehicle.route % 4);
        }
        vehicle.blocked = obstructed;
        const float angle = std::atan(car.tuning().wheelbase * curvature);
        const bool brake = desired_speed < .1f || speed > desired_speed + .5f;
        const float throttle = brake ? 0 : std::clamp((desired_speed + (desired_speed - speed) * .8f) / car.tuning().top_speed, 0.0f, 1.0f);
        vehicle.input = {throttle, std::clamp(angle / steer_limit, -1.0f, 1.0f), false, brake};
        car.step(vehicle.input, dt);
        const bool lost = location.distance > 8 || car.rotate(Vec3::sAxisY()).GetY() < .35f || environment_.submerged(position);
        vehicle.stuck_time = lost || (speed < .3f && !yielding && !obstructed && !vehicle.pass_blocker) ? vehicle.stuck_time + plan_dt : 0;
        if (vehicle.stuck_time > 8 && flat(position - player_position).LengthSq() > 35 * 35) {
            const Vec3 recovery = ahead(route, location, 18);
            if (space_available(recovery, car, starter_car, plane, player_position)) {
                car.reset(Vec3(recovery.GetX(), environment_.height(recovery.GetX(), recovery.GetZ()) + .56f, recovery.GetZ()),
                    yaw(ahead(route, locate(route, recovery), 2) - recovery));
                vehicle.stuck_time = 0;
                clear_driver(vehicle);
            }
        }
    }
}
} // namespace forza
