#include "car_renderer.hpp"
#include <rlgl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(256, 256, "Vehicle shader checks");
    int result = 0;
    {
        ambaretto::CarRenderer renderer;
        const auto texture = LoadRenderTexture(256, 256);
        const Camera3D camera{{0, 2, 10}, {0, 2, 0}, {0, 1, 0}, 8, CAMERA_ORTHOGRAPHIC};
        const auto render = [&](bool wreck, float age, float time, bool covered = false) {
            BeginTextureMode(texture); ClearBackground(BLACK); BeginMode3D(camera);
            if (covered) DrawCube({0, 2, 2}, 8, 8, .2f, GREEN);
            renderer.draw_damage(camera, {0, 0, 0}, wreck, age, 1, time);
            using GetBoolean = void (*)(unsigned int, unsigned char*);
            const auto get_boolean = reinterpret_cast<GetBoolean>(rlGetProcAddress("glGetBooleanv"));
            unsigned char depth_write = 0;
            get_boolean(0x0B72, &depth_write); // GL_DEPTH_WRITEMASK
            require(depth_write, "Vehicle effects did not restore depth writes");
            EndMode3D(); EndTextureMode();
            Image image = LoadImageFromTexture(texture.texture); ImageFlipVertical(&image); return image;
        };
        const auto pixel = [&](Image image, Vector3 p) {
            const auto screen = GetWorldToScreenEx(p, camera, 256, 256);
            return GetImageColor(image, int(screen.x), int(screen.y));
        };
        try {
            Image fire = render(true, 0, 0);
            const Color core = pixel(fire, {0, 0, 0}), edge = pixel(fire, {.55f, 0, 0});
            require(core.r > 120 && core.g > 20 && core.g > core.b, "Explosion shader lost its warm core");
            require(edge.r > 0 && edge.r + 30 < core.r, "Explosion edge is not feathered");
            const Color outside = pixel(fire, {.7f, 0, 0}), corner = pixel(fire, {.55f, -.55f, 0});
            require(outside.r == 0 && corner.r == 0, "Explosion has a visible square billboard edge");
            UnloadImage(fire);
            Image smoke = render(false, 0, 0), later = render(false, 0, .25f);
            int faint = 0, dense = 0, changed = 0;
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
                const Color a = GetImageColor(smoke, x, y), b = GetImageColor(later, x, y);
                faint += a.r > 0 && a.r < 8;
                dense += a.r > 16;
                changed += a.r != b.r;
            }
            require(faint > 50 && dense > 50, "Smoke shader lost its translucent soft edges");
            require(changed > 100, "Smoke shader and puffs stopped animating");
            if (argc == 2) ExportImage(later, argv[1]);
            UnloadImage(smoke); UnloadImage(later);
            Image covered = render(true, .2f, 0, true);
            const Color wall = pixel(covered, {0, 0, 0});
            require(wall.r == GREEN.r && wall.g == GREEN.g && wall.b == GREEN.b, "Vehicle effects bleed through opaque scenery");
            UnloadImage(covered);
            Image expired = render(true, 8, 0);
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
                require(GetImageColor(expired, x, y).r == 0, "Expired wreck effects remain visible");
            UnloadImage(expired);
            std::cout << "Vehicle shader softness, animation, occlusion and lifetime checks passed\n";
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
        UnloadRenderTexture(texture);
    }
    CloseWindow();
    return result;
}
