#include "player.hpp"
#include "environment.hpp"
#include "third_person_camera.hpp"
#include <algorithm>
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

void articulated_ragdoll() {
    using namespace forza;
    PhysicsWorld world(false);
    Character character(world), observer(world);
    const Vec3 feet(0, .08f, 0), inherited(6, 2, -4);
    character.reset(feet);
    tick(world, character, {}, 120);
    character.ragdoll(inherited, Vec3(0, 35, -90));
    require(character.ragdolling() && (character.velocity() - inherited).Length() < .01f,
        "ragdoll did not inherit velocity");
    const auto initial = character.body_parts();
    require(initial.size() == 15 && !observer.can_stand_at(feet), "ragdoll parts have no physical collision");
    require(character.can_stand_at(feet), "character recovery collides with its own ragdoll");
    require(world.camera_fraction(initial[static_cast<std::size_t>(BodyPart::Torso)].position, Vec3(0, 0, 5)) > .99f,
        "ragdoll bodies obstruct their own camera");
    const int parents[] = {-1, 0, 1, 1, 3, 4, 1, 6, 7, 0, 9, 10, 0, 12, 13};
    bool articulated = false;
    for (int step = 0; step < 360 && character.ragdolling(); ++step) {
        tick(world, character, {}, 1);
        const auto parts = character.body_parts();
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto& part = parts[i];
            require(std::isfinite(part.position.LengthSq()) && std::isfinite(part.rotation.LengthSq())
                && std::abs(part.rotation.LengthSq() - 1) < .01f && part.position.GetY() > -.3f,
                "ragdoll body is invalid or fell through the ground");
            if (parents[i] < 0) continue;
            const auto& parent = parts[parents[i]];
            require((part.position - parent.position).Length() < (part.size.Length() + parent.size.Length()) * .5f + .2f,
                "ragdoll limb detached from its parent");
            const Quat relative = parent.rotation.Conjugated() * part.rotation;
            const Quat neutral = initial[parents[i]].rotation.Conjugated() * initial[i].rotation;
            articulated |= std::abs(relative.Dot(neutral)) < .985f;
        }
    }
    require(articulated, "ragdoll moved as one rigid body instead of separate limbs");
    for (int step = 0; step < 1440 && character.ragdolling(); ++step) tick(world, character, {}, 1);
    tick(world, character, {}, 120);
    require(!character.ragdolling() && character.grounded(), "settled ragdoll did not recover to standing");
    character.reset(feet);
    character.ragdoll(Vec3::sZero());
    require(!observer.can_stand_at(feet), "second ragdoll did not create colliders");
    character.set_enabled(false);
    require(!character.ragdolling() && observer.can_stand_at(feet), "seated character retained ragdoll colliders");
    character.set_enabled(true);
    character.ragdoll(Vec3::sZero());
    character.reset(feet);
    require(!character.ragdolling() && character.velocity().Length() < .001f && observer.can_stand_at(feet),
        "reset retained ragdoll bodies or momentum");
    std::cout << "Ragdoll: articulated limbs, collision, camera, recovery and cleanup passed\n";
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
    const auto bailout_velocity = car.velocity(), bailout_position = car.position();
    require(bailout_velocity.Length() > 2.5f && player.interact() == forza::Interaction::Exited
        && player.on_foot() && player.character().ragdolling(), "moving car exit did not trigger a ragdoll");
    require((player.character().velocity() - bailout_velocity).Length() < 3.3f && !player.can_enter(),
        "bailout lost vehicle momentum or allowed immediate reentry");
    for (int i = 0; i < 30; ++i) player.step({}, {});
    require(car.velocity().Length() > bailout_velocity.Length() * .8f && (car.position() - bailout_position).Length() > .5f,
        "abandoned moving car stopped immediately after bailout");
    player.reset();

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

void ragdoll_impact_damage() {
    using namespace forza;
    PhysicsWorld flat(false);
    Character character(flat);
    character.reset(Vec3(0, 30, 0));
    character.ragdoll(Vec3(18, 0, 0));
    tick(flat, character, {}, 60);
    require(character.health() == 100, "airborne bailout motion caused damage without a collision");
    character.reset(Vec3(0, .08f, 0));
    character.ragdoll(Vec3::sZero());
    tick(flat, character, {}, 240);
    require(character.health() == 100, "resting ragdoll contacts caused injury");
    character.reset(Vec3(0, .65f, 0));
    character.ragdoll(Vec3(18, 0, 0));
    tick(flat, character, {}, 120);
    require(character.health() < 100, "fast sideways road landing caused no injury");
    character.reset(Vec3(50, .08f, 0));
    character.revive();
    Car car(flat);
    car.reset(Vec3(0, .56f, 0));
    for (int i = 0; i < 240; ++i) { car.step({0, 0, false, true}); flat.step(); }
    character.reset(Vec3(0, .65f, 6));
    character.ragdoll(Vec3(0, 0, -18));
    for (int i = 0; i < 30; ++i) { car.step({0, 0, false, true}); tick(flat, character, {}, 1); }
    require(character.health() < 100, "ragdoll collision with parked car caused no injury");
    character.reset(Vec3(50, .08f, 0));
    character.revive();
    car.reset(Vec3(0, .56f, 20));
    for (int i = 0; i < 360; ++i) { car.step({1}); flat.step(); }
    Vec3 target = car.position() + car.forward() * 6;
    target.SetY(.08f);
    character.reset(target);
    character.ragdoll(Vec3::sZero());
    for (int i = 0; i < 120; ++i) { car.step({1}); tick(flat, character, {}, 1); }
    require(character.health() < 100, "moving car hit ignored an already ragdolling character");
    character.reset(Vec3(50, .08f, 0));
    const Environment map;
    Player player(flat, car, map);
    car.reset(Vec3(0, .56f, 0));
    for (int i = 0; i < 600; ++i) player.step({1}, {});
    require(car.velocity().Length() > 12 && player.interact() == Interaction::Exited, "fast bailout setup failed");
    require(player.character().health() == 100, "bailout injured player before contact");
    for (int i = 0; i < 240; ++i) player.step({}, {});
    require(player.character().health() < 100, "fast car bailout caused no landing injury");
    PhysicsWorld city(map);
    Character victim(city);
    const auto& building = map.buildings().front();
    const float z = building.solid_center().GetZ() + building.solid_size().GetZ() / 2 + 3;
    const float x = building.solid_center().GetX();
    victim.reset(Vec3(x, map.height(x, z) + .9f, z));
    victim.ragdoll(Vec3(0, 0, -18));
    tick(city, victim, {}, 30);
    require(victim.health() < 100, "horizontal ragdoll impact against building caused no injury");
    std::cout << "Ragdoll damage: road, parked/moving cars, building, bailout and harmless motion passed\n";
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
    camera.look(0, -10000, -1000, false, forza::Vec3(0, 0, -1), 0, forza::fixed_step);
    require(camera.desired_position(forza::Vec3(0, .3f, 0), false).GetY() >= .3f,
        "low swimming orbit put camera underwater");
    require(forza::ThirdPersonCamera::above_water(forza::Vec3(0, -4, 0)).GetY() >= .3f,
        "smoothed/collision-adjusted camera can sink below water");
    for (bool flying : {false, true}) {
        constexpr float frame = 1.0f / 60;
        camera.reset();
        camera.look(400, 0, 0, !flying, forza::Vec3(0, 0, -1), 12, frame, flying);
        const float manual_yaw = camera.yaw();
        const auto idle_frame = [&]() {
            camera.recoil(0);
            camera.look(0, 0, 0, !flying, forza::Vec3(0, 0, -1), 12, frame, flying);
        };
        for (int i = 0; i < 90; ++i) idle_frame();
        require(std::abs(camera.yaw() - manual_yaw) < .001f, "vehicle camera recentered before the look delay");
        for (int i = 0; i < 150; ++i) idle_frame();
        require(std::abs(camera.yaw()) < .03f, "recoil update prevented vehicle camera recentering");
        camera.look(200, 0, 0, !flying, forza::Vec3(0, 0, -1), 12, frame, flying);
        const float renewed_yaw = camera.yaw();
        for (int i = 0; i < 90; ++i) idle_frame();
        require(std::abs(camera.yaw() - renewed_yaw) < .001f, "manual look did not restart vehicle recenter delay");
    }
}

void surface_swimming() {
    using namespace forza;
    const Environment map;
    PhysicsWorld world(map);
    Car car(world);
    Plane plane(world);
    Player player(world, car, map, &plane);
    const Vec3 ocean(2500, 0, 1800);
    require(map.terrain_height(ocean.GetX(), ocean.GetZ()) < -2, "swimming check is on land");
    car.reset(ocean + Vec3(0, 2, 0));
    for (int i = 0; i < 360 && player.driving(); ++i) player.step({}, {});
    auto& character = player.character();
    require(player.on_foot() && character.swimming() && !character.ragdolling(),
        "car entering water did not automatically eject player into swimming");
    require(character.can_stand_at(character.position()) && !player.can_enter(),
        "water exit overlaps wreck or allows boarding a submerged car");
    for (int i = 0; i < 240; ++i) player.step({}, {});
    require(character.swimming() && std::abs(character.position().GetY() + 1.25f) < .08f,
        "idle swimmer did not stay at the surface");
    const auto idle = character.body_parts();
    const float start = character.position().GetZ();
    for (int i = 0; i < 240; ++i) player.step({}, {Vec3(0, 0, -1), false, true});
    const float normal = start - character.position().GetZ();
    const float fast_start = character.position().GetZ();
    for (int i = 0; i < 240; ++i) player.step({}, {Vec3(0, 0, -1), true, false});
    require(normal > 2 && fast_start - character.position().GetZ() > normal * 1.5f,
        "swimming movement or fast swimming is broken");
    require(std::abs(character.position().GetY() + 1.25f) < .08f && !character.grounded(),
        "jump input or moving swimmer left the water surface");
    const auto stroke = character.body_parts();
    require(std::abs(stroke[3].rotation.Dot(idle[3].rotation)) < .95f
        && stroke[2].position.GetY() > .15f, "swimming stroke missing or head is underwater");
    const float released = character.position().GetZ();
    for (int i = 0; i < 240; ++i) player.step({}, {});
    require(character.velocity().Length() < .05f && released - character.position().GetZ() < 1,
        "swimmer did not stop and tread water");
    character.reset(ocean + Vec3(30, 25, 0));
    bool tumbled = false;
    for (int i = 0; i < 1200 && !character.swimming(); ++i) {
        player.step({}, {}); tumbled |= character.ragdolling();
    }
    require(tumbled && character.swimming() && !character.ragdolling(),
        "falling ragdoll did not automatically recover into surface swimming");
    character.reset(ocean + Vec3(30, 3, 0));
    for (int i = 0; i < 360 && !character.swimming(); ++i) player.step({}, {Vec3::sZero(), false, true});
    require(character.swimming(), "short jump/fall into water did not switch to swimming");
    const auto& bridge = map.bridges()[1];
    const Vec3 mid = bridge.point(.5f);
    character.reset(mid + Vec3(0, .08f, 0));
    for (int i = 0; i < 120; ++i) player.step({}, {});
    require(!character.swimming() && character.grounded(), "bridge over water triggered swimming");
    character.reset(Vec3(mid.GetX(), -.95f, mid.GetZ()));
    player.step({}, {});
    require(character.swimming(), "water underneath bridge did not allow swimming");
    float shore_x = 0;
    for (float x = 1900; x > 1450; x -= .5f) {
        const float ground = map.terrain_height(x, 0);
        if (ground < -1.8f && ground > -2.0f) { shore_x = x; break; }
    }
    require(shore_x > 0, "no beach available for shore check");
    character.start_swimming(Vec3(shore_x, 0, 0));
    for (int i = 0; i < 2400 && (character.swimming() || character.position().GetY() < 1); ++i)
        player.step({}, {Vec3(-1, 0, 0), false, false});
    require(!character.swimming() && character.position().GetY() > .5f && character.grounded(),
        "swimmer could not return to walking on beach");
    player.reset();
    require(!character.swimming() && player.driving(), "reset retained swimming state");
    for (int i = 0; i < 120; ++i) player.step({}, {});
    require(player.interact() == Interaction::Exited, "could not exit car before plane water check");
    const Vec3 door = plane.position() + plane.rotate(Vec3(-1.9f, 0, -1.8f));
    character.reset(Vec3(door.GetX(), map.height(door.GetX(), door.GetZ()) + .08f, door.GetZ()));
    require(player.interact() == Interaction::Entered && player.flying(), "could not board plane for water check");
    plane.reset(ocean + Vec3(0, 2, 0), 0, Vec3(0, -4, -8));
    for (int i = 0; i < 360 && player.flying(); ++i) player.step({}, {});
    require(player.on_foot() && character.swimming() && character.can_stand_at(character.position()),
        "plane entering water did not eject player clear of aircraft into swimming");
    std::cout << "Swimming: car/plane exits, falling, strokes, controls, surface buoyancy, bridge and shore passed\n";
}
void nearby_respawns() {
    using namespace forza;
    Environment map;
    PhysicsWorld world(map);
    Car car(world);
    Player player(world, car, map);
    const auto check = [&](Vec3 death, float maximum_distance) {
        player.respawn_on_foot(death);
        player.character().take_damage(100);
        player.respawn_near(death);
        const auto feet = player.position();
        require(player.on_foot() && player.character().alive() && !player.character().ragdolling(), "respawn did not revive on foot");
        require(std::hypot(feet.GetX() - death.GetX(), feet.GetZ() - death.GetZ()) <= maximum_distance, "respawn was too far from death");
        require(feet.GetY() > Environment::water_level && player.character().can_stand_at(feet), "respawn overlaps obstacle or is underwater");
        GroundHit hit;
        require(world.cast_ground(feet + Vec3(0, .5f, 0), Vec3(0, -1, 0), 1, hit) && hit.normal.GetY() > .65f,
            "respawn has no safe ground");
        for (int i = 0; i < 120; ++i) player.step({}, {});
        require(player.character().alive() && !player.character().swimming(), "respawn immediately killed player or entered water");
    };
    check(Vec3(0, map.height(0, 300) + .08f, 300), 40);
    check(Vec3(-2190, 120, 4290), 40); // An airborne death above the Keys airport.
    const auto& building = map.buildings().front();
    check(building.center, 220);
    // Deep ocean: the nearest bridge or road replaces the distant fixed spawn.
    const Vec3 sea(2500, -2, 1800);
    float nearest_road = 1e9f;
    for (const auto& road : map.roads()) {
        Vec3 direction = road.b - road.a; direction.SetY(0);
        const float t = std::clamp((sea - road.a).Dot(direction) / std::max(.001f, direction.LengthSq()), 0.f, 1.f);
        const Vec3 point = road.a + direction * t;
        nearest_road = std::min(nearest_road, std::hypot(point.GetX() - sea.GetX(), point.GetZ() - sea.GetZ()));
    }
    check(sea, nearest_road + 40);
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
        character_movement(); articulated_ragdoll(); city_collisions_and_interaction(); ragdoll_impact_damage(); car_coasting_and_direction_changes(); mouse_camera(); surface_swimming(); nearby_respawns(); model_tree_collisions();
        std::cout << "All player checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player check failed: " << error.what() << '\n';
        return 1;
    }
}
