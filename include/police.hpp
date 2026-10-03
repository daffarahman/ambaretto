#pragma once
#include "player.hpp"
#include <vector>

namespace forza {
enum class Crime { RecklessDriving, VehicleTheft, Gunfire, Assault, Homicide, OfficerAssault, OfficerHomicide, PoliceVehicleTheft, Count };
struct CrimeData { const char* name; int points, minimum_stars; float report_range, repeat_delay; };
const CrimeData& crime_data(Crime crime);
struct PoliceResponse { int cars; float speed, pit_interval, fire_interval, sight_range, dispatch_interval; };
const PoliceResponse& police_response(int stars);

class WantedLevel {
public:
    void report(Crime crime, Vec3 position);
    void step(Vec3 player_position, bool seen, float dt);
    void clear();
    int stars() const { return stars_; }
    bool searching() const { return stars_ > 0 && !seen_; }
    Vec3 last_seen() const { return last_seen_; }
    float radius() const { return 120.f + stars_ * 45; }
    float cooldown() const { return 6.f + stars_ * 4; }
    float escape_progress() const { return stars_ ? outside_time_ / cooldown() : 0; }
private:
    int points_ = 0, stars_ = 0;
    Vec3 last_seen_{0, 0, 0};
    float outside_time_ = 0;
    bool seen_ = false;
};

struct PoliceOfficer {
    std::unique_ptr<Character> character;
    bool seated = true;
    float fire_time = 0, flash = 0, previous_health = 100;
    unsigned shots_fired = 0;
};
struct PoliceUnit {
    std::unique_ptr<Car> car;
    std::array<PoliceOfficer, 2> officers;
    bool active = false, claimed = false;
    std::vector<Vec3> route;
    std::size_t waypoint = 0;
    Vec3 destination{0, 0, 0}, steering_target{0, 0, 0};
    float plan_time = 0, pit_cooldown = 0, pit_time = 0, stuck_time = 0;
    unsigned pit_attempts = 0;
};

class Police {
public:
    Police(PhysicsWorld& world, const Environment& environment, Traffic* traffic = nullptr, Pedestrians* pedestrians = nullptr);
    void crime(Crime type, Vec3 position, Character* victim = nullptr);
    void prepare(Player& player, float dt = fixed_step);
    void finish(Player& player, float dt = fixed_step);
    void clear();
    void steal(Car& car);
    bool is_officer(const Character* character) const;
    void raycast(Vec3 origin, Vec3 direction, ShotHit& hit, const Character* ignore = nullptr) const;
    void set_view(Vec3 position, Vec3 direction) { camera_ = position; camera_forward_ = direction.NormalizedOr(Vec3(0, 0, -1)); view_set_ = true; }
    const std::array<PoliceUnit, 12>& units() const { return units_; }
    const WantedLevel& wanted() const { return wanted_; }
    bool arrested() const { return arrest_time_ >= 1.5f; }
    std::vector<Vec3> road_path(Vec3 from, Vec3 to) const;
private:
    struct RoadNode { Vec3 point; std::vector<std::pair<int, float>> edges; float width = 9; int component = -1; };
    void build_roads();
    int nearest_node(Vec3 position) const;
    bool visible(Vec3 eye, Vec3 target, float range, JPH::BodyID ignore = {}) const;
    bool camera_visible(Vec3 position) const;
    bool spawn(PoliceUnit& unit, const Player& player, std::size_t index);
    bool exit(PoliceUnit& unit, PoliceOfficer& officer, std::size_t side);
    void drive(PoliceUnit& unit, Player& player, std::size_t index, float dt);
    void walk(PoliceUnit& unit, PoliceOfficer& officer, Player& player, std::size_t index, float dt);
    PhysicsWorld& world_;
    const Environment& environment_;
    Traffic* traffic_;
    Pedestrians* pedestrians_;
    std::vector<RoadNode> roads_;
    // ponytail: six response units plus six reserve slots; recycle distant casualties outside the camera.
    std::array<PoliceUnit, 12> units_;
    WantedLevel wanted_;
    std::array<float, int(Crime::Count)> crime_cooldowns_{};
    std::array<float, 64> civilian_health_{};
    std::array<bool, 64> civilian_ragdoll_{};
    float dispatch_time_ = 0, report_time_ = 0, observation_time_ = 0, arrest_time_ = 0;
    int pending_crime_ = -1;
    Vec3 report_position_{0, 0, 0}, camera_{0, 0, 0}, camera_forward_{0, 0, -1};
    bool view_set_ = false;
    unsigned spawn_sequence_ = 0;
};
} // namespace forza
