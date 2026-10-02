#pragma once
#include "player.hpp"
#include <vector>

namespace forza {
class Traffic;

struct Pedestrian {
    std::unique_ptr<Character> character;
    std::size_t route = 0, target = 0;
    unsigned appearance = 0;
    float walking_speed = 1.5f, stuck_time = 0;
    bool enabled = false, reverse = false, was_ragdoll = false;
};

class Pedestrians {
public:
    Pedestrians(PhysicsWorld& world, const Environment& environment);
    // Impact detection precedes the shared world step; walking follows it.
    void prepare(const Car& starter, const Traffic* traffic, Vec3 player_position, float dt = fixed_step);
    void step(const Car& starter, const Traffic* traffic, Vec3 player_position, float dt = fixed_step);
    const std::vector<Pedestrian>& people() const { return people_; }
private:
    bool walkable(Vec3 position) const;
    void stream(const Car* starter, const Traffic* traffic, Vec3 player_position, bool initial = false);
    const Environment& environment_;
    std::vector<std::vector<Vec3>> routes_;
    std::vector<Pedestrian> people_;
    float stream_time_ = 0;
    unsigned spawn_sequence_ = 0;
};
} // namespace forza
