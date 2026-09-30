#pragma once
#include "vehicle.hpp"
#include <vector>

namespace forza {
class Plane;

struct TrafficCar {
    std::unique_ptr<Car> car;
    bool npc = true;
    std::size_t route = 0;
    float stuck_time = 0;
};

// Traffic uses the same dynamic chassis and suspension as the player's car.
// It applies inputs before the single shared PhysicsWorld step.
class Traffic {
public:
    Traffic(PhysicsWorld& world, const Environment& environment);
    const std::vector<TrafficCar>& cars() const { return cars_; }
    bool is_npc(const Car* car) const;
    void steal(Car& car);
    void step(Car* controlled, const Car& starter_car, const Plane* plane,
              const Vec3* pedestrian, Vec3 player_position, float dt = fixed_step);
private:
    struct RouteLocation {
        std::size_t segment;
        Vec3 point;
        float distance;
    };
    RouteLocation locate(const std::vector<Vec3>& route, Vec3 position) const;
    Vec3 ahead(const std::vector<Vec3>& route, RouteLocation location, float distance) const;
    bool space_available(Vec3 position, const Car& ignore, const Car& starter_car,
                         const Plane* plane, Vec3 player_position) const;
    PhysicsWorld& world_;
    const Environment& environment_;
    std::vector<std::vector<Vec3>> routes_;
    std::vector<TrafficCar> cars_;
};
} // namespace forza
