#include "pedestrians.hpp"
#include "environment.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace forza;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::size_t walking_count(const Pedestrians& pedestrians, const Environment& map) {
    std::size_t count = 0;
    for (const auto& person : pedestrians.people()) if (person.enabled) {
        const Vec3 p = person.character->position();
        require(map.terrain_height(p.GetX(), p.GetZ()) > 1, "pedestrian spawned in water");
        require(person.character->can_stand_at(p), "pedestrian spawned inside a building or obstacle");
        ++count;
    }
    return count;
}
} // namespace

int main() {
    try {
        const Environment map;
        PhysicsWorld world(map);
        Car car(world);
        car.reset(map.spawn());
        Pedestrians pedestrians(world, map);
        require(pedestrians.people().size() == 64, "pedestrian physics pool is not bounded");
        const auto initial_count = walking_count(pedestrians, map);
        require(initial_count >= 12, "downtown has too few walking pedestrians");
        const auto walker = [&]() -> Character& {
            for (const auto& person : pedestrians.people()) if (person.enabled) return *person.character;
            throw std::runtime_error("no visible pedestrian");
        };
        Character& character = walker();
        const Vec3 start = character.position();
        for (int i = 0; i < 120; ++i) {
            pedestrians.prepare(car, nullptr, map.spawn()); world.step();
            pedestrians.step(car, nullptr, map.spawn());
        }
        require((character.position() - start).Length() > 1, "pedestrian did not walk along the sidewalk");
        require(character.grounded() && std::abs(character.position().GetY()
            - map.terrain_height(character.position().GetX(), character.position().GetZ())) < .2f,
            "walking pedestrian left the ground");
        for (const Vec3 town : {Vec3(1320, 4, 0), Vec3(-360, 4, 1500), Vec3(-1320, 4, 2940), Vec3(-2100, 4, 4380)}) {
            pedestrians.prepare(car, nullptr, town, .6f);
            require(walking_count(pedestrians, map) >= 8, "pedestrians did not stream into a beach or Keys town");
        }
        for (int i = 0; i < 120; ++i) { car.step({1, 0, false}); world.step(); }
        Character& target = walker();
        Vec3 hit = car.position() + car.forward() * 4;
        hit.SetY(map.terrain_height(hit.GetX(), hit.GetZ()) + .08f);
        target.reset(hit);
        for (int i = 0; i < 160 && !target.ragdolling(); ++i) {
            car.step({1, 0, false});
            pedestrians.prepare(car, nullptr, hit);
            world.step();
            pedestrians.step(car, nullptr, hit);
        }
        require(target.ragdolling(), "moving car did not ragdoll the pedestrian");
        std::cout << initial_count << " downtown pedestrians; walking, town streaming and car impact passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
