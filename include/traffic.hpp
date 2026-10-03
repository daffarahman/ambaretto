#pragma once
#include "vehicle.hpp"
#include <vector>

namespace forza {
class Plane;
class Police;
struct Road;

struct TrafficCar {
    std::unique_ptr<Car> car;
    bool npc = true;
    std::size_t route = 0;
    const char* route_name = "";
    float cruise_speed = 8;
    float stuck_time = 0;
    std::size_t segment = 0;
    Vec3 point = Vec3::sZero();
    Input input{};
    float plan_time = 0;
    float horn_time = 0, horn_cooldown = 0, blocked_time = 0, pass_retry = 0;
    bool blocked = false;
    const Car* pass_blocker = nullptr;
    const Road* pass_road = nullptr;
    Vec3 pass_end = Vec3::sZero(), pass_direction = Vec3::sZero();
};

// Nearby traffic uses the player's dynamic chassis and suspension. Distant
// cars advance on routes with their bodies removed from the physics world.
// Claimed cars stay physical. Inputs precede the shared PhysicsWorld step.
class Traffic {
public:
    Traffic(PhysicsWorld& world, const Environment& environment);
    const std::vector<TrafficCar>& cars() const { return cars_; }
    bool is_npc(const Car* car) const;
    void steal(Car& car);
    void step(Car* controlled, const Car& starter_car, const Plane* plane,
              const Vec3* pedestrian, Vec3 player_position, float dt = fixed_step, const Police* police = nullptr);
private:
    struct RouteLocation {
        std::size_t segment;
        Vec3 point;
        float distance;
    };
    RouteLocation locate(const std::vector<Vec3>& route, Vec3 position) const;
    Vec3 ahead(const std::vector<Vec3>& route, RouteLocation location, float distance) const;
    RouteLocation advance(const std::vector<Vec3>& route, RouteLocation location, float distance) const;
    void stream(Vec3 player_position, const Car* starter, const Plane* plane);
    bool space_available(Vec3 position, const Car& ignore, const Car& starter_car,
                         const Plane* plane, Vec3 player_position) const;
    PhysicsWorld& world_;
    const Environment& environment_;
    std::vector<std::vector<Vec3>> routes_;
    std::vector<TrafficCar> cars_;
    float stream_time_ = 0;
    const Police* police_ = nullptr;
};
} // namespace forza
