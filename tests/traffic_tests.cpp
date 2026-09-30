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
    require(traffic.cars().size() >= 12, "island has too few NPC cars");
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
    for (float distance : traveled) std::cout << ' ' << distance;
    std::cout << '\n';
    require(road_samples > total_samples * .98f, "traffic left the island roads");
    for (float distance : traveled) require(distance > 80, "NPC traffic stopped making progress");
}

void braking_and_theft() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Plane plane(world);
    Traffic traffic(world, map);
    Player player(world, starter, map, &plane, &traffic);
    starter.reset(ground(map, -60, 90));
    tick(player, 120);
    require(player.interact() == Interaction::Exited, "could not leave starter car");

    Car& target = *traffic.cars().front().car;
    target.reset(ground(map, 2.5f, 100));
    player.character().reset(ground(map, 2.5f, 76, .08f));
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
    auto tuning = target.tuning(); tuning.tire_grip = 2;
    target.set_tuning(tuning);
    target.reset(ground(map, 0, 110));
    const Vec3 parked = starter.position();
    tick(player, 240, {1, 0, false});
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
    second.reset(ground(map, 60, 85));
    player.character().reset(ground(map, 58, 85, .08f));
    require(player.can_steal() && player.interact() == Interaction::Entered && &player.car() == &second,
        "could not switch to a second NPC car");
    tick(player, 240);
    require((target.position() - abandoned).Length() < 1 && second.tuning().tire_grip != 2,
        "switching cars moved the previous car or shared its tuning");
    require(player.interact() == Interaction::Exited, "could not exit second stolen car");

    const Vec3 door = plane.position() + plane.rotate(Vec3(-1.9f, 0, -1.8f));
    player.character().reset(ground(map, door.GetX(), door.GetZ(), .08f));
    require(player.entry_vehicle() == EntryVehicle::Plane && player.interact() == Interaction::Entered && player.flying(),
        "plane boarding broke after stealing a car");
    tick(player, 120);
    require(player.interact() == Interaction::Exited, "plane exit broke with traffic enabled");
}

void traffic_obstructions() {
    Environment map;
    PhysicsWorld world(map);
    Car starter(world);
    Traffic traffic(world, map);
    Player player(world, starter, map, nullptr, &traffic);
    starter.reset(ground(map, 2.5f, 74));
    Car& follower = *traffic.cars().front().car;
    follower.reset(ground(map, 2.5f, 100));
    const Vec3 parked = starter.position();
    tick(player, 1200, {0, 0, false, true});
    std::cout << "Parked car displacement: " << (starter.position() - parked).Length()
        << "; follower clearance: " << (follower.position() - starter.position()).Length()
        << ", speed " << follower.velocity().Length() << '\n';
    require((starter.position() - parked).Length() < .8f, "traffic rear-ended the parked player car");
    require(follower.position().GetZ() > starter.position().GetZ() + 4 && follower.velocity().Length() < 1,
        "NPC did not queue behind a parked car");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::string check = argc > 1 ? argv[1] : "all";
        require(check == "all" || check == "roads" || check == "theft" || check == "obstructions", "unknown traffic check");
        if (check == "all" || check == "roads") roads_and_driving();
        if (check == "all" || check == "theft") braking_and_theft();
        if (check == "all" || check == "obstructions") traffic_obstructions();
        std::cout << "All traffic and theft checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Traffic check failed: " << error.what() << '\n';
        return 1;
    }
}
