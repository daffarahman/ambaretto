#include "tuning_panel.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = .001f) { return std::abs(a - b) < tolerance; }
void tick(forza::PhysicsWorld& world, forza::Car& car, int steps) {
    for (int i = 0; i < steps; ++i) {
        car.step({0, 0, false, true}); world.step();
        require(std::isfinite(car.position().GetY()) && std::isfinite(car.velocity().Length()), "non-finite tuned physics");
    }
}
void live_physics() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    tick(world, car, 480);
    const float height = car.position().GetY(), compression = car.wheels()[0].compression;
    auto tuning = car.tuning();
    tuning.wheel_radius = .45f; tuning.rest_length = .74f;
    tuning.mount_height = .28f; tuning.spring_rate = 40000; tuning.damper_rate = 4300;
    car.set_tuning(tuning);
    tick(world, car, 720);
    require(car.position().GetY() > height + .23f, "live suspension/radius edits did not raise ride height");
    require(car.wheels()[0].compression < compression - .025f, "stronger springs did not reduce compression");
    for (const auto& wheel : car.wheels()) {
        require(wheel.grounded && near(wheel.center.GetY(), .45f, .01f), "tuned tire radius disagrees with ground contact");
        require(near(wheel.mount.GetY(), .28f), "mount height did not update wheel attachment points");
        require(wheel.normal_force > 1000 && wheel.normal_force <= tuning.max_spring_force, "invalid tuned spring force");
    }
    car.reset(forza::Vec3(0, 1, 0));
    require(near(car.tuning().rest_length, .74f) && near(car.tuning().wheel_radius, .45f), "reset erased tuning");
    require(near(car.wheels()[0].center.GetY(), 1 + .28f - .74f), "reset did not use tuned suspension geometry");
    car.set_tuning({});
    tick(world, car, 720);
    require(near(car.position().GetY(), height, .025f), "restoring defaults did not restore suspension behavior");
    tuning = car.tuning(); tuning.max_steer = .2f; car.set_tuning(tuning);
    for (int i = 0; i < 120; ++i) { car.step({0, 1, false, true}); world.step(); }
    require(near(car.steering(), .2f), "runtime steering limit was ignored");
    std::cout << "Live tuning: ride height, tire contact, springs, reset, steering passed\n";
}
void validation_and_isolation() {
    forza::PhysicsWorld world(false), other_world(false);
    forza::Car car(world), other(other_world);
    auto tuning = car.tuning();
    tuning.wheel_radius = std::numeric_limits<float>::quiet_NaN();
    tuning.spring_rate = std::numeric_limits<float>::infinity();
    tuning.damper_rate = -100; tuning.rest_length = -4; tuning.travel = 20;
    tuning.tire_grip = 100; tuning.max_spring_force = 4500;
    tuning.wheelbase = -100; tuning.track_width = std::numeric_limits<float>::infinity();
    tuning.top_speed = std::numeric_limits<float>::infinity(); tuning.acceleration = -100;
    car.set_tuning(tuning);
    for (const auto& control : forza::tuning_controls) {
        const float value = car.tuning().*(control.value);
        require(std::isfinite(value) && value >= control.min && value <= control.max, "unsafe tuning value escaped validation");
    }
    require(car.tuning().travel < car.tuning().rest_length, "travel allowed negative suspension length");
    require(near(other.tuning().tire_grip, 1.6f), "tuning leaked to another vehicle");
    tick(world, car, 480);
    for (const auto& wheel : car.wheels()) require(wheel.normal_force <= 4500, "spring force cap was ignored");
}
void wheel_spacing() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    const auto original_position = car.position();
    auto tuning = car.tuning();
    tuning.wheelbase = 3.1f; tuning.track_width = 1.92f;
    car.set_tuning(tuning);
    const auto& wheels = car.wheels();
    require((car.position() - original_position).Length() < .0001f, "wheel spacing teleported the chassis");
    require(near((wheels[1].center - wheels[0].center).Length(), 1.92f), "live track width did not move wheel centers");
    require(near((wheels[2].center - wheels[0].center).Length(), 3.1f), "live wheelbase did not move wheel centers");
    require(near((wheels[1].ground_point - wheels[0].ground_point).Length(), 1.92f), "track width did not move ground contacts");
    require(near((wheels[2].ground_point - wheels[0].ground_point).Length(), 3.1f), "wheelbase did not move ground contacts");
    tick(world, car, 480);
    for (const auto& wheel : wheels) require(wheel.grounded, "edited wheel spacing lost suspension support");
    car.reset(forza::Vec3(0, 1, 0), .8f);
    const auto across = car.rotate(forza::Vec3::sAxisX());
    require(near((wheels[1].center - wheels[0].center).Dot(across), 1.92f), "reset lost track width on a rotated chassis");
    require(near((wheels[0].center - wheels[2].center).Dot(car.forward()), 3.1f), "reset lost wheelbase on a rotated chassis");
    require(near(car.tuning().wheelbase, 3.1f) && near(car.tuning().track_width, 1.92f), "reset erased wheel spacing settings");
    car.set_tuning({});
    require(near((wheels[1].mount - wheels[0].mount).Length(), 1.64f) &&
            near((wheels[2].mount - wheels[0].mount).Length(), 2.5f), "defaults did not restore original wheel spacing");
    std::cout << "Wheel spacing: live geometry, ground contacts, support, rotated reset and defaults passed\n";
}
void panel_input() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    forza::TuningPanel panel;
    constexpr int width = 1280, height = 720;
    const auto rect = panel.bounds(width, height);
    const Vector2 slider{rect.x + 18 + 174, rect.y + 111 + 28};
    forza::TuningPanelInput input;
    input.mouse = slider; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    input.pressed = false; input.mouse.x = width + 300.0f;
    panel.update(car, width, height, input);
    require(near(car.tuning().wheel_radius, .55f), "slider drag did not clamp beyond its edge");
    input.down = false; panel.update(car, width, height, input);
    input.mouse = {100, 200}; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    require(near(car.tuning().wheel_radius, .55f), "canvas click continued a released slider drag");
    input = {}; input.mouse = slider; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    const float mid = car.tuning().wheel_radius;
    input.focused = false; panel.update(car, width, height, input);
    input.focused = true; input.pressed = false; input.mouse.x += 130;
    panel.update(car, width, height, input);
    require(near(car.tuning().wheel_radius, mid), "focus loss did not cancel dragging");
    input = {}; input.mouse = slider; input.fine = true; input.horizontal = 1;
    panel.update(car, width, height, input);
    require(near(car.tuning().wheel_radius, mid + .0005f, .00001f), "fine keyboard adjustment was ignored");
    input = {}; input.mouse = {rect.x + 180, rect.y + 80}; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    input = {}; input.horizontal = 1; panel.update(car, width, height, input);
    require(near(car.tuning().tire_grip, 1.65f), "handling tab edited the wrong setting");
    input = {}; input.vertical = 1;
    for (int i = 0; i < 4; ++i) panel.update(car, width, height, input);
    input = {}; input.horizontal = 1; panel.update(car, width, height, input);
    require(near(car.tuning().wheelbase, 2.51f), "wheelbase control edited the wrong setting");
    input = {}; input.vertical = 1; panel.update(car, width, height, input);
    input = {}; input.horizontal = 1; input.fine = true; panel.update(car, width, height, input);
    require(near(car.tuning().track_width, 1.641f, .00001f), "track width fine adjustment failed");
    input = {}; input.mouse = {rect.x + 290, rect.y + 80}; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    input = {}; input.horizontal = 1; panel.update(car, width, height, input);
    require(near(car.tuning().top_speed * 3.6f, 245, .01f), "performance tab did not edit top speed in km/h");
    input = {}; input.vertical = 1; panel.update(car, width, height, input);
    input = {}; input.horizontal = 1; input.fine = true; panel.update(car, width, height, input);
    require(near(car.tuning().acceleration, 9.025f, .0001f), "acceleration fine adjustment failed");
    input = {}; input.mouse = {rect.x + 50, rect.y + rect.height - 43}; input.pressed = input.down = true;
    panel.update(car, width, height, input);
    require(near(car.tuning().wheel_radius, .34f) && near(car.tuning().tire_grip, 1.6f), "defaults failed across tabs");
    require(near(car.tuning().wheelbase, 2.5f) && near(car.tuning().track_width, 1.64f), "panel defaults failed for wheel spacing");
    require(near(car.tuning().top_speed * 3.6f, 240, .01f) && near(car.tuning().acceleration, 9), "defaults did not restore performance");
    input.mouse.x = rect.x + 180;
    require(panel.update(car, width, height, input).reset_car, "reset car action did not reach game controls");
    input.mouse.x = rect.x + 290;
    require(panel.update(car, width, height, input).close, "close action did not reach game controls");
    require(!panel.contains({100, 200}, width, height), "panel consumes camera interaction outside its bounds");
    std::cout << "Panel input: dragging, release, focus loss, precision, tabs, reset and close passed\n";
}
void engine_performance() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    const auto drive = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            car.step({1, 0, false}); world.step();
            require(std::isfinite(car.velocity().Length()) && car.rotate(forza::Vec3::sAxisY()).GetY() > .98f,
                "high-speed driving became unstable");
        }
        return car.velocity().Dot(car.forward()) * 3.6f;
    };
    tick(world, car, 120);
    const float four_seconds = drive(480);
    std::cout << "Default speed after 4 seconds: " << four_seconds << " km/h\n";
    require(four_seconds > 90, "default acceleration did not exceed the old 86 km/h limit quickly");
    const float stock = drive(120 * 26);
    std::cout << "Default top speed: " << stock << " km/h, velocity " << car.velocity().Length()
        << ", grounded " << car.wheels()[0].grounded << car.wheels()[1].grounded << car.wheels()[2].grounded << car.wheels()[3].grounded << '\n';
    require(near(stock, 240, 1.5f), "default top speed did not reach 240 km/h");
    auto tuning = car.tuning(); tuning.top_speed = 120 / 3.6f; car.set_tuning(tuning);
    const float before = car.velocity().Length();
    drive(1);
    require(car.velocity().Length() < before && car.velocity().Length() > before - .2f,
        "lowering top speed teleported or clamped vehicle velocity");
    require(near(drive(120 * 10), 120, 1), "live top-speed reduction did not slow the car");
    tuning.top_speed = 360 / 3.6f; tuning.acceleration = 12; tuning.motor_grip = 1.4f;
    car.set_tuning(tuning);
    const float tuned = drive(120 * 25);
    require(near(tuned, 360, 1.5f), "tuned engine still has an old hard-coded speed limit");
    tuning.top_speed = 400 / 3.6f; car.set_tuning(tuning);
    require(near(drive(120 * 10), 400, 1.5f), "maximum speed setting could not sustain 400 km/h");
    tuning.top_speed = 360 / 3.6f; car.set_tuning(tuning);
    car.reset(forza::Vec3(0, .56f, 0));
    require(near(car.tuning().top_speed * 3.6f, 360, .01f) && near(car.tuning().acceleration, 12),
        "reset erased engine tuning");
    tuning.acceleration = 2; car.set_tuning(tuning); tick(world, car, 120);
    const float slow = drive(240);
    car.reset(forza::Vec3(0, .56f, 0)); tuning.acceleration = 12; car.set_tuning(tuning); tick(world, car, 120);
    const float fast = drive(240);
    require(fast > slow + 35, "acceleration slider did not change engine response");
    std::cout << "Engine: " << four_seconds << " km/h after 4 seconds, " << stock << " stock / " << tuned
        << " tuned top speed; acceleration " << slow << " / " << fast << " km/h after 2 seconds\n";
}
} // namespace
int main() {
    try { live_physics(); validation_and_isolation(); wheel_spacing(); panel_input(); engine_performance(); return 0; }
    catch (const std::exception& error) { std::cerr << "Tuning check failed: " << error.what() << '\n'; return 1; }
}
