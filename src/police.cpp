#include "police.hpp"
#include "environment.hpp"
#include "pedestrians.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <tuple>

namespace forza {
namespace {
Vec3 flat(Vec3 p) { p.SetY(0); return p; }
float cross(Vec3 a, Vec3 b) { return a.GetX() * b.GetZ() - a.GetZ() * b.GetX(); }
float yaw(Vec3 p) { return std::atan2(-p.GetX(), -p.GetZ()); }
constexpr int thresholds[] = {0, 1, 4, 9, 17, 28, 42};
constexpr float vehicle_fire_range = 30, vehicle_return_range = 40, dismount_range = 20;
}
const CrimeData& crime_data(Crime crime) {
    static const std::array<CrimeData, int(Crime::Count)> data{{
        {"Reckless driving", 1, 1, 65, 8}, {"Vehicle theft", 2, 1, 60, 0},
        {"Gunfire", 1, 1, 130, 2}, {"Assault", 3, 2, 80, .2f},
        {"Homicide", 8, 3, 110, .1f}, {"Assault on an officer", 6, 3, 120, .2f},
        {"Officer homicide", 12, 4, 150, .1f}, {"Police vehicle theft", 5, 2, 90, 0}
    }};
    return data[std::clamp(int(crime), 0, int(Crime::Count) - 1)];
}
const PoliceResponse& police_response(int stars) {
    static const std::array<PoliceResponse, 6> responses{{
        {1, 16, 0, 2.8f, 110, 4}, {2, 22, 30, 2.2f, 130, 3.5f},
        {3, 27, 22, 1.7f, 150, 3}, {4, 32, 14, 1.3f, 170, 2.5f},
        {5, 37, 8, 1, 190, 2}, {6, 42, 4, .75f, 210, 1.5f}
    }};
    return responses[std::clamp(stars - 1, 0, 5)];
}
void WantedLevel::report(Crime crime, Vec3 position) {
    if (int(crime) < 0 || crime >= Crime::Count || !std::isfinite(position.LengthSq())) return;
    const auto& data = crime_data(crime);
    points_ = std::min(100, std::max(points_ + data.points, thresholds[data.minimum_stars]));
    for (int i = 1; i <= 6; ++i) if (points_ >= thresholds[i]) stars_ = i;
    last_seen_ = position; outside_time_ = 0; seen_ = true;
}
void WantedLevel::step(Vec3 position, bool seen, float dt) {
    if (!stars_ || !std::isfinite(dt) || dt <= 0 || !std::isfinite(position.LengthSq())) return;
    seen_ = seen;
    if (seen) { last_seen_ = position; outside_time_ = 0; }
    else if (flat(position - last_seen_).LengthSq() > radius() * radius()) {
        outside_time_ += dt;
        if (outside_time_ >= cooldown()) clear();
    } else outside_time_ = 0;
}
void WantedLevel::clear() { points_ = stars_ = 0; outside_time_ = 0; seen_ = false; }

Police::Police(PhysicsWorld& world, const Environment& environment, Traffic* traffic, Pedestrians* pedestrians)
    : world_(world), environment_(environment), traffic_(traffic), pedestrians_(pedestrians) {
    build_roads();
    civilian_health_.fill(100);
    for (auto& unit : units_) {
        unit.car = std::make_unique<Car>(world, CarType::Police);
        unit.car->set_simulated(false);
        for (auto& officer : unit.officers) {
            officer.character = std::make_unique<Character>(world, &environment);
            officer.character->set_enabled(false);
        }
    }
}
void Police::build_roads() {
    const auto& source = environment_.roads();
    std::vector<std::vector<float>> cuts(source.size(), {0, 1});
    for (std::size_t i = 0; i < source.size(); ++i) {
        const Vec3 a = source[i].a, u = flat(source[i].b - a);
        const int steps = std::max(1, int(u.Length() / 18));
        for (int k = 1; k < steps; ++k) cuts[i].push_back(float(k) / steps);
        for (std::size_t j = i + 1; j < source.size(); ++j) {
            const Vec3 b = source[j].a, v = flat(source[j].b - b), w = flat(b - a);
            const float determinant = cross(u, v);
            const auto join = [&](float t, float s) {
                if (t < -.001f || t > 1.001f || s < -.001f || s > 1.001f) return;
                const Vec3 p = a + u * t;
                if (std::abs(environment_.road_height(source[i], p.GetX(), p.GetZ())
                    - environment_.road_height(source[j], p.GetX(), p.GetZ())) > 1) return;
                cuts[i].push_back(std::clamp(t, 0.f, 1.f)); cuts[j].push_back(std::clamp(s, 0.f, 1.f));
            };
            if (std::abs(determinant) > .001f) join(cross(w, v) / determinant, cross(w, u) / determinant);
            else if (u.LengthSq() > .01f && v.LengthSq() > .01f && std::abs(cross(w, u)) < .1f) {
                join(w.Dot(u) / u.LengthSq(), 0); join((w + v).Dot(u) / u.LengthSq(), 1);
                join(0, (-w).Dot(v) / v.LengthSq()); join(1, (u - w).Dot(v) / v.LengthSq());
            }
        }
    }
    std::map<std::tuple<int, int, int>, int> nodes;
    for (std::size_t i = 0; i < source.size(); ++i) {
        auto& split = cuts[i];
        std::sort(split.begin(), split.end());
        split.erase(std::unique(split.begin(), split.end(), [](float a, float b) { return std::abs(a - b) < .0001f; }), split.end());
        int previous = -1;
        for (float t : split) {
            Vec3 p = source[i].a + (source[i].b - source[i].a) * t;
            p.SetY(environment_.road_height(source[i], p.GetX(), p.GetZ()));
            const auto key = std::make_tuple(int(std::round(p.GetX() * 2)), int(std::round(p.GetY() * 2)), int(std::round(p.GetZ() * 2)));
            auto [found, added] = nodes.emplace(key, int(roads_.size()));
            if (added) roads_.push_back({p, {}, source[i].width, -1});
            const int node = found->second;
            roads_[node].width = std::max(roads_[node].width, source[i].width);
            if (previous >= 0 && previous != node) {
                const float distance = (roads_[previous].point - p).Length();
                roads_[previous].edges.push_back({node, distance}); roads_[node].edges.push_back({previous, distance});
            }
            previous = node;
        }
    }
    int component = 0;
    for (std::size_t i = 0; i < roads_.size(); ++i) if (roads_[i].component < 0) {
        std::vector<int> pending{int(i)}; roads_[i].component = component;
        while (!pending.empty()) {
            const int node = pending.back(); pending.pop_back();
            for (const auto& edge : roads_[node].edges) if (roads_[edge.first].component < 0) {
                roads_[edge.first].component = component; pending.push_back(edge.first);
            }
        }
        ++component;
    }
}
int Police::nearest_node(Vec3 position) const {
    int nearest = -1; float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < roads_.size(); ++i) {
        Vec3 delta = roads_[i].point - position; delta.SetY(delta.GetY() * 4);
        if (delta.LengthSq() < distance) { distance = delta.LengthSq(); nearest = int(i); }
    }
    return nearest;
}
std::vector<Vec3> Police::road_path(Vec3 from, Vec3 to) const {
    const int start = nearest_node(from), end = nearest_node(to);
    if (start < 0 || end < 0 || roads_[start].component != roads_[end].component) return {};
    std::vector<float> cost(roads_.size(), std::numeric_limits<float>::max());
    std::vector<int> parent(roads_.size(), -1);
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> queue;
    cost[start] = 0; queue.push({0, start});
    while (!queue.empty()) {
        const auto [distance, node] = queue.top(); queue.pop();
        if (distance > cost[node]) continue;
        if (node == end) break;
        for (const auto& [next, length] : roads_[node].edges) if (distance + length < cost[next]) {
            cost[next] = distance + length; parent[next] = node; queue.push({cost[next], next});
        }
    }
    if (start != end && parent[end] < 0) return {};
    std::vector<Vec3> path;
    for (int node = end; node >= 0; node = parent[node]) { path.push_back(roads_[node].point); if (node == start) break; }
    std::reverse(path.begin(), path.end());
    return path;
}
bool Police::visible(Vec3 eye, Vec3 target, float range, JPH::BodyID ignore, JPH::BodyID target_body) const {
    const Vec3 delta = target - eye;
    if (delta.LengthSq() >= range * range) return false;
    if (target_body.IsInvalid()) return world_.camera_fraction(eye, delta, ignore) > .96f;
    GroundHit hit;
    return !world_.cast_ray(eye, delta, delta.Length(), hit, ignore) || hit.body == target_body;
}
bool Police::camera_visible(Vec3 position) const {
    if (!view_set_) return false;
    const Vec3 delta = position + Vec3(0, 1, 0) - camera_;
    return delta.LengthSq() < 400 * 400 && delta.NormalizedOr(camera_forward_).Dot(camera_forward_) > .55f
        && world_.camera_fraction(camera_, delta) > .96f;
}
bool Police::is_officer(const Character* character) const {
    for (const auto& unit : units_) for (const auto& officer : unit.officers) if (officer.character.get() == character) return true;
    return false;
}
void Police::crime(Crime type, Vec3 position, Character* victim) {
    if (int(type) < 0 || type >= Crime::Count || crime_cooldowns_[int(type)] > 0 || !std::isfinite(position.LengthSq())) return;
    const auto& data = crime_data(type);
    crime_cooldowns_[int(type)] = data.repeat_delay;
    bool observed = victim && is_officer(victim);
    for (const auto& unit : units_) if (unit.active)
        for (const auto& officer : unit.officers) if (officer.character->alive() && !(unit.claimed && officer.seated)) {
            const Vec3 eye = officer.seated ? unit.car->position() + Vec3(0, 1.1f, 0) : officer.character->position() + Vec3(0, 1.5f, 0);
            observed |= visible(eye, position + Vec3(0, 1, 0), 120, officer.seated ? unit.car->body_id() : JPH::BodyID{});
            if (type == Crime::Gunfire && (eye - position).LengthSq() < 75 * 75) observed = true;
        }
    const unsigned witnesses = pedestrians_ ? pedestrians_->alarm(position, data.report_range) : 0;
    if (observed) { wanted_.report(type, position); dispatch_time_ = std::min(dispatch_time_, 1.f); }
    else if (witnesses || (victim && victim->alive())) {
        if (pending_crime_ < 0 || data.points > crime_data(Crime(pending_crime_)).points) {
            pending_crime_ = int(type); report_position_ = position;
        }
        if (report_time_ <= 0) report_time_ = 2.5f;
    }
}
bool Police::spawn(PoliceUnit& unit, const Player& player, std::size_t index) {
    if (roads_.empty()) return false;
    const Vec3 center = wanted_.stars() ? wanted_.last_seen() : player.position();
    const int destination = nearest_node(center);
    const std::size_t start = (++spawn_sequence_ * 71 + index * 113) % roads_.size();
    for (std::size_t attempt = 0; attempt < roads_.size(); ++attempt) {
        const auto& node = roads_[(start + attempt) % roads_.size()];
        if (node.component != roads_[destination].component) continue;
        const float distance = flat(node.point - center).Length();
        if (distance < (wanted_.stars() ? 110.f : 55.f) || distance > (wanted_.stars() ? 240.f : 140.f) || node.edges.empty()) continue;
        Vec3 direction = flat(roads_[node.edges.front().first].point - node.point).NormalizedOr(Vec3(0, 0, -1));
        if (direction.Dot(flat(center - node.point)) < 0) direction = -direction;
        if (!wanted_.stars() && std::abs(node.point.GetY() - environment_.terrain_height(node.point.GetX(), node.point.GetZ())) > .3f) continue;
        const Vec3 position = node.point + direction.Cross(Vec3::sAxisY()) * (wanted_.stars() ? 2.2f : node.width / 2 - 1.8f) + Vec3(0, .56f, 0);
        if ((position - player.position()).LengthSq() < 45 * 45 || camera_visible(position) || environment_.submerged(position)) continue;
        const auto occupied = [&](const Car& other) { return other.simulated() && (other.position() - position).LengthSq() < 9 * 9; };
        bool blocked = occupied(player.car());
        if (traffic_) for (const auto& other : traffic_->cars()) blocked |= occupied(*other.car);
        for (const auto& other : units_) if (&other != &unit && other.active) blocked |= occupied(*other.car);
        if (blocked || !unit.officers.front().character->can_stand_at(position - Vec3(0, .48f, 0))
            || world_.camera_fraction(position + Vec3(0, .8f, 0), direction * 5) < .96f) continue;
        unit.car->set_simulated(true); unit.car->reset(position, yaw(direction)); unit.car->repair();
        unit.active = true; unit.claimed = false; unit.route.clear(); unit.waypoint = 0;
        unit.plan_time = unit.pit_time = unit.stuck_time = 0;
        unit.pit_cooldown = 4.f + index; unit.pit_attempts = 0;
        unit.destination = center;
        for (auto& officer : unit.officers) {
            officer.character->reset(position); officer.character->revive(); officer.character->set_enabled(false);
            officer.seated = true; officer.fire_time = 1 + float(index) * .2f; officer.flash = 0; officer.previous_health = 100;
            officer.shots_fired = 0;
        }
        if (!wanted_.stars()) for (std::size_t side = 0; side < unit.officers.size(); ++side) exit(unit, unit.officers[side], side);
        return true;
    }
    return false;
}
bool Police::exit(PoliceUnit& unit, PoliceOfficer& officer, std::size_t side) {
    if (!officer.character->alive() || !officer.seated || unit.car->velocity().Length() > 1.8f) return false;
    for (float offset : {2.1f, 2.8f, -2.1f}) {
        Vec3 feet = unit.car->position() + unit.car->rotate(Vec3((side ? 1.f : -1.f) * offset, 0, .35f));
        GroundHit ground;
        if (!world_.cast_ground(feet + Vec3(0, 2, 0), -Vec3::sAxisY(), 5, ground) || ground.normal.GetY() < .65f
            || environment_.terrain_height(feet.GetX(), feet.GetZ()) < Environment::water_level - 1) continue;
        feet = ground.point + Vec3(0, .08f, 0);
        if (!officer.character->can_stand_at(feet)
            || world_.camera_fraction(unit.car->position() + Vec3(0, 1, 0), feet + Vec3(0, 1, 0) - unit.car->position() - Vec3(0, 1, 0), unit.car->body_id()) < .96f) continue;
        officer.character->reset(feet, yaw(unit.destination - feet)); officer.seated = false;
        return true;
    }
    return false;
}
void Police::drive(PoliceUnit& unit, Player& player, std::size_t index, float dt) {
    auto& car = *unit.car;
    if (unit.claimed) { if (&player.car() != &car) car.step({0, 0, false, true}, dt); return; }
    const bool crew = std::any_of(unit.officers.begin(), unit.officers.end(), [](const PoliceOfficer& officer) { return officer.seated && officer.character->alive(); });
    const bool boarding = std::any_of(unit.officers.begin(), unit.officers.end(), [](const PoliceOfficer& officer) { return !officer.seated && officer.character->alive(); });
    if (car.destroyed() || !crew || boarding || !wanted_.stars() || arrested() || !player.character().alive()) {
        car.step({0, 0, false, true}, dt); return;
    }
    const auto& response = police_response(wanted_.stars());
    const Vec3 position = car.position(), forward = flat(car.forward()).NormalizedOr(Vec3(0, 0, -1));
    const float speed = flat(car.velocity()).Length(), distance = flat(unit.destination - position).Length();
    unit.plan_time -= dt; unit.pit_cooldown = std::max(0.f, unit.pit_cooldown - dt); unit.pit_time = std::max(0.f, unit.pit_time - dt);
    if (unit.plan_time <= 0 || unit.route.empty()) {
        unit.route = road_path(position, unit.destination); unit.waypoint = 0; unit.plan_time = .8f + float(index) * .05f;
    }
    Vec3 target = position;
    if (!unit.route.empty()) {
        while (unit.waypoint + 1 < unit.route.size() && flat(unit.route[unit.waypoint] - position).Length() < 5 + speed * .35f) ++unit.waypoint;
        target = unit.route[unit.waypoint];
    }
    if (!wanted_.searching() && distance < 45 && visible(position + Vec3(0, 1.2f, 0), unit.destination + Vec3(0, 1, 0), 50, car.body_id())) {
        target = unit.destination;
        if (player.driving() && player.car().velocity().Length() > 2) {
            const Vec3 heading = flat(player.forward()).NormalizedOr(forward);
            target -= heading * 8;
            if (flat(target - position).Dot(forward) < 2) target = unit.destination - heading * 2;
        }
    }
    const float player_speed = player.driving() ? flat(player.car().velocity()).Length() : 0;
    if (response.pit_interval > 0 && !wanted_.searching() && player.driving() && player_speed > 6 && player_speed < 38
        && distance < 16 && forward.Dot(flat(player.forward()).NormalizedOr(forward)) > .85f
        && std::abs(position.GetY() - player.position().GetY()) < 2 && unit.pit_cooldown <= 0
        && flat(position - player.position()).Dot(flat(player.forward()).NormalizedOr(forward)) < 1.5f
        && roads_[nearest_node(position)].width >= 9) {
        unit.pit_time = 4.5f; unit.pit_cooldown = response.pit_interval; ++unit.pit_attempts;
    }
    if (unit.pit_time > 0 && player.driving() && !wanted_.searching()) {
        const Vec3 heading = flat(player.forward()).NormalizedOr(forward), side = heading.Cross(Vec3::sAxisY());
        target = player.position() - heading * 1.6f + side * ((index % 2 ? -1.f : 1.f) * (unit.pit_time > 1.3f ? 2.15f : .3f));
    }
    unit.steering_target = target;
    const Vec3 delta = flat(target - position);
    const float curvature = 2 * delta.Dot(-forward.Cross(Vec3::sAxisY())) / std::max(delta.LengthSq(), 1.f);
    float desired = std::min(response.speed, std::sqrt((4.5f + wanted_.stars()) / std::max(std::abs(curvature), .001f)));
    if (player.driving() && player_speed > 2 && distance < 14 && unit.pit_time <= 0)
        desired = std::min(desired, std::max(0.f, distance - 6) * 2 + player_speed * .95f);
    if (distance < 11 && (!player.driving() || player_speed < 2)) desired = std::min(desired, std::max(0.f, (distance - 7) * 1.5f));
    if (player.driving() && player_speed < 4 && !wanted_.searching() && distance < dismount_range
        && visible(position + Vec3(0, 1.2f, 0), player.position() + player.car().rotate(Vec3(0, chassis_offset, 0)),
            dismount_range + 2, car.body_id(), player.car().body_id())) desired = 0;
    if (unit.route.empty() || (wanted_.searching() && distance < 9)) desired = 0;
    const auto avoid = [&](const Car& other) {
        if (&other == &car || (player.driving() && &other == &player.car() && unit.pit_time > 0) || !other.simulated()) return;
        const Vec3 offset = flat(other.position() - position);
        if (std::abs(other.position().GetY() - position.GetY()) < 3 && offset.Dot(forward) > 0
            && std::abs(offset.Dot(forward.Cross(Vec3::sAxisY()))) < 2.1f)
            desired = std::min(desired, std::max(0.f, (offset.Dot(forward) - 5) * 1.8f));
    };
    if (traffic_) for (const auto& other : traffic_->cars()) avoid(*other.car);
    for (const auto& other : units_) if (other.active) avoid(*other.car);
    if (!player.driving()) avoid(player.car());
    if (unit.pit_time <= 0 && speed > 1 && world_.camera_fraction(position + Vec3(0, 1.2f, 0), forward * (4 + speed * .45f), car.body_id()) < .5f)
        desired = std::min(desired, 3.f);
    const float steer_limit = std::min(car.tuning().max_steer, std::atan(car.tuning().wheelbase * 16 / std::max(speed * speed, 1.f)));
    const float steer = std::clamp(std::atan(car.tuning().wheelbase * curvature) / steer_limit, -1.f, 1.f);
    const bool brake = desired < .1f || speed > desired + 1;
    float throttle = brake ? 0 : std::clamp((desired + (desired - speed) * 1.1f) / car.tuning().top_speed, 0.f, 1.f);
    unit.stuck_time = speed < .5f && desired > 4 ? unit.stuck_time + dt : 0;
    if (unit.stuck_time > 2 && unit.stuck_time < 3.2f) throttle = -.35f;
    if (unit.stuck_time > 4) unit.stuck_time = 0;
    car.step({throttle, throttle < 0 ? -steer : steer, false, brake}, dt);
}
void Police::walk(PoliceUnit& unit, PoliceOfficer& officer, Player& player, std::size_t index, float dt) {
    auto& character = *officer.character;
    if (officer.seated) return;
    officer.fire_time = std::max(0.f, officer.fire_time - dt); officer.flash = std::max(0.f, officer.flash - dt);
    if (character.ragdolling() || !character.alive()) { character.step({}, dt); return; }
    const Vec3 position = character.position();
    const bool returning = wanted_.stars() && !unit.claimed && !unit.car->destroyed() && ((player.driving() && flat(player.position() - position).Length() > vehicle_return_range)
        || (wanted_.searching() && flat(unit.destination - position).Length() > 45));
    Vec3 target = returning ? unit.car->position() + unit.car->rotate(Vec3(index % 2 ? 2.f : -2.f, 0, .35f)) : unit.destination;
    if (!wanted_.stars()) target = unit.car->position() + unit.car->rotate(Vec3(3.5f, 0, index % 2 ? 3.f : -3.f));
    if (returning && (target - position).Length() < 1.8f && unit.car->velocity().Length() < 1.8f
        && visible(position + Vec3(0, 1, 0), target + Vec3(0, 1, 0), 5, unit.car->body_id())) {
        character.set_enabled(false); officer.seated = true; return;
    }
    Vec3 direction = flat(target - position).NormalizedOr(Vec3::sZero());
    const float distance = flat(target - position).Length();
    const Vec3 aim_point = player.driving() ? player.position() + player.car().rotate(Vec3(0, chassis_offset, 0))
        : player.position() + Vec3(0, .95f, 0);
    const bool aiming = wanted_.stars() >= 2 && !returning && !wanted_.searching()
        && (player.on_foot() || player.driving()) && player.character().alive()
        && visible(position + Vec3(0, 1.5f, 0), aim_point, player.driving() ? vehicle_fire_range : 45,
            {}, player.driving() ? player.car().body_id() : JPH::BodyID{});
    if (distance < (aiming ? 12.f : wanted_.stars() ? 1.4f : 1.f)) direction = Vec3::sZero();
    if (direction.LengthSq() > .1f && world_.camera_fraction(position + Vec3(0, .9f, 0), direction * 2.5f, unit.car->body_id()) < .8f) {
        const Vec3 side = direction.Cross(Vec3::sAxisY());
        direction = world_.camera_fraction(position + Vec3(0, .9f, 0), side * 2, unit.car->body_id())
            > world_.camera_fraction(position + Vec3(0, .9f, 0), -side * 2, unit.car->body_id()) ? side : -side;
    }
    character.step({direction, wanted_.stars() > 0 && !aiming, false,
        aiming ? (aim_point - position - Vec3(0, 1.4f, 0)).NormalizedOr(direction) : Vec3::sZero(), wanted_.stars() ? WeaponType::Pistol : WeaponType::Unarmed}, dt);
    if (aiming && officer.fire_time <= 0 && !arrested()) {
        const auto gun = character.held_weapon();
        const Vec3 origin = gun.position + gun.rotation * Vec3(0, .06f, 0);
        const float miss = (7 - wanted_.stars()) * .16f;
        const float phase = float(index) * 2.4f + spawn_sequence_ * .71f + officer.shots_fired * .77f;
        const Vec3 aim = aim_point + Vec3(std::sin(phase) * miss, std::cos(phase) * miss * (player.driving() ? .15f : 1.f), std::cos(phase * 1.7f) * miss);
        const Vec3 ray = (aim - origin).NormalizedOr(direction);
        const auto obstruction = trace_shot(world_, pedestrians_, origin, ray, 55, this, &character);
        if (obstruction.character == &player.character())
            player.character().take_damage(12, BodyPart(obstruction.part), ray * 16);
        else if (obstruction.character) { officer.fire_time = .3f; return; }
        else if (obstruction.car) obstruction.car->take_damage(12 * VehicleDamage::gunfire_multiplier);
        else if (obstruction.plane) obstruction.plane->take_damage(12 * VehicleDamage::gunfire_multiplier);
        ++officer.shots_fired;
        world_.emit_sound(SoundEffect::Pistol, origin);
        officer.fire_time = police_response(wanted_.stars()).fire_interval; officer.flash = .065f;
    }
}
void Police::prepare(Player& player, float dt) {
    for (auto& cooldown : crime_cooldowns_) cooldown = std::max(0.f, cooldown - dt);
    dispatch_time_ -= dt;
    if (report_time_ > 0) {
        report_time_ -= dt;
        if (report_time_ <= 0 && pending_crime_ >= 0) { wanted_.report(Crime(pending_crime_), report_position_); pending_crime_ = -1; dispatch_time_ = 1; }
    }
    observation_time_ += dt;
    if (observation_time_ >= .1f) {
        const float elapsed = observation_time_;
        observation_time_ = 0;
        bool seen = false;
        for (auto& unit : units_) if (unit.active) {
            for (auto& officer : unit.officers) if (officer.character->alive() && !officer.character->ragdolling() && !(unit.claimed && officer.seated)) {
                const Vec3 eye = officer.seated ? unit.car->position() + Vec3(0, 1.2f, 0) : officer.character->position() + Vec3(0, 1.5f, 0);
                const Vec3 target = player.driving() ? player.position() + player.car().rotate(Vec3(0, chassis_offset, 0))
                    : player.position() + Vec3(0, player.on_foot() ? 1.f : 1.7f, 0);
                seen |= visible(eye, target, police_response(wanted_.stars()).sight_range,
                    officer.seated ? unit.car->body_id() : JPH::BodyID{}, player.driving() ? player.car().body_id() : JPH::BodyID{});
            }
        }
        wanted_.step(player.position(), seen, elapsed);
        if (player.driving() && player.car().velocity().Length() > 30) crime(Crime::RecklessDriving, player.position());
    }
    int active = 0;
    for (auto& unit : units_) if (unit.active) {
        if (unit.claimed) for (auto& officer : unit.officers)
            if (!officer.seated && (officer.character->position() - player.position()).LengthSq() > 260 * 260
                && !camera_visible(officer.character->position())) {
                officer.character->set_enabled(false); officer.seated = true;
            }
        const bool living = std::any_of(unit.officers.begin(), unit.officers.end(), [](const PoliceOfficer& officer) { return officer.character->alive(); });
        const float distance = (unit.car->position() - player.position()).Length();
        const bool hidden = !camera_visible(unit.car->position()) && std::none_of(unit.officers.begin(), unit.officers.end(),
            [&](const PoliceOfficer& officer) { return !officer.seated && camera_visible(officer.character->position()); });
        const bool abandoned = unit.claimed && unit.car.get() != &player.car() && distance > 650;
        if ((!unit.claimed || abandoned) && distance > (!living ? 80.f : wanted_.stars() ? 650.f : 260.f) && hidden) {
            unit.active = unit.claimed = false; unit.car->set_simulated(false);
            for (auto& officer : unit.officers) { officer.character->set_enabled(false); officer.seated = true; }
        } else if (living && !unit.claimed && !unit.car->destroyed()) ++active;
    }
    const int desired = wanted_.stars() ? police_response(wanted_.stars()).cars : 1;
    if (active < desired && dispatch_time_ <= 0 && !arrested() && player.character().alive()) {
        for (std::size_t i = 0; i < units_.size(); ++i) if (!units_[i].active && spawn(units_[i], player, i)) break;
        dispatch_time_ = wanted_.stars() ? police_response(wanted_.stars()).dispatch_interval : 4;
    }
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto& unit = units_[i]; if (!unit.active) continue;
        unit.destination = wanted_.stars() ? wanted_.last_seen() : unit.car->position();
        const float distance = flat(player.position() - unit.car->position()).Length();
        if (wanted_.stars() && !wanted_.searching() && distance < dismount_range
            && (!player.driving() || player.car().velocity().Length() < 4)
            && visible(unit.car->position() + Vec3(0, 1.2f, 0),
                player.driving() ? player.position() + player.car().rotate(Vec3(0, chassis_offset, 0)) : player.position() + Vec3(0, 1, 0),
                dismount_range + 2, unit.car->body_id(), player.driving() ? player.car().body_id() : JPH::BodyID{}))
            for (std::size_t side = 0; side < unit.officers.size(); ++side) exit(unit, unit.officers[side], side);
        drive(unit, player, i, dt);
        for (auto& officer : unit.officers) if (!officer.seated) {
            officer.character->hit_by(player.car(), dt);
            if (traffic_) for (const auto& other : traffic_->cars()) officer.character->hit_by(*other.car, dt);
            for (const auto& other : units_) if (other.active) officer.character->hit_by(*other.car, dt);
        }
        if (pedestrians_) for (const auto& person : pedestrians_->people()) if (person.enabled) person.character->hit_by(*unit.car, dt);
    }
}
void Police::finish(Player& player, float dt) {
    bool arresting = false;
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto& unit = units_[i]; if (!unit.active) continue;
        for (std::size_t j = 0; j < unit.officers.size(); ++j) {
            auto& officer = unit.officers[j];
            if (officer.seated && unit.car->destroyed()) {
                if (!officer.character->pull_from(*unit.car, j ? 1 : -1))
                    officer.character->reset(unit.car->position() + Vec3(0, 2 + float(j), 0));
                officer.seated = false;
                officer.character->take_damage(100);
            } else if (officer.seated && unit.claimed) exit(unit, officer, j);
            const float health = officer.character->health();
            if (health < officer.previous_health && (officer.character->last_vehicle_hit() == &player.car()
                || (player.driving() && officer.character->touching(player.car()))))
                crime(health <= 0 ? Crime::OfficerHomicide : Crime::OfficerAssault, player.position(), officer.character.get());
            officer.previous_health = health;
            walk(unit, officer, player, i * 2 + j, dt);
            if (wanted_.stars() && !officer.seated && officer.character->alive() && !officer.character->ragdolling()
                && player.character().alive() && !player.character().ragdolling() && !player.character().swimming()
                && (player.on_foot() ? player.character().velocity().Length() < 1 && player.weapons().selected() == WeaponType::Unarmed
                    : player.driving() && player.car().velocity().Length() < 1)
                && (officer.character->position() - player.position()).LengthSq() < (player.driving() ? 3.5f * 3.5f : 2 * 2)) arresting = true;
        }
    }
    arrest_time_ = arresting ? arrest_time_ + dt : arrested() ? arrest_time_ : 0;
    if (pedestrians_) for (std::size_t i = 0; i < pedestrians_->people().size(); ++i) {
        const auto& person = pedestrians_->people()[i];
        const float health = person.character->health();
        if (person.enabled && (person.character->last_vehicle_hit() == &player.car()
            || (player.driving() && person.character->touching(player.car())))
            && (health < civilian_health_[i] || (person.character->ragdolling() && !civilian_ragdoll_[i])))
            crime(health <= 0 ? Crime::Homicide : Crime::Assault, player.position(), person.character.get());
        civilian_health_[i] = health; civilian_ragdoll_[i] = person.character->ragdolling();
    }
}
bool Police::steal(Car& car) {
    if (car.destroyed()) return false;
    for (auto& unit : units_) if (unit.car.get() == &car) {
        if (unit.claimed) return true;
        auto& driver = unit.officers.front();
        if (driver.seated && driver.character->alive()) {
            if (!driver.character->pull_from(car)) return false;
            driver.seated = false;
        }
        exit(unit, unit.officers[1], 1);
        unit.claimed = true; unit.pit_time = 0;
        Character* observer = nullptr;
        for (const auto& officer : unit.officers) if (!officer.seated && officer.character->alive()
            && visible(officer.character->position() + Vec3(0, 1.5f, 0), car.position() + Vec3(0, 1.7f, 0), 40, car.body_id())) observer = officer.character.get();
        crime(Crime::PoliceVehicleTheft, car.position(), observer);
        return true;
    }
    return false;
}
void Police::raycast(Vec3 origin, Vec3 direction, ShotHit& hit, const Character* ignore) const {
    for (const auto& unit : units_) if (unit.active) for (const auto& officer : unit.officers) if (!officer.seated && officer.character.get() != ignore) {
        BodyPart part; float distance = hit.distance;
        if (officer.character->raycast(origin, direction, distance, part))
            hit = {officer.character.get(), int(part), origin + direction.NormalizedOr(Vec3(0, 0, -1)) * distance, distance};
    }
}
void Police::clear() {
    wanted_.clear(); report_time_ = observation_time_ = 0; pending_crime_ = -1; arrest_time_ = 0; dispatch_time_ = 3;
    crime_cooldowns_.fill(0);
    for (auto& unit : units_) {
        for (auto& officer : unit.officers) { officer.character->set_enabled(false); officer.seated = true; }
        if (!unit.claimed) { unit.active = false; unit.car->set_simulated(false); }
    }
}
} // namespace forza
