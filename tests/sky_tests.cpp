#include "sky_renderer.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int changed(Image a, Image b) {
    int count = 0;
    for (int y = 4; y < a.height; y += 4) for (int x = 4; x < a.width; x += 4) {
        const Color p = GetImageColor(a, x, y), q = GetImageColor(b, x, y);
        count += std::abs(int(p.r) - q.r) + std::abs(int(p.g) - q.g) + std::abs(int(p.b) - q.b) > 24;
    }
    return count;
}
double luminance(Image image) {
    double sum = 0; int count = 0;
    for (int y = 4; y < image.height; y += 4) for (int x = 4; x < image.width; x += 4) {
        const Color p = GetImageColor(image, x, y); sum += p.r + p.g + p.b; ++count;
    }
    return sum / count;
}
}
int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(960, 540, "Volumetric cloud checks");
    SetTargetFPS(0);
    int result = 0;
    try {
        ambaretto::SkyRenderer sky;
        ambaretto::GraphicsSettings settings;
        ambaretto::DayNight time; require(time.set_time("12:00"), "Noon fixture failed");
        auto light = time.lighting();
        Camera3D camera{{0, 2, 0}, {0, 2.45f, 1}, {0, 1, 0}, 65, CAMERA_PERSPECTIVE};
        const auto frame = [&](float seconds = 0) {
            BeginDrawing(); ClearBackground(MAGENTA); sky.draw(camera, light, seconds, settings); EndDrawing();
        };
        const auto render = [&](float seconds = 0) { frame(seconds); frame(seconds); return LoadImageFromScreen(); };
        Image day = render(); int clouds = 0, blue = 0, lightest = 0, darkest = 765;
        for (int y = 4; y < day.height; y += 4) for (int x = 4; x < day.width; x += 4) {
            const Color p = GetImageColor(day, x, y);
            blue += p.b > p.r + 45 && p.b > p.g + 10;
            if (p.r > 90 && p.r > p.b * .82f && p.g > p.b * .85f) {
                ++clouds; lightest = std::max(lightest, int(p.r) + p.g + p.b); darkest = std::min(darkest, int(p.r) + p.g + p.b);
            }
        }
        std::cout << "Cloud samples: " << clouds << ", blue gaps: " << blue << ", shading range: " << lightest - darkest << '\n';
        require(clouds > 500 && blue > 500, "Cloud shader must produce visible puffs and clear sky gaps");
        require(lightest - darkest > 80, "Clouds need visible volume lighting rather than a flat tint");
        ExportImage(day, (std::string(GetApplicationDirectory()) + "volumetric-clouds-day.png").c_str());
        Image drift = render(200);
        require(changed(day, drift) > 1000, "Cloud volume did not drift with time"); UnloadImage(drift);
        camera.position.x += 400; camera.target.x += 400;
        Image translated = render();
        require(changed(day, translated) > 1000, "Clouds must occupy world space instead of following the camera"); UnloadImage(translated);
        camera.position.x -= 400; camera.target.x -= 400;
        require(time.set_time("00:00"), "Night fixture failed"); light = time.lighting();
        Image night = render();
        require(luminance(night) < luminance(day) * .5, "Clouds must follow night lighting");
        ExportImage(night, (std::string(GetApplicationDirectory()) + "volumetric-clouds-night.png").c_str()); UnloadImage(night);
        require(time.set_time("12:00"), "Noon reset failed"); light = time.lighting(); light.night = .5f;
        Image twilight = render();
        require(luminance(twilight) > 100, "Halfway day/night lighting produced invalid cloud shading"); UnloadImage(twilight);
        light = time.lighting(); settings.brightness = .6f;
        Image dimmed = render();
        require(luminance(dimmed) < luminance(day) * .7, "Cloud compositing must respect scene brightness"); UnloadImage(dimmed);
        settings.brightness = 1;
        camera = {{0, 900, 0}, {0, 899, 1}, {0, 1, 0}, 65, CAMERA_PERSPECTIVE};
        Image above = render(); int below_clouds = 0;
        for (int y = 4; y < above.height; y += 4) for (int x = 4; x < above.width; x += 4) {
            const Color p = GetImageColor(above, x, y);
            below_clouds += std::abs(int(p.r) - int(light.horizon.x * 255)) + std::abs(int(p.g) - int(light.horizon.y * 255)) > 16;
        }
        require(below_clouds > 1000, "Aircraft above the volume must see clouds below them"); UnloadImage(above);
        SetWindowSize(961, 541);
        camera = {{0, 550, 0}, {0, 550, 1}, {0, 1, 0}, 65, CAMERA_PERSPECTIVE};
        Image inside = render();
        require(luminance(inside) > 20, "Horizontal rays inside the cloud layer produced an invalid image"); UnloadImage(inside);
        camera = {{0, 2, 0}, {0, 2.45f, 1}, {0, 1, 0}, 65, CAMERA_PERSPECTIVE};
        for (Vector2 size : {Vector2{960, 540}, Vector2{1920, 1080}}) {
            SetWindowSize(int(size.x), int(size.y)); frame(); frame(); frame();
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 12; ++i) { frame(); MemFree(rlReadScreenPixels(1, 1)); }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 12;
            std::cout << "Synchronized sky/cloud frame at " << GetScreenWidth() << 'x' << GetScreenHeight() << ": " << ms << " ms\n";
            const auto image = LoadImageFromScreen();
            const Color corner = GetImageColor(image, image.width - 1, image.height - 1);
            require(corner.r != MAGENTA.r || corner.g != MAGENTA.g || corner.b != MAGENTA.b, "Resized sky left part of the screen undrawn");
            UnloadImage(image);
        }
        UnloadImage(day);
        std::cout << "Cloud volume, shading, wind, parallax, night, brightness, aircraft and resize checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    CloseWindow(); return result;
}
