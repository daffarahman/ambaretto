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
        {{-360, 0, 1260}, {-360, 0, 1500}, {-360, 0, 1740}, 390, 360, "KEY LARGO"},
        {{-540, 0, 2040}, {-780, 0, 2220}, {-840, 0, 2340}, 390, 330, "ISLAMORADA"},
        {{-1080, 0, 2790}, {-1320, 0, 2940}, {-1440, 0, 3090}, 420, 300, "MARATHON"},
        {{-1590, 0, 3540}, {-1740, 0, 3720}, {-1890, 0, 3870}, 372, 300, "LOWER KEYS"},
        {{-1980, 0, 4170}, {-2100, 0, 4380}, {-2190, 0, 4530}, 288, 330, "KEY WEST"}
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
            {{-540, 0, 60}, 960, 1080, "MIAMI"},
            {{1320, 0, -60}, 300, 990, "MIAMI BEACH"},
            {{660, 0, 240}, 198, 186, "PORTMIAMI"},
            {{540, 0, -240}, 84, 66, "VENETIAN ISLANDS"},
            {{720, 0, -240}, 84, 66, "VENETIAN ISLANDS"},
            {{900, 0, -240}, 84, 66, "VENETIAN ISLANDS"}
        };
        for (const auto& key : keys()) result.push_back({key.center, key.rx, key.rz, key.name});
        return result;
    }();
    return data;
}
const std::vector<Bridge>& Environment::bridges() {
    static const std::vector<Bridge> data{
        {{240, 0, 240}, {570, 0, 240}, 12, 8, "MACARTHUR CAUSEWAY"},
        {{750, 0, 240}, {1140, 0, 240}, 12, 8, "MACARTHUR CAUSEWAY"},
        {{240, 0, -240}, {1140, 0, -240}, 12, 7, "VENETIAN CAUSEWAY"},
        {{-360, 0, 900}, {-360, 0, 1260}, 10, 8, "JEWFISH CREEK BRIDGE"},
        {{-360, 0, 1740}, {-540, 0, 2040}, 10, 9, "OVERSEAS HIGHWAY"},
        {{-840, 0, 2340}, {-1080, 0, 2790}, 10, 10, "LONG KEY BRIDGE"},
        {{-1440, 0, 3090}, {-1590, 0, 3540}, 10, 12, "SEVEN MILE BRIDGE"},
        {{-1890, 0, 3870}, {-1980, 0, 4170}, 10, 9, "KEY WEST CAUSEWAY"}
    };
    return data;
}
const std::vector<StreetLoop>& Environment::street_loops() {
    static const std::vector<StreetLoop> data = [] {
        std::vector<StreetLoop> result{
            {{{0, 0, -180}, {120, 0, -180}, {120, 0, 180}, {0, 0, 180}}, 9, "BISCAYNE BLOCK"},
            {{{-120, 0, -240}, {0, 0, -240}, {0, 0, -60}, {-100, 0, -60}}, 8, "DOWNTOWN MARKET"},
            {{{-100, 0, -60}, {0, 0, -60}, {0, 0, 120}, {-140, 0, 100}}, 8, "CIVIC QUARTER"},
            {{{-140, 0, 100}, {0, 0, 120}, {0, 0, 240}, {-120, 0, 240}}, 8, "BRICKELL SHOPS"},
            {{{120, 0, -240}, {240, 0, -240}, {240, 0, 0}, {120, 0, 0}}, 9, "DOWNTOWN EAST"},
            {{{120, 0, 0}, {240, 0, 0}, {240, 0, 240}, {120, 0, 240}}, 9, "BAYFRONT"},
            {{{-360, 0, -480}, {-600, 0, -480}, {-600, 0, -240}, {-360, 0, -240}}, 10, "WAREHOUSE DISTRICT"},
            {{{-360, 0, -240}, {-240, 0, -240}, {-210, 0, -60}, {-360, 0, -80}}, 8, "DESIGN DISTRICT"},
            {{{-600, 0, -240}, {-360, 0, -240}, {-360, 0, -80}, {-580, 0, -40}}, 8, "LITTLE HAVANA"},
            {{{-580, 0, -40}, {-360, 0, -80}, {-360, 0, 160}, {-600, 0, 200}}, 8, "RESIDENTIAL TERRACE"},
            {{{-600, 0, 200}, {-360, 0, 160}, {-360, 0, 360}, {-580, 0, 340}}, 8, "GARDEN NEIGHBORHOOD"},
            {{{-580, 0, 340}, {-360, 0, 360}, {-360, 0, 600}, {-600, 0, 600}}, 8, "SOUTH MIAMI"},
            {{{-360, 0, 240}, {-120, 0, 240}, {-160, 0, 420}, {-360, 0, 400}}, 8, "BRICKELL RESIDENTIAL"},
            {{{-160, 0, 420}, {0, 0, 400}, {0, 0, 600}, {-360, 0, 600}, {-360, 0, 400}}, 8, "SOUTHERN NEIGHBORHOOD"},
            {{{-780, 0, -240}, {-600, 0, -240}, {-580, 0, -40}, {-760, 0, -80}}, 8, "AIRPORT NEIGHBORHOOD"},
            {{{-1040, 0, -520}, {-1240, 0, -480}, {-1220, 0, -160}, {-1040, 0, -160}}, 8, "AIRPORT WORKSHOPS"},
            {{{-1220, 0, -160}, {-1040, 0, -160}, {-1040, 0, 100}, {-1200, 0, 140}}, 7.5f, "AIRPORT RESIDENTIAL"},
            {{{-1040, 0, -160}, {-780, 0, -160}, {-760, 0, -80}, {-1040, 0, 100}}, 7.5f, "AIRPORT SOUTH"}
        };
        for (float x : {1140.0f, 1260.0f, 1380.0f}) for (float z : {-480.0f, -240.0f, 0.0f, 240.0f})
            result.push_back({{{x, 0, z}, {x + (x == 1380 ? 90 : 120), 0, z},
                {x + (x == 1380 ? 90 : 120), 0, z + 240}, {x, 0, z + 240}}, 8, "SOUTH BEACH STREET"});
        for (std::size_t i = 0; i < keys().size(); ++i) {
            const auto& key = keys()[i];
            const auto loop = [&](std::initializer_list<Vec3> points) {
                std::vector<Vec3> corners;
                for (const auto& p : points) corners.push_back(key.center + p);
                result.push_back({std::move(corners), 7.5f, key.name});
            };
            if (i == 0) {
                loop({{0, 0, 0}, {100, 0, -50}, {120, 0, 100}, {0, 0, 120}});
                loop({{-120, 0, -100}, {0, 0, -100}, {0, 0, 80}, {-100, 0, 100}});
            } else if (i == 1) {
                loop({{0, 0, 0}, {110, 0, -80}, {200, 0, -15}, {155, 0, 110}, {40, 0, 95}});
                loop({{0, 0, 0}, {-120, 0, -100}, {-220, 0, -10}, {-130, 0, 110}});
            } else if (i == 2) {
                loop({{0, 0, 0}, {160, 0, -50}, {180, 0, 100}, {0, 0, 120}});
                loop({{0, 0, 0}, {-100, 0, -80}, {-190, 0, -60}, {-160, 0, 80}, {-60, 0, 100}});
                loop({{0, 0, 120}, {90, 0, 120}, {90, 0, 200}, {0, 0, 200}});
            } else if (i == 3) {
                loop({{0, 0, 0}, {140, 0, -100}, {190, 0, -10}, {90, 0, 95}, {-40, 0, 80}});
                loop({{-40, 0, 80}, {-140, 0, 130}, {-220, 0, 40}, {-130, 0, -50}, {0, 0, 0}});
            } else {
                for (float x : {-90.0f, 0.0f}) for (float z : {-90.0f, 0.0f})
                    loop({{x, 0, z}, {x + 90, 0, z}, {x + 90, 0, z + 90}, {x, 0, z + 90}});
            }
        }
        return result;
    }();
    return data;
}
const std::vector<Road>& Environment::roads() {
    static const std::vector<Road> data = [] {
        std::vector<Road> result;
        const auto add = [&](float ax, float az, float bx, float bz, float width, const char* name) {
            result.push_back({Vec3(ax, 0, az), Vec3(bx, 0, bz), width, name});
        };
        add(0, -600, 0, 600, 9, "BISCAYNE BOULEVARD");
        add(240, -600, 240, 480, 10, "BAYFRONT AVENUE");
        add(-360, -600, -360, 900, 10, "US 1 SOUTH");
        for (float z : {-240.0f, 240.0f}) add(-600, z, 240, z, 10, "CAUSEWAY APPROACH");
        for (const auto& loop : street_loops()) for (std::size_t i = 0; i < loop.corners.size(); ++i) {
            const Vec3 a = loop.corners[i], b = loop.corners[(i + 1) % loop.corners.size()];
            bool duplicate = false;
            for (const auto& road : result)
                if (((road.a - a).LengthSq() < .01f && (road.b - b).LengthSq() < .01f)
                    || ((road.b - a).LengthSq() < .01f && (road.a - b).LengthSq() < .01f)) { duplicate = true; break; }
            if (!duplicate) result.push_back({a, b, loop.width, loop.name});
        }
        for (float z : {-60.0f, 60.0f}) add(0, z, 120, z, 7.5f, "MARKET STREET");
        add(-600, -360, -360, -360, 8, "WORKSHOP LANE");
        add(-520, -440, -520, -240, 8, "INDUSTRIAL ACCESS");
        add(-440, -480, -440, -240, 7, "WORKSHOP ACCESS");
        for (float z : {-420.0f, -300.0f}) add(-600, z, -360, z, 7, "SERVICE STREET");
        add(-560, 40, -360, 20, 7, "NEIGHBORHOOD LANE");
        add(-560, 460, -360, 480, 7, "RESIDENTIAL LANE");
        add(-500, -240, -500, 600, 7, "LITTLE HAVANA LANE");
        add(-430, -240, -430, 600, 7, "GARDEN LANE");
        for (float z : {-150.0f, 100.0f, 260.0f, 430.0f, 530.0f})
            add(-590, z, -360, z, 7, "NEIGHBORHOOD STREET");
        add(-280, 240, -280, 408, 7, "BRICKELL LANE");
        add(-210, 240, -210, 415, 7, "BRICKELL LANE");
        add(-360, 320, -140, 320, 7, "BRICKELL STREET");
        add(-240, 412, -240, 600, 7, "SOUTHERN LANE");
        add(-120, 415, -120, 600, 7, "SOUTHERN LANE");
        add(-360, 520, 0, 520, 7, "SOUTHERN STREET");
        add(-700, -240, -680, -62, 7, "AIRPORT VILLAGE LANE");
        add(-770, -140, -600, -140, 7, "AIRPORT VILLAGE STREET");
        add(-1140, -500, -1140, 120, 7, "AIRPORT NEIGHBORHOOD LANE");
        for (float z : {-400.0f, -280.0f, -40.0f, 60.0f})
            add(-1210, z, -1040, z, 7, "AIRPORT SERVICE STREET");
        add(60, -180, 60, 180, 7, "MARKET LANE");
        add(180, 0, 180, 240, 7, "BAYFRONT LANE");
        add(airports[0].apron_x(), airports[0].apron_z(), -780, -240, 8, "AIRPORT ACCESS");
        add(airports[1].apron_x(), airports[1].apron_z(), -2190, 4290, 7.5f, "KEY WEST AIRPORT ACCESS");
        for (float x : {1140.0f, 1260.0f, 1380.0f}) for (float z : {-360.0f, -120.0f, 120.0f, 360.0f})
            add(x, z, x + (x == 1380 ? 90 : 120), z, 7, "BEACH NEIGHBORHOOD LANE");
        for (float x : {1200.0f, 1320.0f, 1425.0f}) add(x, -480, x, 480, 7, "BEACH STUDIO LANE");
        add(570, 240, 750, 240, 12, "PORTMIAMI ROAD");
        add(660, 240, 660, 340, 8, "CRUISE PORT ACCESS");
        for (const auto& key : keys()) {
            result.push_back({key.entry, key.center, 10, "OVERSEAS HIGHWAY"});
            result.push_back({key.center, key.exit, 10, "OVERSEAS HIGHWAY"});
            const float x = key.center.GetX(), z = key.center.GetZ();
            add(x, z, x + key.rx * .84f, z, 7.5f, "MARINA LANE");
            for (const auto& loop : street_loops()) if (loop.name == key.name)
                for (std::size_t i = 1; i < loop.corners.size(); i += 2) {
                    const Vec3 a = loop.corners[i], b = a + (a - key.center).Normalized() * 35;
                    if (coast_radius(b.GetX(), b.GetZ()) < .8f) result.push_back({a, b, 6.5f, "KEYS CUL-DE-SAC"});
                }
        }
        const auto key_lane = [&](int index, float ax, float az, float bx, float bz) {
            const Vec3 center = keys()[index].center;
            add(center.GetX() + ax, center.GetZ() + az, center.GetX() + bx, center.GetZ() + bz, 6.5f, "VILLAGE LANE");
        };
        key_lane(0, 0, 40, 112, 40); key_lane(0, -110, 0, 0, 0); key_lane(0, -55, -90, -55, 92);
        key_lane(1, 0, 0, -100, 15); key_lane(1, -100, 15, -180, -10);
        key_lane(1, 0, 0, 150, 20); key_lane(1, 150, 20, 180, 0);
        key_lane(2, 0, 60, 175, 60); key_lane(2, -50, -40, -70, 60);
        key_lane(3, 0, 0, 70, 25); key_lane(3, 70, 25, 120, 15);
        key_lane(3, -40, 80, -80, 20); key_lane(3, -80, 20, -140, 30);
        key_lane(4, -45, -90, -45, 90); key_lane(4, 45, -90, 45, 90);
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
    for (const auto& airport : airports) if (airport.pavement(x, z)) return true;
    for (const auto& street : roads())
        if (distance_to(street.a, street.b, x, z) <= street.width / 2) return true;
    return false;
}
const char* Environment::district(float x, float z) {
    for (const auto& airport : airports) if (airport.contains(x, z)) return airport.name;
    if (x > 1000 && z < 1020) return "MIAMI BEACH / SOUTH BEACH";
    if (x > 420 && x < 930 && z > 30 && z < 450) return "PORTMIAMI";
    for (const auto& key : keys())
        if (std::hypot((x - key.center.GetX()) / key.rx, (z - key.center.GetZ()) / key.rz) < 1.2f) return key.name;
    if (z > 1080) return "OVERSEAS HIGHWAY / FLORIDA KEYS";
    if (x > 300) return "BISCAYNE BAY";
    if (x < -650) return "AIRPORT NEIGHBORHOOD";
    if (x < -330 && z < -120) return "WAREHOUSE / DESIGN DISTRICT";
    if (x < -240 || z > 330) return "LITTLE HAVANA / RESIDENTIAL";
    return z > 150 ? "BRICKELL / DOWNTOWN MIAMI" : "DOWNTOWN MIAMI";
}
float Environment::elevation(float x, float z) {
    const float radius = coast_radius(x, z);
    float ground = road_level - (road_level + 9) * smooth(.90f, 1.12f, radius);
    for (const auto& airport : airports) {
        const float airport_blend = (1 - smooth(airport.grounds_half_width + 5, airport.grounds_half_width + 75,
            std::abs(x - airport.center_x))) * (1 - smooth(220, 280, std::abs(z - airport.runway_z)));
        ground += (Airport::elevation - ground) * airport_blend;
    }
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
    const auto add_building = [&](float x, float z, Vec3 size, BuildingKind kind, bool east = false, bool positive = true) {
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
            overlaps &= clip(street.a.GetX(), street.b.GetX() - street.a.GetX(), x, size.GetX() / 2 + street.width / 2 + 3);
            overlaps &= clip(street.a.GetZ(), street.b.GetZ() - street.a.GetZ(), z, size.GetZ() / 2 + street.width / 2 + 3);
            if (overlaps) return false;
        }
        for (const auto& b : buildings_)
            if (std::abs(x - b.center.GetX()) < (size.GetX() + b.size.GetX()) / 2 + 2
                && std::abs(z - b.center.GetZ()) < (size.GetZ() + b.size.GetZ()) / 2 + 2) return false;
        // Keep entrances and the road footprint clear, including diagonal US 1.
        for (float sx = -size.GetX() / 2; sx <= size.GetX() / 2; sx += size.GetX() / 4)
            for (float sz = -size.GetZ() / 2; sz <= size.GetZ() / 2; sz += size.GetZ() / 4)
                if (road(x + sx, z + sz) || height(x + sx, z + sz) < 2.9f
                    || std::abs(height(x + sx, z + sz) - height(x, z)) > .35f) return false;
        buildings_.push_back({Vec3(x, height(x, z) + size.GetY() / 2, z), size, int(random() % 5), kind, east, positive});
        return true;
    };
    add_building(32, -120, Vec3(38, 9, 30), BuildingKind::Mall);
    add_building(150, 120, Vec3(38, 11, 30), BuildingKind::Mall);
    add_building(-60, -150, Vec3(24, 96, 24), BuildingKind::Tower);
    ports_.push_back({Vec3(660, road_level, 345), false, "PORTMIAMI"});
    for (float x : {606.0f, 714.0f}) add_building(x, 300, Vec3(24, 8, 22), BuildingKind::Warehouse);
    for (std::size_t i = 0; i < keys().size(); ++i) {
        const auto& key = keys()[i];
        const float x = key.center.GetX(), z = key.center.GetZ();
        if (i + 1 < keys().size()) {
            bool cafe = false;
            for (float ox : {40.0f, -40.0f, 80.0f, -80.0f, 140.0f, -140.0f}) {
                for (float oz : {-40.0f, 40.0f, -80.0f, 80.0f, 140.0f})
                    if (add_building(x + ox, z + oz, Vec3(14, 4, 12), BuildingKind::Cafe, true)) { cafe = true; break; }
                if (cafe) break;
            }
        }
        ports_.push_back({Vec3(x + key.rx * .84f, height(x + key.rx * .84f, z), z), true, key.name});
    }
    const auto& miami = airports[0];
    add_building(miami.center_x + 80, miami.runway_z - 36, Vec3(30, 7, 18), BuildingKind::Terminal, true, false);
    add_building(miami.center_x + 82, miami.runway_z + 76, Vec3(32, 9, 24), BuildingKind::Hangar, true, false);
    add_building(miami.center_x + 82, miami.runway_z - 104, Vec3(7, 18, 7), BuildingKind::ControlTower);
    add_building(airports[1].center_x + 82, airports[1].plane_z(), Vec3(24, 4.5f, 14), BuildingKind::Terminal, true, false);
    // Frontage lots fill both sides of streets; a second row fills deep blocks.
    // ponytail: cardinal footprints on angled streets; rotate lots if precise parcel geometry is needed.
    for (const auto& street : roads()) {
        if (street.bridge >= 0) continue;
        const Vec3 direction = (street.b - street.a).Normalized(), side = direction.Cross(Vec3::sAxisY());
        const float length = (street.b - street.a).Length();
        for (float along = 12; along < length - 12; along += 18) for (float sign : {-1.0f, 1.0f}) for (int row = 0; row < 2; ++row) {
            const Vec3 edge = street.a + direction * along;
            const float x = edge.GetX(), z = edge.GetZ();
            const int roll = int(random() % 100);
            int key_index = -1;
            for (std::size_t i = 0; i < keys().size(); ++i)
                if (std::hypot((x - keys()[i].center.GetX()) / keys()[i].rx, (z - keys()[i].center.GetZ()) / keys()[i].rz) < 1)
                    key_index = int(i);
            BuildingKind kind;
            if (key_index >= 0 || z > 1080) kind = BuildingKind::House;
            else if (x > 1000) kind = roll < 30 ? BuildingKind::Hotel : roll < 52 ? BuildingKind::Apartment
                : roll < 76 ? BuildingKind::Shop : roll < 90 ? BuildingKind::Cafe : BuildingKind::Club;
            else if ((x < -330 && z < -120) || (x > 420 && x < 930)) kind = roll < 60 ? BuildingKind::Warehouse
                : roll < 80 ? BuildingKind::Office : BuildingKind::Shop;
            else if (x < -240 || z > 330) kind = roll < 65 ? BuildingKind::House : roll < 82 ? BuildingKind::Apartment
                : roll < 94 ? BuildingKind::Shop : BuildingKind::Cafe;
            else kind = roll < 25 ? BuildingKind::Tower : roll < 45 ? BuildingKind::Office : roll < 65 ? BuildingKind::Apartment
                : roll < 83 ? BuildingKind::Shop : roll < 94 ? BuildingKind::Cafe : BuildingKind::Club;
            float width = 10 + random() % 7, depth = 10 + random() % 6, height = 4;
            if (kind == BuildingKind::House) { width = 7 + random() % 5; depth = 8 + random() % 6; height = key_index == 4 || random() % 4 == 0 ? 6.2f : 3.3f; }
            if (kind == BuildingKind::Tower) { width = 20 + random() % 9; depth = 20 + random() % 7; height = 35 + random() % 56; }
            if (kind == BuildingKind::Apartment || kind == BuildingKind::Hotel || kind == BuildingKind::Office) {
                width = 14 + random() % 9; depth = 14 + random() % 8; height = 9 + 3.2f * (random() % 6);
            }
            if (kind == BuildingKind::Warehouse) { width = 20 + random() % 12; depth = 16 + random() % 9; height = 6 + random() % 4; }
            if (kind == BuildingKind::Club) height = 6;
            const bool east = std::abs(side.GetX()) > std::abs(side.GetZ());
            const Vec3 size = east ? Vec3(depth, height, width) : Vec3(width, height, depth);
            const float half_depth = (std::abs(side.GetX()) * size.GetX() + std::abs(side.GetZ()) * size.GetZ()) / 2;
            // Keep the front row beyond the reserved sidewalk, including its boundary.
            const float setback = (street.width / 2 + 3.5f) * (std::abs(side.GetX()) + std::abs(side.GetZ()));
            const Vec3 center = edge + side * (sign * (setback + half_depth + row * (half_depth * 2 + 4)));
            bool airport_reserved = false;
            for (const auto& airport : airports)
                if (airport.contains(center.GetX(), center.GetZ()) || airport.flight_path(center.GetX(), center.GetZ())) airport_reserved = true;
            if (airport_reserved) continue;
            add_building(center.GetX(), center.GetZ(), size, kind, east, (east ? side.GetX() : side.GetZ()) * sign < 0);
        }
    }
    const auto add_tree = [&](float x, float z) {
        if (coast_radius(x, z) > .88f || height(x, z) < 2.9f || road(x, z)) return;
        for (const auto& airport : airports) if (airport.contains(x, z) || airport.flight_path(x, z)) return;
        for (const auto& street : roads()) if (distance_to(street.a, street.b, x, z) < street.width / 2 + 2) return;
        for (const auto& b : buildings_) {
            const float clearance = b.kind == BuildingKind::Cafe ? 3.0f : .8f;
            if (std::abs(x - b.center.GetX()) < b.size.GetX() / 2 + clearance
                && std::abs(z - b.center.GetZ()) < b.size.GetZ() / 2 + clearance) return;
        }
        for (const auto& tree : trees_) if (std::hypot(x - tree.base.GetX(), z - tree.base.GetZ()) < 6) return;
        trees_.push_back({Vec3(x, height(x, z), z), 5.5f + random() % 40 * .1f,
            std::remainder(x * 13 + z * 17, 360.0f) * .017453293f});
    };
    // Plant verges first so random park trees cannot consume their spacing.
    for (const auto& street : roads()) {
        if (street.bridge >= 0) continue;
        const Vec3 direction = (street.b - street.a).Normalized(), side = direction.Cross(Vec3::sAxisY());
        for (float along = 8; along < (street.b - street.a).Length() - 8; along += 20)
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 p = street.a + direction * along + side * (sign * (street.width / 2 + 2.25f));
                add_tree(p.GetX(), p.GetZ());
            }
    }
    for (int i = 0; i < 5000; ++i) {
        const auto& island = islands()[random() % islands().size()];
        const float x = island.center.GetX() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_x;
        const float z = island.center.GetZ() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_z;
        add_tree(x, z);
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
