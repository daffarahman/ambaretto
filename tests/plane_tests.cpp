#include "airport.hpp"
#include "environment.hpp"
#include "plane.hpp"
#include "player.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

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
    Car car(world);
    for (const auto& airport : airports) {
        for (int runway = 0; runway < airport.runway_count(); ++runway) {
            for (float along = -airport.runway_length() / 2 + 8; along <= airport.runway_length() / 2 - 8; along += 8)
                for (float across : {-airport.runway_width() / 2 + 2, 0.0f, airport.runway_width() / 2 - 2}) {
                    const auto p = airport.point(along, across, runway);
                    require(airport.pavement(p.x, p.z) && std::abs(map.height(p.x, p.z) - Airport::elevation) < .001f, "runway is not level or paved");
                    require(world.cast_ground(Vec3(p.x, Airport::elevation + 20, p.z), Vec3(0, -1, 0), 30, hit)
                        && std::abs(hit.point.GetY() - map.height(p.x, p.z)) < .001f, "airport render and collision heights differ");
                }
        }
        const Vec3 access(airport.apron_x(), 0, airport.apron_z());
        Vec3 entrance(-2190, 0, 4290);
        if (airport.international) {
            require(airport.center_x < map.islands().front().center.GetX()
                && Environment::coast_radius(airport.center_x, airport.runway_z) < .8f,
                "international airport is not on mainland grass");
            bool connected = false;
            for (const auto& road : map.roads()) if (std::string_view(road.name) == "AIRPORT INTERCHANGE")
                connected |= (road.a - Vec3(airport.gate_x(), road.a.GetY(), airport.gate_z())).Length() < .01f;
            require(connected, "terminal driveway has no public road connection");
            entrance = Vec3(airport.gate_x(), 0, airport.gate_z());
            for (const auto& path : map.highways())
                require(std::string_view(path.name) != "AIRPORT INTERNAL ACCESS", "NPC route enters airfield");
            bool terminal = false;
            for (const auto& building : map.buildings())
                terminal |= building.kind == BuildingKind::Terminal && std::abs(building.center.GetX() - airport.gate_x() - 40) < .01f;
            require(terminal, "airport terminal is missing beside the public road");
            for (const auto& barrier : map.barriers())
                require(!airport.contains(barrier.center.GetX(), barrier.center.GetZ()), "airport perimeter barrier remains");
        }
        if (airport.international) {
            const auto paths = airport.access_points();
            for (int segment = 1; segment < int(paths.size()); ++segment) for (int i = 0; i <= 100; ++i) {
                const auto a = paths[segment - 1], b = paths[segment];
                const float x = a.x + (b.x - a.x) * (i / 100.0f), z = a.z + (b.z - a.z) * (i / 100.0f);
                require(map.road(x, z) && map.height(x, z) > 2.9f, "airport access road has a gap or enters water");
            }
            for (float x = airport.center_x - airport.grounds_half_width + 20; x < airport.center_x + airport.grounds_half_width; x += 80)
                for (float z = airport.runway_z - airport.grounds_half_length() + 20; z < airport.runway_z + airport.grounds_half_length(); z += 80) {
                    require(std::abs(map.terrain_height(x, z) - Airport::elevation) < .001f, "airport grass is not solid level ground");
                    // Sample beneath any flyover: the airport's solid ground is a separate layer.
                    const bool found = world.cast_ground(Vec3(x, Airport::elevation + 5, z), Vec3(0, -1, 0), 10, hit);
                    if (!found || std::abs(hit.point.GetY() - Airport::elevation) >= .001f)
                        std::cout << "Airport extension sample " << x << ',' << z << " terrain=" << map.terrain_height(x, z)
                            << " collision=" << (found ? hit.point.GetY() : -999) << '\n';
                    require(found && std::abs(hit.point.GetY() - Airport::elevation) < .001f, "airport extension lacks matching ground collision");
                }
            require(airport.runway_count() == 1 && airport.runway_length() == 1100 && airport.runway_width() == 44,
                "restored airport lost its single long, wide runway");
            require(!airport.pavement(airport.center_x - 60, airport.runway_z + 300),
                "grass beside the runway was paved over");
        } else for (int i = 0; i <= 100; ++i) {
            const Vec3 p = access + (entrance - access) * (i / 100.0f);
            require(map.road(p.GetX(), p.GetZ()) && map.height(p.GetX(), p.GetZ()) > 2.9f, "airport access road has a gap or enters water");
        }
        for (const auto& tree : map.trees())
            require(!airport.contains(tree.base.GetX(), tree.base.GetZ()) && !airport.flight_path(tree.base.GetX(), tree.base.GetZ()), "tree obstructs airport or departure path");
        plane.reset(Vec3(airport.plane_x(), Airport::elevation + Plane::parked_height, airport.plane_z()), airport.yaw());
        const Vec3 gate_target = access;
        const Vec3 direction = (gate_target - entrance).Normalized();
        car.reset(entrance + Vec3(0, map.height(entrance.GetX(), entrance.GetZ()) + .56f, 0), std::atan2(-direction.GetX(), -direction.GetZ()));
        for (int i = 0; i < 1440 && (car.position() - entrance).Dot(direction) < (gate_target - entrance).Length(); ++i) {
            car.step({1, 0, false}); plane.step({0, 0, 0, 0, false, false, true}); world.step();
        }
        require((car.position() - entrance).Dot(direction) >= (gate_target - entrance).Length(), "car cannot drive along player-only airport access");
        car.reset(map.spawn());
        for (int runway = 0; runway < airport.runway_count(); ++runway) {
            const auto parked = airport.point(airport.plane_along(), 0, runway), forward = airport.direction(runway);
            plane.reset(Vec3(parked.x, Airport::elevation + Plane::parked_height, parked.z), airport.yaw(runway));
            tick(world, plane, {0, 0, 0, 0, false, false, true}, 480);
            require(plane.grounded() && std::abs(plane.position().GetY() - Airport::elevation - Plane::parked_height) < .15f,
                "plane did not settle on landing gear");
            require(plane.velocity().Length() < .15f && plane.throttle() == 0, "parked plane moves or engine runs");
            const Vec3 departure(forward.x, 0, forward.z), start = plane.position();
            require(plane.forward().Dot(departure) > .99f, "aircraft parking does not face along its departure runway");
            float takeoff_distance = -1;
            for (int i = 0; i < 2160; ++i) {
                FlightInput input;
                input.throttle = 1;
                input.pitch = plane.airspeed() > 28 ? .5f : 0;
                plane.step(input); world.step();
                if (!plane.grounded() && plane.position().GetY() > Airport::elevation + 3 && takeoff_distance < 0)
                    takeoff_distance = (plane.position() - start).Dot(departure);
            }
            std::cout << airport.name << " runway " << runway + 1 << " takeoff: " << takeoff_distance << " m, altitude " << plane.position().GetY()
                << " m, airspeed " << plane.airspeed() << " m/s\n";
            require(takeoff_distance > 50 && takeoff_distance < 280, "aircraft did not take off before runway end");
            require(plane.position().GetY() > 20 && !plane.damaged(), "powered climb failed or damaged aircraft");
        }
    }
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
    const auto tower = std::find_if(map.buildings().begin(), map.buildings().end(), [](const Building& b) {
        return b.kind == BuildingKind::Tower && b.size.GetY() > 60;
    });
    require(tower != map.buildings().end(), "impact check has no tall building");
    const auto& b = *tower;
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
    player.step({}, {}, fixed_step, {});
    require(player.position().GetX() > 800 && player.position().GetY() > 70, "flight beyond island was blocked");
    const Vec3 bailout_velocity = plane.velocity();
    require(player.interact() == Interaction::Exited && player.on_foot() && player.character().ragdolling(),
        "could not bail out of an airborne plane");
    require(player.position().GetY() > 70 && (player.character().velocity() - bailout_velocity).Length() < 3.3f
        && !player.can_enter(), "airborne bailout lost altitude or vehicle momentum");
    player.recover_plane();
    require(player.on_foot() && player.character().ragdolling() && plane.velocity().Length() < .001f
        && !plane.damaged() && plane.throttle() == 0,
        "aircraft recovery retained velocity, damage or throttle");
    require(std::abs(plane.position().GetX() - airports[0].plane_x()) < .001f
        && std::abs(plane.position().GetZ() - airports[0].plane_z()) < .001f, "recovery did not return to runway threshold");
    plane.reset(Vec3(airports[1].center_x, 80, airports[1].runway_z + 300), 0, Vec3(0, 0, 40));
    player.recover_plane();
    require(std::abs(plane.position().GetX() - airports[1].center_x) < .001f
        && std::abs(plane.position().GetZ() - airports[1].plane_z()) < .001f && plane.forward().GetZ() > .99f,
        "recovery near Key West did not use its southbound runway");
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
