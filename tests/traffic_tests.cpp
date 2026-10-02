#include "traffic.hpp"
#include "environment.hpp"
#include "player.hpp"
#include "airport.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace forza;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Vec3 ground(const Environment& map, float x, float z, float height = .56f) {
    return Vec3(x, map.height(x, z) + height, z);
}
void tick(Player& player, int count, Input input = {}) {
    for (int i = 0; i < count; ++i) player.step(input, {});
}

void roads_and_driving() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Traffic traffic(world, map);
    Player player(world, starter, map, nullptr, &traffic);
    // Keep this circulation check clear of parked-player queues (tested separately).
    starter.reset(ground(map, -180, -700));
    require(traffic.cars().size() >= 250, "Miami region has too few NPC cars");
    std::vector<Vec3> previous;
    std::vector<float> traveled(traffic.cars().size(), 0);
    for (const auto& vehicle : traffic.cars()) {
        previous.push_back(vehicle.car->position());
        require(Environment::road(previous.back().GetX(), previous.back().GetZ()), "traffic spawned off the road");
    }
    int road_samples = 0, total_samples = 0;
    for (int step = 0; step < 120 * 90; ++step) {
        player.step({}, {});
        if (step % 30 != 0) continue;
        for (std::size_t i = 0; i < traffic.cars().size(); ++i) {
            const auto& car = *traffic.cars()[i].car;
            const Vec3 p = car.position();
            require(std::isfinite(p.Length()) && car.velocity().Length() < 16, "traffic physics became unstable");
            const float distance = (p - previous[i]).Length();
            if (distance < 8) traveled[i] += distance; // Do not count recovery teleports.
            previous[i] = p;
            ++total_samples;
            if (Environment::road(p.GetX(), p.GetZ())) ++road_samples;
            require(car.rotate(Vec3::sAxisY()).GetY() > .5f, "traffic overturned on its route");
        }
    }
    std::cout << "Road samples: " << road_samples << '/' << total_samples << "; travel:";
    for (std::size_t i = 0; i < 8; ++i) std::cout << ' ' << traveled[i];
    std::cout << '\n';
    require(road_samples > total_samples * .98f, "traffic left the island roads");
    for (std::size_t i = 0; i < traveled.size(); ++i) {
        if (traveled[i] <= 80) std::cout << "Stopped car " << i << ", route " << traffic.cars()[i].route
            << ", distance " << traveled[i] << ", position " << traffic.cars()[i].car->position().GetX()
            << ", " << traffic.cars()[i].car->position().GetZ() << '\n';
        require(traveled[i] > 80, "NPC traffic stopped making progress");
    }
}

void braking_and_theft() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Plane plane(world);
    Traffic traffic(world, map);
    Player player(world, starter, map, &plane, &traffic);
    starter.reset(ground(map, -200, 90));
    tick(player, 120);
    require(player.interact() == Interaction::Exited, "could not leave starter car");

    Car& target = *traffic.cars().front().car;
    target.reset(ground(map, 4, 100));
    player.character().reset(ground(map, 4, 76, .08f));
    float closest = 100;
    for (int i = 0; i < 720; ++i) {
        player.step({}, {});
        closest = std::min(closest, (target.position() - player.position()).Length());
    }
    std::cout << "Pedestrian clearance: " << closest << ", speed " << target.velocity().Length() << '\n';
    require(closest > 3.5f && closest < 10, "NPC did not stop safely for the pedestrian");
    require(target.velocity().Length() < .5f, "traffic did not brake near player");
    player.character().reset(ground(map, target.position().GetX() - 2, target.position().GetZ(), .08f));
    require(player.entry_car() == &target && player.can_steal(), "nearest NPC car was not stealable");
    require(player.interact() == Interaction::Entered && &player.car() == &target, "theft did not transfer control");
    require(!traffic.is_npc(&target) && player.driving(), "stolen car retained NPC control");
    auto tuning = target.tuning(); tuning.tire_grip = 2; tuning.top_speed = 310 / 3.6f; tuning.acceleration = 5;
    target.set_tuning(tuning);
    target.reset(ground(map, 0, 110));
    const Vec3 parked = starter.position();
    tick(player, 300, {1, 0, false});
    require(target.position().GetZ() < 100 && target.velocity().Length() > 4, "player input did not drive the stolen car");
    require((player.position() - target.position()).Length() < .001f && player.forward().Dot(target.forward()) > .999f,
        "player/camera target did not follow stolen car");
    require((starter.position() - parked).Length() < 1, "starter car was also controlled after theft");
    require(player.interact() == Interaction::TooFast, "allowed exit from stolen car at speed");
    starter.reset(map.spawn());
    player.reset();
    tick(player, 120);
    require(&player.car() == &target && target.tuning().tire_grip == 2 && !traffic.is_npc(&target),
        "recovery lost stolen car, tuning, or ownership");
    require(std::abs(target.tuning().top_speed * 3.6f - 310) < .01f && target.tuning().acceleration == 5,
        "stolen car recovery lost engine tuning");
    require((target.position() - starter.position()).Length() > 5 && target.velocity().Length() < .3f,
        "stolen car recovered on top of the starter car");
    require(player.interact() == Interaction::Exited, "could not leave stolen car");
    const Vec3 abandoned = target.position();
    tick(player, 240);
    require((target.position() - abandoned).Length() < .8f && !traffic.is_npc(&target), "abandoned stolen car resumed AI driving");
    require(!player.can_steal() && player.entry_car() == &target && player.interact() == Interaction::Entered,
        "could not reenter abandoned stolen car");
    require(player.interact() == Interaction::Exited, "second exit failed");

    Car& second = *traffic.cars()[1].car;
    second.set_simulated(true);
    // Board on Bayfront Lane, clear of procedural building and tree lots.
    second.reset(ground(map, 180, 85));
    player.character().reset(ground(map, 178, 85, .08f));
    require(player.can_steal() && player.interact() == Interaction::Entered && &player.car() == &second,
        "could not switch to a second NPC car");
    tick(player, 240);
    require((target.position() - abandoned).Length() < 1 && second.tuning().tire_grip != 2,
        "switching cars moved the previous car or shared its tuning");
    require(std::abs(second.tuning().top_speed * 3.6f - 240) < .01f && second.tuning().acceleration == 9,
        "engine tuning leaked to a second stolen car");
    require(player.interact() == Interaction::Exited, "could not exit second stolen car");

    const Vec3 door = plane.position() + plane.rotate(Vec3(-1.9f, 0, -1.8f));
    player.character().reset(ground(map, door.GetX(), door.GetZ(), .08f));
    require(player.entry_vehicle() == EntryVehicle::Plane && player.interact() == Interaction::Entered && player.flying(),
        "plane boarding broke after stealing a car");
    tick(player, 120);
    require(player.interact() == Interaction::Exited, "plane exit broke with traffic enabled");
}

void isolate(Traffic& traffic, std::size_t keep = 0) {
    for (std::size_t i = 0; i < traffic.cars().size(); ++i) {
        if (i == keep) continue;
        traffic.steal(*traffic.cars()[i].car);
        traffic.cars()[i].car->set_simulated(false);
    }
    traffic.cars()[keep].car->set_simulated(true);
}

void traffic_obstructions() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Traffic traffic(world, map);
    isolate(traffic);
    starter.reset(ground(map, 2.2f, 155));
    Car& follower = *traffic.cars().front().car;
    follower.reset(ground(map, 2.2f, 178));
    const Vec3 parked = starter.position();
    bool queued = false, passed = false;
    float closest = 100, widest = 0;
    for (int step = 0; step < 120 * 20; ++step) {
        starter.step({0, 0, false, true});
        traffic.step(&starter, starter, nullptr, nullptr, starter.position());
        world.step();
        closest = std::min(closest, (follower.position() - starter.position()).Length());
        widest = std::max(widest, std::abs(follower.position().GetX()));
        queued |= follower.velocity().Length() < 1 && follower.position().GetZ() > 159;
        passed |= traffic.cars().front().pass_blocker != nullptr;
    }
    std::cout << "Parked car displacement: " << (starter.position() - parked).Length()
        << "; follower clearance: " << (follower.position() - starter.position()).Length()
        << ", speed " << follower.velocity().Length() << '\n';
    require((starter.position() - parked).Length() < .8f, "traffic rear-ended the parked player car");
    require(queued && passed && follower.position().GetZ() < 140 && std::abs(follower.position().GetX() - 2.2f) < 1,
        "NPC did not safely pass and return to its lane");
    require(closest > 3.5f && widest < 3.3f, "passing car left its road or clipped the parked car");
}

void traffic_horns_and_safety() {
    Environment map;
    // A suddenly appearing stopped car gets a short warning, including between cached plans.
    {
        PhysicsWorld world(map); Car starter(world); Traffic traffic(world, map);
        isolate(traffic);
        Car& follower = *traffic.cars().front().car;
        starter.reset(ground(map, -180, 155));
        follower.reset(ground(map, 2.2f, 175));
        for (int step = 0; step < 240; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, nullptr, follower.position()); world.step();
        }
        require(follower.velocity().Length() > 4, "warning check did not reach driving speed");
        starter.reset(ground(map, follower.position().GetX(), follower.position().GetZ() - 12));
        bool warned = false;
        for (int step = 0; step < 60; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, nullptr, follower.position()); world.step();
            warned |= traffic.cars().front().horn_time > 0;
        }
        require(warned && traffic.cars().front().horn_time == 0 && traffic.cars().front().horn_cooldown > 7,
            "sudden vehicle warning was absent, held, or repeated");
        traffic.steal(follower);
        require(traffic.cars().front().horn_time == 0 && traffic.cars().front().horn_cooldown == 0,
            "stolen car retained its NPC horn state");
    }
    // One warning for a pedestrian stepping into the lane; standing there never provokes queue horns.
    {
        PhysicsWorld world(map); Car starter(world); Traffic traffic(world, map);
        isolate(traffic);
        Car& follower = *traffic.cars().front().car;
        starter.reset(ground(map, -180, 155)); follower.reset(ground(map, 2.2f, 175));
        for (int step = 0; step < 240; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, nullptr, follower.position()); world.step();
        }
        const Vec3 pedestrian = ground(map, follower.position().GetX(), follower.position().GetZ() - 8, .08f);
        float closest = 100; int bursts = 0; bool horn = false;
        for (int step = 0; step < 120 * 14; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, &pedestrian, follower.position()); world.step();
            const bool active = traffic.cars().front().horn_time > 0;
            if (active && !horn) ++bursts;
            horn = active; closest = std::min(closest, (follower.position() - pedestrian).Length());
            require(!traffic.cars().front().pass_blocker, "NPC overtook a pedestrian in its lane");
        }
        require(bursts == 1 && closest > 3.5f && follower.velocity().Length() < .5f,
            "NPC pedestrian warning or continued yielding was unsafe");
    }
    // A parked car in the opposite lane prevents passing; long queues get separated bursts and no recovery teleport.
    {
        PhysicsWorld world(map); Car starter(world); Traffic traffic(world, map);
        isolate(traffic);
        Car& follower = *traffic.cars().front().car;
        Car& opposite = *traffic.cars()[1].car;
        opposite.set_simulated(true); opposite.reset(ground(map, -2.2f, 156), 3.14159265f);
        starter.reset(ground(map, 2.2f, 155)); follower.reset(ground(map, 2.2f, 178));
        int bursts = 0; bool horn = false; float first = -1, previous = -1;
        for (int step = 0; step < 120 * 26; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, nullptr, ground(map, 2.2f, 225)); world.step();
            const bool active = traffic.cars().front().horn_time > 0;
            if (active && !horn) {
                const float now = step * fixed_step;
                if (previous >= 0) require(now - previous > 7.9f, "queue horn repeated without its cooldown");
                else first = now;
                previous = now; ++bursts;
            }
            horn = active;
            require(!traffic.cars().front().pass_blocker, "NPC passed into an occupied opposite lane");
        }
        require(bursts >= 2 && bursts <= 3 && first > 6, "long queue horn timing was absent or excessive");
        require(follower.position().GetZ() > 165 && follower.position().GetZ() < 169
            && follower.velocity().Length() < .5f, "queued NPC collided or teleported around its obstruction");
    }
    // An approaching car is projected through the whole pass, rather than just checked at its current location.
    {
        PhysicsWorld world(map); Car starter(world); Traffic traffic(world, map);
        isolate(traffic);
        Car& follower = *traffic.cars().front().car;
        Car& opposite = *traffic.cars()[1].car;
        opposite.set_simulated(true); opposite.reset(ground(map, -2.2f, 137), 3.14159265f);
        starter.reset(ground(map, 2.2f, 155)); follower.reset(ground(map, 2.2f, 178));
        for (int step = 0; step < 120 * 10; ++step) {
            starter.step({0, 0, false, true}); opposite.step({.04f, 0});
            traffic.step(&opposite, starter, nullptr, nullptr, follower.position()); world.step();
            require(!traffic.cars().front().pass_blocker, "NPC started a pass into approaching traffic");
        }
        require(follower.position().GetZ() > 165 && std::abs(follower.position().GetX() - 2.2f) < .5f,
            "NPC left its lane while waiting for approaching traffic");
    }
    // Narrow residential streets and bridge decks stay single-file even with a clear opposite lane.
    for (bool bridge : {false, true}) {
        PhysicsWorld world(map); Car starter(world); Traffic traffic(world, map);
        const std::size_t index = bridge ? traffic.cars().size() - 1 : 16 * 3;
        isolate(traffic, index);
        Car& follower = *traffic.cars()[index].car;
        const float x = bridge ? -362.2f : -1042.2f, z = bridge ? 1000 : 0;
        constexpr float direction = 1, yaw = 3.14159265f;
        follower.reset(ground(map, x, z), yaw);
        starter.reset(ground(map, x, z + direction * 24), yaw);
        for (int step = 0; step < 120 * 12; ++step) {
            starter.step({0, 0, false, true});
            traffic.step(&starter, starter, nullptr, nullptr, starter.position()); world.step();
            require(!traffic.cars()[index].pass_blocker, "NPC overtook on a narrow street or bridge");
        }
        std::cout << "Queue " << (bridge ? "bridge" : "narrow") << ": " << follower.position().GetX()
            << ',' << follower.position().GetZ() << " speed=" << follower.velocity().Length()
            << " clearance=" << (follower.position() - starter.position()).Length() << '\n';
        require(std::abs(follower.position().GetX() - x) < .6f && follower.velocity().Length() < .5f
            && (follower.position() - starter.position()).Length() > 8,
            "NPC did not safely queue on a narrow street or bridge");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::string check = argc > 1 ? argv[1] : "all";
        require(check == "all" || check == "roads" || check == "theft" || check == "obstructions" || check == "safety", "unknown traffic check");
        if (check == "all" || check == "roads") roads_and_driving();
        if (check == "all" || check == "theft") braking_and_theft();
        if (check == "all" || check == "obstructions") traffic_obstructions();
        if (check == "all" || check == "safety") traffic_horns_and_safety();
        std::cout << "All traffic and theft checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Traffic check failed: " << error.what() << '\n';
        return 1;
    }
}
