#include "weapons.hpp"
#include "player.hpp"
#include "pedestrians.hpp"
#include "environment.hpp"
#include "third_person_camera.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    using namespace forza;
    try {
        require(wheel_selection(0, -1, WeaponType::Pistol) == WeaponType::Unarmed &&
            wheel_selection(1, 0, WeaponType::Unarmed) == WeaponType::Pistol &&
            wheel_selection(0, 1, WeaponType::Unarmed) == WeaponType::SMG &&
            wheel_selection(-1, 0, WeaponType::Unarmed) == WeaponType::AK47 &&
            wheel_selection(0, 0, WeaponType::AK47) == WeaponType::AK47, "wheel sectors or deadzone incorrect");
        PhysicsWorld flat(false);
        Character posing(flat);
        for (const auto type : {WeaponType::Pistol, WeaponType::SMG, WeaponType::AK47}) {
            posing.reset(Vec3(20, .08f, 0));
            FootInput input; input.weapon = type;
            for (const float elevation : {0.f, .65f, -.65f}) {
                for (const bool aiming : {false, true}) {
                    input.aim_direction = aiming ? Vec3(0, std::sin(elevation), -std::cos(elevation)) : Vec3::sZero();
                    for (int i = 0; i < 120; ++i) { flat.step(); posing.step(input); }
                    const auto parts = posing.body_parts();
                    const auto gun = posing.held_weapon();
                    const auto& data = weapon_data(type);
                    require(std::abs(gun.size.GetZ() - data.length) < .001f, "weapon length lost its data profile");
                    if (data.two_handed || aiming)
                        require((parts[int(BodyPart::LeftHand)].position - gun.position - gun.rotation * data.support_grip).Length() < .025f,
                            "supporting hand lost its grip in ready/aim pose");
                    for (const auto& part : parts) require(std::isfinite(part.position.LengthSq()) && std::abs(part.rotation.LengthSq() - 1) < .001f,
                        "holding pose contains invalid transform");
                }
            }
            posing.ragdoll(Vec3(3, 1, 0));
            for (int i = 0; i < 120; ++i) {
                flat.step(); posing.step({});
                const auto parts = posing.body_parts();
                for (const auto& part : parts) require(std::isfinite(part.position.LengthSq()) && part.position.GetY() > -.3f,
                    "weapon holding pose destabilized the ragdoll");
                require((parts[int(BodyPart::RightForearm)].position - parts[int(BodyPart::RightUpperArm)].position).Length() < .6f
                    && (parts[int(BodyPart::LeftForearm)].position - parts[int(BodyPart::LeftUpperArm)].position).Length() < .6f,
                    "weapon pose detached a ragdoll arm");
            }
        }
        posing.reset(Vec3(20, .08f, 0));
        float knee_bend = 0;
        for (int i = 0; i < 240; ++i) {
            flat.step(); posing.step({Vec3(0, 0, -1), true});
            const auto parts = posing.body_parts();
            knee_bend = std::max(knee_bend, 2 * std::acos(std::clamp(std::abs(parts[int(BodyPart::LeftShin)].rotation.Dot(parts[int(BodyPart::LeftThigh)].rotation)), 0.f, 1.f)));
        }
        require(posing.velocity().Length() > 7.1f && posing.velocity().Length() < 7.5f, "sprint speed boost incorrect");
        const auto running = posing.body_parts();
        require((running[int(BodyPart::Torso)].rotation * Vec3::sAxisY()).GetZ() < -.15f && knee_bend > 1.45f,
            "sprint lacks forward lean or running knee lift");
        require(std::abs(running[int(BodyPart::RightUpperArm)].rotation.Dot(running[int(BodyPart::RightForearm)].rotation)) < .9f,
            "sprint arms remained in walking pose");
        Weapons weapons;
        const Vec3 origin(0, 2, 0), direction(0, 0, -1);
        require(!weapons.fire(flat, nullptr, origin, direction, true, true, true).fired, "unarmed fired");
        weapons.select(WeaponType::Pistol);
        const auto shot = weapons.fire(flat, nullptr, origin, direction, true, true, true);
        require(shot.fired && weapons.ammo() == 11 && shot.recoil > 0, "pistol did not consume ammo or recoil");
        require(std::abs((shot.to - shot.from).Length() - weapon_data(WeaponType::Pistol).range) < .01f, "weapon-specific range ignored");
        require(!weapons.fire(flat, nullptr, origin, direction, true, true, true).fired, "fire rate not enforced");
        weapons.step(.3f);
        require(!weapons.fire(flat, nullptr, origin, direction, true, false, true).fired, "pistol repeated while held");
        weapons.reload(); weapons.step(.5f);
        require(weapons.reloading() && !weapons.fire(flat, nullptr, origin, direction, true, true, true).fired, "reloading allowed shots");
        weapons.step(1);
        require(weapons.ammo() == 12 && weapons.reserve() == 119, "reload ammo transfer incorrect");
        const auto pistol_audio = flat.take_sound_events();
        require(pistol_audio.size() == 1 && pistol_audio[0].effect == SoundEffect::Pistol
            && (pistol_audio[0].position - origin).Length() < .001f && flat.take_sound_events().empty(),
            "pistol sound missing, duplicated by blocked fire, or not consumed once");
        for (const auto type : {WeaponType::SMG, WeaponType::AK47}) {
            weapons.select(type);
            require(weapons.fire(flat, nullptr, origin, direction, true, false, true).fired, "automatic weapon failed while held");
            weapons.step(weapon_data(type).interval + .01f);
            require(weapons.fire(flat, nullptr, origin, direction, true, false, true).fired, "automatic fire did not repeat");
            weapons.step(.5f);
        }
        const auto automatic_audio = flat.take_sound_events();
        require(automatic_audio.size() == 4 && automatic_audio[0].effect == SoundEffect::SMG
            && automatic_audio[1].effect == SoundEffect::SMG && automatic_audio[2].effect == SoundEffect::AK47
            && automatic_audio[3].effect == SoundEffect::AK47, "automatic gunfire did not emit one matching sound per shot");
        weapons.select(WeaponType::SMG);
        require(weapons.ammo() == 28, "weapon switching lost its ammo state");
        weapons.reload(); weapons.select(WeaponType::Pistol); weapons.step(3);
        weapons.select(WeaponType::SMG);
        require(weapons.ammo() == 28 && !weapons.reloading(), "switching did not cancel reload");
        for (int i = 0; i < 28; ++i) {
            weapons.step(.1f);
            require(weapons.fire(flat, nullptr, origin, direction, true, false, true).fired, "magazine emptied too early");
        }
        weapons.step(.1f);
        require(!weapons.fire(flat, nullptr, origin, direction, true, false, true).fired && weapons.reloading(), "empty magazine did not auto-reload");
        Car blocker(flat); blocker.reset(Vec3(0, .56f, -4));
        GroundHit obstacle;
        require(flat.cast_ray(Vec3(0, 1, 0), direction, 50, obstacle) && obstacle.distance < 5, "ray missed vehicle occlusion");
        Character character(flat);
        character.reset(Vec3(5, .08f, -6), .7f);
        const auto parts = character.body_parts();
        const auto head = parts[int(BodyPart::Head)].position;
        float distance = 20; BodyPart hit;
        require(character.raycast(head + Vec3(0, 0, 5), direction, distance, hit) && hit == BodyPart::Head, "body-part ray missed head");
        character.take_damage(20, hit, Vec3(0, 0, -30));
        require(character.health() == 80 && character.ragdolling(), "nonlethal hit failed to ragdoll");
        character.take_damage(100, BodyPart::Torso, Vec3(0, 0, -10));
        for (int i = 0; i < 900; ++i) { flat.step(); character.step({}); }
        require(!character.alive() && character.health() == 0 && character.ragdolling(), "dead NPC stood up or health became negative");
        character.reset(Vec3(5, .08f, -6));
        require(!character.alive(), "pose reset healed damage");
        character.revive(); require(character.health() == 100, "respawn did not heal");
        character.reset(Vec3(12, 45, 0));
        for (int i = 0; i < 900 && character.alive(); ++i) { flat.step(); character.step({}); }
        require(!character.alive() && character.ragdolling(), "large fall did not cause fatal impact damage");
        ThirdPersonCamera camera;
        const Vec3 camera_focus(0, 4, 0);
        require(std::abs((camera.desired_position(camera_focus, false, false, 1, true) - camera_focus).Length() - .85f) < .01f,
            "aim camera did not close to shoulder");
        const float pitch = camera.pitch(); camera.recoil(.02f);
        require(camera.pitch() < pitch, "recoil did not raise aim");

        Environment environment;
        PhysicsWorld world(environment);
        Pedestrians pedestrians(world, environment);
        Character* npc = nullptr;
        for (const auto& person : pedestrians.people()) if (person.enabled) { npc = person.character.get(); break; }
        require(npc != nullptr, "no test NPC spawned");
        const Vec3 feet(-360, environment.height(-360, 250) + .08f, 250);
        npc->reset(feet);
        const Vec3 torso = npc->body_parts()[int(BodyPart::Torso)].position;
        const Vec3 shooter = torso + Vec3(0, 0, 8);
        auto traced = trace_shot(world, &pedestrians, shooter, direction, 100);
        require(traced.character == npc && traced.part == int(BodyPart::Torso), "closest NPC ray failed");
        weapons.reset(); weapons.select(WeaponType::AK47);
        require(weapons.fire(world, &pedestrians, shooter, direction, true, true, true).hit && npc->health() < 100 && npc->ragdolling(), "NPC bullet failed to damage and ragdoll");
        npc->reset(feet); npc->revive();
        const Vec3 head_origin = npc->body_parts()[int(BodyPart::Head)].position + Vec3(0, 0, 8);
        weapons.step(.2f);
        require(weapons.fire(world, &pedestrians, head_origin, direction, true, true, true).hit && !npc->alive(), "headshot did not apply lethal head damage");
        Car cover(world); cover.reset(feet + Vec3(0, .6f, 4));
        traced = trace_shot(world, &pedestrians, shooter, direction, 100);
        require(traced.character == nullptr && traced.distance < 6, "shot went through vehicle cover");
        Player player(world, cover, environment);
        player.weapons().select(WeaponType::Pistol);
        require(!player.shoot(shooter, direction, true, true, true).fired, "seated player fired");
        require(player.interact() == Interaction::Exited, "player failed to exit for weapon check");
        require(player.shoot(shooter, direction, true, true, true).fired, "on-foot player could not fire");
        player.character().take_damage(100);
        require(!player.shoot(shooter, direction, true, true, true).fired && player.interact() == Interaction::Blocked, "dead player used weapons or entered car");
        player.reset();
        require(player.character().health() == 100 && player.weapons().selected() == WeaponType::Unarmed, "player respawn failed to restore health/loadout");
        player.respawn_on_foot(feet, .4f);
        require(player.on_foot() && player.character().alive() && (player.position() - feet).Length() < .01f, "on-foot respawn lost its location or mode");
        player.weapons().select(WeaponType::AK47);
        player.character().start_swimming(Vec3(1200, Environment::water_level, 1000));
        require(!player.shoot(shooter, direction, true, true, true).fired, "swimming player fired");
        std::cout << "Weapon data, wheel, ammo, fire rates, reload, cover, body-part hits, health, death and on-foot gating passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
