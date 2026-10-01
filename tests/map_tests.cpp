#include "environment.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace forza;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Vec3 flat(Vec3 p) { p.SetY(0); return p; }
float segment_distance(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 d = flat(b - a);
    const float t = std::clamp(flat(p - a).Dot(d) / d.LengthSq(), 0.0f, 1.0f);
    return flat(p - a - d * t).Length();
}
float cross(Vec3 a, Vec3 b) { return a.GetX() * b.GetZ() - a.GetZ() * b.GetX(); }
bool connected(const Road& a, const Road& b) {
    const Vec3 d = a.b - a.a, e = b.b - b.a, delta = b.a - a.a;
    const float divisor = cross(d, e);
    if (std::abs(divisor) > .001f) {
        const float t = cross(delta, e) / divisor, u = cross(delta, d) / divisor;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return true;
    }
    const float width = (a.width + b.width) / 2;
    return segment_distance(a.a, b.a, b.b) <= width || segment_distance(a.b, b.a, b.b) <= width
        || segment_distance(b.a, a.a, a.b) <= width || segment_distance(b.b, a.a, a.b) <= width;
}
void map_layout(const Environment& map, PhysicsWorld& world) {
    const auto& roads = map.roads();
    std::vector<bool> reached(roads.size(), false);
    reached[0] = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < roads.size(); ++i) if (reached[i])
            for (std::size_t j = 0; j < roads.size(); ++j) if (!reached[j] && connected(roads[i], roads[j])) {
                reached[j] = true; changed = true;
            }
    }
    require(std::all_of(reached.begin(), reached.end(), [](bool b) { return b; }), "road network contains disconnected streets");
    require(map.bridges().size() == 8 && map.ports().size() == 6, "regional bridges or ports missing");
    require(map.islands().back().center.GetZ() - map.islands().front().center.GetZ() > 7000, "region is not substantially larger than the old island");
    int kinds[11]{};
    float tallest = 0;
    for (const auto& building : map.buildings()) {
        ++kinds[int(building.kind)]; tallest = std::max(tallest, building.size.GetY());
        // Sample every two meters along streets near the full reserved plot.
        for (const auto& road : roads) {
            if (segment_distance(building.center, road.a, road.b) > building.size.Length() / 2 + road.width) continue;
            const Vec3 d = road.b - road.a;
            for (float t = 0; t <= 1; t += 2 / d.Length()) {
                const Vec3 p = road.a + d * t;
                require(std::abs(p.GetX() - building.center.GetX()) > building.size.GetX() / 2 + road.width / 2
                    || std::abs(p.GetZ() - building.center.GetZ()) > building.size.GetZ() / 2 + road.width / 2,
                    "building footprint obstructs a street");
            }
        }
    }
    require(tallest > 150 && kinds[int(BuildingKind::Mall)] >= 2 && kinds[int(BuildingKind::Hotel)] >= 15
        && kinds[int(BuildingKind::Cafe)] >= 10 && kinds[int(BuildingKind::Club)] >= 8
        && kinds[int(BuildingKind::House)] >= 20 && kinds[int(BuildingKind::GasStation)] == 5,
        "district landmarks or businesses missing");
    for (std::size_t i = 6; i < map.islands().size(); ++i) {
        const auto& key = map.islands()[i];
        bool gas = false;
        for (const auto& b : map.buildings())
            if (b.kind == BuildingKind::GasStation && flat(b.center - key.center).Length() < 250) gas = true;
        require(gas, "a Key has no gas station");
    }
    GroundHit hit;
    int samples = 0;
    for (const auto& road : roads) {
        const Vec3 d = road.b - road.a, side = d.Normalized().Cross(Vec3::sAxisY());
        const int count = int(d.Length() / 8) + 1;
        for (int i = 0; i <= count; ++i) for (float offset : {-road.width * .35f, 0.0f, road.width * .35f}) {
            const Vec3 p = road.a + d * (float(i) / count) + side * offset;
            const float height = map.height(p.GetX(), p.GetZ());
            require(height >= 2.9f, "road or bridge approach enters water");
            require(world.cast_ground(p + Vec3(0, 60, 0), Vec3(0, -1, 0), 80, hit), "road surface has a physics hole");
            require(std::abs(hit.point.GetY() - height) < .01f, "bridge/road physics differs from visible surface");
            require(hit.normal.GetY() > .98f, "bridge ramp is too steep");
            ++samples;
        }
    }
    for (const auto& port : map.ports()) {
        const Vec3 dir = port.east ? Vec3::sAxisX() : Vec3::sAxisZ();
        for (float d = 10; d < 220; d += 10)
            require(world.cast_ground(port.center + dir * d + Vec3(0, 20, 0), Vec3(0, -1, 0), 30, hit)
                && std::abs(hit.point.GetY() - port.center.GetY()) < .01f, "marina deck is not solid");
    }
    std::cout << roads.size() << " connected streets, " << samples << " aligned road samples, "
        << map.buildings().size() << " buildings, " << map.trees().size() << " model trees; tallest " << tallest << " m\n";
}
void drive_bridges(const Environment& map, PhysicsWorld& world, Car& car) {
    for (const auto& bridge : map.bridges()) {
        const Vec3 dir = (bridge.b - bridge.a).Normalized(), side = dir.Cross(Vec3::sAxisY());
        Vec3 start = bridge.a - dir * 20 + side * 4;
        start.SetY(map.height(start.GetX(), start.GetZ()) + .56f);
        car.reset(start, std::atan2(-dir.GetX(), -dir.GetZ()));
        float progress = -20;
        for (int i = 0; i < 120 * 140 && progress < (bridge.b - bridge.a).Length() + 15; ++i) {
            const float speed = car.velocity().Dot(dir);
            const Vec3 direction = flat(car.forward()).Normalized();
            const float lane = flat(car.position() - bridge.a).Dot(side);
            const Vec3 target = dir * 12 + side * (4 - lane);
            const float curvature = 2 * target.Dot(-direction.Cross(Vec3::sAxisY())) / target.LengthSq();
            const float steer = std::atan(car.tuning().wheelbase * curvature) / car.tuning().max_steer;
            car.step({speed < 13 ? .55f : .08f, std::clamp(steer, -1.0f, 1.0f), false, speed > 15}); world.step();
            const Vec3 p = car.position();
            progress = flat(p - bridge.a).Dot(dir);
            if (std::abs(flat(p - bridge.a).Dot(side) - 4) >= 3)
                std::cout << bridge.name << ": progress " << progress << ", lane " << flat(p - bridge.a).Dot(side)
                    << ", speed " << speed << ", height " << p.GetY() << '\n';
            require(std::abs(flat(p - bridge.a).Dot(side) - 4) < 3, "car drifted out of its bridge lane");
            require(std::abs(p.GetY() - map.height(p.GetX(), p.GetZ()) - .56f) < 1,
                "car fell through a bridge or lost contact at an approach");
            require(car.rotate(Vec3::sAxisY()).GetY() > .95f, "car tipped on a bridge ramp");
        }
        require(progress > (bridge.b - bridge.a).Length() + 10, "car could not cross a connecting bridge");
    }
    const auto& rail = map.barriers()[map.barriers().size() / 2];
    const Vec3 across = Quat::sRotation(Vec3::sAxisY(), rail.yaw) * Vec3::sAxisX();
    require(world.camera_fraction(rail.center + across * 3, across * -6) < .5f, "bridge rail has no collision");
    std::cout << "Drove across all eight bridges and both approaches.\n";
}
void traffic_streaming(const Environment& map, PhysicsWorld& world, Car& starter) {
    starter.reset(map.spawn());
    Traffic traffic(world, map);
    require(traffic.cars().size() >= 250, "regional traffic density too low");
    Car& stolen = *traffic.cars().front().car;
    traffic.steal(stolen);
    const Vec3 far_start = traffic.cars().back().car->position();
    for (std::size_t region : {std::size_t(1), std::size_t(6), std::size_t(8), std::size_t(10)}) {
        const Vec3 p = map.islands()[region].center;
        for (int i = 0; i < 120 * 2; ++i) {
            starter.step({0, 0, false, true});
            traffic.step(nullptr, starter, nullptr, nullptr, p); world.step();
        }
        int active = 0, local = 0;
        for (const auto& vehicle : traffic.cars()) if (vehicle.npc && vehicle.car->simulated()) {
            ++active;
            if (flat(vehicle.car->position() - p).Length() < 650) ++local;
        }
        require(active <= 48 && local >= 5, "traffic failed to stream densely around a distant district");
        require(stolen.simulated() && !traffic.is_npc(&stolen), "traveling despawned or reclaimed a stolen car");
    }
    require((traffic.cars().back().car->position() - far_start).Length() > 10, "distant traffic stopped advancing");
    std::cout << traffic.cars().size() << " traffic cars stream across Miami Beach and the Keys; ownership retained.\n";
}
}
int main() {
    try {
        Environment map; PhysicsWorld world(map); Car car(world);
        map_layout(map, world); drive_bridges(map, world, car); traffic_streaming(map, world, car);
        std::cout << "All Miami map checks passed.\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Map check failed: " << error.what() << '\n'; return 1;
    }
}
