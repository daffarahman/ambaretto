#include "car_renderer.hpp"
#include <raymath.h>
#include <rlgl.h>
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
const char* fragment_shader = R"glsl(#version 330
in vec3 worldPosition;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 cameraPosition;
uniform vec3 emissionColor;
out vec4 finalColor;
void main() {
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    vec3 light = normalize(vec3(-0.45, 0.85, 0.30));
    vec3 view = normalize(cameraPosition - worldPosition);
    float lighting = 0.58 + 0.42 * max(dot(n, light), 0.0);
    float highlight = pow(max(dot(n, normalize(light + view)), 0.0), 48.0) * 0.18;
    vec4 surface = texture(texture0, texCoord) * colDiffuse * color;
    vec3 shaded = surface.rgb * lighting + vec3(highlight) + emissionColor * 0.85;
    float fog = 1.0 - exp(-length(cameraPosition - worldPosition) * 0.00125);
    finalColor = vec4(mix(shaded, vec3(0.64, 0.80, 0.87), fog * 0.80), surface.a);
})glsl";
} // namespace

CarRenderer::CarRenderer() {
    const std::string relative = "assets/models/trueno.glb";
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    const std::string path = FileExists(bundled.c_str()) ? bundled : relative;
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "CAR: Trueno body missing; using the procedural body");
        return;
    }
    body_ = LoadModel(path.c_str());
    if (body_.meshCount != 9 || !body_.meshes || !body_.materials || !body_.meshMaterial) {
        TraceLog(LOG_WARNING, "CAR: Unexpected Trueno mesh layout; using the procedural body");
        return;
    }
    shader_ = LoadShaderFromMemory(vertex_shader, fragment_shader);
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
    camera_location_ = GetShaderLocation(shader_, "cameraPosition");
    emission_location_ = GetShaderLocation(shader_, "emissionColor");
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

CarRenderer::~CarRenderer() {
    if (body_.meshes || body_.materials) UnloadModel(body_);
    if (shader_.id != 0) UnloadShader(shader_);
}

bool CarRenderer::draw_body(const Car& car, const Camera3D& camera) const {
    if (!ready_) return false;
    const auto q = car.rotation();
    const auto p = car.position();
    const Matrix transform = MatrixMultiply(QuaternionToMatrix({q.GetX(), q.GetY(), q.GetZ(), q.GetW()}),
        MatrixTranslate(p.GetX(), p.GetY(), p.GetZ()));
    SetShaderValue(shader_, camera_location_, &camera.position, SHADER_UNIFORM_VEC3);
    // The source GLB marks every material as double-sided.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int i = 0; i < body_.meshCount; ++i) {
        const auto& material = body_.materials[body_.meshMaterial[i]];
        const auto color = material.maps[MATERIAL_MAP_EMISSION].color;
        const Vector3 emission{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
        SetShaderValue(shader_, emission_location_, &emission, SHADER_UNIFORM_VEC3);
        DrawMesh(body_.meshes[i], material, transform);
    }
    rlEnableBackfaceCulling();
    return true;
}
} // namespace forza
