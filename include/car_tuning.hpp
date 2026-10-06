#pragma once
#include <array>

namespace forza {
struct CarTuning {
    float wheel_radius = 0.3f;
    float rest_length = 0.58f;
    float travel = 0.32f;
    float mount_height = 0.31f;
    float spring_rate = 22500;
    float damper_rate = 3000;
    float max_spring_force = 14000;
    float tire_grip = 1.6f;
    float rear_drift_grip = 0.85f;
    float motor_grip = 0.9f;
    float max_steer = 0.52f;
    float wheelbase = 2.5f;
    float track_width = 1.480f;
    float top_speed = 240.0f / 3.6f;
    float acceleration = 9;
};
struct TuningControl {
    const char* label;
    float CarTuning::* value;
    float min, max, step;
    const char* format;
    float display_scale = 1;
};
// Shared limits keep sliders, keyboard edits and physics validation consistent.
inline constexpr std::array<TuningControl, 15> tuning_controls{{
    {"Wheel radius", &CarTuning::wheel_radius, .18f, .55f, .005f, "%.3f m"},
    {"Rest length", &CarTuning::rest_length, .30f, .95f, .005f, "%.3f m"},
    {"Suspension travel", &CarTuning::travel, .05f, .45f, .005f, "%.3f m"},
    {"Mount height", &CarTuning::mount_height, .10f, .55f, .005f, "%.3f m"},
    {"Spring strength", &CarTuning::spring_rate, 8000, 60000, 500, "%.0f N/m"},
    {"Damping", &CarTuning::damper_rate, 0, 8000, 100, "%.0f Ns/m"},
    {"Spring force cap", &CarTuning::max_spring_force, 4000, 30000, 500, "%.0f N"},
    {"Tire grip", &CarTuning::tire_grip, .4f, 2.8f, .05f, "%.2f"},
    {"Rear drift grip", &CarTuning::rear_drift_grip, .15f, 1.6f, .05f, "%.2f"},
    {"Drive traction", &CarTuning::motor_grip, .15f, 1.6f, .05f, "%.2f"},
    {"Steering limit", &CarTuning::max_steer, .15f, .8f, .01f, "%.1f deg", 57.2957795f},
    {"Wheelbase", &CarTuning::wheelbase, 1.6f, 3.5f, .01f, "%.3f m"},
    {"Track width", &CarTuning::track_width, 1.1f, 2.2f, .01f, "%.3f m"},
    {"Top speed", &CarTuning::top_speed, 80.0f / 3.6f, 400.0f / 3.6f, 5.0f / 3.6f, "%.1f km/h", 3.6f},
    {"Acceleration", &CarTuning::acceleration, 2, 16, .25f, "%.2f m/s2"}
}};
inline constexpr int suspension_controls = 7;
inline constexpr int handling_controls = 6;
} // namespace forza
