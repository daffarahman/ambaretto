#include "hud_dials.hpp"
#include "ui_font.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool needle(Image image, Vector2 p) {
    const Color color = GetImageColor(image, int(p.x), int(p.y));
    return color.r > 220 && color.g > 220 && color.b > 220;
}
}
int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(256, 256, "HUD dial checks");
    int result = 0;
    {
        ambaretto::ui::FontResource font;
        const auto target = LoadRenderTexture(256, 256);
        try {
            const auto render = [&](float speed) {
                BeginTextureMode(target); ClearBackground(ambaretto::ui::face);
                ambaretto::draw_speedometer({128, 128}, speed); EndTextureMode();
                Image image = LoadImageFromTexture(target.texture); ImageFlipVertical(&image); return image;
            };
            const Vector2 zero{100, 156}, middle{128, 88}, maximum{156, 156};
            Image stopped = render(0), cruising = render(200), fast = render(400);
            require(needle(stopped, zero) && !needle(stopped, middle), "Zero-speed needle is misplaced");
            require(needle(cruising, middle) && !needle(cruising, zero), "200 km/h must point to the dial midpoint");
            require(needle(fast, maximum) && !needle(fast, middle), "400 km/h must point to the scale endpoint");
            for (float speed : {-10.f, std::numeric_limits<float>::quiet_NaN()}) {
                Image image = render(speed); const bool valid = needle(image, zero); UnloadImage(image);
                require(valid, "Invalid or negative speed moved the needle below zero");
            }
            Image overspeed = render(500); const bool limited = needle(overspeed, maximum); UnloadImage(overspeed);
            require(limited, "Overspeed made the needle wrap around the dial");
            const auto render_clock = [&](float minutes) {
                BeginTextureMode(target); ClearBackground(ambaretto::ui::face);
                ambaretto::draw_clock({128, 128}, minutes); EndTextureMode();
                Image image = LoadImageFromTexture(target.texture); ImageFlipVertical(&image); return image;
            };
            Image midnight = render_clock(0), noon = render_clock(720), three = render_clock(180), half_past = render_clock(210);
            require(needle(midnight, middle) && !needle(midnight, {158, 128}), "Midnight clock hands must point to twelve");
            require(needle(noon, middle) && !needle(noon, {158, 128}), "Noon clock hands must point to twelve");
            require(needle(three, middle) && needle(three, {158, 128}), "Three o'clock must have distinct hour and minute hands");
            require(needle(half_past, {128, 168}) && needle(half_past, {159, 136}) && !needle(half_past, {158, 128}),
                "Half-past clock must advance the hour hand between hour markers");
            for (int x = 30; x < 40; ++x) {
                const Color speed_rim = GetImageColor(cruising, x, 128), clock_rim = GetImageColor(noon, x, 128);
                require(speed_rim.r == clock_rim.r && speed_rim.g == clock_rim.g && speed_rim.b == clock_rim.b,
                    "Clock and speedometer rims must have identical size and shading");
            }
            UnloadImage(midnight); UnloadImage(noon); UnloadImage(three); UnloadImage(half_past);
            if (argc > 1) require(ExportImage(cruising, argv[1]), "Speedometer preview failed to save");
            UnloadImage(stopped); UnloadImage(cruising); UnloadImage(fast);
            std::cout << "Analog km/h needle, invalid speeds, clock hands and matching rims passed\n";
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
        UnloadRenderTexture(target);
    }
    CloseWindow(); return result;
}
