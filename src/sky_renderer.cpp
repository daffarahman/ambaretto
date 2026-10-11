#include "sky_renderer.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace ambaretto {
namespace {
const char* sky_fragment = R"GLSL(#version 330
uniform vec2 screenSize;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float tanFov;
uniform vec3 horizonColor;
uniform vec3 zenithColor;
uniform vec3 sunDirection;
uniform float night;
uniform vec3 sunColor;
uniform float time;
uniform float brightness;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    vec2 uv = gl_FragCoord.xy / screenSize * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward + cameraRight * uv.x * tanFov * screenSize.x / screenSize.y + cameraUp * uv.y * tanFov);
    float height = max(ray.y, 0.0);
    vec3 sky = mix(horizonColor, zenithColor, pow(clamp(height / 0.65, 0.0, 1.0), 0.55));
    float sun = max(dot(ray, sunDirection), 0.0);
    float above = smoothstep(-0.035, 0.035, sunDirection.y);
    sky += sunColor * pow(sun, 20.0) * 0.18 * above;
    sky = mix(sky, mix(sunColor, vec3(1.0), 0.45), smoothstep(cos(0.020), cos(0.016), sun) * above);
    float moon = smoothstep(cos(0.013), cos(0.009), dot(ray, -sunDirection));
    sky = mix(sky, vec3(0.79, 0.86, 1.0), moon * night);
    vec2 stars = vec2(atan(ray.z, ray.x) / 6.2831853 + 0.5, asin(clamp(ray.y, -1.0, 1.0)) / 3.1415927 + 0.5) * vec2(600, 300);
    float seed = hash(floor(stars));
    float star = (1.0 - smoothstep(0.03, 0.14, length(fract(stars) - 0.5))) * step(0.994, seed);
    sky += vec3(0.74, 0.82, 1.0) * star * night * smoothstep(0.03, 0.18, ray.y) * (0.70 + 0.30 * sin(time * 1.2 + seed * 31));
    finalColor = vec4(sky * brightness, 1.0);
})GLSL";

const char* cloud_fragment = R"GLSL(#version 330
uniform sampler2D texture0;
uniform vec2 screenSize;
uniform vec3 cameraPosition;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float tanFov;
uniform vec3 horizonColor;
uniform vec3 ambientLight;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float night;
uniform float time;
uniform float brightness;
out vec4 finalColor;

float noise(vec3 p) {
    vec3 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    // R/G store adjacent Z slices; bilinear filtering supplies the other four corners.
    vec2 uv = (cell.xy + vec2(19.0, 47.0) * cell.z + f.xy + 0.5) / 128.0;
    vec2 slices = textureLod(texture0, uv, 0.0).rg;
    return mix(slices.r, slices.g, f.z);
}
float density(vec3 position) {
    float height = (position.y - 400.0) / 300.0;
    if (height <= 0.0 || height >= 1.0) return 0.0;
    vec3 p = position * vec3(0.0035, 0.007, 0.0035) + vec3(time * 0.009, 0.0, time * 0.003);
    float body = noise(p) * 0.72 + noise(p * 2.07) * 0.28 - 0.45;
    if (body <= 0.02) return 0.0;
    body = max(body - (1.0 - noise(p * 4.11)) * 0.14, 0.0);
    float profile = smoothstep(0.0, 0.14, height) * (1.0 - smoothstep(0.45, 1.0, height));
    return body * profile * 0.055;
}
void main() {
    finalColor = vec4(0.0);
    vec2 uv = gl_FragCoord.xy / screenSize * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward + cameraRight * uv.x * tanFov * screenSize.x / screenSize.y + cameraUp * uv.y * tanFov);
    float start = 0.0, end = 6000.0;
    if (abs(ray.y) < 0.0001) {
        if (cameraPosition.y <= 400.0 || cameraPosition.y >= 700.0) return;
    } else {
        vec2 hits = (vec2(400.0, 700.0) - cameraPosition.y) / ray.y;
        start = max(0.0, min(hits.x, hits.y));
        end = min(end, max(hits.x, hits.y));
    }
    if (end <= start) return;
    float fade = 1.0 - smoothstep(3500.0, 6000.0, start);
    if (fade <= 0.0) return;
    float stepLength = (end - start) / 16.0;
    float transmittance = 1.0;
    vec3 accumulated = vec3(0.0);
    vec3 lightDirection = normalize(night < 0.5 ? sunDirection : -sunDirection);
    vec3 directColor = mix(sunColor, vec3(0.52, 0.65, 0.85), night);
    float directStrength = mix(0.78, 0.10, night);
    float glow = pow(max(dot(ray, lightDirection), 0.0), 8.0) * 0.22 * (1.0 - night);
    for (int i = 0; i < 16; ++i) {
        float distance = start + (float(i) + 0.5) * stepLength;
        vec3 position = cameraPosition + ray * distance;
        float d = density(position);
        if (d <= 0.0001) continue;
        float lightDepth = density(position + lightDirection * 80.0) * 80.0;
        if (lightDepth > 0.01) lightDepth += density(position + lightDirection * 180.0) * 120.0;
        float lit = exp(-lightDepth);
        float height = clamp((position.y - 400.0) / 300.0, 0.0, 1.0);
        vec3 color = ambientLight * mix(0.5, 0.8, height) + directColor * lit * (directStrength + glow);
        color = mix(color, horizonColor, smoothstep(1800.0, 6000.0, distance) * 0.65);
        float opacity = 1.0 - exp(-d * stepLength);
        accumulated += color * opacity * transmittance;
        transmittance *= 1.0 - opacity;
        if (transmittance < 0.03) break;
    }
    finalColor = vec4(accumulated * fade * brightness, (1.0 - transmittance) * fade);
})GLSL";
bool supported(Shader shader) { return IsShaderValid(shader) && shader.id != rlGetShaderIdDefault(); }
}

SkyRenderer::SkyRenderer() {
    sky_ = LoadShaderFromMemory(nullptr, sky_fragment);
    clouds_ = LoadShaderFromMemory(nullptr, cloud_fragment);
    if (!supported(clouds_)) {
        TraceLog(LOG_WARNING, "SKY: Volumetric cloud shader unavailable; retaining clear sky");
        return;
    }
    Image image = GenImageColor(128, 128, BLACK);
    const auto value = [](std::uint32_t n) {
        n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
        return static_cast<unsigned char>(n & 255);
    };
    auto* pixels = static_cast<Color*>(image.data);
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        pixels[y * 128 + x] = {value(y * 128 + x), value(((y + 47) & 127) * 128 + ((x + 19) & 127)), 0, 255};
    noise_ = LoadTextureFromImage(image); UnloadImage(image);
    SetTextureFilter(noise_, TEXTURE_FILTER_BILINEAR); SetTextureWrap(noise_, TEXTURE_WRAP_REPEAT);
}
SkyRenderer::~SkyRenderer() {
    if (layer_.id) UnloadRenderTexture(layer_);
    if (noise_.id) UnloadTexture(noise_);
    if (supported(clouds_)) UnloadShader(clouds_);
    if (supported(sky_)) UnloadShader(sky_);
}
void SkyRenderer::draw(const Camera3D& camera, const Daylight& light, float time, const GraphicsSettings& settings) {
    const int width = GetScreenWidth(), height = GetScreenHeight();
    if (width <= 0 || height <= 0) return;
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up)), up = Vector3CrossProduct(right, forward);
    const float tan_fov = std::tan(camera.fovy * DEG2RAD / 2);
    const auto set = [&](Shader shader, Vector2 screen) {
        SetShaderValue(shader, GetShaderLocation(shader, "screenSize"), &screen, SHADER_UNIFORM_VEC2);
        for (const auto& scalar : {std::pair{"tanFov", tan_fov}, {"night", light.night}, {"time", time}, {"brightness", settings.brightness}})
            SetShaderValue(shader, GetShaderLocation(shader, scalar.first), &scalar.second, SHADER_UNIFORM_FLOAT);
        for (const auto& vector : {std::pair{"cameraForward", forward}, {"cameraRight", right}, {"cameraUp", up},
            {"horizonColor", light.horizon}, {"sunDirection", light.sun_direction}, {"sunColor", light.sun_color}})
            SetShaderValue(shader, GetShaderLocation(shader, vector.first), &vector.second, SHADER_UNIFORM_VEC3);
    };
    set(sky_, {float(GetRenderWidth()), float(GetRenderHeight())});
    SetShaderValue(sky_, GetShaderLocation(sky_, "zenithColor"), &light.zenith, SHADER_UNIFORM_VEC3);
    BeginShaderMode(sky_); DrawRectangle(0, 0, width, height, WHITE); EndShaderMode();
    if (!supported(clouds_) || !noise_.id) return;
    const float scale = std::min({0.5f, 640.f / width, 360.f / height});
    const int w = std::max(1, int(width * scale)), h = std::max(1, int(height * scale));
    if (layer_.texture.width != w || layer_.texture.height != h) {
        if (layer_.id) UnloadRenderTexture(layer_);
        layer_ = LoadRenderTexture(w, h);
        if (!IsRenderTextureValid(layer_)) {
            if (layer_.id) UnloadRenderTexture(layer_);
            layer_ = {}; return;
        }
        SetTextureFilter(layer_.texture, TEXTURE_FILTER_BILINEAR);
    }
    set(clouds_, {float(w), float(h)});
    SetShaderValue(clouds_, GetShaderLocation(clouds_, "cameraPosition"), &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(clouds_, GetShaderLocation(clouds_, "ambientLight"), &light.ambient, SHADER_UNIFORM_VEC3);
    // Premultiplied blending keeps transparent edges correct through filtering and compositing.
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    BeginTextureMode(layer_); ClearBackground(BLANK); BeginShaderMode(clouds_);
    DrawTexturePro(noise_, {0, 0, 128, 128}, {0, 0, float(w), float(h)}, {}, 0, WHITE);
    EndShaderMode(); EndTextureMode();
    // shortcut: sky backdrop only; add scene-depth compositing when cloud fog must cover foreground geometry.
    DrawTexturePro(layer_.texture, {0, 0, float(w), -float(h)}, {0, 0, float(width), float(height)}, {}, 0, WHITE);
    EndBlendMode();
}
} // namespace ambaretto
