#pragma once
#include "player.hpp"
#include <filesystem>
#include <string>

namespace ambaretto {
class DayNight;
struct PlaySession {
    std::string city_id;
    float minutes = 720, yaw = 0, health = 100, vehicle_health = 100, throttle = 0;
    EntryVehicle mode = EntryVehicle::None;
    PlayMode play_mode = PlayMode::FreeRoam;
    Vec3 position{0, 0, 0}, velocity{0, 0, 0};
    Quat rotation = Quat::sIdentity();
    PlaneType plane_type = PlaneType::Trainer;
    std::optional<Vec3> placement;
    std::optional<CarDesign> car_design;
    WeaponType weapon = WeaponType::Unarmed;
    std::array<int, int(WeaponType::Count)> ammo{}, reserve{};

    static PlaySession capture(const Player& player, const DayNight& time);
    bool validate(std::string& error) const;
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, PlaySession& session, std::string& error);
    void restore(Player& player, DayNight& time, std::string& notice) const;
};
} // namespace ambaretto
