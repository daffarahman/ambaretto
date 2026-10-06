#pragma once
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace ambaretto {
struct Daylight {
    Vector3 horizon, zenith, sun_direction, sun_color, ambient;
    float day, night;
};

class DayNight {
public:
    static constexpr double cycle_seconds = 24 * 60;
    void advance(double real_seconds, bool running = true) {
        if (running && std::isfinite(real_seconds) && real_seconds > 0) {
            seconds_ = std::fmod(seconds_ + std::fmod(real_seconds, cycle_seconds), cycle_seconds);
            if (cycle_seconds - seconds_ < 1e-8) seconds_ = 0;
        }
    }
    bool set_time(std::string_view text) {
        if (text.size() != 5 || text[2] != ':') return false;
        for (int i : {0, 1, 3, 4}) if (text[i] < '0' || text[i] > '9') return false;
        const int hour = (text[0] - '0') * 10 + text[1] - '0', minute = (text[3] - '0') * 10 + text[4] - '0';
        if (hour > 23 || minute > 59) return false;
        seconds_ = hour * 60 + minute;
        return true;
    }
    std::array<char, 6> clock() const {
        const int minute = int(seconds_ + 1e-8) % 1440, hour = minute / 60;
        return {{char('0' + hour / 10), char('0' + hour % 10), ':',
            char('0' + minute % 60 / 10), char('0' + minute % 10), '\0'}};
    }
    Daylight lighting() const {
        struct SkyKey { float hour; Vector3 horizon, zenith; };
        static constexpr SkyKey keys[] = {
            {0, {12, 20, 48}, {4, 7, 24}},
            {5, {73, 43, 110}, {20, 17, 55}},
            {6, {249, 151, 191}, {101, 62, 160}},
            {7, {247, 180, 215}, {102, 116, 202}},
            {9, {157, 210, 233}, {59, 143, 211}},
            {16, {160, 211, 230}, {54, 133, 203}},
            {18, {255, 148, 75}, {119, 72, 160}},
            {19, {238, 98, 112}, {57, 37, 104}},
            {20, {79, 52, 116}, {15, 20, 59}},
            {22, {17, 29, 61}, {5, 10, 31}},
            {24, {12, 20, 48}, {4, 7, 24}}
        };
        const auto mix = [](Vector3 a, Vector3 b, float t) {
            return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        };
        const float hour = float(seconds_ / 60);
        int i = 0;
        while (keys[i + 1].hour < hour) ++i;
        float t = (hour - keys[i].hour) / (keys[i + 1].hour - keys[i].hour);
        t = t * t * (3 - 2 * t);
        const auto unit = [](Vector3 rgb) { return Vector3{rgb.x / 255, rgb.y / 255, rgb.z / 255}; };
        const float angle = (hour - 6) * 3.14159265359f / 12;
        const float elevation = std::sin(angle);
        float day = std::clamp((elevation + .12f) / .32f, 0.0f, 1.0f);
        day = day * day * (3 - 2 * day);
        const float warmth = 1 - std::clamp(elevation * 3, 0.0f, 1.0f);
        return {unit(mix(keys[i].horizon, keys[i + 1].horizon, t)),
            unit(mix(keys[i].zenith, keys[i + 1].zenith, t)),
            {std::cos(angle) * .9578263f, elevation, std::cos(angle) * .2873479f},
            mix({1, 1, .96f}, {1, .61f, .43f}, warmth),
            mix({.16f, .18f, .29f}, {.58f, .58f, .58f}, day), day, 1 - day};
    }
private:
    double seconds_ = 8 * 60;
};

inline void apply_daylight(Shader shader, const Daylight& light) {
    SetShaderValue(shader, GetShaderLocation(shader, "sunDirection"), &light.sun_direction, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "sunColor"), &light.sun_color, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &light.ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "horizonColor"), &light.horizon, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "daylight"), &light.day, SHADER_UNIFORM_FLOAT);
}
} // namespace ambaretto
