#include "vehicle.hpp"
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {
Vector3 render_vector(const btVector3& value) {
    return {float(value.x()), float(value.y()), float(value.z())};
}
Vector3 lerp(Vector3 a, Vector3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

struct SkidMark { btVector3 from, to; };
struct Scene {
    forza::PhysicsWorld world;
    forza::Car car{world};
    std::vector<SkidMark> marks;
    std::size_t next_mark = 0;
    std::array<btVector3, 4> last_skid{};
    std::array<bool, 4> last_valid{};
    static constexpr std::size_t max_marks = 2400;

    Scene() { marks.reserve(max_marks); }

    void record_skids() {
        for (std::size_t i = 2; i < 4; ++i) {
            const auto& wheel = car.wheels()[i];
            if (!wheel.grounded || !wheel.skidding) {
                last_valid[i] = false;
                continue;
            }
            const btVector3 point = wheel.ground_point + wheel.ground_normal * btScalar(0.025);
            if (!last_valid[i]) {
                last_skid[i] = point;
                last_valid[i] = true;
                continue;
            }
            const btScalar distance = (point - last_skid[i]).length();
            if (distance < btScalar(0.12)) continue;
            if (distance < btScalar(1.5)) {
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
        const btVector3 direction = mark.to - mark.from;
        const btScalar length = std::hypot(direction.x(), direction.z());
        if (length < btScalar(0.001)) continue;
        const btVector3 side(-direction.z() / length * btScalar(0.105), 0,
                             direction.x() / length * btScalar(0.105));
        const Vector3 a = render_vector(mark.from - side);
        const Vector3 b = render_vector(mark.from + side);
        const Vector3 c = render_vector(mark.to + side);
        const Vector3 d = render_vector(mark.to - side);
        DrawTriangle3D(a, b, c, tint);
        DrawTriangle3D(a, c, d, tint);
    }
}

void draw_ground() {
    DrawPlane({0, 0, 0}, {400, 400}, {82, 132, 90, 255});
    DrawPlane({0, 0.005f, 0}, {10, 200}, {53, 60, 70, 255});
    for (float z = -95; z <= 95; z += 8) {
        DrawCube({0, 0.016f, z}, 0.12f, 0.01f, 4, {242, 221, 126, 255});
    }
    for (float z : {-15.0f, -16.7f, -35.0f, -36.7f}) {
        DrawCube({0, 0.06f, z}, 8, 0.12f, 0.32f, {225, 181, 71, 255});
    }
    for (float x : {-5.0f, 5.0f}) DrawCube({x, 0.025f, 0}, 0.09f, 0.02f, 200, RAYWHITE);
}

void draw_box(const btVector3& center, const btMatrix3x3& rotation,
              const btVector3& size, Color tint) {
    const auto half = size * btScalar(0.5);
    const std::array<btVector3, 8> local = {
        btVector3(-half.x(), -half.y(), -half.z()), btVector3(half.x(), -half.y(), -half.z()),
        btVector3(half.x(), half.y(), -half.z()), btVector3(-half.x(), half.y(), -half.z()),
        btVector3(-half.x(), -half.y(), half.z()), btVector3(half.x(), -half.y(), half.z()),
        btVector3(half.x(), half.y(), half.z()), btVector3(-half.x(), half.y(), half.z())};
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
    const auto& basis = car.body().getWorldTransform().getBasis();
    draw_box(car.position() + car.rotate(btVector3(0, forza::chassis_offset, 0)), basis,
             btVector3(btScalar(1.85), btScalar(0.5), btScalar(3.7)), {209, 46, 54, 255});
    draw_box(car.position() + car.rotate(btVector3(0, btScalar(0.93), btScalar(0.25))), basis,
             btVector3(btScalar(1.45), btScalar(0.55), btScalar(1.7)), {39, 58, 73, 255});
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        DrawLine3D(render_vector(mount), render_vector(wheel.center), LIGHTGRAY);
        auto axis = car.rotate(btVector3(1, 0, 0));
        if (wheel.front) axis = quatRotate(btQuaternion(car.rotate(btVector3(0, 1, 0)), car.steering()), axis);
        DrawCylinderEx(render_vector(wheel.center - axis * btScalar(0.13)),
                       render_vector(wheel.center + axis * btScalar(0.13)), 0.34f, 0.34f, 16, BLACK);
        DrawCylinderEx(render_vector(wheel.center - axis * btScalar(0.14)),
                       render_vector(wheel.center + axis * btScalar(0.14)), 0.17f, 0.17f, 16, DARKGRAY);
    }
}

void draw_hud(const forza::Car& car, bool handbrake) {
    const float speed = float(car.velocity().dot(car.forward()) * btScalar(3.6));
    int grounded = 0;
    for (const auto& wheel : car.wheels()) if (wheel.grounded) ++grounded;
    DrawRectangle(14, 14, 650, 92, {19, 28, 35, 220});
    DrawText("W/S drive   A/D steer   SPACE handbrake / drift   R reset", 26, 26, 20, RAYWHITE);
    DrawText(TextFormat("Speed: %5.1f km/h    Wheels grounded: %d/4", double(speed), grounded), 26, 58, 20, GOLD);
    if (handbrake && std::abs(speed) > 15) {
        const bool sliding = std::abs(car.velocity().dot(car.rotate(btVector3(1, 0, 0)))) > 2;
        DrawText(sliding ? "DRIFT" : "HANDBRAKE", 680, 26, 28, ORANGE);
    }
}
} // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Forza Ambazon - C++ Raycast Car");
    if (!IsWindowReady()) return 1;
    SetTargetFPS(60);
    {
        auto scene = std::make_unique<Scene>();
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        double accumulator = 0;
        while (!WindowShouldClose()) {
            if (IsKeyPressed(KEY_R)) {
                scene = std::make_unique<Scene>();
                accumulator = 0;
            }
            forza::Input input;
            input.throttle = btScalar(IsKeyDown(KEY_W)) - btScalar(IsKeyDown(KEY_S));
            input.steer = btScalar(IsKeyDown(KEY_A)) - btScalar(IsKeyDown(KEY_D));
            input.handbrake = IsKeyDown(KEY_SPACE);
            const double frame = std::min(double(GetFrameTime()), 0.1);
            accumulator += frame;
            while (accumulator >= double(forza::fixed_step)) {
                scene->car.step(input);
                scene->record_skids();
                scene->world.step();
                accumulator -= double(forza::fixed_step);
            }
            const auto& car = scene->car;
            const auto target = car.position() + btVector3(0, btScalar(0.65), 0);
            const auto camera_position = target - car.forward() * 8 + btVector3(0, btScalar(3.4), 0);
            const float follow = float(1 - std::exp(-5 * frame));
            camera.position = lerp(camera.position, render_vector(camera_position), follow);
            camera.target = lerp(camera.target, render_vector(target), follow);
            BeginDrawing();
            ClearBackground({153, 203, 233, 255});
            BeginMode3D(camera);
            draw_ground();
            draw_skid_marks(scene->marks);
            draw_car(car);
            EndMode3D();
            draw_hud(car, input.handbrake);
            EndDrawing();
        }
    }
    CloseWindow();
    return 0;
}
