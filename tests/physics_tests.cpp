#include "vehicle.hpp"
#include "environment.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr double pi = 3.141592653589793;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
double slip_degrees(const ambaretto::Car& car) {
    auto forward = car.forward();
    auto velocity = car.velocity();
    forward.SetY(0);
    velocity.SetY(0);
    if (velocity.Length() < 2) return 0;
    return std::acos(std::clamp(double(velocity.Normalized().Dot(forward.Normalized())), -1.0, 1.0)) * 180 / pi;
}
void tick(ambaretto::PhysicsWorld& world, ambaretto::Car& car, ambaretto::Input input, int steps) {
    for (int i = 0; i < steps; ++i) {
        car.step(input);
        world.step();
        require(std::isfinite(double(car.position().GetY())), "non-finite chassis state");
    }
}

void test_settle_drive_reverse() {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    tick(world, car, {}, 240);
    require(car.position().GetY() > 0.35 && car.position().GetY() < 0.75, "car did not settle on suspension");
    tick(world, car, {1, 0, false}, 480);
    require(car.position().GetZ() < -4, "throttle did not move forward");
    tick(world, car, {-1, 0, false}, 1200);
    require(car.velocity().Dot(car.forward()) < -4, "reverse input did not reverse the car");
}

void test_no_wheelie() {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    double peak_pitch = 0;
    int front_air = 0;
    for (int i = 0; i < 600; ++i) {
        tick(world, car, {1, 0, false}, 1);
        peak_pitch = std::max(peak_pitch, std::asin(std::clamp(double(car.forward().GetY()), -1.0, 1.0)) * 180 / pi);
        if (i > 120 && !car.wheels()[0].grounded && !car.wheels()[1].grounded) ++front_air;
    }
    std::cout << "Full throttle: pitch " << peak_pitch << " deg, front airborne " << front_air << " steps\n";
    require(peak_pitch < 12.6 && front_air < 60, "acceleration lifted the front wheels");
}

void test_speed_independent_suspension() {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    tick(world, car, {0, 0, false, true}, 480);
    const float rest_height = car.position().GetY();
    for (float target : {80.0f, 240.0f, 400.0f}) {
        auto tuning = car.tuning(); tuning.top_speed = target / 3.6f; car.set_tuning(tuning);
        float peak_height_error = 0, peak_wheel_error = 0;
        for (int i = 0; i < 120 * 25; ++i) {
            tick(world, car, {1, 0, false}, 1);
            for (const auto& wheel : car.wheels()) {
                const auto local = car.rotation().Conjugated() * (car.wheel_center(wheel) - car.position());
                peak_wheel_error = std::max(peak_wheel_error, std::hypot(local.GetX() - wheel.mount.GetX(), local.GetZ() - wheel.mount.GetZ()));
                require(std::abs(local.GetY() - wheel.mount.GetY() + tuning.rest_length) <= tuning.travel + .001f,
                    "wheel exceeded suspension travel after physics integration");
                if (i > 120 * 23) require(wheel.grounded, "steady high-speed driving lost tire contact");
            }
            if (i > 120 * 23) peak_height_error = std::max(peak_height_error, std::abs(car.position().GetY() - rest_height));
        }
        std::cout << "Suspension at " << car.velocity().Length() * 3.6f << " km/h: ride-height error "
            << peak_height_error << " m, axle alignment error " << peak_wheel_error << " m\n";
        require(car.velocity().Length() * 3.6f > target - 2, "suspension speed check did not reach target speed");
        require(peak_height_error < .015f, "forward speed changed the car's steady ride height");
        require(peak_wheel_error < .001f, "rendered wheels lagged behind the chassis after physics integration");
    }
}

void test_grip_steering() {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    double max_slip = 0;
    for (int i = 0; i < 360; ++i) {
        tick(world, car, {1, float(0.22), false}, 1);
        if (i >= 60) max_slip = std::max(max_slip, slip_degrees(car));
    }
    std::cout << "Powered corner: slip " << max_slip << " deg\n";
    require(max_slip < 8 && car.position().GetX() < -1, "powered steering lost grip or did not turn left");
}

void test_high_speed_steering() {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    tick(world, car, {1, 0, false}, 300);
    auto start_forward = car.forward();
    start_forward.SetY(0);
    double max_slip = 0;
    for (int i = 0; i < 180; ++i) {
        tick(world, car, {1, 1, false}, 1);
        max_slip = std::max(max_slip, slip_degrees(car));
    }
    auto end_forward = car.forward();
    end_forward.SetY(0);
    const double heading_change = std::acos(std::clamp(
        double(start_forward.Normalized().Dot(end_forward.Normalized())), -1.0, 1.0)) * 180 / pi;
    std::cout << "High-speed steering: heading " << heading_change << " deg, slip " << max_slip << " deg\n";
    require(heading_change > 40 && max_slip < 15, "high-speed steering is unresponsive or drifts");
}

struct DriftResult { double slip; double speed; double recovered; int skid_steps; };
DriftResult run_drift(bool handbrake) {
    ambaretto::PhysicsWorld world(false);
    ambaretto::Car car(world);
    tick(world, car, {1, 0, false}, 300);
    double max_slip = 0;
    int skid_steps = 0;
    for (int i = 0; i < 150; ++i) {
        tick(world, car, {1, 1, handbrake}, 1);
        max_slip = std::max(max_slip, slip_degrees(car));
        if (car.wheels()[2].skidding || car.wheels()[3].skidding) ++skid_steps;
    }
    const double speed = car.velocity().Length();
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
    ambaretto::PhysicsWorld world(true);
    ambaretto::Car car(world);
    tick(world, car, {1, 0, false}, 720);
    require(car.position().GetZ() < -45 && car.position().GetY() > 0.25 && car.position().GetY() < 1.5,
            "car failed to cross the suspension ridges");
}

void test_ground_queries_and_chassis_collision() {
    ambaretto::PhysicsWorld world(true);
    ambaretto::Car car(world);
    require(std::abs(car.position().GetY() - 0.56f) < 0.001f, "chassis center of mass is misplaced");
    ambaretto::GroundHit hit;
    require(world.cast_ground(ambaretto::Vec3(0, 2, 0), ambaretto::Vec3(0, -1, 0), 4, hit), "ground ray missed");
    require(std::abs(hit.point.GetY()) < 0.001f && hit.normal.GetY() > 0.99f,
            "wheel ray hit the chassis instead of the ground");
    require(world.cast_ground(ambaretto::Vec3(0, 2, -15), ambaretto::Vec3(0, -1, 0), 4, hit), "ridge ray missed");
    require(std::abs(hit.point.GetY() - 0.12f) < 0.005f, "wheel ray missed the ridge surface");
    // Disable suspension impulses to check Jolt's actual chassis collisions.
    for (int i = 0; i < 360; ++i) world.step();
    require(car.position().GetY() > -0.35f && car.position().GetY() < 0.1f,
            "chassis collider fell through the floor");
    require(car.velocity().Length() < 0.5f, "chassis collision did not settle");
}

void test_world_reset_lifetime() {
    struct TestScene {
        ambaretto::PhysicsWorld world{false};
        ambaretto::Car car{world};
    };
    auto scene = std::make_unique<TestScene>();
    for (int i = 0; i < 8; ++i) {
        tick(scene->world, scene->car, {1, 0, false}, 60);
        // Like R in the game: construct the replacement before destroying
        // the old world. Jolt registration must remain valid for both.
        scene = std::make_unique<TestScene>();
    }
    tick(scene->world, scene->car, {1, 0, false}, 240);
    require(scene->car.position().GetZ() < -4, "driving failed after world reset");
}

void test_city_terrain() {
    const ambaretto::Environment map;
    ambaretto::PhysicsWorld world(map);
    ambaretto::Car car(world);
    ambaretto::GroundHit hit;
    for (const auto& p : {ambaretto::Vec3(0, 0, 105), ambaretto::Vec3(-95, 0, -85),
            ambaretto::Vec3(283, 0, 40), ambaretto::Vec3(-210, 0, 80), ambaretto::Vec3(350, 0, 250)}) {
        require(world.cast_ground(p + ambaretto::Vec3(0, 100, 0), ambaretto::Vec3(0, -1, 0), 150, hit),
                "city terrain has a collision hole");
        require(std::abs(hit.point.GetY() - map.height(p.GetX(), p.GetZ())) < 0.005f,
                "rendered terrain does not match Jolt collision height");
        require(hit.normal.GetY() > 0.7f, "city slope is too steep to drive");
    }
    car.reset(map.spawn());
    tick(world, car, {}, 120);
    require(std::abs(car.position().GetY() - map.height(0, 105) - 0.56f) < 0.15f,
            "car did not settle at the city spawn");
    for (int i = 0; i < 1200; ++i) {
        // Follow the lane: dense frontage no longer leaves a field beside the avenue.
        const auto target = ambaretto::Vec3(-car.position().GetX(), 0, -12);
        const float curvature = 2 * target.Dot(-car.forward().Cross(ambaretto::Vec3::sAxisY())) / target.LengthSq();
        const float steer = std::atan(car.tuning().wheelbase * curvature) / car.tuning().max_steer;
        tick(world, car, {1, std::clamp(steer, -1.0f, 1.0f), false}, 1);
        require(std::abs(car.position().GetX()) < 3, "car failed to stay on the narrow avenue");
    }
    const float terrain_y = map.height(car.position().GetX(), car.position().GetZ());
    std::cout << "City avenue: position " << car.position().GetX() << ", " << car.position().GetY()
        << ", " << car.position().GetZ() << ", elevation " << terrain_y << " m\n";
    require(car.position().GetZ() < -30 && std::abs(terrain_y - ambaretto::Environment::road_level) < .01f,
            "car failed to follow the Miami avenue");
    require(std::abs(car.position().GetY() - terrain_y - 0.56f) < 0.7f,
            "car lost the raised terrain");
    const auto& building = map.buildings().front();
    const float x = building.center.GetX(), z = building.center.GetZ();
    require(world.cast_ground(ambaretto::Vec3(x, 100, z), ambaretto::Vec3(0, -1, 0), 150, hit)
            && std::abs(hit.point.GetY() - map.height(x, z)) < 0.005f,
            "suspension ray treated a building roof as terrain");
    const float approach = z + building.size.GetZ() / 2 + 12;
    car.reset(ambaretto::Vec3(x, map.height(x, approach) + 0.56f, approach));
    tick(world, car, {1, 0, false}, 480);
    require(car.position().GetZ() > z + building.size.GetZ() / 2,
            "car passed through a city building");
    require(map.submerged(ambaretto::Vec3(330, -1, 0)), "water recovery did not trigger");
    car.reset(map.spawn());
    require(car.velocity().Length() < 0.001f && !car.wheels()[2].skidding,
            "recovery retained velocity or skid state");
}
} // namespace

int main() {
    try {
        test_settle_drive_reverse();
        test_no_wheelie();
        test_speed_independent_suspension();
        test_grip_steering();
        test_high_speed_steering();
        test_drift_recovery();
        test_ridges();
        test_ground_queries_and_chassis_collision();
        test_world_reset_lifetime();
        test_city_terrain();
        std::cout << "All vehicle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vehicle check failed: " << error.what() << '\n';
        return 1;
    }
}
