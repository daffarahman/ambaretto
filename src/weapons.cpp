#include "weapons.hpp"
#include "pedestrians.hpp"
#include "police.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
const WeaponData& weapon_data(WeaponType type) {
    static const std::array<WeaponData, int(WeaponType::Count)> data{{
        {"Unarmed", 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0, Vec3::sZero(), false},
        {"Pistol", 28, .26f, 120, .018f, .002f, 1.35f, 32, 12, 120, false, .24f, {-.08f, -.02f, .02f}, false},
        {"SMG", 18, .085f, 160, .010f, .004f, 1.7f, 22, 30, 240, true, .44f, {0, -.025f, -.17f}, true},
        {"AK-47", 36, .12f, 300, .024f, .003f, 2.1f, 48, 30, 180, true, .78f, {0, -.01f, -.24f}, true}
    }};
    return data[std::clamp(int(type), 0, int(WeaponType::Count) - 1)];
}
WeaponType wheel_selection(float x, float y, WeaponType current) {
    if (!std::isfinite(x) || !std::isfinite(y) || x * x + y * y < .04f) return current;
    const float angle = std::atan2(x, -y);
    return WeaponType((int(std::floor(angle / 1.57079633f + .5f)) + 4) % 4);
}
ShotHit trace_shot(const PhysicsWorld& world, const Pedestrians*, Vec3 origin, Vec3 direction, float range, const Police*, const Character* ignore) {
    direction = direction.NormalizedOr(Vec3(0, 0, -1));
    ShotHit result{nullptr, 0, origin + direction * range, range};
    GroundHit obstacle;
    if (world.cast_ray(origin, direction, range, obstacle)) {
        result.point = obstacle.point;
        result.distance = obstacle.distance;
        result.car = obstacle.car;
        result.plane = obstacle.plane;
    }
    world.raycast_characters(origin, direction, result, ignore);
    return result;
}
void Weapons::reset() {
    selected_ = WeaponType::Unarmed;
    cooldown_ = reload_time_ = 0;
    for (int i = 0; i < int(WeaponType::Count); ++i) {
        ammo_[i] = weapon_data(WeaponType(i)).magazine;
        reserve_[i] = weapon_data(WeaponType(i)).reserve;
    }
}
void Weapons::select(WeaponType type) {
    if (int(type) < 0 || type >= WeaponType::Count || type == selected_) return;
    selected_ = type;
    reload_time_ = 0;
}
void Weapons::reload() {
    if (!reloading() && ammo() < data().magazine && reserve() > 0) reload_time_ = data().reload_time;
}
void Weapons::step(float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    cooldown_ = std::max(0.f, cooldown_ - dt);
    if (!reloading()) return;
    reload_time_ = std::max(0.f, reload_time_ - dt);
    if (reloading()) return;
    const int amount = std::min(data().magazine - ammo(), reserve());
    ammo_[int(selected_)] += amount;
    reserve_[int(selected_)] -= amount;
}
Shot Weapons::fire(PhysicsWorld& world, const Pedestrians* pedestrians, Vec3 origin, Vec3 direction,
    bool held, bool pressed, bool aiming, const Police* police, const Character* ignore) {
    if (selected_ == WeaponType::Unarmed || !held || (!data().automatic && !pressed) || reloading() || cooldown_ > 0) return {};
    if (ammo() == 0) { reload(); return {}; }
    direction = direction.NormalizedOr(Vec3(0, 0, -1));
    const Vec3 right = direction.Cross(Vec3::sAxisY()).NormalizedOr(Vec3::sAxisX());
    const Vec3 up = right.Cross(direction);
    std::uniform_real_distribution<float> spread(-data().spread, data().spread);
    direction = (direction + (right * spread(random_) + up * spread(random_)) * (aiming ? 1.f : 3.f)).Normalized();
    const auto hit = trace_shot(world, pedestrians, origin, direction, data().range, police, ignore);
    const bool alive = hit.character && hit.character->alive();
    if (hit.character) {
        const auto part = BodyPart(hit.part);
        const float multiplier = part == BodyPart::Head ? 3.f : hit.part >= int(BodyPart::LeftUpperArm) ? .65f : 1.f;
        hit.character->take_damage(data().damage * multiplier, part, direction * data().impulse);
    }
    if (hit.car) hit.car->take_damage(data().damage * VehicleDamage::gunfire_multiplier);
    if (hit.plane) hit.plane->take_damage(data().damage * VehicleDamage::gunfire_multiplier);
    switch (selected_) {
        case WeaponType::Pistol: world.emit_sound(SoundEffect::Pistol, origin); break;
        case WeaponType::SMG: world.emit_sound(SoundEffect::SMG, origin); break;
        case WeaponType::AK47: world.emit_sound(SoundEffect::AK47, origin); break;
        default: break;
    }
    --ammo_[int(selected_)];
    cooldown_ = data().interval;
    return {true, hit.character != nullptr || hit.car != nullptr || hit.plane != nullptr, origin, hit.point, data().recoil, hit.character,
        alive && !hit.character->alive(), alive};
}
} // namespace forza
