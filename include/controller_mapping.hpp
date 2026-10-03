#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace forza {
enum class Action {
    FootForward, FootBackward, FootLeft, FootRight, EnterVehicle, Sprint, Jump, Respawn,
    WeaponWheel, Fire, Aim, Reload, Cover,
    FootLookLeft, FootLookRight, FootLookUp, FootLookDown, FootZoomIn, FootZoomOut,
    Forward, Backward, Left, Right, Brake, ExitVehicle, Recover, Horn, Tuning,
    VehicleLookLeft, VehicleLookRight, VehicleLookUp, VehicleLookDown, VehicleZoomIn, VehicleZoomOut,
    PitchDown, PitchUp, BankLeft, BankRight, ThrottleUp, ThrottleDown, RudderLeft, RudderRight,
    Flaps, PlaneBrake, PlaneExit, PlaneRecover,
    PlaneLookLeft, PlaneLookRight, PlaneLookUp, PlaneLookDown, PlaneZoomIn, PlaneZoomOut,
    Map, Pause, Count
};
constexpr int action_count = int(Action::Count);
extern const std::array<const char*, action_count> action_labels;
struct ActionGroup { const char* label; Action first, end; };
extern const std::array<ActionGroup, 4> action_groups;
enum class BindingKind { Key, Button, Axis, JoystickButton, JoystickAxis, JoystickHat, Mouse };
struct Binding {
    BindingKind kind;
    int code;
    int direction = 1;
    bool operator==(const Binding& b) const { return kind == b.kind && code == b.code && direction == b.direction; }
};
// Include GLFW's raw USB joysticks, which may have no standardized gamepad mapping.
struct ControllerState {
    std::array<bool, 512> keys{};
    std::array<bool, 8> mouse{};
    struct Pad {
        bool connected = false, raw = false;
        std::string name;
        int axis_count = 0, hat_count = 0;
        std::array<bool, 64> buttons{};
        std::array<float, 16> axes{};
        std::array<unsigned char, 4> hats{};
    };
    std::array<Pad, 16> pads{};
};
ControllerState read_controllers();
float axis_amount(int axis, int direction, float raw, float deadzone, bool joystick = false);
std::string binding_label(Binding binding);
class BindingCapture {
public:
    void start(const ControllerState& input);
    std::optional<Binding> poll(const ControllerState& input);
private:
    ControllerState previous_;
    std::array<std::array<bool, 16>, 16> armed_{};
};
class ControllerMapping {
public:
    ControllerMapping();
    std::array<std::vector<Binding>, action_count> bindings;
    float deadzone = .2f;
    void defaults();
    bool add(Action action, Binding binding);
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(const std::filesystem::path& path, std::string& error) const;
    void update(const ControllerState& input);
    float value(Action action) const { return values_[int(action)]; }
    bool pressed(Action action) const { return pressed_[int(action)]; }
private:
    std::array<float, action_count> values_{};
    std::array<bool, action_count> pressed_{};
};
} // namespace forza
