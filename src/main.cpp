#include "vehicle.hpp"
#include "environment.hpp"
#include "environment_renderer.hpp"
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
        car.reset(environment.spawn());
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
              const forza::Vec3& size, Color tint) {
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
    for (const auto& edge : edges) DrawLine3D(p[edge[0]], p[edge[1]], MAROON);
}

void draw_car(const forza::Car& car) {
    const auto basis = car.rotation();
    draw_box(car.position() + car.rotate(forza::Vec3(0, forza::chassis_offset, 0)), basis,
             forza::Vec3(float(1.85), float(0.5), float(3.7)), {209, 46, 54, 255});
    draw_box(car.position() + car.rotate(forza::Vec3(0, float(0.93), float(0.25))), basis,
             forza::Vec3(float(1.45), float(0.55), float(1.7)), {39, 58, 73, 255});
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        DrawLine3D(render_vector(mount), render_vector(wheel.center), LIGHTGRAY);
        auto axis = car.rotate(forza::Vec3(1, 0, 0));
        if (wheel.front) axis = forza::Quat::sRotation(car.rotate(forza::Vec3(0, 1, 0)), car.steering()) * axis;
        DrawCylinderEx(render_vector(wheel.center - axis * float(0.13)),
                       render_vector(wheel.center + axis * float(0.13)), 0.34f, 0.34f, 16, BLACK);
        DrawCylinderEx(render_vector(wheel.center - axis * float(0.14)),
                       render_vector(wheel.center + axis * float(0.14)), 0.17f, 0.17f, 16, DARKGRAY);
    }
}

void draw_hud(const forza::Car& car, bool handbrake) {
    const float speed = float(car.velocity().Dot(car.forward()) * float(3.6));
    int grounded = 0;
    for (const auto& wheel : car.wheels()) if (wheel.grounded) ++grounded;
    DrawRectangle(14, 14, 700, 92, {19, 28, 35, 220});
    DrawText("W/S drive  A/D steer  SPACE drift  R reset  F2 aerial view", 26, 26, 20, RAYWHITE);
    DrawText(TextFormat("Speed: %5.1f km/h    Wheels grounded: %d/4", double(speed), grounded), 26, 58, 20, GOLD);
    if (handbrake && std::abs(speed) > 15) {
        const bool sliding = std::abs(car.velocity().Dot(car.rotate(forza::Vec3(1, 0, 0)))) > 2;
        DrawText(sliding ? "DRIFT" : "HANDBRAKE", 530, 56, 24, ORANGE);
    }
}
} // namespace

int main(int argc, char** argv) {
    std::string screenshot;
    bool aerial = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--overview") aerial = true;
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    }
    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!screenshot.empty()) window_flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(window_flags);
    InitWindow(1280, 720, "Forza Ambazon - Coastal City");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1024, 600);
    // A 1 m near plane retains precision for road overlays in the aerial view.
    rlSetClipPlanes(1.0, 5000);
    SetTargetFPS(60);
    {
        const forza::Environment environment;
        forza::EnvironmentRenderer scenery(environment);
        auto scene = std::make_unique<Scene>(environment);
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const auto snap_camera = [&]() {
            camera.target = render_vector(scene->car.position() + forza::Vec3(0, 0.65f, 0));
            camera.position = render_vector(scene->car.position() + forza::Vec3(0, 4.65f, 9));
        };
        snap_camera();
        double accumulator = 0;
        float recovery_notice = 0;
        int rendered_frames = 0;
        while (!WindowShouldClose()) {
            if (screenshot.empty() && IsKeyPressed(KEY_R)) {
                scene->reset();
                snap_camera();
                accumulator = 0;
            }
            if (screenshot.empty() && IsKeyPressed(KEY_F2)) aerial = !aerial;
            forza::Input input;
            input.throttle = float(IsKeyDown(KEY_W)) - float(IsKeyDown(KEY_S));
            input.steer = float(IsKeyDown(KEY_A)) - float(IsKeyDown(KEY_D));
            input.handbrake = IsKeyDown(KEY_SPACE);
            if (!screenshot.empty()) input = {};
            const double frame = std::min(double(GetFrameTime()), 0.1);
            accumulator += frame;
            while (accumulator >= double(forza::fixed_step)) {
                scene->car.step(input);
                scene->record_skids();
                scene->world.step();
                if (environment.submerged(scene->car.position())) {
                    scene->reset();
                    snap_camera();
                    recovery_notice = 3;
                }
                accumulator -= double(forza::fixed_step);
            }
            const auto& car = scene->car;
            const auto target = car.position() + forza::Vec3(0, float(0.65), 0);
            auto camera_position = target - car.forward() * 9 + forza::Vec3(0, 4.0f, 0);
            camera_position.SetY(std::max(camera_position.GetY(),
                environment.height(camera_position.GetX(), camera_position.GetZ()) + 3));
            const float follow = float(1 - std::exp(-5 * frame));
            camera.position = lerp(camera.position, render_vector(camera_position), follow);
            camera.target = lerp(camera.target, render_vector(target), follow);
            Camera3D view = camera;
            if (aerial) view = Camera3D{{330, 370, 380}, {0, 5, 0}, {0, 1, 0}, 52, CAMERA_PERSPECTIVE};
            recovery_notice = std::max(0.0f, recovery_notice - float(frame));
            BeginDrawing();
            ClearBackground({153, 203, 233, 255});
            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), {116, 175, 208, 255}, {210, 228, 227, 255});
            BeginMode3D(view);
            scenery.draw(view, float(GetTime()));
            draw_skid_marks(scene->marks);
            draw_car(car);
            EndMode3D();
            draw_hud(car, input.handbrake);
            scenery.minimap(environment, car, GetScreenWidth());
            DrawText("AMBAZON ISLAND", 26, GetScreenHeight() - 58, 24, RAYWHITE);
            DrawText(aerial ? "AERIAL VIEW  /  F2 to return" : "Downtown avenues / hill districts / coastal loop / beaches", 26, GetScreenHeight() - 30, 16, RAYWHITE);
            if (recovery_notice > 0) DrawText("Recovered from water", 26, 118, 20, RAYWHITE);
            EndDrawing();
            if (!screenshot.empty() && ++rendered_frames >= 90) {
                const Image capture = LoadImageFromScreen();
                ExportImage(capture, screenshot.c_str());
                UnloadImage(capture);
                break;
            }
        }
    }
    CloseWindow();
    return 0;
}
