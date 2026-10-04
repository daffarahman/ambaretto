#include "ui_font.hpp"
#include "vehicle.hpp"
#include "environment.hpp"
#include "environment_renderer.hpp"
#include "car_renderer.hpp"
#include "tuning_panel.hpp"
#include "menu_bar.hpp"
#include "graphics_panel.hpp"
#include "scene_lighting.hpp"
#include "vehicle_audio.hpp"
#include "player.hpp"
#include "third_person_camera.hpp"
#include "airport.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include "police.hpp"
#include "minimap.hpp"
#include <string>
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace {
Vector3 render_vector(const forza::Vec3& value) {
    return {float(value.GetX()), float(value.GetY()), float(value.GetZ())};
}
Vector3 lerp(Vector3 a, Vector3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

struct SkidMark { forza::Vec3 from, to; };
struct Scene {
    const forza::Environment& environment;
    forza::PhysicsWorld world;
    forza::Car car{world};
    forza::Plane plane{world};
    std::vector<std::unique_ptr<forza::Plane>> aircraft = forza::parked_aircraft(world, environment);
    forza::Traffic traffic{world, environment};
    forza::Pedestrians pedestrians{world, environment};
    forza::Police police{world, environment, &traffic, &pedestrians};
    forza::Player player{world, car, environment, &plane, &traffic, &pedestrians, &aircraft, &police};
    std::vector<SkidMark> marks;
    std::size_t next_mark = 0;
    std::array<forza::Vec3, 4> last_skid{};
    std::array<bool, 4> last_valid{};
    const forza::Car* skid_car = nullptr;
    static constexpr std::size_t max_marks = 2400;

    explicit Scene(const forza::Environment& map) : environment(map), world(map) {
        marks.reserve(max_marks);
        reset();
    }
    void reset() {
        police.clear();
        if (player.flying()) player.recover_plane();
        else if (player.on_foot()) respawn_on_foot();
        else player.reset();
        marks.clear();
        next_mark = 0;
        last_valid.fill(false);
    }
    void respawn_on_foot() { respawn_on_foot(player.position()); }
    void respawn_on_foot(forza::Vec3 origin) {
        police.clear();
        player.respawn_near(origin);
    }

    void record_skids() {
        const auto& current = player.car();
        if (skid_car != &current) { last_valid.fill(false); skid_car = &current; }
        for (std::size_t i = 2; i < 4; ++i) {
            const auto& wheel = current.wheels()[i];
            if (!wheel.grounded || !wheel.skidding) {
                last_valid[i] = false;
                continue;
            }
            const forza::Vec3 point = wheel.ground_point + wheel.ground_normal * 0.09f;
            if (!last_valid[i]) {
                last_skid[i] = point;
                last_valid[i] = true;
                continue;
            }
            const float distance = (point - last_skid[i]).Length();
            if (distance < float(0.12)) continue;
            if (distance < float(1.5)) {
                const SkidMark mark{last_skid[i], point};
                if (marks.size() < max_marks) marks.push_back(mark);
                else {
                    marks[next_mark] = mark;
                    next_mark = (next_mark + 1) % max_marks;
                }
            }
            last_skid[i] = point;
        }
    }
};

void draw_skid_marks(const std::vector<SkidMark>& marks) {
    constexpr Color tint{26, 28, 31, 185};
    for (const auto& mark : marks) {
        const forza::Vec3 direction = mark.to - mark.from;
        const float length = std::hypot(direction.GetX(), direction.GetZ());
        if (length < float(0.001)) continue;
        const forza::Vec3 side(-direction.GetZ() / length * float(0.105), 0,
                             direction.GetX() / length * float(0.105));
        const Vector3 a = render_vector(mark.from - side);
        const Vector3 b = render_vector(mark.from + side);
        const Vector3 c = render_vector(mark.to + side);
        const Vector3 d = render_vector(mark.to - side);
        DrawTriangle3D(a, b, c, tint);
        DrawTriangle3D(a, c, d, tint);
    }
}

void draw_box(const forza::Vec3& center, const forza::Quat& rotation,
              const forza::Vec3& size, Color tint, bool outline = true) {
    const auto half = size * float(0.5);
    const std::array<forza::Vec3, 8> local = {
        forza::Vec3(-half.GetX(), -half.GetY(), -half.GetZ()), forza::Vec3(half.GetX(), -half.GetY(), -half.GetZ()),
        forza::Vec3(half.GetX(), half.GetY(), -half.GetZ()), forza::Vec3(-half.GetX(), half.GetY(), -half.GetZ()),
        forza::Vec3(-half.GetX(), -half.GetY(), half.GetZ()), forza::Vec3(half.GetX(), -half.GetY(), half.GetZ()),
        forza::Vec3(half.GetX(), half.GetY(), half.GetZ()), forza::Vec3(-half.GetX(), half.GetY(), half.GetZ())};
    std::array<Vector3, 8> p{};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = render_vector(center + rotation * local[i]);
    constexpr int faces[6][4] = {{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0}};
    rlBegin(RL_TRIANGLES);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    for (const auto& face : faces) {
        const auto normal = rotation * (local[face[1]] - local[face[2]]).Cross(local[face[0]] - local[face[2]]).Normalized();
        rlNormal3f(normal.GetX(), normal.GetY(), normal.GetZ());
        rlTexCoord2f(0, 0);
        for (int index : {face[2], face[1], face[0], face[3], face[2], face[0]})
            rlVertex3f(p[index].x, p[index].y, p[index].z);
    }
    rlEnd();
    constexpr int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    if (outline) for (const auto& edge : edges) DrawLine3D(p[edge[0]], p[edge[1]], MAROON);
}

Color traffic_paint(std::size_t index) {
    constexpr Color colors[] = {{193, 65, 53, 255}, {64, 127, 161, 255}, {219, 174, 64, 255},
        {83, 139, 100, 255}, {176, 183, 195, 255}, {149, 93, 157, 255}};
    return colors[index % std::size(colors)];
}

void draw_car(const forza::Car& car, const forza::CarRenderer& renderer, const Camera3D& camera,
              Color paint = {235, 235, 224, 255}, Shader override_shader = {}, bool emergency = false) {
    if (car.type() == forza::CarType::Police) paint = {30, 36, 48, 255};
    if (car.destroyed()) paint = {18, 18, 18, 255};
    emergency &= !car.destroyed();
    const bool model_body = renderer.draw_body(car, camera, paint, override_shader);
    if (!model_body) {
        const auto basis = car.rotation();
        draw_box(car.position() + car.rotate(forza::Vec3(0, forza::chassis_offset, 0)), basis,
                 forza::Vec3(float(1.85), float(0.5), float(3.7)), paint);
        draw_box(car.position() + car.rotate(forza::Vec3(0, float(0.93), float(0.25))), basis,
                 forza::Vec3(float(1.45), float(0.55), float(1.7)), car.destroyed() ? paint : Color{39, 58, 73, 255});
    }
    if (car.type() == forza::CarType::Police) {
        const auto box = [&](forza::Vec3 p, forza::Vec3 size, Color color) {
            draw_box(car.position() + car.rotate(p), car.rotation(), size, car.destroyed() ? paint : color, false);
        };
        const float roof = model_body ? .88f : 1.205f;
        box({0, roof + .04f, .18f}, {.95f, .07f, .23f}, BLACK);
        const bool blink = int(GetTime() * 7) % 2 == 0;
        box({-.28f, roof + .11f, .18f}, {.36f, .12f, .22f}, emergency && blink ? RED : Color{94, 24, 36, 255});
        box({.28f, roof + .11f, .18f}, {.36f, .12f, .22f}, emergency && !blink ? SKYBLUE : Color{26, 46, 98, 255});
    }
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        const auto center = car.wheel_center(wheel);
        DrawLine3D(render_vector(mount), render_vector(center), LIGHTGRAY);
        if (renderer.draw_wheel(car, wheel, camera, override_shader)) continue;
        auto axis = car.rotate(forza::Vec3(1, 0, 0));
        if (wheel.front) axis = forza::Quat::sRotation(car.rotate(forza::Vec3(0, 1, 0)), car.steering()) * axis;
        const float radius = car.tuning().wheel_radius, size = radius / forza::wheel_radius;
        DrawCylinderEx(render_vector(center - axis * (0.13f * size)),
                       render_vector(center + axis * (0.13f * size)), radius, radius, 16, BLACK);
        DrawCylinderEx(render_vector(center - axis * (0.14f * size)),
                       render_vector(center + axis * (0.14f * size)), radius * .5f, radius * .5f, 16, DARKGRAY);
    }
}

template<class Vehicle>
void draw_vehicle_damage(const Vehicle& vehicle, float scale = 1, forza::Vec3 smoke = {0, .5f, -1.2f}) {
    if (vehicle.health() > 50) return;
    const bool wreck = vehicle.destroyed();
    const float age = vehicle.explosion_time();
    const auto center = wreck ? vehicle.explosion_position() : vehicle.position() + vehicle.rotate(smoke);
    if (wreck && age < .8f) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * .78539816f;
            const auto p = center + forza::Vec3(std::cos(angle), .5f, std::sin(angle)) * (age * 4 * scale);
            DrawSphereEx(render_vector(p), (.6f + age * 2.5f) * scale, 8, 12, Fade(i % 2 ? ORANGE : GOLD, 1 - age / .8f));
        }
        EndBlendMode();
    }
    if (wreck && age >= 8) return;
    const float phase = wreck ? age : float(GetTime());
    for (int i = 5; i >= 0; --i) {
        const float rise = std::fmod(phase * .7f + i * .19f, 1.f);
        const float angle = i * 2.4f;
        const auto p = center + forza::Vec3(std::cos(angle) * rise, .7f + rise * 4, std::sin(angle) * rise) * scale;
        DrawSphereEx(render_vector(p), (.25f + rise * (wreck ? 1.3f : .6f)) * scale, 6, 8,
            Fade(wreck ? Color{48, 44, 40, 255} : GRAY, (1 - rise) * .65f * (wreck ? 1 - age / 8 : 1)));
    }
}

void draw_character(const forza::Character& character, const forza::Environment& environment, std::size_t appearance = 0, bool police = false) {
    const auto feet = character.position();
    const float ground = environment.surface_height(feet);
    const float radius = std::clamp(0.33f - (feet.GetY() - ground) * 0.08f, 0.18f, 0.33f);
    const auto shadow_point = [&](float x, float z) { return Vector3{x, environment.surface_height(forza::Vec3(x, feet.GetY(), z)) + 0.085f, z}; };
    for (int i = 0; !character.swimming() && i < 24; ++i) {
        const float a = i * 6.2831853f / 24, b = (i + 1) * 6.2831853f / 24;
        DrawTriangle3D(shadow_point(feet.GetX(), feet.GetZ()),
            shadow_point(feet.GetX() + std::cos(b) * radius, feet.GetZ() + std::sin(b) * radius),
            shadow_point(feet.GetX() + std::cos(a) * radius, feet.GetZ() + std::sin(a) * radius), {15, 23, 29, 100});
    }
    constexpr Color shirts[]{{38, 97, 133, 255}, {160, 58, 49, 255}, {219, 174, 62, 255}, {59, 121, 82, 255},
        {168, 164, 156, 255}, {97, 70, 134, 255}, {42, 131, 145, 255}, {219, 115, 69, 255}};
    constexpr Color skins[]{{211, 155, 113, 255}, {129, 83, 60, 255}, {235, 185, 148, 255}, {173, 118, 78, 255}};
    const Color shirt = police ? Color{25, 38, 65, 255} : shirts[appearance % std::size(shirts)], skin = skins[appearance % std::size(skins)];
    const Color pants{39, 48, 66, 255}, shoes = police ? Color{24, 26, 29, 255} : Color{214, 221, 222, 255};
    const auto parts = character.body_parts();
    using Part = forza::BodyPart;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto kind = Part(i);
        const auto& part = parts[i];
        Color color = pants;
        if (kind == Part::Torso || kind == Part::LeftUpperArm || kind == Part::RightUpperArm) color = shirt;
        if (kind == Part::Head || kind == Part::LeftForearm || kind == Part::RightForearm
            || kind == Part::LeftHand || kind == Part::RightHand) color = skin;
        if (kind == Part::LeftFoot || kind == Part::RightFoot) color = shoes;
        if (kind == Part::Head) {
            DrawSphereEx(render_vector(part.position), part.size.GetY() / 2, 10, 12, skin);
            draw_box(part.position + part.rotation * forza::Vec3(0, .14f, .02f), part.rotation,
                forza::Vec3(.28f, .08f, .26f), police ? shirt : Color{44, 35, 31, 255}, false);
            if (police) draw_box(part.position + part.rotation * forza::Vec3(0, .1f, -.13f), part.rotation, {.31f, .035f, .18f}, shirt, false);
            DrawSphereEx(render_vector(part.position + part.rotation * forza::Vec3(0, -.02f, -.16f)), .045f, 6, 8, skin);
        } else if (kind == Part::Pelvis || kind == Part::Torso || kind == Part::LeftHand || kind == Part::RightHand
            || kind == Part::LeftFoot || kind == Part::RightFoot) {
            draw_box(part.position, part.rotation, part.size, color, false);
            if (police && kind == Part::Torso) {
                draw_box(part.position + part.rotation * forza::Vec3(-.075f, .1f, -.116f), part.rotation, {.06f, .075f, .02f}, GOLD, false);
                draw_box(part.position + part.rotation * forza::Vec3(0, -.15f, 0), part.rotation, {.37f, .055f, .245f}, BLACK, false);
            }
        } else {
            const float radius = part.size.GetX() / 2;
            const auto axis = part.rotation * forza::Vec3(0, part.size.GetY() / 2 - radius, 0);
            DrawCapsule(render_vector(part.position - axis), render_vector(part.position + axis), radius, 6, 8, color);
        }
    }
}

void draw_weapon_model(const forza::BodyPartPose& hand, forza::WeaponType type, float flash = 0) {
    if (type == forza::WeaponType::Unarmed) return;
    const auto rotation = hand.rotation;
    const float length = forza::weapon_data(type).length;
    const Color metal{47, 49, 52, 255}, wood{127, 74, 36, 255};
    const auto box = [&](forza::Vec3 offset, forza::Vec3 size, Color color) {
        draw_box(hand.position + rotation * offset, rotation, size, color, false);
    };
    box({0, .06f, -length * .35f}, {.10f, .11f, length}, metal);
    box({0, -.07f, 0}, {.075f, .17f, .09f}, metal);
    if (type != forza::WeaponType::Pistol) {
        box({0, .04f, .09f}, {.09f, .13f, .16f}, type == forza::WeaponType::AK47 ? wood : metal);
        box({0, -.1f, -length * .3f}, {.075f, .22f, .1f}, metal);
        if (type == forza::WeaponType::AK47) box({0, .04f, -.26f}, {.12f, .13f, .21f}, wood);
    }
    if (flash > 0) DrawSphereEx(render_vector(hand.position + rotation * forza::Vec3(0, .06f, -length * .88f)), .07f, 6, 8, YELLOW);
}
void draw_weapon(const forza::Character& character, forza::WeaponType type, float flash) {
    if (character.alive() && !character.ragdolling() && !character.swimming()) draw_weapon_model(character.held_weapon(), type, flash);
}

void draw_wanted(const forza::WantedLevel& wanted, int x, int y) {
    if (!wanted.stars()) return;
    DrawRectangle(x, y, 180, 34, {19, 28, 45, 230});
    for (int i = 0; i < 6; ++i) {
        const Vector2 center{float(x + 20 + i * 28), float(y + 16)};
        std::array<Vector2, 12> vertices; vertices[0] = center;
        for (int j = 0; j <= 10; ++j) {
            const float angle = -j * PI / 5 - PI / 2, radius = j % 2 ? 4.4f : 10.f;
            vertices[j + 1] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        }
        const Color active = wanted.searching() && int(GetTime() * 3) % 2 ? GOLD : RAYWHITE;
        DrawTriangleFan(vertices.data(), int(vertices.size()), i < wanted.stars() ? active : Color{68, 73, 89, 255});
    }
    if (wanted.escape_progress() > 0) DrawRectangle(x, y + 31, int(180 * wanted.escape_progress()), 3, GOLD);
}

void draw_ammo(const forza::Weapons& weapons, int x, int y) {
    if (weapons.selected() == forza::WeaponType::Unarmed) return;
    DrawRectangle(x, y, 180, weapons.reloading() ? 62 : 40, {19, 28, 45, 230});
    const char* ammo = TextFormat("%d / %d", weapons.ammo(), weapons.reserve());
    forza::ui::draw_text(ammo, x + (180 - forza::ui::measure_text(ammo, 24)) / 2, y + 8, 24, RAYWHITE);
    if (weapons.reloading()) {
        const char* status = "RELOADING";
        forza::ui::draw_text(status, x + (180 - forza::ui::measure_text(status, 14)) / 2, y + 40, 14, LIGHTGRAY);
    }
}

void draw_weapon_wheel(forza::WeaponType selection, Vector2 cursor) {
    const Vector2 center{GetScreenWidth() * .5f, GetScreenHeight() * .5f};
    const float radius = std::min(200.f, GetScreenHeight() * .28f);
    DrawRectangle(0, forza::menu_height, GetScreenWidth(), GetScreenHeight() - forza::menu_height, {8, 14, 23, 130});
    for (int i = 0; i < int(forza::WeaponType::Count); ++i) {
        const bool selected = i == int(selection);
        DrawRing(center, radius * .40f, radius, -135.f + i * 90, -45.f + i * 90, 24,
            selected ? Color{68, 134, 157, 235} : Color{27, 36, 45, 235});
        const float angle = i * 1.57079633f;
        const Vector2 label{center.x + std::sin(angle) * radius * .72f, center.y - std::cos(angle) * radius * .72f};
        const char* name = forza::weapon_data(forza::WeaponType(i)).name;
        forza::ui::draw_text(name, int(label.x) - forza::ui::measure_text(name, 18) / 2, int(label.y) - 9, 18, selected ? YELLOW : RAYWHITE);
    }
    DrawCircleV(center, radius * .39f, {16, 24, 32, 245});
    const char* name = forza::weapon_data(selection).name;
    forza::ui::draw_text(name, int(center.x) - forza::ui::measure_text(name, 20) / 2, int(center.y) - 12, 20, RAYWHITE);
    const char* hint = "Release to equip";
    forza::ui::draw_text(hint, int(center.x) - forza::ui::measure_text(hint, 18) / 2, int(center.y + radius + 20), 18, RAYWHITE);
    if (selection != forza::WeaponType::Unarmed) {
        const auto& data = forza::weapon_data(selection);
        const char* details = TextFormat("DAMAGE %.0f   MAG %d   %s", data.damage, data.magazine, data.automatic ? "AUTO" : "SEMI");
        forza::ui::draw_text(details, int(center.x) - forza::ui::measure_text(details, 16) / 2, int(center.y + radius + 50), 16, LIGHTGRAY);
    }
    DrawCircleV({center.x + cursor.x * radius, center.y + cursor.y * radius}, 4, YELLOW);
}

void draw_plane(const forza::Plane& plane, bool occupied) {
    const auto basis = plane.rotation();
    const auto tint = [&](Color color) { return plane.destroyed() ? Color{18, 18, 18, 255} : color; };
    occupied &= !plane.destroyed();
    const auto point = [&](float x, float y, float z) { return plane.position() + plane.rotate(forza::Vec3(x, y, z)); };
    const auto box = [&](forza::Vec3 center, forza::Vec3 size, Color color) {
        draw_box(plane.position() + plane.rotate(center), basis, size, tint(color), false);
    };
    const auto cylinder = [&](forza::Vec3 a, forza::Vec3 b, float r1, float r2, Color color) {
        DrawCylinderEx(render_vector(plane.position() + plane.rotate(a)),
            render_vector(plane.position() + plane.rotate(b)), r1, r2, 12, tint(color));
    };
    constexpr Color paint{234, 236, 223, 255}, blue{39, 103, 152, 255}, glass{59, 112, 139, 255};
    const auto panel = [&](std::array<forza::Vec3, 4> vertices, Color color) {
        color = tint(color);
        std::array<Vector3, 4> p{};
        for (int i = 0; i < 4; ++i) p[i] = render_vector(plane.position() + basis * vertices[i]);
        DrawTriangle3D(p[0], p[1], p[2], color); DrawTriangle3D(p[0], p[2], p[3], color);
        DrawTriangle3D(p[2], p[1], p[0], color); DrawTriangle3D(p[3], p[2], p[0], color);
    };
    if (plane.type() == forza::PlaneType::F18) {
        constexpr Color grey{157, 170, 180, 255}, dark{69, 82, 95, 255};
        cylinder({0, 0, -8.55f}, {0, 0, -4}, .025f, .72f, grey);
        cylinder({0, 0, -4}, {0, 0, 5.5f}, .72f, .8f, grey);
        cylinder({0, .72f, -4.8f}, {0, .78f, -2}, .42f, .22f, glass);
        for (float s : {-1.f, 1.f}) {
            panel({forza::Vec3(s * .6f, -.1f, -2), {s * 6.15f, -.1f, 2.4f},
                {s * 6.15f, -.1f, 4}, {s * .6f, -.1f, 2.6f}}, grey);
            panel({forza::Vec3(s * .5f, .3f, 4.8f), {s * 2.5f, .3f, 6.8f},
                {s * 2.5f, .3f, 8.2f}, {s * .5f, .3f, 8}}, grey);
            panel({forza::Vec3(s * .9f, .3f, 4.8f), {s * 1.6f, 3, 6},
                {s * 1.6f, 2.8f, 8}, {s * .9f, .3f, 8.4f}}, dark);
            cylinder({s * .75f, -.25f, -2.8f}, {s * .75f, -.25f, 7.8f}, .45f, .50f, grey);
            cylinder({s * .75f, -.25f, -2.83f}, {s * .75f, -.25f, -2.75f}, .36f, .36f, BLACK);
            cylinder({s * .75f, -.25f, 7.7f}, {s * .75f, -.25f, 8.5f}, .47f, .32f, dark);
            if (plane.throttle() > .8f && !plane.damaged())
                cylinder({s * .75f, -.25f, 8.5f}, {s * .75f, -.25f, 9.3f}, .29f, .04f, ORANGE);
            DrawSphereEx(render_vector(point(s * 6.15f, -.1f, 2.8f)), .08f, 6, 8, tint(s < 0 ? RED : GREEN));
        }
        if (occupied) DrawSphereEx(render_vector(point(0, 1, -3.6f)), .15f, 8, 10, {211, 155, 113, 255});
    } else if (plane.type() == forza::PlaneType::Boeing747) {
        cylinder({0, 0, -35.33f}, {0, 0, -27}, .2f, 3.1f, paint);
        cylinder({0, 0, -27}, {0, 0, 20}, 3.1f, 3.0f, paint);
        cylinder({0, 0, 20}, {0, 1, 35.33f}, 3, .25f, paint);
        cylinder({0, 2.7f, -28}, {0, 2.7f, -11}, 1.7f, .8f, paint);
        box({0, 3.1f, -28.1f}, {2.4f, .7f, .25f}, glass);
        panel({forza::Vec3(0, .8f, 18), {0, 12, 25}, {0, 12, 31}, {0, .8f, 33}}, blue);
        for (float s : {-1.f, 1.f}) {
            panel({forza::Vec3(s * 2, -.2f, -9), {s * 32.22f, .6f, 8},
                {s * 32.22f, .6f, 11}, {s * 2, -.2f, 11}}, paint);
            panel({forza::Vec3(s * 1.2f, .8f, 22), {s * 12, 1.2f, 29},
                {s * 12, 1.2f, 32}, {s * 1.2f, .8f, 31}}, paint);
            panel({forza::Vec3(s * 32.22f, .6f, 8), {s * 32.22f, 2.5f, 9},
                {s * 32.22f, 2.5f, 11}, {s * 32.22f, .6f, 11}}, blue);
            for (float e : {11.f, 21.f}) {
                box({s * e, -.7f, 1.2f}, {.35f, 2.1f, 2.5f}, LIGHTGRAY);
                cylinder({s * e, -1.7f, -2}, {s * e, -1.7f, 4}, 1.3f, .9f, paint);
                cylinder({s * e, -1.7f, -2.08f}, {s * e, -1.7f, -1.98f}, 1.03f, 1.03f, DARKGRAY);
                DrawSphereEx(render_vector(point(s * e, -1.7f, -2.1f)), .25f, 8, 10, tint(LIGHTGRAY));
            }
            box({s * 3.05f, -.5f, -1}, {.08f, .7f, 45}, blue);
            for (float z = -25; z <= 19; z += 1.6f) box({s * 3.08f, .6f, z}, {.08f, .40f, .45f}, glass);
            DrawSphereEx(render_vector(point(s * 32.22f, .7f, 9)), .17f, 6, 8, tint(s < 0 ? RED : GREEN));
        }
    } else {
    cylinder(forza::Vec3(0, 0, -2.8f), forza::Vec3(0, 0, 1.1f), .42f, .48f, paint);
    cylinder(forza::Vec3(0, 0, 1.1f), forza::Vec3(0, .2f, 3.1f), .48f, .12f, paint);
    box(forza::Vec3(0, .18f, -.8f), forza::Vec3(.84f, .64f, 1.3f), glass);
    box(forza::Vec3(0, .52f, -.8f), forza::Vec3(.88f, .06f, 1.4f), paint);
    for (float side : {-1.0f, 1.0f}) {
        box(forza::Vec3(side * .43f, .18f, -.8f), forza::Vec3(.04f, .65f, .045f), paint);
        box(forza::Vec3(side * .47f, -.18f, -.6f), forza::Vec3(.03f, .12f, 3.3f), blue);
        cylinder(forza::Vec3(side * .42f, -.3f, -.45f), forza::Vec3(side * 3.5f, .57f, -.3f), .035f, .035f, LIGHTGRAY);
    }
    const auto wing = [&](float span, float y, float z, float chord, Color color) {
        color = tint(color);
        const std::array<forza::Vec3, 4> corners = {forza::Vec3(-span, y, z - chord * .36f),
            forza::Vec3(-span, y, z + chord * .4f), forza::Vec3(span, y, z + chord * .4f),
            forza::Vec3(span, y, z - chord * .36f)};
        for (float offset : {-.07f, .07f}) {
            std::array<Vector3, 4> p{};
            for (int i = 0; i < 4; ++i) p[i] = render_vector(plane.position() + basis * (corners[i] + forza::Vec3(0, offset, 0)));
            DrawTriangle3D(p[0], p[1], p[2], color); DrawTriangle3D(p[0], p[2], p[3], color);
            DrawTriangle3D(p[2], p[1], p[0], color); DrawTriangle3D(p[3], p[2], p[0], color);
        }
        box(forza::Vec3(0, y, z - chord * .36f), forza::Vec3(span * 2, .14f, .1f), color);
        box(forza::Vec3(0, y, z + chord * .4f), forza::Vec3(span * 2, .14f, .1f), color);
    };
    wing(5.5f, .6f, -.3f, 1.8f, paint);
    wing(1.8f, .35f, 2.5f, 1.2f, paint);
    for (float side : {-1.0f, 1.0f}) {
        box(forza::Vec3(side * 4.9f, .69f, -.26f), forza::Vec3(.7f, .015f, 1.3f), blue);
        DrawSphereEx(render_vector(point(side * 5.48f, .6f, -.65f)), .09f, 6, 8, tint(side < 0 ? RED : GREEN));
    }
    box(forza::Vec3(0, .95f, 2.5f), forza::Vec3(.14f, 1.3f, 1.1f), blue);
    const auto prop = forza::Quat::sRotation(plane.forward(), plane.propeller_angle());
    const auto blade = prop * plane.rotate(forza::Vec3(0, 1.05f, 0));
    const auto hub = point(0, 0, -3.02f);
    DrawCylinderEx(render_vector(hub - blade), render_vector(hub + blade), .055f, .055f, 6, tint(DARKGRAY));
    DrawSphereEx(render_vector(hub), .18f, 8, 10, tint(blue));
    if (occupied) DrawSphereEx(render_vector(point(-.19f, .25f, -.75f)), .14f, 8, 10, {211, 155, 113, 255});
    }
    for (const auto& wheel : plane.wheels()) {
        const auto mount = plane.position() + plane.rotate(wheel.mount);
        DrawCylinderEx(render_vector(mount), render_vector(wheel.center), .045f, .045f, 8, tint(LIGHTGRAY));
        const float radius = plane.specs().wheel_radius;
        const auto axis = plane.rotate(forza::Vec3(radius * .42f, 0, 0));
        DrawCylinderEx(render_vector(wheel.center - axis), render_vector(wheel.center + axis),
            radius, radius, 12, BLACK);
        if (plane.type() == forza::PlaneType::Boeing747 && !wheel.front) for (float z : {-1.4f, 1.4f}) {
            const auto center = wheel.center + plane.rotate(forza::Vec3(0, 0, z));
            DrawCylinderEx(render_vector(center - axis), render_vector(center + axis), radius, radius, 12, BLACK);
        }
    }
}

void draw_resume_prompt(bool captured) {
    if (!captured) {
        const int panel_width = std::max(408, forza::ui::measure_text("Click to resume and capture mouse", 20) + 28);
        const int x = (GetScreenWidth() - panel_width) / 2 + 14, y = GetScreenHeight() / 2 - 36;
        DrawRectangle(x - 14, y - 12, panel_width, 87, {19, 28, 35, 235});
        forza::ui::draw_text("Click to resume and capture mouse", x, y, 20, RAYWHITE);
        forza::ui::draw_text("F10: menu / Start: resume / Esc: map", x, y + 32, 18, LIGHTGRAY);
    }
}
} // namespace

int main(int argc, char** argv) {
    forza::DayNight day_night;
    std::random_device random;
    day_night.advance(std::uniform_int_distribution<int>(0, 1439)(random));
    std::string screenshot;
    std::string start_district;
    bool performance_tuning = false;
    bool start_controllers = false, start_menu = false, start_help = false;
    bool start_graphics = false;
    std::string quality;
    bool map_open = false, start_region_map = false, start_on_foot = true, tuning_open = false, start_at_airport = false, start_in_plane = false, start_at_traffic = false;
    forza::PlaneType start_plane_type = forza::PlaneType::Trainer;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--overview") { map_open = true; start_region_map = true; }
        if (arg == "--map") map_open = true;
        if (arg == "--beach" || arg == "--keys" || arg == "--key-west" || arg == "--bridge") start_district = arg;
        if (arg == "--on-foot") start_on_foot = true;
        if (arg == "--traffic") { start_at_traffic = true; start_on_foot = true; }
        if (arg == "--tuning") tuning_open = true;
        if (arg == "--performance") { tuning_open = true; performance_tuning = true; }
        if (arg == "--controllers") start_controllers = true;
        if (arg == "--graphics") start_graphics = true;
        if (arg == "--quality" && i + 1 < argc) quality = argv[++i];
        if (arg == "--menu") start_menu = true;
        if (arg == "--help-menu") start_help = true;
        if (arg == "--airport") start_at_airport = true;
        if (arg == "--plane") { start_at_airport = true; start_in_plane = true; }
        if (arg == "--f18" || arg == "--747") {
            start_plane_type = arg == "--f18" ? forza::PlaneType::F18 : forza::PlaneType::Boeing747;
            start_at_airport = start_in_plane = true;
        }
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        if (arg == "--time") {
            if (i + 1 >= argc || !day_night.set_time(argv[++i])) {
                TraceLog(LOG_ERROR, "Time must use HH:MM (00:00 through 23:59)");
                return 1;
            }
        }
    }
    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!screenshot.empty()) window_flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(window_flags);
    InitWindow(1280, 720, "Ambaretto - Miami & Florida Keys");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1024, 600);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    InitAudioDevice();
    bool captured = screenshot.empty() && !map_open && !tuning_open && !start_controllers && !start_menu && !start_help && !start_graphics;
    bool resume_capture = screenshot.empty() && !map_open;
    bool resume_map = map_open;
    if (tuning_open) map_open = false;
    if (captured) DisableCursor();
    {
        const forza::ui::FontResource ui_font;
        forza::VehicleAudio vehicle_audio;
        const forza::Environment environment;
        forza::EnvironmentRenderer scenery(environment);
        forza::CarRenderer car_renderer;
        forza::TuningPanel tuning_panel;
        forza::GraphicsPanel graphics_panel;
        forza::SceneLighting lighting;
        forza::GraphicsSettings graphics;
        const auto graphics_path = std::filesystem::path(GetApplicationDirectory()) / "graphics-settings.ini";
        std::string graphics_status;
        if (std::filesystem::exists(graphics_path)) graphics.load(graphics_path, graphics_status);
        if (!quality.empty()) {
            if (quality == "low") graphics.apply_preset(forza::GraphicsPreset::Low);
            else if (quality == "balanced") graphics.apply_preset(forza::GraphicsPreset::Balanced);
            else if (quality == "high") graphics.apply_preset(forza::GraphicsPreset::High);
            else { TraceLog(LOG_ERROR, "Quality must be low, balanced, or high"); return 1; }
        }
        const auto pacing = [&]() {
            if (graphics.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
            SetTargetFPS(graphics.fps_limit);
        };
        pacing();
        bool graphics_resume_capture = captured, graphics_resume_map = map_open;
        forza::GraphicsSettings graphics_original = graphics;
        if (start_graphics) { graphics_panel.open(graphics); map_open = false; }
        using forza::Action;
        using forza::MenuCommand;
        forza::ControllerMapping controls;
        forza::MenuBar menu;
        const auto mapping_path = std::filesystem::path(GetApplicationDirectory()) / "controller-mappings.ini";
        std::string mapping_error;
        if (std::filesystem::exists(mapping_path)) controls.load(mapping_path, mapping_error);
        else if (screenshot.empty()) controls.save(mapping_path, mapping_error);
        if (start_controllers) menu.show(MenuCommand::Controllers);
        else if (start_help) menu.show(MenuCommand::Controls);
        else if (start_menu) menu.open(2);
        if (performance_tuning) tuning_panel.select_tab(2);
        auto scene = std::make_unique<Scene>(environment);
        scene->car.reset(forza::Vec3(-360, environment.height(-360, 330) + .56f, 330), 3.14159265f);
        if (!start_district.empty()) {
            forza::Vec3 p = start_district == "--beach" ? forza::Vec3(1470, 0, 240) :
                start_district == "--keys" ? environment.islands()[6].center :
                start_district == "--key-west" ? environment.islands().back().center : environment.bridges()[6].point(.5f);
            p.SetY(environment.height(p.GetX(), p.GetZ()) + .56f);
            const auto heading = environment.bridges()[6].a - environment.bridges()[6].b;
            scene->car.reset(p, start_district == "--bridge" ? std::atan2(-heading.GetX(), -heading.GetZ()) : 0);
        }
        forza::Plane* start_aircraft = &scene->plane;
        if (start_at_airport) {
            const auto& airport = forza::airports[start_district == "--key-west" ? 1 : 0];
            if (start_plane_type != forza::PlaneType::Trainer) for (const auto& other : scene->aircraft)
                if (other->type() == start_plane_type && airport.contains(other->position().GetX(), other->position().GetZ()))
                    start_aircraft = other.get();
            const auto vacated = start_aircraft->position();
            start_aircraft->reset(forza::Vec3(airport.plane_x(), forza::Airport::elevation + start_aircraft->parking_height(),
                airport.plane_z()), airport.yaw());
            const auto move_occupant = [&](forza::Plane& other) {
                if (&other != start_aircraft && (other.position() - start_aircraft->position()).Length() < start_aircraft->specs().length / 2 + 5)
                    other.reset(forza::Vec3(vacated.GetX(), environment.terrain_height(vacated.GetX(), vacated.GetZ()) + other.parking_height(), vacated.GetZ()), airport.yaw());
            };
            move_occupant(scene->plane);
            for (const auto& other : scene->aircraft) move_occupant(*other);
            const auto parking = airport.point(airport.plane_along(), 8);
            scene->car.reset(forza::Vec3(parking.x,
                environment.height(parking.x, parking.z) + .56f, parking.z), airport.yaw());
        }
        if (start_in_plane) {
            if (!scene->player.on_foot()) scene->player.interact();
            const auto door = start_aircraft->boarding_position();
            scene->player.character().reset(forza::Vec3(door.GetX(), environment.surface_height(door) + .08f, door.GetZ()));
            scene->player.interact();
        }
        if (start_on_foot && !start_in_plane && !scene->player.on_foot()) scene->player.interact();
        if (start_at_traffic) {
            const auto& car = *scene->traffic.cars().front().car;
            const auto door = car.position() + car.rotate(forza::Vec3(-2, 0, .35f));
            scene->player.character().reset(forza::Vec3(door.GetX(), environment.surface_height(door) + .08f, door.GetZ()));
        }
        forza::ThirdPersonCamera orbit;
        forza::AimAssist aim_assist;
        forza::ThirdPersonCamera saved_orbit = orbit;
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        bool weapon_wheel = false, aiming = false;
        auto wheel_choice = scene->player.weapons().selected();
        Vector2 wheel_cursor{};
        const auto focus = [&]() {
            if (!tuning_open && scene->player.on_foot() && scene->player.character().covering())
                return orbit.shoulder_focus(scene->player.character().body_parts()[int(forza::BodyPart::Head)].position,
                    aiming ? .4f : 0, scene->player.character().cover_aim_side());
            if (scene->player.can_shoot() && scene->player.weapons().selected() != forza::WeaponType::Unarmed && !tuning_open)
                return orbit.shoulder_focus(scene->player.position() + forza::Vec3(0, aiming ? 1.48f : 1.45f, 0),
                    aiming ? .62f : .6f);
            if (!tuning_open) return scene->player.position() + forza::Vec3(0, scene->player.flying() ? .5f : scene->player.driving() ? .7f :
                scene->player.character().swimming() ? 1.55f : 1.25f, 0);
            const auto center = scene->player.car().position() + forza::Vec3(0, .7f, 0);
            const float distance = (orbit.desired_position(center, true) - center).Length();
            // Shift the camera's aim to frame the car in the area beside the
            // 400-pixel panel. Scale with zoom and viewport height (60-deg FOV).
            const auto right = orbit.forward().Cross(forza::Vec3::sAxisY());
            return center + right * (distance * 400.0f / GetScreenHeight() * .57735027f);
        };
        const auto snap_camera = [&]() {
            aim_assist.reset();
            const auto heading = tuning_open ? scene->player.car().forward() : scene->player.forward();
            if (tuning_open) orbit = forza::ThirdPersonCamera{};
            orbit.reset(std::atan2(-heading.GetX(), -heading.GetZ()));
            if (tuning_open) orbit.look(-230, 25, 5, true, heading, 0, 0);
            camera.target = render_vector(focus());
            camera.position = render_vector(orbit.desired_position(focus(), tuning_open || scene->player.driving(), !tuning_open && scene->player.flying(), scene->player.plane().camera_scale()));
        };
        snap_camera();
        forza::WorldMapView world_map;
        float min_x = forza::Environment::extent, min_z = min_x, max_x = -min_x, max_z = -min_x;
        for (const auto& island : environment.islands()) {
            min_x = std::min(min_x, island.center.GetX() - island.radius_x);
            min_z = std::min(min_z, island.center.GetZ() - island.radius_z);
            max_x = std::max(max_x, island.center.GetX() + island.radius_x);
            max_z = std::max(max_z, island.center.GetZ() + island.radius_z);
        }
        const Rectangle map_region{min_x - 160, min_z - 160, max_x - min_x + 320, max_z - min_z + 320};
        const Rectangle initial_map_viewport{14, 124, float(GetScreenWidth() - 28), float(GetScreenHeight() - 202)};
        if (start_region_map) world_map.fit(initial_map_viewport, map_region, forza::Environment::extent);
        else world_map.focus(scene->player.position(), initial_map_viewport, forza::Environment::extent);
        double accumulator = 0;
        float notice_time = mapping_error.empty() ? 0 : 8;
        std::string notice = mapping_error;
        if (!graphics_status.empty()) { notice = "Graphics defaults in use: " + graphics_status; notice_time = 8; TraceLog(LOG_WARNING, "%s", notice.c_str()); }
        bool jump_pending = false, discard_mouse = true, orbit_dragging = false, map_dragging = false, flaps = false;
        bool fire_pending = false, suppress_fire = true;
        float shot_flash = 0, hit_marker = 0, death_time = 0, arrest_time = 0, recoil_return = 0, hurt_flash = 0, kill_flash = 0;
        forza::Vec3 death_position = scene->player.position();
        float previous_health = scene->player.character().health();
        forza::Shot last_shot;
        int rendered_frames = 0;
        bool quit_requested = false;
        const auto mode_action = [&](Action foot, Action car, Action plane) {
            return scene->player.on_foot() ? foot : scene->player.flying() ? plane : car;
        };
        const auto mode_value = [&](Action foot, Action car, Action plane) { return controls.value(mode_action(foot, car, plane)); };
        const auto look_stick = [&]() {
            return Vector2{mode_value(Action::FootLookRight, Action::VehicleLookRight, Action::PlaneLookRight)
                    - mode_value(Action::FootLookLeft, Action::VehicleLookLeft, Action::PlaneLookLeft),
                mode_value(Action::FootLookDown, Action::VehicleLookDown, Action::PlaneLookDown)
                    - mode_value(Action::FootLookUp, Action::VehicleLookUp, Action::PlaneLookUp)};
        };
        const auto zoom_amount = [&]() {
            return mode_value(Action::FootZoomIn, Action::VehicleZoomIn, Action::PlaneZoomIn)
                - mode_value(Action::FootZoomOut, Action::VehicleZoomOut, Action::PlaneZoomOut);
        };
        while (!quit_requested && !WindowShouldClose()) {
            const float elapsed = GetFrameTime(), frame = std::min(elapsed, 0.1f);
            shot_flash = std::max(0.f, shot_flash - frame);
            hit_marker = std::max(0.f, hit_marker - frame);
            kill_flash = std::max(0.f, kill_flash - frame);
            const Rectangle map_viewport{14, 124, float(GetScreenWidth() - 28), float(GetScreenHeight() - 202)};
            const std::array<Rectangle, 5> map_buttons{{
                {float(GetScreenWidth() - 420), 82, 94, 30}, {float(GetScreenWidth() - 318), 82, 94, 30},
                {float(GetScreenWidth() - 216), 82, 42, 30}, {float(GetScreenWidth() - 166), 82, 42, 30},
                {float(GetScreenWidth() - 116), 82, 102, 30}}};
            if (map_open) world_map.constrain(map_viewport, forza::Environment::extent);
            bool mode_changed = false;
            const auto toggle_map = [&]() {
                map_open = !map_open;
                if (map_open) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                captured = !map_open && IsWindowFocused();
                if (captured) DisableCursor(); else EnableCursor();
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                map_dragging = false; discard_mouse = true; mode_changed = true;
                jump_pending = false; accumulator = 0;
            };
            const auto toggle_tuning = [&]() {
                mode_changed = true;
                tuning_panel.cancel_drag();
                orbit_dragging = false; map_dragging = false;
                jump_pending = false; accumulator = 0; discard_mouse = true;
                if (!tuning_open) {
                    resume_capture = captured;
                    resume_map = map_open;
                    saved_orbit = orbit;
                    tuning_open = true; map_open = false; captured = false;
                    EnableCursor(); snap_camera();
                } else {
                    tuning_open = false;
                    map_open = resume_map;
                    orbit = saved_orbit;
                    captured = resume_capture && IsWindowFocused() && !map_open;
                    if (captured) DisableCursor(); else EnableCursor();
                    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                }
            };
            if (screenshot.empty()) {
                const auto controller_input = forza::read_controllers();
                controls.update(controller_input);
                if (!IsWindowFocused()) {
                    if (captured) EnableCursor();
                    captured = false; resume_capture = false;
                    tuning_panel.cancel_drag(); graphics_panel.cancel_drag(); orbit_dragging = false; map_dragging = false; discard_mouse = true;
                }
                const auto command = menu.update(controls, mapping_path, !captured, controller_input);
                if (menu.blocking() || menu.interacted()) {
                    mode_changed = true; jump_pending = false; discard_mouse = true;
                    tuning_panel.cancel_drag(); graphics_panel.cancel_drag(); orbit_dragging = false; map_dragging = false;
                    if (captured) { captured = false; EnableCursor(); }
                }
                const auto close_graphics = [&](bool cancel) {
                    if (cancel) { graphics = graphics_original; pacing(); }
                    graphics_panel.close(); map_open = graphics_resume_map;
                    captured = graphics_resume_capture && IsWindowFocused() && !map_open && !menu.blocking();
                    if (captured) DisableCursor(); else EnableCursor();
                    discard_mouse = true; mode_changed = true;
                };
                const bool graphics_to_map = graphics_panel.visible() && command == MenuCommand::Map;
                if (graphics_panel.visible() && command != MenuCommand::None && command != MenuCommand::Graphics) close_graphics(true);
                if (graphics_to_map) map_open = false;
                if (command == MenuCommand::Graphics && !graphics_panel.visible()) {
                    if (tuning_open) toggle_tuning();
                    graphics_original = graphics;
                    graphics_resume_capture = captured; graphics_resume_map = map_open;
                    graphics_panel.open(graphics); graphics_status.clear();
                    captured = false; map_open = false; EnableCursor(); mode_changed = true;
                    jump_pending = false; accumulator = 0;
                }
                if (graphics_panel.visible() && !mode_changed && !menu.blocking() && !menu.interacted()) {
                    const auto action = graphics_panel.update();
                    if (action == forza::GraphicsPanelAction::Preview) {
                        const auto old = graphics;
                        graphics = graphics_panel.pending();
                        if (old.vsync != graphics.vsync || old.fps_limit != graphics.fps_limit) pacing();
                        graphics_status.clear();
                    } else if (action == forza::GraphicsPanelAction::Apply) {
                        if (graphics_panel.pending().save(graphics_path, graphics_status)) {
                            graphics = graphics_panel.pending(); pacing(); close_graphics(false);
                            notice = "Graphics settings saved"; notice_time = 3;
                        }
                    } else if (action == forza::GraphicsPanelAction::Cancel) close_graphics(true);
                }
                if (command == MenuCommand::Quit) quit_requested = true;
                if (command == MenuCommand::AimMode) {
                    controls.auto_lock = !controls.auto_lock;
                    std::string error;
                    if (!controls.save(mapping_path, error)) { controls.auto_lock = !controls.auto_lock; notice = error; }
                    else notice = controls.auto_lock ? "Aim mode: Auto lock" : "Aim mode: Free aim";
                    notice_time = 3;
                }
                if (command == MenuCommand::Resume) {
                    if (tuning_open) toggle_tuning();
                    map_open = false; captured = true; DisableCursor(); discard_mouse = true; map_dragging = false; SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                }
                if (command == MenuCommand::Pause) {
                    if (tuning_open) toggle_tuning();
                    captured = false; EnableCursor();
                }
                if (command == MenuCommand::Map && tuning_open) { toggle_tuning(); map_open = false; }
                if (command == MenuCommand::Recover) {
                    scene->reset(); snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (command == MenuCommand::CarDefaults) scene->player.car().set_tuning({});
                const bool shortcuts = IsWindowFocused() && !menu.blocking() && !menu.interacted() && !graphics_panel.visible() && !mode_changed;
                if (command == MenuCommand::Tuning || (shortcuts && scene->player.driving() && controls.pressed(Action::Tuning))) {
                    if (scene->player.flying()) { notice = "Land and exit the plane before tuning the car"; notice_time = 3; }
                    else toggle_tuning();
                }
                else if (shortcuts && (controls.pressed(Action::Pause) || IsKeyPressed(KEY_ESCAPE))) {
                    if (tuning_open) toggle_tuning();
                    else if (IsKeyPressed(KEY_ESCAPE) || map_open) toggle_map();
                    else if (captured) { EnableCursor(); captured = false; mode_changed = true; }
                    else if (!IsKeyPressed(KEY_ESCAPE)) { map_open = false; captured = true; DisableCursor(); discard_mouse = true; mode_changed = true; }
                }
                if (!tuning_open && (command == MenuCommand::Map || (shortcuts && controls.pressed(Action::Map) && !mode_changed))) {
                    toggle_map();
                }
                if (map_open && !mode_changed && !menu.blocking() && IsWindowFocused()) {
                    const auto mouse = GetMousePosition();
                    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                    const bool inside = CheckCollisionPointRec(mouse, map_viewport);
                    if (click) for (int i = 0; i < int(map_buttons.size()); ++i) if (CheckCollisionPointRec(mouse, map_buttons[i])) {
                        if (i == 0) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                        else if (i == 1) world_map.fit(map_viewport, map_region, forza::Environment::extent);
                        else if (i == 4) toggle_map();
                        else world_map.zoom(i == 2 ? -1 : 1, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, forza::Environment::extent);
                    }
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    if (IsKeyPressed(KEY_HOME)) world_map.fit(map_viewport, map_region, forza::Environment::extent);
                    if (IsKeyPressed(KEY_C)) world_map.focus(scene->player.position(), map_viewport, forza::Environment::extent);
                    const auto stick = look_stick();
                    const float horizontal = std::max(float(IsKeyDown(KEY_RIGHT)), std::max(mode_value(Action::FootRight, Action::Right, Action::BankRight), std::max(0.f, stick.x)))
                        - std::max(float(IsKeyDown(KEY_LEFT)), std::max(mode_value(Action::FootLeft, Action::Left, Action::BankLeft), std::max(0.f, -stick.x)));
                    const float vertical = std::max(float(IsKeyDown(KEY_DOWN)), std::max(mode_value(Action::FootBackward, Action::Backward, Action::PitchUp), std::max(0.f, stick.y)))
                        - std::max(float(IsKeyDown(KEY_UP)), std::max(mode_value(Action::FootForward, Action::Forward, Action::PitchDown), std::max(0.f, -stick.y)));
                    world_map.pan({-horizontal * 400 * frame, -vertical * 400 * frame}, forza::Environment::extent);
                    const float zoom = float(pressed(KEY_EQUAL) || pressed(KEY_KP_ADD)) - float(pressed(KEY_MINUS) || pressed(KEY_KP_SUBTRACT))
                        + zoom_amount() * 4 * frame;
                    if (zoom != 0) world_map.zoom(zoom, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, forza::Environment::extent);
                    if (inside) world_map.zoom(GetMouseWheelMove(), mouse, map_viewport, forza::Environment::extent);
                    if (click && inside) map_dragging = true;
                    else if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map_dragging = false;
                    if (map_dragging && !click) world_map.pan(GetMouseDelta(), forza::Environment::extent);
                    SetMouseCursor(map_dragging ? MOUSE_CURSOR_RESIZE_ALL : inside ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
                }
                if (tuning_open && !mode_changed && !menu.blocking()) {
                    const auto mouse = GetMousePosition();
                    const bool fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    const forza::TuningPanelInput input{mouse, IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
                        IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsWindowFocused(), fine,
                        int(pressed(KEY_RIGHT)) - int(pressed(KEY_LEFT)),
                        int(pressed(KEY_DOWN)) - int(pressed(KEY_UP)), GetMouseWheelMove()};
                    const auto action = tuning_panel.update(scene->player.car(), GetScreenWidth(), GetScreenHeight(), input);
                    if (action.reset_car) { scene->reset(); snap_camera(); jump_pending = false; accumulator = 0; }
                    if (action.close) toggle_tuning();
                    const bool outside = !tuning_panel.contains(mouse, GetScreenWidth(), GetScreenHeight());
                    const bool dragging = outside && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && IsWindowFocused();
                    if (tuning_open && IsWindowFocused() && !mode_changed && (dragging || (outside && input.scroll != 0))) {
                        Vector2 delta = dragging && orbit_dragging ? GetMouseDelta() : Vector2{};
                        orbit.look(delta.x, delta.y, outside ? input.scroll : 0, true, scene->player.car().forward(), 0, frame);
                    }
                    orbit_dragging = dragging;
                    SetMouseCursor(tuning_open && !outside ? MOUSE_CURSOR_DEFAULT : dragging ? MOUSE_CURSOR_RESIZE_ALL : MOUSE_CURSOR_DEFAULT);
                }
                if (!captured && !map_open && !tuning_open && !graphics_panel.visible() && !menu.blocking() && !mode_changed && IsWindowFocused() &&
                    GetMousePosition().y >= forza::menu_height && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    captured = true; DisableCursor(); discard_mouse = true;
                }
            }
            const bool active = !map_open && !tuning_open && !graphics_panel.visible() && !menu.blocking() && !mode_changed &&
                (!screenshot.empty() || (captured && IsWindowFocused()));
            const bool simulate = !map_open && !graphics_panel.visible() && !menu.blocking() && (active || (tuning_open && (!screenshot.empty() || IsWindowFocused())));
            const bool wheel_held = active && scene->player.can_shoot() && controls.value(Action::WeaponWheel) > .5f;
            if (wheel_held && !weapon_wheel) {
                wheel_cursor = {};
                wheel_choice = scene->player.weapons().selected();
            }
            if (weapon_wheel && !wheel_held) {
                if (active && scene->player.can_shoot()) scene->player.weapons().select(wheel_choice);
                suppress_fire = true;
            }
            weapon_wheel = wheel_held;
            if (!active || weapon_wheel || !scene->player.can_shoot()) { fire_pending = false; suppress_fire = true; }
            else if (controls.value(Action::Fire) < .5f) suppress_fire = false;
            aiming = active && !weapon_wheel && scene->player.can_shoot()
                && scene->player.weapons().selected() != forza::WeaponType::Unarmed && controls.value(Action::Aim) > .5f;
            if (!aiming || !controls.auto_lock) aim_assist.reset();
            day_night.advance(elapsed, screenshot.empty() && simulate);
            if (active && screenshot.empty()) {
                const bool armed = scene->player.on_foot() && scene->player.weapons().selected() != forza::WeaponType::Unarmed;
                const bool reload_pressed = controls.pressed(Action::Reload) && scene->player.can_shoot() && armed && !weapon_wheel;
                if (reload_pressed) scene->player.weapons().reload();
                if (controls.pressed(mode_action(Action::Respawn, Action::Recover, Action::PlaneRecover)) && !reload_pressed && !weapon_wheel) {
                    if (scene->player.on_foot()) scene->respawn_on_foot(); else scene->reset();
                    snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (controls.pressed(mode_action(Action::EnterVehicle, Action::ExitVehicle, Action::PlaneExit)) && !weapon_wheel) {
                    const bool stealing = scene->player.can_steal();
                    const auto result = scene->player.interact();
                    if (result == forza::Interaction::Entered || result == forza::Interaction::Exited) {
                        jump_pending = false;
                        snap_camera();
                        notice = result == forza::Interaction::Entered ? (scene->player.flying() ? "Entered plane - increase throttle to take off" :
                            stealing ? "Pulled driver out - stole traffic car" : "Entered car") : scene->player.character().ragdolling()
                            ? "Jumped out - brace for impact" : "On foot";
                    } else if (result == forza::Interaction::Blocked) notice = "Exit blocked - move the vehicle to an open space";
                    else notice = "Move closer to a car or plane to enter";
                    notice_time = 2.5f;
                }
                if (scene->player.on_foot() && controls.pressed(Action::Jump)) jump_pending = true;
                if (scene->player.on_foot() && !scene->police.arrested() && controls.pressed(Action::Cover) && !weapon_wheel) {
                    if (!scene->player.character().toggle_cover()) {
                        notice = "Move closer to a wall or low barrier to take cover";
                        notice_time = 2.5f;
                    }
                }
                if (scene->player.flying() && controls.pressed(Action::Flaps)) flaps = !flaps;
                Vector2 mouse = GetMouseDelta();
                if (discard_mouse) { mouse = {}; discard_mouse = false; }
                const auto stick = look_stick();
                mouse.x += stick.x * 700 * frame;
                mouse.y += stick.y * 700 * frame;
                if (weapon_wheel) {
                    if (std::hypot(stick.x, stick.y) > .1f) wheel_cursor = stick;
                    else { wheel_cursor.x += mouse.x / 200; wheel_cursor.y += mouse.y / 200; }
                    const float length = std::hypot(wheel_cursor.x, wheel_cursor.y);
                    if (length > 1) { wheel_cursor.x /= length; wheel_cursor.y /= length; }
                    wheel_choice = forza::wheel_selection(wheel_cursor.x, wheel_cursor.y, wheel_choice);
                    mouse = {};
                }
                if (!weapon_wheel && !suppress_fire && controls.pressed(Action::Fire)) fire_pending = true;
                const float returned = std::min(recoil_return, recoil_return * 7 * frame);
                orbit.recoil(-returned); recoil_return -= returned;
                const auto& character = scene->player.character();
                const auto foot_velocity = character.velocity();
                const bool auto_follow = !scene->player.on_foot() || (!aiming && !weapon_wheel && !character.covering()
                    && !character.ragdolling() && character.alive() && !scene->police.arrested()
                    && !(armed && !suppress_fire && controls.value(Action::Fire) > .5f));
                orbit.look(mouse.x, mouse.y, weapon_wheel ? 0 : GetMouseWheelMove() + zoom_amount() * 6 * frame, scene->player.driving(), scene->player.forward(),
                    scene->player.flying() ? scene->player.plane().velocity().Length() : scene->player.driving() ? scene->player.car().velocity().Length()
                        : std::hypot(foot_velocity.GetX(), foot_velocity.GetZ()), frame, scene->player.flying(), auto_follow);
                if (aiming && controls.auto_lock) {
                    std::vector<forza::Character*> candidates;
                    for (const auto& person : scene->pedestrians.people()) if (person.enabled) candidates.push_back(person.character.get());
                    for (const auto& unit : scene->police.units()) if (unit.active)
                        for (const auto& officer : unit.officers) if (!officer.seated) candidates.push_back(officer.character.get());
                    const forza::Vec3 origin(camera.position.x, camera.position.y, camera.position.z);
                    aim_assist.update(scene->world, candidates, &scene->player.character(), origin, orbit.aim_direction(),
                        scene->player.weapons().data().range, mouse.x, mouse.y, frame);
                    if (const auto point = aim_assist.point()) { orbit.aim_at(origin, *point); orbit.recoil(recoil_return); }
                }
                if (aiming) {
                    const auto direction = orbit.aim_direction();
                    camera.target = {camera.position.x + direction.GetX(), camera.position.y + direction.GetY(), camera.position.z + direction.GetZ()};
                }
            }
            forza::Input driving;
            forza::FootInput walking;
            forza::FlightInput flight;
            flight.flaps = flaps;
            driving.parking_brake = tuning_open;
            if (active && screenshot.empty()) {
                driving.throttle = controls.value(Action::Forward) - controls.value(Action::Backward);
                driving.steer = controls.value(Action::Left) - controls.value(Action::Right);
                driving.handbrake = controls.value(Action::Brake) > .5f;
                walking.direction = orbit.move_direction(controls.value(Action::FootForward) - controls.value(Action::FootBackward),
                    controls.value(Action::FootRight) - controls.value(Action::FootLeft));
                walking.sprint = controls.value(Action::Sprint) > .5f
                    || (scene->player.character().swimming() && controls.value(Action::Jump) > .5f);
                if ((aiming || (!suppress_fire && controls.value(Action::Fire) > .5f)) && scene->player.can_shoot()
                    && scene->player.weapons().selected() != forza::WeaponType::Unarmed && !weapon_wheel) {
                    walking.aim_direction = orbit.aim_direction();
                    walking.sprint = false;
                    walking.direction *= .6f;
                }
                flight.throttle = controls.value(Action::ThrottleUp) - controls.value(Action::ThrottleDown);
                flight.pitch = controls.value(Action::PitchUp) - controls.value(Action::PitchDown);
                flight.roll = controls.value(Action::BankLeft) - controls.value(Action::BankRight);
                flight.yaw = controls.value(Action::RudderLeft) - controls.value(Action::RudderRight);
                flight.brake = controls.value(Action::PlaneBrake) > .5f;
            }
            if (simulate) accumulator += frame * (weapon_wheel ? .15f : 1.f);
            else { accumulator = 0; jump_pending = false; }
            while (accumulator >= double(forza::fixed_step)) {
                walking.jump = jump_pending;
                scene->police.set_view({camera.position.x, camera.position.y, camera.position.z},
                    {camera.target.x - camera.position.x, camera.target.y - camera.position.y, camera.target.z - camera.position.z});
                scene->player.step(driving, walking, forza::fixed_step, flight);
                if (active && !weapon_wheel && !suppress_fire && screenshot.empty() && scene->player.can_shoot()
                    && scene->player.weapons().selected() != forza::WeaponType::Unarmed && controls.value(Action::Fire) > .5f) {
                    const auto ray = GetScreenToWorldRay({GetScreenWidth() * .5f, GetScreenHeight() * .5f}, camera);
                    const auto aim = forza::trace_shot(scene->world, &scene->pedestrians,
                        {ray.position.x, ray.position.y, ray.position.z}, {ray.direction.x, ray.direction.y, ray.direction.z}, scene->player.weapons().data().range, &scene->police, &scene->player.character());
                    const auto gun = scene->player.character().held_weapon();
                    // Cast from the receiver so a barrel protruding into cover cannot bypass it.
                    const auto muzzle = gun.position + gun.rotation * forza::Vec3(0, .06f, 0);
                    const auto shot = scene->player.shoot(muzzle, aim.point - muzzle, controls.value(Action::Fire) > .5f, fire_pending, aiming);
                    if (shot.fired) {
                        last_shot = shot; shot_flash = .055f;
                        if (shot.hit) hit_marker = .12f;
                        const float before = orbit.pitch();
                        orbit.recoil(shot.recoil); recoil_return += before - orbit.pitch();
                    }
                }
                fire_pending = fire_pending && active && !weapon_wheel && !suppress_fire && scene->player.can_shoot()
                    && scene->player.character().cover_peeking() && !scene->player.character().cover_aim_ready()
                    && controls.value(Action::Fire) > .5f;
                jump_pending = false;
                scene->record_skids();
                const auto position = scene->player.position();
                const bool recover = scene->player.flying() ? position.Length() > 24000
                    : std::abs(position.GetX()) > forza::Environment::extent - 40 || std::abs(position.GetZ()) > forza::Environment::extent - 40;
                if (recover) {
                    scene->reset(); snap_camera(); flaps = false;
                    notice = scene->player.flying() ? "Recovered aircraft at the airport" : "Recovered from world boundary"; notice_time = 3;
                }
                if (!scene->player.flying() && !scene->player.plane().destroyed() && scene->player.plane().position().GetY() < -.6f) scene->player.recover_plane();
                if (scene->player.on_foot() && !scene->player.character().alive()) {
                    if (death_time == 0) death_position = scene->player.position();
                    death_time += forza::fixed_step;
                    if (death_time > 4) { scene->respawn_on_foot(death_position); snap_camera(); death_time = 0; suppress_fire = true; }
                } else death_time = 0;
                if (scene->police.arrested()) {
                    arrest_time += forza::fixed_step;
                    if (arrest_time > 3) { scene->respawn_on_foot(); snap_camera(); arrest_time = 0; suppress_fire = true; }
                } else arrest_time = 0;
                accumulator -= double(forza::fixed_step);
            }
            if (scene->world.take_player_kill()) kill_flash = .18f;
            const auto target = focus();
            const auto desired = orbit.desired_position(target, tuning_open || scene->player.driving(), !tuning_open && scene->player.flying(),
                scene->player.plane().camera_scale(), aiming && scene->player.can_shoot());
            const float follow = scene->player.driving() && !tuning_open ? 1.f : 1 - std::exp(-12 * frame);
            const bool aim_view = aiming && scene->player.can_shoot();
            camera.target = aim_view ? render_vector(target) : lerp(camera.target, render_vector(target), follow);
            auto smoothed = forza::Vec3(camera.position.x, camera.position.y, camera.position.z);
            smoothed += (desired - smoothed) * follow;
            const auto camera_target = forza::Vec3(camera.target.x, camera.target.y, camera.target.z);
            const auto offset = smoothed - camera_target;
            const float fraction = scene->world.camera_fraction(camera_target, offset,
                (tuning_open || scene->player.driving()) ? scene->player.car().body_id() : scene->player.flying() ? scene->player.plane().body_id() : JPH::BodyID());
            camera.position = render_vector(forza::ThirdPersonCamera::above_water(camera_target + offset * fraction));
            if (aim_view) {
                if (const auto point = aim_assist.point()) {
                    orbit.aim_at({camera.position.x, camera.position.y, camera.position.z}, *point);
                    orbit.recoil(recoil_return);
                }
                const auto direction = orbit.aim_direction();
                camera.target = {camera.position.x + direction.GetX(), camera.position.y + direction.GetY(), camera.position.z + direction.GetZ()};
            }
            vehicle_audio.update(scene->traffic, scene->player.car(), scene->player.position(),
                orbit.forward().Cross(forza::Vec3::sAxisY()), scene->player.driving(),
                active && scene->player.driving() && controls.value(Action::Horn) > .5f, simulate && screenshot.empty(), frame, driving.throttle);
            vehicle_audio.update_police(scene->police, scene->player.position(), orbit.forward().Cross(forza::Vec3::sAxisY()), simulate && screenshot.empty());
            vehicle_audio.update_effects(scene->world, scene->player.position(), orbit.forward().Cross(forza::Vec3::sAxisY()), simulate && screenshot.empty());
            const Camera3D view = camera;
            if (scene->player.character().health() < previous_health) hurt_flash = .3f;
            previous_health = scene->player.character().health();
            hurt_flash = std::max(0.f, hurt_flash - frame);
            rlSetClipPlanes(0.2, 30000);
            notice_time = std::max(0.0f, notice_time - frame);
            BeginDrawing();
            const auto daylight = day_night.lighting();
            ClearBackground(BLACK);
            if (!map_open) {
                std::array<forza::SceneLight, forza::SceneLighting::max_lights> nearby_lights{};
                int light_count = 0;
                if (graphics.local_lights && daylight.night > .01f) {
                    const auto headlights = [&](const forza::Car& car) {
                        if (car.destroyed() || light_count + 2 > forza::SceneLighting::max_lights) return;
                        for (float side : {-1.0f, 1.0f}) nearby_lights[light_count++] = {
                            render_vector(car.position() + car.rotate(forza::Vec3(side * .65f, .40f, -1.83f))),
                            {2.6f * daylight.night, 2.3f * daylight.night, 1.8f * daylight.night}, 48,
                            render_vector((car.forward() + forza::Vec3(0, -.13f, 0)).Normalized()), .87f};
                    };
                    headlights(scene->player.car());
                    for (const auto& unit : scene->police.units()) if (unit.active && light_count < 6
                        && (unit.car->position() - scene->player.position()).LengthSq() < 60 * 60) headlights(*unit.car);
                    for (const auto& vehicle : scene->traffic.cars())
                        if (light_count < 6 && vehicle.car.get() != &scene->player.car() && vehicle.car->simulated() &&
                            (vehicle.car->position() - scene->player.position()).LengthSq() < 45 * 45) headlights(*vehicle.car);
                    const int first_lamp = light_count;
                    std::array<float, forza::SceneLighting::max_lights> distances{};
                    distances.fill(1e30f);
                    const auto p = scene->player.position();
                    for (const auto& lamp : scenery.lights()) {
                        const float distance = (forza::Vec3(lamp.position.x, lamp.position.y, lamp.position.z) - p).LengthSq();
                        if (distance > (lamp.radius + 50) * (lamp.radius + 50)) continue;
                        for (int i = first_lamp; i < forza::SceneLighting::max_lights; ++i) if (distance < distances[i]) {
                            for (int j = forza::SceneLighting::max_lights - 1; j > i; --j) { distances[j] = distances[j - 1]; nearby_lights[j] = nearby_lights[j - 1]; }
                            distances[i] = distance; nearby_lights[i] = lamp;
                            nearby_lights[i].color.x *= daylight.night; nearby_lights[i].color.y *= daylight.night; nearby_lights[i].color.z *= daylight.night;
                            light_count = std::min(light_count + 1, forza::SceneLighting::max_lights); break;
                        }
                    }
                }
                lighting.set_lights(nearby_lights.data(), light_count);
                const auto shadow_focus = render_vector(scene->player.position());
                if (lighting.begin_shadow(shadow_focus, daylight, graphics)) {
                    const auto shader = lighting.shadow_shader();
                    // Include tall, sunward casters throughout the light's depth volume.
                    scenery.draw_shadow(shader, shadow_focus, graphics.shadow_distance * 3 + 250);
                    draw_car(scene->car, car_renderer, view, {235, 235, 224, 255}, shader);
                    for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                        const auto& vehicle = scene->traffic.cars()[i];
                        if (vehicle.car->simulated() && (vehicle.car->position() - scene->player.position()).LengthSq() <
                            (graphics.shadow_distance + 15) * (graphics.shadow_distance + 15))
                            draw_car(*vehicle.car, car_renderer, view, traffic_paint(i), shader);
                    }
                    for (const auto& unit : scene->police.units()) if (unit.active
                        && (unit.car->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_car(*unit.car, car_renderer, view, WHITE, shader);
                    if ((scene->plane.position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_plane(scene->plane, scene->player.flying() && &scene->player.plane() == &scene->plane);
                    for (const auto& other : scene->aircraft) if ((other->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_plane(*other, scene->player.flying() && &scene->player.plane() == other.get());
                    if (scene->player.on_foot()) draw_character(scene->player.character(), environment);
                    for (const auto& pedestrian : scene->pedestrians.people()) if (pedestrian.enabled
                        && (pedestrian.character->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_character(*pedestrian.character, environment, pedestrian.appearance + 1);
                    for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                        const auto& driver = scene->traffic.cars()[i].driver;
                        if (driver && (driver->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                            draw_character(*driver, environment, i + 1);
                    }
                    lighting.end_shadow();
                }
            }
            if (map_open) {
                ClearBackground({0, 0, 112, 255});
                forza::ui::draw_text("MIAMI & FLORIDA KEYS / MAP", 26, 48, 22, RAYWHITE);
                forza::ui::draw_text("NORTH UP / GAMEPLAY PAUSED", 26, 88, 16, {174, 231, 246, 255});
                const char* labels[] = {"Player", "Region", "-", "+", "Close"};
                for (int i = 0; i < int(map_buttons.size()); ++i) {
                    const auto button = map_buttons[i];
                    const bool hover = CheckCollisionPointRec(GetMousePosition(), button);
                    DrawRectangleRec(button, hover ? Color{0, 112, 112, 255} : Color{170, 170, 170, 255});
                    DrawRectangleLinesEx(button, 1, RAYWHITE);
                    forza::ui::draw_text(labels[i], int(button.x + (button.width - forza::ui::measure_text(labels[i], 16)) / 2),
                        int(button.y + 7), 16, hover ? RAYWHITE : Color{0, 0, 0, 255});
                }
                scenery.world_map(world_map, map_viewport, scene->player.position(), scene->player.forward(), &scene->police);
                forza::ui::draw_text("Drag: pan / Wheel: zoom / Arrows or left stick: pan", 26, GetScreenHeight() - 66, 16, RAYWHITE);
                forza::ui::draw_text("Home: region / C: player / +/-: zoom / F2 or Esc: close", 26, GetScreenHeight() - 40, 16, RAYWHITE);
            } else {
                scenery.draw_sky(view, daylight, float(GetTime()), graphics);
                car_renderer.set_lighting(lighting, view, daylight, graphics);
                BeginMode3D(view);
                scenery.draw(view, float(GetTime()), daylight, lighting, graphics);
                BeginShaderMode(scenery.object_shader());
                draw_skid_marks(scene->marks);
                draw_car(scene->car, car_renderer, view);
                for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                    const auto& vehicle = scene->traffic.cars()[i];
                    if (!vehicle.car->simulated()) continue;
                    const auto delta = vehicle.car->position() - forza::Vec3(view.position.x, view.position.y, view.position.z);
                    if (delta.GetX() * delta.GetX() + delta.GetZ() * delta.GetZ() > graphics.view_distance * graphics.view_distance) continue;
                    draw_car(*vehicle.car, car_renderer, view, traffic_paint(i));
                }
                draw_plane(scene->plane, scene->player.flying() && &scene->player.plane() == &scene->plane);
                for (const auto& other : scene->aircraft) if ((other->position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance)
                    draw_plane(*other, scene->player.flying() && &scene->player.plane() == other.get());
                if (scene->player.on_foot()) {
                    draw_character(scene->player.character(), environment);
                    draw_weapon(scene->player.character(), scene->player.weapons().selected(), shot_flash);
                    if (shot_flash > 0) DrawLine3D(render_vector(last_shot.from), render_vector(last_shot.to), {255, 223, 151, 175});
                }
                for (const auto& pedestrian : scene->pedestrians.people()) if (pedestrian.enabled
                    && (pedestrian.character->position() - scene->player.position()).LengthSq() < 160 * 160)
                    draw_character(*pedestrian.character, environment, pedestrian.appearance + 1);
                for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                    const auto& driver = scene->traffic.cars()[i].driver;
                    if (driver && (driver->position() - scene->player.position()).LengthSq() < 160 * 160)
                        draw_character(*driver, environment, i + 1);
                }
                for (const auto& unit : scene->police.units()) if (unit.active && (unit.car->position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance) {
                    draw_car(*unit.car, car_renderer, view, WHITE, {}, scene->police.wanted().stars() > 0 && !unit.claimed);
                    for (const auto& officer : unit.officers) if (!officer.seated) {
                        draw_character(*officer.character, environment, 0, true);
                        if (scene->police.wanted().stars()) draw_weapon(*officer.character, officer.weapon, officer.flash);
                    }
                }
                for (const auto& pickup : scene->police.pickups()) if ((pickup.position - scene->player.position()).LengthSq() < 160 * 160) {
                    DrawCylinder(render_vector(pickup.position - forza::Vec3(0, .07f, 0)), .4f, .4f, .02f, 16, {238, 198, 66, 180});
                    draw_weapon_model({pickup.position + forza::Vec3(0, .08f, 0), forza::Quat::sRotation(forza::Vec3::sAxisZ(), 1.57079633f)}, pickup.weapon);
                }
                EndShaderMode();
                draw_vehicle_damage(scene->car);
                for (const auto& vehicle : scene->traffic.cars()) if (vehicle.car->simulated()
                    && (vehicle.car->position() - scene->player.position()).LengthSq() < 160 * 160) draw_vehicle_damage(*vehicle.car);
                for (const auto& unit : scene->police.units()) if (unit.active
                    && (unit.car->position() - scene->player.position()).LengthSq() < 160 * 160) draw_vehicle_damage(*unit.car);
                const auto draw_plane_damage = [&](const forza::Plane& plane) {
                    if ((plane.position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance)
                        draw_vehicle_damage(plane, plane.explosion_radius() / 10, {0, plane.specs().body_radius, 0});
                };
                draw_plane_damage(scene->plane);
                for (const auto& plane : scene->aircraft) draw_plane_damage(*plane);
                EndMode3D();
                if (kill_flash > 0) DrawRectangle(0, forza::menu_height, GetScreenWidth(), GetScreenHeight() - forza::menu_height,
                    {150, 150, 150, static_cast<unsigned char>(110 * kill_flash / .18f)});
                draw_resume_prompt(captured || tuning_open || graphics_panel.visible() || !screenshot.empty());
                if (!tuning_open) scenery.minimap(scene->player.position(), scene->player.forward(), view, &scene->police, !scene->player.on_foot());
                const auto region = scene->player.position();
                if (!tuning_open) {
                    const auto bounds = forza::MinimapView(GetScreenHeight(), region, scene->player.forward(), view).bounds;
                    const int info_x = int(bounds.x + bounds.width) + 20, info_y = GetScreenHeight() - 42;
                    const auto draw_info = [&](const char* text, int y) {
                        forza::ui::draw_text(text, info_x + 1, y + 1, 20, BLACK);
                        forza::ui::draw_text(text, info_x, y, 20, RAYWHITE);
                    };
                    if (!scene->player.on_foot()) {
                        const auto velocity = scene->player.flying() ? scene->player.plane().velocity() : scene->player.car().velocity();
                        draw_info(TextFormat("%.0f km/h", velocity.Length() * 3.6f), info_y);
                    }
                    const float health = scene->player.character().health();
                    DrawRectangle(26, forza::menu_height + 18, 184, 16, {26, 20, 25, 235});
                    DrawRectangle(28, forza::menu_height + 20, int(180 * health / 100), 12, {177, 112, 137, 255});
                    if (scene->player.on_foot()) {
                        if (scene->player.character().covering()) draw_info(scene->player.character().cover_peeking() ? "COVER - exposed while aiming"
                            : scene->player.character().cover_stance() == forza::CoverStance::Crawling ? "COVER - crawling | Q / B: leave"
                            : scene->player.character().cover_stance() == forza::CoverStance::Crouched ? "COVER - crouched | Q / B: leave"
                            : "COVER - standing | Q / B: leave", info_y - 68);
                        const auto& weapons = scene->player.weapons();
                        if (scene->player.can_shoot() && weapons.selected() != forza::WeaponType::Unarmed && !weapon_wheel && active) {
                            DrawCircle(GetScreenWidth() / 2, GetScreenHeight() / 2, 3, BLACK);
                            DrawCircle(GetScreenWidth() / 2, GetScreenHeight() / 2, 2, hit_marker > 0 ? RED : RAYWHITE);
                        }
                        if (hurt_flash > 0) DrawRectangle(0, forza::menu_height, GetScreenWidth(), GetScreenHeight() - forza::menu_height,
                            {160, 0, 0, static_cast<unsigned char>(hurt_flash * 160)});
                        if (!scene->player.character().alive()) {
                            DrawRectangle(0, forza::menu_height, GetScreenWidth(), GetScreenHeight() - forza::menu_height, {22, 8, 8, 140});
                            const char* text = "WASTED";
                            forza::ui::draw_text(text, (GetScreenWidth() - forza::ui::measure_text(text, 44)) / 2, GetScreenHeight() / 2 - 22, 44, RED);
                        }
                    }
                    if (scene->police.arrested()) {
                        DrawRectangle(0, forza::menu_height, GetScreenWidth(), GetScreenHeight() - forza::menu_height, {8, 14, 28, 135});
                        const char* text = "BUSTED";
                        forza::ui::draw_text(text, (GetScreenWidth() - forza::ui::measure_text(text, 44)) / 2, GetScreenHeight() / 2 - 22, 44, SKYBLUE);
                    }
                    if (scene->player.flying()) {
                        draw_info(TextFormat("ALT %.0f m", std::max(0.f, float(region.GetY()) - forza::Environment::water_level)), info_y - 28);
                    }
                    const char* location = forza::Environment::district(region.GetX(), region.GetZ());
                    const int width = forza::ui::measure_text(location, 20), x = GetScreenWidth() - width - 26;
                    DrawRectangle(x - 12, GetScreenHeight() - 50, width + 24, 36, {19, 28, 45, 230});
                    forza::ui::draw_text(location, x, GetScreenHeight() - 42, 20, RAYWHITE);
                } else forza::ui::draw_text("Settings > Car tuning / Esc to close tuning", 26, GetScreenHeight() - 30, 16, RAYWHITE);
                if (notice_time > 0) forza::ui::draw_text(notice.c_str(), 26, scene->player.flying() ? 190 : 161, 20, RAYWHITE);
                if (tuning_open) tuning_panel.draw(scene->player.car(), GetScreenWidth(), GetScreenHeight());
            }
            const int clock_x = tuning_open ? 14 : GetScreenWidth() - 194, clock_y = tuning_open ? 198 : 46;
            DrawRectangle(clock_x, clock_y, 180, 40, {19, 28, 45, 230});
            const auto clock = day_night.clock();
            forza::ui::draw_text(clock.data(), clock_x + (180 - forza::ui::measure_text(clock.data(), 24)) / 2,
                clock_y + 8, 24, {174, 231, 246, 255});
            if (!tuning_open) {
                draw_wanted(scene->police.wanted(), clock_x, clock_y + 48);
                if (!map_open && scene->player.on_foot()) draw_ammo(scene->player.weapons(), clock_x,
                    clock_y + 48 + (scene->police.wanted().stars() ? 42 : 0));
            }
            if (graphics_panel.visible()) graphics_panel.draw(float(GetFPS()), graphics_status.empty() && lighting.warning() ? lighting.warning() : graphics_status);
            if (weapon_wheel) draw_weapon_wheel(wheel_choice, wheel_cursor);
            menu.draw(controls, mapping_path);
            EndDrawing();
            if (!screenshot.empty() && ++rendered_frames >= 90) {
                const Image capture = LoadImageFromScreen();
                ExportImage(capture, screenshot.c_str());
                UnloadImage(capture);
                break;
            }
        }
        if (!menu.save_pending(controls, mapping_path)) TraceLog(LOG_ERROR, "Could not save controller mappings; previous file retained");
    }
    EnableCursor();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
