#include "environment.hpp"
#include "plane.hpp"
#include "police.hpp"
#include "traffic.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace forza;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void tick(PhysicsWorld& world, Car& car, int count, Input input = {}) {
    for (int i = 0; i < count; ++i) { car.step(input); world.step(); }
}
void damage_and_gunfire() {
    PhysicsWorld world(false);
    Car target(world), behind(world);
    target.reset(Vec3(0, .56f, -6)); behind.reset(Vec3(0, .56f, -12));
    tick(world, target, 240);
    require(target.health() == 100, "settling damaged the car");
    target.take_damage(-1); target.take_damage(std::numeric_limits<float>::quiet_NaN());
    require(target.health() == 100, "invalid damage changed health");
    Weapons gun; gun.select(WeaponType::AK47);
    auto hit = trace_shot(world, nullptr, Vec3(0, 1, 0), Vec3(0, 0, -1), 40);
    require(hit.car == &target && !hit.character, "bullet did not identify nearest car");
    const auto shot = gun.fire(world, nullptr, Vec3(0, 1, 0), Vec3(0, 0, -1), true, true, true);
    require(shot.hit && target.health() < 100 && behind.health() == 100, "bullet missed car or passed through cover");
    require(target.health() > 85, "one rifle shot caused excessive vehicle damage");
    const float health = target.health();
    target.set_simulated(false); target.reset(Vec3(20, .56f, -6)); target.set_simulated(true);
    require(target.health() == health, "position/streaming reset healed car damage");
    target.repair(); require(target.health() == 100 && !target.destroyed(), "repair failed");

    target.reset(Vec3(0, 12, 20));
    tick(world, target, 600);
    require(target.health() > 60 && target.health() < 100, "moderate crash damage was absent or excessive");
    target.repair(); target.reset(Vec3(0, 200, 20));
    tick(world, target, 960);
    require(target.destroyed(), "lethal crash failed to destroy car");
    target.repair(); target.reset(Vec3(30, .56f, 20));
    tick(world, target, 360, {1, 0, false});
    require(target.health() == 100 && target.velocity().Length() > 5, "normal driving damaged repaired car");

    behind.reset(Vec3(30, .56f, -45));
    target.reset(Vec3(30, .56f, -10));
    for (int i = 0; i < 720 && target.health() == 100; ++i) {
        target.step({1, 0, false}); behind.step({0, 0, false, true}); world.step();
    }
    require(target.health() < 100 && behind.health() < 100, "car-to-car crash did not damage both cars");
}
void explosions() {
    PhysicsWorld world(false);
    Car source(world), chain(world), distant(world);
    source.reset(Vec3(0, .56f, 0)); chain.reset(Vec3(2.8f, .56f, 0)); distant.reset(Vec3(30, .56f, 0));
    Character victim(world), outside(world);
    victim.reset(Vec3(-4, .08f, 0)); outside.reset(Vec3(-20, .08f, 0));
    source.take_damage(1000); world.step();
    const auto sounds = world.take_sound_events();
    require(sounds.size() == 2 && sounds[0].effect == SoundEffect::Explosion && sounds[1].effect == SoundEffect::Explosion,
        "vehicle chain did not emit an explosion sound for each blast");
    require(source.destroyed() && chain.destroyed() && distant.health() == 100, "blast radius or chain reaction incorrect");
    require(victim.health() < 100 && victim.ragdolling() && outside.health() == 100, "blast did not damage/ragdoll nearby character");
    const float health = victim.health();
    source.take_damage(1000); world.step();
    require(world.take_sound_events().empty(), "destroyed vehicle repeated its explosion sound");
    require(victim.health() == health && source.explosion_time() > 0, "destroyed car exploded twice");
    source.repair(); source.reset(Vec3(50, .56f, 0));
    tick(world, source, 240);
    source.take_damage(100);
    const auto position = source.position();
    tick(world, source, 180, {1, 1, false});
    require(source.destroyed() && std::abs((source.position() - position).GetZ()) < .5f, "wreck still accepted driving input");
}
void blast_cover() {
    PhysicsWorld world(false);
    Car source(world), cover(world);
    source.reset(Vec3(0, .56f, 0)); cover.reset(Vec3(4, .56f, 0));
    Character shielded(world), exposed(world);
    shielded.reset(Vec3(6, .08f, 0)); exposed.reset(Vec3(-6, .08f, 0));
    source.take_damage(100); world.step();
    require(cover.health() > 0 && cover.health() < 100 && shielded.health() == 100 && exposed.health() < 100,
        "blast ignored cover or failed to damage exposed targets");
}
void kill_feedback() {
    {
        PhysicsWorld world(false);
        Car source(world), chain(world);
        source.reset(Vec3(0, .56f, 0)); chain.reset(Vec3(2.8f, .56f, 0));
        Character shooter(world, nullptr, true), victim(world);
        shooter.reset(Vec3(0, .08f, -6)); victim.reset(Vec3(5, .08f, 0));
        Weapons gun; gun.select(WeaponType::AK47);
        for (int i = 0; i < 8; ++i) {
            require(gun.fire(world, nullptr, Vec3(0, 1, -6), Vec3(0, 0, 1), true, true, true, nullptr, &shooter).fired,
                "player vehicle-destruction setup could not fire");
            gun.step(.2f);
        }
        require(source.player_destroyed() && !world.take_player_kill(), "destroying an empty vehicle triggered a person-kill flash");
        world.step();
        require(chain.player_destroyed() && !victim.alive() && world.take_player_kill() && !world.take_player_kill(),
            "player explosion chain lost its kill credit or repeated feedback");
        world.step();
        require(!world.take_player_kill(), "corpse in an old blast repeated kill feedback");
        source.repair(); source.reset(Vec3(30, .56f, 0));
        victim.revive(); victim.reset(Vec3(32, .08f, 0));
        source.take_damage(100); world.step();
        require(!source.player_destroyed() && !victim.alive() && !world.take_player_kill(), "environmental explosion triggered player kill feedback");
    }
    {
        PhysicsWorld world(false);
        Car car(world);
        car.reset(Vec3(0, .56f, 0));
        tick(world, car, 960, {1, 0, false, false, true});
        require(car.velocity().Length() > 42, "runover kill-feedback setup did not reach lethal speed");
        Character victim(world);
        victim.reset(car.position() + car.forward() * 2 - Vec3(0, .48f, 0));
        victim.hit_by(car);
        require(!victim.alive() && world.take_player_kill(), "player runover did not trigger kill feedback");
        car.step({1, 0, false});
        victim.revive(); victim.reset(car.position() + car.forward() * 2 - Vec3(0, .48f, 0));
        victim.hit_by(car);
        require(!victim.alive() && !world.take_player_kill(), "NPC runover triggered player kill feedback");
    }
}
void aircraft_damage() {
    for (auto type : {PlaneType::Trainer, PlaneType::F18, PlaneType::Boeing747}) {
        PhysicsWorld world(false);
        Plane plane(world, type);
        Car behind(world);
        plane.reset(Vec3(0, 20, 0), 0, {}, .7f);
        behind.reset(Vec3(0, 20, plane.specs().length + 10));
        const Vec3 origin(0, 20, -plane.specs().length), direction(0, 0, 1);
        const auto hit = trace_shot(world, nullptr, origin, direction, plane.specs().length * 3 + 30);
        require(hit.plane == &plane && !hit.car, "bullet did not identify nearest aircraft");
        Weapons gun; gun.select(WeaponType::AK47);
        require(gun.fire(world, nullptr, origin, direction, true, true, true).hit && plane.health() < 100
            && behind.health() == 100, "gunfire missed aircraft or passed through it");
        require(plane.health() > 85, "one rifle shot caused excessive aircraft damage");
        const float health = plane.health();
        plane.reset(Vec3(100, 20, 0));
        require(plane.health() == health, "aircraft position reset repaired damage");
        plane.take_damage(100);
        plane.step({1, 1, 1, 1}); world.step();
        const auto sounds = world.take_sound_events();
        require(sounds.size() == 2 && sounds[0].effect == SoundEffect::AK47 && sounds[1].effect == SoundEffect::Explosion,
            "aircraft gunfire or explosion sound missing");
        require(plane.destroyed() && plane.throttle() == 0 && plane.explosion_time() < .05f,
            "destroyed aircraft retained thrust or failed to explode");
        world.step();
        require(plane.explosion_time() > 0, "aircraft exploded repeatedly");
        plane.repair();
        require(plane.health() == 100 && !plane.destroyed(), "aircraft repair failed");
        plane.reset(Vec3(100, 100, 0));
        for (int i = 0; i < 1800 && !plane.destroyed(); ++i) { plane.step({}); world.step(); }
        require(plane.health() < 100, "hard aircraft landing caused no crash damage");
    }
    PhysicsWorld world(false);
    Plane source(world), distant(world);
    Car chain(world);
    source.reset(Vec3(0, 10, 0)); distant.reset(Vec3(50, 10, 0));
    chain.reset(Vec3(0, 10, -5.5f)); chain.take_damage(40);
    Character victim(world); victim.reset(Vec3(-8, 10, 0));
    source.take_damage(100); world.step();
    require(source.destroyed() && chain.destroyed() && distant.health() == 100 && victim.health() < 100,
        "aircraft blast did not damage characters or chain into cars");
    source.repair(); source.reset(Vec3(100, 10, 0));
    chain.repair(); chain.reset(Vec3(100, 10, -5.5f));
    chain.take_damage(100); world.step();
    require(source.health() < 100, "car explosion failed to damage aircraft");

    Environment map;
    PhysicsWorld city(map);
    Car car(city);
    Plane occupied(city);
    Player pilot(city, car, map, &occupied);
    pilot.respawn_on_foot(occupied.boarding_position());
    require(pilot.interact() == Interaction::Entered && pilot.flying(), "could not board aircraft for explosion check");
    occupied.take_damage(100); pilot.step({}, {});
    require(pilot.on_foot() && !pilot.character().alive() && pilot.character().ragdolling(), "aircraft explosion spared pilot");
    pilot.respawn_on_foot(occupied.boarding_position());
    require(pilot.entry_vehicle() == EntryVehicle::None, "aircraft wreck can be boarded");
    pilot.recover_plane();
    require(occupied.health() == 100 && !occupied.destroyed(), "airport recovery failed to repair aircraft");
}
void theft_and_occupants() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Traffic traffic(world, map);
    Player player(world, starter, map, nullptr, &traffic);
    auto& target = *traffic.cars().front().car;
    target.set_simulated(true);
    const Vec3 parking(0, map.height(0, 100) + .56f, 100);
    target.reset(parking);
    target.take_damage(20);
    player.respawn_on_foot(Vec3(-2, map.height(-2, 100) + .08f, 100));
    require(player.entry_car() == &target && player.interact() == Interaction::Entered, "occupied traffic theft failed");
    const auto* driver = traffic.cars().front().driver.get();
    require(driver && driver->alive() && driver->ragdolling() && driver->enabled(), "theft did not pull driver into a living ragdoll");
    require(target.health() == 80 && !traffic.is_npc(&target), "theft healed car or retained AI");
    bool hittable = false;
    const Vec3 torso = driver->body_parts()[int(BodyPart::Torso)].position;
    for (const auto offset : {Vec3(0, 0, 5), Vec3(0, 0, -5), Vec3(5, 0, 0), Vec3(-5, 0, 0)})
        hittable |= trace_shot(world, nullptr, torso + offset, -offset, 10).character == driver;
    require(hittable, "ejected driver was missing from weapon raycasts");
    require(player.interact() == Interaction::Exited, "stolen car exit failed");
    require(player.interact() == Interaction::Entered && traffic.cars().front().driver.get() == driver, "reentry created another driver");
    target.take_damage(100); player.step({}, {});
    require(player.on_foot() && !player.character().alive() && player.character().ragdolling(), "vehicle explosion left player driving/alive");
    player.character().revive(); player.character().reset(Vec3(-2, map.height(-2, 100) + .08f, 100));
    require(player.entry_car() != &target, "wreck could be entered");
    player.reset();
    require(target.health() == 100 && player.driving() && player.character().alive(), "recovery failed to restore vehicle and occupant");

    auto& second = *traffic.cars()[1].car;
    second.set_simulated(true);
    second.reset(Vec3(0, map.height(0, 200) + .56f, 200));
    second.take_damage(100); player.step({}, {});
    require(!traffic.is_npc(&second) && traffic.cars()[1].driver && !traffic.cars()[1].driver->alive(), "NPC wreck retained driver AI or a living occupant");
    auto& blocked = *traffic.cars()[2].car;
    blocked.set_simulated(true); blocked.reset(map.buildings().front().solid_center());
    require(!traffic.steal(blocked) && traffic.is_npc(&blocked) && !traffic.cars()[2].driver,
        "blocked driver exit still transferred ownership");
    blocked.set_simulated(false);

    Police police(world, map, &traffic);
    Player suspect(world, starter, map, nullptr, &traffic, nullptr, nullptr, &police);
    for (int i = 0; i < 420; ++i) suspect.step({}, {});
    const PoliceUnit* patrol = nullptr;
    for (const auto& unit : police.units()) if (unit.active) { patrol = &unit; break; }
    require(patrol, "no police unit for occupied theft check");
    patrol->car->reset(Vec3(0, map.height(0, 300) + .56f, 300));
    for (const auto& officer : patrol->officers) officer.character->set_enabled(false);
    // Return the existing patrol crew to their seats for an occupied-car theft.
    auto& occupied = const_cast<PoliceUnit&>(*patrol);
    for (auto& officer : occupied.officers) officer.seated = true;
    suspect.respawn_on_foot(Vec3(-2, map.height(-2, 300) + .08f, 300));
    require(suspect.entry_car() == patrol->car.get() && suspect.interact() == Interaction::Entered, "occupied police car was not stealable");
    require(patrol->claimed && !patrol->officers[0].seated && patrol->officers[0].character->ragdolling(), "police driver was not pulled out");
    patrol->car->take_damage(100); suspect.step({}, {});
    require(!suspect.character().alive(), "police car explosion did not kill player occupant");
}
}
int main() {
    try {
        damage_and_gunfire(); explosions(); blast_cover(); kill_feedback(); aircraft_damage(); theft_and_occupants();
        std::cout << "Crash/gunfire damage, blasts, chains, wrecks, recovery and occupied theft passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
