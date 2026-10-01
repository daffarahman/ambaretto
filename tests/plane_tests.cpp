#include "airport.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include "player.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace forza;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void tick(PhysicsWorld& world, Plane& plane, FlightInput input, int steps) {
    for (int i = 0; i < steps; ++i) { plane.step(input); world.step(); }
}
void airport_and_takeoff() {
    Environment map;
    PhysicsWorld world(map);
    Plane plane(world);
    GroundHit hit;
    for (int x = -164; x <= 164; x += 4) for (int dz : {-10, 0, 10}) {
        const float z = Airport::runway_z + dz;
        const float px = Airport::center_x + x;
        require(std::abs(map.height(px, z) - Airport::elevation) < .001f, "runway is not level");
        require(world.cast_ground(Vec3(px, 20, z), Vec3(0, -1, 0), 30, hit)
            && std::abs(hit.point.GetY() - map.height(px, z)) < .001f, "airport render and collision heights differ");
    }
    for (int z = int(Airport::apron_z); z <= 400; ++z)
        require(map.road(Airport::center_x, float(z)) && map.height(Airport::center_x, float(z)) > .5f, "airport access road has a gap or enters water");
    for (const auto& tree : map.trees())
        require(!Airport::contains(tree.base.GetX(), tree.base.GetZ()), "tree obstructs airport");
    Car car(world);
    plane.reset(Vec3(Airport::plane_x, Airport::elevation + Plane::parked_height, Airport::runway_z));
    car.reset(Vec3(Airport::center_x, map.height(Airport::center_x, -400) + .56f, -400));
    for (int i = 0; i < 1440 && car.position().GetZ() > Airport::apron_z; ++i) {
        car.step({1, 0, false}); plane.step({0, 0, 0, 0, false, false, true}); world.step();
    }
    std::cout << "Airport road: car z " << car.position().GetZ() << '\n';
    require(car.position().GetZ() <= Airport::apron_z, "car cannot reach airport from access avenue");
    plane.reset(Vec3(Airport::plane_x, Airport::elevation + Plane::parked_height, Airport::runway_z));
    tick(world, plane, {0, 0, 0, 0, false, false, true}, 480);
    require(plane.grounded() && std::abs(plane.position().GetY() - Airport::elevation - Plane::parked_height) < .15f,
        "plane did not settle on landing gear");
    require(plane.velocity().Length() < .15f && plane.throttle() == 0, "parked plane moves or engine runs");
    const float start = plane.position().GetX();
    float takeoff_distance = -1;
    for (int i = 0; i < 2160; ++i) {
        FlightInput input;
        input.throttle = 1;
        input.pitch = plane.airspeed() > 28 ? .5f : 0;
        plane.step(input); world.step();
        if (!plane.grounded() && plane.position().GetY() > Airport::elevation + 3 && takeoff_distance < 0)
            takeoff_distance = plane.position().GetX() - start;
    }
    std::cout << "Takeoff: " << takeoff_distance << " m, altitude " << plane.position().GetY()
        << " m, airspeed " << plane.airspeed() << " m/s\n";
    require(takeoff_distance > 50 && takeoff_distance < 280, "aircraft did not take off before runway end");
    require(plane.position().GetY() > 20 && !plane.damaged(), "powered climb failed or damaged aircraft");
}
void flight_controls_and_gravity() {
    PhysicsWorld world(false);
    Plane plane(world);
    plane.reset(Vec3(0, 100, 0), 0);
    tick(world, plane, {}, 120);
    require(plane.position().GetY() < 96, "plane hovers without airspeed or thrust");
    plane.reset(Vec3(0, 100, 0), 0, Vec3(0, -25, -20));
    plane.step({});
    require(plane.stalled(), "high angle of attack did not stall");
    world.step();
    plane.reset(Vec3(0, 100, 0), 0, Vec3(0, 0, -42), .7f);
    tick(world, plane, {0, .45f, 0, 0}, 240);
    require(plane.forward().GetY() > .12f && plane.position().GetY() > 102, "elevator did not pitch up and climb");
    plane.reset(Vec3(0, 100, 0), 0, Vec3(0, 0, -42), .7f);
    tick(world, plane, {0, 0, .5f, 0}, 120);
    require(plane.rotate(Vec3::sAxisY()).GetX() < -.25f, "ailerons did not bank left");
    tick(world, plane, {}, 240);
    require(plane.velocity().GetX() < -2, "banking did not turn lift and flight path");
    plane.reset(Vec3(0, 100, 0), 0, Vec3(0, 0, -42), .7f);
    tick(world, plane, {0, 0, 0, .7f}, 120);
    require(plane.forward().GetX() < -.05f, "rudder did not yaw left");
    plane.reset(Vec3(0, 150, 0), 0, Vec3(0, 0, -38));
    tick(world, plane, {}, 1200);
    require(plane.position().GetZ() < -250 && plane.position().GetY() > 50 && plane.position().GetY() < 150,
        "unpowered aircraft did not glide under aerodynamic forces");
    require(std::isfinite(plane.velocity().Length()) && plane.velocity().Length() < 100, "flight became unstable");
}
void landing_brakes_and_collisions() {
    PhysicsWorld world(false);
    Plane plane(world);
    plane.reset(Vec3(0, 2, 0), 0, Vec3(0, -1, -28));
    tick(world, plane, {0, 0, 0, 0, true}, 960);
    std::cout << "Landing: height " << plane.position().GetY() << ", speed " << plane.velocity().Length() << '\n';
    require(plane.grounded() && plane.velocity().Length() < .3f && !plane.damaged(), "gear touchdown or braking failed");
    require(std::abs(plane.position().GetY() - Plane::parked_height) < .15f, "landing gear passed through ground");
    plane.reset(Vec3(0, Plane::parked_height, 0), 0);
    tick(world, plane, {1, 0, 0, .8f}, 480);
    require(plane.forward().GetX() < -.1f, "nose-wheel steering did not turn while taxiing");
    Environment map;
    PhysicsWorld city(map);
    Plane collision_plane(city);
    const auto& b = map.buildings().front();
    collision_plane.reset(b.center + Vec3(0, 0, b.size.GetZ() / 2 + 20), 0, Vec3(0, 0, -35));
    tick(city, collision_plane, {}, 160);
    require(collision_plane.position().GetZ() > b.center.GetZ(), "plane passed through building");
    require(collision_plane.damaged(), "severe impact did not damage aircraft");
}
void entry_exit_and_recovery() {
    Environment map;
    PhysicsWorld world(map);
    Car car(world);
    Plane plane(world);
    Player player(world, car, map, &plane);
    for (int i = 0; i < 240; ++i) player.step({}, {});
    require(player.interact() == Interaction::Exited && player.on_foot(), "could not leave car");
    const Vec3 door = plane.position() + plane.rotate(Vec3(-1.9f, 0, -1.8f));
    player.character().reset(Vec3(door.GetX(), map.height(door.GetX(), door.GetZ()) + .08f, door.GetZ()));
    require(player.entry_vehicle() == EntryVehicle::Plane && player.interact() == Interaction::Entered && player.flying(),
        "could not enter nearby plane");
    require(player.interact() == Interaction::Exited && player.on_foot(), "could not exit parked aircraft");
    require(player.character().can_stand_at(player.position()), "plane exit overlaps collider");
    require(player.interact() == Interaction::Entered && player.flying(), "could not reenter plane after exit");
    plane.reset(Vec3(900, 80, 0), 0, Vec3(0, 0, -40), .7f);
    require(player.interact() == Interaction::TooFast && player.flying(), "allowed exit during flight");
    player.step({}, {}, fixed_step, {});
    require(player.position().GetX() > 800 && player.position().GetY() > 70, "flight beyond island was blocked");
    player.recover_plane();
    require(player.flying() && plane.velocity().Length() < .001f && !plane.damaged() && plane.throttle() == 0,
        "aircraft recovery retained velocity, damage or throttle");
    require(std::abs(plane.position().GetX() - Airport::plane_x) < .001f, "recovery did not return to runway");
    player.reset();
    require(player.driving() && !player.flying(), "full reset did not restore driving mode");
}
}
int main() {
    try {
        airport_and_takeoff(); flight_controls_and_gravity(); landing_brakes_and_collisions(); entry_exit_and_recovery();
        std::cout << "All airport and flight checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Flight check failed: " << e.what() << '\n';
        return 1;
    }
}
