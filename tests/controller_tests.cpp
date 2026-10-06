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
        ControllerMapping separate;
        separate.bindings[int(Action::FootForward)].clear();
        separate.add(Action::FootForward, {BindingKind::Key, KEY_UP});
        separate.bindings[int(Action::EnterVehicle)].clear();
        separate.add(Action::EnterVehicle, {BindingKind::Key, KEY_G});
        separate.bindings[int(Action::Jump)].clear();
        separate.add(Action::Jump, {BindingKind::Key, KEY_J});
        separate.bindings[int(Action::FootLookLeft)].clear();
        separate.add(Action::FootLookLeft, {BindingKind::Key, KEY_V});
        ControllerState modes;
        modes.keys[KEY_W] = modes.keys[KEY_E] = modes.keys[KEY_SPACE] = true;
        separate.update(modes);
        require(separate.value(Action::FootForward) == 0 && separate.value(Action::Forward) == 1 && separate.value(Action::PitchDown) == 1,
            "remapping walking changed car or plane movement");
        require(!separate.pressed(Action::EnterVehicle) && separate.pressed(Action::ExitVehicle) && separate.pressed(Action::PlaneExit),
            "remapping vehicle entry changed car or plane exit");
        require(!separate.pressed(Action::Jump) && separate.value(Action::Brake) == 1 && separate.value(Action::PlaneBrake) == 1,
            "remapping jump changed vehicle brakes");
        separate.update(modes);
        require(!separate.pressed(Action::ExitVehicle) && !separate.pressed(Action::PlaneExit),
            "holding entry/exit input retriggered an interaction across modes");
        modes = {}; modes.keys[KEY_UP] = modes.keys[KEY_G] = modes.keys[KEY_J] = modes.keys[KEY_V] = true;
        separate.update(modes);
        require(separate.value(Action::FootForward) == 1 && separate.value(Action::Forward) == 0 && separate.value(Action::PitchDown) == 0
            && separate.pressed(Action::EnterVehicle) && !separate.pressed(Action::ExitVehicle) && !separate.pressed(Action::PlaneExit)
            && separate.pressed(Action::Jump) && separate.value(Action::Brake) == 0 && separate.value(Action::PlaneBrake) == 0
            && separate.value(Action::FootLookLeft) == 1 && separate.value(Action::VehicleLookLeft) == 0 && separate.value(Action::PlaneLookLeft) == 0,
            "on-foot remaps leaked into vehicle controls");
        modes = {}; modes.keys[KEY_R] = true; separate.update(modes);
        require(separate.pressed(Action::Reload) && !separate.pressed(Action::Respawn), "reload also triggered on-foot respawn");
        modes = {}; modes.keys[KEY_F5] = true; separate.update(modes);
        require(separate.pressed(Action::Respawn) && !separate.pressed(Action::Reload), "on-foot respawn also reloaded");
        std::string separate_error;
        separate.auto_lock = false;
        require(separate.save(path, separate_error), separate_error.c_str());
        ControllerMapping restored;
        require(restored.load(path, separate_error) && restored.bindings == separate.bindings && !restored.auto_lock,
            "independent movement, interaction and camera remaps did not persist");
        separate.defaults();
        require(separate.bindings == ControllerMapping().bindings && separate.value(Action::Respawn) == 0 && !separate.pressed(Action::Respawn),
            "reset did not restore all mode defaults and clear held input");
        require(!separate.auto_lock, "restoring bindings changed the chosen aim mode");
        int next_action = 0;
        for (const auto& group : action_groups) {
            require(int(group.first) == next_action && int(group.end) > int(group.first), "binding tabs skipped or duplicated actions");
            for (int a = int(group.first); a < int(group.end); ++a) require(action_labels[a] && action_labels[a][0], "binding tab has an unnamed action");
            next_action = int(group.end);
        }
        require(next_action == action_count, "binding tabs omitted actions");
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
        require(!mapping.pressed(Action::Map) && !mapping.pressed(Action::Pause), "Escape must release the cursor outside mapped gameplay actions");
        mapping.update(escape);
        require(!mapping.pressed(Action::Map), "held Escape triggered the map");
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
        require(mapping.add(Action::EnterVehicle, *captured), "raw USB button was rejected");
        capture.start(input);
        require(!capture.poll(input), "held button was mistaken for a new binding");
        usb.buttons[0] = false; require(!capture.poll(input), "button release created a binding");
        usb.buttons[0] = true;
        require(bool(capture.poll(input)), "released USB button could not be captured again");
        usb.buttons[7] = true; mapping.update(input);
        require(mapping.pressed(Action::EnterVehicle) && mapping.value(Action::Jump) == 0, "raw USB buttons leaked into Xbox mappings or did not reach gameplay");
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
        mapping.add(Action::FootLookLeft, *captured); mapping.update(input);
        require(mapping.value(Action::FootLookLeft) > .8f, "raw axis did not drive its mapped action");
        usb.axes[4] = 0; mapping.update(input);
        require(mapping.value(Action::FootLookLeft) == 0, "centered raw axis activated input");
        capture.start(input);
        usb.hats[0] = 3; // Up + right: retain individual D-pad directions.
        captured = capture.poll(input);
        require(captured && *captured == Binding{BindingKind::JoystickHat, 0}, "USB D-pad hat was not captured");
        mapping.add(Action::Jump, *captured);
        mapping.add(Action::Flaps, {BindingKind::JoystickHat, 1}); mapping.update(input);
        require(mapping.pressed(Action::Jump) && mapping.pressed(Action::Flaps), "diagonal USB D-pad did not activate both directions");
        usb.connected = false; mapping.update(input);
        require(mapping.value(Action::EnterVehicle) == 0 && mapping.value(Action::Jump) == 0, "unplugged USB joystick retained input");
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
        loaded.add(Action::EnterVehicle, {BindingKind::Key, KEY_ENTER});
        require(loaded.save(path, error), "could not replace existing mapping file");
        require(mapping.load(path, error) && mapping.bindings == loaded.bindings, "replacement file was not persisted");
        const auto before = loaded.bindings;
        for (const char* invalid : {"version=1\n", "version=3\n", "version=2\n[car_forward]\nkey=9999\n", "version=2\ndeadzone=nan\n",
            "version=2\n[unknown]\nkey=87\n", "version=2\n[car_forward]\naxis=1,0\n", "version=2\n[car_forward]\nkey=87 garbage\n",
            "version=2\n[car_forward]\n[car_forward]\n", "; empty file\n"}) {
            std::ofstream(path) << invalid;
            require(!loaded.load(path, error) && !error.empty() && loaded.bindings == before && loaded.deadzone == .3f, "malformed file partially replaced working mappings");
        }
        std::ofstream(path) << "version=2\n; Partial files retain other defaults\n[car_forward]\nkey=265\n";
        require(loaded.load(path, error) && loaded.bindings[int(Action::Forward)].size() == 1 &&
            !loaded.bindings[int(Action::EnterVehicle)].empty(), "partial mapping did not retain unspecified defaults");
        require(loaded.auto_lock, "existing version 2 mappings did not default to auto lock");
        std::ofstream(path) << "version=2\nauto_lock=0\n";
        require(loaded.load(path, error) && !loaded.auto_lock, "free aim did not load");
        std::ofstream(path) << "version=2\nauto_lock=2\n";
        require(!loaded.load(path, error) && !loaded.auto_lock, "invalid aim mode was accepted or changed the current mode");
        std::ofstream(path) << "version=2\nauto_lock=1\n";
        require(loaded.load(path, error) && loaded.auto_lock, "auto lock did not load");
        require(loaded.bindings[int(Action::Horn)] == horn_defaults, "partial file without a horn section did not retain new horn defaults");
        require(loaded.bindings[int(Action::Cover)] == std::vector<Binding>{{BindingKind::Key, KEY_Q}, {BindingKind::Button, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}},
            "partial mappings lost cover defaults");
        require(loaded.bindings[int(Action::Fire)] == std::vector<Binding>{{BindingKind::Mouse, MOUSE_BUTTON_LEFT}, {BindingKind::Axis, GAMEPAD_AXIS_RIGHT_TRIGGER}},
            "partial mappings lost new fire defaults");
        std::ofstream(path) << "version=2\n[foot_zoom_out]\nkey=334\n[car_horn]\n";
        require(loaded.load(path, error) && loaded.bindings[int(Action::FootZoomOut)] == std::vector<Binding>{{BindingKind::Key, 334}} &&
            loaded.bindings[int(Action::Horn)].empty(), "explicitly unbound horn or existing INI section was not preserved");
        require(loaded.save(path, error) && mapping.load(path, error) && mapping.bindings[int(Action::Horn)].empty(),
            "intentionally unbound horn did not round-trip");
        std::ofstream(path) << "version=2\n[map]\nkey=291\nkey=256\nbutton=13\n[car_horn]\n";
        require(loaded.load(path,error) && loaded.bindings[int(Action::Horn)].empty(), "legacy Escape migration changed other bindings");
        loaded.update(escape);
        require(!loaded.pressed(Action::Map), "saved Escape map binding was not removed");
        ControllerState map_key; map_key.keys[KEY_F2] = true; loaded.update(map_key);
        require(loaded.pressed(Action::Map), "F2 stopped opening the map after Escape migration");
        require(!loaded.save(folder / "missing" / "controls.ini", error), "save error was hidden");
        std::filesystem::remove(path);
        std::filesystem::remove(folder);
        std::cout << "Independent foot/car/plane bindings, reset, held/remapped horn, raw USB input, analog input, edges, disconnect, persistence and validation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Controller check failed: " << error.what() << '\n'; return 1; }
}
