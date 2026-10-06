#include "pedestrians.hpp"
#include "airport.hpp"
#include "environment.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>

namespace ambaretto {
namespace {
Vec3 flat(Vec3 p) { p.SetY(0); return p; }
float segment_distance(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 delta = flat(b - a);
    return flat(p - a - delta * std::clamp(flat(p - a).Dot(delta) / std::max(.001f, delta.LengthSq()), 0.0f, 1.0f)).LengthSq();
}
std::size_t next_point(const Pedestrian& person, std::size_t target, std::size_t count) {
    return (target + (person.reverse ? count - 1 : 1)) % count;
}
} // namespace

bool Pedestrians::walkable(Vec3 p) const {
    const float ground = environment_.terrain_height(p.GetX(), p.GetZ());
    if (ground < 1 || std::abs(environment_.ground_height(p.GetX(), p.GetZ()) - ground) > .2f) return false;
    if (!environment_.city()) for (const auto& airport : airports) if (airport.contains(p.GetX(), p.GetZ())) return false;
    for (const auto& building : environment_.buildings()) {
        const Vec3 relative = p - building.solid_center(), half = building.solid_size() / 2;
        if (std::abs(relative.GetX()) < half.GetX() + .5f && std::abs(relative.GetZ()) < half.GetZ() + .5f) return false;
    }
    for (const auto& tree : environment_.trees())
        if (flat(p - tree.base).LengthSq() < std::pow(Tree::model_trunk_radius * tree.scale() + .5f, 2)) return false;
    for (const auto& barrier : environment_.barriers()) {
        const Vec3 relative = barrier.rotation().Conjugated() * (p + Vec3(0, .9f, 0) - barrier.center);
        if (std::abs(relative.GetX()) < barrier.size.GetX() / 2 + .4f
            && std::abs(relative.GetZ()) < barrier.size.GetZ() / 2 + .4f
            && std::abs(relative.GetY()) < barrier.size.GetY() / 2 + .9f) return false;
    }
    return true;
}

Pedestrians::Pedestrians(PhysicsWorld& world, const Environment& environment) : world_(world), environment_(environment) {
    if (environment.city()) for (const auto& road : environment.road_segments()) {
        const Vec3 side = flat(road.b-road.a).Normalized().Cross(Vec3::sAxisY())*(road.width/2);
        for (float sign : {-1.f,1.f}) {
            std::vector<Vec3> route;
            const Vec3 a = road.a+side*sign, b = road.b+side*sign;
            const int count = std::max(2,int((b-a).Length()/3)); bool valid = true;
            for (int i = 0; i <= count; ++i) {
                Vec3 p = a+(b-a)*(float(i)/count)+Vec3(0,.08f,0);
                if (!walkable(p)) { valid = false; break; }
                route.push_back(p);
            }
            if (valid) { for (int i = count-1; i > 0; --i) route.push_back(route[std::size_t(i)]); routes_.push_back(std::move(route)); }
        }
    }
    for (const auto& loop : environment.street_routes()) {
        if (!loop.closed || loop.corners.size() < 3) continue;
        const std::size_t count = loop.corners.size();
        float area = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 a = loop.corners[i], b = loop.corners[(i + 1) % count];
            area += a.GetX() * b.GetZ() - b.GetX() * a.GetZ();
        }
        const float inward = area >= 0 ? 1.0f : -1.0f;
        std::vector<Vec3> sidewalks, route;
        std::vector<float> offsets;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 a = loop.corners[i], b = loop.corners[(i + 1) % count];
            const Vec3 direction = flat(b - a).Normalized();
            float width = loop.width;
            for (const auto& road : environment.road_segments())
                if (road.bridge < 0 && segment_distance((a + b) / 2, road.a, road.b) < .01f
                    && std::abs(direction.Dot(flat(road.b - road.a).Normalized())) > .99f)
                    width = std::max(width, road.width);
            // Verge trees sit 2.25 m beyond pavement; walk between them and the curb.
            offsets.push_back(width / 2 + 1.0f);
        }
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t before = (i + count - 1) % count;
            const Vec3 incoming = flat(loop.corners[i] - loop.corners[before]).Normalized().Cross(Vec3::sAxisY());
            const Vec3 outgoing = flat(loop.corners[(i + 1) % count] - loop.corners[i]).Normalized().Cross(Vec3::sAxisY());
            // Intersect the two sidewalks, preserving their different street widths.
            const float dot = incoming.Dot(outgoing), denominator = std::max(.05f, 1 - dot * dot);
            const Vec3 offset = std::abs(dot) > .99f ? outgoing * offsets[i]
                : incoming * ((offsets[before] - dot * offsets[i]) / denominator)
                    + outgoing * ((offsets[i] - dot * offsets[before]) / denominator);
            sidewalks.push_back(loop.corners[i] + offset * inward);
        }
        bool valid = true;
        for (std::size_t i = 0; i < count && valid; ++i) {
            const Vec3 a = sidewalks[i], b = sidewalks[(i + 1) % count];
            const int samples = std::max(1, int(flat(b - a).Length() / 3));
            for (int sample = 0; sample < samples; ++sample) {
                Vec3 p = a + (b - a) * (float(sample) / samples);
                p.SetY(environment.terrain_height(p.GetX(), p.GetZ()) + .08f);
                if (!walkable(p)) { valid = false; break; }
                route.push_back(p);
            }
        }
        if (valid && !route.empty()) routes_.push_back(std::move(route));
    }
    // A fixed pool bounds both walking and articulated ragdoll physics costs.
    for (unsigned i = 0; i < 64; ++i) {
        Pedestrian person;
        person.character = std::make_unique<Character>(world);
        person.character->set_enabled(false);
        person.appearance = i;
        person.walking_speed = 1.25f + float(i % 9) * .09f;
        person.reverse = i % 2 != 0;
        people_.push_back(std::move(person));
    }
    stream(nullptr, nullptr, environment.spawn(), true);
}

void Pedestrians::stream(const Car* starter, const Traffic* traffic, Vec3 player_position, bool initial) {
    struct Spawn { std::size_t route, point; };
    std::vector<Spawn> candidates;
    for (std::size_t route = 0; route < routes_.size(); ++route)
        for (std::size_t point = 0; point < routes_[route].size(); ++point) {
            const float distance = flat(routes_[route][point] - player_position).LengthSq();
            if (distance > (initial ? 12 * 12 : 80 * 80) && distance < 270 * 270) candidates.push_back({route, point});
        }
    for (auto& person : people_) {
        const Vec3 old_position = person.character->position();
        const float distance = flat(old_position - player_position).LengthSq();
        if (person.enabled && distance < 330 * 330
            && (distance < 80 * 80 || (!environment_.submerged(old_position) && person.stuck_time < 12))) continue;
        person.enabled = false;
        person.character->set_enabled(false);
        if (candidates.empty()) continue;
        const std::size_t start = (spawn_sequence_++ * 47) % candidates.size();
        for (std::size_t attempt = 0; attempt < candidates.size(); ++attempt) {
            const auto spawn = candidates[(start + attempt) % candidates.size()];
            const Vec3 p = routes_[spawn.route][spawn.point];
            bool occupied = starter && flat(p - starter->position()).LengthSq() < 4 * 4;
            if (traffic) for (const auto& vehicle : traffic->cars())
                if (vehicle.car->simulated() && flat(p - vehicle.car->position()).LengthSq() < 4 * 4) { occupied = true; break; }
            for (const auto& other : people_) if (other.enabled && flat(p - other.character->position()).LengthSq() < 3 * 3) {
                occupied = true; break;
            }
            if (occupied || !person.character->can_stand_at(p)) continue;
            person.route = spawn.route;
            person.target = next_point(person, spawn.point, routes_[person.route].size());
            const Vec3 direction = routes_[person.route][person.target] - p;
            person.character->reset(p, std::atan2(-direction.GetX(), -direction.GetZ()));
            person.character->revive();
            person.character->set_enabled(true);
            person.enabled = true; person.was_ragdoll = false; person.stuck_time = 0;
            person.fear_time = person.call_time = 0;
            break;
        }
    }
}

void Pedestrians::prepare(const Car& starter, const Traffic* traffic, Vec3 player_position, float dt) {
    stream_time_ -= dt;
    if (stream_time_ <= 0) { stream(&starter, traffic, player_position); stream_time_ = .5f; }
    for (auto& person : people_) if (person.enabled) {
        person.character->hit_by(starter, dt);
        if (traffic && !person.character->ragdolling()) for (const auto& vehicle : traffic->cars())
            if (vehicle.car->simulated()) {
                person.character->hit_by(*vehicle.car, dt);
                if (person.character->ragdolling()) break;
            }
    }
}

void Pedestrians::step(const Car&, const Traffic*, Vec3 player_position, float dt) {
    for (auto& person : people_) if (person.enabled) {
        auto& character = *person.character;
        person.fear_time = std::max(0.f, person.fear_time - dt);
        person.call_time = std::max(0.f, person.call_time - dt);
        if (character.ragdolling()) { character.step({}, dt); person.was_ragdoll = true; continue; }
        if (person.call_time > 0) { character.step({}, dt); continue; }
        const auto& route = routes_[person.route];
        if (person.was_ragdoll) {
            const auto nearest = std::min_element(route.begin(), route.end(), [&](Vec3 a, Vec3 b) {
                return flat(a - character.position()).LengthSq() < flat(b - character.position()).LengthSq();
            });
            person.target = next_point(person, std::size_t(nearest - route.begin()), route.size());
            person.was_ragdoll = false;
        }
        if (flat(route[person.target] - character.position()).LengthSq() < .6f * .6f)
            person.target = next_point(person, person.target, route.size());
        const Vec3 direction = flat(route[person.target] - character.position()).NormalizedOr(Vec3::sZero());
        const Vec3 before = character.position();
        bool blocked = false;
        for (const auto& other : people_) if (&other != &person && other.enabled) {
            const Vec3 offset = flat(other.character->position() - before);
            if (offset.LengthSq() < 1.2f * 1.2f && offset.Dot(direction) > .2f) { blocked = true; break; }
        }
        if (flat(player_position - before).LengthSq() < 1.2f * 1.2f) blocked = true;
        character.step({blocked ? Vec3::sZero() : direction * (person.fear_time > 0 ? .75f : person.walking_speed / 3.2f), person.fear_time > 0}, dt);
        person.stuck_time = flat(character.position() - before).LengthSq() < .05f * .05f * dt * dt
            ? person.stuck_time + dt : 0;
        if (person.stuck_time > 3 && person.stuck_time < 3 + dt * 1.5f) {
            person.reverse = !person.reverse;
            person.target = next_point(person, person.target, route.size());
        }
    }
}
unsigned Pedestrians::alarm(Vec3 origin, float radius) {
    unsigned witnesses = 0;
    for (auto& person : people_) if (person.enabled && person.character->alive()) {
        const Vec3 position = person.character->position();
        if ((position - origin).LengthSq() > radius * radius) continue;
        const bool sees = world_.camera_fraction(position + Vec3(0, 1.5f, 0), origin + Vec3(0, 1.1f, 0) - position - Vec3(0, 1.5f, 0)) > .96f;
        if (sees && !person.character->ragdolling()) ++witnesses;
        if (person.fear_time <= 0) {
            const auto& route = routes_[person.route];
            const Vec3 direction = flat(route[person.target] - position);
            if (direction.Dot(flat(origin - position)) > 0) { person.reverse = !person.reverse; person.target = next_point(person, person.target, route.size()); }
            person.call_time = sees ? 1.6f : 0;
        }
        person.fear_time = 14;
    }
    return witnesses;
}
} // namespace ambaretto
