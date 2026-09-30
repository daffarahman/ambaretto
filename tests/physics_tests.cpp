#include "vehicle.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double pi = 3.141592653589793;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
double slip_degrees(const forza::Car& car) {
    auto forward = car.forward();
    auto velocity = car.velocity();
    forward.setY(0);
    velocity.setY(0);
    if (velocity.length() < 2) return 0;
    return std::acos(std::clamp(double(velocity.normalized().dot(forward.normalized())), -1.0, 1.0)) * 180 / pi;
}
void tick(forza::PhysicsWorld& world, forza::Car& car, forza::Input input, int steps) {
    for (int i = 0; i < steps; ++i) {
        car.step(input);
        world.step();
        require(std::isfinite(double(car.position().y())), "non-finite chassis state");
    }
}

void test_settle_drive_reverse() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    tick(world, car, {}, 240);
    require(car.position().y() > 0.35 && car.position().y() < 0.75, "car did not settle on suspension");
    tick(world, car, {1, 0, false}, 480);
    require(car.position().z() < -4, "throttle did not move forward");
    tick(world, car, {-1, 0, false}, 1200);
    require(car.velocity().dot(car.forward()) < -4, "reverse input did not reverse the car");
}

void test_no_wheelie() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    double peak_pitch = 0;
    int front_air = 0;
    for (int i = 0; i < 600; ++i) {
        tick(world, car, {1, 0, false}, 1);
        peak_pitch = std::max(peak_pitch, std::asin(std::clamp(double(car.forward().y()), -1.0, 1.0)) * 180 / pi);
        if (i > 120 && !car.wheels()[0].grounded && !car.wheels()[1].grounded) ++front_air;
    }
    std::cout << "Full throttle: pitch " << peak_pitch << " deg, front airborne " << front_air << " steps\n";
    require(peak_pitch < 12.6 && front_air < 60, "acceleration lifted the front wheels");
}

void test_grip_steering() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    double max_slip = 0;
    for (int i = 0; i < 360; ++i) {
        tick(world, car, {1, btScalar(0.22), false}, 1);
        if (i >= 60) max_slip = std::max(max_slip, slip_degrees(car));
    }
    std::cout << "Powered corner: slip " << max_slip << " deg\n";
    require(max_slip < 8 && car.position().x() < -1, "powered steering lost grip or did not turn left");
}

void test_high_speed_steering() {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    tick(world, car, {1, 0, false}, 300);
    auto start_forward = car.forward();
    start_forward.setY(0);
    double max_slip = 0;
    for (int i = 0; i < 180; ++i) {
        tick(world, car, {1, 1, false}, 1);
        max_slip = std::max(max_slip, slip_degrees(car));
    }
    auto end_forward = car.forward();
    end_forward.setY(0);
    const double heading_change = std::acos(std::clamp(
        double(start_forward.normalized().dot(end_forward.normalized())), -1.0, 1.0)) * 180 / pi;
    std::cout << "High-speed steering: heading " << heading_change << " deg, slip " << max_slip << " deg\n";
    require(heading_change > 40 && max_slip < 15, "high-speed steering is unresponsive or drifts");
}

struct DriftResult { double slip; double speed; double recovered; int skid_steps; };
DriftResult run_drift(bool handbrake) {
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    tick(world, car, {1, 0, false}, 300);
    double max_slip = 0;
    int skid_steps = 0;
    for (int i = 0; i < 150; ++i) {
        tick(world, car, {1, 1, handbrake}, 1);
        max_slip = std::max(max_slip, slip_degrees(car));
        if (car.wheels()[2].skidding || car.wheels()[3].skidding) ++skid_steps;
    }
    const double speed = car.velocity().length();
    tick(world, car, {1, 0, false}, 180);
    return {max_slip, speed, slip_degrees(car), skid_steps};
}

void test_drift_recovery() {
    const auto normal = run_drift(false);
    const auto drift = run_drift(true);
    std::cout << "Normal corner: " << normal.slip << " deg; drift: " << drift.slip
              << " deg at " << drift.speed << " m/s; recovered: " << drift.recovered << " deg\n";
    require(drift.slip > normal.slip + 5, "handbrake did not create a slide");
    require(drift.slip < 40 && drift.recovered < 10, "drift is too strong or cannot recover");
    require(drift.skid_steps > 30, "drifting tires did not report skid contacts");
}

void test_ridges() {
    forza::PhysicsWorld world(true);
    forza::Car car(world);
    tick(world, car, {1, 0, false}, 720);
    require(car.position().z() < -45 && car.position().y() > 0.25 && car.position().y() < 1.5,
            "car failed to cross the suspension ridges");
}
} // namespace

int main() {
    try {
        test_settle_drive_reverse();
        test_no_wheelie();
        test_grip_steering();
        test_high_speed_steering();
        test_drift_recovery();
        test_ridges();
        std::cout << "All vehicle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vehicle check failed: " << error.what() << '\n';
        return 1;
    }
}
