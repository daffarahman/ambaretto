#include "player.hpp"
#include "environment.hpp"
#include "third_person_camera.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void tick(forza::PhysicsWorld& world, forza::Character& character, forza::FootInput input, int steps) {
    for (int i = 0; i < steps; ++i) { world.step(); character.step(input); }
}
void character_movement() {
    forza::PhysicsWorld world(false);
    forza::Character character(world);
    character.reset(forza::Vec3(0, 0.08f, 0));
    tick(world, character, {}, 120);
    require(character.grounded() && std::abs(character.position().GetY()) < 0.08f, "character did not stand on ground");
    tick(world, character, {forza::Vec3(0, 0, -1), false, false}, 120);
    require(character.position().GetZ() < -2.8f, "walking did not move forward");
    const float start = character.position().GetZ();
    tick(world, character, {forza::Vec3(0, 0, -1), true, false}, 120);
    require(start - character.position().GetZ() > 6, "sprint did not increase movement speed");
    tick(world, character, {}, 120);
    require(character.velocity().Length() < 0.2f, "character did not stop after releasing movement");
    character.step({forza::Vec3::sZero(), false, true});
    float peak = character.position().GetY();
    bool airborne = false;
    for (int i = 0; i < 150; ++i) {
        tick(world, character, {}, 1);
        peak = std::max(peak, character.position().GetY());
        airborne |= !character.grounded();
    }
    require(airborne && peak > 0.7f && character.grounded(), "jump did not rise and land");
    std::cout << "Character jump: " << peak << " m; walking and sprint passed\n";
}

void city_collisions_and_interaction() {
    const forza::Environment map;
    forza::PhysicsWorld world(map);
    forza::Car car(world);
    forza::Player player(world, car, map);
    for (int i = 0; i < 120; ++i) player.step({}, {});
    require(player.driving() && player.interact() == forza::Interaction::Exited && !player.driving(), "could not exit car");
    const auto parked = car.position();
    for (int i = 0; i < 240; ++i) player.step({}, {});
    require((car.position() - parked).Length() < 0.8f, "parked car rolled away");
    require(player.character().grounded(), "exit did not place character on terrain");
    require(player.can_enter() && player.interact() == forza::Interaction::Entered, "could not enter nearby car");
    require(player.interact() == forza::Interaction::Exited, "second exit failed");
    for (int i = 0; i < 180; ++i) player.step({}, {forza::Vec3(-1, 0, 0), true, false});
    require(!player.can_enter() && player.interact() == forza::Interaction::TooFar, "entered a car from too far away");
    player.reset();
    for (int i = 0; i < 150; ++i) player.step({1, 0, false}, {});
    require(player.interact() == forza::Interaction::TooFast && player.driving(), "exited a car at speed");

    const auto& building = map.buildings().front();
    const float x = building.center.GetX(), z = building.center.GetZ();
    require(!player.character().can_stand_at(forza::Vec3(x, map.height(x, z) + 0.08f, z)), "exit collision check accepted a building interior");
    const float beside = x + building.size.GetX() / 2 + 1.3f;
    car.reset(forza::Vec3(beside, map.height(beside, z) + 0.56f, z));
    require(player.interact() == forza::Interaction::Exited && player.position().GetX() > car.position().GetX(),
            "did not use opposite door when driver-side exit was blocked");
    auto& character = player.character();
    const float approach = z + building.size.GetZ() / 2 + 12;
    character.reset(forza::Vec3(x, map.height(x, approach) + 0.08f, approach));
    tick(world, character, {forza::Vec3(0, 0, -1), true, false}, 300);
    require(character.position().GetZ() > z + building.size.GetZ() / 2, "character walked through a building");
    character.reset(forza::Vec3(0, map.height(0, 105) + 0.08f, 105));
    tick(world, character, {forza::Vec3(0, 0, -1), true, false}, 1200);
    require(character.position().GetZ() < 45 && character.grounded(), "character failed to walk up the city avenue");
    require(std::abs(character.position().GetY() - map.height(character.position().GetX(), character.position().GetZ())) < 0.15f,
            "character did not follow terrain elevation");
    car.reset(map.spawn());
    character.reset(forza::Vec3(0, map.height(0, 113) + 0.08f, 113));
    for (int i = 0; i < 240; ++i) player.step({}, {forza::Vec3(0, 0, -1), true, false});
    require(character.position().GetZ() > car.position().GetZ() + 1.7f,
            "character walked through the parked car");
    const forza::Vec3 camera_origin(x, building.center.GetY(), approach);
    require(world.camera_fraction(camera_origin, forza::Vec3(0, 0, -30)) < 0.5f, "camera passed through building");
    player.reset();
    require(player.driving() && player.character().velocity().Length() < 0.001f, "reset retained on-foot state or motion");
}

void car_coasting_and_direction_changes() {
    const forza::Environment map;
    forza::PhysicsWorld world(false);
    forza::Car car(world);
    forza::Player player(world, car, map);
    const auto advance = [&](forza::Input input, int steps) {
        for (int i = 0; i < steps; ++i) player.step(input, {});
    };
    const auto speed = [&] { return car.velocity().Dot(car.forward()); };
    car.reset(forza::Vec3(0, .56f, 0));
    advance({}, 240);
    for (float direction : {1.0f, -1.0f}) {
        advance({direction}, 240);
        const float powered_speed = speed() * direction;
        require(powered_speed > 4, "player car did not accelerate");
        advance({}, 120);
        require(speed() * direction > powered_speed * .9f, "releasing player throttle applied automatic brakes");
        int steps = 0;
        while (std::abs(speed()) > .1f && steps++ < 1200) advance({-direction}, 1);
        require(steps < 1200, "opposite direction did not brake to a stop");
        advance({-direction}, 20);
        require(std::abs(speed()) < .1f, "opposite direction skipped the stopped pause");
        advance({}, 60);
        require(std::abs(speed()) < .1f, "released opposite input kept driving");
        advance({-direction}, 20);
        require(std::abs(speed()) < .1f, "released input did not cancel the direction-change timer");
        advance({-direction}, 240);
        require(speed() * direction < -4, "held opposite direction did not drive after braking");
    }
    advance({0, 0, false, true}, 240);
    require(std::abs(speed()) < .1f, "player parking brake no longer stops the car");
    car.reset(forza::Vec3(0, .56f, 0));
    advance({}, 240);
    advance({-1}, 12);
    require(speed() < -.25f, "recovery retained the old drive direction");
    std::cout << "Player car: coasting, both direction changes, stopped pause, release, parking and recovery passed\n";
}

void mouse_camera() {
    forza::ThirdPersonCamera camera;
    require(camera.move_direction(1, 1).Length() <= 1.001f, "diagonal movement is faster");
    camera.look(300, 0, 0, false, forza::Vec3(0, 0, -1), 0, 1.0f / 60);
    require(camera.move_direction(1, 0).GetX() > 0.6f, "mouse look did not rotate camera-relative movement");
    camera.look(0, 10000, 0, false, forza::Vec3(0, 0, -1), 0, 1.0f / 60);
    require(camera.pitch() <= 1.12f, "mouse pitch flipped camera");
    camera.look(0, 0, 1000, false, forza::Vec3(0, 0, -1), 0, 1.0f / 60);
    require(camera.desired_position(forza::Vec3::sZero(), false).Length() >= 2.49f, "camera zoom entered the character");
}
void model_tree_collisions() {
    const forza::Environment map;
    forza::PhysicsWorld world(map);
    forza::Character character(world);
    // Pick an isolated tree outside the city so nearby buildings or other
    // trunks cannot make a collision query pass for the wrong reason.
    for (const auto& tree : map.trees()) {
        if (std::abs(tree.base.GetX()) < 150 && std::abs(tree.base.GetZ()) < 150) continue;
        bool isolated = true;
        for (const auto& other : map.trees())
            if (&other != &tree && (tree.base - other.base).Length() < 10) isolated = false;
        if (!isolated) continue;
        require(!character.can_stand_at(tree.base + forza::Vec3(0, .08f, 0)), "character can overlap the model's tree trunk");
        require(character.can_stand_at(tree.base + forza::Vec3(0, tree.height * .75f, 0)), "tree has an invisible trunk collider in its upper foliage");
        const forza::Vec3 across(0, 0, -4);
        require(world.camera_fraction(tree.base + forza::Vec3(0, 1, 2), across) < .5f, "camera passed through the model's tree trunk");
        require(world.camera_fraction(tree.base + forza::Vec3(0, tree.height * .85f, 2), across) > .99f,
            "tree foliage unexpectedly blocks the camera");
        return;
    }
    require(false, "no isolated tree available for collision checks");
}
}
int main() {
    try {
        character_movement(); city_collisions_and_interaction(); car_coasting_and_direction_changes(); mouse_camera(); model_tree_collisions();
        std::cout << "All player checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player check failed: " << error.what() << '\n';
        return 1;
    }
}
