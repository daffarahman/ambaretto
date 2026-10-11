#include "play_session.hpp"
#include "day_night.hpp"
#include "city.hpp"
#include "environment.hpp"
#include "traffic.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ambaretto {
namespace {
bool fail(std::string& error, const char* message) { error = message; return false; }
bool bounded(Vec3 value, float limit) {
    for (int i = 0; i < 3; ++i) if (!std::isfinite(value[i]) || std::abs(value[i]) > limit) return false;
    return true;
}
void write_vector(std::ostream& file, Vec3 v) { file << v.GetX() << ' ' << v.GetY() << ' ' << v.GetZ() << '\n'; }
bool read_vector(std::istream& file, Vec3& v) {
    float x, y, z;
    if (!(file >> x >> y >> z)) return false;
    v = Vec3(x, y, z); return true;
}
}

PlaySession PlaySession::capture(const Player& player, const DayNight& time) {
    PlaySession saved;
    if (const auto* city = player.environment_.city()) saved.city_id = city->id;
    saved.minutes = time.minutes_of_day(); saved.position = player.position();
    saved.yaw = player.character_.yaw(); saved.health = player.character_.health();
    saved.weapon = player.weapons_.selected_; saved.ammo = player.weapons_.ammo_; saved.reserve = player.weapons_.reserve_;
    if (!player.character_.alive()) {
        saved.position = player.environment_.spawn() - Vec3(0, .48f, 0); saved.health = 100;
    } else if (player.driving() && !player.car().destroyed()) {
        const auto& car = player.car();
        saved.mode = EntryVehicle::Car; saved.rotation = car.rotation(); saved.velocity = car.velocity();
        saved.vehicle_health = car.health(); saved.car_design = car.design();
        if (player.traffic_) for (const auto& placed : player.traffic_->cars())
            if (placed.car.get() == &car) saved.placement = placed.placement;
    } else if (player.flying() && !player.plane().destroyed()) {
        const auto& plane = player.plane();
        saved.mode = EntryVehicle::Plane; saved.rotation = plane.rotation(); saved.velocity = plane.velocity();
        saved.vehicle_health = plane.health(); saved.plane_type = plane.type(); saved.throttle = plane.throttle();
        if (player.aircraft_ && player.environment_.city()) {
            std::size_t index = 0;
            for (const auto& placed : player.environment_.city()->vehicles) if (placed.kind != CityVehicleKind::Car) {
                if (index < player.aircraft_->size() && (*player.aircraft_)[index].get() == &plane) saved.placement = placed.position;
                ++index;
            }
        }
    }
    return saved;
}

bool PlaySession::validate(std::string& error) const {
    error.clear();
    if (city_id.size() != 16 || city_id.find_first_not_of("0123456789abcdef") != std::string::npos)
        return fail(error, "Invalid progress map ID.");
    if (!std::isfinite(minutes) || minutes < 0 || minutes >= 1440 || !std::isfinite(yaw)
        || !std::isfinite(health) || health <= 0 || health > 100 || !std::isfinite(vehicle_health)
        || vehicle_health <= 0 || vehicle_health > 100 || !std::isfinite(throttle) || throttle < 0 || throttle > 1
        || !bounded(position, 24000) || !bounded(velocity, 1000) || (placement && !bounded(*placement, 24000)))
        return fail(error, "Invalid progress position, time or health.");
    if (mode != EntryVehicle::None && mode != EntryVehicle::Car && mode != EntryVehicle::Plane)
        return fail(error, "Invalid progress vehicle.");
    if (plane_type < PlaneType::Trainer || plane_type > PlaneType::Boeing747 || !rotation.IsNormalized())
        return fail(error, "Invalid progress vehicle orientation.");
    if (car_design && !car_design->validate(error)) return false;
    if (weapon < WeaponType::Unarmed || weapon >= WeaponType::Count) return fail(error, "Invalid progress weapon.");
    for (int i = 0; i < int(WeaponType::Count); ++i) {
        const auto& data = weapon_data(WeaponType(i));
        if (ammo[i] < 0 || ammo[i] > data.magazine || reserve[i] < 0 || reserve[i] > data.reserve)
            return fail(error, "Invalid progress ammunition.");
    }
    return true;
}

bool PlaySession::save(const std::filesystem::path& directory, std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory, ec);
    if (ec) return fail(error, "Cannot create progress folder; previous save retained.");
    const auto path = directory / (city_id + ".play"); auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary);
    if (!file) return fail(error, "Cannot write progress; previous save retained.");
    file << "AMBARETTO_PLAY 1\n" << city_id << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10)
        << minutes << ' ' << int(mode) << ' ' << yaw << ' ' << health << ' ' << vehicle_health << ' ' << throttle << ' ' << int(plane_type) << '\n';
    write_vector(file, position); write_vector(file, velocity);
    file << rotation.GetX() << ' ' << rotation.GetY() << ' ' << rotation.GetZ() << ' ' << rotation.GetW() << '\n'
        << int(placement.has_value()) << '\n';
    if (placement) write_vector(file, *placement);
    file << int(car_design.has_value()) << '\n';
    if (car_design) car_design->write(file);
    file << int(weapon) << '\n';
    for (int i = 0; i < int(WeaponType::Count); ++i) file << ammo[i] << ' ' << reserve[i] << '\n';
    file.close();
    if (!file) return fail(error, "Cannot finish saving progress; previous save retained.");
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, ec); const bool replaced = !ec;
#endif
    if (!replaced) return fail(error, "Cannot replace progress; previous save retained.");
    error.clear(); return true;
}

bool PlaySession::load(const std::filesystem::path& path, PlaySession& session, std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > 8192 || ec) return fail(error, "Progress save is missing or too large.");
    std::ifstream file(path); PlaySession next; std::string magic, extra;
    int version, mode, type, has_placement, has_car, weapon;
    float x, y, z, w;
    if (!(file >> magic >> version >> next.city_id >> next.minutes >> mode >> next.yaw >> next.health >> next.vehicle_health >> next.throttle >> type)
        || magic != "AMBARETTO_PLAY" || version != 1 || !read_vector(file, next.position) || !read_vector(file, next.velocity)
        || !(file >> x >> y >> z >> w >> has_placement) || (has_placement != 0 && has_placement != 1))
        return fail(error, "Invalid progress save.");
    next.mode = EntryVehicle(mode); next.plane_type = PlaneType(type); next.rotation = Quat(x, y, z, w);
    if (has_placement) { next.placement.emplace(); if (!read_vector(file, *next.placement)) return fail(error, "Invalid progress placement."); }
    if (!(file >> has_car) || (has_car != 0 && has_car != 1)) return fail(error, "Invalid progress car.");
    if (has_car) { next.car_design.emplace(); if (!CarDesign::read(file, *next.car_design, error)) return false; }
    if (!(file >> weapon)) return fail(error, "Invalid progress weapon.");
    next.weapon = WeaponType(weapon);
    for (int i = 0; i < int(WeaponType::Count); ++i)
        if (!(file >> next.ammo[i] >> next.reserve[i])) return fail(error, "Invalid progress ammunition.");
    if (!next.validate(error)) return false;
    if (file >> extra) return fail(error, "Unexpected progress data.");
    session = std::move(next); error.clear(); return true;
}

void PlaySession::restore(Player& player, DayNight& time, std::string& notice) const {
    std::string error;
    if (!validate(error)) { notice = "Cannot continue: " + error; return; }
    if (!player.environment_.city() || player.environment_.city()->id != city_id) { notice = "Cannot continue progress from another map."; return; }
    time = DayNight(); time.advance(minutes - 480 + 1440);
    Vec3 feet = position;
    const float surface = player.environment_.surface_height(feet);
    feet.SetY(std::max(feet.GetY(), surface + .02f));
    player.respawn_on_foot(feet, yaw);
    if (!player.character_.can_stand_at(feet)) player.respawn_near(feet);
    if (player.environment_.submerged(position))
        player.character_.start_swimming(Vec3(feet.GetX(), Environment::water_level, feet.GetZ()), yaw);
    bool entered = false;
    if (mode == EntryVehicle::Car) {
        Car* car = placement ? nullptr : &player.starter_car_;
        if (placement && player.traffic_) for (const auto& placed : player.traffic_->cars())
            if (placed.placement && (*placed.placement - *placement).Length() < .01f) { car = placed.car.get(); break; }
        if (car) {
            if (!placement && car_design) car->set_design(*car_design);
            else if (car_design && car->design() && car_design->body == car->design()->body && car_design->wheel == car->design()->wheel)
                car->set_tuning(car_design->tuning);
            car->set_simulated(true); car->repair(); car->damage_.health = vehicle_health;
            Vec3 center = position; center.SetY(std::max(center.GetY(), surface + car->ride_height()));
            car->restore_motion(center, rotation, velocity);
            if (player.traffic_) player.traffic_->steal(*car);
            player.car_ = car; player.driving_ = true; entered = true;
        }
    } else if (mode == EntryVehicle::Plane && player.aircraft_ && placement) {
        std::size_t index = 0;
        for (const auto& placed : player.environment_.city()->vehicles) if (placed.kind != CityVehicleKind::Car) {
            if (index < player.aircraft_->size() && (placed.position - *placement).Length() < .01f) {
                auto* plane = (*player.aircraft_)[index].get();
                if (plane->type() == plane_type) {
                    plane->repair(); plane->damage_.health = vehicle_health;
                    Vec3 center = position; center.SetY(std::max(center.GetY(), surface + plane->parking_height()));
                    plane->restore_motion(center, rotation, velocity, throttle);
                    player.plane_ = plane; player.flying_ = true; entered = true;
                }
                break;
            }
            ++index;
        }
    }
    if (entered) player.character_.set_enabled(false);
    player.character_.health_ = health;
    player.weapons_.selected_ = weapon; player.weapons_.ammo_ = ammo; player.weapons_.reserve_ = reserve;
    notice = mode != EntryVehicle::None && !entered ? "Saved vehicle was removed; continued on foot." : "Continued saved progress.";
    // shortcut: traffic and pedestrians restart with the map; snapshot them if full world persistence is needed.
}
} // namespace ambaretto
