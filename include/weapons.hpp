#pragma once
#include "vehicle.hpp"
#include <random>

namespace forza {
class Character;
class Pedestrians;
enum class WeaponType { Unarmed, Pistol, SMG, AK47, Count };
struct WeaponData {
    const char* name;
    float damage, interval, range, recoil, spread, reload_time, impulse;
    int magazine, reserve;
    bool automatic;
    float length;
    Vec3 support_grip;
    bool two_handed;
};
const WeaponData& weapon_data(WeaponType type);
// Four clockwise slots, starting at the top. The centre keeps the current selection.
WeaponType wheel_selection(float x, float y, WeaponType current);
struct ShotHit {
    Character* character = nullptr;
    int part = 0;
    Vec3 point{0, 0, 0};
    float distance = 0;
};
ShotHit trace_shot(const PhysicsWorld& world, const Pedestrians* pedestrians,
    Vec3 origin, Vec3 direction, float range);
struct Shot {
    bool fired = false, hit = false;
    Vec3 from{0, 0, 0}, to{0, 0, 0};
    float recoil = 0;
};
class Weapons {
public:
    Weapons() { reset(); }
    void reset();
    void select(WeaponType type);
    WeaponType selected() const { return selected_; }
    const WeaponData& data() const { return weapon_data(selected_); }
    int ammo() const { return ammo_[int(selected_)]; }
    int reserve() const { return reserve_[int(selected_)]; }
    bool reloading() const { return reload_time_ > 0; }
    void reload();
    void step(float dt);
    Shot fire(const PhysicsWorld& world, const Pedestrians* pedestrians, Vec3 origin, Vec3 direction,
        bool held, bool pressed, bool aiming);
private:
    WeaponType selected_ = WeaponType::Unarmed;
    std::array<int, int(WeaponType::Count)> ammo_{}, reserve_{};
    float cooldown_ = 0, reload_time_ = 0;
    std::minstd_rand random_{731};
};
} // namespace forza
