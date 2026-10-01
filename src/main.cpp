#include "ui_font.hpp"
#include "vehicle.hpp"
#include "environment.hpp"
#include "environment_renderer.hpp"
#include "car_renderer.hpp"
#include "tuning_panel.hpp"
#include "menu_bar.hpp"
#include "player.hpp"
#include "third_person_camera.hpp"
#include "airport.hpp"
#include "traffic.hpp"
#include <string>
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {
Vector3 render_vector(const forza::Vec3& value) {
    return {float(value.GetX()), float(value.GetY()), float(value.GetZ())};
}
Vector3 lerp(Vector3 a, Vector3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

struct SkidMark { forza::Vec3 from, to; };
struct Scene {
    const forza::Environment& environment;
    forza::PhysicsWorld world;
    forza::Car car{world};
    forza::Plane plane{world};
    forza::Traffic traffic{world, environment};
    forza::Player player{world, car, environment, &plane, &traffic};
    std::vector<SkidMark> marks;
    std::size_t next_mark = 0;
    std::array<forza::Vec3, 4> last_skid{};
    std::array<bool, 4> last_valid{};
    const forza::Car* skid_car = nullptr;
    static constexpr std::size_t max_marks = 2400;

    explicit Scene(const forza::Environment& map) : environment(map), world(map) {
        marks.reserve(max_marks);
        reset();
    }
    void reset() {
        if (player.flying()) player.recover_plane();
        else player.reset();
        marks.clear();
        next_mark = 0;
        last_valid.fill(false);
    }

    void record_skids() {
        const auto& current = player.car();
        if (skid_car != &current) { last_valid.fill(false); skid_car = &current; }
        for (std::size_t i = 2; i < 4; ++i) {
            const auto& wheel = current.wheels()[i];
            if (!wheel.grounded || !wheel.skidding) {
                last_valid[i] = false;
                continue;
            }
            const forza::Vec3 point = wheel.ground_point + wheel.ground_normal * 0.09f;
            if (!last_valid[i]) {
                last_skid[i] = point;
                last_valid[i] = true;
                continue;
            }
            const float distance = (point - last_skid[i]).Length();
            if (distance < float(0.12)) continue;
            if (distance < float(1.5)) {
                const SkidMark mark{last_skid[i], point};
                if (marks.size() < max_marks) marks.push_back(mark);
                else {
                    marks[next_mark] = mark;
                    next_mark = (next_mark + 1) % max_marks;
                }
            }
            last_skid[i] = point;
        }
    }
};

void draw_skid_marks(const std::vector<SkidMark>& marks) {
    constexpr Color tint{26, 28, 31, 185};
    for (const auto& mark : marks) {
        const forza::Vec3 direction = mark.to - mark.from;
        const float length = std::hypot(direction.GetX(), direction.GetZ());
        if (length < float(0.001)) continue;
        const forza::Vec3 side(-direction.GetZ() / length * float(0.105), 0,
                             direction.GetX() / length * float(0.105));
        const Vector3 a = render_vector(mark.from - side);
        const Vector3 b = render_vector(mark.from + side);
        const Vector3 c = render_vector(mark.to + side);
        const Vector3 d = render_vector(mark.to - side);
        DrawTriangle3D(a, b, c, tint);
        DrawTriangle3D(a, c, d, tint);
    }
}

void draw_box(const forza::Vec3& center, const forza::Quat& rotation,
              const forza::Vec3& size, Color tint, bool outline = true) {
    const auto half = size * float(0.5);
    const std::array<forza::Vec3, 8> local = {
        forza::Vec3(-half.GetX(), -half.GetY(), -half.GetZ()), forza::Vec3(half.GetX(), -half.GetY(), -half.GetZ()),
        forza::Vec3(half.GetX(), half.GetY(), -half.GetZ()), forza::Vec3(-half.GetX(), half.GetY(), -half.GetZ()),
        forza::Vec3(-half.GetX(), -half.GetY(), half.GetZ()), forza::Vec3(half.GetX(), -half.GetY(), half.GetZ()),
        forza::Vec3(half.GetX(), half.GetY(), half.GetZ()), forza::Vec3(-half.GetX(), half.GetY(), half.GetZ())};
    std::array<Vector3, 8> p{};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = render_vector(center + rotation * local[i]);
    constexpr int faces[6][4] = {{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0}};
    for (const auto& face : faces) {
        DrawTriangle3D(p[face[2]], p[face[1]], p[face[0]], tint);
        DrawTriangle3D(p[face[3]], p[face[2]], p[face[0]], tint);
    }
    constexpr int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    if (outline) for (const auto& edge : edges) DrawLine3D(p[edge[0]], p[edge[1]], MAROON);
}

Color traffic_paint(std::size_t index) {
    constexpr Color colors[] = {{193, 65, 53, 255}, {64, 127, 161, 255}, {219, 174, 64, 255},
        {83, 139, 100, 255}, {176, 183, 195, 255}, {149, 93, 157, 255}};
    return colors[index % std::size(colors)];
}

void draw_car(const forza::Car& car, const forza::CarRenderer& renderer, const Camera3D& camera,
              Color paint = {235, 235, 224, 255}) {
    if (!renderer.draw_body(car, camera, paint)) {
        const auto basis = car.rotation();
        draw_box(car.position() + car.rotate(forza::Vec3(0, forza::chassis_offset, 0)), basis,
                 forza::Vec3(float(1.85), float(0.5), float(3.7)), paint);
        draw_box(car.position() + car.rotate(forza::Vec3(0, float(0.93), float(0.25))), basis,
                 forza::Vec3(float(1.45), float(0.55), float(1.7)), {39, 58, 73, 255});
    }
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        const auto center = car.wheel_center(wheel);
        DrawLine3D(render_vector(mount), render_vector(center), LIGHTGRAY);
        if (renderer.draw_wheel(car, wheel, camera)) continue;
        auto axis = car.rotate(forza::Vec3(1, 0, 0));
        if (wheel.front) axis = forza::Quat::sRotation(car.rotate(forza::Vec3(0, 1, 0)), car.steering()) * axis;
        const float radius = car.tuning().wheel_radius, size = radius / forza::wheel_radius;
        DrawCylinderEx(render_vector(center - axis * (0.13f * size)),
                       render_vector(center + axis * (0.13f * size)), radius, radius, 16, BLACK);
        DrawCylinderEx(render_vector(center - axis * (0.14f * size)),
                       render_vector(center + axis * (0.14f * size)), radius * .5f, radius * .5f, 16, DARKGRAY);
    }
}

void draw_character(const forza::Character& character, const forza::Environment& environment) {
    const auto basis = forza::Quat::sRotation(forza::Vec3::sAxisY(), character.yaw());
    const auto point = [&](float x, float y, float z) { return character.position() + basis * forza::Vec3(x, y, z); };
    const auto avatar_box = [&](forza::Vec3 center, const forza::Quat& rotation, forza::Vec3 size, Color color) {
        draw_box(center, rotation, size, color, false);
    };
    const auto feet = character.position();
    const float ground = environment.height(feet.GetX(), feet.GetZ());
    const float radius = std::clamp(0.33f - (feet.GetY() - ground) * 0.08f, 0.18f, 0.33f);
    const auto shadow_point = [&](float x, float z) { return Vector3{x, environment.height(x, z) + 0.085f, z}; };
    for (int i = 0; i < 24; ++i) {
        const float a = i * 6.2831853f / 24, b = (i + 1) * 6.2831853f / 24;
        DrawTriangle3D(shadow_point(feet.GetX(), feet.GetZ()),
            shadow_point(feet.GetX() + std::cos(b) * radius, feet.GetZ() + std::sin(b) * radius),
            shadow_point(feet.GetX() + std::cos(a) * radius, feet.GetZ() + std::sin(a) * radius), {15, 23, 29, 100});
    }
    const auto limb = [&](forza::Vec3 a, forza::Vec3 b, float radius, Color color) {
        DrawCylinderEx(render_vector(a), render_vector(b), radius, radius, 8, color);
        DrawSphereEx(render_vector(b), radius, 6, 8, color);
    };
    const float speed = std::hypot(character.velocity().GetX(), character.velocity().GetZ());
    const float swing = character.grounded() ? std::sin(character.gait()) * std::min(1.0f, speed / 3.2f) : 0.15f;
    const float bob = character.grounded() ? std::abs(std::sin(character.gait())) * std::min(speed * 0.006f, 0.035f) : 0;
    constexpr Color skin{211, 155, 113, 255}, shirt{38, 97, 133, 255}, pants{39, 48, 66, 255};
    avatar_box(point(0, 1.13f + bob, 0), basis, forza::Vec3(0.47f, 0.57f, 0.28f), shirt);
    avatar_box(point(0, 0.80f + bob, 0), basis, forza::Vec3(0.38f, 0.16f, 0.27f), pants);
    avatar_box(point(0, 1.33f + bob, -0.15f), basis, forza::Vec3(0.23f, 0.055f, 0.018f), {115, 198, 200, 255});
    DrawSphereEx(render_vector(point(0, 1.61f + bob, 0)), 0.17f, 10, 12, skin);
    avatar_box(point(0, 1.75f + bob, 0.02f), basis, forza::Vec3(0.28f, 0.08f, 0.26f), {44, 35, 31, 255});
    DrawSphereEx(render_vector(point(0, 1.59f + bob, -0.16f)), 0.045f, 6, 8, skin);
    for (float side : {-1.0f, 1.0f}) {
        const float phase = swing * side;
        const auto shoulder = point(side * 0.27f, 1.34f + bob, 0);
        const auto elbow = point(side * 0.31f, 1.10f + bob, phase * 0.13f);
        const auto hand = point(side * 0.30f, 0.92f + bob, phase * 0.27f - 0.07f);
        limb(shoulder, elbow, 0.075f, shirt); limb(elbow, hand, 0.06f, skin);
        const auto hip = point(side * 0.12f, 0.76f + bob, 0);
        const auto knee = point(side * 0.13f, 0.42f, -phase * 0.13f);
        const float lift = character.grounded() ? std::max(0.0f, phase) * 0.09f : 0.09f;
        const auto ankle = point(side * 0.13f, 0.12f + lift, -phase * 0.25f);
        limb(hip, knee, 0.09f, pants); limb(knee, ankle, 0.075f, pants);
        avatar_box(ankle + basis * forza::Vec3(0, -0.055f, -0.065f), basis,
            forza::Vec3(0.17f, 0.12f, 0.32f), {214, 221, 222, 255});
    }
}

void draw_plane(const forza::Plane& plane, bool occupied) {
    const auto basis = plane.rotation();
    const auto point = [&](float x, float y, float z) { return plane.position() + plane.rotate(forza::Vec3(x, y, z)); };
    const auto box = [&](forza::Vec3 center, forza::Vec3 size, Color color) {
        draw_box(plane.position() + plane.rotate(center), basis, size, color, false);
    };
    const auto cylinder = [&](forza::Vec3 a, forza::Vec3 b, float r1, float r2, Color color) {
        DrawCylinderEx(render_vector(plane.position() + plane.rotate(a)),
            render_vector(plane.position() + plane.rotate(b)), r1, r2, 12, color);
    };
    constexpr Color paint{234, 236, 223, 255}, blue{39, 103, 152, 255}, glass{59, 112, 139, 255};
    cylinder(forza::Vec3(0, 0, -2.8f), forza::Vec3(0, 0, 1.1f), .42f, .48f, paint);
    cylinder(forza::Vec3(0, 0, 1.1f), forza::Vec3(0, .2f, 3.1f), .48f, .12f, paint);
    box(forza::Vec3(0, .18f, -.8f), forza::Vec3(.84f, .64f, 1.3f), glass);
    box(forza::Vec3(0, .52f, -.8f), forza::Vec3(.88f, .06f, 1.4f), paint);
    for (float side : {-1.0f, 1.0f}) {
        box(forza::Vec3(side * .43f, .18f, -.8f), forza::Vec3(.04f, .65f, .045f), paint);
        box(forza::Vec3(side * .47f, -.18f, -.6f), forza::Vec3(.03f, .12f, 3.3f), blue);
        cylinder(forza::Vec3(side * .42f, -.3f, -.45f), forza::Vec3(side * 3.5f, .57f, -.3f), .035f, .035f, LIGHTGRAY);
    }
    const auto wing = [&](float span, float y, float z, float chord, Color color) {
        const std::array<forza::Vec3, 4> corners = {forza::Vec3(-span, y, z - chord * .36f),
            forza::Vec3(-span, y, z + chord * .4f), forza::Vec3(span, y, z + chord * .4f),
            forza::Vec3(span, y, z - chord * .36f)};
        for (float offset : {-.07f, .07f}) {
            std::array<Vector3, 4> p{};
            for (int i = 0; i < 4; ++i) p[i] = render_vector(plane.position() + basis * (corners[i] + forza::Vec3(0, offset, 0)));
            DrawTriangle3D(p[0], p[1], p[2], color); DrawTriangle3D(p[0], p[2], p[3], color);
            DrawTriangle3D(p[2], p[1], p[0], color); DrawTriangle3D(p[3], p[2], p[0], color);
        }
        box(forza::Vec3(0, y, z - chord * .36f), forza::Vec3(span * 2, .14f, .1f), color);
        box(forza::Vec3(0, y, z + chord * .4f), forza::Vec3(span * 2, .14f, .1f), color);
    };
    wing(5.5f, .6f, -.3f, 1.8f, paint);
    wing(1.8f, .35f, 2.5f, 1.2f, paint);
    for (float side : {-1.0f, 1.0f}) {
        box(forza::Vec3(side * 4.9f, .69f, -.26f), forza::Vec3(.7f, .015f, 1.3f), blue);
        DrawSphereEx(render_vector(point(side * 5.48f, .6f, -.65f)), .09f, 6, 8, side < 0 ? RED : GREEN);
    }
    box(forza::Vec3(0, .95f, 2.5f), forza::Vec3(.14f, 1.3f, 1.1f), blue);
    const auto prop = forza::Quat::sRotation(plane.forward(), plane.propeller_angle());
    const auto blade = prop * plane.rotate(forza::Vec3(0, 1.05f, 0));
    const auto hub = point(0, 0, -3.02f);
    DrawCylinderEx(render_vector(hub - blade), render_vector(hub + blade), .055f, .055f, 6, DARKGRAY);
    DrawSphereEx(render_vector(hub), .18f, 8, 10, blue);
    if (occupied) DrawSphereEx(render_vector(point(-.19f, .25f, -.75f)), .14f, 8, 10, {211, 155, 113, 255});
    for (const auto& wheel : plane.wheels()) {
        const auto mount = plane.position() + plane.rotate(wheel.mount);
        DrawCylinderEx(render_vector(mount), render_vector(wheel.center), .045f, .045f, 8, LIGHTGRAY);
        const auto axis = plane.rotate(forza::Vec3(.11f, 0, 0));
        DrawCylinderEx(render_vector(wheel.center - axis), render_vector(wheel.center + axis),
            forza::Plane::tire_radius, forza::Plane::tire_radius, 12, BLACK);
    }
}

void draw_hud(const Scene& scene, bool handbrake, bool captured, bool tuning, bool flaps) {
    const auto& player = scene.player;
    DrawRectangle(14, 46, 790, player.flying() ? 132 : 103, {19, 28, 35, 220});
    forza::ui::draw_text(tuning ? "TUNING MODE  |  Drag sliders to adjust your car" :
        player.flying() ? "FLIGHT MODE  |  Help > Controls for flight instructions" :
        player.driving() ? "DRIVING MODE  |  Help > Controls for driving instructions" : "ON FOOT  |  Help > Controls for movement instructions", 26, 56, 19, RAYWHITE);
    forza::ui::draw_text(tuning ? "Right drag outside panel: camera  |  Scroll: adjust / zoom" :
        "Settings > Controller mapping  |  F10 menu / Esc map", 26, 81, 17, LIGHTGRAY);
    if (tuning) forza::ui::draw_text("Physics live / parking brake / Driving input disabled", 26, 111, 19, GOLD);
    else if (player.flying()) {
        const auto& plane = scene.plane;
        const auto p = plane.position();
        const float ground = std::abs(p.GetX()) <= forza::Environment::extent && std::abs(p.GetZ()) <= forza::Environment::extent
            ? std::max(scene.environment.height(p.GetX(), p.GetZ()), 0.0f) : 0;
        forza::ui::draw_text(TextFormat("FLYING | %3.0f km/h | AGL %4.0f m | THROTTLE %3.0f%%", double(plane.airspeed() * 3.6f),
            double(std::max(0.0f, p.GetY() - ground)), double(plane.throttle() * 100)), 26, 111, 20, GOLD);
        forza::ui::draw_text(plane.damaged() ? "AIRCRAFT DAMAGED - use Recover vehicle" : plane.stalled() ? "STALL - lower nose and add throttle" :
            plane.grounded() ? "RUNWAY | Full throttle, pitch up gently above 100 km/h" : "Mouse look / Wheel zoom / Land and stop before exiting", 26, 140, 17, plane.stalled() || plane.damaged() ? ORANGE : LIGHTGRAY);
        if (flaps) forza::ui::draw_text("FLAPS", 712, 112, 17, SKYBLUE);
    } else if (player.driving()) {
        const float speed = player.car().velocity().Dot(player.car().forward()) * 3.6f;
        forza::ui::draw_text(TextFormat("DRIVING  |  %5.1f km/h", double(speed)), 26, 111, 20, GOLD);
        if (handbrake && std::abs(speed) > 15) forza::ui::draw_text("DRIFT", 340, 111, 20, ORANGE);
    } else {
        const auto nearby = player.entry_vehicle();
        forza::ui::draw_text(nearby == forza::EntryVehicle::Plane ? "Use Enter / exit vehicle to board the plane" :
            player.can_steal() ? "Use Enter / exit vehicle to steal this traffic car" :
            nearby == forza::EntryVehicle::Car ? "Use Enter / exit vehicle to enter this car" :
            "ON FOOT | Approach traffic to stop it, then enter to steal", 26, 111, 20, GOLD);
    }
    if (!captured) {
        const int panel_width = std::max(408, forza::ui::measure_text("Click to resume and capture mouse", 20) + 28);
        const int x = (GetScreenWidth() - panel_width) / 2 + 14, y = GetScreenHeight() / 2 - 36;
        DrawRectangle(x - 14, y - 12, panel_width, 87, {19, 28, 35, 235});
        forza::ui::draw_text("Click to resume and capture mouse", x, y, 20, RAYWHITE);
        forza::ui::draw_text("F10: menu / Start: resume / Esc: map", x, y + 32, 18, LIGHTGRAY);
    }
}
} // namespace

int main(int argc, char** argv) {
    forza::DayNight day_night;
    std::string screenshot;
    std::string start_district;
    bool performance_tuning = false;
    bool start_controllers = false, start_menu = false, start_help = false;
    bool map_open = false, start_region_map = false, start_on_foot = false, tuning_open = false, start_at_airport = false, start_in_plane = false, start_at_traffic = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--overview") { map_open = true; start_region_map = true; }
        if (arg == "--map") map_open = true;
        if (arg == "--beach" || arg == "--keys" || arg == "--key-west" || arg == "--bridge") start_district = arg;
        if (arg == "--on-foot") start_on_foot = true;
        if (arg == "--traffic") { start_at_traffic = true; start_on_foot = true; }
        if (arg == "--tuning") tuning_open = true;
        if (arg == "--performance") { tuning_open = true; performance_tuning = true; }
        if (arg == "--controllers") start_controllers = true;
        if (arg == "--menu") start_menu = true;
        if (arg == "--help-menu") start_help = true;
        if (arg == "--airport") start_at_airport = true;
        if (arg == "--plane") { start_at_airport = true; start_in_plane = true; }
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        if (arg == "--time") {
            if (i + 1 >= argc || !day_night.set_time(argv[++i])) {
                TraceLog(LOG_ERROR, "Time must use HH:MM (00:00 through 23:59)");
                return 1;
            }
        }
    }
    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!screenshot.empty()) window_flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(window_flags);
    InitWindow(1280, 720, "Forza Ambazon - Miami & Florida Keys");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1024, 600);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    InitAudioDevice();
    bool captured = screenshot.empty() && !map_open && !tuning_open && !start_controllers && !start_menu && !start_help;
    bool resume_capture = screenshot.empty() && !map_open;
    bool resume_map = map_open;
    if (tuning_open) map_open = false;
    if (captured) DisableCursor();
    {
        const forza::ui::FontResource ui_font;
        Music engine{};
        if (IsAudioDeviceReady()) {
            const std::string bundled = std::string(GetApplicationDirectory()) + "assets/sounds/car-engine.wav";
            engine = LoadMusicStream(FileExists(bundled.c_str()) ? bundled.c_str() : "assets/sounds/car-engine.wav");
        }
        const bool engine_ready = IsMusicValid(engine);
        float engine_pitch = .7f, engine_volume = 0;
        if (engine_ready) {
            engine.looping = true;
            SetMusicPitch(engine, engine_pitch);
            SetMusicVolume(engine, 0);
            PlayMusicStream(engine);
            TraceLog(LOG_INFO, "AUDIO: Car engine loop ready");
        } else TraceLog(LOG_WARNING, "AUDIO: Engine sound unavailable; continuing without it");
        const forza::Environment environment;
        forza::EnvironmentRenderer scenery(environment);
        forza::CarRenderer car_renderer;
        forza::TuningPanel tuning_panel;
        using forza::Action;
        using forza::MenuCommand;
        forza::ControllerMapping controls;
        forza::MenuBar menu;
        const auto mapping_path = std::filesystem::path(GetApplicationDirectory()) / "controller-mappings.ini";
        std::string mapping_error;
        if (std::filesystem::exists(mapping_path)) controls.load(mapping_path, mapping_error);
        else if (screenshot.empty()) controls.save(mapping_path, mapping_error);
        if (start_controllers) menu.show(MenuCommand::Controllers);
        else if (start_help) menu.show(MenuCommand::Controls);
        else if (start_menu) menu.open(2);
        if (performance_tuning) tuning_panel.select_tab(2);
        auto scene = std::make_unique<Scene>(environment);
        if (!start_district.empty()) {
            forza::Vec3 p = start_district == "--beach" ? forza::Vec3(1470, 0, 240) :
                start_district == "--keys" ? environment.islands()[6].center :
                start_district == "--key-west" ? environment.islands().back().center : environment.bridges()[6].point(.5f);
            p.SetY(environment.height(p.GetX(), p.GetZ()) + .56f);
            const auto heading = environment.bridges()[6].a - environment.bridges()[6].b;
            scene->car.reset(p, start_district == "--bridge" ? std::atan2(-heading.GetX(), -heading.GetZ()) : 0);
        }
        if (start_at_airport) {
            const auto& airport = forza::airports[start_district == "--key-west" ? 1 : 0];
            scene->plane.reset(forza::Vec3(airport.center_x, forza::Airport::elevation + forza::Plane::parked_height,
                airport.plane_z()), airport.yaw());
            scene->car.reset(forza::Vec3(airport.center_x + 8,
                environment.height(airport.center_x + 8, airport.plane_z()) + .56f, airport.plane_z()), airport.yaw());
        }
        if (start_in_plane) {
            scene->player.interact();
            const auto door = scene->plane.position() + scene->plane.rotate(forza::Vec3(-1.9f, 0, -1.8f));
            scene->player.character().reset(forza::Vec3(door.GetX(), environment.height(door.GetX(), door.GetZ()) + .08f, door.GetZ()));
            scene->player.interact();
        }
        if (start_on_foot && !scene->player.on_foot()) scene->player.interact();
        if (start_at_traffic) {
            const auto& car = *scene->traffic.cars().front().car;
            const auto door = car.position() + car.rotate(forza::Vec3(-2, 0, .35f));
            scene->player.character().reset(forza::Vec3(door.GetX(), environment.height(door.GetX(), door.GetZ()) + .08f, door.GetZ()));
        }
        forza::ThirdPersonCamera orbit;
        forza::ThirdPersonCamera saved_orbit = orbit;
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const auto focus = [&]() {
            if (!tuning_open) return scene->player.position() + forza::Vec3(0, scene->player.flying() ? .5f : scene->player.driving() ? .7f : 1.25f, 0);
            const auto center = scene->player.car().position() + forza::Vec3(0, .7f, 0);
            const float distance = (orbit.desired_position(center, true) - center).Length();
            // Shift the camera's aim to frame the car in the area beside the
            // 400-pixel panel. Scale with zoom and viewport height (60-deg FOV).
            const auto right = orbit.forward().Cross(forza::Vec3::sAxisY());
            return center + right * (distance * 400.0f / GetScreenHeight() * .57735027f);
        };
        const auto snap_camera = [&]() {
            const auto heading = tuning_open ? scene->player.car().forward() : scene->player.forward();
            if (tuning_open) orbit = forza::ThirdPersonCamera{};
            orbit.reset(std::atan2(-heading.GetX(), -heading.GetZ()));
            if (tuning_open) orbit.look(-230, 25, 5, true, heading, 0, 0);
            camera.target = render_vector(focus());
            camera.position = render_vector(orbit.desired_position(focus(), tuning_open || scene->player.driving(), !tuning_open && scene->player.flying()));
        };
        snap_camera();
        forza::WorldMapView world_map;
        float min_x = forza::Environment::extent, min_z = min_x, max_x = -min_x, max_z = -min_x;
        for (const auto& island : environment.islands()) {
            min_x = std::min(min_x, island.center.GetX() - island.radius_x);
            min_z = std::min(min_z, island.center.GetZ() - island.radius_z);
            max_x = std::max(max_x, island.center.GetX() + island.radius_x);
            max_z = std::max(max_z, island.center.GetZ() + island.radius_z);
        }
        const Rectangle map_region{min_x - 160, min_z - 160, max_x - min_x + 320, max_z - min_z + 320};
        const Rectangle initial_map_viewport{14, 124, float(GetScreenWidth() - 28), float(GetScreenHeight() - 202)};
        if (start_region_map) world_map.fit(initial_map_viewport, map_region, forza::Environment::extent);
        else world_map.focus(scene->player.position(), initial_map_viewport, forza::Environment::extent);
        double accumulator = 0;
        float notice_time = mapping_error.empty() ? 0 : 8;
        std::string notice = mapping_error;
        bool jump_pending = false, discard_mouse = true, orbit_dragging = false, map_dragging = false, flaps = false;
        int rendered_frames = 0;
        bool quit_requested = false;
        while (!quit_requested && !WindowShouldClose()) {
            const float elapsed = GetFrameTime(), frame = std::min(elapsed, 0.1f);
            const Rectangle map_viewport{14, 124, float(GetScreenWidth() - 28), float(GetScreenHeight() - 202)};
            const std::array<Rectangle, 5> map_buttons{{
                {float(GetScreenWidth() - 420), 82, 94, 30}, {float(GetScreenWidth() - 318), 82, 94, 30},
                {float(GetScreenWidth() - 216), 82, 42, 30}, {float(GetScreenWidth() - 166), 82, 42, 30},
                {float(GetScreenWidth() - 116), 82, 102, 30}}};
            if (map_open) world_map.constrain(map_viewport, forza::Environment::extent);
            bool mode_changed = false;
            const auto toggle_map = [&]() {
                map_open = !map_open;
                if (map_open) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                captured = !map_open && IsWindowFocused();
                if (captured) DisableCursor(); else EnableCursor();
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                map_dragging = false; discard_mouse = true; mode_changed = true;
                jump_pending = false; accumulator = 0;
            };
            const auto toggle_tuning = [&]() {
                mode_changed = true;
                tuning_panel.cancel_drag();
                orbit_dragging = false; map_dragging = false;
                jump_pending = false; accumulator = 0; discard_mouse = true;
                if (!tuning_open) {
                    resume_capture = captured;
                    resume_map = map_open;
                    saved_orbit = orbit;
                    tuning_open = true; map_open = false; captured = false;
                    EnableCursor(); snap_camera();
                } else {
                    tuning_open = false;
                    map_open = resume_map;
                    orbit = saved_orbit;
                    captured = resume_capture && IsWindowFocused() && !map_open;
                    if (captured) DisableCursor(); else EnableCursor();
                    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                }
            };
            if (screenshot.empty()) {
                const auto controller_input = forza::read_controllers();
                controls.update(controller_input);
                if (!IsWindowFocused()) {
                    if (captured) EnableCursor();
                    captured = false; resume_capture = false;
                    tuning_panel.cancel_drag(); orbit_dragging = false; map_dragging = false; discard_mouse = true;
                }
                const auto command = menu.update(controls, mapping_path, !captured, controller_input);
                if (menu.blocking() || menu.interacted()) {
                    mode_changed = true; jump_pending = false; discard_mouse = true;
                    tuning_panel.cancel_drag(); orbit_dragging = false; map_dragging = false;
                    if (captured) { captured = false; EnableCursor(); }
                }
                if (command == MenuCommand::Quit) quit_requested = true;
                if (command == MenuCommand::Resume) {
                    if (tuning_open) toggle_tuning();
                    map_open = false; captured = true; DisableCursor(); discard_mouse = true; map_dragging = false; SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                }
                if (command == MenuCommand::Pause) {
                    if (tuning_open) toggle_tuning();
                    captured = false; EnableCursor();
                }
                if (command == MenuCommand::Map && tuning_open) { toggle_tuning(); map_open = false; }
                if (command == MenuCommand::Recover) {
                    scene->reset(); snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (command == MenuCommand::CarDefaults) scene->player.car().set_tuning({});
                const bool shortcuts = IsWindowFocused() && !menu.blocking() && !menu.interacted();
                if (command == MenuCommand::Tuning || (shortcuts && controls.pressed(Action::Tuning))) {
                    if (scene->player.flying()) { notice = "Land and exit the plane before tuning the car"; notice_time = 3; }
                    else toggle_tuning();
                }
                else if (shortcuts && (controls.pressed(Action::Pause) || IsKeyPressed(KEY_ESCAPE))) {
                    if (tuning_open) toggle_tuning();
                    else if (IsKeyPressed(KEY_ESCAPE) || map_open) toggle_map();
                    else if (captured) { EnableCursor(); captured = false; mode_changed = true; }
                    else if (!IsKeyPressed(KEY_ESCAPE)) { map_open = false; captured = true; DisableCursor(); discard_mouse = true; mode_changed = true; }
                }
                if (!tuning_open && (command == MenuCommand::Map || (shortcuts && controls.pressed(Action::Map) && !mode_changed))) {
                    toggle_map();
                }
                if (map_open && !mode_changed && !menu.blocking() && IsWindowFocused()) {
                    const auto mouse = GetMousePosition();
                    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                    const bool inside = CheckCollisionPointRec(mouse, map_viewport);
                    if (click) for (int i = 0; i < int(map_buttons.size()); ++i) if (CheckCollisionPointRec(mouse, map_buttons[i])) {
                        if (i == 0) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                        else if (i == 1) world_map.fit(map_viewport, map_region, forza::Environment::extent);
                        else if (i == 4) toggle_map();
                        else world_map.zoom(i == 2 ? -1 : 1, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, forza::Environment::extent);
                    }
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    if (IsKeyPressed(KEY_HOME)) world_map.fit(map_viewport, map_region, forza::Environment::extent);
                    if (IsKeyPressed(KEY_C)) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                    const float horizontal = std::max(float(IsKeyDown(KEY_RIGHT)), std::max(controls.value(Action::Right), controls.value(Action::LookRight)))
                        - std::max(float(IsKeyDown(KEY_LEFT)), std::max(controls.value(Action::Left), controls.value(Action::LookLeft)));
                    const float vertical = std::max(float(IsKeyDown(KEY_DOWN)), std::max(controls.value(Action::Backward), controls.value(Action::LookDown)))
                        - std::max(float(IsKeyDown(KEY_UP)), std::max(controls.value(Action::Forward), controls.value(Action::LookUp)));
                    world_map.pan({-horizontal * 400 * frame, -vertical * 400 * frame}, forza::Environment::extent);
                    const float zoom = float(pressed(KEY_EQUAL) || pressed(KEY_KP_ADD)) - float(pressed(KEY_MINUS) || pressed(KEY_KP_SUBTRACT))
                        + (controls.value(Action::ZoomIn) - controls.value(Action::ZoomOut)) * 4 * frame;
                    if (zoom != 0) world_map.zoom(zoom, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, forza::Environment::extent);
                    if (inside) world_map.zoom(GetMouseWheelMove(), mouse, map_viewport, forza::Environment::extent);
                    if (click && inside) map_dragging = true;
                    else if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map_dragging = false;
                    if (map_dragging && !click) world_map.pan(GetMouseDelta(), forza::Environment::extent);
                    SetMouseCursor(map_dragging ? MOUSE_CURSOR_RESIZE_ALL : inside ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
                }
                if (tuning_open && !mode_changed && !menu.blocking()) {
                    const auto mouse = GetMousePosition();
                    const bool fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    const forza::TuningPanelInput input{mouse, IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
                        IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsWindowFocused(), fine,
                        int(pressed(KEY_RIGHT)) - int(pressed(KEY_LEFT)),
                        int(pressed(KEY_DOWN)) - int(pressed(KEY_UP)), GetMouseWheelMove()};
                    const auto action = tuning_panel.update(scene->player.car(), GetScreenWidth(), GetScreenHeight(), input);
                    if (action.reset_car) { scene->reset(); snap_camera(); jump_pending = false; accumulator = 0; }
                    if (action.close) toggle_tuning();
                    const bool outside = !tuning_panel.contains(mouse, GetScreenWidth(), GetScreenHeight());
                    const bool dragging = outside && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && IsWindowFocused();
                    if (tuning_open && IsWindowFocused() && !mode_changed && (dragging || (outside && input.scroll != 0))) {
                        Vector2 delta = dragging && orbit_dragging ? GetMouseDelta() : Vector2{};
                        orbit.look(delta.x, delta.y, outside ? input.scroll : 0, true, scene->player.car().forward(), 0, frame);
                    }
                    orbit_dragging = dragging;
                    SetMouseCursor(tuning_open && !outside ? MOUSE_CURSOR_DEFAULT : dragging ? MOUSE_CURSOR_RESIZE_ALL : MOUSE_CURSOR_DEFAULT);
                }
                if (!captured && !map_open && !tuning_open && !menu.blocking() && !mode_changed && IsWindowFocused() &&
                    GetMousePosition().y >= forza::menu_height && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    captured = true; DisableCursor(); discard_mouse = true;
                }
            }
            const bool active = !map_open && !tuning_open && !menu.blocking() && !mode_changed &&
                (!screenshot.empty() || (captured && IsWindowFocused()));
            const bool simulate = !map_open && !menu.blocking() && (active || (tuning_open && (!screenshot.empty() || IsWindowFocused())));
            day_night.advance(elapsed, screenshot.empty() && simulate);
            if (active && screenshot.empty()) {
                if (controls.pressed(Action::Recover)) {
                    scene->reset(); snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (controls.pressed(Action::Interact)) {
                    const bool stealing = scene->player.can_steal();
                    const auto result = scene->player.interact();
                    if (result == forza::Interaction::Entered || result == forza::Interaction::Exited) {
                        jump_pending = false;
                        snap_camera();
                        notice = result == forza::Interaction::Entered ? (scene->player.flying() ? "Entered plane - increase throttle to take off" :
                            stealing ? "Stole traffic car" : "Entered car") : "On foot";
                    } else if (result == forza::Interaction::TooFast) notice = scene->player.flying()
                        ? "Land and stop before exiting the plane" : "Slow down before exiting the car";
                    else if (result == forza::Interaction::Blocked) notice = "Exit blocked - move the vehicle to an open space";
                    else notice = "Move closer to a car or plane to enter";
                    notice_time = 2.5f;
                }
                if (scene->player.on_foot() && controls.pressed(Action::Jump)) jump_pending = true;
                if (scene->player.flying() && controls.pressed(Action::Flaps)) flaps = !flaps;
                Vector2 mouse = GetMouseDelta();
                if (discard_mouse) { mouse = {}; discard_mouse = false; }
                mouse.x += (controls.value(Action::LookRight) - controls.value(Action::LookLeft)) * 700 * frame;
                mouse.y += (controls.value(Action::LookDown) - controls.value(Action::LookUp)) * 700 * frame;
                orbit.look(mouse.x, mouse.y, GetMouseWheelMove() + (controls.value(Action::ZoomIn) - controls.value(Action::ZoomOut)) * 6 * frame, scene->player.driving(), scene->player.forward(),
                    scene->player.flying() ? scene->plane.velocity().Length() : scene->player.driving() ? scene->player.car().velocity().Length() : 0, frame, scene->player.flying());
            }
            forza::Input driving;
            forza::FootInput walking;
            forza::FlightInput flight;
            flight.flaps = flaps;
            driving.parking_brake = tuning_open;
            if (active && screenshot.empty()) {
                driving.throttle = controls.value(Action::Forward) - controls.value(Action::Backward);
                driving.steer = controls.value(Action::Left) - controls.value(Action::Right);
                driving.handbrake = controls.value(Action::Brake) > .5f;
                walking.direction = orbit.move_direction(driving.throttle, -driving.steer);
                walking.sprint = controls.value(Action::Sprint) > .5f;
                flight.throttle = controls.value(Action::ThrottleUp) - controls.value(Action::ThrottleDown);
                flight.pitch = -driving.throttle;
                flight.roll = driving.steer;
                flight.yaw = controls.value(Action::RudderLeft) - controls.value(Action::RudderRight);
                flight.brake = driving.handbrake;
            }
            if (simulate) accumulator += frame;
            else { accumulator = 0; jump_pending = false; }
            while (accumulator >= double(forza::fixed_step)) {
                walking.jump = jump_pending;
                scene->player.step(driving, walking, forza::fixed_step, flight);
                jump_pending = false;
                scene->record_skids();
                const auto position = scene->player.position();
                const bool recover = scene->player.flying() ? position.GetY() < -.6f || position.Length() > 24000
                    : environment.submerged(position);
                if (recover) {
                    scene->reset(); snap_camera(); flaps = false;
                    notice = scene->player.flying() ? "Recovered aircraft at the airport" : "Recovered from water"; notice_time = 3;
                }
                if (!scene->player.flying() && scene->plane.position().GetY() < -.6f) scene->player.recover_plane();
                accumulator -= double(forza::fixed_step);
            }
            if (engine_ready) {
                const auto& car = scene->player.car();
                const float speed = car.velocity().Dot(car.forward());
                // ponytail: speed/throttle approximate revs; use drivetrain RPM if gears are simulated.
                const float revs = std::clamp(std::abs(speed) / (speed < 0 ? 11.0f : car.tuning().top_speed), 0.0f, 1.0f);
                const float throttle = driving.throttle * speed >= 0 ? std::abs(driving.throttle) : 0;
                const float target_pitch = .7f + 1.5f * revs + .2f * throttle;
                const float target_volume = simulate && scene->player.driving() && screenshot.empty()
                    ? .22f + .18f * revs + .25f * throttle : 0;
                engine_pitch += (target_pitch - engine_pitch) * (1 - std::exp(-8 * frame));
                engine_volume += (target_volume - engine_volume) * (1 - std::exp(-10 * frame));
                SetMusicPitch(engine, engine_pitch);
                SetMusicVolume(engine, engine_volume);
                UpdateMusicStream(engine);
            }
            const auto target = focus();
            const auto desired = orbit.desired_position(target, tuning_open || scene->player.driving(), !tuning_open && scene->player.flying());
            const float follow = 1 - std::exp(-12 * frame);
            camera.target = lerp(camera.target, render_vector(target), follow);
            auto smoothed = forza::Vec3(camera.position.x, camera.position.y, camera.position.z);
            smoothed += (desired - smoothed) * follow;
            const auto camera_target = forza::Vec3(camera.target.x, camera.target.y, camera.target.z);
            const auto offset = smoothed - camera_target;
            const float fraction = scene->world.camera_fraction(camera_target, offset,
                (tuning_open || scene->player.driving()) ? scene->player.car().body_id() : scene->player.flying() ? scene->plane.body_id() : JPH::BodyID());
            camera.position = render_vector(camera_target + offset * fraction);
            const Camera3D view = camera;
            rlSetClipPlanes(0.2, 30000);
            notice_time = std::max(0.0f, notice_time - frame);
            BeginDrawing();
            const auto daylight = day_night.lighting();
            ClearBackground(BLACK);
            if (map_open) {
                ClearBackground({0, 0, 112, 255});
                forza::ui::draw_text("MIAMI & FLORIDA KEYS / MAP", 26, 48, 22, RAYWHITE);
                forza::ui::draw_text("NORTH UP / GAMEPLAY PAUSED", 26, 88, 16, {174, 231, 246, 255});
                const char* labels[] = {"Player", "Region", "-", "+", "Close"};
                for (int i = 0; i < int(map_buttons.size()); ++i) {
                    const auto button = map_buttons[i];
                    const bool hover = CheckCollisionPointRec(GetMousePosition(), button);
                    DrawRectangleRec(button, hover ? Color{0, 112, 112, 255} : Color{170, 170, 170, 255});
                    DrawRectangleLinesEx(button, 1, RAYWHITE);
                    forza::ui::draw_text(labels[i], int(button.x + (button.width - forza::ui::measure_text(labels[i], 16)) / 2),
                        int(button.y + 7), 16, hover ? RAYWHITE : Color{0, 0, 0, 255});
                }
                scenery.world_map(world_map, map_viewport, scene->car, scene->plane, scene->player.position(), scene->player.forward(), &scene->traffic);
                forza::ui::draw_text("Drag: pan / Wheel: zoom / Arrows or left stick: pan", 26, GetScreenHeight() - 66, 16, RAYWHITE);
                forza::ui::draw_text("Home: region / C: player / +/-: zoom / F2 or Esc: close", 26, GetScreenHeight() - 40, 16, RAYWHITE);
            } else {
                scenery.draw_sky(view, daylight, float(GetTime()));
                car_renderer.set_lighting(daylight);
                BeginMode3D(view);
                scenery.draw(view, float(GetTime()), daylight);
                BeginShaderMode(scenery.object_shader());
                draw_skid_marks(scene->marks);
                draw_car(scene->car, car_renderer, view);
                for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                    const auto& vehicle = scene->traffic.cars()[i];
                    if (!vehicle.car->simulated()) continue;
                    if ((vehicle.car->position() - scene->player.position()).LengthSq() > 1000 * 1000) continue;
                    draw_car(*vehicle.car, car_renderer, view, traffic_paint(i));
                }
                draw_plane(scene->plane, scene->player.flying());
                if (scene->player.on_foot()) draw_character(scene->player.character(), environment);
                EndShaderMode();
                EndMode3D();
                draw_hud(*scene, driving.handbrake, captured || tuning_open || !screenshot.empty(), tuning_open, flaps);
                if (!tuning_open) scenery.minimap(scene->car, scene->plane, scene->player.position(), scene->player.forward(), view, &scene->traffic);
                const auto region = scene->player.position();
                if (!tuning_open) {
                    const char* location = forza::Environment::district(region.GetX(), region.GetZ());
                    const int width = forza::ui::measure_text(location, 20), x = GetScreenWidth() - width - 26;
                    DrawRectangle(x - 12, GetScreenHeight() - 50, width + 24, 36, {19, 28, 45, 230});
                    forza::ui::draw_text(location, x, GetScreenHeight() - 42, 20, RAYWHITE);
                } else forza::ui::draw_text("Settings > Car tuning / Esc to close tuning", 26, GetScreenHeight() - 30, 16, RAYWHITE);
                if (notice_time > 0) forza::ui::draw_text(notice.c_str(), 26, scene->player.flying() ? 190 : 161, 20, RAYWHITE);
                if (tuning_open) tuning_panel.draw(scene->player.car(), GetScreenWidth(), GetScreenHeight());
            }
            const int clock_x = tuning_open ? 14 : GetScreenWidth() - 194, clock_y = tuning_open ? 198 : 46;
            DrawRectangle(clock_x, clock_y, 180, 40, {19, 28, 45, 230});
            const auto clock = day_night.clock();
            forza::ui::draw_text(clock.data(), clock_x + (180 - forza::ui::measure_text(clock.data(), 24)) / 2,
                clock_y + 8, 24, {174, 231, 246, 255});
            menu.draw(controls, mapping_path, captured);
            EndDrawing();
            if (!screenshot.empty() && ++rendered_frames >= 90) {
                const Image capture = LoadImageFromScreen();
                ExportImage(capture, screenshot.c_str());
                UnloadImage(capture);
                break;
            }
        }
        if (!menu.save_pending(controls, mapping_path)) TraceLog(LOG_ERROR, "Could not save controller mappings; previous file retained");
        if (engine_ready) UnloadMusicStream(engine);
    }
    EnableCursor();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
