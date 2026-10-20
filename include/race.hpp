#pragma once
#include "city.hpp"
#include <array>

namespace ambaretto {
class Traffic;
struct RaceLeg {
    std::vector<Vec3> points;
    std::vector<float> distances, widths;
    float length() const { return distances.empty() ? 0 : distances.back(); }
    Vec3 point(float distance) const;
    Vec3 direction(float distance) const;
    float locate(Vec3 position, float hint) const;
    float width(float distance) const;
};
bool snap_race_point(const City& city, Vec3 click, Vec3& point);
bool build_race_route(const City& city, const RaceMap& race, std::vector<RaceLeg>& legs, std::string& error);

struct RaceProgress {
    int lap = 0, next = 1, place = 0;
    float distance = 0, time = 0;
    Vec3 previous = Vec3::sZero();
    bool finished = false;
};
class Race {
public:
    static constexpr float checkpoint_radius = 4, checkpoint_height = 40;
    static std::unique_ptr<Race> start(PhysicsWorld& world, const Environment& environment, Traffic& traffic,
        Car& player, const RaceMap& map, const std::vector<CarDesign>& designs, std::string& error);
    ~Race();
    void step(float dt = fixed_step);
    void finish(float dt = fixed_step);
    void recover_player();
    const RaceMap& map() const { return map_; }
    const RaceProgress& progress(int racer = 0) const { return progress_[racer]; }
    const std::array<Car*,5>& cars() const { return cars_; }
    float countdown() const { return countdown_; }
    float elapsed() const { return elapsed_; }
    int position() const;
    Vec3 checkpoint() const { return map_.checkpoints[progress_[0].next]; }
    bool done() const { return progress_[0].finished; }
    bool failed() const { return failed_; }
private:
    Race(PhysicsWorld& world, const Environment& environment, Traffic& traffic, Car& player, RaceMap map);
    PhysicsWorld& world_;
    const Environment& environment_;
    Traffic& traffic_;
    RaceMap map_;
    std::vector<RaceLeg> legs_;
    std::array<Car*,5> cars_{};
    std::array<RaceProgress,5> progress_{};
    std::array<float,5> stuck_{};
    std::array<float,5> lane_offsets_{};
    std::array<bool,5> turning_back_{}, reversing_{};
    std::array<const Car*,5> pass_blockers_{};
    std::array<float,5> pass_offsets_{};
    float countdown_ = 3, elapsed_ = 0, plan_time_ = 0;
    int finishers_ = 0;
    bool failed_ = false;
    std::array<Input,5> inputs_{};
};
} // namespace ambaretto
