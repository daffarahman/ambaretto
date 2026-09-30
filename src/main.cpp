#include "vehicle.hpp"
#include "environment.hpp"
#include "environment_renderer.hpp"
#include "car_renderer.hpp"
#include "tuning_panel.hpp"
#include "player.hpp"
#include "third_person_camera.hpp"
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
    forza::Player player{world, car, environment};
    std::vector<SkidMark> marks;
    std::size_t next_mark = 0;
    std::array<forza::Vec3, 4> last_skid{};
    std::array<bool, 4> last_valid{};
    static constexpr std::size_t max_marks = 2400;

    explicit Scene(const forza::Environment& map) : environment(map), world(map) {
        marks.reserve(max_marks);
        reset();
    }
    void reset() {
        player.reset();
        marks.clear();
        next_mark = 0;
        last_valid.fill(false);
    }

    void record_skids() {
        for (std::size_t i = 2; i < 4; ++i) {
            const auto& wheel = car.wheels()[i];
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

void draw_car(const forza::Car& car, const forza::CarRenderer& renderer, const Camera3D& camera) {
    if (!renderer.draw_body(car, camera)) {
        const auto basis = car.rotation();
        draw_box(car.position() + car.rotate(forza::Vec3(0, forza::chassis_offset, 0)), basis,
                 forza::Vec3(float(1.85), float(0.5), float(3.7)), {209, 46, 54, 255});
        draw_box(car.position() + car.rotate(forza::Vec3(0, float(0.93), float(0.25))), basis,
                 forza::Vec3(float(1.45), float(0.55), float(1.7)), {39, 58, 73, 255});
    }
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        DrawLine3D(render_vector(mount), render_vector(wheel.center), LIGHTGRAY);
        if (renderer.draw_wheel(car, wheel, camera)) continue;
        auto axis = car.rotate(forza::Vec3(1, 0, 0));
        if (wheel.front) axis = forza::Quat::sRotation(car.rotate(forza::Vec3(0, 1, 0)), car.steering()) * axis;
        const float radius = car.tuning().wheel_radius, size = radius / forza::wheel_radius;
        DrawCylinderEx(render_vector(wheel.center - axis * (0.13f * size)),
                       render_vector(wheel.center + axis * (0.13f * size)), radius, radius, 16, BLACK);
        DrawCylinderEx(render_vector(wheel.center - axis * (0.14f * size)),
                       render_vector(wheel.center + axis * (0.14f * size)), radius * .5f, radius * .5f, 16, DARKGRAY);
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

void draw_hud(const Scene& scene, bool handbrake, bool captured, bool aerial, bool tuning) {
    const auto& player = scene.player;
    DrawRectangle(14, 14, 770, 103, {19, 28, 35, 220});
    DrawText(tuning ? "TUNING MODE  |  Drag sliders to adjust your car" :
        player.driving() ? "W/S drive  A/D steer  SPACE drift  E exit car" : "WASD move  SHIFT run  SPACE jump  E enter car", 26, 24, 19, RAYWHITE);
    DrawText(tuning ? "Right drag outside panel: camera  |  Scroll: adjust / zoom" :
        "Mouse look  Wheel zoom  ESC pause  R reset  F2 map  F3 tuning", 26, 49, 17, LIGHTGRAY);
    if (tuning) DrawText("Physics live / parking brake / Driving input disabled", 26, 79, 19, GOLD);
    else if (player.driving()) {
        const float speed = scene.car.velocity().Dot(scene.car.forward()) * 3.6f;
        DrawText(TextFormat("DRIVING  |  %5.1f km/h", double(speed)), 26, 79, 20, GOLD);
        if (handbrake && std::abs(speed) > 15) DrawText("DRIFT", 340, 79, 20, ORANGE);
    } else {
        DrawText(player.can_enter() ? "ON FOOT  |  Press E to enter your car" : "ON FOOT  |  Blue marker: your car", 26, 79, 20, GOLD);
    }
    if (!captured && !aerial) {
        const int x = GetScreenWidth() / 2 - 190, y = GetScreenHeight() / 2 - 36;
        DrawRectangle(x - 14, y - 12, 408, 87, {19, 28, 35, 235});
        DrawText("Click to resume and capture mouse", x, y, 20, RAYWHITE);
        DrawText("ESC releases the mouse", x, y + 32, 18, LIGHTGRAY);
    }
}
} // namespace

int main(int argc, char** argv) {
    std::string screenshot;
    bool aerial = false, start_on_foot = false, tuning_open = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--overview") aerial = true;
        if (arg == "--on-foot") start_on_foot = true;
        if (arg == "--tuning") tuning_open = true;
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    }
    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!screenshot.empty()) window_flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(window_flags);
    InitWindow(1280, 720, "Forza Ambazon - Coastal City");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1024, 600);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    bool captured = screenshot.empty() && !aerial && !tuning_open;
    bool resume_capture = screenshot.empty() && !aerial;
    bool resume_aerial = aerial;
    if (tuning_open) aerial = false;
    if (captured) DisableCursor();
    {
        const forza::Environment environment;
        forza::EnvironmentRenderer scenery(environment);
        forza::CarRenderer car_renderer;
        forza::TuningPanel tuning_panel;
        auto scene = std::make_unique<Scene>(environment);
        if (start_on_foot) scene->player.interact();
        forza::ThirdPersonCamera orbit;
        forza::ThirdPersonCamera saved_orbit = orbit;
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const auto focus = [&]() {
            if (!tuning_open) return scene->player.position() + forza::Vec3(0, scene->player.driving() ? 0.7f : 1.25f, 0);
            const auto center = scene->car.position() + forza::Vec3(0, .7f, 0);
            const float distance = (orbit.desired_position(center, true) - center).Length();
            // Shift the camera's aim to frame the car in the area beside the
            // 400-pixel panel. Scale with zoom and viewport height (60-deg FOV).
            const auto right = orbit.forward().Cross(forza::Vec3::sAxisY());
            return center + right * (distance * 400.0f / GetScreenHeight() * .57735027f);
        };
        const auto snap_camera = [&]() {
            const auto heading = tuning_open ? scene->car.forward() : scene->player.forward();
            if (tuning_open) orbit = forza::ThirdPersonCamera{};
            orbit.reset(std::atan2(-heading.GetX(), -heading.GetZ()));
            if (tuning_open) orbit.look(-230, 25, 5, true, heading, 0, 0);
            camera.target = render_vector(focus());
            camera.position = render_vector(orbit.desired_position(focus(), tuning_open || scene->player.driving()));
        };
        snap_camera();
        double accumulator = 0;
        float notice_time = 0;
        std::string notice;
        bool jump_pending = false, discard_mouse = true, orbit_dragging = false;
        int rendered_frames = 0;
        while (!WindowShouldClose()) {
            const float frame = std::min(GetFrameTime(), 0.1f);
            bool mode_changed = false;
            const auto toggle_tuning = [&]() {
                mode_changed = true;
                tuning_panel.cancel_drag();
                orbit_dragging = false;
                jump_pending = false; accumulator = 0; discard_mouse = true;
                if (!tuning_open) {
                    resume_capture = captured;
                    resume_aerial = aerial;
                    saved_orbit = orbit;
                    tuning_open = true; aerial = false; captured = false;
                    EnableCursor(); snap_camera();
                } else {
                    tuning_open = false;
                    aerial = resume_aerial;
                    orbit = saved_orbit;
                    captured = resume_capture && IsWindowFocused() && !aerial;
                    if (captured) DisableCursor(); else EnableCursor();
                    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                }
            };
            if (screenshot.empty()) {
                if (!IsWindowFocused()) {
                    if (captured) EnableCursor();
                    captured = false; resume_capture = false;
                    tuning_panel.cancel_drag(); orbit_dragging = false; discard_mouse = true;
                }
                if (IsKeyPressed(KEY_F3)) toggle_tuning();
                else if (IsKeyPressed(KEY_ESCAPE)) {
                    if (tuning_open) toggle_tuning();
                    else if (captured) { EnableCursor(); captured = false; mode_changed = true; }
                }
                if (IsKeyPressed(KEY_F2) && !tuning_open && !mode_changed) {
                    aerial = !aerial;
                    captured = !aerial;
                    if (captured) DisableCursor(); else EnableCursor();
                    discard_mouse = true; mode_changed = true;
                }
                if (tuning_open && !mode_changed) {
                    const auto mouse = GetMousePosition();
                    const bool fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    const forza::TuningPanelInput input{mouse, IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
                        IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsWindowFocused(), fine,
                        int(pressed(KEY_RIGHT)) - int(pressed(KEY_LEFT)),
                        int(pressed(KEY_DOWN)) - int(pressed(KEY_UP)), GetMouseWheelMove()};
                    const auto action = tuning_panel.update(scene->car, GetScreenWidth(), GetScreenHeight(), input);
                    if (action.reset_car) { scene->reset(); snap_camera(); jump_pending = false; accumulator = 0; }
                    if (action.close) toggle_tuning();
                    const bool outside = !tuning_panel.contains(mouse, GetScreenWidth(), GetScreenHeight());
                    const bool dragging = outside && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && IsWindowFocused();
                    if (tuning_open && IsWindowFocused() && !mode_changed && (dragging || (outside && input.scroll != 0))) {
                        Vector2 delta = dragging && orbit_dragging ? GetMouseDelta() : Vector2{};
                        orbit.look(delta.x, delta.y, outside ? input.scroll : 0, true, scene->car.forward(), 0, frame);
                    }
                    orbit_dragging = dragging;
                    SetMouseCursor(tuning_open && !outside ? MOUSE_CURSOR_DEFAULT : dragging ? MOUSE_CURSOR_RESIZE_ALL : MOUSE_CURSOR_DEFAULT);
                }
                if (!captured && !aerial && !tuning_open && !mode_changed && IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    captured = true; DisableCursor(); discard_mouse = true;
                }
            }
            const bool active = !tuning_open && !mode_changed &&
                (!screenshot.empty() || (captured && IsWindowFocused() && !aerial));
            const bool simulate = active || (tuning_open && (!screenshot.empty() || IsWindowFocused()));
            if (active && screenshot.empty()) {
                if (IsKeyPressed(KEY_R)) {
                    scene->reset(); snap_camera(); accumulator = 0; jump_pending = false;
                }
                if (IsKeyPressed(KEY_E)) {
                    const auto result = scene->player.interact();
                    if (result == forza::Interaction::Entered || result == forza::Interaction::Exited) {
                        jump_pending = false;
                        camera.target = render_vector(focus());
                        notice = result == forza::Interaction::Entered ? "Entered car" : "On foot";
                    } else if (result == forza::Interaction::TooFast) notice = "Slow down before entering or exiting";
                    else if (result == forza::Interaction::Blocked) notice = "Exit blocked - move the car to an open space";
                    else notice = "Move closer to the car to enter";
                    notice_time = 2.5f;
                }
                if (!scene->player.driving() && IsKeyPressed(KEY_SPACE)) jump_pending = true;
                Vector2 mouse = GetMouseDelta();
                if (discard_mouse) { mouse = {}; discard_mouse = false; }
                orbit.look(mouse.x, mouse.y, GetMouseWheelMove(), scene->player.driving(), scene->car.forward(),
                    scene->car.velocity().Length(), frame);
            }
            forza::Input driving;
            forza::FootInput walking;
            driving.parking_brake = tuning_open;
            if (active && screenshot.empty()) {
                driving.throttle = float(IsKeyDown(KEY_W)) - float(IsKeyDown(KEY_S));
                driving.steer = float(IsKeyDown(KEY_A)) - float(IsKeyDown(KEY_D));
                driving.handbrake = IsKeyDown(KEY_SPACE);
                walking.direction = orbit.move_direction(driving.throttle, float(IsKeyDown(KEY_D)) - float(IsKeyDown(KEY_A)));
                walking.sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            }
            if (simulate) accumulator += frame;
            else { accumulator = 0; jump_pending = false; }
            while (accumulator >= double(forza::fixed_step)) {
                walking.jump = jump_pending;
                scene->player.step(driving, walking);
                jump_pending = false;
                scene->record_skids();
                if (environment.submerged(scene->player.position())) {
                    scene->reset(); snap_camera(); notice = "Recovered from water"; notice_time = 3;
                }
                accumulator -= double(forza::fixed_step);
            }
            const auto target = focus();
            const auto desired = orbit.desired_position(target, tuning_open || scene->player.driving());
            const float follow = 1 - std::exp(-12 * frame);
            camera.target = lerp(camera.target, render_vector(target), follow);
            auto smoothed = forza::Vec3(camera.position.x, camera.position.y, camera.position.z);
            smoothed += (desired - smoothed) * follow;
            const auto camera_target = forza::Vec3(camera.target.x, camera.target.y, camera.target.z);
            const auto offset = smoothed - camera_target;
            const float fraction = scene->world.camera_fraction(camera_target, offset,
                (tuning_open || scene->player.driving()) ? scene->car.body_id() : JPH::BodyID());
            camera.position = render_vector(camera_target + offset * fraction);
            Camera3D view = camera;
            if (aerial) view = Camera3D{{330, 370, 380}, {0, 5, 0}, {0, 1, 0}, 52, CAMERA_PERSPECTIVE};
            rlSetClipPlanes(aerial ? 1.0 : 0.2, 5000);
            notice_time = std::max(0.0f, notice_time - frame);
            BeginDrawing();
            ClearBackground({153, 203, 233, 255});
            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), {116, 175, 208, 255}, {210, 228, 227, 255});
            BeginMode3D(view);
            scenery.draw(view, float(GetTime()));
            draw_skid_marks(scene->marks);
            draw_car(scene->car, car_renderer, view);
            if (!scene->player.driving()) draw_character(scene->player.character(), environment);
            EndMode3D();
            draw_hud(*scene, driving.handbrake, captured || tuning_open || !screenshot.empty(), aerial, tuning_open);
            if (!tuning_open) scenery.minimap(environment, scene->car, scene->player.position(), scene->player.forward(), GetScreenWidth());
            DrawText("AMBAZON ISLAND", 26, GetScreenHeight() - 58, 24, RAYWHITE);
            DrawText(tuning_open ? "F3 / Esc to close tuning and return to play" :
                aerial ? "AERIAL VIEW  /  F2 to return" : "Explore on foot or drive / Click to capture mouse / Close window to quit", 26, GetScreenHeight() - 30, 16, RAYWHITE);
            if (notice_time > 0) DrawText(notice.c_str(), 26, 129, 20, RAYWHITE);
            if (tuning_open) tuning_panel.draw(scene->car, GetScreenWidth(), GetScreenHeight());
            EndDrawing();
            if (!screenshot.empty() && ++rendered_frames >= 90) {
                const Image capture = LoadImageFromScreen();
                ExportImage(capture, screenshot.c_str());
                UnloadImage(capture);
                break;
            }
        }
    }
    EnableCursor();
    CloseWindow();
    return 0;
}
