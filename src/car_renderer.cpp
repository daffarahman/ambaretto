#include "car_renderer.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace forza {
namespace {
// raylib bakes the GLB node translation into the imported vertices. Undo that
// export pivot before mirroring the half-body about its original X = 0 seam.
constexpr Vector3 export_translation{0.055822067f, -0.075678185f, 0.068677470f};
constexpr float front_axle_z = 1.19f, rear_axle_z = -1.40f;
constexpr float axle_y = -0.145f;
constexpr float scale = 2.5f / (front_axle_z - rear_axle_z);
constexpr float axle_midpoint = (front_axle_z + rear_axle_z) / 2;
constexpr float resting_axle_y = -0.15f; // Relative to the physics center of mass.

std::string model_path(const char* filename) {
    const std::string relative = std::string("assets/models/") + filename;
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    return FileExists(bundled.c_str()) ? bundled : relative;
}

Vector3 fit_position(Vector3 p, bool mirrored) {
    p = Vector3Subtract(p, export_translation);
    // The asset faces +Z; the physics car faces -Z.
    return {(mirrored ? p.x : -p.x) * scale,
            (p.y - axle_y) * scale + resting_axle_y,
            -(p.z - axle_midpoint) * scale};
}

Mesh complete_body(const Mesh& source) {
    Mesh mesh{};
    // Expand the indexed primitives, duplicating each triangle for the other
    // half. Reversing mirrored winding and normals keeps lighting/culling valid.
    mesh.vertexCount = source.triangleCount * 6;
    mesh.triangleCount = source.triangleCount * 2;
    mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
    if (source.colors) mesh.colors = static_cast<unsigned char*>(MemAlloc(mesh.vertexCount * 4));
    for (int half = 0; half < 2; ++half) {
        for (int triangle = 0; triangle < source.triangleCount; ++triangle) {
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle * 3 + (half == 1 ? 2 - corner : corner);
                const int src = source.indices ? source.indices[index] : index;
                const int dst = half * source.triangleCount * 3 + triangle * 3 + corner;
                const Vector3 p = fit_position({source.vertices[src * 3], source.vertices[src * 3 + 1],
                                               source.vertices[src * 3 + 2]}, half == 1);
                mesh.vertices[dst * 3] = p.x;
                mesh.vertices[dst * 3 + 1] = p.y;
                mesh.vertices[dst * 3 + 2] = p.z;
                const Vector3 n = source.normals
                    ? Vector3{source.normals[src * 3], source.normals[src * 3 + 1], source.normals[src * 3 + 2]}
                    : Vector3{0, 1, 0};
                mesh.normals[dst * 3] = half == 1 ? n.x : -n.x;
                mesh.normals[dst * 3 + 1] = n.y;
                mesh.normals[dst * 3 + 2] = -n.z;
                for (int uv = 0; uv < 2; ++uv)
                    mesh.texcoords[dst * 2 + uv] = source.texcoords ? source.texcoords[src * 2 + uv] : 0;
                if (source.colors) std::memcpy(mesh.colors + dst * 4, source.colors + src * 4, 4);
            }
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

const char* vertex_shader = R"glsl(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 worldPosition;
out vec3 normal;
out vec2 texCoord;
out vec4 color;
void main() {
    worldPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    normal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    texCoord = vertexTexCoord;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})glsl";
const std::string fragment_shader = std::string(R"glsl(#version 330
in vec3 worldPosition;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 cameraPosition;
uniform vec3 emissionColor;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform vec3 horizonColor;
uniform float daylight;
)glsl") + scene_lighting_glsl() + R"glsl(
out vec4 finalColor;
void main() {
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    vec3 light = sunDirection;
    vec3 view = normalize(cameraPosition - worldPosition);
    float shadow = scene_shadow(worldPosition, n, light);
    vec3 lighting = ambientLight + sunColor * (0.42 * daylight * max(dot(n, light), 0.0) * shadow);
    lighting += scene_local_light(worldPosition, n, view, 48.0);
    float highlight = pow(max(dot(n, normalize(light + view)), 0.0), 48.0) * 0.18 * daylight * shadow;
    vec4 surface = texture(texture0, texCoord) * colDiffuse * color;
    vec3 shaded = surface.rgb * lighting + vec3(highlight) + emissionColor * 0.85;
    float fog = smoothstep(viewDistance * 0.65, viewDistance, distance(cameraPosition.xz, worldPosition.xz));
    finalColor = vec4(mix(shaded, horizonColor, fog) * brightness, surface.a);
})glsl";
const char* damage_fragment = R"glsl(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform float effectTime;
uniform int fire;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1, 0)), f.x),
               mix(hash(cell + vec2(0, 1)), hash(cell + vec2(1, 1)), f.x), f.y);
}
void main() {
    vec2 p = fragTexCoord * 2.0 - 1.0;
    vec2 flow = p * 3.0 + vec2(effectTime * 0.3, -effectTime * 0.7);
    float cloud = noise(flow) * 0.6 + noise(flow * 2.0) * 0.3 + noise(flow * 4.0) * 0.1;
    float edge = 1.0 - smoothstep(0.25, 1.0, length(p) + (cloud - 0.5) * 0.18);
    vec3 color = fragColor.rgb * mix(0.65, 1.15, cloud);
    if (fire == 1) {
        float core = 1.0 - smoothstep(0.0, 0.8, length(p));
        color = mix(vec3(1.0, 0.18, 0.015), vec3(1.0, 0.9, 0.45), core * cloud) * fragColor.rgb;
    }
    finalColor = vec4(color, fragColor.a * edge * mix(0.65, 1.0, cloud));
})glsl";
} // namespace

CarRenderer::CarRenderer() {
    shader_ = LoadShaderFromMemory(vertex_shader, fragment_shader.c_str());
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
    camera_location_ = GetShaderLocation(shader_, "cameraPosition");
    emission_location_ = GetShaderLocation(shader_, "emissionColor");
    damage_shader_ = LoadShaderFromMemory(nullptr, damage_fragment);
    damage_time_location_ = GetShaderLocation(damage_shader_, "effectTime");
    damage_fire_location_ = GetShaderLocation(damage_shader_, "fire");
    load_body();
    load_wheel();
}

void CarRenderer::load_body() {
    const std::string path = model_path("trueno.glb");
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "CAR: Trueno body missing; using the procedural body");
        return;
    }
    body_ = LoadModel(path.c_str());
    if (body_.meshCount != 9 || !body_.meshes || !body_.materials || !body_.meshMaterial) {
        TraceLog(LOG_WARNING, "CAR: Unexpected Trueno mesh layout; using the procedural body");
        return;
    }
    // The export leaves paint, trim and glass at default white. Give those
    // slots the Trueno's white paint, dark trim and tinted glass.
    constexpr Color palette[9] = {{235, 235, 224, 255}, {32, 35, 39, 255},
        {236, 146, 41, 255}, {157, 163, 166, 255}, BLACK,
        {43, 66, 79, 255}, BLACK, {27, 30, 34, 255}, BLACK};
    // raylib only imports emissiveFactor when there is an emission texture.
    // This GLB has no textures, so restore its three lamp factors explicitly.
    constexpr Color lamp_colors[9] = {BLANK, BLANK, BLANK, BLANK,
        {240, 255, 128, 255}, BLANK, {255, 189, 27, 255}, BLANK, {255, 0, 1, 255}};
    for (int i = 0; i < body_.meshCount; ++i) {
        Mesh complete = complete_body(body_.meshes[i]);
        UnloadMesh(body_.meshes[i]);
        body_.meshes[i] = complete;
        auto& material = body_.materials[body_.meshMaterial[i]];
        material.shader = shader_;
        if (i != 4 && i != 6 && i != 8) material.maps[MATERIAL_MAP_DIFFUSE].color = palette[i];
        material.maps[MATERIAL_MAP_EMISSION].color = lamp_colors[i];
    }
    ready_ = true;
    TraceLog(LOG_INFO, "CAR: Loaded complete mirrored Trueno body, aligned to existing wheels");
}

void CarRenderer::load_wheel() {
    const std::string path = model_path("trueno-wheel.glb");
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "CAR: Trueno wheel missing; using the procedural wheels");
        return;
    }
    wheel_ = LoadModel(path.c_str());
    if (wheel_.meshCount != 3 || !wheel_.meshes || !wheel_.materials || !wheel_.meshMaterial) {
        TraceLog(LOG_WARNING, "CAR: Unexpected Trueno wheel layout; using the procedural wheels");
        return;
    }
    for (int i = 0; i < wheel_.meshCount; ++i) {
        const auto& mesh = wheel_.meshes[i];
        const int material = wheel_.meshMaterial[i];
        if (!mesh.vertices || mesh.vertexCount <= 0 || material < 0 || material >= wheel_.materialCount) {
            TraceLog(LOG_WARNING, "CAR: Invalid Trueno wheel geometry; using the procedural wheels");
            return;
        }
    }
    // The GLB was exported at a rear wheel's position. Recenter the entire
    // assembly, then fit its circular YZ cross-section to the raycast radius.
    const auto bounds = GetModelBoundingBox(wheel_);
    const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    float radius = 0;
    for (int i = 0; i < wheel_.meshCount; ++i) {
        const auto& mesh = wheel_.meshes[i];
        for (int v = 0; v < mesh.vertexCount; ++v)
            radius = std::max(radius, std::hypot(mesh.vertices[v * 3 + 1] - center.y,
                                               mesh.vertices[v * 3 + 2] - center.z));
    }
    if (!std::isfinite(radius) || radius < 0.0001f) {
        TraceLog(LOG_WARNING, "CAR: Invalid Trueno wheel radius; using the procedural wheels");
        return;
    }
    const float wheel_scale = wheel_radius / radius;
    // Rim, tire, and tread slots are default white in this export.
    constexpr Color wheel_colors[3] = {{186, 193, 201, 255}, {22, 24, 27, 255}, {37, 39, 43, 255}};
    for (int i = 0; i < wheel_.meshCount; ++i) {
        auto& mesh = wheel_.meshes[i];
        for (int v = 0; v < mesh.vertexCount; ++v) {
            mesh.vertices[v * 3] = (mesh.vertices[v * 3] - center.x) * wheel_scale;
            mesh.vertices[v * 3 + 1] = (mesh.vertices[v * 3 + 1] - center.y) * wheel_scale;
            mesh.vertices[v * 3 + 2] = (mesh.vertices[v * 3 + 2] - center.z) * wheel_scale;
        }
        UpdateMeshBuffer(mesh, 0, mesh.vertices, mesh.vertexCount * 3 * sizeof(float), 0);
        auto& material = wheel_.materials[wheel_.meshMaterial[i]];
        material.shader = shader_;
        material.maps[MATERIAL_MAP_DIFFUSE].color = wheel_colors[i];
        material.maps[MATERIAL_MAP_EMISSION].color = BLANK;
    }
    wheel_ready_ = true;
    TraceLog(LOG_INFO, "CAR: Loaded Trueno wheel, radius %.3f m, width %.3f m", wheel_radius,
             (bounds.max.x - bounds.min.x) * wheel_scale);
}

CarRenderer::~CarRenderer() {
    if (body_.meshes || body_.materials) UnloadModel(body_);
    if (wheel_.meshes || wheel_.materials) UnloadModel(wheel_);
    if (shader_.id != 0) UnloadShader(shader_);
    if (damage_shader_.id != 0) UnloadShader(damage_shader_);
}

void CarRenderer::draw_damage(const Camera3D& camera, Vec3 center, bool wreck, float age, float size, float time) const {
    if (wreck && age >= 8) return;
    const Texture2D white{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    const Vector3 direction = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(direction, camera.up));
    const Vector3 up = Vector3CrossProduct(right, direction);
    const float phase = wreck ? age : time;
    const auto puff = [&](Vec3 p, float radius, Color tint) {
        DrawBillboardPro(camera, white, {0, 0, 1, 1}, {p.GetX(), p.GetY(), p.GetZ()}, up,
            {radius * 2, radius * 2}, {.5f, .5f}, 0, tint);
    };
    BeginShaderMode(damage_shader_);
    SetShaderValue(damage_shader_, damage_time_location_, &phase, SHADER_UNIFORM_FLOAT);
    // Keep opaque scene occlusion, but let translucent puffs overlap without cutting each other off.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    int fire = 1;
    if (wreck && age < .8f) {
        SetShaderValue(damage_shader_, damage_fire_location_, &fire, SHADER_UNIFORM_INT);
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * .78539816f;
            const Vec3 p = center + Vec3(std::cos(angle), .5f, std::sin(angle)) * (age * 4 * size);
            puff(p, (.6f + age * 2.5f) * size, Fade(WHITE, .45f * std::pow(1 - age / .8f, 2)));
        }
        EndBlendMode();
    }
    fire = 0;
    SetShaderValue(damage_shader_, damage_fire_location_, &fire, SHADER_UNIFORM_INT);
    struct Smoke { Vec3 p; float radius, alpha; };
    std::array<Smoke, 6> smoke;
    for (int i = 0; i < int(smoke.size()); ++i) {
        const float rise = std::fmod(phase * .7f + i / 6.f, 1.f), angle = i * 2.4f;
        smoke[i] = {center + Vec3(std::cos(angle) * rise, .7f + rise * 4, std::sin(angle) * rise) * size,
            (.25f + rise * (wreck ? 1.3f : .6f)) * size,
            std::sin(rise * PI) * .65f * (wreck ? 1 - age / 8 : 1)};
    }
    const Vec3 eye(camera.position.x, camera.position.y, camera.position.z);
    std::sort(smoke.begin(), smoke.end(), [&](const Smoke& a, const Smoke& b) { return (a.p - eye).LengthSq() > (b.p - eye).LengthSq(); });
    for (const auto& s : smoke) puff(s.p, s.radius, Fade(wreck ? Color{48, 44, 40, 255} : GRAY, s.alpha));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndShaderMode();
}

bool CarRenderer::draw_body(const Car& car, const Camera3D& camera, Color paint, Shader override_shader) const {
    if (!ready_) return false;
    const auto q = car.rotation();
    const auto p = car.position();
    const Matrix transform = MatrixMultiply(QuaternionToMatrix({q.GetX(), q.GetY(), q.GetZ(), q.GetW()}),
        MatrixTranslate(p.GetX(), p.GetY(), p.GetZ()));
    draw_model(body_, transform, camera, paint, override_shader, !car.destroyed());
    return true;
}

bool CarRenderer::draw_wheel(const Car& car, const Wheel& wheel, const Camera3D& camera, Shader override_shader) const {
    if (!wheel_ready_) return false;
    // Orient the detailed rim outward on each side. Spin about chassis +X
    // after this turn so both sides roll in the same physical direction.
    const Quat side = Quat::sRotation(Vec3::sAxisY(), wheel.mount.GetX() < 0 ? PI : 0);
    const Quat spin = Quat::sRotation(Vec3::sAxisX(), -std::remainder(wheel.spin, 2 * PI));
    const Quat steering = Quat::sRotation(Vec3::sAxisY(), wheel.front ? car.steering() : 0);
    const Quat q = car.rotation() * steering * spin * side;
    const auto p = car.wheel_center(wheel);
    const float size = car.tuning().wheel_radius / wheel_radius;
    const Matrix transform = MatrixMultiply(MatrixMultiply(MatrixScale(size, size, size),
        QuaternionToMatrix({q.GetX(), q.GetY(), q.GetZ(), q.GetW()})), MatrixTranslate(p.GetX(), p.GetY(), p.GetZ()));
    draw_model(wheel_, transform, camera, BLANK, override_shader, !car.destroyed());
    return true;
}

void CarRenderer::draw_model(const Model& model, const Matrix& transform, const Camera3D& camera, Color paint, Shader override_shader, bool intact) const {
    if (!override_shader.id) SetShaderValue(shader_, camera_location_, &camera.position, SHADER_UNIFORM_VEC3);
    // Both source GLBs mark every material as double-sided.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int i = 0; i < model.meshCount; ++i) {
        auto& material = model.materials[model.meshMaterial[i]];
        const Shader original_shader = material.shader;
        if (override_shader.id) material.shader = override_shader;
        const Color original = material.maps[MATERIAL_MAP_DIFFUSE].color;
        if (!intact) material.maps[MATERIAL_MAP_DIFFUSE].color = {18, 18, 18, 255};
        else if (i == 0 && paint.a != 0) material.maps[MATERIAL_MAP_DIFFUSE].color = paint;
        const auto color = intact ? material.maps[MATERIAL_MAP_EMISSION].color : BLANK;
        const Vector3 emission{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
        if (!override_shader.id) SetShaderValue(shader_, emission_location_, &emission, SHADER_UNIFORM_VEC3);
        DrawMesh(model.meshes[i], material, transform);
        material.maps[MATERIAL_MAP_DIFFUSE].color = original;
        material.shader = original_shader;
    }
    rlEnableBackfaceCulling();
}
} // namespace forza
