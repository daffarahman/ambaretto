#include "environment.hpp"
#include "airport.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
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
Vec3 closest_point(Vec3 p, const Road& road) {
    const Vec3 d = flat(road.b - road.a);
    const float t = std::clamp(flat(p - road.a).Dot(d) / d.LengthSq(), 0.0f, 1.0f);
    return road.a + (road.b - road.a) * t;
}
float cross(Vec3 a, Vec3 b) { return a.GetX() * b.GetZ() - a.GetZ() * b.GetX(); }
void coastlines(const Environment& map) {
    constexpr float pi = 3.14159265359f;
    for (int index : {1, 7, 8, 9}) {
        const auto& island = map.islands()[index];
        float shortest = 100, longest = 0;
        for (int sample = 0; sample < 32; ++sample) {
            const float angle = index == 1 ? -pi / 2 + pi * sample / 31 : 2 * pi * sample / 32;
            float inside = 0, outside = 1.6f;
            for (int step = 0; step < 16; ++step) {
                const float radius = (inside + outside) / 2;
                const float x = island.center.GetX() + std::cos(angle) * island.radius_x * radius;
                const float z = island.center.GetZ() + std::sin(angle) * island.radius_z * radius;
                if (map.coast_radius(x, z) < 1) inside = radius; else outside = radius;
            }
            shortest = std::min(shortest, inside); longest = std::max(longest, inside);
        }
        require(longest - shortest > .20f, "island shoreline is still a plain ellipse");
        require(map.terrain_height(island.center.GetX(), island.center.GetZ()) >= 2.9f, "island town center is submerged");
    }
    require(map.terrain_height(-600, -1020) < 0 && map.terrain_height(340, 80) < 0 && map.terrain_height(1090, -650) < 0,
        "unused mainland or Beach waterfront is still a flat field");
    for (int index : {0, 3, 7}) {
        const auto& bridge = map.bridges()[index];
        const Vec3 side = flat(bridge.b - bridge.a).Normalized().Cross(Vec3::sAxisY());
        for (float t : {.45f, .5f, .55f}) for (float offset : {-150.0f, -60.0f, 0.0f, 60.0f, 150.0f}) {
            const Vec3 p = bridge.a + (bridge.b - bridge.a) * t + side * offset;
            require(map.terrain_height(p.GetX(), p.GetZ()) < -1,
                "PortMiami or Keys shores touch beneath their bridge");
        }
    }
    const Vec3 river[]{{-1313, 0, -400}, {-1290, 0, 0}, {-1284, 0, 200}, {-1288, 0, 400},
        {-1326, 0, 600}, {-1464, 0, 850}, {-1495, 0, 1050}};
    for (std::size_t i = 1; i < std::size(river); ++i)
        for (int step = 0; step <= 20; ++step) {
            const Vec3 p = river[i - 1] + (river[i] - river[i - 1]) * (float(step) / 20);
            require(map.terrain_height(p.GetX(), p.GetZ()) < -1, "western river is blocked before reaching the sea");
        }
    require(map.terrain_height(-1332, 654) < -1 && std::abs(map.ground_height(-1332, 654) - Environment::road_level) < .001f,
        "western river bridge is raised or missing its level deck");
    for (Vec3 p : {Vec3(-900, 0, -650), Vec3(-780, 0, -420), Vec3(-800, 0, -1000), Vec3(-200, 0, -860),
            Vec3(-1170, 0, -600), Vec3(-1290, 0, -490), Vec3(-1115, 0, -420), Vec3(-975, 0, -120)})
        require(map.terrain_height(p.GetX(), p.GetZ()) < -1 && map.coast_radius(p.GetX(), p.GetZ()) > 1,
            "marked northern waterfront is still land");
    for (Vec3 p : {Vec3(-1040, 0, -550), Vec3(-360, 0, -840), Vec3(-540, 0, -460), Vec3(-120, 0, -720)})
        require(map.terrain_height(p.GetX(), p.GetZ()) >= 2.9f, "northern bay flooded the airport or developed shore");
    for (Vec3 p : {Vec3(-920, 0, -110), Vec3(-720, 0, 160), Vec3(-730, 0, 360), Vec3(-990, 0, 700),
            Vec3(-960, 0, 1000), Vec3(-540, 0, 950), Vec3(-250, 0, 990)})
        require(map.terrain_height(p.GetX(), p.GetZ()) < -1 && map.coast_radius(p.GetX(), p.GetZ()) > 1,
            "marked southern channel or coastal inlet is still land");
    for (Vec3 p : {Vec3(-1040, 0, 550), Vec3(-600, 0, 350), Vec3(-1180, 0, 700)})
        require(map.terrain_height(p.GetX(), p.GetZ()) >= 2.9f, "southern inlets flooded the runway or neighboring land");
    for (Vec3 p : {Vec3(-930, 0, -110), Vec3(-740, 0, 240), Vec3(-950, 0, 750)})
        require(map.terrain_height(p.GetX(), p.GetZ()) < -1
            && std::abs(map.ground_height(p.GetX(), p.GetZ()) - (p.GetZ() < 0 ? Airport::elevation : Environment::road_level)) < .001f,
            "southern water crossing is filled in or missing its level deck");
    const auto crossing = std::find_if(map.bridges().begin(), map.bridges().end(), [](const Bridge& bridge) {
        return std::string_view(bridge.name) == "INTERSTATE CITY LOOP" && std::abs(bridge.a.GetZ() + 690) < .01f;
    });
    require(crossing != map.bridges().end(), "northern bay has no highway bridge");
    for (int step = 0; step <= 100; ++step) {
        const Vec3 p = crossing->point(float(step) / 100);
        if (std::abs(map.ground_height(p.GetX(), p.GetZ()) - Environment::road_level) >= .001f)
            std::cout << "Northern crossing at " << p.GetX() << ',' << p.GetZ()
                << ": deck " << p.GetY() << ", ground " << map.ground_height(p.GetX(), p.GetZ()) << '\n';
        require(std::abs(p.GetY() - Environment::road_level) < .001f
            && std::abs(map.ground_height(p.GetX(), p.GetZ()) - Environment::road_level) < .001f,
            "northern highway crossing is raised, sloped or missing its deck");
        if (p.GetX() > -950 && p.GetX() < -500)
            require(map.terrain_height(p.GetX(), p.GetZ()) < -1, "highway verge fills the northern bay");
    }
    std::cout << "Irregular island outlines and waterfront inlets checked.\n";
}
bool connected(const Environment& map, const Road& a, const Road& b) {
    const auto same_level = [&](Vec3 p, Vec3 q) {
        return std::abs(map.road_height(a, p.GetX(), p.GetZ())
            - map.road_height(b, q.GetX(), q.GetZ())) < .5f;
    };
    const Vec3 d = a.b - a.a, e = b.b - b.a, delta = b.a - a.a;
    const float divisor = cross(d, e);
    if (std::abs(divisor) > .001f) {
        const float t = cross(delta, e) / divisor, u = cross(delta, d) / divisor;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
            const Vec3 p = a.a + d * t;
            if (same_level(p, p)) return true;
        }
    }
    const float width = (a.width + b.width) / 2;
    for (Vec3 p : {a.a, a.b}) {
        const Vec3 q = closest_point(p, b);
        if (flat(p - q).Length() <= width && same_level(p, q)) return true;
    }
    for (Vec3 q : {b.a, b.b}) {
        const Vec3 p = closest_point(q, a);
        if (flat(p - q).Length() <= width && same_level(p, q)) return true;
    }
    return false;
}
void map_layout(const Environment& map, PhysicsWorld& world) {
    const auto& roads = map.roads();
    std::vector<bool> reached(roads.size(), false);
    reached[0] = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < roads.size(); ++i) if (reached[i])
            for (std::size_t j = 0; j < roads.size(); ++j) if (!reached[j] && connected(map, roads[i], roads[j])) {
                reached[j] = true; changed = true;
            }
    }
    for (std::size_t i = 0; i < roads.size(); ++i) if (!reached[i])
        std::cout << "Disconnected street: " << roads[i].name << " at " << roads[i].a.GetX() << ',' << roads[i].a.GetZ() << '\n';
    require(std::all_of(reached.begin(), reached.end(), [](bool b) { return b; }), "road network contains disconnected streets or highway ramps");
    require(!map.highways().empty() && map.ports().size() == 6, "regional highways or marinas missing");
    const float regional_length = map.islands().back().center.GetZ() - map.islands().front().center.GetZ();
    require(regional_length > 3000 && regional_length < 5000, "region did not retain all Keys at the compact scale");
    for (const auto& road : roads) require(road.width >= 6 && road.width <= 24, "street width is outside its intended range");
    for (const auto& road : roads)
        if (std::string_view(road.name) == "US 1 GRAND BOULEVARD" || std::string_view(road.name) == "BEACH TO KEYS SKYWAY"
            || std::string_view(road.name) == "KEYS EXIT RAMP" || (road.a.GetZ() >= 1260 && road.width > 8))
            require(road.width == Environment::highway_width, "highway narrows between the flyover and the Keys");
    int kinds[int(BuildingKind::Office) + 1]{};
    float tallest = 0, frontage_gap = 0;
    for (const auto& building : map.buildings()) {
        ++kinds[int(building.kind)]; tallest = std::max(tallest, building.size.GetY());
        float gap = 10000;
        for (const auto& road : roads) gap = std::min(gap, segment_distance(building.center, road.a, road.b)
            - std::max(building.size.GetX(), building.size.GetZ()) / 2 - road.width / 2);
        frontage_gap += std::max(0.0f, gap);
        if (building.kind == BuildingKind::House)
            require(building.size.GetX() <= 14 && building.size.GetZ() <= 14 && building.size.GetY() <= 6.3f, "house is not at player scale");
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
    std::cout << "Compact layout: " << map.buildings().size() << " buildings; mean frontage gap "
        << frontage_gap / map.buildings().size() << " m; houses " << kinds[int(BuildingKind::House)] << '\n';
    require(map.buildings().size() >= 800 && frontage_gap / map.buildings().size() < 10, "streets still have sparse building frontage");
    // The interstate and outer South Beach street retain continuous seaside views.
    for (const auto& road : roads)
        require(std::string_view(road.name) != "BAYFRONT AVENUE", "removed coastal avenue remains");
    for (int bridge : {0, 2})
        require(map.bridges()[bridge].a.GetX() == 180, "downtown causeway does not join interstate");
    bool airport_connector = false, airport_main_street = false;
    for (const auto& road : roads) {
        if (std::string_view(road.name) == "AIRPORT CROSS STREET") {
            airport_connector = true;
            require(road.width == 18 && std::abs(road.a.GetZ() - 240) < .01f
                && std::abs(road.b.GetZ() - 240) < .01f, "airport cross street misses marked alignment");
        }
        if (std::string_view(road.name) == "AIRPORT MAIN STREET"
            || (std::string_view(road.name) == "CAUSEWAY APPROACH" && road.a.GetZ() == -240)) {
            airport_main_street = true;
            require(road.width == 18, "northern airport corridor has a narrow section");
        }
    }
    require(airport_connector && airport_main_street, "airport cross connection or widened corridor missing");
    for (float z = -650; z < 750; z += 40) {
        int continuations = 0;
        for (const auto& road : roads) {
            const std::string_view name(road.name);
            if (name != "AIRPORT WORKSHOPS" && name != "AIRPORT RESIDENTIAL" && name != "AIRPORT NEIGHBORHOOD LANE") continue;
            require(std::abs(road.a.GetX() + 1180) < .001f && std::abs(road.b.GetX() + 1180) < .001f
                && road.width == 7, "airport neighborhood road does not align with its bridge");
            if (z >= std::min(road.a.GetZ(), road.b.GetZ()) && z < std::max(road.a.GetZ(), road.b.GetZ())) ++continuations;
        }
        require(continuations == 1, "airport neighborhood road is doubled or disconnected");
    }
    int inland_frontage = 0;
    for (const auto& b : map.buildings()) {
        const float left = b.center.GetX() - b.size.GetX() / 2;
        const float right = b.center.GetX() + b.size.GetX() / 2;
        if (std::abs(b.center.GetZ()) < 180) {
            require(!(right > 171 && left < 420), "mainland building blocks the interstate beach view");
            require(!(left > 1474 && left < 1630), "South Beach building blocks the beach view");
            if ((right < 171 && right > 120) || (right < 1466 && right > 1400)) ++inland_frontage;
        }
    }
    require(inland_frontage >= 10, "coastal streets lost their inland building frontage");
    require(tallest > 70 && tallest < 110 && kinds[int(BuildingKind::Mall)] >= 2 && kinds[int(BuildingKind::Hotel)] >= 15
        && kinds[int(BuildingKind::Cafe)] >= 10 && kinds[int(BuildingKind::Club)] >= 8
        && kinds[int(BuildingKind::House)] >= 250
        && kinds[int(BuildingKind::Apartment)] >= 20 && kinds[int(BuildingKind::Office)] >= 10 && kinds[int(BuildingKind::Shop)] >= 50,
        "district landmarks or businesses missing");
    for (std::size_t i = 6; i < map.islands().size(); ++i) {
        const auto& key = map.islands()[i];
        require(key.radius_x == 150 && key.radius_z == 300, "Key is wider than Ngawish");
        int houses = 0, other_buildings = 0;
        for (const auto& b : map.buildings())
            if (std::hypot((b.center.GetX() - key.center.GetX()) / key.radius_x, (b.center.GetZ() - key.center.GetZ()) / key.radius_z) < 1) {
                if (b.kind == BuildingKind::House) {
                    ++houses;
                    require(b.size.GetX() <= 10 && b.size.GetZ() <= 10 && b.size.GetY() <= 3.5f, "Keys house is too large");
                    for (const auto& road : roads) if (std::string_view(road.name) == "OVERSEAS HIGHWAY")
                        require(segment_distance(b.center, road.a, road.b) > 30, "Keys house fronts the main highway");
                }
                else {
                    ++other_buildings;
                    require(b.kind == (i == 6 ? BuildingKind::GasStation : BuildingKind::Terminal)
                        && b.size.GetY() <= 5, "Key contains a tall or unwanted non-house building");
                }
            }
        if (i == 6) {
            require(std::string_view(key.name) == "NGAWISH" && key.radius_x <= 150 && key.radius_z <= 300
                && map.coast_radius(key.center.GetX() - 280, key.center.GetZ()) > 1,
                "Ngawish was not renamed or reduced to a smaller island");
            require(other_buildings == 1 && houses == 0, "Ngawish must contain only its gas station");
            require(std::none_of(map.street_loops().begin(), map.street_loops().end(), [&](const StreetLoop& loop) {
                return std::string_view(loop.name) == key.name;
            }), "Ngawish still contains town streets");
        } else {
            require(houses >= 4 && houses <= 8 && other_buildings <= (i == 10 ? 1 : 0), "Key must have a small housing neighborhood");
            const auto town = std::find_if(map.street_loops().begin(), map.street_loops().end(), [&](const StreetLoop& loop) {
                return std::string_view(loop.name) == key.name;
            });
            require(town != map.street_loops().end() && town->corners.size() == 4, "Keys residential area has no side-road loop");
            require(std::any_of(roads.begin(), roads.end(), [&](const Road& road) {
                return std::string_view(road.name) == "KEYS NEIGHBORHOOD ACCESS" && flat(road.a - key.center).Length() < .01f;
            }), "Keys neighborhood has no turn from the highway");
            int trees = 0;
            for (const auto& tree : map.trees()) if (flat(tree.base - key.center).Length() < 300) ++trees;
            std::cout << key.name << ": " << houses << " small houses, " << trees << " trees.\n";
            require(trees > houses * 8, "Keys neighborhood lacks woodland around its houses");
        }
    }
    for (int index = 4; index <= 7; ++index) {
        const auto& bridge = map.bridges()[index];
        float bend = 0;
        for (float t : {.25f, .5f, .75f}) bend = std::max(bend, segment_distance(bridge.point(t), bridge.a, bridge.b));
        require(bridge.has_curve() && bend > 5 && bridge.width == Environment::highway_width,
            "Keys bridge is straight or narrower than its highway");
    }
    int roadside_trees = 0;
    for (const auto& tree : map.trees()) {
        float gap = 10000;
        for (const auto& road : roads)
            gap = std::min(gap, segment_distance(tree.base, road.a, road.b) - road.width / 2);
        require(gap >= 1.99f, "tree trunk obstructs a road or sidewalk");
        if (gap <= 3) ++roadside_trees;
        for (const auto& airport : airports)
            require(!airport.contains(tree.base.GetX(), tree.base.GetZ()) && !airport.flight_path(tree.base.GetX(), tree.base.GetZ()),
                "tree obstructs an airfield or its departure corridor");
    }
    std::cout << roadside_trees << " trees line the streets.\n";
    require(roadside_trees >= 500, "streets have too few roadside trees");
    GroundHit hit;
    int samples = 0;
    for (const auto& road : roads) {
        const Vec3 d = road.b - road.a, side = flat(d).Normalized().Cross(Vec3::sAxisY());
        const int count = int(d.Length() / 8) + 1;
        for (int i = 0; i <= count; ++i) for (float offset : {-road.width * .35f, 0.0f, road.width * .35f}) {
            const float t = float(i) / count;
            const Vec3 p = road.bridge >= 0 && map.bridges()[road.bridge].has_curve()
                ? map.bridges()[road.bridge].point(t) + map.bridges()[road.bridge].side(t) * offset
                : road.a + d * t + side * offset;
            const float height = map.road_height(road, p.GetX(), p.GetZ());
            if (height < 2.9f) std::cout << road.name << " enters water at " << p.GetX() << ',' << p.GetZ() << ": " << height << '\n';
            require(height >= 2.9f, "road or bridge approach enters water");
            // A ray from above the whole city would silently validate the upper
            // deck for a ground street. Start just above this road's own layer.
            const bool found = world.cast_ground(Vec3(p.GetX(), height + 1, p.GetZ()), Vec3(0, -1, 0), 2, hit);
            if (!found) std::cout << road.name << " missing floor at " << p.GetX() << ',' << p.GetZ() << " height " << height << '\n';
            require(found, "road surface has a physics hole");
            // Jolt compresses regional mesh vertices; allow two centimeters on sloped decks.
            if (std::abs(hit.point.GetY() - height) >= .02f) std::cout << road.name << " at " << p.GetX() << ',' << p.GetZ()
                << ": visible " << height << ", collision " << hit.point.GetY() << '\n';
            require(std::abs(hit.point.GetY() - height) < .02f, "bridge/road physics differs from visible surface");
            if (hit.normal.GetY() <= .98f) std::cout << road.name << ": steep surface at "
                << p.GetX() << ", " << p.GetZ() << "; height " << height << "; normal " << hit.normal.GetY() << '\n';
            require(hit.normal.GetY() > .98f, "road or bridge ramp is too steep");
            ++samples;
        }
    }
    for (const auto& port : map.ports()) {
        const Vec3 dir = port.east ? Vec3::sAxisX() : Vec3::sAxisZ();
        if (std::string_view(port.name) == "NGAWISH")
            require(port.length == 60 && port.width == 5
                && map.terrain_height(port.center.GetX() + port.length - 5, port.center.GetZ()) < -1,
                "Ngawish dock is not small or does not reach the water");
        const Vec3 side = dir.Cross(Vec3::sAxisY());
        for (float d = 1; d < port.length; d += 10) for (float offset : {-port.width * .4f, 0.0f, port.width * .4f})
            require(world.cast_ground(port.center + dir * d + side * offset + Vec3(0, 20, 0), Vec3(0, -1, 0), 30, hit)
                && std::abs(hit.point.GetY() - port.center.GetY()) < .01f, "marina deck is not solid");
    }
    std::cout << roads.size() << " connected streets, " << samples << " aligned road samples, "
        << map.buildings().size() << " buildings, " << map.trees().size() << " model trees; tallest " << tallest << " m\n";
}
void drive_bridges(const Environment& map, PhysicsWorld& world, Car& car) {
    for (const auto& bridge : map.bridges()) {
        if (bridge.a.GetY() > 0 || bridge.has_curve()) continue; // Curved highways are driven continuously below.
        const Vec3 dir = (bridge.b - bridge.a).Normalized(), side = dir.Cross(Vec3::sAxisY());
        const float lane_center = bridge.width * .22f;
        Vec3 start = bridge.a - dir * 20 + side * lane_center;
        start.SetY(map.ground_height(start.GetX(), start.GetZ()) + .56f);
        car.reset(start, std::atan2(-dir.GetX(), -dir.GetZ()));
        float progress = -20;
        for (int i = 0; i < 120 * 140 && progress < (bridge.b - bridge.a).Length() + 15; ++i) {
            const float speed = car.velocity().Dot(dir);
            const Vec3 direction = flat(car.forward()).Normalized();
            const float lane = flat(car.position() - bridge.a).Dot(side);
            const Vec3 target = dir * 12 + side * (lane_center - lane);
            const float curvature = 2 * target.Dot(-direction.Cross(Vec3::sAxisY())) / target.LengthSq();
            const float steer = std::atan(car.tuning().wheelbase * curvature) / car.tuning().max_steer;
            car.step({speed < 13 ? .55f : .08f, std::clamp(steer, -1.0f, 1.0f), false, speed > 15}); world.step();
            const Vec3 p = car.position();
            progress = flat(p - bridge.a).Dot(dir);
            if (std::abs(flat(p - bridge.a).Dot(side) - lane_center) >= 2)
                std::cout << bridge.name << ": progress " << progress << ", lane " << flat(p - bridge.a).Dot(side)
                    << ", speed " << speed << ", height " << p.GetY() << '\n';
            require(std::abs(flat(p - bridge.a).Dot(side) - lane_center) < 2, "car drifted out of its bridge lane");
            require(std::abs(p.GetY() - map.ground_height(p.GetX(), p.GetZ()) - .56f) < 1,
                "car fell through a bridge or lost contact at an approach");
            require(car.rotate(Vec3::sAxisY()).GetY() > .95f, "car tipped on a bridge ramp");
        }
        require(progress > (bridge.b - bridge.a).Length() + 10, "car could not cross a connecting bridge");
    }
    const auto& rail = map.barriers()[map.barriers().size() / 2];
    const Vec3 across = Quat::sRotation(Vec3::sAxisY(), rail.yaw) * Vec3::sAxisX();
    require(world.camera_fraction(rail.center + across * 3, across * -6) < .5f, "bridge rail has no collision");
    std::cout << "Drove the regional ground bridges and both approaches.\n";
}
void highway_layers(const Environment& map, PhysicsWorld& world) {
    bool ring = false, arterial = false, flyover = false, underpass = false;
    for (const auto& highway : map.highways()) {
        require(highway.corners.size() >= 2, "highway has no usable path");
        require(highway.lane_offset > 0 && highway.lane_offset < highway.width / 2 - 1,
            "highway lane is outside its pavement");
        float lowest = 10000, highest = -10000;
        for (Vec3 p : highway.corners) {
            if (p.GetY() <= 0) p.SetY(map.ground_height(p.GetX(), p.GetZ()));
            lowest = std::min(lowest, p.GetY()); highest = std::max(highest, p.GetY());
            if (std::abs(map.surface_height(p) - p.GetY()) >= .02f)
                std::cout << highway.name << " path height mismatch " << p.GetX() << ',' << p.GetZ()
                    << " expected=" << p.GetY() << " actual=" << map.surface_height(p) << '\n';
            require(std::abs(map.surface_height(p) - p.GetY()) < .02f,
                "highway path endpoint and physical ramp joint disagree");
        }
        ring = ring || (highway.closed && highway.width >= 18 && lowest >= 2.9f && highest <= Environment::road_level + .01f
            && std::count_if(highway.corners.begin(), highway.corners.end(), [](Vec3 p) { return p.GetY() == 0; })
                > int(highway.corners.size() / 2));
        arterial = arterial || (highway.width == 24 && lowest <= 3.3f && highest >= Environment::highway_level);
        flyover = flyover || (!highway.closed && highway.width == 24 && highest >= 31.9f
            && std::max(highway.corners.front().GetX(), highway.corners.back().GetX()) > 1300
            && std::min(highway.corners.front().GetX(), highway.corners.back().GetX()) < 0);
        if (std::string_view(highway.name) == "BEACH TO KEYS SKYWAY") {
            float length = 0;
            for (std::size_t i = 1; i < highway.corners.size(); ++i) length += (highway.corners[i] - highway.corners[i - 1]).Length();
            require(length < 2500, "beach skyway still takes a long detour");
        }
    }
    require(ring && arterial && flyover, "level city ring with river bridge, six-lane arterial or beach flyover missing");
    for (const auto& before : map.bridges()) if (before.a.GetY() > 0)
        for (const auto& after : map.bridges()) if (&before != &after && std::string_view(before.name) == after.name
            && (before.b - after.a).Length() < .01f)
            require((before.side(1) - after.side(0)).Length() < .001f,
                "adjoining highway sections have an open pavement edge joint");
    const auto& roads = map.roads();
    for (const auto& upper : roads) if (upper.bridge >= 0 && map.bridges()[upper.bridge].a.GetY() > 0) {
        const Vec3 d = upper.b - upper.a;
        for (const auto& lower : roads) if (lower.bridge < 0 || map.bridges()[lower.bridge].a.GetY() == 0) {
            const Vec3 e = lower.b - lower.a, delta = lower.a - upper.a;
            const float divisor = cross(d, e);
            if (std::abs(divisor) < .001f) continue;
            const float t = cross(delta, e) / divisor, u = cross(delta, d) / divisor;
            if (t < .05f || t > .95f || u < .05f || u > .95f) continue;
            const Vec3 p = upper.a + d * t;
            const float upper_y = map.road_height(upper, p.GetX(), p.GetZ());
            const float lower_y = map.road_height(lower, p.GetX(), p.GetZ());
            if (upper_y - lower_y < 5) continue;
            Road upper_crossing = upper, lower_crossing = lower;
            upper_crossing.a = p - flat(d).Normalized(); upper_crossing.b = p + flat(d).Normalized();
            lower_crossing.a = p - flat(e).Normalized(); lower_crossing.b = p + flat(e).Normalized();
            require(!connected(map, upper_crossing, lower_crossing), "grade-separated crossing incorrectly joins the road graph");
            GroundHit ground, deck;
            require(world.cast_ground(Vec3(p.GetX(), lower_y + 1, p.GetZ()), Vec3(0, -1, 0), 2, ground)
                && world.cast_ground(Vec3(p.GetX(), upper_y + 1, p.GetZ()), Vec3(0, -1, 0), 2, deck),
                "overpass has no independently drivable ground street or upper deck");
            require(std::abs(ground.point.GetY() - lower_y) < .01f && std::abs(deck.point.GetY() - upper_y) < .01f,
                "stacked road layers do not match collision");
            require(std::abs(map.surface_height(Vec3(p.GetX(), lower_y, p.GetZ())) - lower_y) < .01f
                && std::abs(map.surface_height(Vec3(p.GetX(), upper_y, p.GetZ())) - upper_y) < .01f,
                "actor surface lookup jumps to the wrong overpass layer");
            underpass = true;
        }
    }
    require(underpass, "city has no actual road beneath an elevated highway");
    std::cout << "Highway ramp joints and independent ground/deck crossings align.\n";
}
void bridge_walls(const Environment& map, PhysicsWorld& world) {
    int checked = 0;
    bool sloped = false, curved = false;
    for (const auto& bridge : map.bridges()) {
        const bool skyway = std::string_view(bridge.name) == "BEACH TO KEYS SKYWAY";
        if (!skyway && std::string_view(bridge.name) != "US 1 GRAND BOULEVARD"
            && std::string_view(bridge.name) != "KEYS EXIT RAMP") continue;
        const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
        for (int row = 0; row < rows; ++row) for (float sign : {-1.0f, 1.0f}) {
            const float from = float(row) / rows, to = float(row + 1) / rows;
            const Vec3 a = bridge.point(from) + bridge.side(from) * (sign * (bridge.width / 2 - .3f));
            const Vec3 b = bridge.point(to) + bridge.side(to) * (sign * (bridge.width / 2 - .3f));
            const float start_z = std::string_view(bridge.name) == "US 1 GRAND BOULEVARD" && sign > 0 ? 650 : 850;
            if (skyway ? std::min(a.GetY(), b.GetY()) <= 12
                : bridge.a.GetY() <= 0 || a.GetZ() < start_z || b.GetZ() > 1100) continue;
            const Vec3 center = (a + b) / 2 + Vec3(0, .65f, 0);
            const auto wall = std::find_if(map.barriers().begin(), map.barriers().end(), [&](const Barrier& rail) {
                return (rail.center - center).Length() < .03f;
            });
            require(wall != map.barriers().end(), "elevated bridge wall has a missing segment");
            for (Vec3 end : {a, b}) {
                const Vec3 local = wall->rotation().Conjugated() * (end + Vec3(0, .65f, 0) - wall->center);
                require(std::abs(local.GetX()) < .01f && std::abs(local.GetY()) < .01f
                    && std::abs(local.GetZ()) <= wall->size.GetZ() / 2, "wall does not reach its sloped or curved deck joint");
            }
            sloped |= std::abs(wall->pitch) > .01f;
            curved |= (bridge.side(from) - bridge.side(to)).Length() > .001f;
            ++checked;
        }
    }
    std::cout << checked << " bridge wall segments follow their deck joints.\n";
    require(checked > 100, "bridge wall check missed the elevated spans");
    require(sloped, "bridge wall check missed slopes");
    require(curved, "bridge wall check missed curves");
    for (const auto& before : map.bridges()) for (const auto& after : map.bridges())
        if (before.a.GetY() > 0 && after.a.GetY() > 0 && before.width != after.width
            && (before.b - after.a).LengthSq() < .0001f)
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 a = before.b + before.side(1) * (sign * (before.width / 2 - .3f));
                const Vec3 b = after.a + after.side(0) * (sign * (after.width / 2 - .3f));
                const Vec3 center = (a + b) / 2 + Vec3(0, .65f, 0);
                const Vec3 across = flat(b - a).Normalized().Cross(Vec3::sAxisY());
                require(world.camera_fraction(center + across * 2, across * -4) < .5f,
                    "bridge width transition has an open wall shoulder");
            }
}
void highway_dividers(const Environment& map) {
    bool divided = false;
    for (const auto& main : map.roads()) if (main.bridge < 0 && main.width == 24 && (main.b - main.a).Length() > 600) {
        const Vec3 direction = flat(main.b - main.a).Normalized(), side = direction.Cross(Vec3::sAxisY());
        int left = 0, right = 0;
        for (const auto& local : map.roads()) if (local.bridge < 0 && local.width >= 6 && local.width <= 9) {
            const Vec3 local_direction = flat(local.b - local.a).Normalized();
            if (std::abs(direction.Dot(local_direction)) < .99f || (local.b - local.a).Length() < 600) continue;
            const float lateral = flat(local.a - main.a).Dot(side);
            const float median_width = std::abs(lateral) - (main.width + local.width) / 2;
            if (median_width < 4 || median_width > 12) continue;
            int trees = 0;
            for (const auto& tree : map.trees()) {
                const float along = flat(tree.base - main.a).Dot(direction);
                const float across = flat(tree.base - main.a).Dot(side) * (lateral > 0 ? 1 : -1);
                if (along > 20 && along < (main.b - main.a).Length() - 20
                    && across > main.width / 2 + 1.9f && across < std::abs(lateral) - local.width / 2 - 1.9f) ++trees;
            }
            if (trees >= 20) { if (lateral > 0) ++left; else ++right; }
        }
        if (left && right) divided = true;
    }
    require(divided, "six-lane ground arterial lacks separate slow lanes and tree dividers on both sides");
    std::cout << "Wide ground arterial has two slow lanes separated by planted medians.\n";
}
void drive_highways(const Environment& map, PhysicsWorld& world, Car& car) {
    for (const auto& highway : map.highways()) for (bool reverse : {false, true}) {
        std::vector<Vec3> path = highway.corners;
        for (auto& p : path) if (p.GetY() <= 0) p.SetY(map.ground_height(p.GetX(), p.GetZ()));
        if (highway.closed && (path.back() - path.front()).Length() > .001f) path.push_back(path.front());
        if (reverse) std::reverse(path.begin(), path.end());
        std::vector<float> covered(path.size(), 0);
        for (std::size_t i = 1; i < path.size(); ++i) covered[i] = covered[i - 1] + (path[i] - path[i - 1]).Length();
        const float length = covered.back(), lane_center = highway.lane_offset;
        const Vec3 start_direction = flat(path[1] - path[0]).Normalized();
        Vec3 start = path.front() + start_direction.Cross(Vec3::sAxisY()) * lane_center;
        start.SetY(map.surface_height(start) + .56f);
        car.reset(start, std::atan2(-start_direction.GetX(), -start_direction.GetZ()));
        float progress = 0; std::size_t current_segment = 0;
        const float cruise = highway.cruise_speed;
        const int budget = int(120 * (length / cruise * 2 + 45));
        for (int step = 0; step < budget && progress < length - 1; ++step) {
            float closest = 10000, fraction = 0; std::size_t segment = current_segment;
            // Search locally so a closed ring cannot count its coincident final
            // point as a complete lap immediately after spawning.
            const std::size_t first = current_segment > 0 ? current_segment - 1 : 0;
            for (std::size_t i = first; i + 1 < path.size() && i <= current_segment + 5; ++i) {
                const Vec3 d = flat(path[i + 1] - path[i]);
                const float t = std::clamp(flat(car.position() - path[i]).Dot(d) / d.LengthSq(), 0.0f, 1.0f);
                const float distance = flat(car.position() - path[i] - d * t).LengthSq();
                if (distance < closest) { closest = distance; segment = i; fraction = t; }
            }
            current_segment = segment;
            progress = covered[segment] + (path[segment + 1] - path[segment]).Length() * fraction;
            const Vec3 projection = path[segment] + (path[segment + 1] - path[segment]) * fraction;
            const Vec3 direction = flat(path[segment + 1] - path[segment]).Normalized();
            const float lane = flat(car.position() - path[segment]).Dot(direction.Cross(Vec3::sAxisY()));
            if (std::abs(lane - lane_center) >= 2.5f || std::abs(car.position().GetY() - projection.GetY() - .56f) >= 1.2f)
                std::cout << highway.name << (reverse ? " reverse" : " forward") << ": progress " << progress
                    << '/' << length << ", lane " << lane << ", height " << car.position().GetY()
                    << ", path " << projection.GetY() << ", speed " << car.velocity().Length() << '\n';
            require(std::abs(lane - lane_center) < 2.5f, "car drifted out of its highway lane");
            require(std::abs(car.position().GetY() - projection.GetY() - .56f) < 1.2f,
                "car fell from its selected highway layer or lost contact at a ramp joint");
            const Vec3 reference(car.position().GetX(), projection.GetY(), car.position().GetZ());
            require(std::abs(car.position().GetY() - map.surface_height(reference) - .56f) < .8f,
                "car lost contact with highway pavement");
            Vec3 aim = projection;
            const float speed = car.velocity().Length();
            float lookahead = 3.5f + speed * .55f;
            while (segment + 1 < path.size()) {
                const float remaining = (path[segment + 1] - aim).Length();
                if (remaining >= lookahead) { aim += (path[segment + 1] - aim).Normalized() * lookahead; break; }
                lookahead -= remaining; aim = path[++segment];
            }
            const std::size_t aim_segment = std::min(segment, path.size() - 2);
            const Vec3 aim_direction = flat(path[aim_segment + 1] - path[aim_segment]).Normalized();
            const Vec3 target = flat(aim - car.position()) + aim_direction.Cross(Vec3::sAxisY()) * lane_center;
            const float curvature = 2 * target.Dot(-flat(car.forward()).Normalized().Cross(Vec3::sAxisY())) / std::max(target.LengthSq(), 1.0f);
            const float desired = std::min(cruise, std::sqrt(2.8f / std::max(std::abs(curvature), .001f)));
            const float steer_limit = std::min(car.tuning().max_steer, std::atan(car.tuning().wheelbase * 16 / std::max(speed * speed, 1.0f)));
            car.step({std::clamp((desired + (desired - speed) * .8f) / car.tuning().top_speed, 0.0f, 1.0f),
                std::clamp(std::atan(car.tuning().wheelbase * curvature) / steer_limit, -1.0f, 1.0f), false, speed > desired + .5f});
            world.step();
        }
        require(progress >= length - 1, "car could not drive a complete highway path");
        std::cout << "Drove " << highway.name << (reverse ? " reverse: " : " forward: ") << length << " m.\n";
    }
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
    std::cout << std::unitbuf;
    try {
        Environment map; PhysicsWorld world(map); Car car(world);
        coastlines(map); map_layout(map, world); highway_layers(map, world); bridge_walls(map, world); highway_dividers(map); drive_bridges(map, world, car); drive_highways(map, world, car); traffic_streaming(map, world, car);
        std::cout << "All Miami map checks passed.\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Map check failed: " << error.what() << '\n'; return 1;
    }
}
