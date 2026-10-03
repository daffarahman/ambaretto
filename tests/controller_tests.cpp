#include "controller_mapping.hpp"
#include <raylib.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    using namespace forza;
    const auto folder = std::filesystem::temp_directory_path() / "forza-controller-check";
    const auto path = folder / "controls.ini";
    try {
        std::filesystem::create_directories(folder);
        ControllerMapping mapping;
        require(mapping.bindings[int(Action::Forward)].front().code == KEY_W, "keyboard defaults changed");
        ControllerState cover;
        cover.keys[KEY_Q] = true; mapping.update(cover);
        require(mapping.pressed(Action::Cover), "Q did not enter cover");
        mapping.update(cover); require(!mapping.pressed(Action::Cover), "held Q repeatedly toggled cover");
        mapping.update({}); cover = {}; cover.pads[0].connected = true;
        cover.pads[0].buttons[GAMEPAD_BUTTON_RIGHT_FACE_RIGHT] = true; mapping.update(cover);
        require(mapping.pressed(Action::Cover), "gamepad B did not enter cover");
        mapping.update({});
        ControllerState combat;
        combat.keys[KEY_TAB] = true; combat.keys[KEY_R] = true;
        combat.mouse[MOUSE_BUTTON_LEFT] = combat.mouse[MOUSE_BUTTON_RIGHT] = true;
        mapping.update(combat);
        require(mapping.pressed(Action::WeaponWheel) && mapping.pressed(Action::Reload) &&
            mapping.pressed(Action::Fire) && mapping.value(Action::Aim) == 1, "keyboard/mouse weapon bindings failed");
        mapping.update(combat);
        require(!mapping.pressed(Action::Fire) && mapping.value(Action::Fire) == 1, "held mouse repeated its leading edge");
        mapping.update({});
        combat = {}; combat.pads[1].connected = true; combat.pads[1].axis_count = 6;
        combat.pads[1].buttons[GAMEPAD_BUTTON_LEFT_TRIGGER_1] = true;
        combat.pads[1].buttons[GAMEPAD_BUTTON_RIGHT_FACE_LEFT] = true;
        combat.pads[1].axes[4] = combat.pads[1].axes[5] = 1;
        mapping.update(combat);
        require(mapping.pressed(Action::WeaponWheel) && mapping.pressed(Action::Reload) &&
            mapping.value(Action::Fire) == 1 && mapping.value(Action::Aim) == 1, "gamepad weapon bindings failed");
        BindingCapture mouse_capture;
        mouse_capture.start({});
        combat = {}; combat.mouse[MOUSE_BUTTON_MIDDLE] = true;
        const auto mouse_binding = mouse_capture.poll(combat);
        require(mouse_binding && *mouse_binding == Binding{BindingKind::Mouse, MOUSE_BUTTON_MIDDLE}, "mouse remap capture failed");
        require(!mapping.add(Action::Fire, {BindingKind::Mouse, 8}), "invalid mouse binding accepted");
        mapping.update({});
        static_assert(int(Action::ZoomOut) == 22 && int(Action::Horn) == 23, "existing action IDs must stay stable");
        const std::vector<Binding> horn_defaults{{BindingKind::Key, KEY_H}, {BindingKind::Button, GAMEPAD_BUTTON_RIGHT_THUMB}};
        require(mapping.bindings[int(Action::Horn)] == horn_defaults, "horn defaults must be H and right-stick click");
        ControllerState horn;
        horn.keys[KEY_H] = true; mapping.update(horn);
        require(mapping.value(Action::Horn) == 1 && mapping.pressed(Action::Horn), "keyboard horn did not activate");
        mapping.update(horn);
        require(mapping.value(Action::Horn) == 1 && !mapping.pressed(Action::Horn), "held horn must remain active without repeating its leading edge");
        horn.keys[KEY_H] = false; mapping.update(horn);
        require(mapping.value(Action::Horn) == 0, "released keyboard horn remained active");
        horn.pads[3].connected = true; horn.pads[3].buttons[GAMEPAD_BUTTON_RIGHT_THUMB] = true;
        mapping.update(horn);
        require(mapping.value(Action::Horn) == 1 && mapping.value(Action::Sprint) == 0, "gamepad horn conflicted with left-stick sprint");
        mapping.update(horn);
        require(mapping.value(Action::Horn) == 1 && !mapping.pressed(Action::Horn), "held gamepad horn did not stay active");
        horn.pads[3].connected = false; mapping.update(horn);
        require(mapping.value(Action::Horn) == 0, "disconnected gamepad retained horn input");
        ControllerState escape;
        escape.keys[KEY_ESCAPE] = true;
        mapping.update(escape);
        require(mapping.pressed(Action::Map) && !mapping.pressed(Action::Pause), "Escape must open the map instead of pause");
        mapping.update(escape);
        require(!mapping.pressed(Action::Map), "held Escape repeatedly toggled the map");
        mapping.update({});
        require(mapping.add(Action::Forward, {BindingKind::Key, KEY_UP}), "extra key rejected");
        require(mapping.add(Action::Forward, {BindingKind::Button, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}), "extra gamepad button rejected");
        require(mapping.add(Action::Forward, {BindingKind::Axis, GAMEPAD_AXIS_RIGHT_TRIGGER}), "trigger binding rejected");
        const auto count = mapping.bindings[int(Action::Forward)].size();
        require(mapping.add(Action::Forward, {BindingKind::Key, KEY_UP}) && mapping.bindings[int(Action::Forward)].size() == count, "duplicate binding added");
        require(!mapping.add(Action::Forward, {BindingKind::Key, 512}), "out-of-range key accepted");
        require(!mapping.add(Action::Forward, {BindingKind::Axis, 4, -1}), "negative trigger direction accepted");
        ControllerState input;
        input.keys[KEY_UP] = true;
        mapping.update(input);
        require(mapping.value(Action::Forward) == 1 && mapping.pressed(Action::Forward), "added key did not drive action");
        mapping.update(input);
        require(!mapping.pressed(Action::Forward), "held key repeated a one-shot action");
        input = {}; mapping.update(input);
        auto& pad = input.pads[2]; pad.connected = true; pad.axis_count = 6;
        pad.axes[4] = pad.axes[5] = -1; pad.axes[1] = -.6f;
        mapping.update(input);
        require(std::abs(mapping.value(Action::Forward) - .5f) < .001f, "analog/deadzone scaling failed on another gamepad");
        pad.axes[1] = -.1f; mapping.update(input);
        require(mapping.value(Action::Forward) == 0 && mapping.value(Action::ThrottleUp) == 0, "stick drift or resting trigger activated input");
        pad.axes[5] = 0; mapping.update(input);
        require(std::abs(mapping.value(Action::Forward) - .375f) < .001f, "trigger range was not normalized");
        pad.buttons[GAMEPAD_BUTTON_RIGHT_FACE_DOWN] = true; mapping.update(input);
        require(mapping.value(Action::Forward) == 1 && mapping.pressed(Action::Jump), "button mapping did not activate actions");
        pad.connected = false; mapping.update(input);
        require(mapping.value(Action::Forward) == 0, "disconnected gamepad retained input");
        // Generic USB joysticks have no Xbox mapping: button 0 and raw axis 4 are valid.
        auto& usb = input.pads[12]; usb.connected = usb.raw = true; usb.axis_count = 7; usb.hat_count = 1;
        BindingCapture capture;
        capture.start(input);
        usb.buttons[0] = true;
        auto captured = capture.poll(input);
        require(captured && *captured == Binding{BindingKind::JoystickButton, 0}, "unmapped USB button 0 was not captured beyond raylib's four slots");
        require(mapping.add(Action::Interact, *captured), "raw USB button was rejected");
        capture.start(input);
        require(!capture.poll(input), "held button was mistaken for a new binding");
        usb.buttons[0] = false; require(!capture.poll(input), "button release created a binding");
        usb.buttons[0] = true;
        require(bool(capture.poll(input)), "released USB button could not be captured again");
        usb.buttons[7] = true; mapping.update(input);
        require(mapping.pressed(Action::Interact) && mapping.value(Action::Jump) == 0, "raw USB buttons leaked into Xbox mappings or did not reach gameplay");
        require(mapping.add(Action::Horn, {BindingKind::JoystickButton, 11}), "generic USB horn remap was rejected");
        usb.buttons[11] = true; mapping.update(input);
        require(mapping.value(Action::Horn) == 1 && mapping.pressed(Action::Horn), "generic USB horn button did not reach its action");
        mapping.update(input);
        require(mapping.value(Action::Horn) == 1 && !mapping.pressed(Action::Horn), "generic USB held horn stopped or repeated its edge");
        usb.buttons[11] = false; mapping.update(input);
        require(mapping.value(Action::Horn) == 0, "generic USB horn button release was ignored");
        capture.start(input);
        usb.axes[4] = -.9f;
        captured = capture.poll(input);
        require(captured && *captured == Binding{BindingKind::JoystickAxis, 4, -1}, "raw USB axis 4 was incorrectly treated as a trigger");
        mapping.add(Action::LookLeft, *captured); mapping.update(input);
        require(mapping.value(Action::LookLeft) > .8f, "raw axis did not drive its mapped action");
        usb.axes[4] = 0; mapping.update(input);
        require(mapping.value(Action::LookLeft) == 0, "centered raw axis activated input");
        capture.start(input);
        usb.hats[0] = 3; // Up + right: retain individual D-pad directions.
        captured = capture.poll(input);
        require(captured && *captured == Binding{BindingKind::JoystickHat, 0}, "USB D-pad hat was not captured");
        mapping.add(Action::Jump, *captured);
        mapping.add(Action::Flaps, {BindingKind::JoystickHat, 1}); mapping.update(input);
        require(mapping.pressed(Action::Jump) && mapping.pressed(Action::Flaps), "diagonal USB D-pad did not activate both directions");
        usb.connected = false; mapping.update(input);
        require(mapping.value(Action::Interact) == 0 && mapping.value(Action::Jump) == 0, "unplugged USB joystick retained input");
        require(!mapping.add(Action::Forward, {BindingKind::JoystickButton, 64}) &&
            !mapping.add(Action::Forward, {BindingKind::JoystickAxis, 16}) &&
            !mapping.add(Action::Forward, {BindingKind::JoystickHat, 16}), "out-of-range USB binding accepted");
        // Normalized analog triggers still capture their pressure axis, not a synthesized button.
        input = {}; input.pads[0].connected = true; input.pads[0].axis_count = 6;
        input.pads[0].axes[4] = input.pads[0].axes[5] = -1;
        capture.start(input);
        input.pads[0].axes[5] = 1; input.pads[0].buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER_2] = true;
        captured = capture.poll(input);
        require(captured && *captured == Binding{BindingKind::Axis, GAMEPAD_AXIS_RIGHT_TRIGGER}, "normalized trigger lost analog capture");
        std::string error;
        mapping.deadzone = .3f; mapping.bindings[int(Action::Brake)].clear();
        require(mapping.save(path, error), error.c_str());
        ControllerMapping loaded;
        require(loaded.load(path, error), error.c_str());
        require(loaded.bindings == mapping.bindings && loaded.deadzone == mapping.deadzone, "saved mappings did not round-trip (including an unbound action)");
        loaded.add(Action::Interact, {BindingKind::Key, KEY_ENTER});
        require(loaded.save(path, error), "could not replace existing mapping file");
        require(mapping.load(path, error) && mapping.bindings == loaded.bindings, "replacement file was not persisted");
        const auto before = loaded.bindings;
        for (const char* invalid : {"version=2\n", "version=1\n[forward]\nkey=9999\n", "version=1\ndeadzone=nan\n",
            "version=1\n[unknown]\nkey=87\n", "version=1\n[forward]\naxis=1,0\n", "version=1\n[forward]\nkey=87 garbage\n",
            "version=1\n[forward]\n[forward]\n", "; empty file\n"}) {
            std::ofstream(path) << invalid;
            require(!loaded.load(path, error) && !error.empty() && loaded.bindings == before && loaded.deadzone == .3f, "malformed file partially replaced working mappings");
        }
        std::ofstream(path) << "version=1\n; Partial files retain other defaults\n[forward]\nkey=265\n";
        require(loaded.load(path, error) && loaded.bindings[int(Action::Forward)].size() == 1 &&
            !loaded.bindings[int(Action::Interact)].empty(), "partial mapping did not retain unspecified defaults");
        require(loaded.bindings[int(Action::Horn)] == horn_defaults, "legacy file without a horn section did not retain new horn defaults");
        require(loaded.bindings[int(Action::Cover)] == std::vector<Binding>{{BindingKind::Key, KEY_Q}, {BindingKind::Button, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}},
            "legacy mappings lost cover defaults");
        require(loaded.bindings[int(Action::Fire)] == std::vector<Binding>{{BindingKind::Mouse, MOUSE_BUTTON_LEFT}, {BindingKind::Axis, GAMEPAD_AXIS_RIGHT_TRIGGER}},
            "legacy mappings lost new fire defaults");
        std::ofstream(path) << "version=1\n[zoom_out]\nkey=334\n[horn]\n";
        require(loaded.load(path, error) && loaded.bindings[int(Action::ZoomOut)] == std::vector<Binding>{{BindingKind::Key, 334}} &&
            loaded.bindings[int(Action::Horn)].empty(), "explicitly unbound horn or existing INI section was not preserved");
        require(loaded.save(path, error) && mapping.load(path, error) && mapping.bindings[int(Action::Horn)].empty(),
            "intentionally unbound horn did not round-trip");
        require(!loaded.save(folder / "missing" / "controls.ini", error), "save error was hidden");
        std::filesystem::remove(path);
        std::filesystem::remove(folder);
        std::cout << "Controller defaults, held/remapped horn, raw USB input, analog input, edges, disconnect, legacy persistence and validation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Controller check failed: " << error.what() << '\n'; return 1; }
}
