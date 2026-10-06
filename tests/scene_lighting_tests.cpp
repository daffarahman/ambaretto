#include "scene_lighting.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 480, "Lighting checks");
    require(IsWindowReady(), "A graphics context is required for lighting checks");
    const Camera3D camera{{7, 6, 8}, {0, 0, 0}, {0, 1, 0}, 55, CAMERA_PERSPECTIVE};
    auto daylight = ambaretto::DayNight().lighting();
    daylight.sun_direction = Vector3Normalize({-.6f, .7f, .3f}); daylight.day = 1;
    ambaretto::GraphicsSettings settings;
    settings.shadows = 3; settings.shadow_distance = 30;
    const char* vertex = R"GLSL(#version 330
in vec3 vertexPosition; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; out vec3 position; out vec3 normal; out vec4 color;
void main() { position=vertexPosition; normal=vertexNormal; color=vertexColor; gl_Position=mvp*vec4(vertexPosition,1); }
)GLSL";
    std::string fragment = "#version 330\nin vec3 position; in vec3 normal; in vec4 color; uniform vec3 sunDirection; uniform vec3 cameraPosition; out vec4 finalColor;\n";
    fragment += ambaretto::scene_lighting_glsl();
    fragment += R"GLSL(
void main() {
    vec3 n=normalize(normal);
    vec3 lighting=vec3(.15)+vec3(.85)*max(dot(n,sunDirection),0.0)*scene_shadow(position,n,sunDirection);
    lighting+=scene_local_light(position,n,normalize(cameraPosition-position),0.0);
    finalColor=vec4(color.rgb*lighting*brightness,1);
})GLSL";
    const Shader shader = LoadShaderFromMemory(vertex, fragment.c_str());
    require(IsShaderValid(shader) && shader.id != rlGetShaderIdDefault(), "Lighting receiver shader must compile");
    {
        ambaretto::SceneLighting lighting;
        const auto render = [&] {
            BeginDrawing();
            if (lighting.begin_shadow({0, 0, 0}, daylight, settings)) {
                DrawCube({0, 1, 0}, 2, 2, 2, WHITE); DrawPlane({0, 0, 0}, {32, 32}, WHITE);
                lighting.end_shadow();
            } else require(settings.shadows == 0, "Shadow framebuffer and shader must be available");
            lighting.apply(shader, camera, daylight, settings);
            ClearBackground({80, 100, 130, 255});
            BeginMode3D(camera); BeginShaderMode(shader);
            DrawCube({0, 1, 0}, 2, 2, 2, {255, 180, 90, 255}); DrawPlane({0, 0, 0}, {32, 32}, {160, 180, 160, 255});
            EndShaderMode(); EndMode3D(); EndDrawing();
        };
        const auto refresh = [&] { render(); render(); };
        const auto pixel = [&](Vector3 position) {
            const Vector2 screen = GetWorldToScreen(position, camera);
            Image image = LoadImageFromScreen();
            const Color color = GetImageColor(image, int(screen.x), int(screen.y));
            UnloadImage(image);
            return int(color.r) + color.g + color.b;
        };
        refresh();
        const int shadowed = pixel({2, 0, -.7f});
        settings.shadows = 0; refresh();
        require(shadowed + 100 < pixel({2, 0, -.7f}), "Turning shadows off must remove the cube's cast shadow");
        const Vector3 sample{3, 0, 3};
        const int baseline = pixel(sample);
        ambaretto::SceneLight lamp{{3, 2, 3}, {1, 1, 1}, 8};
        lighting.set_lights(&lamp, 1); settings.local_lights = true; refresh();
        require(pixel(sample) > baseline + 100, "Nearby light must illuminate an object surface");
        settings.local_lights = false; refresh();
        require(std::abs(pixel(sample) - baseline) < 4, "Turning local lights off must remove their illumination");
        lamp.cone = .9f; lamp.direction = {0, 1, 0};
        lighting.set_lights(&lamp, 1); settings.local_lights = true; refresh();
        require(std::abs(pixel(sample) - baseline) < 4, "A surface outside the spotlight cone must stay unlit");
        lamp.direction = {0, -1, 0}; lighting.set_lights(&lamp, 1); refresh();
        require(pixel(sample) > baseline + 100, "Turning a spotlight toward the surface must illuminate it");
        settings.local_lights = false; settings.brightness = .6f; refresh();
        require(pixel(sample) < baseline * .7f, "Brightness adjustment must affect scene rendering");
        settings.brightness = 1; settings.shadows = 1; settings.soft_shadows = false; refresh();
        require(pixel({2, 0, -.7f}) + 100 < baseline, "Low resolution hard shadows must still cast a shadow");
        require(!lighting.warning(), "Changing shadow resolution must preserve GPU support");
        const Vector3 origin{2000, 0, 2000};
        const Camera3D wall_camera{{2008, 6, 2010}, {2000, 4, 2000}, {0, 1, 0}, 55, CAMERA_PERSPECTIVE};
        daylight = ambaretto::DayNight().lighting();
        settings.shadows = 3; settings.shadow_distance = 150; settings.soft_shadows = true;
        SetWindowSize(1280, 720);
        const auto wall = [&] {
            BeginDrawing();
            if (lighting.begin_shadow(origin, daylight, settings)) {
                DrawCube({2000, 4, 2000}, 16, 8, .2f, WHITE);
                lighting.end_shadow();
            }
            lighting.apply(shader, wall_camera, daylight, settings);
            ClearBackground(BLACK); BeginMode3D(wall_camera); BeginShaderMode(shader);
            DrawCube({2000, 4, 2000}, 16, 8, .2f, {160, 180, 160, 255});
            EndShaderMode(); EndMode3D(); EndDrawing();
        };
        wall(); wall();
        Image shadow_wall = LoadImageFromScreen();
        settings.shadows = 0; wall(); wall();
        Image lit_wall = LoadImageFromScreen();
        int darkest_difference = 0;
        for (int y = 20; y <= 60; ++y) for (int x = -50; x <= 50; ++x) {
            const Vector2 screen = GetWorldToScreen({2000 + float(x) / 10, float(y) / 10, 2000.1f}, wall_camera);
            const Color a = GetImageColor(shadow_wall, int(screen.x), int(screen.y)), b = GetImageColor(lit_wall, int(screen.x), int(screen.y));
            darkest_difference = std::max(darkest_difference, int(b.r) + b.g + b.b - a.r - a.g - a.b);
        }
        UnloadImage(shadow_wall); UnloadImage(lit_wall);
        std::printf("Grazing facade darkest self-shadow difference: %d\n", darkest_difference);
        require(darkest_difference <= 4, "A grazing-angle facade far from the origin must not shadow itself");
    }
    UnloadShader(shader); CloseWindow();
    std::puts("Scene shadow, light, spotlight, and brightness checks passed");
}
