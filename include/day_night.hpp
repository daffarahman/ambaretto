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
    float minutes_of_day() const { return float(seconds_); }
    std::array<char, 6> clock() const {
        const int minute = int(seconds_ + 1e-8) % 1440, hour = minute / 60;
        return {{char('0' + hour / 10), char('0' + hour % 10), ':',
            char('0' + minute % 60 / 10), char('0' + minute % 10), '\0'}};
    }
    Daylight lighting() const {
        struct SkyKey { float hour; Vector3 horizon, zenith; };
        // Clear blues and golden twilight keep fog and reflections out of the pink/purple range.
        static constexpr SkyKey keys[] = {
            {0, {20, 38, 69}, {6, 14, 34}},
            {5, {92, 137, 164}, {28, 64, 105}},
            {6, {248, 218, 156}, {64, 134, 196}},
            {7, {205, 230, 233}, {62, 154, 221}},
            {9, {177, 226, 243}, {38, 137, 224}},
            {16, {190, 230, 241}, {49, 135, 211}},
            {18, {248, 196, 116}, {65, 122, 183}},
            {19, {126, 161, 181}, {32, 68, 112}},
            {20, {51, 87, 121}, {16, 36, 68}},
            {22, {25, 44, 76}, {8, 18, 39}},
            {24, {20, 38, 69}, {6, 14, 34}}
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
            mix({1, .99f, .96f}, {1, .80f, .55f}, warmth),
            mix({.16f, .21f, .30f}, {.60f, .64f, .61f}, day), day, 1 - day};
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
