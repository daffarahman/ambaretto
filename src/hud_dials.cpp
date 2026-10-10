#include "hud_dials.hpp"
#include "ui_font.hpp"
#include <algorithm>
#include <cmath>

namespace ambaretto {
namespace {
Vector2 dial_point(Vector2 center, float degrees, float radius) {
    const float angle = degrees * DEG2RAD;
    return {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
}
void draw_dial_face(Vector2 center) {
    DrawCircleV({center.x + 3, center.y + 4}, 100, {0, 0, 0, 110});
    DrawCircleV(center, 98, {19, 19, 19, 255});
    DrawCircleV({center.x - 1, center.y - 1}, 96, {160, 160, 160, 255});
    DrawCircleV({center.x + 1, center.y + 1}, 94, {62, 62, 62, 255});
    DrawCircleV(center, 92, {90, 90, 90, 255});
    DrawCircleV({center.x - 1, center.y - 1}, 90, {210, 210, 210, 255});
    DrawCircleGradient(center, 88, {35, 35, 35, 255}, {9, 9, 9, 255});
}
void draw_hand(Vector2 center, float angle, float length, float width) {
    const auto tip = dial_point(center, angle, length), tail = dial_point(center, angle + 180, 10);
    DrawLineEx({tail.x + 1.5f, tail.y + 1.5f}, {tip.x + 1.5f, tip.y + 1.5f}, width + 1.5f, {0, 0, 0, 180});
    DrawLineEx(tail, tip, width, RAYWHITE);
}
void draw_hub(Vector2 center) {
    DrawCircleV(center, 7, {5, 5, 5, 255});
    DrawCircleV({center.x - 1, center.y - 1}, 5, {180, 180, 180, 255});
    DrawCircleV(center, 3, {24, 24, 24, 255});
}
} // namespace

void draw_speedometer(Vector2 center, float kilometers_per_hour) {
    draw_dial_face(center);
    for (int speed = 0; speed <= 400; speed += 10) {
        const float angle = 135 + 270 * speed / 400.f;
        const bool major = speed % 50 == 0;
        DrawLineEx(dial_point(center, angle, major ? 68 : 75), dial_point(center, angle, 82), major ? 2.5f : 1.5f,
            major ? RAYWHITE : Color{170, 170, 170, 255});
        if (major) {
            const auto p = dial_point(center, angle, 56);
            const char* label = TextFormat("%d", speed);
            ui::draw_text(label, int(p.x - ui::measure_text(label, 13) / 2.f), int(p.y - 6), 13, RAYWHITE);
        }
    }
    ui::draw_text("km/h", int(center.x - ui::measure_text("km/h", 16) / 2.f), int(center.y + 24), 16, RAYWHITE);
    const float speed = std::isfinite(kilometers_per_hour) ? std::clamp(kilometers_per_hour, 0.f, 400.f) : 0.f;
    const float angle = 135 + 270 * speed / 400;
    draw_hand(center, angle, 69, 2.5f);
    draw_hub(center);
}

void draw_clock(Vector2 center, float minutes_of_day) {
    draw_dial_face(center);
    for (int minute = 0; minute < 60; ++minute) {
        const float angle = minute * 6.f - 90;
        const bool hour = minute % 5 == 0;
        DrawLineEx(dial_point(center, angle, hour ? 70 : 77), dial_point(center, angle, 82), hour ? 2.5f : 1.5f,
            hour ? RAYWHITE : Color{170, 170, 170, 255});
        if (hour) {
            const auto p = dial_point(center, angle, 56);
            const char* label = TextFormat("%d", minute ? minute / 5 : 12);
            ui::draw_text(label, int(p.x - ui::measure_text(label, 13) / 2.f), int(p.y - 6), 13, RAYWHITE);
        }
    }
    draw_hand(center, std::fmod(minutes_of_day, 720.f) * .5f - 90, 43, 4.5f);
    draw_hand(center, std::fmod(minutes_of_day, 60.f) * 6 - 90, 69, 2.5f);
    draw_hub(center);
}
}
