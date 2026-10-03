#include "player.hpp"
#include "environment.hpp"
#include "third_person_camera.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace forza;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void tick(Player& player, int count, FootInput input = {}) {
    for (int i = 0; i < count; ++i) player.step({}, input);
}
float top(const Character& character) {
    float result = -1e6f;
    for (const auto& part : character.body_parts()) {
        const float half = (std::abs((part.rotation * Vec3::sAxisX()).GetY()) * part.size.GetX()
            + std::abs((part.rotation * Vec3::sAxisY()).GetY()) * part.size.GetY()
            + std::abs((part.rotation * Vec3::sAxisZ()).GetY()) * part.size.GetZ()) / 2;
        result = std::max(result, part.position.GetY() + half);
    }
    return result;
}
float pose_motion(const std::array<BodyPartPose, static_cast<std::size_t>(BodyPart::Count)>& before, const Character& character) {
    const auto after = character.body_parts();
    float motion = 0;
    for (std::size_t i = 0; i < after.size(); ++i) motion = std::max(motion, (after[i].position - before[i].position).Length());
    return motion;
}
void animate(Player& player, int count, FootInput input = {}) {
    for (int i = 0; i < count; ++i) {
        const auto before = player.character().body_parts();
        tick(player, 1, input);
        require(pose_motion(before, player.character()) < .15f, "cover animation snapped between frames");
        const auto after = player.character().body_parts();
        for (int foot : {int(BodyPart::LeftFoot), int(BodyPart::RightFoot)}) {
            const auto& part = after[foot];
            const float half = (std::abs((part.rotation * Vec3::sAxisX()).GetY()) * part.size.GetX()
                + std::abs((part.rotation * Vec3::sAxisY()).GetY()) * part.size.GetY()
                + std::abs((part.rotation * Vec3::sAxisZ()).GetY()) * part.size.GetZ()) / 2;
            require(part.position.GetY() - half >= player.position().GetY() - .025f, "animated foot clipped through the floor");
        }
    }
}
struct Wall { Vec3 feet, normal, side; float roof, half_length; };
Wall find_wall(Player& player, PhysicsWorld& world, const Environment& map, CoverStance stance) {
    const auto try_wall = [&](Vec3 center, Vec3 size, Quat rotation, Wall& result) {
        for (float sign : {1.f, -1.f}) {
            const Vec3 normal = rotation * Vec3(sign, 0, 0);
            Vec3 feet = center + normal * (size.GetX() / 2 + .8f);
            feet.SetY(map.height(feet.GetX(), feet.GetZ()) + 2);
            GroundHit floor;
            if (!world.cast_ground(feet, -Vec3::sAxisY(), 5, floor)) continue;
            feet = floor.point + Vec3(0, .06f, 0);
            player.respawn_on_foot(feet);
            if (!player.character().can_stand_at(feet)) continue;
            tick(player, 60);
            if (!player.character().toggle_cover() || player.character().cover_stance() != stance) continue;
            result = {feet, normal, normal.Cross(Vec3::sAxisY()), center.GetY() + size.GetY() / 2, size.GetZ() / 2};
            return true;
        }
        return false;
    };
    Wall result;
    if (stance == CoverStance::Standing) {
        for (const auto& wall : map.buildings())
            if (try_wall(wall.solid_center(), wall.solid_size(), Quat::sIdentity(), result)) return result;
    } else for (const auto& wall : map.barriers()) {
        if ((stance == CoverStance::Crawling) != (wall.size.GetY() < 1)) continue;
        if (std::abs(wall.pitch) < .001f && try_wall(wall.center, wall.size, wall.rotation(), result)) return result;
    }
    throw std::runtime_error("no suitable real map wall for cover test");
}
}
int main() {
    try {
        const Environment map;
        PhysicsWorld world(map);
        Car car(world);
        Player player(world, car, map);
        auto& character = player.character();
        require(!character.toggle_cover(), "disabled seated character entered cover");
        player.respawn_on_foot(Vec3(0, map.height(0, 300) + .06f, 300)); tick(player, 60);
        require(!character.toggle_cover(), "open road was treated as cover");
        for (const auto stance : {CoverStance::Standing, CoverStance::Crouched, CoverStance::Crawling}) {
            const Wall wall = find_wall(player, world, map, stance);
            tick(player, 120);
            require(character.covering() && character.cover_stance() == stance, "cover attachment or wall-height stance was lost");
            for (const auto weapon : {WeaponType::Unarmed, WeaponType::Pistol, WeaponType::SMG, WeaponType::AK47}) {
                FootInput pose; pose.weapon = weapon;
                character.step(pose);
                require(top(character) < wall.roof + .02f, "tucked body or weapon arms stood above the wall");
            }
            const Vec3 head = character.body_parts()[int(BodyPart::Head)].position;
            const Vec3 enemy = head - wall.normal * 100;
            const auto blocked = trace_shot(world, nullptr, enemy, head - enemy, 120);
            require(blocked.character != &character && blocked.distance < 100, "wall failed to protect the lowered character from gunfire");
            tick(player, 60);
            const Vec3 start = player.position();
            const auto stationary = character.body_parts();
            tick(player, 1, {wall.side});
            require(pose_motion(stationary, character) < .04f, "starting wall movement snapped the body turn");
            animate(player, 119, {wall.side});
            const float moved = (player.position() - start).Dot(wall.side);
            require(character.covering() && moved > .25f && moved < 1.6f, "cover movement was stuck or used ordinary walking speed");
            require(std::abs((player.position() - start).Dot(wall.normal)) < .1f, "cover movement drifted away from the wall");
            const auto facing = [&]() {
                return character.body_parts()[int(BodyPart::Pelvis)].rotation
                    * (stance == CoverStance::Crawling ? Vec3::sAxisY() : -Vec3::sAxisZ());
            };
            require(facing().Dot(wall.side) > .95f, "wall movement did not turn the body toward travel");
            for (int frame = 0; frame < 120; ++frame) {
                animate(player, 1, {-wall.side});
                if (stance == CoverStance::Crawling) for (const auto& part : character.body_parts()) {
                    const float extent = (std::abs((part.rotation * Vec3::sAxisX()).Dot(wall.normal)) * part.size.GetX()
                        + std::abs((part.rotation * Vec3::sAxisY()).Dot(wall.normal)) * part.size.GetY()
                        + std::abs((part.rotation * Vec3::sAxisZ()).Dot(wall.normal)) * part.size.GetZ()) / 2;
                    require((part.position - wall.feet).Dot(wall.normal) + .8f - extent > -.02f,
                        "crawling direction reversal swung the body through the wall");
                }
            }
            require(facing().Dot(-wall.side) > .95f, "reversing wall movement did not turn the body");
            animate(player, 60);
            require(facing().Dot(-wall.side) > .95f, "stopping wall movement forgot the last facing direction");
            require(character.cover_aim_side().Dot(-wall.side) > .95f, "cover camera forgot the last travel side");
            FootInput aim; aim.aim_direction = -wall.normal; aim.weapon = WeaponType::Pistol;
            player.weapons().select(WeaponType::Pistol);
            tick(player, 60);
            const auto tucked = character.body_parts();
            const auto tucked_gun = character.held_weapon();
            tick(player, 1, aim);
            require(pose_motion(tucked, character) < .04f, "starting aim snapped the cover pose");
            require((character.held_weapon().rotation * Vec3::sAxisZ() - tucked_gun.rotation * Vec3::sAxisZ()).Length() < .05f,
                "starting aim snapped the weapon rotation");
            require(!character.cover_aim_ready(), "cover aim became ready before the raising animation");
            const int ammo = player.weapons().ammo();
            require(!player.shoot(head, -wall.normal, true, true, true).fired && player.weapons().ammo() == ammo,
                "raising the weapon fired early or consumed ammo");
            animate(player, 20, aim);
            const float intermediate = character.body_parts()[int(BodyPart::Head)].position.GetY();
            animate(player, 64, aim);
            if (stance == CoverStance::Standing) {
                require(!character.cover_peeking() && !player.shoot(head, -wall.normal, true, true, true).fired,
                    "player fired through the middle of a tall wall");
            } else {
                require(character.cover_aim_ready() && top(character) > wall.roof + .1f, "aiming failed to expose player above low cover");
                require(intermediate > tucked[int(BodyPart::Head)].position.GetY() + .05f
                    && intermediate < character.body_parts()[int(BodyPart::Head)].position.GetY() - .05f,
                    "cover-to-aim animation had no intermediate rising pose");
                const auto gun = character.held_weapon();
                require(player.shoot(gun.position + Vec3(0, .06f, 0), -wall.normal, true, true, true).fired,
                    "player could not fire while peeking over low cover");
            }
            const auto aimed = character.body_parts();
            tick(player, 1);
            require(pose_motion(aimed, character) < .04f, "releasing aim snapped the cover pose");
            animate(player, 84);
            require(!character.cover_peeking() && top(character) < wall.roof + .02f, "releasing aim did not tuck player back behind cover");
            if (stance != CoverStance::Standing) {
                animate(player, 20, aim);
                animate(player, 12); // Reverse partway through raising, then reverse again while lowering.
                animate(player, 84, aim);
                require(character.cover_aim_ready(), "reversing aim failed to finish raising");
                animate(player, 84);
                require(top(character) < wall.roof + .02f, "reversing aim failed to return behind cover");
            }
            require(character.toggle_cover() && !character.covering(), "cover toggle did not detach");
            animate(player, 84);
            require(top(character) > player.position().GetY() + 1.7f, "leaving low cover did not restore standing pose");
            player.respawn_on_foot(wall.feet); tick(player, 60);
            require(character.toggle_cover(), "could not reenter cover");
            tick(player, 1, {wall.side, true});
            require(!character.covering(), "sprinting did not leave cover");
            player.respawn_on_foot(wall.feet); tick(player, 60); require(character.toggle_cover(), "could not reenter for jump");
            tick(player, 1, {Vec3::sZero(), false, true});
            require(!character.covering() && character.velocity().GetY() > 0, "jumping failed to leave cover and jump");
            player.respawn_on_foot(wall.feet); tick(player, 60); require(character.toggle_cover(), "could not reenter for impact");
            animate(player, 20, aim);
            character.take_damage(1);
            require(character.ragdolling() && !character.covering(), "impact retained cover on a ragdoll");
            tick(player, 60);
            require(std::isfinite(character.position().GetY()) && character.velocity().Length() < 20, "lowered cover pose produced an unstable ragdoll");
            if (stance == CoverStance::Standing) {
                for (float sign : {1.f, -1.f}) {
                    player.respawn_on_foot(wall.feet + wall.side * (sign * (wall.half_length - .6f))); tick(player, 60);
                    require(character.toggle_cover(), "could not take cover near wall end");
                    const Vec3 edge_start = player.position();
                    tick(player, 240, {wall.side * sign});
                    require(character.covering() && (player.position() - edge_start).Dot(wall.side * sign) < .8f, "character walked off the end of cover");
                    const auto edge_pose = character.body_parts();
                    tick(player, 1, aim);
                    require(pose_motion(edge_pose, character) < .04f, "starting edge aim snapped the lean pose");
                    animate(player, 18, aim);
                    const Vec3 halfway = character.body_parts()[int(BodyPart::Head)].position;
                    animate(player, 66, aim);
                    const Vec3 leaned = character.body_parts()[int(BodyPart::Head)].position;
                    const float partial_lean = (halfway - edge_pose[int(BodyPart::Head)].position).Dot(wall.side);
                    require(character.cover_aim_ready() && std::abs(partial_lean) > .1f && std::abs(partial_lean) < .75f
                        && std::abs((leaned - edge_pose[int(BodyPart::Head)].position).Dot(wall.side)) > .75f,
                        "tall-wall edge had no intermediate leaning pose");
                    ThirdPersonCamera camera;
                    camera.reset(std::atan2(wall.normal.GetX(), wall.normal.GetZ()));
                    const Vec3 right = camera.forward().Cross(Vec3::sAxisY());
                    const Vec3 camera_focus = camera.shoulder_focus(leaned, .4f, character.cover_aim_side());
                    const Vec3 shoulder = camera_focus - leaned;
                    require(shoulder.Dot(character.cover_aim_side()) > .3f && std::abs(shoulder.Dot(right) + sign * .4f) < .001f,
                        "edge aim selected the shoulder on the blocked side of cover");
                    require(world.camera_fraction(camera_focus,
                        camera.desired_position(camera_focus, false, false, 1, true) - camera_focus) > .99f,
                        "edge shoulder camera was obstructed by its cover wall");
                    animate(player, 84);
                    require(!character.cover_peeking(), "edge aim failed to return to cover");
                }
            }
            player.respawn_on_foot(wall.feet); tick(player, 60); require(character.toggle_cover(), "could not reenter for reset");
            tick(player, 20, aim);
            player.respawn_on_foot(wall.feet);
            require(!character.covering() && !character.cover_aim_ready() && top(character) > player.position().GetY() + 1.7f,
                "respawn retained an unfinished cover animation");
            std::cout << "Cover stance " << int(stance) << ": slow wall movement, pose height, bullet cover and exits passed at "
                << wall.feet.GetX() << ',' << wall.feet.GetY() << ',' << wall.feet.GetZ() << '\n';
        }
        character.start_swimming(Vec3(1200, Environment::water_level, 1000));
        require(!character.covering() && !character.toggle_cover(), "swimming retained or entered cover");
        std::cout << "Standing, crouching, crawling, directional turns, animated aim/release/reversals, left/right shoulder peeks and cover lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Cover check failed: " << error.what() << '\n'; return 1; }
}
