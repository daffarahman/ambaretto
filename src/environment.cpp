#include "environment.hpp"
#include "airport.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <string_view>
#include <limits>

namespace forza {
namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float river_radius(float x, float z) {
    const float center = -1300 + 16 * std::sin((z + 160) / 240) - 180 * smooth(480, 980, z);
    return std::hypot(x - center, std::max({-420 - z, 0.0f, z - 1100})) / 55;
}
float segment_fraction(Vec3 a, Vec3 b, float x, float z) {
    const float dx = b.GetX() - a.GetX(), dz = b.GetZ() - a.GetZ();
    return ((x - a.GetX()) * dx + (z - a.GetZ()) * dz) / (dx * dx + dz * dz);
}
float distance_to(Vec3 a, Vec3 b, float x, float z) {
    const float t = std::clamp(segment_fraction(a, b, x, z), 0.0f, 1.0f);
    return std::hypot(x - (a.GetX() + (b.GetX() - a.GetX()) * t), z - (a.GetZ() + (b.GetZ() - a.GetZ()) * t));
}
Vec3 cubic(Vec3 a, Vec3 control_a, Vec3 control_b, Vec3 b, float t) {
    const float u = 1 - t;
    return a * (u * u * u) + control_a * (3 * u * u * t) + control_b * (3 * u * t * t) + b * (t * t * t);
}
template<std::size_t N>
float inlet_radius(const Vec3 (&shore)[N], float x, float z) {
    bool inside = false;
    float distance = 10000;
    Vec3 a = shore[N - 1];
    for (Vec3 b : shore) {
        distance = std::min(distance, distance_to(a, b, x, z));
        if ((a.GetZ() > z) != (b.GetZ() > z)
            && x < a.GetX() + (b.GetX() - a.GetX()) * ((z - a.GetZ()) / (b.GetZ() - a.GetZ()))) inside = !inside;
        a = b;
    }
    return 1 + (inside ? distance : -distance) / 100;
}
float northern_bay_radius(float x, float z) {
    if (x < -1450 || x > 550 || z < -1650 || z > -20) return 0;
    // Follow the northern waterfront around the airport and the US 1 neighborhood.
    // The outer edge lies offshore so the old inlet opens directly onto the sea.
    static const Vec3 shore[]{
        {-1400, 0, -1600}, {500, 0, -1600}, {350, 0, -950}, {-150, 0, -1110},
        {-230, 0, -1120}, {-320, 0, -1100}, {-310, 0, -1060}, {-320, 0, -1010},
        {-305, 0, -990}, {-280, 0, -985}, {-240, 0, -1000}, {-195, 0, -1010},
        {-165, 0, -995}, {-135, 0, -965}, {-65, 0, -945}, {-70, 0, -895},
        {-30, 0, -825}, {-60, 0, -790}, {-110, 0, -765}, {-205, 0, -765},
        {-250, 0, -785}, {-255, 0, -825}, {-260, 0, -865}, {-280, 0, -890},
        {-370, 0, -890}, {-430, 0, -875}, {-470, 0, -830}, {-485, 0, -785},
        {-455, 0, -695}, {-445, 0, -640}, {-465, 0, -615}, {-530, 0, -580},
        {-600, 0, -575}, {-665, 0, -520}, {-690, 0, -480}, {-685, 0, -435},
        {-710, 0, -365}, {-755, 0, -330}, {-815, 0, -305}, {-875, 0, -310},
        {-900, 0, -293}, {-930, 0, -265}, {-947, 0, -243}, {-958, 0, -191},
        {-956, 0, -151}, {-960, 0, -103}, {-958, 0, -65}, {-970, 0, -48},
        {-980, 0, -52}, {-980, 0, -75}, {-980, 0, -119}, {-980, 0, -159},
        {-978, 0, -196}, {-978, 0, -235}, {-980, 0, -263}, {-980, 0, -301},
        {-980, 0, -349}, {-980, 0, -379}, {-980, 0, -460}, {-980, 0, -545},
        {-1004, 0, -590}, {-1030, 0, -590}, {-1065, 0, -590}, {-1095, 0, -558},
        {-1095, 0, -520}, {-1100, 0, -480}, {-1100, 0, -444}, {-1100, 0, -397},
        {-1100, 0, -347}, {-1105, 0, -312}, {-1116, 0, -294}, {-1129, 0, -311},
        {-1135, 0, -341}, {-1135, 0, -378}, {-1135, 0, -423}, {-1131, 0, -459},
        {-1141, 0, -505}, {-1154, 0, -560}, {-1198, 0, -560}, {-1229, 0, -551},
        {-1253, 0, -522}, {-1269, 0, -486}, {-1276, 0, -440}, {-1297, 0, -458},
        {-1328, 0, -471}, {-1290, 0, -530}, {-1251, 0, -589}, {-1213, 0, -621},
        {-1185, 0, -638}, {-1156, 0, -650}, {-1129, 0, -650}, {-1091, 0, -660},
        {-1053, 0, -654}, {-1008, 0, -655}, {-983, 0, -676}, {-964, 0, -715},
        {-990, 0, -750}, {-1060, 0, -780}, {-1140, 0, -785},
        {-1280, 0, -1050}
    };
    return inlet_radius(shore, x, z);
}
float southern_inlets_radius(float x, float z) {
    if (x < -1250 || x > -100 || z < -210 || z > 1230) return 0;
    // The marked channel runs east of the runway, through the city loop and out to sea.
    static const Vec3 airport_shore[]{
        {-966, 0, -149}, {-903, 0, -149}, {-854, 0, -123}, {-828, 0, -60},
        {-810, 0, -30}, {-776, 0, -7}, {-731, 0, 7}, {-668, 0, 15},
        {-657, 0, 41}, {-668, 0, 82}, {-664, 0, 131}, {-672, 0, 187},
        {-668, 0, 239}, {-657, 0, 287}, {-657, 0, 336}, {-672, 0, 388},
        {-713, 0, 433}, {-769, 0, 481}, {-806, 0, 522}, {-821, 0, 548},
        {-817, 0, 582}, {-784, 0, 627}, {-757, 0, 679}, {-750, 0, 742},
        {-735, 0, 787}, {-746, 0, 840}, {-743, 0, 925}, {-731, 0, 989},
        {-728, 0, 1022}, {-730, 0, 1180}, {-1195, 0, 1180}, {-1194, 0, 1022},
        {-1194, 0, 989}, {-1157, 0, 922}, {-1172, 0, 892}, {-1190, 0, 862},
        {-1183, 0, 836}, {-1142, 0, 821}, {-1097, 0, 813}, {-1045, 0, 817},
        {-1041, 0, 791}, {-1067, 0, 735}, {-1067, 0, 687}, {-1071, 0, 634},
        {-1052, 0, 604}, {-1004, 0, 571}, {-963, 0, 545}, {-959, 0, 507},
        {-944, 0, 466}, {-910, 0, 433}, {-866, 0, 403}, {-832, 0, 358},
        {-813, 0, 317}, {-802, 0, 265}, {-795, 0, 228}, {-791, 0, 205},
        {-810, 0, 183}, {-832, 0, 175}, {-843, 0, 149}, {-840, 0, 101},
        {-821, 0, 63}, {-828, 0, -4}, {-836, 0, -45}, {-858, 0, -75},
        {-888, 0, -86}, {-929, 0, -71}, {-963, 0, -71}, {-970, 0, -86},
        {-966, 0, -119}
    };
    static const Vec3 coastal_shore[]{
        {-633, 0, 1037}, {-611, 0, 978}, {-588, 0, 870}, {-551, 0, 821},
        {-528, 0, 821}, {-494, 0, 884}, {-450, 0, 925}, {-398, 0, 929},
        {-357, 0, 951}, {-301, 0, 933}, {-241, 0, 918}, {-200, 0, 940},
        {-159, 0, 993}, {-166, 0, 1015}, {-170, 0, 1150}, {-650, 0, 1150}
    };
    return std::max(inlet_radius(airport_shore, x, z), inlet_radius(coastal_shore, x, z));
}
bool city_loop_crossing(Vec3 a, Vec3 b) {
    return (std::abs(a.GetZ() + 690) < .01f || std::abs(a.GetZ() - 750) < .01f)
        && std::abs(a.GetZ() - b.GetZ()) < .01f
        && std::min(a.GetX(), b.GetX()) >= -1120.01f && std::max(a.GetX(), b.GetX()) <= -359.99f;
}
bool southern_water_crossing(Vec3 a, Vec3 b) {
    const Vec3 middle = (a + b) / 2;
    return std::max({southern_inlets_radius(a.GetX(), a.GetZ()), southern_inlets_radius(b.GetX(), b.GetZ()),
        southern_inlets_radius(middle.GetX(), middle.GetZ())}) >= .6f;
}
bool level_bridge_covers(Vec3 a, Vec3 b, float width) {
    for (const auto& bridge : Environment::bridges())
        if (bridge.clearance <= Airport::elevation && bridge.width >= width
            && distance_to(bridge.a, bridge.b, a.GetX(), a.GetZ()) < .01f
            && distance_to(bridge.a, bridge.b, b.GetX(), b.GetZ()) < .01f) return true;
    return false;
}
float deck_height(const Bridge& bridge, float x, float z) {
    const float t = segment_fraction(bridge.a, bridge.b, x, z);
    float result = -std::numeric_limits<float>::infinity();
    const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
    if (bridge.a.GetY() <= 0 && !bridge.has_curve()) {
        if (t < 0 || t > 1 || distance_to(bridge.a, bridge.b, x, z) > bridge.width / 2 + .0001f) return result;
        const float row = t * rows;
        const int i = std::min(int(row), rows - 1);
        return bridge.point(float(i) / rows).GetY()
            + (bridge.point(float(i + 1) / rows).GetY() - bridge.point(float(i) / rows).GetY()) * (row - i);
    }
    const float bow = bridge.has_curve() ? std::max(distance_to(bridge.a, bridge.b, bridge.control_a.GetX(), bridge.control_a.GetZ()),
        distance_to(bridge.a, bridge.b, bridge.control_b.GetX(), bridge.control_b.GetZ())) : 0;
    if (t < -.05f || t > 1.05f || distance_to(bridge.a, bridge.b, x, z) > bridge.width / 2 + bow + 1) return result;
    const auto sample = [&](Vec3 a, Vec3 b, Vec3 c) {
        const auto cross = [](Vec3 u, Vec3 v) { return u.GetX() * v.GetZ() - u.GetZ() * v.GetX(); };
        const Vec3 p(x - a.GetX(), 0, z - a.GetZ()), u = b - a, v = c - a;
        const float determinant = cross(u, v), s = cross(p, v) / determinant, r = cross(u, p) / determinant;
        if (s >= -.00001f && r >= -.00001f && s + r <= 1.00001f)
            result = std::max(result, a.GetY() + s * u.GetY() + r * v.GetY());
    };
    for (int row = 0; row < rows; ++row) {
        const float start = float(row) / rows, end = float(row + 1) / rows;
        const Vec3 a = bridge.point(start) - bridge.side(start) * (bridge.width / 2);
        const Vec3 b = bridge.point(start) + bridge.side(start) * (bridge.width / 2);
        const Vec3 c = bridge.point(end) - bridge.side(end) * (bridge.width / 2);
        const Vec3 d = bridge.point(end) + bridge.side(end) * (bridge.width / 2);
        sample(a, b, c); sample(b, d, c);
    }
    return result;
}
struct KeyLayout {
    Vec3 entry, center, exit; float rx, rz; const char* name;
    Vec3 forward() const { return (exit - entry).Normalized(); }
    Vec3 right() const { return Vec3::sAxisY().Cross(forward()); }
    Vec3 local(float across, float along) const { return center + right() * across + forward() * along; }
    Vec3 dock() const { return local(std::string_view(name) == "NGAWISH" ? rx * .84f : 95, 0); }
};
const std::vector<KeyLayout>& keys() {
    static const std::vector<KeyLayout> layout{
        {{-360, 0, 1260}, {-360, 0, 1500}, {-360, 0, 1740}, 150, 300, "NGAWISH"},
        {{-540, 0, 2040}, {-780, 0, 2220}, {-840, 0, 2340}, 150, 300, "AMBAMORADA"},
        {{-1080, 0, 2790}, {-1320, 0, 2940}, {-1440, 0, 3090}, 150, 300, "MARATHON"},
        {{-1590, 0, 3540}, {-1740, 0, 3720}, {-1890, 0, 3870}, 150, 300, "LOWER KEYS"},
        {{-1980, 0, 4170}, {-2100, 0, 4380}, {-2130, 0, 4650}, 150, 300, "KEY WEST"}
    };
    return layout;
}
Bridge key_bridge(std::size_t index) {
    static constexpr float clearances[]{9, 10, 12, 9};
    static constexpr const char* names[]{"OVERSEAS HIGHWAY", "LONG KEY BRIDGE", "SEVEN MILE BRIDGE", "KEY WEST CAUSEWAY"};
    const auto& before = keys()[index]; const auto& after = keys()[index + 1];
    Bridge bridge{before.exit, after.entry, Environment::highway_width, clearances[index], names[index]};
    const float handle = (bridge.b - bridge.a).Length() * .45f;
    bridge.control_a = bridge.a + before.forward() * handle;
    bridge.control_b = bridge.b - after.forward() * handle;
    return bridge;
}
}

Vec3 Bridge::point(float t) const {
    // Explicit endpoint elevations keep curved deck sections on one continuous grade.
    if (a.GetY() > 0) {
        Vec3 p = a + (b - a) * t;
        if (std::string_view(name) == "KEYS EXIT RAMP") p.SetY(a.GetY() + (b.GetY() - a.GetY()) * smooth(1.0f / 6, 1, t));
        return p;
    }
    const float ramp = std::min(.34f, 160.0f / (b - a).Length());
    const float blend = smooth(0, ramp, std::min(t, 1 - t));
    const Vec3 p = has_curve() ? cubic(a, control_a, control_b, b, t) : a + (b - a) * t;
    return Vec3(p.GetX(), Environment::road_level + (clearance - Environment::road_level) * blend, p.GetZ());
}
Vec3 Bridge::side(float t) const {
    if (start_side.LengthSq() > 0) return start_side + (end_side - start_side) * t;
    const float u = 1 - t;
    const Vec3 delta = has_curve() ? (control_a - a) * (3 * u * u) + (control_b - control_a) * (6 * u * t)
        + (b - control_b) * (3 * t * t) : b - a;
    return Vec3(delta.GetX(), 0, delta.GetZ()).Normalized().Cross(Vec3::sAxisY());
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
    static const std::vector<Bridge> data = [] {
        std::vector<Bridge> result{
        {{180, 0, 240}, {570, 0, 240}, 16, 8, "MACARTHUR CAUSEWAY"},
        {{750, 0, 240}, {1140, 0, 240}, 16, 8, "MACARTHUR CAUSEWAY"},
        {{180, 0, -240}, {1140, 0, -240}, 12, 7, "VENETIAN CAUSEWAY"},
        {{-360, highway_level, 900}, {-360, road_level, 1260}, highway_width, highway_level, "KEYS EXIT RAMP"},
        key_bridge(0), key_bridge(1), key_bridge(2), key_bridge(3)
        };
        // Keep the city-loop crossings at street level, with no hump or ramps.
        result.push_back({{-360, 0, -690}, {-1120, 0, -690}, 18, road_level, "INTERSTATE CITY LOOP"});
        result.push_back({{-1120, 0, 750}, {-692.5f, 0, 750}, 18, road_level, "INTERSTATE CITY LOOP"});
        result.push_back({{-1180, 0, -690}, {-1180, 0, -500}, 7, road_level, "AIRPORT NEIGHBORHOOD LANE"});
        for (const auto& path : highways()) {
            if (std::string_view(path.name) == "KEYS EXIT RAMP" || std::string_view(path.name) == "OVERSEAS HIGHWAY"
                || std::string_view(path.name) == "AIRPORT INTERNAL ACCESS"
                || std::string_view(path.name) == "US 1 SLOW LANES") continue;
            auto points = path.corners;
            if (path.closed) points.push_back(points.front());
            std::vector<Vec3> sides;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const std::size_t before = i == 0 ? (path.closed ? points.size() - 2 : 0) : i - 1;
                const std::size_t after = i + 1 == points.size() ? (path.closed ? 1 : i) : i + 1;
                Vec3 incoming = points[i] - points[before], outgoing = points[after] - points[i];
                incoming.SetY(0); outgoing.SetY(0);
                incoming = incoming.NormalizedOr(outgoing.NormalizedOr(Vec3::sAxisZ()));
                outgoing = outgoing.NormalizedOr(incoming);
                sides.push_back((incoming + outgoing).Cross(Vec3::sAxisY()) / (1 + incoming.Dot(outgoing)));
            }
            for (std::size_t i = 0; i + 1 < points.size(); ++i) {
                const bool city_loop = std::string_view(path.name) == "INTERSTATE CITY LOOP";
                const bool airport = std::string_view(path.name) == "AIRPORT INTERCHANGE"
                    || std::string_view(path.name) == "AIRPORT CROSS STREET";
                Vec3 a = points[i], b = points[i + 1];
                Vec3 start_side = sides[i], end_side = sides[i + 1];
                if (airport) {
                    if (!southern_water_crossing(a, b)) continue;
                    const float level = (a.GetZ() + b.GetZ()) / 2 < 0 ? Airport::elevation : road_level;
                    a.SetY(level); b.SetY(level);
                    // Square the bank ends; shared bridge joints retain their matching miters.
                    const Vec3 direction = (b - a).Normalized(), side = direction.Cross(Vec3::sAxisY());
                    // Overlap the ground pavement slightly so compressed collision edges cannot leave a seam.
                    if (i == 0 || !southern_water_crossing(points[i - 1], points[i])) { start_side = side; a -= direction * .25f; }
                    if (i + 2 == points.size() || !southern_water_crossing(points[i + 1], points[i + 2])) { end_side = side; b += direction * .25f; }
                }
                if ((std::string_view(path.name) == "US 1 GRAND BOULEVARD"
                        && points[i].GetY() <= road_level + .001f && points[i + 1].GetY() <= road_level + .001f)
                    || (city_loop && (points[i].GetY() <= 0 || points[i + 1].GetY() <= 0 || city_loop_crossing(a, b)))) continue;
                result.push_back({a, b, path.width, airport ? a.GetY() : city_loop ? road_level : 32, path.name,
                    start_side, end_side, !path.closed && i == 0, !path.closed && i + 2 == points.size()});
            }
        }
        return result;
    }();
    return data;
}
const std::vector<StreetLoop>& Environment::highways() {
    static const std::vector<StreetLoop> data = [] {
        std::vector<StreetLoop> result;
        const auto line = [](std::vector<Vec3>& points, Vec3 end) {
            const Vec3 start = points.back();
            const int count = std::max(1, int(std::ceil((end - start).Length() / 24)));
            for (int i = 1; i <= count; ++i) points.push_back(start + (end - start) * (float(i) / count));
        };
        std::vector<Vec3> ring{{-360, road_level, 750}};
        const auto arc = [&](float x, float z, float start, float end) {
            line(ring, Vec3(x + 280 * std::cos(start), road_level, z + 280 * std::sin(start)));
            for (int i = 1; i <= 20; ++i) {
                const float angle = start + (end - start) * (float(i) / 20);
                ring.emplace_back(x + 280 * std::cos(angle), road_level, z + 280 * std::sin(angle));
            }
        };
        constexpr float pi = 3.14159265359f;
        arc(-100, 470, pi / 2, 0); arc(-100, -410, 0, -pi / 2);
        line(ring, Vec3(-360, road_level, -690));
        arc(-1120, -410, -pi / 2, -pi); arc(-1120, 470, pi, pi / 2);
        line(ring, ring.front());
        ring.pop_back(); // The closed route adds the shared first point itself.
        // The western river crossing follows the curve at street level, without ramps.
        for (auto& p : ring) {
            const float distance = std::hypot(p.GetX() + 1332, p.GetZ() - 654);
            p.SetY(distance < 265 ? road_level : 0);
        }
        result.push_back({ring, 18, "INTERSTATE CITY LOOP", true, 20, 48, 4});
        std::vector<Vec3> main{{-360, road_level, -840}};
        line(main, Vec3(-360, road_level, 360));
        for (int i = 1; i <= 15; ++i) {
            const float z = 360 + 300 * float(i) / 15;
            main.emplace_back(-360, road_level + (highway_level - road_level) * smooth(360, 660, z), z);
        }
        line(main, Vec3(-360, highway_level, 900));
        result.push_back({main, highway_width, "US 1 GRAND BOULEVARD", false, 16, 24, 6});
        std::vector<Vec3> skyway{{-360, 0, 750}};
        const auto sky_curve = [&](Vec3 control_a, Vec3 control_b, Vec3 end, int samples) {
            const Vec3 start = skyway.back();
            for (int i = 1; i <= samples; ++i) {
                const float t = float(i) / samples, u = 1 - t;
                skyway.push_back(start * (u * u * u) + control_a * (3 * u * u * t)
                    + control_b * (3 * u * t * t) + end * (t * t * t));
            }
        };
        sky_curve(Vec3(-360, 0, 1050), Vec3(1000, 0, 960), Vec3(1260, 0, 760), 72);
        sky_curve(Vec3(1470, 0, 600), Vec3(1470, 0, 550), Vec3(1470, 0, 480), 24);
        std::vector<float> distance{0};
        for (std::size_t i = 1; i < skyway.size(); ++i) {
            distance.push_back(distance.back() + (skyway[i] - skyway[i - 1]).Length());
        }
        for (std::size_t i = 0; i < skyway.size(); ++i) {
            const float high = highway_level + (32 - highway_level) * smooth(180, 780, distance[i]);
            skyway[i].SetY(road_level + (high - road_level) * smooth(120, 700, distance.back() - distance[i]));
        }
        result.push_back({skyway, highway_width, "BEACH TO KEYS SKYWAY", false, 22, 36, 6});
        std::vector<Vec3> cutoff;
        for (int i = 0; i <= 18; ++i) {
            const float t = float(i) / 18;
            cutoff.emplace_back(-360, highway_level + (road_level - highway_level) * smooth(1.0f / 6, 1, t), 900 + 360 * t);
        }
        result.push_back({cutoff, highway_width, "KEYS EXIT RAMP", false, 12, 12, 6});
        const auto& airport = airports[0];
        std::vector<Vec3> access{{-360, 0, 750}};
        const Vec3 start = access.front(), end(airport.gate_x(), 0, airport.gate_z());
        for (int i = 1; i <= 48; ++i) {
            const float t = float(i) / 48, u = 1 - t;
            access.push_back(start * (u * u * u) + Vec3(-780, 0, 750) * (3 * u * u * t)
                + Vec3(airport.gate_x(), 0, 380) * (3 * u * t * t) + end * (t * t * t));
        }
        line(access, Vec3(airport.gate_x(), 0, -180));
        for (int i = 1; i <= 16; ++i) {
            const float angle = pi + pi / 2 * float(i) / 16;
            access.emplace_back(airport.gate_x() + 60 + 60 * std::cos(angle), 0, -180 + 60 * std::sin(angle));
        }
        line(access, Vec3(-780, 0, -240));
        result.push_back({access, 18, "AIRPORT INTERCHANGE", false, 14, 12, 4});
        for (std::size_t i = 1; i < access.size(); ++i) {
            const Vec3 a = access[i - 1], b = access[i];
            if (a.GetZ() >= 240 && b.GetZ() <= 240) {
                std::vector<Vec3> connector{a + (b - a) * ((240 - a.GetZ()) / (b.GetZ() - a.GetZ()))};
                line(connector, Vec3(-600, 0, 240));
                result.push_back({connector, 18, "AIRPORT CROSS STREET", false, 10, 8, 4});
                break;
            }
        }
        std::vector<Vec3> frontage{{-382, road_level, -588}};
        const auto frontage_turn = [&](float x, float z, float start, float end) {
            for (int i = 1; i <= 8; ++i) {
                const float angle = start + (end - start) * float(i) / 8;
                frontage.emplace_back(x + 12 * std::cos(angle), road_level, z + 12 * std::sin(angle));
            }
        };
        frontage_turn(-370, -588, pi, pi * 1.5f); line(frontage, Vec3(-350, road_level, -600));
        frontage_turn(-350, -588, -pi / 2, 0); line(frontage, Vec3(-338, road_level, 888));
        frontage_turn(-350, 888, 0, pi / 2); line(frontage, Vec3(-370, road_level, 900));
        frontage_turn(-370, 888, pi / 2, pi);
        result.push_back({frontage, 7, "US 1 SLOW LANES", true, 6, 12, 2.2f});
        std::vector<Vec3> overseas{keys().front().entry};
        const auto island_curve = [&](Vec3 end, Vec3 direction) {
            const Vec3 start = overseas.back();
            const float handle = (end - start).Length() / 3;
            const int count = int((end - start).Length() / 8) + 1;
            for (int i = 1; i <= count; ++i)
                overseas.push_back(cubic(start, start + direction * handle, end - direction * handle, end, float(i) / count));
        };
        for (std::size_t i = 0; i < keys().size(); ++i) {
            const auto& key = keys()[i];
            island_curve(key.center, key.forward()); island_curve(key.exit, key.forward());
            if (i + 1 == keys().size()) break;
            const Bridge bridge = key_bridge(i);
            const int count = int((bridge.b - bridge.a).Length() / 8) + 1;
            for (int row = 1; row <= count; ++row) overseas.push_back(bridge.point(float(row) / count));
            overseas.back().SetY(0); // The next island follows the terrain, including the airfield.
        }
        result.push_back({overseas, highway_width, "OVERSEAS HIGHWAY", false, 12, 126, 6});
        return result;
    }();
    return data;
}
const std::vector<StreetLoop>& Environment::street_loops() {
    static const std::vector<StreetLoop> data = [] {
        std::vector<StreetLoop> result{
            {{{0, 0, -180}, {120, 0, -180}, {120, 0, 180}, {0, 0, 180}}, 9, "BISCAYNE BLOCK"},
            {{{-120, 0, -240}, {0, 0, -240}, {0, 0, -60}, {-100, 0, -60}}, 8, "DOWNTOWN MARKET"},
            {{{-100, 0, -60}, {0, 0, -60}, {0, 0, 120}, {-140, 0, 100}}, 8, "CIVIC QUARTER"},
            {{{-140, 0, 100}, {0, 0, 120}, {0, 0, 240}, {-120, 0, 240}}, 8, "BRICKELL SHOPS"},
            {{{120, 0, -240}, {180, 0, -240}, {180, 0, 0}, {120, 0, 0}}, 9, "DOWNTOWN EAST"},
            {{{120, 0, 0}, {180, 0, 0}, {180, 0, 240}, {120, 0, 240}}, 9, "BAYFRONT"},
            {{{-360, 0, -480}, {-600, 0, -480}, {-600, 0, -240}, {-360, 0, -240}}, 10, "WAREHOUSE DISTRICT"},
            {{{-360, 0, -240}, {-240, 0, -240}, {-210, 0, -60}, {-360, 0, -80}}, 8, "DESIGN DISTRICT"},
            {{{-600, 0, -240}, {-360, 0, -240}, {-360, 0, -80}, {-580, 0, -40}}, 8, "LITTLE HAVANA"},
            {{{-580, 0, -40}, {-360, 0, -80}, {-360, 0, 160}, {-600, 0, 200}}, 8, "RESIDENTIAL TERRACE"},
            {{{-600, 0, 200}, {-360, 0, 160}, {-360, 0, 360}, {-580, 0, 340}}, 8, "GARDEN NEIGHBORHOOD"},
            {{{-580, 0, 340}, {-360, 0, 360}, {-360, 0, 600}, {-600, 0, 600}}, 8, "SOUTH MIAMI"},
            {{{-360, 0, 240}, {-120, 0, 240}, {-160, 0, 420}, {-360, 0, 400}}, 8, "BRICKELL RESIDENTIAL"},
            {{{-160, 0, 420}, {0, 0, 400}, {0, 0, 600}, {-360, 0, 600}, {-360, 0, 400}}, 8, "SOUTHERN NEIGHBORHOOD"},
            {{{-780, 0, -240}, {-600, 0, -240}, {-580, 0, -40}, {-760, 0, -80}}, 8, "AIRPORT NEIGHBORHOOD"},
            // One straight neighborhood road continues the airport's northern bridge.
            {{{-1180, 0, -690}, {-1180, 0, -500}, {-1180, 0, 120}, {-1180, 0, 750}},
                7, "AIRPORT RESIDENTIAL", false, 8, 12},
        };
        for (float x : {1140.0f, 1260.0f, 1380.0f}) for (float z : {-480.0f, -240.0f, 0.0f, 240.0f})
            result.push_back({{{x, 0, z}, {x + (x == 1380 ? 90 : 120), 0, z},
                {x + (x == 1380 ? 90 : 120), 0, z + 240}, {x, 0, z + 240}}, 8, "SOUTH BEACH STREET"});
        // Ngawish is a highway service island, without neighborhood streets.
        for (std::size_t i = 1; i < keys().size(); ++i) {
            const auto& key = keys()[i];
            result.push_back({{key.local(50, -80), key.local(95, -80), key.local(95, 80), key.local(50, 80)},
                6.5f, key.name, true, 5, 3, 1.8f});
        }
        for (auto& loop : result) {
            float center_x = 0;
            for (const auto& p : loop.corners) center_x += p.GetX();
            center_x /= loop.corners.size();
            for (auto& p : loop.corners) if (p.GetX() == airports[0].center_x) {
                const bool west = center_x < airports[0].center_x;
                const float half = west ? std::max(airports[0].grounds_half_width, 115.f) : airports[0].grounds_half_width;
                p.SetX(airports[0].center_x + (west ? -1 : 1) * (half + 20));
            }
            for (auto& p : loop.corners) if (std::abs(p.GetX() + 360) < .01f && p.GetZ() <= 900)
                p.SetX(center_x < -360 ? -382 : -338);
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
        add(0, -600, 0, 600, 14, "BISCAYNE BOULEVARD");
        add(-360, -840, -360, 360, highway_width, "US 1 GRAND BOULEVARD");
        for (float x : {-382.0f, -338.0f}) add(x, -600, x, 900, 7, "US 1 SLOW LANE");
        for (float z : {-600.0f, 900.0f}) add(-382, z, -338, z, 14, "US 1 SLOW LANE TURN");
        for (float z : {-240.0f, 240.0f}) add(-600, z, 180, z, z < 0 ? 18 : 14, "CAUSEWAY APPROACH");
        add(-780, -240, -600, -240, 18, "AIRPORT MAIN STREET");
        for (const auto& loop : street_loops()) for (std::size_t i = 0; i + 1 < loop.corners.size() + std::size_t(loop.closed); ++i) {
            const Vec3 a = loop.corners[i], b = loop.corners[(i + 1) % loop.corners.size()];
            bool duplicate = level_bridge_covers(a, b, loop.width);
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
        for (float z : {-400.0f, -280.0f, -40.0f, 60.0f})
            add(-1210, z, airports[0].center_x - std::max(airports[0].grounds_half_width, 115.f) - 20, z, 7, "AIRPORT SERVICE STREET");
        add(60, -180, 60, 180, 7, "MARKET LANE");
        add(180, 0, 180, 240, 7, "BAYFRONT LANE");
        const auto airport_access = airports[0].access_points();
        for (std::size_t i = 1; i < airport_access.size(); ++i)
            add(airport_access[i - 1].x, airport_access[i - 1].z, airport_access[i].x, airport_access[i].z, airports[0].gate_width(), "AIRPORT INTERNAL ACCESS");
        add(airports[1].apron_x(), airports[1].apron_z(), keys().back().center.GetX(), keys().back().center.GetZ(),
            7.5f, "KEY WEST AIRPORT ACCESS");
        for (float x : {1140.0f, 1260.0f, 1380.0f}) for (float z : {-360.0f, -120.0f, 120.0f, 360.0f})
            add(x, z, x + (x == 1380 ? 90 : 120), z, 7, "BEACH NEIGHBORHOOD LANE");
        for (float x : {1200.0f, 1320.0f, 1425.0f}) add(x, -480, x, 480, 7, "BEACH STUDIO LANE");
        add(570, 240, 750, 240, 12, "PORTMIAMI ROAD");
        add(660, 240, 660, 340, 8, "CRUISE PORT ACCESS");
        for (const auto& key : keys()) {
            if (std::string_view(key.name) == "NGAWISH") result.push_back({key.center, key.dock(), 7.5f, "MARINA LANE"});
            else result.push_back({key.center, key.local(50, 0), 6.5f, "KEYS NEIGHBORHOOD ACCESS"});
        }
        // Local streets meet the frontage lanes beside the rising flyover.
        for (auto& road : result) if (road.width < 24) {
            const float frontage = (road.a.GetX() + road.b.GetX()) / 2 < -360 ? -382 : -338;
            for (Vec3* end : {&road.a, &road.b})
                if (end->GetX() == -360 && (end->GetZ() < -440 || end->GetZ() > 360) && end->GetZ() <= 900)
                    end->SetX(frontage);
        }
        for (const auto& path : highways()) if (std::string_view(path.name) == "INTERSTATE CITY LOOP"
            || std::string_view(path.name) == "AIRPORT INTERCHANGE" || std::string_view(path.name) == "AIRPORT CROSS STREET"
            || std::string_view(path.name) == "US 1 SLOW LANES" || std::string_view(path.name) == "OVERSEAS HIGHWAY") {
            for (std::size_t i = 0; i + 1 < path.corners.size() + std::size_t(path.closed); ++i) {
                const Vec3 a = path.corners[i], b = path.corners[(i + 1) % path.corners.size()];
                if (std::string_view(path.name) == "OVERSEAS HIGHWAY" && (a.GetY() != 0 || b.GetY() != 0)) continue;
                if (a.GetY() <= road_level + .001f && b.GetY() <= road_level + .001f
                    && !level_bridge_covers(a, b, path.width)) result.push_back({a, b, path.width, path.name});
            }
        }
        // The southern fork rounds the turn onto the new cross street.
        for (const auto& path : highways()) if (std::string_view(path.name) == "AIRPORT INTERCHANGE") {
            for (std::size_t i = 1; i < path.corners.size(); ++i) {
                const Vec3 a = path.corners[i - 1], b = path.corners[i];
                if (a.GetZ() < 300 || b.GetZ() > 300) continue;
                const Vec3 start = a + (b - a) * ((300 - a.GetZ()) / (b.GetZ() - a.GetZ()));
                const Vec3 end(-820, 0, 240);
                Vec3 previous = start;
                for (int step = 1; step <= 12; ++step) {
                    const float t = float(step) / 12, u = 1 - t;
                    const Vec3 point = start * (u * u * u) + (start + Vec3(-8, 0, -35)) * (3 * u * u * t)
                        + (end - Vec3(40, 0, 0)) * (3 * u * t * t) + end * (t * t * t);
                    result.push_back({previous, point, 18, "AIRPORT CROSS STREET MERGE"});
                    previous = point;
                }
                break;
            }
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
    for (std::size_t i = 0; i < islands().size(); ++i) {
        const auto& island = islands()[i];
        float dx = (x - island.center.GetX()) / island.radius_x;
        float dz = (z - island.center.GetZ()) / island.radius_z;
        if (i >= 6) {
            const auto& key = keys()[i - 6];
            const Vec3 offset(x - key.center.GetX(), 0, z - key.center.GetZ());
            dx = offset.Dot(key.right()) / key.rx; dz = offset.Dot(key.forward()) / key.rz;
        }
        if (dx * dx + dz * dz > 9) continue;
        const float phase = float(i) * 1.73f;
        // Bent island spines and asymmetric lobes make coves, points and narrow waists.
        if (i == 0) { dx += .10f * std::sin(dz * 3); dz *= .88f; }
        else if (i == 1) dx += .20f * std::sin(dz * 3 + .4f);
        else if (i >= 6) { dx += .28f * std::sin(dz * 3 + phase); dz = dz * .90f + dx * .12f; }
        const float angle = std::atan2(dz, dx);
        const float outline = 1 + .15f * std::sin(3 * angle + phase)
            + .09f * std::sin(5 * angle - phase) + .035f * std::cos(8 * angle + phase);
        float shape = std::hypot(dx, dz) / outline;
        const auto inlet = [&](float cx, float cz, float rx, float rz) {
            const float u = (x - cx) / rx, v = (z - cz) / rz;
            const float edge = 1 + .13f * std::sin(3 * std::atan2(v, u) + phase);
            shape = std::max(shape, 1.24f - .34f * std::hypot(u + .18f * std::sin(v * 3 + phase), v) / edge);
        };
        if (i == 0) {
            inlet(-600, -1020, 260, 260);
            inlet(340, 80, 150, 210);
            inlet(260, -540, 140, 210);
            inlet(280, 650, 160, 180);
        } else if (i == 1) {
            inlet(1090, -650, 120, 170);
            inlet(1070, 100, 95, 170);
            inlet(1210, 760, 130, 160);
        } else if (i >= 6) {
            inlet(island.center.GetX() - island.radius_x * .78f, island.center.GetZ() + island.radius_z * .20f,
                island.radius_x * .28f, island.radius_z * .42f);
            inlet(island.center.GetX() + island.radius_x * .45f, island.center.GetZ() + island.radius_z * .68f,
                island.radius_x * .25f, island.radius_z * .24f);
            // Keep a narrow, level bank around the island highway and its bridge approaches.
            const auto& key = keys()[i - 6];
            shape = std::min(shape, std::min(distance_to(key.entry, key.center, x, z), distance_to(key.center, key.exit, x, z)) / 45);
        }
        radius = std::min(radius, shape);
    }
    // Extend the western inlet into a winding river that opens onto the south coast.
    radius = std::max(radius, 1.30f - .40f * river_radius(x, z));
    radius = std::max(radius, northern_bay_radius(x, z));
    radius = std::max(radius, southern_inlets_radius(x, z));
    // Keep PortMiami and the Keys separated where their shorelines overlap.
    for (int index : {0, 3, 7}) {
        const auto& bridge = bridges()[index];
        const Vec3 direction = Vec3(bridge.b.GetX() - bridge.a.GetX(), 0,
            bridge.b.GetZ() - bridge.a.GetZ()).Normalized();
        const Vec3 offset = Vec3(x, 0, z) - (bridge.a + bridge.b) / 2;
        const float across = offset.Dot(direction.Cross(Vec3::sAxisY()));
        const float bend = 12 * std::sin(across / 85) + 8 * std::sin(across / 180);
        const float channel = (1 - smooth(30, 65, std::abs(offset.Dot(direction) - bend)))
            * (1 - smooth(450, 650, std::abs(across)));
        if (channel > 0) radius = std::max(radius, .90f + .40f * channel);
    }
    return radius;
}
bool Environment::road(float x, float z) {
    for (const auto& airport : airports) if (airport.pavement(x, z)) return true;
    for (const auto& street : roads())
        if (street.bridge >= 0 && bridges()[street.bridge].has_curve()) {
            if (std::isfinite(deck_height(bridges()[street.bridge], x, z))) return true;
        } else if (distance_to(street.a, street.b, x, z) <= street.width / 2) return true;
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
    if (radius < 1.6f) {
        float verge = 10000;
        for (const auto& street : roads()) if (street.bridge < 0 || bridges()[street.bridge].clearance <= Airport::elevation)
            verge = std::min(verge, distance_to(street.a, street.b, x, z) - street.width / 2);
        // Flatten streets and level bridge approaches without raising ground under overpasses.
        ground += (std::max(ground, road_level) - ground) * (1 - smooth(45, 85, verge));
        float relief = (1 + std::sin(x * .013f) * std::cos(z * .011f)) * .8f
            * (1 - smooth(.65f, .92f, radius)) * smooth(45, 120, verge);
        for (const auto& key : keys()) {
            const Vec3 dock = key.dock();
            if (distance_to(dock, dock + Vec3(220, 0, 0), x, z) < spacing * 2) relief = 0;
        }
        ground += relief;
    }
    for (const auto& airport : airports) {
        const float padding = airport.international ? 25 : 5;
        const float airport_blend = (1 - smooth(airport.grounds_half_width + padding, airport.grounds_half_width + (airport.international ? 90 : 25),
            std::abs(x - airport.center_x))) * (1 - smooth(airport.grounds_half_length() + padding,
            airport.grounds_half_length() + 65, std::abs(z - airport.runway_z)));
        const float parking_distance = std::max(std::abs(x - airport.center_x + 76.5f) - 38.5f,
            std::abs(z - airport.runway_z + 30) - 190);
        ground += (Airport::elevation - ground) * std::max(airport_blend, 1 - smooth(5, 45, parking_distance));
    }
    // Keep the terrain below level decks, including the airport's higher verge.
    for (const auto& bridge : bridges()) if (bridge.clearance <= Airport::elevation
        && distance_to(bridge.a, bridge.b, x, z) < bridge.width / 2 + spacing)
        ground = std::min(ground, bridge.point(.5f).GetY());
    // Nearby road and airport verges must stop at the bank instead of filling the water.
    float water = std::max({1.30f - .40f * river_radius(x, z), northern_bay_radius(x, z), southern_inlets_radius(x, z)});
    bool airfield = false;
    for (const auto& airport : airports) airfield |= airport.contains(x, z);
    if (z > 1200 && !airfield) water = std::max(water, radius); // Keep the narrow shore instead of reclaiming it with road verges.
    if (water > .90f) ground = std::min(ground, road_level - (road_level + 9) * smooth(.90f, 1.12f, water));
    return ground;
}
Surface Environment::surface(float x, float z, float y) {
    if (y < -.1f) return Surface::Seabed;
    if (road(x, z)) return Surface::Road;
    return Surface::Soil;
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
    const auto add_wall = [&](Vec3 a, Vec3 b) {
        const Vec3 delta = b - a;
        barriers_.push_back({(a + b) / 2 + Vec3(0, .65f, 0), Vec3(.45f, 1.3f, delta.Length() + .1f),
            std::atan2(-delta.GetX(), -delta.GetZ()),
            std::atan2(delta.GetY(), std::hypot(delta.GetX(), delta.GetZ()))});
    };
    for (const auto& bridge : bridges()) {
        const Vec3 direction = (bridge.b - bridge.a).Normalized();
        const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
        const std::uint32_t first = std::uint32_t(vertices_.size());
        for (int row = 0; row <= rows; ++row) {
            const Vec3 center = bridge.point(float(row) / rows);
            const Vec3 side = bridge.side(float(row) / rows);
            vertices_.push_back(center - side * (bridge.width / 2));
            vertices_.push_back(center + side * (bridge.width / 2));
        }
        for (int row = 0; row < rows; ++row) {
            const auto a = first + row * 2;
            triangle(a, a + 1, a + 2, true); triangle(a + 1, a + 3, a + 2, true);
            const Vec3 center = bridge.point((row + .5f) / rows);
            // Ground approaches leave room to turn; elevated walls reach their joins.
            const float along = (row + .5f) / rows * (bridge.b - bridge.a).Length();
            if (bridge.a.GetY() <= 0
                && ((bridge.open_a && along < 20) || (bridge.open_b && along > (bridge.b - bridge.a).Length() - 20))) continue;
            const Vec3 side = bridge.side((row + .5f) / rows);
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 edge = center + side * (sign * (bridge.width / 2 - .3f));
                const Vec3 outside = edge + side.Normalized() * (sign * .8f);
                bool junction = false;
                for (const auto& road : roads()) {
                    if (road.bridge == int(&bridge - bridges().data())) continue;
                    const Vec3 delta = road.b - road.a;
                    if (std::string_view(bridge.name) == road.name
                        && (std::abs(direction.Dot(delta.Normalized())) > .97f
                            || (bridge.b - road.a).LengthSq() < .0001f || (bridge.a - road.b).LengthSq() < .0001f)) continue;
                    // Only open a wall where pavement actually continues beyond it.
                    float level;
                    if (road.bridge >= 0) level = deck_height(bridges()[road.bridge], outside.GetX(), outside.GetZ());
                    else {
                        const float t = segment_fraction(road.a, road.b, outside.GetX(), outside.GetZ());
                        if (t < 0 || t > 1 || distance_to(road.a, road.b, outside.GetX(), outside.GetZ()) > road.width / 2) continue;
                        level = ground_height(outside.GetX(), outside.GetZ());
                    }
                    if (std::abs(level - center.GetY()) < .4f) { junction = true; break; }
                }
                if (!junction) {
                    const float start = float(row) / rows, end = float(row + 1) / rows;
                    const Vec3 a = bridge.point(start) + bridge.side(start) * (sign * (bridge.width / 2 - .3f));
                    const Vec3 b = bridge.point(end) + bridge.side(end) * (sign * (bridge.width / 2 - .3f));
                    add_wall(a, b);
                }
            }
        }
        // Close the shoulder where an elevated road narrows into its next deck.
        for (const auto& next : bridges())
            if (bridge.a.GetY() > 0 && next.a.GetY() > 0 && bridge.width != next.width
                && (bridge.b - next.a).LengthSq() < .0001f)
                for (float sign : {-1.0f, 1.0f})
                    add_wall(bridge.b + bridge.side(1) * (sign * (bridge.width / 2 - .3f)),
                        next.a + next.side(0) * (sign * (next.width / 2 - .3f)));
    }
    // Short planted dividers leave every local cross street open.
    for (float x : {-376.0f, -344.0f}) for (float z = -590; z < 890; z += 20) {
        bool crossing = false;
        for (const auto& street : roads()) {
            if (distance_to(street.a, street.b, x, z) < street.width / 2 + 2) { crossing = true; break; }
            const Vec3 delta = street.b - street.a;
            if (std::abs(delta.GetX()) < std::abs(delta.GetZ()) * .2f) continue;
            if (distance_to(street.a, street.b, x, z) < street.width / 2 + 12
                && std::abs(road_height(street, x, z) - ground_height(x, z)) < .5f) { crossing = true; break; }
        }
        if (crossing) continue;
        const float base = ground_height(x, z);
        barriers_.push_back({Vec3(x, base + .3f, z), Vec3(4, .6f, 14), 0});
        trees_.push_back({Vec3(x, base, z), 7, 0});
    }
    std::mt19937 random(4317);
    const auto add_building = [&](float x, float z, Vec3 size, BuildingKind kind, bool east = false, bool positive = true) {
        // Keep the mainland's entire seaward side of the interstate open.
        if (x > -360 && x < 420) for (float edge_z : {z - size.GetZ() / 2, z, z + size.GetZ() / 2}) {
            for (const auto& highway : highways()) if (std::string_view(highway.name) == "INTERSTATE CITY LOOP") {
                float shoreline_edge = -10000;
                for (std::size_t i = 0; i < highway.corners.size(); ++i) {
                    const Vec3 a = highway.corners[i], b = highway.corners[(i + 1) % highway.corners.size()];
                    if (std::abs(b.GetZ() - a.GetZ()) < .001f || edge_z < std::min(a.GetZ(), b.GetZ())
                        || edge_z > std::max(a.GetZ(), b.GetZ())) continue;
                    const float t = (edge_z - a.GetZ()) / (b.GetZ() - a.GetZ());
                    shoreline_edge = std::max(shoreline_edge, a.GetX() + (b.GetX() - a.GetX()) * t);
                }
                if (shoreline_edge > -10000 && x + size.GetX() / 2 > shoreline_edge - highway.width / 2) return false;
            }
        }
        const bool airport_structure = kind == BuildingKind::Terminal || kind == BuildingKind::Hangar || kind == BuildingKind::ControlTower;
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
                if (road(x + sx, z + sz) || ground_height(x + sx, z + sz) < 2.9f
                    || std::abs(ground_height(x + sx, z + sz) - ground_height(x, z)) > .35f) return false;
        // Leave the seaward side of coastal streets open, including second-row lots.
        Vec3 frontage = Vec3::sZero();
        float nearest = 10000;
        for (const auto& street : roads()) {
            if ((street.bridge >= 0 && bridges()[street.bridge].a.GetY() > 0)
                || std::string_view(street.name) == "INTERSTATE CITY LOOP") continue;
            const float t = std::clamp(segment_fraction(street.a, street.b, x, z), 0.0f, 1.0f);
            const Vec3 p = street.a + (street.b - street.a) * t;
            const float distance = std::hypot(x - p.GetX(), z - p.GetZ());
            if (distance < nearest) { nearest = distance; frontage = p; }
        }
        const Vec3 outward = Vec3(x - frontage.GetX(), 0, z - frontage.GetZ()).NormalizedOr(Vec3::sAxisX());
        // ponytail: nearby shores only (220 m); extend the probe for wider waterfront setbacks.
        for (float distance = nearest; distance <= 220 && !airport_structure && kind != BuildingKind::GasStation; distance += 5) {
            const Vec3 p = frontage + outward * distance;
            bool inland = false;
            for (const auto& street : roads()) {
                if ((street.bridge >= 0 && bridges()[street.bridge].a.GetY() > 0)
                    || std::string_view(street.name) == "INTERSTATE CITY LOOP") continue;
                if (distance_to(street.a, street.b, p.GetX(), p.GetZ()) <= street.width / 2) { inland = true; break; }
            }
            if (inland) break; // A ground street makes this an inland block.
            if (coast_radius(p.GetX(), p.GetZ()) >= 1) return false;
        }
        buildings_.push_back({Vec3(x, ground_height(x, z) + size.GetY() / 2, z), size, int(random() % 5), kind, east, positive});
        return true;
    };
    add_building(32, -120, Vec3(38, 9, 30), BuildingKind::Mall);
    add_building(148, 120, Vec3(38, 11, 30), BuildingKind::Mall);
    add_building(-60, -150, Vec3(24, 96, 24), BuildingKind::Tower);
    ports_.push_back({Vec3(660, road_level, 345), false, "PORTMIAMI"});
    for (float x : {606.0f, 714.0f}) add_building(x, 300, Vec3(24, 8, 22), BuildingKind::Warehouse);
    for (std::size_t i = 0; i < keys().size(); ++i) {
        const auto& key = keys()[i];
        const float x = key.center.GetX(), z = key.center.GetZ();
        if (i == 0) {
            add_building(x - 36, z - 40, Vec3(32, 4, 28), BuildingKind::GasStation, true);
        } else {
            const Vec3 right = key.right();
            const bool east = std::abs(right.GetX()) > std::abs(right.GetZ());
            for (float across : {65.0f, 80.0f}) for (float along : {-55.0f, -20.0f, 15.0f, 50.0f}) {
                const Vec3 p = key.local(across, along);
                const float facing = (across < 70 ? -1 : 1) * (east ? right.GetX() : right.GetZ());
                add_building(p.GetX(), p.GetZ(), Vec3(8, 3.3f, 9), BuildingKind::House, east, facing > 0);
            }
        }
        Vec3 dock = key.dock(); dock.SetY(height(dock.GetX(), dock.GetZ()));
        ports_.push_back({dock, true, key.name,
            i == 0 ? 60.0f : 220.0f, i == 0 ? 5.0f : 8.0f});
    }
    const auto& miami = airports[0];
    add_building(miami.gate_x() + 40, miami.gate_z(), Vec3(42, 9, 30), BuildingKind::Terminal, true, false);
    add_building(miami.center_x - 48, miami.apron_z() + 80, Vec3(28, 9, 24), BuildingKind::Hangar, true, true);
    add_building(miami.center_x - 48, miami.apron_z() - 80, Vec3(7, 18, 7), BuildingKind::ControlTower);
    add_building(airports[1].center_x - 65, airports[1].runway_z - airports[1].departure * 280,
        Vec3(16, 4, 12), BuildingKind::Terminal, true, true);
    // Frontage lots fill both sides of streets; a second row fills deep blocks.
    // ponytail: cardinal footprints on angled streets; rotate lots if precise parcel geometry is needed.
    for (const auto& street : roads()) {
        if (street.bridge >= 0 || street.a.GetZ() > 1080 || street.b.GetZ() > 1080) continue;
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
            if (key_index == 0) continue;
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
        if (coast_radius(x, z) > .88f || ground_height(x, z) < 2.9f || road(x, z)) return;
        for (const auto& airport : airports) if (airport.contains(x, z) || airport.flight_path(x, z)) return;
        for (const auto& street : roads()) if (distance_to(street.a, street.b, x, z) < street.width / 2 + (z > 1740 ? 3 : 2)) return;
        for (const auto& b : buildings_) {
            const float clearance = b.kind == BuildingKind::Cafe ? 3.0f : .8f;
            if (std::abs(x - b.center.GetX()) < b.size.GetX() / 2 + clearance
                && std::abs(z - b.center.GetZ()) < b.size.GetZ() / 2 + clearance) return;
        }
        for (const auto& tree : trees_) if (std::hypot(x - tree.base.GetX(), z - tree.base.GetZ()) < 6) return;
        trees_.push_back({Vec3(x, ground_height(x, z), z), 5.5f + random() % 40 * .1f,
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
    for (int cluster = 0; cluster < 160; ++cluster) {
        const auto& island = islands()[cluster % 4 < 2 ? cluster % 4 : 6 + cluster % 5];
        const float x = island.center.GetX() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_x;
        const float z = island.center.GetZ() + (int(random() % 2001) - 1000) / 1000.0f * island.radius_z;
        const float radius = 55 + random() % 40;
        for (int i = 0; i < 80; ++i) {
            const float angle = random() % 6283 * .001f;
            const float distance = radius * std::sqrt(random() % 1000 / 1000.0f);
            add_tree(x + std::cos(angle) * distance, z + std::sin(angle) * distance);
        }
    }
    for (std::size_t i = 1; i < keys().size(); ++i)
        for (float across = -135; across <= 135; across += 10) for (float along = -260; along <= 260; along += 10) {
            const Vec3 p = keys()[i].local(across + int(random() % 7) - 3, along + int(random() % 7) - 3);
            add_tree(p.GetX(), p.GetZ());
        }
}
float Environment::terrain_height(float x, float z) const {
    if (city_) {
        if (city_->land(City::cell(x,z))) return city_->height(x,z);
        if (city_heights_.empty() || std::abs(x)>=city_mesh_extent || std::abs(z)>=city_mesh_extent) return -8;
        const float gx = (x+city_mesh_extent)/city_mesh_spacing, gz = (z+city_mesh_extent)/city_mesh_spacing;
        const int ix = std::min(int(gx),city_mesh_width-2), iz = std::min(int(gz),city_mesh_width-2);
        const float u = gx-ix, v = gz-iz;
        const int a = iz*city_mesh_width+ix;
        const float ha = city_heights_[a], hb = city_heights_[a+city_mesh_width];
        const float hc = city_heights_[a+city_mesh_width+1], hd = city_heights_[a+1];
        return v>=u ? ha+v*(hb-ha)+u*(hc-hb) : ha+u*(hd-ha)+v*(hc-hd);
    }
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
    if (city_) {
        const auto cell = City::cell(x,z);
        if (city_->tile(cell)!=CityTile::Bridge) return terrain_height(x,z);
        const int owner = city_diagonal_bridge_owner_[City::index(cell)];
        if (owner<0) return city_->height(x,z);
        const auto& deck = city_diagonal_bridge_decks_[owner];
        for (std::size_t i = 0; i < deck.size(); ++i) {
            const Vec3 a = deck[i], b = deck[(i+1)%deck.size()];
            if ((b.GetZ()-a.GetZ())*(x-a.GetX())-(b.GetX()-a.GetX())*(z-a.GetZ())<-.0001f)
                return terrain_height(x,z);
        }
        return city_->height(x,z);
    }
    float ground = terrain_height(x, z);
    for (const auto& bridge : bridges()) ground = std::max(ground, deck_height(bridge, x, z));
    return ground;
}
float Environment::ground_height(float x, float z) const {
    if (city_) return height(x,z);
    float ground = terrain_height(x, z);
    for (const auto& bridge : bridges()) if (bridge.a.GetY() <= 0 || bridge.clearance <= Airport::elevation)
        ground = std::max(ground, deck_height(bridge, x, z));
    return ground;
}
float Environment::road_height(const Road& road, float x, float z) const {
    if (city_) return height(x,z);
    float level = ground_height(x, z);
    if (road.bridge >= 0) {
        const auto& bridge = bridges()[std::size_t(road.bridge)];
        const float sampled = deck_height(bridge, x, z);
        level = std::isfinite(sampled) ? sampled : bridge.point(std::clamp(segment_fraction(bridge.a, bridge.b, x, z), 0.0f, 1.0f)).GetY();
    }
    float top = level;
    // At a merge the overlapping pavement is one layer; distant floors stay separate.
    for (const auto& bridge : bridges()) {
        const float other = deck_height(bridge, x, z);
        if (std::abs(other - level) < .4f) top = std::max(top, other);
    }
    return top;
}
float Environment::surface_height(Vec3 reference) const {
    if (city_) {
        const float ground = terrain_height(reference.GetX(),reference.GetZ());
        const float top = height(reference.GetX(),reference.GetZ());
        return reference.GetY()>=top-.6f ? top : ground;
    }
    float best = ground_height(reference.GetX(), reference.GetZ());
    float difference = std::abs(reference.GetY() - best);
    for (const auto& bridge : bridges()) {
        const float surface = deck_height(bridge, reference.GetX(), reference.GetZ());
        const float distance = std::abs(reference.GetY() - surface);
        if (distance < difference) { best = surface; difference = distance; }
    }
    return best;
}
bool Environment::submerged(const Vec3& point) const {
    return point.GetY() < water_level - .6f || std::abs(point.GetX()) > extent - 40 || std::abs(point.GetZ()) > extent - 40;
}
} // namespace forza
