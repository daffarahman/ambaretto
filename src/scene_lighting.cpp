#include "scene_lighting.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace ambaretto {
namespace {
const char* depth_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 texCoord;
out vec4 color;
void main() {
    texCoord = vertexTexCoord;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})GLSL";
const char* depth_fragment = R"GLSL(#version 330
in vec2 texCoord;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    if (texture(texture0, texCoord).a * colDiffuse.a * color.a < 0.3) discard;
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
})GLSL";

void scalar(Shader shader, const char* name, float value) {
    const int location = GetShaderLocation(shader, name);
    if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_FLOAT);
}
void integer(Shader shader, const char* name, int value) {
    const int location = GetShaderLocation(shader, name);
    if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
}
void vector(Shader shader, const char* name, Vector3 value) {
    const int location = GetShaderLocation(shader, name);
    if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_VEC3);
}
} // namespace

const char* scene_lighting_glsl() {
    return R"GLSL(
uniform sampler2D shadowMap;
uniform mat4 shadowMatrix;
uniform vec3 shadowCenter;
uniform float shadowDistance;
uniform float shadowTexel;
uniform float shadowDepth;
uniform float shadowSunFade;
uniform int shadowEnabled;
uniform int softShadows;
uniform float brightness;
uniform float viewDistance;
uniform int lightCount;
uniform vec4 pointLights[16];
uniform vec4 lightColors[16];
uniform vec4 lightDirections[16];

float shadow_sample(vec2 uv, float receiverDepth, vec2 receiverUV, vec2 gradient) {
    vec2 center = (floor(uv / shadowTexel) + 0.5) * shadowTexel;
    float depth = texture(shadowMap, center).r;
    return step(receiverDepth + dot(center - receiverUV, gradient), depth);
}
float scene_shadow(vec3 position, vec3 normal, vec3 sunDirection) {
    if (shadowEnabled == 0) return 1.0;
    float fade = 1.0 - smoothstep(shadowDistance * 0.75, shadowDistance, distance(position.xz, shadowCenter.xz));
    if (fade <= 0.0) return 1.0;
    vec4 projection = shadowMatrix * vec4(position, 1.0);
    vec3 samplePosition = projection.xyz / projection.w * 0.5 + 0.5;
    // Compare each PCF tap against the receiver plane at that texel, not the pixel's depth.
    vec3 dx = dFdx(samplePosition), dy = dFdy(samplePosition);
    float determinant = dx.x * dy.y - dx.y * dy.x;
    vec2 gradient = abs(determinant) > 1e-12 ? vec2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) / determinant : vec2(0.0);
    if (samplePosition.z <= 0.0 || samplePosition.z >= 1.0 || any(lessThan(samplePosition.xy, vec2(0.0))) || any(greaterThan(samplePosition.xy, vec2(1.0)))) return 1.0;
    float bias = (0.015 + 0.015 * (1.0 - max(dot(normalize(normal), sunDirection), 0.0))) / shadowDepth;
    float depth = samplePosition.z - bias;
    float visibility = 0.0;
    if (softShadows != 0) {
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                visibility += shadow_sample(samplePosition.xy + vec2(x, y) * shadowTexel, depth, samplePosition.xy, gradient);
        visibility /= 9.0;
    } else visibility = shadow_sample(samplePosition.xy, depth, samplePosition.xy, gradient);
    vec2 edge = min(samplePosition.xy, 1.0 - samplePosition.xy);
    fade *= smoothstep(0.0, shadowTexel * 2.0, min(edge.x, edge.y));
    return mix(1.0, visibility, fade * shadowSunFade);
}
vec3 scene_local_light(vec3 position, vec3 normal, vec3 view, float shininess) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < lightCount; ++i) {
        vec3 delta = pointLights[i].xyz - position;
        float distanceSquared = dot(delta, delta);
        float radius = pointLights[i].w;
        if (distanceSquared >= radius * radius) continue;
        float distanceToLight = sqrt(max(distanceSquared, 0.0001));
        vec3 direction = delta / distanceToLight;
        float attenuation = pow(1.0 - distanceToLight / radius, 2.0);
        if (lightDirections[i].w > -0.99) {
            float cone = lightDirections[i].w;
            attenuation *= smoothstep(cone, min(cone + 0.10, 0.999), dot(-direction, lightDirections[i].xyz));
        }
        float diffuse = max(dot(normal, direction), 0.0);
        float specular = shininess > 0.0 ? pow(max(dot(normal, normalize(direction + view)), 0.0), shininess) * 0.30 : 0.0;
        result += lightColors[i].rgb * attenuation * (diffuse + specular);
    }
    return result;
}
)GLSL";
}

SceneLighting::SceneLighting() {
    shadow_shader_ = LoadShaderFromMemory(depth_vertex, depth_fragment);
    if (!IsShaderValid(shadow_shader_) || shadow_shader_.id == rlGetShaderIdDefault()) {
        shadow_shader_ = {};
        warning_ = "Shadows unavailable: the graphics driver could not compile the shadow shader.";
        TraceLog(LOG_WARNING, "%s", warning_);
    }
}

SceneLighting::~SceneLighting() {
    if (shadow_active_) end_shadow();
    if (shadow_map_.id) UnloadRenderTexture(shadow_map_);
    if (shadow_shader_.id) UnloadShader(shadow_shader_);
}

void SceneLighting::set_lights(const SceneLight* lights, int count) {
    light_count_ = 0;
    if (!lights) return;
    for (int i = 0; i < count && light_count_ < max_lights; ++i) {
        SceneLight light = lights[i];
        if (!std::isfinite(light.radius) || light.radius <= 0) continue;
        light.direction = Vector3Normalize(light.direction);
        lights_[light_count_++] = light;
    }
}

bool SceneLighting::begin_shadow(Vector3 focus, const Daylight& daylight, const GraphicsSettings& settings) {
    shadow_rendered_ = false;
    const int size = settings.shadow_resolution();
    if (!size) {
        if (shadow_map_.id) UnloadRenderTexture(shadow_map_);
        shadow_map_ = {};
        requested_size_ = 0;
        if (shadow_shader_.id) warning_ = nullptr;
        return false;
    }
    if (!shadow_shader_.id || daylight.day <= 0.001f || daylight.sun_direction.y <= 0.03f) return false;
    if (requested_size_ != size) {
        if (shadow_map_.id) UnloadRenderTexture(shadow_map_);
        shadow_map_ = LoadRenderTexture(size, size);
        requested_size_ = size;
        if (IsRenderTextureValid(shadow_map_)) {
            const unsigned int texture = rlLoadTexture(nullptr, size, size, PIXELFORMAT_UNCOMPRESSED_R32, 1);
            if (texture) {
                rlFramebufferAttach(shadow_map_.id, texture, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
                UnloadTexture(shadow_map_.texture);
                shadow_map_.texture = {texture, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R32};
            }
        }
        if (!IsRenderTextureValid(shadow_map_) || shadow_map_.texture.format != PIXELFORMAT_UNCOMPRESSED_R32 || !rlFramebufferComplete(shadow_map_.id)) {
            if (shadow_map_.id) UnloadRenderTexture(shadow_map_);
            shadow_map_ = {};
            warning_ = "Shadows unavailable at this resolution. Try a lower shadow quality.";
            TraceLog(LOG_WARNING, "%s", warning_);
        } else {
            SetTextureFilter(shadow_map_.texture, TEXTURE_FILTER_POINT);
            SetTextureWrap(shadow_map_.texture, TEXTURE_WRAP_CLAMP);
            warning_ = nullptr;
        }
    }
    if (!shadow_map_.id) return false;
    // ponytail: one nearby sun map; use cascades if distant shadows are needed.
    shadow_distance_ = std::clamp(settings.shadow_distance, 30.0f, 180.0f);
    shadow_depth_ = shadow_distance_ * 6 + 500;
    shadow_center_ = focus;
    const Vector3 sun = Vector3Normalize(daylight.sun_direction);
    // Stabilize the map on whole light-space texels rather than swimming with the camera.
    const Vector3 up = std::abs(sun.y) > 0.98f ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(up, sun));
    const Vector3 light_up = Vector3Normalize(Vector3CrossProduct(sun, right));
    const float texel = 2 * shadow_distance_ / size;
    Vector3 target{focus.x, focus.y + 25, focus.z};
    for (Vector3 axis : {right, light_up}) {
        const float projection = Vector3DotProduct(target, axis);
        target = Vector3Add(target, Vector3Scale(axis, std::round(projection / texel) * texel - projection));
    }
    Camera3D camera{};
    camera.target = target;
    camera.position = Vector3Add(target, Vector3Scale(sun, shadow_depth_ * 0.5f));
    camera.up = up;
    camera.fovy = shadow_distance_ * 2;
    camera.projection = CAMERA_ORTHOGRAPHIC;
    previous_near_ = rlGetCullDistanceNear();
    previous_far_ = rlGetCullDistanceFar();
    BeginTextureMode(shadow_map_);
    ClearBackground(WHITE);
    rlSetClipPlanes(0.1, shadow_depth_);
    BeginMode3D(camera);
    shadow_matrix_ = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    BeginShaderMode(shadow_shader_);
    rlDisableColorBlend();
    rlEnableDepthMask();
    shadow_active_ = true;
    return true;
}

void SceneLighting::end_shadow() {
    if (!shadow_active_) return;
    rlDrawRenderBatchActive();
    EndShaderMode();
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(previous_near_, previous_far_);
    rlEnableColorBlend();
    shadow_active_ = false;
    shadow_rendered_ = true;
}

void SceneLighting::apply(Shader shader, const Camera3D& camera, const Daylight& daylight, const GraphicsSettings& settings) const {
    if (!IsShaderValid(shader)) return;
    apply_daylight(shader, daylight);
    vector(shader, "cameraPosition", camera.position);
    scalar(shader, "brightness", settings.brightness);
    scalar(shader, "viewDistance", settings.view_distance);
    const bool shadows = settings.shadows > 0 && shadow_rendered_ && shadow_map_.id;
    integer(shader, "shadowEnabled", shadows ? 1 : 0);
    if (shadows) {
        const int location = GetShaderLocation(shader, "shadowMatrix");
        if (location >= 0) SetShaderValueMatrix(shader, location, shadow_matrix_);
        vector(shader, "shadowCenter", shadow_center_);
        scalar(shader, "shadowDistance", shadow_distance_);
        scalar(shader, "shadowTexel", 1.0f / shadow_map_.texture.width);
        scalar(shader, "shadowDepth", shadow_depth_);
        scalar(shader, "shadowSunFade", std::clamp((daylight.sun_direction.y - 0.03f) / 0.10f, 0.0f, 1.0f));
        integer(shader, "softShadows", settings.soft_shadows ? 1 : 0);
        // raylib's batch samplers reset on flush. A reserved slot also works for native DrawMesh.
        integer(shader, "shadowMap", 15);
        rlActiveTextureSlot(15);
        rlEnableTexture(shadow_map_.texture.id);
        rlActiveTextureSlot(0);
    }
    const int count = settings.local_lights ? light_count_ : 0;
    integer(shader, "lightCount", count);
    if (count) {
        std::array<Vector4, max_lights> positions{}, colors{}, directions{};
        for (int i = 0; i < count; ++i) {
            const auto& light = lights_[i];
            positions[i] = {light.position.x, light.position.y, light.position.z, light.radius};
            colors[i] = {light.color.x, light.color.y, light.color.z, 1};
            directions[i] = {light.direction.x, light.direction.y, light.direction.z, light.cone};
        }
        for (const auto& uniform : {std::pair<const char*, const Vector4*>{"pointLights", positions.data()},
                {"lightColors", colors.data()}, {"lightDirections", directions.data()}}) {
            const int location = GetShaderLocation(shader, uniform.first);
            if (location >= 0) SetShaderValueV(shader, location, uniform.second, SHADER_UNIFORM_VEC4, count);
        }
    }
}
} // namespace ambaretto
