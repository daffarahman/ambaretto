#pragma once
#include "day_night.hpp"
#include "graphics_settings.hpp"
#include <array>
#include <raylib.h>

namespace ambaretto {
struct SceneLight {
    Vector3 position{};
    Vector3 color{1, 1, 1};
    float radius = 18;
    Vector3 direction{0, -1, 0};
    float cone = -1; // Outer cone cosine; -1 is an omnidirectional lamp.
};

// Append after the fragment shader's uniforms, before main().
const char* scene_lighting_glsl();

class SceneLighting {
public:
    // ponytail: one nearby sun map and 16 unshadowed local lights; add cascades/local shadow maps if their limits become visible.
    static constexpr int max_lights = 16;
    SceneLighting();
    ~SceneLighting();
    SceneLighting(const SceneLighting&) = delete;
    SceneLighting& operator=(const SceneLighting&) = delete;
    void set_lights(const SceneLight* lights, int count);
    bool begin_shadow(Vector3 focus, const Daylight& daylight, const GraphicsSettings& settings);
    void end_shadow();
    Shader shadow_shader() const { return shadow_shader_; }
    void apply(Shader shader, const Camera3D& camera, const Daylight& daylight, const GraphicsSettings& settings) const;
    const char* warning() const { return warning_; }
private:
    Shader shadow_shader_{};
    RenderTexture2D shadow_map_{};
    Matrix shadow_matrix_{};
    Vector3 shadow_center_{};
    std::array<SceneLight, max_lights> lights_{};
    int light_count_ = 0, requested_size_ = 0;
    float shadow_distance_ = 90, shadow_depth_ = 1040;
    double previous_near_ = 0.2, previous_far_ = 30000;
    bool shadow_active_ = false, shadow_rendered_ = false;
    const char* warning_ = nullptr;
};
} // namespace ambaretto
