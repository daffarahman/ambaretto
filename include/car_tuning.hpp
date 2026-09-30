#pragma once
#include <array>

namespace forza {
struct CarTuning {
    float wheel_radius = 0.34f;
    float rest_length = 0.58f;
    float travel = 0.32f;
    float mount_height = 0.31f;
    float spring_rate = 22500;
    float damper_rate = 3000;
    float max_spring_force = 14000;
    float tire_grip = 1.6f;
    float rear_drift_grip = 0.85f;
    float motor_grip = 0.65f;
    float max_steer = 0.52f;
    float wheelbase = 2.5f;
    float track_width = 1.64f;
};
struct TuningControl {
    const char* label;
    const char* hint;
    float CarTuning::* value;
    float min, max, step;
    const char* format;
    float display_scale = 1;
};
// Shared limits keep sliders, keyboard edits and physics validation consistent.
inline constexpr std::array<TuningControl, 13> tuning_controls{{
    {"Wheel radius", "Changes tire size and its ground contact radius.", &CarTuning::wheel_radius, .18f, .55f, .005f, "%.3f m"},
    {"Rest length", "Longer suspension raises the body above the wheels.", &CarTuning::rest_length, .30f, .95f, .005f, "%.3f m"},
    {"Suspension travel", "Maximum compression / extension from rest.", &CarTuning::travel, .05f, .45f, .005f, "%.3f m"},
    {"Mount height", "Moves suspension attachment points on the chassis.", &CarTuning::mount_height, .10f, .55f, .005f, "%.3f m"},
    {"Spring strength", "Stiffer springs reduce compression under load.", &CarTuning::spring_rate, 8000, 60000, 500, "%.0f N/m"},
    {"Damping", "Resists suspension motion and reduces bouncing.", &CarTuning::damper_rate, 0, 8000, 100, "%.0f Ns/m"},
    {"Spring force cap", "Maximum upward force from each wheel.", &CarTuning::max_spring_force, 4000, 30000, 500, "%.0f N"},
    {"Tire grip", "Higher grip resists sideways slip in normal driving.", &CarTuning::tire_grip, .4f, 2.8f, .05f, "%.2f"},
    {"Rear drift grip", "Rear tire grip while holding the handbrake.", &CarTuning::rear_drift_grip, .15f, 1.6f, .05f, "%.2f"},
    {"Drive traction", "Limits the driving force available at each tire.", &CarTuning::motor_grip, .15f, 1.2f, .05f, "%.2f"},
    {"Steering limit", "Maximum steering angle; it still reduces at speed.", &CarTuning::max_steer, .15f, .8f, .01f, "%.1f deg", 57.2957795f},
    {"Wheelbase", "Distance between front and rear wheel centers.", &CarTuning::wheelbase, 1.6f, 3.5f, .01f, "%.3f m"},
    {"Track width", "Distance between left and right wheel centers.", &CarTuning::track_width, 1.1f, 2.2f, .01f, "%.3f m"}
}};
inline constexpr int suspension_controls = 7;
} // namespace forza
