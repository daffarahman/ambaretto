#include "controller_mapping.hpp"
#include <raylib.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

namespace ambaretto {
const std::array<const char*, action_count> action_labels{{
    "Walk forward", "Walk backward", "Walk left", "Walk right", "Enter vehicle", "Sprint", "Jump", "Respawn on foot",
    "Weapon wheel (hold)", "Fire weapon", "Aim weapon", "Reload weapon", "Enter / leave cover",
    "Camera left", "Camera right", "Camera up", "Camera down", "Zoom in", "Zoom out",
    "Accelerate", "Brake / reverse", "Steer left", "Steer right", "Handbrake", "Exit car", "Recover car", "Car horn", "Car tuning",
    "Camera left", "Camera right", "Camera up", "Camera down", "Zoom in", "Zoom out",
    "Pitch down", "Pitch up", "Bank left", "Bank right", "Throttle up", "Throttle down", "Rudder left", "Rudder right",
    "Toggle flaps", "Wheel brake", "Exit plane", "Recover plane",
    "Camera left", "Camera right", "Camera up", "Camera down", "Zoom in", "Zoom out",
    "World map", "Pause / resume"
}};
const std::array<ActionGroup, 4> action_groups{{
    {"On foot", Action::FootForward, Action::Forward},
    {"Car", Action::Forward, Action::PitchDown},
    {"Plane", Action::PitchDown, Action::Map},
    {"General", Action::Map, Action::Count}
}};
namespace {
constexpr std::array<const char*, action_count> ids{{
    "foot_forward", "foot_backward", "foot_left", "foot_right", "foot_enter", "foot_sprint", "foot_jump", "foot_respawn",
    "foot_weapon_wheel", "foot_fire", "foot_aim", "foot_reload", "foot_cover",
    "foot_look_left", "foot_look_right", "foot_look_up", "foot_look_down", "foot_zoom_in", "foot_zoom_out",
    "car_forward", "car_backward", "car_left", "car_right", "car_brake", "car_exit", "car_recover", "car_horn", "car_tuning",
    "car_look_left", "car_look_right", "car_look_up", "car_look_down", "car_zoom_in", "car_zoom_out",
    "plane_pitch_down", "plane_pitch_up", "plane_bank_left", "plane_bank_right", "plane_throttle_up", "plane_throttle_down", "plane_rudder_left", "plane_rudder_right",
    "plane_flaps", "plane_brake", "plane_exit", "plane_recover",
    "plane_look_left", "plane_look_right", "plane_look_up", "plane_look_down", "plane_zoom_in", "plane_zoom_out",
    "map", "pause"
}};
bool valid(Binding b) {
    if (b.kind == BindingKind::Mouse) return b.code >= 0 && b.code < 8 && b.direction == 1;
    if (b.kind == BindingKind::Key) return b.code > 0 && b.code < 512 && b.direction == 1;
    if (b.kind == BindingKind::Button) return b.code > 0 && b.code < 18 && b.direction == 1;
    if (b.kind == BindingKind::JoystickButton) return b.code >= 0 && b.code < 64 && b.direction == 1;
    if (b.kind == BindingKind::JoystickHat) return b.code >= 0 && b.code < 16 && b.direction == 1;
    if (b.kind == BindingKind::JoystickAxis) return b.code >= 0 && b.code < 16 && (b.direction == 1 || b.direction == -1);
    return b.kind == BindingKind::Axis && b.code >= 0 && b.code < 8 &&
        (b.direction == 1 || (b.direction == -1 && b.code != 4 && b.code != 5));
}
}
ControllerState read_controllers() {
    ControllerState input;
    for (int key = 1; key < 512; ++key) input.keys[key] = IsKeyDown(key);
    for (int b = 0; b < 8; ++b) input.mouse[b] = IsMouseButtonDown(b);
    for (int pad = 0; pad < int(input.pads.size()); ++pad) {
        auto& p = input.pads[pad];
        if (!(p.connected = glfwJoystickPresent(pad) == GLFW_TRUE)) continue;
        const char* name = glfwGetJoystickName(pad);
        p.name = name ? name : "Unnamed controller";
        p.raw = !glfwJoystickIsGamepad(pad) || pad >= 4;
        if (!p.raw) {
            p.axis_count = std::clamp(GetGamepadAxisCount(pad), 0, 8);
            for (int button = 1; button < 18; ++button) p.buttons[button] = IsGamepadButtonDown(pad, button);
            for (int axis = 0; axis < p.axis_count; ++axis) p.axes[axis] = GetGamepadAxisMovement(pad, axis);
        } else {
            int count = 0;
            const auto* buttons = glfwGetJoystickButtons(pad, &count);
            if (buttons) for (int b = 0; b < std::min(count, 64); ++b) p.buttons[b] = buttons[b] == GLFW_PRESS;
            const auto* axes = glfwGetJoystickAxes(pad, &count);
            if (axes) {
                p.axis_count = std::clamp(count, 0, 16);
                std::copy_n(axes, p.axis_count, p.axes.begin());
            }
            const auto* hats = glfwGetJoystickHats(pad, &count);
            if (hats) {
                p.hat_count = std::clamp(count, 0, 4);
                std::copy_n(hats, p.hat_count, p.hats.begin());
            }
        }
    }
    return input;
}
float axis_amount(int axis, int direction, float raw, float deadzone, bool joystick) {
    if (!std::isfinite(raw)) return 0;
    const float amount = !joystick && (axis == 4 || axis == 5) ? (raw + 1) * .5f : raw * direction;
    return std::clamp((amount - deadzone) / (1 - deadzone), 0.0f, 1.0f);
}
std::string binding_label(Binding b) {
    if (b.kind == BindingKind::Mouse) {
        constexpr const char* names[] = {"Left mouse", "Right mouse", "Middle mouse", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8"};
        return names[std::clamp(b.code, 0, 7)];
    }
    if (b.kind == BindingKind::JoystickButton) return "USB button " + std::to_string(b.code + 1);
    if (b.kind == BindingKind::JoystickAxis) return "USB axis " + std::to_string(b.code + 1) + (b.direction < 0 ? " -" : " +");
    if (b.kind == BindingKind::JoystickHat) {
        constexpr const char* directions[] = {"up", "right", "down", "left"};
        return "USB D-pad " + std::to_string(b.code / 4 + 1) + " " + directions[b.code % 4];
    }
    if (b.kind == BindingKind::Axis) {
        constexpr const char* axes[] = {"Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "LT", "RT", "Axis 6", "Axis 7"};
        return std::string(axes[b.code]) + (b.code == 4 || b.code == 5 ? "" : b.direction < 0 ? " -" : " +");
    }
    if (b.kind == BindingKind::Button) {
        constexpr const char* names[] = {"?", "D-pad up", "D-pad right", "D-pad down", "D-pad left", "Y / Triangle", "B / Circle",
            "A / Cross", "X / Square", "LB / L1", "LT / L2 button", "RB / R1", "RT / R2 button", "Back / Select", "Guide", "Start", "L-stick click", "R-stick click"};
        return names[b.code];
    }
    if (b.code >= KEY_A && b.code <= KEY_Z) return std::string(1, char(b.code));
    if (b.code >= KEY_ZERO && b.code <= KEY_NINE) return std::string(1, char(b.code));
    if (b.code >= KEY_F1 && b.code <= KEY_F12) return "F" + std::to_string(b.code - KEY_F1 + 1);
    switch (b.code) {
        case KEY_SPACE: return "Space"; case KEY_ESCAPE: return "Esc"; case KEY_ENTER: return "Enter"; case KEY_TAB: return "Tab";
        case KEY_LEFT_SHIFT: return "Left Shift"; case KEY_RIGHT_SHIFT: return "Right Shift";
        case KEY_LEFT_CONTROL: return "Left Ctrl"; case KEY_RIGHT_CONTROL: return "Right Ctrl";
        case KEY_LEFT: return "Left arrow"; case KEY_RIGHT: return "Right arrow";
        case KEY_UP: return "Up arrow"; case KEY_DOWN: return "Down arrow";
        default: return "Key " + std::to_string(b.code);
    }
}
void BindingCapture::start(const ControllerState& input) {
    previous_ = input;
    armed_ = {};
    for (int p = 0; p < int(input.pads.size()); ++p) for (int a = 0; a < input.pads[p].axis_count; ++a) {
        const auto& pad = input.pads[p];
        const float amount = !pad.raw && (a == 4 || a == 5) ? (pad.axes[a] + 1) * .5f : std::abs(pad.axes[a]);
        armed_[p][a] = amount < .25f;
    }
}
std::optional<Binding> BindingCapture::poll(const ControllerState& input) {
    std::optional<Binding> result;
    for (int b = 0; b < 8; ++b) if (input.mouse[b] && !previous_.mouse[b]) { result = Binding{BindingKind::Mouse, b}; break; }
    for (int p = 0; p < int(input.pads.size()) && !result; ++p) {
        const auto& pad = input.pads[p];
        const auto& before = previous_.pads[p];
        if (!pad.connected) { armed_[p] = {}; continue; }
        for (int h = 0; h < pad.hat_count && !result; ++h) for (int d = 0; d < 4 && !result; ++d) {
            if ((pad.hats[h] & (1 << d)) && !(before.connected && (before.hats[h] & (1 << d)))) result = Binding{BindingKind::JoystickHat, h * 4 + d};
        }
        for (int b = pad.raw ? 0 : 1; b < (pad.raw ? 64 : 18) && !result; ++b) {
            // Preserve analog pressure instead of raylib's synthesized trigger button.
            if (!pad.raw && ((b == GAMEPAD_BUTTON_LEFT_TRIGGER_2 && pad.axis_count > 4) ||
                (b == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 && pad.axis_count > 5))) continue;
            if (pad.buttons[b] && !(before.connected && before.buttons[b])) result = Binding{pad.raw ? BindingKind::JoystickButton : BindingKind::Button, b};
        }
        for (int a = 0; a < pad.axis_count && !result; ++a) {
            const float raw = pad.axes[a];
            const bool trigger = !pad.raw && (a == 4 || a == 5);
            const float amount = trigger ? (raw + 1) * .5f : std::abs(raw);
            if (amount < .25f) armed_[p][a] = true;
            if (armed_[p][a] && amount > .65f) result = Binding{pad.raw ? BindingKind::JoystickAxis : BindingKind::Axis, a, trigger || raw > 0 ? 1 : -1};
        }
    }
    previous_ = input;
    return result;
}
ControllerMapping::ControllerMapping() { defaults(); }
void ControllerMapping::defaults() {
    for (auto& list : bindings) list.clear();
    values_ = {}; pressed_ = {};
    deadzone = .2f;
    const auto key = [&](Action a, int code) { add(a, {BindingKind::Key, code}); };
    const auto button = [&](Action a, int code) { add(a, {BindingKind::Button, code}); };
    const auto axis = [&](Action a, int code, int sign = 1) { add(a, {BindingKind::Axis, code, sign}); };
    for (const auto actions : {std::array<Action, 4>{Action::FootForward, Action::FootBackward, Action::FootLeft, Action::FootRight},
            std::array<Action, 4>{Action::Forward, Action::Backward, Action::Left, Action::Right},
            std::array<Action, 4>{Action::PitchDown, Action::PitchUp, Action::BankLeft, Action::BankRight}}) {
        key(actions[0], KEY_W); key(actions[1], KEY_S); key(actions[2], KEY_A); key(actions[3], KEY_D);
        axis(actions[0], GAMEPAD_AXIS_LEFT_Y, -1); axis(actions[1], GAMEPAD_AXIS_LEFT_Y);
        axis(actions[2], GAMEPAD_AXIS_LEFT_X, -1); axis(actions[3], GAMEPAD_AXIS_LEFT_X);
    }
    for (const auto a : {Action::EnterVehicle, Action::ExitVehicle, Action::PlaneExit}) {
        key(a, KEY_E); button(a, GAMEPAD_BUTTON_RIGHT_FACE_UP);
    }
    for (const auto a : {Action::Brake, Action::PlaneBrake}) {
        key(a, KEY_SPACE); button(a, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    }
    key(Action::Jump, KEY_SPACE);
    for (int k : {KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT}) { key(Action::Sprint, k); key(Action::ThrottleUp, k); }
    for (int k : {KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL}) key(Action::ThrottleDown, k);
    key(Action::RudderLeft, KEY_LEFT); key(Action::RudderRight, KEY_RIGHT); key(Action::Flaps, KEY_F);
    for (const auto a : {Action::Recover, Action::PlaneRecover}) {
        key(a, KEY_R); button(a, GAMEPAD_BUTTON_LEFT_FACE_UP);
    }
    key(Action::Respawn, KEY_F5); button(Action::Respawn, GAMEPAD_BUTTON_LEFT_FACE_UP);
    key(Action::Map, KEY_F2); key(Action::Tuning, KEY_F3);
    button(Action::Sprint, GAMEPAD_BUTTON_LEFT_THUMB); button(Action::Jump, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    axis(Action::ThrottleUp, GAMEPAD_AXIS_RIGHT_TRIGGER); axis(Action::ThrottleDown, GAMEPAD_AXIS_LEFT_TRIGGER);
    button(Action::RudderLeft, GAMEPAD_BUTTON_LEFT_TRIGGER_1); button(Action::RudderRight, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
    button(Action::Flaps, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    button(Action::Map, GAMEPAD_BUTTON_MIDDLE_LEFT); button(Action::Tuning, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    button(Action::Pause, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    for (const auto actions : {std::array<Action, 6>{Action::FootLookLeft, Action::FootLookRight, Action::FootLookUp, Action::FootLookDown, Action::FootZoomIn, Action::FootZoomOut},
            std::array<Action, 6>{Action::VehicleLookLeft, Action::VehicleLookRight, Action::VehicleLookUp, Action::VehicleLookDown, Action::VehicleZoomIn, Action::VehicleZoomOut},
            std::array<Action, 6>{Action::PlaneLookLeft, Action::PlaneLookRight, Action::PlaneLookUp, Action::PlaneLookDown, Action::PlaneZoomIn, Action::PlaneZoomOut}}) {
        axis(actions[0], GAMEPAD_AXIS_RIGHT_X, -1); axis(actions[1], GAMEPAD_AXIS_RIGHT_X);
        axis(actions[2], GAMEPAD_AXIS_RIGHT_Y, -1); axis(actions[3], GAMEPAD_AXIS_RIGHT_Y);
        button(actions[4], GAMEPAD_BUTTON_LEFT_FACE_DOWN); button(actions[5], GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    }
    key(Action::Horn, KEY_H); button(Action::Horn, GAMEPAD_BUTTON_RIGHT_THUMB);
    key(Action::WeaponWheel, KEY_TAB); button(Action::WeaponWheel, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    add(Action::Fire, {BindingKind::Mouse, MOUSE_BUTTON_LEFT}); axis(Action::Fire, GAMEPAD_AXIS_RIGHT_TRIGGER);
    add(Action::Aim, {BindingKind::Mouse, MOUSE_BUTTON_RIGHT}); axis(Action::Aim, GAMEPAD_AXIS_LEFT_TRIGGER);
    key(Action::Reload, KEY_R); button(Action::Reload, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    key(Action::Cover, KEY_Q); button(Action::Cover, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
}
bool ControllerMapping::add(Action a, Binding b) {
    if (int(a) < 0 || int(a) >= action_count || !valid(b)) return false;
    auto& list = bindings[int(a)];
    if (std::find(list.begin(), list.end(), b) != list.end()) return true;
    if (list.size() >= 64) return false;
    list.push_back(b); return true;
}
void ControllerMapping::update(const ControllerState& input) {
    for (int a = 0; a < action_count; ++a) {
        float next = 0;
        for (const auto b : bindings[a]) {
            if (b.kind == BindingKind::Key) next = std::max(next, float(input.keys[b.code]));
            else if (b.kind == BindingKind::Mouse) next = std::max(next, float(input.mouse[b.code]));
            else for (const auto& p : input.pads) if (p.connected) {
                const bool joystick = b.kind == BindingKind::JoystickButton || b.kind == BindingKind::JoystickAxis || b.kind == BindingKind::JoystickHat;
                if (p.raw != joystick) continue;
                if (b.kind == BindingKind::Button || b.kind == BindingKind::JoystickButton) next = std::max(next, float(p.buttons[b.code]));
                else if (b.kind == BindingKind::JoystickHat) {
                    if (b.code / 4 < p.hat_count) next = std::max(next, float((p.hats[b.code / 4] & (1 << (b.code % 4))) != 0));
                } else if (b.code < p.axis_count) next = std::max(next, axis_amount(b.code, b.direction, p.axes[b.code], deadzone, joystick));
            }
        }
        pressed_[a] = next > .5f && values_[a] <= .5f;
        values_[a] = next;
    }
}
bool ControllerMapping::load(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::ifstream file(path);
    if (!file) { error = "Cannot open controller mappings"; return false; }
    ControllerMapping candidate;
    int section = -1, line_number = 0;
    bool version = false;
    std::array<bool, action_count> seen{};
    std::string line;
    while (std::getline(file, line)) {
        ++line_number;
        if (line_number > 4096 || line.size() > 1024) { error = "Mapping file is too large"; return false; }
        line = line.substr(0, line.find_first_of(";#"));
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        bool ok = true;
        if (line.front() == '[' && line.back() == ']') {
            const auto id = line.substr(1, line.size() - 2);
            section = -1;
            for (int i = 0; i < action_count; ++i) if (id == ids[i]) section = i;
            ok = version && section >= 0 && !seen[section];
            if (ok) { seen[section] = true; candidate.bindings[section].clear(); }
        } else {
            std::replace(line.begin(), line.end(), '=', ' ');
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            std::string kind, extra; row >> kind;
            if (kind == "version" && section == -1 && !version) { int v = 0; ok = bool(row >> v) && v == 2; version = ok; }
            else if (kind == "deadzone" && section == -1) { ok = version && bool(row >> candidate.deadzone) && std::isfinite(candidate.deadzone) && candidate.deadzone >= .05f && candidate.deadzone <= .5f; }
            else if (kind == "auto_lock" && section == -1) {
                int enabled = -1;
                ok = version && bool(row >> enabled) && (enabled == 0 || enabled == 1);
                candidate.auto_lock = enabled == 1;
            }
            else if (section >= 0) {
                Binding b{BindingKind::Key, 0};
                if (kind == "button") b.kind = BindingKind::Button;
                else if (kind == "axis") b.kind = BindingKind::Axis;
                else if (kind == "joystick_button") b.kind = BindingKind::JoystickButton;
                else if (kind == "joystick_axis") b.kind = BindingKind::JoystickAxis;
                else if (kind == "joystick_hat") b.kind = BindingKind::JoystickHat;
                else if (kind == "mouse") b.kind = BindingKind::Mouse;
                else if (kind != "key") ok = false;
                ok = ok && bool(row >> b.code);
                if (b.kind == BindingKind::Axis || b.kind == BindingKind::JoystickAxis) ok = ok && bool(row >> b.direction);
                ok = ok && candidate.add(Action(section), b);
            } else ok = false;
            if (row >> extra) ok = false;
        }
        if (!ok) { error = "Invalid mapping at line " + std::to_string(line_number); return false; }
    }
    if (!version || file.bad()) { error = "Incomplete controller mapping file"; return false; }
    auto& map = candidate.bindings[int(Action::Map)];
    map.erase(std::remove(map.begin(), map.end(), Binding{BindingKind::Key, KEY_ESCAPE}), map.end());
    bindings = std::move(candidate.bindings); deadzone = candidate.deadzone; auto_lock = candidate.auto_lock;
    values_ = {}; pressed_ = {};
    return true;
}
bool ControllerMapping::save(const std::filesystem::path& path, std::string& error) const {
    error.clear();
    auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) { error = "Cannot write controller mappings"; return false; }
    file << "; Ambaretto - independent on-foot, car and plane bindings; gamepad bindings use any connected pad\nversion=2\ndeadzone=" << deadzone << "\nauto_lock=" << int(auto_lock) << '\n';
    for (int a = 0; a < action_count; ++a) {
        file << '\n' << '[' << ids[a] << "]\n";
        for (auto b : bindings[a]) {
            constexpr const char* kinds[] = {"key=", "button=", "axis=", "joystick_button=", "joystick_axis=", "joystick_hat=", "mouse="};
            file << kinds[int(b.kind)] << b.code;
            if (b.kind == BindingKind::Axis || b.kind == BindingKind::JoystickAxis) file << ',' << b.direction;
            file << '\n';
        }
    }
    file.close();
    if (!file) { error = "Cannot finish saving controller mappings"; return false; }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    const bool replaced = !ec;
#endif
    if (!replaced) { error = "Cannot replace controller mappings; previous file kept"; return false; }
    return true;
}
} // namespace ambaretto
