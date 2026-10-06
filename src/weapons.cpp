#include "weapons.hpp"
#include "pedestrians.hpp"
#include "police.hpp"
#include <algorithm>
#include <cmath>

namespace ambaretto {
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
std::optional<Vec3> AimAssist::point() const {
    if (!target_ || !target_->enabled() || !target_->alive()) return std::nullopt;
    const auto parts = target_->body_parts();
    const Vec3 torso = parts[int(BodyPart::Torso)].position;
    const Vec3 to_head = parts[int(BodyPart::Head)].position - torso;
    // A small head-sized band makes fine camera adjustment usable on a stick.
    const float height = to_head.Length();
    const float elevation = std::abs(vertical_ - height) < .06f ? height : vertical_;
    return torso + right_ * horizontal_ + to_head.NormalizedOr(Vec3::sAxisY()) * elevation;
}
void AimAssist::update(const PhysicsWorld& world, const std::vector<Character*>& candidates, const Character* player,
    Vec3 origin, Vec3 direction, float range, float look_x, float look_y, float dt) {
    range = std::min(range, 60.f);
    direction = direction.NormalizedOr(Vec3(0, 0, -1));
    switch_delay_ = std::max(0.f, switch_delay_ - dt);
    const auto visible = [&](Character* candidate, Vec3 point) {
        if (!candidate || candidate == player || !candidate->enabled() || !candidate->alive()) return false;
        const Vec3 offset = point - origin;
        const float distance = offset.Length();
        return distance > .5f && distance <= range
            && trace_shot(world, nullptr, origin, offset, distance + .1f, nullptr, player).character == candidate;
    };
    if (target_ && std::find(candidates.begin(), candidates.end(), target_) == candidates.end()) target_ = nullptr;
    if (target_) {
        if (!target_->enabled() || !target_->alive() || !visible(target_, target_->body_parts()[int(BodyPart::Torso)].position)) {
            target_ = nullptr; horizontal_ = vertical_ = 0;
        }
    }
    if (!target_) {
        if (released_) return;
        float best = .94f; // Acquire only a nearby, visible body in front of the camera.
        for (auto* candidate : candidates) {
            if (!candidate || candidate == player || !candidate->enabled() || !candidate->alive()) continue;
            const Vec3 body = candidate->body_parts()[int(BodyPart::Torso)].position;
            const Vec3 offset = body - origin;
            const float score = offset.NormalizedOr(-direction).Dot(direction) - offset.Length() * .0001f;
            if (score > best && visible(candidate, body)) { best = score; target_ = candidate; }
        }
        horizontal_ = vertical_ = 0;
        if (!target_) return;
        // A fresh aim locks to the chest; adjustment starts on the next frame.
        right_ = (target_->body_parts()[int(BodyPart::Torso)].position - origin).Cross(Vec3::sAxisY()).NormalizedOr(Vec3::sAxisX());
        return;
    }
    const Vec3 body = target_->body_parts()[int(BodyPart::Torso)].position;
    const Vec3 forward = (body - origin).NormalizedOr(direction);
    right_ = forward.Cross(Vec3::sAxisY()).NormalizedOr(Vec3::sAxisX());
    const float sensitivity = std::min(.012f, (body - origin).Length() * .003f);
    horizontal_ += look_x * sensitivity;
    vertical_ -= look_y * sensitivity;
    if (std::abs(horizontal_) > .75f && switch_delay_ == 0) {
        Character* next = nullptr;
        float best = 3.14159265f;
        for (auto* candidate : candidates) {
            if (candidate == target_ || !candidate || candidate == player || !candidate->enabled() || !candidate->alive()) continue;
            const Vec3 point = candidate->body_parts()[int(BodyPart::Torso)].position;
            const Vec3 offset = (point - origin).NormalizedOr(-forward);
            const float angle = std::atan2(offset.Dot(right_), offset.Dot(forward)) * (horizontal_ > 0 ? 1 : -1);
            if (angle > .015f && angle < 1.05f && angle < best && visible(candidate, point)) { best = angle; next = candidate; }
        }
        if (next) { target_ = next; horizontal_ = vertical_ = 0; switch_delay_ = .25f; }
        else { target_ = nullptr; released_ = true; }
    }
    if (std::abs(vertical_) > 1.1f) { target_ = nullptr; released_ = true; }
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
bool Weapons::pickup(WeaponType type) {
    if (type <= WeaponType::Unarmed || type >= WeaponType::Count) return false;
    const auto& gun = weapon_data(type);
    const int slot = int(type), loaded = std::min(gun.magazine - ammo_[slot], gun.magazine);
    const int stored = std::min(gun.reserve - reserve_[slot], gun.magazine - loaded);
    ammo_[slot] += loaded; reserve_[slot] += stored;
    if (type == selected_ && loaded > 0) reload_time_ = 0;
    return loaded + stored > 0;
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
    const bool by_player = ignore && ignore->player_controlled();
    if (hit.character) {
        const auto part = BodyPart(hit.part);
        const float multiplier = part == BodyPart::Head ? 3.f : hit.part >= int(BodyPart::LeftUpperArm) ? .65f : 1.f;
        hit.character->take_damage(data().damage * multiplier, part, direction * data().impulse, DamageSource::Bullet);
        if (alive && !hit.character->alive() && by_player) world.notify_player_kill();
    }
    if (hit.car) hit.car->take_damage(data().damage * VehicleDamage::gunfire_multiplier, by_player);
    if (hit.plane) hit.plane->take_damage(data().damage * VehicleDamage::gunfire_multiplier, by_player);
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
} // namespace ambaretto
