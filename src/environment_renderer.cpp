#include "environment_renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace forza {
namespace {
constexpr float pi = 3.14159265359f;
struct MeshBuilder {
    std::vector<float> positions, normals;
    std::vector<unsigned char> colors;
    void triangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
        const Vec3 cross = (b - a).Cross(c - a);
        if (cross.LengthSq() < 1.0e-10f) return;
        const Vec3 normal = cross.Normalized();
        for (const auto& p : {a, b, c}) {
            positions.insert(positions.end(), {p.GetX(), p.GetY(), p.GetZ()});
            normals.insert(normals.end(), {normal.GetX(), normal.GetY(), normal.GetZ()});
            colors.insert(colors.end(), {color.r, color.g, color.b, color.a});
        }
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
        triangle(a, b, c, color); triangle(a, c, d, color);
    }
    void box(Vec3 center, Vec3 size, Color color) {
        const Vec3 h = size / 2;
        const std::array<Vec3, 8> p = {
            center + Vec3(-h.GetX(), -h.GetY(), -h.GetZ()), center + Vec3(h.GetX(), -h.GetY(), -h.GetZ()),
            center + Vec3(h.GetX(), h.GetY(), -h.GetZ()), center + Vec3(-h.GetX(), h.GetY(), -h.GetZ()),
            center + Vec3(-h.GetX(), -h.GetY(), h.GetZ()), center + Vec3(h.GetX(), -h.GetY(), h.GetZ()),
            center + Vec3(h.GetX(), h.GetY(), h.GetZ()), center + Vec3(-h.GetX(), h.GetY(), h.GetZ())};
        constexpr int faces[6][4] = {{0,3,2,1}, {4,5,6,7}, {0,4,7,3}, {1,2,6,5}, {3,7,6,2}, {0,1,5,4}};
        for (const auto& f : faces) quad(p[f[0]], p[f[1]], p[f[2]], p[f[3]], color);
    }
    void ribbon(const Environment& env, Vec3 a, Vec3 b, float width, Color color, float lift = 0.045f) {
        const Vec3 direction = (b - a).Normalized();
        const Vec3 side(-direction.GetZ() * width / 2, 0, direction.GetX() * width / 2);
        const std::array<Vec3, 4> edges = {a - side, a + side, b + side, b - side};
        float min_x = edges[0].GetX(), max_x = min_x, min_z = edges[0].GetZ(), max_z = min_z;
        for (const auto& p : edges) {
            min_x = std::min(min_x, p.GetX()); max_x = std::max(max_x, p.GetX());
            min_z = std::min(min_z, p.GetZ()); max_z = std::max(max_z, p.GetZ());
        }
        const auto cell = [](float value) {
            return std::clamp(int(std::floor((value + Environment::extent) / Environment::spacing)), 0, Environment::samples - 2);
        };
        // Clip the road footprint against each original terrain triangle.
        // Interpolating its vertices keeps roads exactly on the hillside;
        // a separate quad would cut through the terrain between samples.
        for (int z = cell(min_z); z <= cell(max_z); ++z) for (int x = cell(min_x); x <= cell(max_x); ++x) {
            const int base = z * Environment::samples + x;
            for (const auto& indices : {std::array<int, 3>{base, base + Environment::samples, base + 1},
                    std::array<int, 3>{base + 1, base + Environment::samples, base + Environment::samples + 1}}) {
                std::vector<Vec3> polygon{env.vertices()[indices[0]], env.vertices()[indices[1]], env.vertices()[indices[2]]};
                for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
                    const Vec3 start = edges[edge], delta = edges[(edge + 1) % 4] - start;
                    const auto signed_distance = [&](Vec3 p) {
                        return delta.GetX() * (p.GetZ() - start.GetZ()) - delta.GetZ() * (p.GetX() - start.GetX());
                    };
                    std::vector<Vec3> clipped;
                    Vec3 previous = polygon.back();
                    float previous_distance = signed_distance(previous);
                    for (const auto& current : polygon) {
                        const float current_distance = signed_distance(current);
                        const bool inside = current_distance <= 0, previous_inside = previous_distance <= 0;
                        if (inside != previous_inside) {
                            const float t = previous_distance / (previous_distance - current_distance);
                            clipped.push_back(previous + (current - previous) * t);
                        }
                        if (inside) clipped.push_back(current);
                        previous = current; previous_distance = current_distance;
                    }
                    polygon = std::move(clipped);
                }
                const Vec3 offset(0, lift, 0);
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                    triangle(polygon[0] + offset, polygon[i] + offset, polygon[i + 1] + offset, color);
            }
        }
    }
    Model upload() const {
        Mesh mesh{};
        mesh.vertexCount = int(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = static_cast<float*>(MemAlloc(unsigned(positions.size() * sizeof(float))));
        mesh.normals = static_cast<float*>(MemAlloc(unsigned(normals.size() * sizeof(float))));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(unsigned(colors.size())));
        std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
        std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
        std::memcpy(mesh.colors, colors.data(), colors.size());
        UploadMesh(&mesh, false);
        return LoadModelFromMesh(mesh);
    }
};
Color surface_color(Surface s) {
    switch (s) {
        case Surface::Sand: return {222, 202, 143, 255};
        case Surface::Rock: return {128, 147, 122, 255};
        // Road ribbons define crisp edges; keep the underlying grid green.
        case Surface::Road: return {98, 151, 96, 255};
        case Surface::Seabed: return {181, 176, 133, 255};
        default: return {98, 151, 96, 255};
    }
}
const char* land_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 position;
out vec3 normal;
out vec4 color;
void main() {
    position = vertexPosition;
    normal = vertexNormal;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})GLSL";
const char* land_fragment = R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec4 color;
uniform vec3 cameraPosition;
out vec4 finalColor;
void main() {
    float lighting = 0.58 + 0.42 * max(dot(normalize(normal), normalize(vec3(-0.45, 0.85, 0.30))), 0.0);
    float fog = smoothstep(450.0, 1400.0, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(color.rgb * lighting, vec3(0.64, 0.80, 0.87), fog * 0.80), color.a);
})GLSL";
const char* water_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform float time;
out vec3 position;
void main() {
    position = vertexPosition;
    position.y += sin(position.x * 0.06 + time * 0.7) * 0.10 + cos(position.z * 0.08 + time) * 0.08;
    gl_Position = mvp * vec4(position, 1.0);
})GLSL";
const char* water_fragment = R"GLSL(#version 330
in vec3 position;
uniform vec3 cameraPosition;
uniform float time;
out vec4 finalColor;
void main() {
    float angle = atan(position.z / 280.0, position.x / 320.0);
    float outline = 1.0 + 0.045 * sin(3.0 * angle + 0.4) + 0.025 * cos(5.0 * angle);
    float radius = length(position.xz / vec2(320.0, 280.0)) / outline;
    vec3 color = mix(vec3(0.17, 0.67, 0.70), vec3(0.07, 0.32, 0.50), smoothstep(0.94, 1.55, radius));
    float ripples = sin(position.x * 0.5 + position.z * 0.36 + time * 1.5) * sin(position.z * 0.21 - time);
    color += 0.028 * ripples;
    float foam = (1.0 - smoothstep(0.012, 0.028, abs(radius - (0.974 + 0.004 * sin(time + angle * 12.0))))) * 0.50;
    color = mix(color, vec3(0.85, 0.94, 0.89), foam);
    float fog = smoothstep(300.0, 1300.0, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(color, vec3(0.64, 0.80, 0.87), fog), 1.0);
})GLSL";
}

EnvironmentRenderer::EnvironmentRenderer(const Environment& env) {
    MeshBuilder ground, city, water;
    for (const auto& t : env.triangles())
        ground.triangle(env.vertices()[t.a], env.vertices()[t.b], env.vertices()[t.c], surface_color(t.surface));

    const auto street = [&](float lane, bool vertical, float start, float end) {
        const auto point = [&](float along, float offset) {
            return vertical ? Vec3(lane + offset, 0, along) : Vec3(along, 0, lane + offset);
        };
        for (float s = start; s < end; s += 2) {
            const float next = std::min(s + 2, end);
            city.ribbon(env, point(s, 0), point(next, 0), 12, {55, 64, 74, 255});
            const bool intersection = std::abs(s) < 130 && std::abs(s - std::round(s / 60) * 60) < 9;
            if (intersection) continue;
            if (int((s - start) / 2) % 6 < 3)
                city.ribbon(env, point(s, 0), point(next, 0), 0.18f, {239, 208, 110, 255}, 0.07f);
            for (float side : {-5.5f, 5.5f})
                city.ribbon(env, point(s, side), point(next, side), 0.14f, {215, 222, 216, 255}, 0.07f);
        }
    };
    for (float lane : {-120.0f, -60.0f, 0.0f, 60.0f, 120.0f}) {
        street(lane, true, lane == 0 ? -205 : -146, lane == 0 ? 205 : 146);
        street(lane, false, lane == 0 ? -235 : -146, lane == 0 ? 235 : 146);
    }
    const auto coast_point = [](float a) {
        const float outline = 1 + 0.045f * std::sin(3 * a + 0.4f) + 0.025f * std::cos(5 * a);
        return Vec3(320 * 0.72f * outline * std::cos(a), 0, 280 * 0.72f * outline * std::sin(a));
    };
    for (int i = 0; i < 480; ++i) {
        const auto a = coast_point(i * 2 * pi / 480), b = coast_point((i + 1) * 2 * pi / 480);
        city.ribbon(env, a, b, 12, {55, 64, 74, 255});
        if (i % 8 < 4) city.ribbon(env, a, b, 0.18f, {239, 208, 110, 255}, 0.07f);
    }
    constexpr Color palette[] = {{177, 192, 198, 255}, {213, 200, 178, 255}, {123, 162, 172, 255},
        {184, 163, 148, 255}, {205, 213, 204, 255}};
    for (const auto& b : env.buildings()) {
        city.box(b.center, b.size, palette[b.style]);
        const float bottom = b.center.GetY() - b.size.GetY() / 2;
        city.box(Vec3(b.center.GetX(), bottom + b.size.GetY() + 0.3f, b.center.GetZ()),
            Vec3(b.size.GetX() + 0.8f, 0.6f, b.size.GetZ() + 0.8f), {91, 112, 124, 255});
        for (float y = bottom + 3; y < bottom + b.size.GetY() - 1; y += 3.8f)
            for (float offset : {-5.0f, 0.0f, 5.0f}) for (float side : {-1.0f, 1.0f}) {
                const Color glass = {45, 84, 107, 255};
                city.box(Vec3(b.center.GetX() + offset, y, b.center.GetZ() + side * 8.025f), Vec3(2.8f, 1.7f, 0.04f), glass);
                city.box(Vec3(b.center.GetX() + side * 8.025f, y, b.center.GetZ() + offset), Vec3(0.04f, 1.7f, 2.8f), glass);
            }
        city.box(Vec3(b.center.GetX(), bottom + 1.3f, b.center.GetZ() + 8.04f), Vec3(2.2f, 2.6f, 0.06f), {29, 57, 68, 255});
    }
    for (const auto& t : env.trees()) {
        city.box(t.base + Vec3(0, t.height / 2, 0), Vec3(0.6f, t.height, 0.6f), {127, 99, 68, 255});
        const Vec3 crown = t.base + Vec3(0, t.height, 0);
        if (t.palm) {
            for (int i = 0; i < 7; ++i) {
                const float a = i * 2 * pi / 7;
                const Vec3 direction(std::cos(a), 0, std::sin(a)), side(-std::sin(a), 0, std::cos(a));
                const Vec3 middle = crown + direction * 2.2f + Vec3(0, 0.7f, 0);
                const Vec3 tip = crown + direction * 4 + Vec3(0, -1, 0);
                city.quad(crown, middle + side * 0.7f, tip, middle - side * 0.7f, {47, 126, 83, 255});
                city.quad(crown, middle - side * 0.7f, tip, middle + side * 0.7f, {47, 126, 83, 255});
            }
        } else {
            const Vec3 top = crown + Vec3(0, 2.6f, 0), bottom = crown - Vec3(0, 2, 0);
            for (int i = 0; i < 8; ++i) {
                const float a = i * 2 * pi / 8, b = (i + 1) * 2 * pi / 8;
                const Vec3 p = crown + Vec3(std::cos(a) * 2.7f, 0, std::sin(a) * 2.7f);
                const Vec3 q = crown + Vec3(std::cos(b) * 2.7f, 0, std::sin(b) * 2.7f);
                city.triangle(top, q, p, {60, 124, 76, 255});
                city.triangle(bottom, p, q, {51, 108, 68, 255});
            }
        }
    }
    // Beach umbrellas and towels on the broad eastern shore.
    for (int i = 0; i < 18; ++i) {
        const float a = -0.6f + i * 0.065f;
        const Vec3 p = coast_point(a) / 0.72f * 0.87f;
        const Vec3 base(p.GetX(), env.height(p.GetX(), p.GetZ()), p.GetZ());
        city.box(base + Vec3(0, 1.2f, 0), Vec3(0.12f, 2.4f, 0.12f), {221, 216, 193, 255});
        const Vec3 top = base + Vec3(0, 2.7f, 0);
        for (int j = 0; j < 8; ++j) {
            const float aa = j * pi / 4, bb = (j + 1) * pi / 4;
            city.triangle(top, base + Vec3(1.8f * std::cos(bb), 2.1f, 1.8f * std::sin(bb)),
                base + Vec3(1.8f * std::cos(aa), 2.1f, 1.8f * std::sin(aa)),
                j % 2 ? Color{232, 126, 87, 255} : Color{246, 235, 193, 255});
        }
        city.box(base + Vec3(2.7f, 0.04f, 0), Vec3(1.4f, 0.06f, 2.4f), {93, 166, 190, 255});
    }
    for (int z = -80; z < 80; ++z) for (int x = -80; x < 80; ++x) {
        const Vec3 a(x * 75.0f, 0, z * 75.0f), b = a + Vec3(75, 0, 0), c = a + Vec3(0, 0, 75), d = a + Vec3(75, 0, 75);
        water.quad(a, c, d, b, WHITE);
    }
    terrain_ = ground.upload(); city_ = city.upload(); ocean_ = water.upload();
    land_shader_ = LoadShaderFromMemory(land_vertex, land_fragment);
    water_shader_ = LoadShaderFromMemory(water_vertex, water_fragment);
    terrain_.materials[0].shader = city_.materials[0].shader = land_shader_;
    ocean_.materials[0].shader = water_shader_;
    land_camera_ = GetShaderLocation(land_shader_, "cameraPosition");
    water_camera_ = GetShaderLocation(water_shader_, "cameraPosition");
    water_time_ = GetShaderLocation(water_shader_, "time");
}

EnvironmentRenderer::~EnvironmentRenderer() {
    UnloadModel(terrain_); UnloadModel(city_); UnloadModel(ocean_);
    UnloadShader(land_shader_); UnloadShader(water_shader_);
}
void EnvironmentRenderer::draw(const Camera3D& camera, float time) {
    SetShaderValue(land_shader_, land_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_time_, &time, SHADER_UNIFORM_FLOAT);
    DrawModel(ocean_, {0, 0, 0}, 1, WHITE);
    DrawModel(terrain_, {0, 0, 0}, 1, WHITE);
    DrawModel(city_, {0, 0, 0}, 1, WHITE);
}
void EnvironmentRenderer::minimap(const Environment& env, const Car& car, Vec3 player_position, Vec3 player_forward, int screen_width) const {
    const float left = float(screen_width - 220), top = 20, scale = 0.28f;
    DrawRectangle(int(left - 8), int(top - 8), 216, 225, {22, 39, 48, 230});
    DrawRectangle(int(left), int(top), 200, 200, {43, 111, 141, 255});
    for (int z = -350; z < 350; z += 10) for (int x = -350; x < 350; x += 10) {
        const float y = env.height(float(x), float(z));
        if (y > 0) DrawRectangle(int(left + 100 + x * scale), int(top + 100 + z * scale), 3, 3,
            env.road(float(x), float(z)) ? Color{61, 71, 78, 255} : y < 1.3f ? Color{213, 193, 135, 255} : Color{104, 147, 103, 255});
    }
    const Vector2 car_dot{left + 100 + car.position().GetX() * scale, top + 100 + car.position().GetZ() * scale};
    DrawRectangle(int(car_dot.x - 3), int(car_dot.y - 3), 6, 6, SKYBLUE);
    const Vector2 dot{left + 100 + player_position.GetX() * scale, top + 100 + player_position.GetZ() * scale};
    DrawCircleV(dot, 4, ORANGE);
    DrawLineEx(dot, {dot.x + player_forward.GetX() * 12, dot.y + player_forward.GetZ() * 12}, 2, RAYWHITE);
    DrawText("COASTAL CITY  /  N", int(left + 5), int(top + 203), 12, RAYWHITE);
}
} // namespace forza
