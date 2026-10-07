#include "traffic.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include "airport.hpp"
#include "police.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace ambaretto {
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

std::vector<Vec3> lane_route(std::vector<Vec3> corners, bool reverse = false, float offset = lane_offset, bool closed = true) {
    corners.erase(std::unique(corners.begin(), corners.end(), [](Vec3 a, Vec3 b) { return (a - b).LengthSq() < .0001f; }), corners.end());
    if (closed && (corners.front() - corners.back()).LengthSq() < .0001f) corners.pop_back();
    if (reverse) std::reverse(corners.begin(), corners.end());
    if (!closed) {
        std::vector<Vec3> loop;
        const auto side_point = [&](std::size_t i, bool back) {
            const Vec3 incoming = flat(corners[i] - corners[i ? i - 1 : 0]).NormalizedOr(
                flat(corners[1] - corners[0]).Normalized());
            const Vec3 outgoing = flat(corners[std::min(i + 1, corners.size() - 1)] - corners[i]).NormalizedOr(incoming);
            return corners[i] + (right(incoming) + right(outgoing)) *
                ((back ? -offset : offset) / std::max(.5f, 1 + incoming.Dot(outgoing)));
        };
        const auto cap = [&](Vec3 end, Vec3 towards_end) {
            const Vec3 direction = flat(towards_end).Normalized(), side = right(direction);
            const Vec3 center = end - towards_end.Normalized() * offset;
            const float slope = towards_end.GetY() / flat(towards_end).Length();
            for (int step = 0; step <= 16; ++step) {
                const float angle = step * 3.14159265f / 16;
                const float ahead = offset * std::sin(angle);
                loop.push_back(center + side * (offset * std::cos(angle)) + direction * ahead + Vec3(0, slope * ahead, 0));
            }
        };
        const Vec3 first = (corners[1] - corners[0]).Normalized(), last = (corners.back() - corners[corners.size() - 2]).Normalized();
        loop.push_back(corners.front() + first * offset + right(flat(first).Normalized()) * offset);
        const auto near_cap = [&](std::size_t i) {
            return (corners[i] - corners.front()).LengthSq() <= offset * offset
                || (corners[i] - corners.back()).LengthSq() <= offset * offset;
        };
        for (std::size_t i = 1; i + 1 < corners.size(); ++i) if (!near_cap(i)) loop.push_back(side_point(i, false));
        cap(corners.back(), last);
        for (std::size_t i = corners.size() - 1; --i > 0;) if (!near_cap(i)) loop.push_back(side_point(i, true));
        cap(corners.front(), -first);
        loop.pop_back(); // The closing cap already reaches the loop's first point.
        return lane_route(std::move(loop), false, 0);
    }
    std::vector<Vec3> lane, result;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec3 incoming = flat(corners[i] - corners[(i + corners.size() - 1) % corners.size()]).Normalized();
        const Vec3 outgoing = flat(corners[(i + 1) % corners.size()] - corners[i]).Normalized();
        lane.push_back(corners[i] + (right(incoming) + right(outgoing)) *
            (offset / std::max(.5f, 1 + incoming.Dot(outgoing))));
    }
    for (std::size_t i = 0; i < lane.size(); ++i) {
        const Vec3 incoming = (lane[i] - lane[(i + lane.size() - 1) % lane.size()]).Normalized();
        const Vec3 outgoing = (lane[(i + 1) % lane.size()] - lane[i]).Normalized();
        const float before = std::min(4.0f, (lane[i] - lane[(i + lane.size() - 1) % lane.size()]).Length() * .45f);
        const float after = std::min(4.0f, (lane[(i + 1) % lane.size()] - lane[i]).Length() * .45f);
        const Vec3 start = lane[i] - incoming * before, end = lane[i] + outgoing * after;
        for (int j = 0; j < 8; ++j) {
            const float t = j / 8.0f;
            result.push_back(start * ((1 - t) * (1 - t)) + lane[i] * (2 * t * (1 - t)) + end * (t * t));
        }
        const Vec3 next = lane[(i + 1) % lane.size()] - outgoing * after;
        const int steps = std::max(1, int((next - end).Length() / 8));
        for (int j = 0; j < steps; ++j) result.push_back(end + (next - end) * (float(j) / steps));
    }
    return result;
}
} // namespace

Traffic::Traffic(PhysicsWorld& world, const Environment& environment,const std::vector<CarDesign>* designs, const std::vector<CharacterDesign>* characters) : world_(world), environment_(environment) {
    if (characters) for (const auto& design:*characters) if (design.type==CharacterType::NPC) driver_designs_.push_back(design);
    std::vector<CarDesign> civilian;
    if (designs) for (const auto& design : *designs) if (design.type==CarType::Civilian) civilian.push_back(design);
    const bool ambient = (!designs || !civilian.empty()) && (!characters || !driver_designs_.empty()); std::size_t design_index = 0;
    const auto create = [&]() { auto car = std::make_unique<Car>(world); if (!civilian.empty()) car->set_design(civilian[design_index++%civilian.size()]); return car; };
    if (environment.city()) {
        const City& city = *environment.city();
        routes_ = city.traffic_routes();
        for (const auto& node : city.road_network()) if (node.edges.size()>=3) intersections_.push_back({node.center(),node.half_size()});
        // ponytail: at most 200 NPCs, with the existing 48-car physics pool; spatial route indexing can raise this later.
        int remaining = 200;
        for (std::size_t r = 0; ambient && r < routes_.size(); ++r) {
            const auto& route = routes_[r]; float length = 0;
            for (std::size_t j = 0; j < route.size(); ++j) length += (route[(j+1)%route.size()]-route[j]).Length();
            const int count = std::min(remaining,std::max(1,int(length/75)));
            for (int i = 0; i < count; ++i) {
                const auto location = advance(route,{0,route.front(),0},length*(i+.5f)/count);
                bool occupied = false;
                for (auto v : city.vehicles) {
                    const float radius = v.kind == CityVehicleKind::Car ? 12.f
                        : std::max(plane_specs(PlaneType(int(v.kind)-1)).span,plane_specs(PlaneType(int(v.kind)-1)).length)/2+6;
                    if (flat(v.position-location.point).LengthSq()<radius*radius) occupied = true;
                }
                if (city.spawn && flat(City::center(*city.spawn)-location.point).LengthSq()<10*10) occupied = true;
                if (occupied) continue;
                TrafficCar vehicle; vehicle.car = create();
                vehicle.car->set_simulated(false); vehicle.route = r; vehicle.route_name = "CITY STREET";
                vehicle.segment = location.segment; vehicle.point = location.point; vehicle.cruise_speed = 4.2f;
                if (!vehicle.car->design()) { auto tuning = vehicle.car->tuning(); tuning.max_steer = .75f; vehicle.car->set_tuning(tuning); }
                vehicle.car->reset(location.point+Vec3(0,vehicle.car->ride_height(),0),yaw(ahead(route,location,2)-location.point));
                cars_.push_back(std::move(vehicle)); --remaining;
            }
        }
        for (auto v : city.vehicles) if (v.kind == CityVehicleKind::Car) {
            if (designs && !v.car) continue;
            if (designs && v.car && std::none_of(designs->begin(),designs->end(),[&](const auto& d){return d.name==v.car->name && d.body==v.car->body && d.wheel==v.car->wheel;})) continue;
            TrafficCar vehicle; vehicle.car = create(); vehicle.npc = false;
            if (v.car) vehicle.car->set_design(*v.car);
            vehicle.car->reset(v.position+Vec3(0,vehicle.car->ride_height(),0),v.rotation*1.57079633f);
            cars_.push_back(std::move(vehicle));
        }
        stream(environment.spawn(),nullptr,nullptr); return;
    }
    const auto add_route = [&](std::vector<Vec3> corners, const char* name, int count, float speed,
                               bool reverse = false, float offset = lane_offset, bool closed = true) {
        const std::size_t r = routes_.size();
        for (auto& p : corners) if (p.GetY() <= 0) p.SetY(environment.ground_height(p.GetX(), p.GetZ()));
        auto points = lane_route(std::move(corners), reverse, offset, closed);
        for (auto& p : points) p.SetY(environment.surface_height(p));
        routes_.push_back(std::move(points));
        const auto& route = routes_.back();
        for (int i = 0; ambient && i < count; ++i) {
            std::size_t start = (route.size() * (i * 2 + 1) / (count * 2) + r * 17) % route.size();
            if (r == 0 && i == 0) start = locate(route, Vec3(lane_offset, environment.ground_height(lane_offset, 88), 88)).segment;
            auto car = create();
            car->set_simulated(false);
            const Vec3 p = route[start];
            car->reset(p + Vec3(0, car->ride_height(), 0), yaw(route[(start + 1) % route.size()] - p));
            TrafficCar vehicle;
            vehicle.car = std::move(car); vehicle.route = r; vehicle.route_name = name;
            vehicle.cruise_speed = speed; vehicle.segment = start; vehicle.point = p;
            cars_.push_back(std::move(vehicle));
        }
    };
    for (bool reverse : {false, true}) {
        for (const auto& loop : environment.street_routes()) {
            if (reverse && !loop.closed) continue;
            add_route(loop.corners, loop.name, loop.traffic_count, loop.cruise_speed, reverse, loop.lane_offset, loop.closed);
        }
    }
    for (bool reverse : {false, true})
        add_route({{0, 0, 240}, {660, 0, 240}, {1260, 0, 240}, {1260, 0, -240}, {0, 0, -240}},
            "CITY CAUSEWAYS", 12, 11, reverse);
    for (const auto& road : environment.highway_routes()) {
        if (road.closed) for (bool reverse : {false, true})
            add_route(road.corners, road.name, (road.traffic_count + (reverse ? 0 : 1)) / 2,
                road.cruise_speed, reverse, road.lane_offset);
        else add_route(road.corners, road.name, road.traffic_count, road.cruise_speed, false, road.lane_offset, false);
    }
    // Make nearby cars physical before the first interaction / preview.
    stream(environment.spawn(), nullptr, nullptr);
}
bool Traffic::is_npc(const Car* car) const {
    for (const auto& vehicle : cars_) if (vehicle.car.get() == car) return vehicle.npc;
    return false;
}
bool Traffic::steal(Car& car) {
    if (car.destroyed()) return false;
    for (auto& vehicle : cars_) if (vehicle.car.get() == &car && vehicle.npc) {
        if (car.simulated()) {
            auto driver = std::make_unique<Character>(world_, &environment_);
            if (!driver_designs_.empty()) driver->set_design(driver_designs_[std::size_t(&vehicle-cars_.data())%driver_designs_.size()]);
            if (!driver->pull_from(car)) return false;
            vehicle.driver = std::move(driver);
        }
        vehicle.npc = false;
        clear_driver(vehicle);
    }
    car.set_simulated(true);
    return true;
}
void Traffic::finish(Vec3 player_position, float dt) {
    for (auto& vehicle : cars_) {
        if (vehicle.npc && vehicle.car->destroyed()) {
            vehicle.npc = false;
            clear_driver(vehicle);
            vehicle.driver = std::make_unique<Character>(world_, &environment_);
            if (!driver_designs_.empty()) vehicle.driver->set_design(driver_designs_[std::size_t(&vehicle-cars_.data())%driver_designs_.size()]);
            if (!vehicle.driver->pull_from(*vehicle.car)) vehicle.driver->reset(vehicle.car->position() + Vec3(0, 2, 0));
            vehicle.driver->take_damage(100);
            if (vehicle.car->player_destroyed()) world_.notify_player_kill();
        }
        if (!vehicle.driver) continue;
        auto& driver = *vehicle.driver;
        if ((driver.position() - player_position).LengthSq() > 330 * 330) { vehicle.driver.reset(); continue; }
        driver.hit_by(*vehicle.car, dt);
        const Vec3 away = flat(driver.position() - player_position).NormalizedOr(Vec3::sAxisX());
        driver.step({away, true}, dt);
    }
}
Traffic::RouteLocation Traffic::locate(const std::vector<Vec3>& route, Vec3 position, std::size_t hint) const {
    RouteLocation nearest{0, route.front(), std::numeric_limits<float>::max()};
    const auto consider=[&](std::size_t i) {
        const Vec3 segment = route[(i + 1) % route.size()] - route[i];
        const float t = std::clamp((position - route[i]).Dot(segment) / std::max(segment.LengthSq(), .001f), 0.0f, 1.0f);
        const Vec3 point = route[i] + segment * t;
        const float distance = (point - position).LengthSq();
        if (distance < nearest.distance) nearest = {i, point, distance};
    };
    if (hint==std::size_t(-1)) for (std::size_t i=0;i<route.size();++i) consider(i);
    else {
        hint%=route.size(); consider(hint); const Vec3 point=nearest.point;
        // Track progress by metres, so dense bends cannot jump to a later junction visit.
        for (int direction:{1,-1}) {
            std::size_t i=hint;
            float distance=(point-route[direction>0 ? (hint+1)%route.size() : hint]).Length();
            for (std::size_t count=1;count<route.size() && distance<=(direction>0 ? 8.f : 2.f);++count) {
                i=(i+route.size()+direction)%route.size(); consider(i);
                distance+=(route[(i+1)%route.size()]-route[i]).Length();
            }
        }
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
    for (std::size_t i = 0; i < cars_.size(); ++i) if (cars_[i].npc && !cars_[i].car->destroyed()) {
        const float distance = flat(cars_[i].car->position() - player_position).LengthSq();
        if (distance < 700 * 700) nearby.push_back({distance, i});
    }
    std::sort(nearby.begin(), nearby.end());
    std::vector<bool> selected(cars_.size(), false);
    for (std::size_t i = 0; i < std::min<std::size_t>(48, nearby.size()); ++i) selected[nearby[i].second] = true;
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        auto& vehicle = cars_[i]; Car& car = *vehicle.car;
        if (!vehicle.npc || car.destroyed() || selected[i] == car.simulated()) continue;
        if (selected[i]) {
            const Vec3 position = car.position();
            // Never create a chassis inside an occupied lane or pedestrian.
            if ((position - player_position).LengthSq() < 3 * 3) continue;
            if (starter && (position - starter->position()).LengthSq() < 6 * 6) continue;
            if (plane && (position - plane->position()).LengthSq() < 12 * 12) continue;
            bool occupied = false;
            for (const auto& other : cars_) if (other.car.get() != &car && other.car->simulated()
                && (position - other.car->position()).LengthSq() < 6 * 6) { occupied = true; break; }
            if (police_) for (const auto& unit : police_->units()) if (unit.active && (position - unit.car->position()).LengthSq() < 6 * 6) occupied = true;
            if (occupied) continue;
            car.set_simulated(true); vehicle.plan_time = 0; vehicle.stuck_time = 0; vehicle.turning_back = false;
        } else {
            const auto location = locate(routes_[vehicle.route], car.position()-Vec3(0,car.ride_height(),0),environment_.city() ? vehicle.segment : std::size_t(-1));
            vehicle.point = location.point; vehicle.segment = location.segment;
            car.set_simulated(false);
            clear_driver(vehicle);
        }
    }
}
bool Traffic::space_available(Vec3 position, const Car& ignore, const Car& starter_car,
                              const Plane* plane, Vec3 player_position) const {
    if ((position - player_position).LengthSq() < 35 * 35 || (position - starter_car.position()).LengthSq() < 10 * 10) return false;
    if (plane && (position - plane->position()).LengthSq() < 12 * 12) return false;
    if (police_) for (const auto& unit : police_->units()) if (unit.active && (position - unit.car->position()).LengthSq() < 10 * 10) return false;
    for (const auto& vehicle : cars_)
        if (vehicle.car.get() != &ignore && vehicle.car->simulated() && (position - vehicle.car->position()).LengthSq() < 10 * 10) return false;
    return true;
}
void Traffic::step(Car* controlled, const Car& starter_car, const Plane* plane,
                   const Vec3* pedestrian, Vec3 player_position, float dt, const Police* police) {
    police_ = police;
    for (const auto& vehicle : cars_) if (vehicle.driver && !vehicle.driver->ragdolling()) {
        vehicle.driver->hit_by(starter_car, dt);
        for (const auto& other : cars_) vehicle.driver->hit_by(*other.car, dt);
        if (police_) for (const auto& unit : police_->units()) if (unit.active) vehicle.driver->hit_by(*unit.car, dt);
    }
    stream_time_ -= dt;
    const bool sync = stream_time_ <= 0;
    if (sync) { stream(player_position, &starter_car, plane); stream_time_ = .5f; }
    for (auto& vehicle : cars_) {
        Car& car = *vehicle.car;
        vehicle.horn_time = std::max(0.0f, vehicle.horn_time - dt);
        vehicle.horn_cooldown = std::max(0.0f, vehicle.horn_cooldown - dt);
        vehicle.pass_retry = std::max(0.0f, vehicle.pass_retry - dt);
        if (&car == controlled) continue;
        if (car.destroyed()) { if (car.simulated()) car.step({}, dt); continue; }
        if (!vehicle.npc) { if (car.simulated()) car.step({0, 0, false, true}, dt); continue; }
        const auto& route = routes_[vehicle.route];
        if (!car.simulated()) {
            const auto location = advance(route, {vehicle.segment, vehicle.point, 0}, dt * vehicle.cruise_speed);
            vehicle.segment = location.segment; vehicle.point = location.point;
            if (sync) car.reset(location.point + Vec3(0, car.ride_height(), 0), yaw(ahead(route, location, 2) - location.point));
            continue;
        }
        vehicle.plan_time -= dt;
        if (vehicle.plan_time > 0) { car.step(vehicle.input, dt); continue; }
        constexpr float plan_dt = .05f;
        vehicle.plan_time = plan_dt;
        const Vec3 position = car.position(), direction = flat(car.forward()).NormalizedOr(Vec3(0, 0, -1));
        const float speed = flat(car.velocity()).Length();
        const auto location = locate(route, position-Vec3(0,car.ride_height(),0),environment_.city() ? vehicle.segment : std::size_t(-1));
        if (environment_.city()) vehicle.segment = location.segment;
        Vec3 target = ahead(route, location, environment_.city() ? 2.2f+speed*.35f : 3.5f+speed*.55f);
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
        float desired_speed = std::min(vehicle.cruise_speed,
            std::sqrt(2.8f / std::max(std::abs(curvature), .001f)));
        // Ground highways slow before crossing local streets; flyovers keep their cruise speed.
        if (vehicle.cruise_speed > 12 && location.point.GetY() <= Airport::elevation + .1f) {
            const Vec3 approach = ahead(route, location, 45);
            for (const auto& road : environment_.road_segments()) {
                if (road.bridge >= 0 || std::abs(flat(road.b - road.a).Normalized().Dot(direction)) > .8f
                    || !crossing(location.point, approach, road.a, road.b, road.width / 2 + 3)) continue;
                if (std::abs(environment_.road_height(road, location.point.GetX(), location.point.GetZ()) - location.point.GetY()) < 1)
                    desired_speed = std::min(desired_speed, 8.0f);
            }
        }
        const float cruise_speed = desired_speed;
        if (environment_.city()) {
            // Give one car the junction until its rear has cleared it.
            for (auto it = junctions_.begin(); it != junctions_.end();) {
                const auto& junction = intersections_[it->first]; const Vec3 delta = it->second->position()-junction.center;
                if (!is_npc(it->second) || !it->second->simulated() || it->second->destroyed() || std::abs(delta.GetX())>junction.half.GetX()+3 || std::abs(delta.GetZ())>junction.half.GetZ()+3) it = junctions_.erase(it);
                else ++it;
            }
            for (float distance : {0.f,6.f,12.f}) {
                int key = -1;
                for (std::size_t j = 0; j < intersections_.size(); ++j) {
                    const Vec3 delta = ahead(route,location,distance)-intersections_[j].center;
                    if (std::abs(delta.GetX())<=intersections_[j].half.GetX() && std::abs(delta.GetZ())<=intersections_[j].half.GetZ()) { key = int(j); break; }
                }
                if (key<0) continue;
                const auto& junction = intersections_[key]; const Vec3 center = junction.center;
                const Vec3 offset=position-center;
                const bool inside=std::abs(offset.GetX())<=junction.half.GetX() && std::abs(offset.GetZ())<=junction.half.GetZ();
                auto found = junctions_.find(key);
                if (found == junctions_.end()) {
                    bool occupied = false;
                    for (const auto& other : cars_) if (other.car.get()!=&car && other.car->simulated()) {
                        const Vec3 delta = other.car->position()-center;
                        if (std::abs(delta.GetX())<junction.half.GetX()+1 && std::abs(delta.GetZ())<junction.half.GetZ()+1) occupied = true;
                    }
                    if (inside || !occupied) found = junctions_.emplace(key,&car).first;
                }
                if (!inside && (found == junctions_.end() || found->second != &car)) {
                    const float stop = flat(center-position).Dot(direction)-std::abs(direction.GetX())*junction.half.GetX()-std::abs(direction.GetZ())*junction.half.GetZ()-3;
                    desired_speed = std::min(desired_speed,std::max(0.f,stop*.7f));
                }
                break;
            }
        }
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
        if (police_) for (const auto& unit : police_->units()) if (unit.active && !unit.claimed) {
            avoid(unit.car->position(), 2.6f);
            if (police_->wanted().stars() && (unit.car->position() - position).LengthSq() < 35 * 35) {
                desired_speed = std::min(desired_speed, 6.f);
                target += right(direction) * .8f; curvature = turn(target);
            }
        }
        const bool yielding = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).LengthSq() < 9 * 9;
        if (yielding) desired_speed = 0;
        const bool pedestrian_ahead = pedestrian && std::abs(pedestrian->GetY() - position.GetY()) < 2
            && flat(*pedestrian - position).Dot(direction) > -2 && flat(*pedestrian - position).Dot(direction) < 25
            && std::abs(flat(*pedestrian - position).Dot(right(direction))) < 3;
        const float obstacle_distance = 4 + speed * .8f;
        if (!vehicle.pass_blocker) {
            const Vec3 approach = ahead(route, location, obstacle_distance) - location.point;
            const float clear = world_.camera_fraction(position + Vec3(0, 1, 0), approach, car.body_id());
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
                a.SetY(environment_.surface_height(a) + 1.1f);
                b.SetY(environment_.surface_height(b) + 1.1f);
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
            if (flat(route_end - finish).Length() < 1.5f) for (const auto& road : environment_.road_segments()) {
                const Vec3 along = flat(road.b - road.a).Normalized();
                if (road.bridge >= 0 || road.width < 9 || std::abs(along.Dot(direction)) < .99f
                    || std::abs(environment_.road_height(road, position.GetX(), position.GetZ()) - location.point.GetY()) > 2
                    || segment_distance(location.point, road.a, road.b) > std::pow(road.width / 2 - 1.3f, 2)) continue;
                bool junction = false;
                for (const auto& other : environment_.road_segments()) {
                    if (std::abs(environment_.road_height(other, position.GetX(), position.GetZ()) - location.point.GetY()) > 3) continue;
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
        bool brake = desired_speed < .1f || speed > desired_speed + .5f;
        float throttle = brake ? 0 : std::clamp((desired_speed + (desired_speed - speed) * .8f) / car.tuning().top_speed, 0.0f, 1.0f);
        float steer=std::clamp(angle/steer_limit,-1.0f,1.0f);
        if (environment_.city()) {
            const auto& city=*environment_.city(); const auto cell=City::cell(position.GetX(),position.GetZ());
            const auto mask=city.road_mask(cell);
            const Vec3 forward=position+direction*(2+speed*.8f);
            const bool clear_forward=city.road(City::cell(forward.GetX(),forward.GetZ()));
            const float alignment=flat(target-position).NormalizedOr(direction).Dot(direction);
            // A car that cannot fit the dead-end circle makes a three-point turn.
            if (!vehicle.turning_back && mask && !(mask&(mask-1)) && !clear_forward && alignment<.4f) vehicle.turning_back=true;
            if (vehicle.turning_back && clear_forward && alignment>.8f) vehicle.turning_back=false;
            if (vehicle.turning_back) {
                brake=yielding;
                for (float edge:{-1.f,0.f,1.f})
                    if (world_.camera_fraction(position+Vec3(0,.4f,0)+right(direction)*edge,-direction*4,car.body_id())<.98f) brake=true;
                throttle=brake ? 0 : -1.5f/11; steer=-steer;
            }
        }
        vehicle.input = {throttle, steer, false, brake};
        car.step(vehicle.input, dt);
        const bool lost = location.distance > 8 || car.rotate(Vec3::sAxisY()).GetY() < .35f || environment_.submerged(position);
        vehicle.stuck_time = lost || (speed < .3f && !yielding && !obstructed && !vehicle.pass_blocker && !vehicle.turning_back) ? vehicle.stuck_time + plan_dt : 0;
        if (vehicle.stuck_time > 8 && flat(position - player_position).LengthSq() > 35 * 35) {
            const auto recovery = advance(route, location, 18);
            if (space_available(recovery.point, car, starter_car, plane, player_position)) {
                car.reset(recovery.point + Vec3(0, car.ride_height(), 0),yaw(ahead(route,recovery,2)-recovery.point));
                vehicle.segment=recovery.segment; vehicle.point=recovery.point;
                vehicle.stuck_time = 0; vehicle.turning_back = false;
                clear_driver(vehicle);
            }
        }
    }
}
} // namespace ambaretto
