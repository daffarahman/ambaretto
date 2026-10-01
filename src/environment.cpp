#include "environment.hpp"
#include "airport.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace forza {
namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float segment_fraction(Vec3 a, Vec3 b, float x, float z) {
    const float dx = b.GetX() - a.GetX(), dz = b.GetZ() - a.GetZ();
    return ((x - a.GetX()) * dx + (z - a.GetZ()) * dz) / (dx * dx + dz * dz);
}
float distance_to(Vec3 a, Vec3 b, float x, float z) {
    const float t = std::clamp(segment_fraction(a, b, x, z), 0.0f, 1.0f);
    return std::hypot(x - (a.GetX() + (b.GetX() - a.GetX()) * t), z - (a.GetZ() + (b.GetZ() - a.GetZ()) * t));
}
struct KeyLayout { Vec3 entry, center, exit; float rx, rz; const char* name; };
const std::vector<KeyLayout>& keys() {
    static const std::vector<KeyLayout> layout{
        {{-600, 0, 2100}, {-600, 0, 2500}, {-600, 0, 2900}, 650, 600, "KEY LARGO"},
        {{-900, 0, 3400}, {-1300, 0, 3700}, {-1400, 0, 3900}, 650, 550, "ISLAMORADA"},
        {{-1800, 0, 4650}, {-2200, 0, 4900}, {-2400, 0, 5150}, 700, 500, "MARATHON"},
        {{-2650, 0, 5900}, {-2900, 0, 6200}, {-3150, 0, 6450}, 620, 500, "LOWER KEYS"},
        {{-3300, 0, 6950}, {-3500, 0, 7300}, {-3650, 0, 7550}, 480, 550, "KEY WEST"}
    };
    return layout;
}
}

Vec3 Bridge::point(float t) const {
    const float ramp = std::min(.34f, 160.0f / (b - a).Length());
    const float blend = smooth(0, ramp, std::min(t, 1 - t));
    const Vec3 p = a + (b - a) * t;
    return Vec3(p.GetX(), Environment::road_level + (clearance - Environment::road_level) * blend, p.GetZ());
}
const std::vector<Island>& Environment::islands() {
    static const std::vector<Island> data = [] {
        std::vector<Island> result{
            {{-900, 0, 100}, 1600, 1800, "MIAMI"},
            {{2200, 0, -100}, 500, 1650, "MIAMI BEACH"},
            {{1100, 0, 400}, 330, 310, "PORTMIAMI"},
            {{900, 0, -400}, 140, 110, "VENETIAN ISLANDS"},
            {{1200, 0, -400}, 140, 110, "VENETIAN ISLANDS"},
            {{1500, 0, -400}, 140, 110, "VENETIAN ISLANDS"}
        };
        for (const auto& key : keys()) result.push_back({key.center, key.rx, key.rz, key.name});
        return result;
    }();
    return data;
}
const std::vector<Bridge>& Environment::bridges() {
    static const std::vector<Bridge> data{
        {{400, 0, 400}, {950, 0, 400}, 26, 10, "MACARTHUR CAUSEWAY"},
        {{1250, 0, 400}, {1900, 0, 400}, 26, 10, "MACARTHUR CAUSEWAY"},
        {{400, 0, -400}, {1900, 0, -400}, 24, 7, "VENETIAN CAUSEWAY"},
        {{-600, 0, 1500}, {-600, 0, 2100}, 26, 9, "JEWFISH CREEK BRIDGE"},
        {{-600, 0, 2900}, {-900, 0, 3400}, 26, 10, "OVERSEAS HIGHWAY"},
        {{-1400, 0, 3900}, {-1800, 0, 4650}, 26, 11, "LONG KEY BRIDGE"},
        {{-2400, 0, 5150}, {-2650, 0, 5900}, 26, 16, "SEVEN MILE BRIDGE"},
        {{-3150, 0, 6450}, {-3300, 0, 6950}, 26, 10, "KEY WEST CAUSEWAY"}
    };
    return data;
}
const std::vector<Road>& Environment::roads() {
    static const std::vector<Road> data = [] {
        std::vector<Road> result;
        const auto add = [&](float ax, float az, float bx, float bz, float width, const char* name) {
            result.push_back({Vec3(ax, 0, az), Vec3(bx, 0, bz), width, name});
        };
        for (float x = -1000; x <= 400; x += block_size)
            add(x, -1000, x, 1000, 24, x == 0 ? "BISCAYNE BOULEVARD" : "DOWNTOWN AVENUE");
        for (float z = -1000; z <= 1000; z += block_size)
            add(-1000, z, 400, z, 24, z == 400 ? "MACARTHUR APPROACH" : "MIAMI STREET");
        add(-600, 1000, -600, 1500, 26, "US 1 SOUTH");
        add(Airport::center_x, Airport::apron_z, Airport::center_x, 400, 24, "AIRPORT ROAD");
        add(Airport::center_x, 400, -1000, 400, 24, "AIRPORT CONNECTOR");
        for (const auto& lane : {std::pair<float, const char*>{1900, "ALTON ROAD"}, {2100, "WASHINGTON AVENUE"},
                {2300, "COLLINS AVENUE"}, {2450, "OCEAN DRIVE"}})
            add(lane.first, -1000, lane.first, 1000, 22, lane.second);
        for (float z = -1000; z <= 1000; z += 200) add(1900, z, 2450, z, 22, "SOUTH BEACH STREET");
        add(950, 400, 1250, 400, 26, "PORTMIAMI ROAD");
        add(1100, 400, 1100, 560, 20, "CRUISE PORT ACCESS");
        for (const auto& key : keys()) {
            result.push_back({key.entry, key.center, 26, "OVERSEAS HIGHWAY"});
            result.push_back({key.center, key.exit, 26, "OVERSEAS HIGHWAY"});
            const float x = key.center.GetX(), z = key.center.GetZ();
            for (float offset : {-200.0f, 0.0f, 200.0f}) {
                add(x + offset, z - 200, x + offset, z + 200, 20, "KEYS RESIDENTIAL STREET");
                add(x - 200, z + offset, x + 200, z + offset, 20, "KEYS MARINA ROAD");
            }
            add(x + 200, z, x + key.rx * .84f, z, 20, "PORT ACCESS");
        }
        for (std::size_t i = 0; i < bridges().size(); ++i) {
            const auto& bridge = bridges()[i];
            result.push_back({bridge.a, bridge.b, bridge.width, bridge.name, int(i)});
        }
        return result;
    }();
    return data;
}
float Environment::coast_radius(float x, float z) {
    float radius = 100;
    for (const auto& island : islands()) {
        const float dx = std::abs((x - island.center.GetX()) / island.radius_x);
        const float dz = std::abs((z - island.center.GetZ()) / island.radius_z);
        const float shape = &island == &islands().front() ? std::sqrt(std::sqrt(dx * dx * dx * dx + dz * dz * dz * dz)) : std::hypot(dx, dz);
        radius = std::min(radius, shape);
    }
    return radius;
}
bool Environment::road(float x, float z) {
    if (Airport::pavement(x, z)) return true;
    for (const auto& street : roads())
        if (distance_to(street.a, street.b, x, z) <= street.width / 2) return true;
    return false;
}
const char* Environment::district(float x, float z) {
    if (Airport::contains(x, z)) return "MIAMI AIRFIELD";
    if (x > 1600 && z < 1700) return "MIAMI BEACH / SOUTH BEACH";
    if (x > 700 && x < 1550 && z > 50 && z < 750) return "PORTMIAMI";
    for (const auto& key : keys())
        if (std::hypot((x - key.center.GetX()) / key.rx, (z - key.center.GetZ()) / key.rz) < 1.2f) return key.name;
    if (z > 1800) return "OVERSEAS HIGHWAY / FLORIDA KEYS";
    if (x > 500) return "BISCAYNE BAY";
    return z > 350 ? "BRICKELL / DOWNTOWN MIAMI" : "DOWNTOWN MIAMI";
}
float Environment::elevation(float x, float z) {
    const float radius = coast_radius(x, z);
    float ground = road_level - (road_level + 9) * smooth(.90f, 1.12f, radius);
    const float airport_blend = (1 - smooth(220, 280, std::abs(x - Airport::center_x)))
        * (1 - smooth(120, 180, std::abs(z - Airport::runway_z)));
    ground += (Airport::elevation - ground) * airport_blend;
    return ground;
}
Surface Environment::surface(float x, float z, float y) {
    if (y < -.1f) return Surface::Seabed;
    if (road(x, z)) return Surface::Road;
    return coast_radius(x, z) > .80f ? Surface::Sand : Surface::Grass;
}

Environment::Environment() {
    vertices_.reserve(samples * samples + 10000);
    for (int z = 0; z < samples; ++z) for (int x = 0; x < samples; ++x) {
        const float px = -extent + x * spacing, pz = -extent + z * spacing;
        vertices_.emplace_back(px, elevation(px, pz), pz);
    }
    triangles_.reserve((samples - 1) * (samples - 1) * 2 + 10000);
    const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, bool deck = false) {
        const Vec3 center = (vertices_[a] + vertices_[b] + vertices_[c]) / 3;
        triangles_.push_back({a, b, c, deck ? Surface::Road : surface(center.GetX(), center.GetZ(), center.GetY()), deck});
    };
    for (int z = 0; z < samples - 1; ++z) for (int x = 0; x < samples - 1; ++x) {
        const std::uint32_t a = z * samples + x, b = a + 1, c = a + samples, d = c + 1;
        triangle(a, c, b); triangle(b, c, d);
    }
    for (const auto& bridge : bridges()) {
        const Vec3 direction = (bridge.b - bridge.a).Normalized(), side = direction.Cross(Vec3::sAxisY());
        const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
        const std::uint32_t first = std::uint32_t(vertices_.size());
        for (int row = 0; row <= rows; ++row) {
            const Vec3 center = bridge.point(float(row) / rows);
            vertices_.push_back(center - side * (bridge.width / 2));
            vertices_.push_back(center + side * (bridge.width / 2));
        }
        for (int row = 0; row < rows; ++row) {
            const auto a = first + row * 2;
            triangle(a, a + 1, a + 2, true); triangle(a + 1, a + 3, a + 2, true);
            const Vec3 center = bridge.point((row + .5f) / rows);
            // Leave adjoining intersections open for cars turning onto the deck.
            const float along = (row + .5f) / rows * (bridge.b - bridge.a).Length();
            if (along < 20 || along > (bridge.b - bridge.a).Length() - 20) continue;
            for (float sign : {-1.0f, 1.0f}) barriers_.push_back({center + side * (sign * (bridge.width / 2 - .3f))
                + Vec3(0, .65f, 0), Vec3(.45f, 1.3f, (bridge.b - bridge.a).Length() / rows + .1f),
                std::atan2(-direction.GetX(), -direction.GetZ())});
        }
    }
    std::mt19937 random(4317);
    const auto add_building = [&](float x, float z, Vec3 size, BuildingKind kind, bool east = false) {
        // Clip each street segment against the footprint expanded by its width.
        // This catches diagonal roads between the footprint sample points.
        for (const auto& street : roads()) {
            float start = 0, end = 1;
            bool overlaps = true;
            const auto clip = [&](float a, float delta, float center, float half) {
                if (std::abs(delta) < .001f) return std::abs(a - center) <= half;
                float lo = (center - half - a) / delta, hi = (center + half - a) / delta;
                if (lo > hi) std::swap(lo, hi);
                start = std::max(start, lo); end = std::min(end, hi);
                return start <= end;
            };
            overlaps &= clip(street.a.GetX(), street.b.GetX() - street.a.GetX(), x, size.GetX() / 2 + street.width / 2 + 2);
            overlaps &= clip(street.a.GetZ(), street.b.GetZ() - street.a.GetZ(), z, size.GetZ() / 2 + street.width / 2 + 2);
            if (overlaps) return false;
        }
        for (const auto& b : buildings_)
            if (std::abs(x - b.center.GetX()) < (size.GetX() + b.size.GetX()) / 2 + 2
                && std::abs(z - b.center.GetZ()) < (size.GetZ() + b.size.GetZ()) / 2 + 2) return false;
        // Keep entrances and the road footprint clear, including diagonal US 1.
        for (float sx = -size.GetX() / 2; sx <= size.GetX() / 2; sx += size.GetX() / 4)
            for (float sz = -size.GetZ() / 2; sz <= size.GetZ() / 2; sz += size.GetZ() / 4)
                if (road(x + sx, z + sz)) return false;
        buildings_.push_back({Vec3(x, height(x, z) + size.GetY() / 2, z), size, int(random() % 5), kind, east});
        return true;
    };
    for (int z = -5; z < 5; ++z) for (int x = -5; x < 2; ++x) {
        const float cx = x * block_size + 100, cz = z * block_size + 100;
        if ((x + z + 14) % 9 == 0) continue;
        if ((x == -1 && z == 2) || (x == -3 && z == -1)) {
            add_building(cx, cz, Vec3(138, 24, 134), BuildingKind::Mall);
            continue;
        }
        for (float ox : {-45.0f, 45.0f}) for (float oz : {-45.0f, 45.0f}) {
            const float skyline = std::max(0.0f, 1 - std::hypot(cx / 1000, cz / 1100));
            const float h = 24 + random() % 45 + skyline * (50 + random() % 115);
            add_building(cx + ox, cz + oz, Vec3(44, h, 46), BuildingKind::Tower);
        }
    }
    for (float z = -900; z <= 900; z += 200) {
        add_building(2375, z, Vec3(104, 15 + random() % 19, 115), BuildingKind::Hotel, true);
        add_building(2190, z - 38, Vec3(65, 7, 52), BuildingKind::Cafe, true);
        add_building(2180, z + 39, Vec3(75, 10, 58), BuildingKind::Club, true);
        add_building(1980, z, Vec3(85, 38 + random() % 44, 110), BuildingKind::Hotel);
    }
    ports_.push_back({Vec3(1100, road_level, 570), false, "PORTMIAMI"});
    for (float x : {1010.0f, 1190.0f}) add_building(x, 500, Vec3(90, 16, 65), BuildingKind::Warehouse);
    for (const auto& key : keys()) {
        const float x = key.center.GetX(), z = key.center.GetZ();
        for (const auto& offset : {Vec3(100, 0, -100), Vec3(-100, 0, -100), Vec3(100, 0, 100), Vec3(-100, 0, 100)})
            if (add_building(x + offset.GetX(), z + offset.GetZ(), Vec3(45, 7, 42), BuildingKind::GasStation, true)) break;
        add_building(x - 82, z + 60, Vec3(52, 7, 38), BuildingKind::Cafe);
        for (float ox : {-135.0f, -65.0f, 65.0f, 135.0f}) for (float oz : {-130.0f, 130.0f})
            add_building(x + ox, z + oz, Vec3(36, 6 + random() % 5, 32), BuildingKind::House);
        add_building(x + 100, z + 295, Vec3(65, 12, 45), BuildingKind::Warehouse);
        ports_.push_back({Vec3(x + key.rx * .84f, height(x + key.rx * .84f, z), z), true, key.name});
    }
    add_building(Airport::center_x + 36, Airport::runway_z - 84, Vec3(42, 8, 18), BuildingKind::Terminal);
    add_building(Airport::center_x - 94, Airport::runway_z - 84, Vec3(36, 10, 24), BuildingKind::Hangar);
    add_building(Airport::center_x + 91, Airport::runway_z - 80, Vec3(9, 22, 9), BuildingKind::ControlTower);
    for (int i = 0; i < 3000; ++i) {
        const auto& island = islands()[random() % islands().size()];
        const float x = island.center.GetX() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_x;
        const float z = island.center.GetZ() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_z;
        if (coast_radius(x, z) > .88f || Airport::contains(x, z) || road(x, z)) continue;
        bool blocked = false;
        for (const auto& b : buildings_) if (std::abs(x - b.center.GetX()) < b.size.GetX() / 2 + 8
            && std::abs(z - b.center.GetZ()) < b.size.GetZ() / 2 + 8) { blocked = true; break; }
        for (const auto& tree : trees_) if (std::hypot(x - tree.base.GetX(), z - tree.base.GetZ()) < 9) { blocked = true; break; }
        if (blocked) continue;
        trees_.push_back({Vec3(x, height(x, z), z), 5.5f + random() % 40 * .1f,
            std::remainder(x * 13 + z * 17, 360.0f) * .017453293f});
    }
}
float Environment::terrain_height(float x, float z) const {
    const float gx = std::clamp((x + extent) / spacing, 0.0f, float(samples - 1));
    const float gz = std::clamp((z + extent) / spacing, 0.0f, float(samples - 1));
    const int ix = std::min(int(gx), samples - 2), iz = std::min(int(gz), samples - 2);
    const float u = gx - ix, v = gz - iz;
    const int a = iz * samples + ix;
    const float ha = vertices_[a].GetY(), hb = vertices_[a + 1].GetY();
    const float hc = vertices_[a + samples].GetY(), hd = vertices_[a + samples + 1].GetY();
    return u + v <= 1 ? ha + u * (hb - ha) + v * (hc - ha)
        : hd + (1 - u) * (hc - hd) + (1 - v) * (hb - hd);
}
float Environment::height(float x, float z) const {
    float ground = terrain_height(x, z);
    for (const auto& bridge : bridges()) {
        const float t = segment_fraction(bridge.a, bridge.b, x, z);
        if (t < 0 || t > 1 || distance_to(bridge.a, bridge.b, x, z) > bridge.width / 2 + .05f) continue;
        const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
        const float row = t * rows;
        const int i = std::min(int(row), rows - 1);
        const float y = bridge.point(float(i) / rows).GetY()
            + (bridge.point(float(i + 1) / rows).GetY() - bridge.point(float(i) / rows).GetY()) * (row - i);
        ground = std::max(ground, y);
    }
    return ground;
}
bool Environment::submerged(const Vec3& point) const {
    return point.GetY() < water_level - .6f || std::abs(point.GetX()) > extent - 40 || std::abs(point.GetZ()) > extent - 40;
}
} // namespace forza
