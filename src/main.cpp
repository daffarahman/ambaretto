#include "ui_font.hpp"
#include "desktop.hpp"
#include "vehicle.hpp"
#include "environment.hpp"
#include "environment_renderer.hpp"
#include "car_renderer.hpp"
#include "character_renderer.hpp"
#include "tuning_panel.hpp"
#include "menu_bar.hpp"
#include "graphics_settings.hpp"
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
Vector3 render_vector(const ambaretto::Vec3& value) {
    return {float(value.GetX()), float(value.GetY()), float(value.GetZ())};
}
Vector3 lerp(Vector3 a, Vector3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

struct SkidMark { ambaretto::Vec3 from, to; };
struct Scene {
    const ambaretto::Environment& environment;
    std::vector<ambaretto::CarDesign> designs;
    std::vector<ambaretto::CharacterDesign> characters;
    ambaretto::PhysicsWorld world;
    ambaretto::Car car{world};
    ambaretto::Plane plane{world};
    std::vector<std::unique_ptr<ambaretto::Plane>> aircraft = ambaretto::parked_aircraft(world, environment);
    ambaretto::Traffic traffic{world, environment, &designs, &characters};
    ambaretto::Pedestrians pedestrians{world, environment, &characters};
    ambaretto::Police police{world, environment, &traffic, &pedestrians, &designs, &characters};
    ambaretto::Player player{world, car, environment, &plane, &traffic, &pedestrians, &aircraft, &police};
    std::vector<SkidMark> marks;
    std::size_t next_mark = 0;
    std::array<ambaretto::Vec3, 4> last_skid{};
    std::array<bool, 4> last_valid{};
    const ambaretto::Car* skid_car = nullptr;
    static constexpr std::size_t max_marks = 2400;

    Scene(const ambaretto::Environment& map,std::vector<ambaretto::CarDesign> cars,std::vector<ambaretto::CharacterDesign> people) : environment(map), designs(std::move(cars)), characters(std::move(people)), world(map) {
        if (map.city() && map.city()->player_character) player.character().set_design(*map.city()->player_character);
        marks.reserve(max_marks);
        if (map.city()) { plane.set_simulated(false); plane.reset({-9000,-8,-9000}); }
        if (!map.city()) {
            if (!designs.empty()) car.set_design(designs.front());
            else { car.set_simulated(false); player.respawn_on_foot(map.spawn()-ambaretto::Vec3(0,.48f,0)); }
        }
        reset();
    }
    void reset() {
        police.clear();
        if (environment.city() && !player.flying() && player.on_foot()) {
            player.respawn_on_foot(environment.spawn()-ambaretto::Vec3(0,.48f,0));
            marks.clear(); next_mark = 0; last_valid.fill(false); return;
        }
        if (player.flying()) player.recover_plane();
        else if (player.on_foot()) respawn_on_foot();
        else player.reset();
        marks.clear();
        next_mark = 0;
        last_valid.fill(false);
    }
    void respawn_on_foot() { respawn_on_foot(player.position()); }
    void respawn_on_foot(ambaretto::Vec3 origin) {
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
            const ambaretto::Vec3 point = wheel.ground_point + wheel.ground_normal * 0.09f;
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
        const ambaretto::Vec3 direction = mark.to - mark.from;
        const float length = std::hypot(direction.GetX(), direction.GetZ());
        if (length < float(0.001)) continue;
        const ambaretto::Vec3 side(-direction.GetZ() / length * float(0.105), 0,
                             direction.GetX() / length * float(0.105));
        const Vector3 a = render_vector(mark.from - side);
        const Vector3 b = render_vector(mark.from + side);
        const Vector3 c = render_vector(mark.to + side);
        const Vector3 d = render_vector(mark.to - side);
        DrawTriangle3D(a, b, c, tint);
        DrawTriangle3D(a, c, d, tint);
    }
}

void draw_box(const ambaretto::Vec3& center, const ambaretto::Quat& rotation,
              const ambaretto::Vec3& size, Color tint, bool outline = true) {
    const auto half = size * float(0.5);
    const std::array<ambaretto::Vec3, 8> local = {
        ambaretto::Vec3(-half.GetX(), -half.GetY(), -half.GetZ()), ambaretto::Vec3(half.GetX(), -half.GetY(), -half.GetZ()),
        ambaretto::Vec3(half.GetX(), half.GetY(), -half.GetZ()), ambaretto::Vec3(-half.GetX(), half.GetY(), -half.GetZ()),
        ambaretto::Vec3(-half.GetX(), -half.GetY(), half.GetZ()), ambaretto::Vec3(half.GetX(), -half.GetY(), half.GetZ()),
        ambaretto::Vec3(half.GetX(), half.GetY(), half.GetZ()), ambaretto::Vec3(-half.GetX(), half.GetY(), half.GetZ())};
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

void draw_car(const ambaretto::Car& car, const ambaretto::CarRenderer& renderer, const Camera3D& camera,
              Shader override_shader = {}) {
    if (!car.design() || !renderer.draw_body(car,camera,override_shader)) return;
    for (const auto& wheel : car.wheels()) {
        const auto mount = car.position() + car.rotate(wheel.mount);
        const auto center = car.wheel_center(wheel);
        DrawLine3D(render_vector(mount), render_vector(center), LIGHTGRAY);
        renderer.draw_wheel(car, wheel, camera, override_shader);
    }
}

template<class Vehicle>
void draw_vehicle_damage(const Vehicle& vehicle, const ambaretto::CarRenderer& renderer, const Camera3D& camera,
                         float draw_distance, float scale = 1, ambaretto::Vec3 smoke = {0, .5f, -1.2f}) {
    if (vehicle.health() > 50) return;
    const bool wreck = vehicle.destroyed();
    const float age = vehicle.explosion_time();
    const auto center = wreck ? vehicle.explosion_position() : vehicle.position() + vehicle.rotate(smoke);
    if ((center - ambaretto::Vec3(camera.position.x, camera.position.y, camera.position.z)).LengthSq() > draw_distance * draw_distance) return;
    renderer.draw_damage(camera, center, wreck, age, scale, float(GetTime()));
}

void draw_weapon_model(const ambaretto::BodyPartPose& hand, ambaretto::WeaponType type, float flash = 0) {
    if (type == ambaretto::WeaponType::Unarmed) return;
    const auto rotation = hand.rotation;
    const float length = ambaretto::weapon_data(type).length;
    const Color metal{47, 49, 52, 255}, wood{127, 74, 36, 255};
    const auto box = [&](ambaretto::Vec3 offset, ambaretto::Vec3 size, Color color) {
        draw_box(hand.position + rotation * offset, rotation, size, color, false);
    };
    box({0, .06f, -length * .35f}, {.10f, .11f, length}, metal);
    box({0, -.07f, 0}, {.075f, .17f, .09f}, metal);
    if (type != ambaretto::WeaponType::Pistol) {
        box({0, .04f, .09f}, {.09f, .13f, .16f}, type == ambaretto::WeaponType::AK47 ? wood : metal);
        box({0, -.1f, -length * .3f}, {.075f, .22f, .1f}, metal);
        if (type == ambaretto::WeaponType::AK47) box({0, .04f, -.26f}, {.12f, .13f, .21f}, wood);
    }
    if (flash > 0) DrawSphereEx(render_vector(hand.position + rotation * ambaretto::Vec3(0, .06f, -length * .88f)), .07f, 6, 8, YELLOW);
}
void draw_weapon(const ambaretto::Character& character, ambaretto::WeaponType type, float flash) {
    if (character.alive() && !character.ragdolling() && !character.swimming()) draw_weapon_model(character.held_weapon(), type, flash);
}

void draw_wanted(const ambaretto::WantedLevel& wanted, int x, int y) {
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

void draw_ammo(const ambaretto::Weapons& weapons, int x, int y) {
    if (weapons.selected() == ambaretto::WeaponType::Unarmed) return;
    DrawRectangle(x, y, 180, weapons.reloading() ? 62 : 40, {19, 28, 45, 230});
    const char* ammo = TextFormat("%d / %d", weapons.ammo(), weapons.reserve());
    ambaretto::ui::draw_text(ammo, x + (180 - ambaretto::ui::measure_text(ammo, 24)) / 2, y + 8, 24, RAYWHITE);
    if (weapons.reloading()) {
        const char* status = "RELOADING";
        ambaretto::ui::draw_text(status, x + (180 - ambaretto::ui::measure_text(status, 14)) / 2, y + 40, 14, LIGHTGRAY);
    }
}

void draw_weapon_wheel(ambaretto::WeaponType selection, Vector2 cursor) {
    const Vector2 center{GetScreenWidth() * .5f, GetScreenHeight() * .5f};
    const float radius = std::min(200.f, GetScreenHeight() * .28f);
    DrawRectangle(0, ambaretto::menu_height, GetScreenWidth(), GetScreenHeight() - ambaretto::menu_height, {8, 14, 23, 130});
    for (int i = 0; i < int(ambaretto::WeaponType::Count); ++i) {
        const bool selected = i == int(selection);
        DrawRing(center, radius * .40f, radius, -135.f + i * 90, -45.f + i * 90, 24,
            selected ? Color{68, 134, 157, 235} : Color{27, 36, 45, 235});
        const float angle = i * 1.57079633f;
        const Vector2 label{center.x + std::sin(angle) * radius * .72f, center.y - std::cos(angle) * radius * .72f};
        const char* name = ambaretto::weapon_data(ambaretto::WeaponType(i)).name;
        ambaretto::ui::draw_text(name, int(label.x) - ambaretto::ui::measure_text(name, 18) / 2, int(label.y) - 9, 18, selected ? YELLOW : RAYWHITE);
    }
    DrawCircleV(center, radius * .39f, {16, 24, 32, 245});
    const char* name = ambaretto::weapon_data(selection).name;
    ambaretto::ui::draw_text(name, int(center.x) - ambaretto::ui::measure_text(name, 20) / 2, int(center.y) - 12, 20, RAYWHITE);
    const char* hint = "Release to equip";
    ambaretto::ui::draw_text(hint, int(center.x) - ambaretto::ui::measure_text(hint, 18) / 2, int(center.y + radius + 20), 18, RAYWHITE);
    if (selection != ambaretto::WeaponType::Unarmed) {
        const auto& data = ambaretto::weapon_data(selection);
        const char* details = TextFormat("DAMAGE %.0f   MAG %d   %s", data.damage, data.magazine, data.automatic ? "AUTO" : "SEMI");
        ambaretto::ui::draw_text(details, int(center.x) - ambaretto::ui::measure_text(details, 16) / 2, int(center.y + radius + 50), 16, LIGHTGRAY);
    }
    DrawCircleV({center.x + cursor.x * radius, center.y + cursor.y * radius}, 4, YELLOW);
}

void draw_plane(const ambaretto::Plane& plane, bool occupied) {
    const auto basis = plane.rotation();
    const auto tint = [&](Color color) { return plane.destroyed() ? Color{18, 18, 18, 255} : color; };
    occupied &= !plane.destroyed();
    const auto point = [&](float x, float y, float z) { return plane.position() + plane.rotate(ambaretto::Vec3(x, y, z)); };
    const auto box = [&](ambaretto::Vec3 center, ambaretto::Vec3 size, Color color) {
        draw_box(plane.position() + plane.rotate(center), basis, size, tint(color), false);
    };
    const auto cylinder = [&](ambaretto::Vec3 a, ambaretto::Vec3 b, float r1, float r2, Color color) {
        DrawCylinderEx(render_vector(plane.position() + plane.rotate(a)),
            render_vector(plane.position() + plane.rotate(b)), r1, r2, 12, tint(color));
    };
    constexpr Color paint{234, 236, 223, 255}, blue{39, 103, 152, 255}, glass{59, 112, 139, 255};
    const auto panel = [&](std::array<ambaretto::Vec3, 4> vertices, Color color) {
        color = tint(color);
        std::array<Vector3, 4> p{};
        for (int i = 0; i < 4; ++i) p[i] = render_vector(plane.position() + basis * vertices[i]);
        DrawTriangle3D(p[0], p[1], p[2], color); DrawTriangle3D(p[0], p[2], p[3], color);
        DrawTriangle3D(p[2], p[1], p[0], color); DrawTriangle3D(p[3], p[2], p[0], color);
    };
    if (plane.type() == ambaretto::PlaneType::F18) {
        constexpr Color grey{157, 170, 180, 255}, dark{69, 82, 95, 255};
        cylinder({0, 0, -8.55f}, {0, 0, -4}, .025f, .72f, grey);
        cylinder({0, 0, -4}, {0, 0, 5.5f}, .72f, .8f, grey);
        cylinder({0, .72f, -4.8f}, {0, .78f, -2}, .42f, .22f, glass);
        for (float s : {-1.f, 1.f}) {
            panel({ambaretto::Vec3(s * .6f, -.1f, -2), {s * 6.15f, -.1f, 2.4f},
                {s * 6.15f, -.1f, 4}, {s * .6f, -.1f, 2.6f}}, grey);
            panel({ambaretto::Vec3(s * .5f, .3f, 4.8f), {s * 2.5f, .3f, 6.8f},
                {s * 2.5f, .3f, 8.2f}, {s * .5f, .3f, 8}}, grey);
            panel({ambaretto::Vec3(s * .9f, .3f, 4.8f), {s * 1.6f, 3, 6},
                {s * 1.6f, 2.8f, 8}, {s * .9f, .3f, 8.4f}}, dark);
            cylinder({s * .75f, -.25f, -2.8f}, {s * .75f, -.25f, 7.8f}, .45f, .50f, grey);
            cylinder({s * .75f, -.25f, -2.83f}, {s * .75f, -.25f, -2.75f}, .36f, .36f, BLACK);
            cylinder({s * .75f, -.25f, 7.7f}, {s * .75f, -.25f, 8.5f}, .47f, .32f, dark);
            if (plane.throttle() > .8f && !plane.damaged())
                cylinder({s * .75f, -.25f, 8.5f}, {s * .75f, -.25f, 9.3f}, .29f, .04f, ORANGE);
            DrawSphereEx(render_vector(point(s * 6.15f, -.1f, 2.8f)), .08f, 6, 8, tint(s < 0 ? RED : GREEN));
        }
        if (occupied) DrawSphereEx(render_vector(point(0, 1, -3.6f)), .15f, 8, 10, {211, 155, 113, 255});
    } else if (plane.type() == ambaretto::PlaneType::Boeing747) {
        cylinder({0, 0, -35.33f}, {0, 0, -27}, .2f, 3.1f, paint);
        cylinder({0, 0, -27}, {0, 0, 20}, 3.1f, 3.0f, paint);
        cylinder({0, 0, 20}, {0, 1, 35.33f}, 3, .25f, paint);
        cylinder({0, 2.7f, -28}, {0, 2.7f, -11}, 1.7f, .8f, paint);
        box({0, 3.1f, -28.1f}, {2.4f, .7f, .25f}, glass);
        panel({ambaretto::Vec3(0, .8f, 18), {0, 12, 25}, {0, 12, 31}, {0, .8f, 33}}, blue);
        for (float s : {-1.f, 1.f}) {
            panel({ambaretto::Vec3(s * 2, -.2f, -9), {s * 32.22f, .6f, 8},
                {s * 32.22f, .6f, 11}, {s * 2, -.2f, 11}}, paint);
            panel({ambaretto::Vec3(s * 1.2f, .8f, 22), {s * 12, 1.2f, 29},
                {s * 12, 1.2f, 32}, {s * 1.2f, .8f, 31}}, paint);
            panel({ambaretto::Vec3(s * 32.22f, .6f, 8), {s * 32.22f, 2.5f, 9},
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
    cylinder(ambaretto::Vec3(0, 0, -2.8f), ambaretto::Vec3(0, 0, 1.1f), .42f, .48f, paint);
    cylinder(ambaretto::Vec3(0, 0, 1.1f), ambaretto::Vec3(0, .2f, 3.1f), .48f, .12f, paint);
    box(ambaretto::Vec3(0, .18f, -.8f), ambaretto::Vec3(.84f, .64f, 1.3f), glass);
    box(ambaretto::Vec3(0, .52f, -.8f), ambaretto::Vec3(.88f, .06f, 1.4f), paint);
    for (float side : {-1.0f, 1.0f}) {
        box(ambaretto::Vec3(side * .43f, .18f, -.8f), ambaretto::Vec3(.04f, .65f, .045f), paint);
        box(ambaretto::Vec3(side * .47f, -.18f, -.6f), ambaretto::Vec3(.03f, .12f, 3.3f), blue);
        cylinder(ambaretto::Vec3(side * .42f, -.3f, -.45f), ambaretto::Vec3(side * 3.5f, .57f, -.3f), .035f, .035f, LIGHTGRAY);
    }
    const auto wing = [&](float span, float y, float z, float chord, Color color) {
        color = tint(color);
        const std::array<ambaretto::Vec3, 4> corners = {ambaretto::Vec3(-span, y, z - chord * .36f),
            ambaretto::Vec3(-span, y, z + chord * .4f), ambaretto::Vec3(span, y, z + chord * .4f),
            ambaretto::Vec3(span, y, z - chord * .36f)};
        for (float offset : {-.07f, .07f}) {
            std::array<Vector3, 4> p{};
            for (int i = 0; i < 4; ++i) p[i] = render_vector(plane.position() + basis * (corners[i] + ambaretto::Vec3(0, offset, 0)));
            DrawTriangle3D(p[0], p[1], p[2], color); DrawTriangle3D(p[0], p[2], p[3], color);
            DrawTriangle3D(p[2], p[1], p[0], color); DrawTriangle3D(p[3], p[2], p[0], color);
        }
        box(ambaretto::Vec3(0, y, z - chord * .36f), ambaretto::Vec3(span * 2, .14f, .1f), color);
        box(ambaretto::Vec3(0, y, z + chord * .4f), ambaretto::Vec3(span * 2, .14f, .1f), color);
    };
    wing(5.5f, .6f, -.3f, 1.8f, paint);
    wing(1.8f, .35f, 2.5f, 1.2f, paint);
    for (float side : {-1.0f, 1.0f}) {
        box(ambaretto::Vec3(side * 4.9f, .69f, -.26f), ambaretto::Vec3(.7f, .015f, 1.3f), blue);
        DrawSphereEx(render_vector(point(side * 5.48f, .6f, -.65f)), .09f, 6, 8, tint(side < 0 ? RED : GREEN));
    }
    box(ambaretto::Vec3(0, .95f, 2.5f), ambaretto::Vec3(.14f, 1.3f, 1.1f), blue);
    const auto prop = ambaretto::Quat::sRotation(plane.forward(), plane.propeller_angle());
    const auto blade = prop * plane.rotate(ambaretto::Vec3(0, 1.05f, 0));
    const auto hub = point(0, 0, -3.02f);
    DrawCylinderEx(render_vector(hub - blade), render_vector(hub + blade), .055f, .055f, 6, tint(DARKGRAY));
    DrawSphereEx(render_vector(hub), .18f, 8, 10, tint(blue));
    if (occupied) DrawSphereEx(render_vector(point(-.19f, .25f, -.75f)), .14f, 8, 10, {211, 155, 113, 255});
    }
    for (const auto& wheel : plane.wheels()) {
        const auto mount = plane.position() + plane.rotate(wheel.mount);
        DrawCylinderEx(render_vector(mount), render_vector(wheel.center), .045f, .045f, 8, tint(LIGHTGRAY));
        const float radius = plane.specs().wheel_radius;
        const auto axis = plane.rotate(ambaretto::Vec3(radius * .42f, 0, 0));
        DrawCylinderEx(render_vector(wheel.center - axis), render_vector(wheel.center + axis),
            radius, radius, 12, BLACK);
        if (plane.type() == ambaretto::PlaneType::Boeing747 && !wheel.front) for (float z : {-1.4f, 1.4f}) {
            const auto center = wheel.center + plane.rotate(ambaretto::Vec3(0, 0, z));
            DrawCylinderEx(render_vector(center - axis), render_vector(center + axis), radius, radius, 12, BLACK);
        }
    }
}

void draw_resume_prompt(bool captured) {
    if (!captured) {
        const int panel_width = std::max(408, ambaretto::ui::measure_text("Game running / F10: menu / F2: map", 18) + 28);
        const int x = (GetScreenWidth() - panel_width) / 2 + 14, y = GetScreenHeight() / 2 - 36;
        DrawRectangle(x - 14, y - 12, panel_width, 87, {19, 28, 35, 235});
        ambaretto::ui::draw_text("Click to capture mouse", x, y, 20, RAYWHITE);
        ambaretto::ui::draw_text("Game running / F10: menu / F2: map", x, y + 32, 18, LIGHTGRAY);
    }
}
} // namespace

int main(int argc, char** argv) {
    ambaretto::DayNight day_night;
    std::string time_override;
    std::string screenshot;
    std::string city_path;
    std::string session, design_path;
    bool cities_app=false, settings_app=false, create_new=false;
    bool preview_editor = false, preview_building = false, preview_car = false, preview_character = false;
    bool performance_tuning = false;
    bool start_controllers = false, start_menu = false, start_help = false;
    bool start_graphics = false;
    std::string quality;
    bool map_open = false, start_region_map = false, tuning_open = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--city" && i + 1 < argc) city_path = argv[++i];
        if (arg == "--editor") preview_editor = true;
        if (arg == "--building-builder") preview_building = true;
        if (arg == "--car-editor") preview_car = true;
        if (arg == "--character-creator") preview_character = true;
        if (arg == "--cities") cities_app=true;
        if (arg == "--settings") settings_app=true;
        if (arg == "--new") create_new=true;
        if (arg == "--session" && i+1<argc) session=argv[++i];
        if (arg == "--design" && i+1<argc) design_path=argv[++i];
        if (arg == "--overview") { map_open = true; start_region_map = true; }
        if (arg == "--map") map_open = true;
        if (arg == "--tuning") tuning_open = true;
        if (arg == "--performance") { tuning_open = true; performance_tuning = true; }
        if (arg == "--controllers") start_controllers = true;
        if (arg == "--graphics") start_graphics = true;
        if (arg == "--quality" && i + 1 < argc) quality = argv[++i];
        if (arg == "--menu") start_menu = true;
        if (arg == "--help-menu") start_help = true;
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        if (arg == "--time") {
            if (i + 1 >= argc || !day_night.set_time(argv[++i])) {
                TraceLog(LOG_ERROR, "Time must use HH:MM (00:00 through 23:59)");
                return 1;
            }
            time_override = argv[i];
        }
    }
    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN;
    if (!screenshot.empty()) window_flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(window_flags);
    InitWindow(1280, 720, "Ambaretto - Desktop");
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
        const ambaretto::ui::FontResource ui_font;
        const auto cities_directory = std::filesystem::path(GetApplicationDirectory()) / "cities";
        ambaretto::set_app_session(session);
        if (preview_building || preview_car || preview_character)
            ambaretto::design_app(preview_building ? ambaretto::DesktopApp::Buildings : preview_car ? ambaretto::DesktopApp::Cars : ambaretto::DesktopApp::Characters,
                cities_directory.parent_path(),screenshot,create_new,design_path);
        ambaretto::ControllerMapping controls;
        const auto mapping_path = std::filesystem::path(GetApplicationDirectory()) / "controller-mappings.ini";
        std::string mapping_error;
        if (std::filesystem::exists(mapping_path)) controls.load(mapping_path, mapping_error);
        else if (screenshot.empty()) controls.save(mapping_path, mapping_error);
        ambaretto::GraphicsSettings graphics;
        const auto graphics_path = std::filesystem::path(GetApplicationDirectory()) / "graphics-settings.ini";
        std::string graphics_status;
        if (std::filesystem::exists(graphics_path)) graphics.load(graphics_path, graphics_status);
        if (!quality.empty()) {
            if (quality == "low") graphics.apply_preset(ambaretto::GraphicsPreset::Low);
            else if (quality == "balanced") graphics.apply_preset(ambaretto::GraphicsPreset::Balanced);
            else if (quality == "high") graphics.apply_preset(ambaretto::GraphicsPreset::High);
            else { TraceLog(LOG_ERROR, "Quality must be low, balanced, or high"); return 1; }
        }
        if (graphics.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(graphics.fps_limit);
        if (settings_app) ambaretto::settings_app(controls,graphics);
        ambaretto::City selected_city;
        bool direct_city = !city_path.empty();
        if (direct_city) {
            std::string error;
            if (!ambaretto::City::load(city_path,selected_city,error)) { TraceLog(LOG_ERROR,"%s",error.c_str()); direct_city = false; }
            else if (!selected_city.playable()) preview_editor = true;
        }
        while (!preview_building && !preview_car && !preview_character && !settings_app && !ambaretto::app_should_close()) {
        if (cities_app || preview_editor || start_graphics || start_controllers) {
            SetWindowTitle("Ambaretto - Cities");
            const auto settings = start_graphics ? ambaretto::MenuCommand::Graphics : start_controllers ? ambaretto::MenuCommand::Controllers : ambaretto::MenuCommand::None;
            if (!ambaretto::city_menu(selected_city,cities_directory,controls,graphics,preview_editor && direct_city,
                    screenshot,preview_editor,settings)) {
                if (!ambaretto::close_apps()) {ambaretto::cancel_app_close(); continue;}
                break;
            }
            if (!ambaretto::close_apps()) continue;
            if (!session.empty()) {
                std::string error;
                if (selected_city.save(cities_directory,error) && ambaretto::request_play(selected_city,error)) break;
                TraceLog(LOG_ERROR,"%s",error.c_str()); continue;
            }
            start_graphics = start_controllers = false;
        } else if (!direct_city) {
            if (!ambaretto::desktop_menu(selected_city,cities_directory,screenshot)) break;
            // Independent settings windows persist to disk; read their saved state before Play.
            if (std::filesystem::exists(mapping_path)) controls.load(mapping_path,mapping_error);
            if (std::filesystem::exists(graphics_path)) graphics.load(graphics_path,graphics_status);
            if (graphics.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
            SetTargetFPS(graphics.fps_limit);
        }
        direct_city = false; preview_editor = false; cities_app=false;
        SetWindowTitle(("Ambaretto - "+selected_city.name).c_str());
        day_night = ambaretto::DayNight();
        if (!time_override.empty()) day_night.set_time(time_override);
        else day_night.advance(selected_city.start_minutes-8*60+1440);
        captured = screenshot.empty() && !map_open && !start_menu && !start_controllers && !start_help && !start_graphics;
        if (captured) DisableCursor(); else EnableCursor();
        ambaretto::VehicleAudio vehicle_audio;
        ambaretto::CarRenderer car_renderer;
        ambaretto::CharacterRenderer character_renderer;
        std::string character_error;
        auto characters=ambaretto::saved_characters(cities_directory.parent_path()/"characters",character_error);
        selected_city.update_player_character(characters);
        if (!character_renderer.available(selected_city,character_error)) {
            TraceLog(LOG_WARNING,"%s",character_error.c_str());
            if (!screenshot.empty()) break;
            direct_city=true; preview_editor=true; continue;
        }
        characters.erase(std::remove_if(characters.begin(),characters.end(),[&](const auto& design){return !character_renderer.available(design,character_error);}),characters.end());
        std::string car_error, car_update_error;
        auto cars = ambaretto::saved_cars(cities_directory.parent_path()/"cars",car_error);
        selected_city.update_car_designs(cars,car_update_error);
        const ambaretto::Environment environment(selected_city);
        ambaretto::EnvironmentRenderer scenery(environment);
        ambaretto::TuningPanel tuning_panel;
        ambaretto::SceneLighting lighting;
        using ambaretto::Action;
        using ambaretto::MenuCommand;
        ambaretto::MenuBar menu;
        if (start_help) menu.show(MenuCommand::Controls);
        else if (start_menu) menu.open();
        if (performance_tuning) tuning_panel.select_tab(2);
        // Cities embed their car settings so shared maps retain their designs.
        for (const auto& vehicle : selected_city.vehicles) if (vehicle.car && std::none_of(cars.begin(),cars.end(),[&](const auto& d) {
            return d.name==vehicle.car->name && d.body==vehicle.car->body && d.wheel==vehicle.car->wheel;
        })) cars.push_back(*vehicle.car);
        cars.erase(std::remove_if(cars.begin(),cars.end(),[&](const auto& design){return !car_renderer.available(design,car_error);}),cars.end());
        auto scene = std::make_unique<Scene>(environment,std::move(cars),std::move(characters));
        ambaretto::ThirdPersonCamera orbit;
        ambaretto::AimAssist aim_assist;
        ambaretto::ThirdPersonCamera saved_orbit = orbit;
        Camera3D camera{{0, 4, 9}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        bool weapon_wheel = false, aiming = false;
        auto wheel_choice = scene->player.weapons().selected();
        Vector2 wheel_cursor{};
        const auto focus = [&]() {
            if (!tuning_open && scene->player.on_foot() && scene->player.character().covering())
                return orbit.shoulder_focus(scene->player.character().body_parts()[int(ambaretto::BodyPart::Head)].position,
                    aiming ? .4f : 0, scene->player.character().cover_aim_side());
            if (scene->player.can_shoot() && scene->player.weapons().selected() != ambaretto::WeaponType::Unarmed && !tuning_open)
                return orbit.shoulder_focus(scene->player.position() + ambaretto::Vec3(0, aiming ? 1.48f : 1.45f, 0),
                    aiming ? .62f : .6f);
            if (!tuning_open) return scene->player.position() + ambaretto::Vec3(0, scene->player.flying() ? .5f : scene->player.driving() ? .7f :
                scene->player.character().swimming() ? 1.55f : 1.25f, 0);
            const auto center = scene->player.car().position() + ambaretto::Vec3(0, .7f, 0);
            const float distance = (orbit.desired_position(center, true) - center).Length();
            // Shift the camera's aim to frame the car in the area beside the
            // 400-pixel panel. Scale with zoom and viewport height (60-deg FOV).
            const auto right = orbit.forward().Cross(ambaretto::Vec3::sAxisY());
            return center + right * (distance * 400.0f / GetScreenHeight() * .57735027f);
        };
        const auto snap_camera = [&]() {
            aim_assist.reset();
            const auto heading = tuning_open ? scene->player.car().forward() : scene->player.forward();
            if (tuning_open) orbit = ambaretto::ThirdPersonCamera{};
            orbit.reset(std::atan2(-heading.GetX(), -heading.GetZ()));
            if (tuning_open) orbit.look(-230, 25, 5, true, heading, 0, 0);
            camera.target = render_vector(focus());
            camera.position = render_vector(orbit.desired_position(focus(), tuning_open || scene->player.driving(), !tuning_open && scene->player.flying(), scene->player.plane().camera_scale()));
        };
        snap_camera();
        ambaretto::WorldMapView world_map;
        float min_x = ambaretto::Environment::extent, min_z = min_x, max_x = -min_x, max_z = -min_x;
        for (const auto& island : environment.land_islands()) {
            min_x = std::min(min_x, island.center.GetX() - island.radius_x);
            min_z = std::min(min_z, island.center.GetZ() - island.radius_z);
            max_x = std::max(max_x, island.center.GetX() + island.radius_x);
            max_z = std::max(max_z, island.center.GetZ() + island.radius_z);
        }
        const Rectangle map_region{min_x - 160, min_z - 160, max_x - min_x + 320, max_z - min_z + 320};
        const Rectangle initial_map_viewport = ambaretto::WorldMapLayout(GetScreenWidth(),GetScreenHeight()).viewport;
        if (start_region_map) world_map.fit(initial_map_viewport, map_region, ambaretto::Environment::extent);
        else world_map.focus(scene->player.position(), initial_map_viewport, ambaretto::Environment::extent);
        double accumulator = 0;
        float notice_time = mapping_error.empty() ? 0 : 8;
        std::string notice = mapping_error;
        if (scene->designs.empty()) { notice="No cars available. Create a car in the map editor's Objects menu."; notice_time=10; }
        if (!car_update_error.empty()) { notice="Map car update rejected: "+car_update_error; notice_time=10; }
        if (!graphics_status.empty()) { notice = "Graphics defaults in use: " + graphics_status; notice_time = 8; TraceLog(LOG_WARNING, "%s", notice.c_str()); }
        bool jump_pending = false, discard_mouse = true, orbit_dragging = false, map_dragging = false, flaps = false;
        bool fire_pending = false, suppress_fire = true;
        float shot_flash = 0, hit_marker = 0, death_time = 0, arrest_time = 0, recoil_return = 0, hurt_flash = 0, kill_flash = 0;
        ambaretto::Vec3 death_position = scene->player.position();
        float previous_health = scene->player.character().health();
        ambaretto::Shot last_shot;
        int rendered_frames = 0;
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
        bool cities_requested = false;
        while (!cities_requested && !WindowShouldClose()) {
            const float elapsed = GetFrameTime(), frame = std::min(elapsed, 0.1f);
            shot_flash = std::max(0.f, shot_flash - frame);
            hit_marker = std::max(0.f, hit_marker - frame);
            kill_flash = std::max(0.f, kill_flash - frame);
            const ambaretto::WorldMapLayout map_layout(GetScreenWidth(),GetScreenHeight());
            const auto map_viewport = map_layout.viewport;
            const auto& map_buttons = map_layout.buttons;
            if (map_open) world_map.constrain(map_viewport, ambaretto::Environment::extent);
            bool mode_changed = false;
            const auto toggle_map = [&]() {
                map_open = !map_open;
                if (map_open) world_map.focus(scene->player.position(), map_viewport, ambaretto::Environment::extent);
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
                const auto controller_input = ambaretto::read_controllers();
                controls.update(controller_input);
                if (!IsWindowFocused()) {
                    if (captured) EnableCursor();
                    captured = false; resume_capture = false;
                    tuning_panel.cancel_drag(); orbit_dragging = false; map_dragging = false; discard_mouse = true;
                }
                const auto command = menu.update(controls, mapping_path, !captured, controller_input);
                if (menu.blocking() || menu.interacted()) {
                    mode_changed = true; jump_pending = false; discard_mouse = true;
                    tuning_panel.cancel_drag(); orbit_dragging = false; map_dragging = false;
                    if (captured) { captured = false; EnableCursor(); }
                }
                if (command == MenuCommand::Cities) { cities_requested = true; EnableCursor(); break; }
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
                    if (tuning_open) {toggle_tuning(); if (!map_open) toggle_map();}
                    else toggle_map();
                }
                if (command == MenuCommand::Map && tuning_open) { toggle_tuning(); map_open = false; }
                if (command == MenuCommand::Recover) {
                    scene->reset(); snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (command == MenuCommand::CarDefaults) scene->player.car().set_tuning({});
                const bool shortcuts = IsWindowFocused() && !menu.blocking() && !menu.interacted() && !mode_changed;
                if (command == MenuCommand::Tuning || (shortcuts && scene->player.driving() && controls.pressed(Action::Tuning))) {
                    if (scene->player.flying()) { notice = "Land and exit the plane before tuning the car"; notice_time = 3; }
                    else toggle_tuning();
                }
                else if (shortcuts && controls.pressed(Action::Pause)) {
                    if (tuning_open) {toggle_tuning(); if (!map_open) toggle_map();}
                    else toggle_map();
                }
                if (!tuning_open && (command == MenuCommand::Map || (shortcuts && controls.pressed(Action::Map) && !mode_changed))) {
                    toggle_map();
                }
                if (map_open && !mode_changed && !menu.blocking() && IsWindowFocused()) {
                    const auto mouse = GetMousePosition();
                    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                    const bool inside = CheckCollisionPointRec(mouse, map_viewport);
                    if (click) for (int i = 0; i < int(map_buttons.size()); ++i) if (CheckCollisionPointRec(mouse, map_buttons[i])) {
                        if (i == 0) world_map.focus(scene->player.position(), map_viewport, ambaretto::Environment::extent);
                        else if (i == 1) world_map.fit(map_viewport, map_region, ambaretto::Environment::extent);
                        else if (i == 4) toggle_map();
                        else world_map.zoom(i == 2 ? -1 : 1, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, ambaretto::Environment::extent);
                    }
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    if (IsKeyPressed(KEY_HOME)) world_map.fit(map_viewport, map_region, ambaretto::Environment::extent);
                    if (IsKeyPressed(KEY_C)) world_map.focus(scene->player.position(), map_viewport, ambaretto::Environment::extent);
                    const auto stick = look_stick();
                    const float horizontal = std::max(float(IsKeyDown(KEY_RIGHT)), std::max(mode_value(Action::FootRight, Action::Right, Action::BankRight), std::max(0.f, stick.x)))
                        - std::max(float(IsKeyDown(KEY_LEFT)), std::max(mode_value(Action::FootLeft, Action::Left, Action::BankLeft), std::max(0.f, -stick.x)));
                    const float vertical = std::max(float(IsKeyDown(KEY_DOWN)), std::max(mode_value(Action::FootBackward, Action::Backward, Action::PitchUp), std::max(0.f, stick.y)))
                        - std::max(float(IsKeyDown(KEY_UP)), std::max(mode_value(Action::FootForward, Action::Forward, Action::PitchDown), std::max(0.f, -stick.y)));
                    world_map.pan({-horizontal * 400 * frame, -vertical * 400 * frame}, ambaretto::Environment::extent);
                    const float zoom = float(pressed(KEY_EQUAL) || pressed(KEY_KP_ADD)) - float(pressed(KEY_MINUS) || pressed(KEY_KP_SUBTRACT))
                        + zoom_amount() * 4 * frame;
                    if (zoom != 0) world_map.zoom(zoom, {map_viewport.x + map_viewport.width / 2, map_viewport.y + map_viewport.height / 2}, map_viewport, ambaretto::Environment::extent);
                    if (inside) world_map.zoom(GetMouseWheelMove(), mouse, map_viewport, ambaretto::Environment::extent);
                    if (click && inside) map_dragging = true;
                    else if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map_dragging = false;
                    if (map_dragging && !click) world_map.pan(GetMouseDelta(), ambaretto::Environment::extent);
                    SetMouseCursor(map_dragging ? MOUSE_CURSOR_RESIZE_ALL : inside ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
                }
                if (tuning_open && !mode_changed && !menu.blocking()) {
                    const auto mouse = GetMousePosition();
                    const bool fine = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    const auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
                    const ambaretto::TuningPanelInput input{mouse, IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
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
                if (!captured && !map_open && !tuning_open && !menu.blocking() && !mode_changed && IsWindowFocused() &&
                    GetMousePosition().y >= ambaretto::menu_height && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    captured = true; DisableCursor(); discard_mouse = true;
                }
            }
            const bool active = !map_open && !tuning_open && !menu.blocking() && !mode_changed &&
                (!screenshot.empty() || (captured && IsWindowFocused()));
            const bool simulate = !map_open && (!screenshot.empty() || IsWindowFocused());
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
                && scene->player.weapons().selected() != ambaretto::WeaponType::Unarmed && controls.value(Action::Aim) > .5f;
            if (!aiming || !controls.auto_lock) aim_assist.reset();
            day_night.advance(elapsed, screenshot.empty() && simulate);
            if (active && screenshot.empty()) {
                const bool armed = scene->player.on_foot() && scene->player.weapons().selected() != ambaretto::WeaponType::Unarmed;
                const bool reload_pressed = controls.pressed(Action::Reload) && scene->player.can_shoot() && armed && !weapon_wheel;
                if (reload_pressed) scene->player.weapons().reload();
                if (controls.pressed(mode_action(Action::Respawn, Action::Recover, Action::PlaneRecover)) && !reload_pressed && !weapon_wheel) {
                    if (scene->player.on_foot()) scene->respawn_on_foot(); else scene->reset();
                    snap_camera(); accumulator = 0; jump_pending = false; flaps = false;
                }
                if (controls.pressed(mode_action(Action::EnterVehicle, Action::ExitVehicle, Action::PlaneExit)) && !weapon_wheel) {
                    const bool stealing = scene->player.can_steal();
                    const auto result = scene->player.interact();
                    if (result == ambaretto::Interaction::Entered || result == ambaretto::Interaction::Exited) {
                        jump_pending = false;
                        snap_camera();
                        notice = result == ambaretto::Interaction::Entered ? (scene->player.flying() ? "Entered plane - increase throttle to take off" :
                            stealing ? "Pulled driver out - stole traffic car" : "Entered car") : scene->player.character().ragdolling()
                            ? "Jumped out - brace for impact" : "On foot";
                    } else if (result == ambaretto::Interaction::Blocked) notice = "Exit blocked - move the vehicle to an open space";
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
                    wheel_choice = ambaretto::wheel_selection(wheel_cursor.x, wheel_cursor.y, wheel_choice);
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
                    std::vector<ambaretto::Character*> candidates;
                    for (const auto& person : scene->pedestrians.people()) if (person.enabled) candidates.push_back(person.character.get());
                    for (const auto& unit : scene->police.units()) if (unit.active)
                        for (const auto& officer : unit.officers) if (!officer.seated) candidates.push_back(officer.character.get());
                    const ambaretto::Vec3 origin(camera.position.x, camera.position.y, camera.position.z);
                    aim_assist.update(scene->world, candidates, &scene->player.character(), origin, orbit.aim_direction(),
                        scene->player.weapons().data().range, mouse.x, mouse.y, frame);
                    if (const auto point = aim_assist.point()) { orbit.aim_at(origin, *point); orbit.recoil(recoil_return); }
                }
                if (aiming) {
                    const auto direction = orbit.aim_direction();
                    camera.target = {camera.position.x + direction.GetX(), camera.position.y + direction.GetY(), camera.position.z + direction.GetZ()};
                }
            }
            ambaretto::Input driving;
            ambaretto::FootInput walking;
            ambaretto::FlightInput flight;
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
                    && scene->player.weapons().selected() != ambaretto::WeaponType::Unarmed && !weapon_wheel) {
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
            while (accumulator >= double(ambaretto::fixed_step)) {
                walking.jump = jump_pending;
                scene->police.set_view({camera.position.x, camera.position.y, camera.position.z},
                    {camera.target.x - camera.position.x, camera.target.y - camera.position.y, camera.target.z - camera.position.z});
                scene->player.step(driving, walking, ambaretto::fixed_step, flight);
                if (active && !weapon_wheel && !suppress_fire && screenshot.empty() && scene->player.can_shoot()
                    && scene->player.weapons().selected() != ambaretto::WeaponType::Unarmed && controls.value(Action::Fire) > .5f) {
                    const auto ray = GetScreenToWorldRay({GetScreenWidth() * .5f, GetScreenHeight() * .5f}, camera);
                    const auto aim = ambaretto::trace_shot(scene->world, &scene->pedestrians,
                        {ray.position.x, ray.position.y, ray.position.z}, {ray.direction.x, ray.direction.y, ray.direction.z}, scene->player.weapons().data().range, &scene->police, &scene->player.character());
                    const auto gun = scene->player.character().held_weapon();
                    // Cast from the receiver so a barrel protruding into cover cannot bypass it.
                    const auto muzzle = gun.position + gun.rotation * ambaretto::Vec3(0, .06f, 0);
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
                    : std::abs(position.GetX()) > ambaretto::Environment::extent - 40 || std::abs(position.GetZ()) > ambaretto::Environment::extent - 40;
                if (recover) {
                    scene->reset(); snap_camera(); flaps = false;
                    notice = scene->player.flying() ? "Recovered aircraft at the airport" : "Recovered from world boundary"; notice_time = 3;
                }
                if (!scene->player.flying() && !scene->player.plane().destroyed() && scene->player.plane().position().GetY() < -.6f) scene->player.recover_plane();
                if (scene->player.on_foot() && !scene->player.character().alive()) {
                    if (death_time == 0) death_position = scene->player.position();
                    death_time += ambaretto::fixed_step;
                    if (death_time > 4) { scene->respawn_on_foot(death_position); snap_camera(); death_time = 0; suppress_fire = true; }
                } else death_time = 0;
                if (scene->police.arrested()) {
                    arrest_time += ambaretto::fixed_step;
                    if (arrest_time > 3) { scene->respawn_on_foot(); snap_camera(); arrest_time = 0; suppress_fire = true; }
                } else arrest_time = 0;
                accumulator -= double(ambaretto::fixed_step);
            }
            if (scene->world.take_player_kill()) kill_flash = .18f;
            const auto target = focus();
            const auto desired = orbit.desired_position(target, tuning_open || scene->player.driving(), !tuning_open && scene->player.flying(),
                scene->player.plane().camera_scale(), aiming && scene->player.can_shoot());
            const float follow = scene->player.driving() && !tuning_open ? 1.f : 1 - std::exp(-12 * frame);
            const bool aim_view = aiming && scene->player.can_shoot();
            camera.target = aim_view ? render_vector(target) : lerp(camera.target, render_vector(target), follow);
            auto smoothed = ambaretto::Vec3(camera.position.x, camera.position.y, camera.position.z);
            smoothed += (desired - smoothed) * follow;
            const auto camera_target = ambaretto::Vec3(camera.target.x, camera.target.y, camera.target.z);
            const auto offset = smoothed - camera_target;
            const float fraction = scene->world.camera_fraction(camera_target, offset,
                (tuning_open || scene->player.driving()) ? scene->player.car().body_id() : scene->player.flying() ? scene->player.plane().body_id() : JPH::BodyID());
            camera.position = render_vector(ambaretto::ThirdPersonCamera::above_water(camera_target + offset * fraction));
            if (aim_view) {
                if (const auto point = aim_assist.point()) {
                    orbit.aim_at({camera.position.x, camera.position.y, camera.position.z}, *point);
                    orbit.recoil(recoil_return);
                }
                const auto direction = orbit.aim_direction();
                camera.target = {camera.position.x + direction.GetX(), camera.position.y + direction.GetY(), camera.position.z + direction.GetZ()};
            }
            vehicle_audio.update(scene->traffic, scene->player.car(), scene->player.position(),
                orbit.forward().Cross(ambaretto::Vec3::sAxisY()), scene->player.driving(),
                active && scene->player.driving() && controls.value(Action::Horn) > .5f, simulate && screenshot.empty(), frame, driving.throttle);
            vehicle_audio.update_police(scene->police, scene->player.position(), orbit.forward().Cross(ambaretto::Vec3::sAxisY()), simulate && screenshot.empty());
            vehicle_audio.update_effects(scene->world, scene->player.position(), orbit.forward().Cross(ambaretto::Vec3::sAxisY()), simulate && screenshot.empty());
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
                std::array<ambaretto::SceneLight, ambaretto::SceneLighting::max_lights> nearby_lights{};
                int light_count = 0;
                if (graphics.local_lights && daylight.night > .01f) {
                    const auto headlights = [&](const ambaretto::Car& car) {
                        if (!car.simulated() || !car.design()) return;
                        if (car.destroyed() || light_count + 2 > ambaretto::SceneLighting::max_lights) return;
                        for (float side : {-1.0f, 1.0f}) nearby_lights[light_count++] = {
                            render_vector(car.position() + car.rotate(car.body_offset()+ambaretto::Vec3(side * car.body_size().GetX()*.35f,
                                0,-car.body_size().GetZ()/2))),
                            {2.6f * daylight.night, 2.3f * daylight.night, 1.8f * daylight.night}, 48,
                            render_vector((car.forward() + ambaretto::Vec3(0, -.13f, 0)).Normalized()), .87f};
                    };
                    headlights(scene->player.car());
                    for (const auto& unit : scene->police.units()) if (unit.active && light_count < 6
                        && (unit.car->position() - scene->player.position()).LengthSq() < 60 * 60) headlights(*unit.car);
                    for (const auto& vehicle : scene->traffic.cars())
                        if (light_count < 6 && vehicle.car.get() != &scene->player.car() && vehicle.car->simulated() &&
                            (vehicle.car->position() - scene->player.position()).LengthSq() < 45 * 45) headlights(*vehicle.car);
                    const int first_lamp = light_count;
                    std::array<float, ambaretto::SceneLighting::max_lights> distances{};
                    distances.fill(1e30f);
                    const auto p = scene->player.position();
                    for (const auto& lamp : scenery.lights()) {
                        const float distance = (ambaretto::Vec3(lamp.position.x, lamp.position.y, lamp.position.z) - p).LengthSq();
                        if (distance > (lamp.radius + 50) * (lamp.radius + 50)) continue;
                        for (int i = first_lamp; i < ambaretto::SceneLighting::max_lights; ++i) if (distance < distances[i]) {
                            for (int j = ambaretto::SceneLighting::max_lights - 1; j > i; --j) { distances[j] = distances[j - 1]; nearby_lights[j] = nearby_lights[j - 1]; }
                            distances[i] = distance; nearby_lights[i] = lamp;
                            nearby_lights[i].color.x *= daylight.night; nearby_lights[i].color.y *= daylight.night; nearby_lights[i].color.z *= daylight.night;
                            light_count = std::min(light_count + 1, ambaretto::SceneLighting::max_lights); break;
                        }
                    }
                }
                lighting.set_lights(nearby_lights.data(), light_count);
                const auto shadow_focus = render_vector(scene->player.position());
                if (lighting.begin_shadow(shadow_focus, daylight, graphics)) {
                    const auto shader = lighting.shadow_shader();
                    // Include tall, sunward casters throughout the light's depth volume.
                    scenery.draw_shadow(shader, shadow_focus, graphics.shadow_distance * 3 + 250);
                    if (scene->car.simulated()) draw_car(scene->car, car_renderer, view, shader);
                    for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                        const auto& vehicle = scene->traffic.cars()[i];
                        if (vehicle.car->simulated() && (vehicle.car->position() - scene->player.position()).LengthSq() <
                            (graphics.shadow_distance + 15) * (graphics.shadow_distance + 15))
                            draw_car(*vehicle.car, car_renderer, view, shader);
                    }
                    for (const auto& unit : scene->police.units()) if (unit.active
                        && (unit.car->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_car(*unit.car, car_renderer, view, shader);
                    if ((scene->plane.position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_plane(scene->plane, scene->player.flying() && &scene->player.plane() == &scene->plane);
                    for (const auto& other : scene->aircraft) if ((other->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        draw_plane(*other, scene->player.flying() && &scene->player.plane() == other.get());
                    if (scene->player.on_foot()) character_renderer.draw(scene->player.character(),view,shader);
                    for (const auto& pedestrian : scene->pedestrians.people()) if (pedestrian.enabled
                        && (pedestrian.character->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                        character_renderer.draw(*pedestrian.character,view,shader);
                    for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                        const auto& driver = scene->traffic.cars()[i].driver;
                        if (driver && (driver->position() - scene->player.position()).LengthSq() < graphics.shadow_distance * graphics.shadow_distance)
                            character_renderer.draw(*driver,view,shader);
                    }
                    for (const auto& unit:scene->police.units()) if (unit.active)
                        for (const auto& officer:unit.officers) if (!officer.seated
                            && (officer.character->position()-scene->player.position()).LengthSq()<graphics.shadow_distance*graphics.shadow_distance)
                            character_renderer.draw(*officer.character,view,shader);
                    lighting.end_shadow();
                }
            }
            if (map_open) {
                ambaretto::ui::draw_desktop();
                ambaretto::ui::draw_window(map_layout.window,(selected_city.name+" / PAUSED").c_str());
                ambaretto::ui::draw_window_close(map_layout.window);
                const char* labels[] = {"Player", "Region", "-", "+"};
                for (int i = 0; i < 4; ++i) {
                    const auto button = map_buttons[i];
                    const bool hover = CheckCollisionPointRec(GetMousePosition(), button);
                    DrawRectangleRec(button, hover ? Color{0, 112, 112, 255} : Color{170, 170, 170, 255});
                    DrawRectangleLinesEx(button, 1, RAYWHITE);
                    ambaretto::ui::draw_text(labels[i], int(button.x + (button.width - ambaretto::ui::measure_text(labels[i], 16)) / 2),
                        int(button.y + 7), 16, hover ? RAYWHITE : Color{0, 0, 0, 255});
                }
                scenery.world_map(world_map, map_viewport, scene->player.position(), scene->player.forward(), &scene->police);
                ambaretto::ui::draw_text("Esc: resume / File: Resume or End Game",int(map_layout.window.x+336),int(map_layout.window.y+53),16,ambaretto::ui::dos_white);
            } else {
                const float nearby_distance = std::min(160.f, graphics.view_distance);
                scenery.draw_sky(view, daylight, float(GetTime()), graphics);
                car_renderer.set_lighting(lighting, view, daylight, graphics);
                character_renderer.set_lighting(lighting, view, daylight, graphics);
                BeginMode3D(view);
                scenery.draw(view, float(GetTime()), daylight, lighting, graphics);
                BeginShaderMode(scenery.object_shader());
                draw_skid_marks(scene->marks);
                if (scene->car.simulated()) draw_car(scene->car, car_renderer, view);
                for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                    const auto& vehicle = scene->traffic.cars()[i];
                    if (!vehicle.car->simulated()) continue;
                    const auto delta = vehicle.car->position() - ambaretto::Vec3(view.position.x, view.position.y, view.position.z);
                    if (delta.GetX() * delta.GetX() + delta.GetZ() * delta.GetZ() > graphics.view_distance * graphics.view_distance) continue;
                    draw_car(*vehicle.car, car_renderer, view);
                }
                if (scene->plane.simulated()) draw_plane(scene->plane, scene->player.flying() && &scene->player.plane() == &scene->plane);
                for (const auto& other : scene->aircraft) if ((other->position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance)
                    draw_plane(*other, scene->player.flying() && &scene->player.plane() == other.get());
                if (scene->player.on_foot()) {
                    character_renderer.draw(scene->player.character(),view);
                    draw_weapon(scene->player.character(), scene->player.weapons().selected(), shot_flash);
                    if (shot_flash > 0) DrawLine3D(render_vector(last_shot.from), render_vector(last_shot.to), {255, 223, 151, 175});
                }
                for (const auto& pedestrian : scene->pedestrians.people()) if (pedestrian.enabled
                    && (pedestrian.character->position() - scene->player.position()).LengthSq() < nearby_distance * nearby_distance)
                    character_renderer.draw(*pedestrian.character,view);
                for (std::size_t i = 0; i < scene->traffic.cars().size(); ++i) {
                    const auto& driver = scene->traffic.cars()[i].driver;
                    if (driver && (driver->position() - scene->player.position()).LengthSq() < nearby_distance * nearby_distance)
                        character_renderer.draw(*driver,view);
                }
                for (const auto& unit : scene->police.units()) if (unit.active && (unit.car->position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance) {
                    draw_car(*unit.car, car_renderer, view);
                    for (const auto& officer : unit.officers) if (!officer.seated) {
                        character_renderer.draw(*officer.character,view);
                        if (scene->police.wanted().stars()) draw_weapon(*officer.character, officer.weapon, officer.flash);
                    }
                }
                for (const auto& pickup : scene->police.pickups()) if ((pickup.position - scene->player.position()).LengthSq() < nearby_distance * nearby_distance) {
                    DrawCylinder(render_vector(pickup.position - ambaretto::Vec3(0, .07f, 0)), .4f, .4f, .02f, 16, {238, 198, 66, 180});
                    draw_weapon_model({pickup.position + ambaretto::Vec3(0, .08f, 0), ambaretto::Quat::sRotation(ambaretto::Vec3::sAxisZ(), 1.57079633f)}, pickup.weapon);
                }
                EndShaderMode();
                draw_vehicle_damage(scene->car, car_renderer, view, nearby_distance);
                for (const auto& vehicle : scene->traffic.cars()) if (vehicle.car->simulated())
                    draw_vehicle_damage(*vehicle.car, car_renderer, view, nearby_distance);
                for (const auto& unit : scene->police.units()) if (unit.active)
                    draw_vehicle_damage(*unit.car, car_renderer, view, nearby_distance);
                const auto draw_plane_damage = [&](const ambaretto::Plane& plane) {
                    if ((plane.position() - scene->player.position()).LengthSq() < graphics.view_distance * graphics.view_distance)
                        draw_vehicle_damage(plane, car_renderer, view, graphics.view_distance, plane.explosion_radius() / 10, {0, plane.specs().body_radius, 0});
                };
                draw_plane_damage(scene->plane);
                for (const auto& plane : scene->aircraft) draw_plane_damage(*plane);
                EndMode3D();
                if (kill_flash > 0) DrawRectangle(0, ambaretto::menu_height, GetScreenWidth(), GetScreenHeight() - ambaretto::menu_height,
                    {150, 150, 150, static_cast<unsigned char>(110 * kill_flash / .18f)});
                draw_resume_prompt(captured || tuning_open || !screenshot.empty());
                if (!tuning_open) scenery.minimap(scene->player.position(), scene->player.forward(), view, &scene->police, !scene->player.on_foot());
                const auto region = scene->player.position();
                if (!tuning_open) {
                    const auto bounds = ambaretto::MinimapView(GetScreenHeight(), region, scene->player.forward(), view).bounds;
                    const int info_x = int(bounds.x + bounds.width) + 20, info_y = GetScreenHeight() - 42;
                    const auto draw_info = [&](const char* text, int y) {
                        ambaretto::ui::draw_text(text, info_x + 1, y + 1, 20, BLACK);
                        ambaretto::ui::draw_text(text, info_x, y, 20, RAYWHITE);
                    };
                    if (!scene->player.on_foot()) {
                        const auto velocity = scene->player.flying() ? scene->player.plane().velocity() : scene->player.car().velocity();
                        draw_info(TextFormat("%.0f km/h", velocity.Length() * 3.6f), info_y);
                    }
                    const float health = scene->player.character().health();
                    DrawRectangle(26, ambaretto::menu_height + 18, 184, 16, {26, 20, 25, 235});
                    DrawRectangle(28, ambaretto::menu_height + 20, int(180 * health / 100), 12, {177, 112, 137, 255});
                    if (scene->player.on_foot()) {
                        if (scene->player.character().covering()) draw_info(scene->player.character().cover_peeking() ? "COVER - exposed while aiming"
                            : scene->player.character().cover_stance() == ambaretto::CoverStance::Crawling ? "COVER - crawling | Q / B: leave"
                            : scene->player.character().cover_stance() == ambaretto::CoverStance::Crouched ? "COVER - crouched | Q / B: leave"
                            : "COVER - standing | Q / B: leave", info_y - 68);
                        const auto& weapons = scene->player.weapons();
                        if (scene->player.can_shoot() && weapons.selected() != ambaretto::WeaponType::Unarmed && !weapon_wheel && active) {
                            DrawCircle(GetScreenWidth() / 2, GetScreenHeight() / 2, 3, BLACK);
                            DrawCircle(GetScreenWidth() / 2, GetScreenHeight() / 2, 2, hit_marker > 0 ? RED : RAYWHITE);
                        }
                        if (hurt_flash > 0) DrawRectangle(0, ambaretto::menu_height, GetScreenWidth(), GetScreenHeight() - ambaretto::menu_height,
                            {160, 0, 0, static_cast<unsigned char>(hurt_flash * 160)});
                        if (!scene->player.character().alive()) {
                            DrawRectangle(0, ambaretto::menu_height, GetScreenWidth(), GetScreenHeight() - ambaretto::menu_height, {22, 8, 8, 140});
                            const char* text = "WASTED";
                            ambaretto::ui::draw_text(text, (GetScreenWidth() - ambaretto::ui::measure_text(text, 44)) / 2, GetScreenHeight() / 2 - 22, 44, RED);
                        }
                    }
                    if (scene->police.arrested()) {
                        DrawRectangle(0, ambaretto::menu_height, GetScreenWidth(), GetScreenHeight() - ambaretto::menu_height, {8, 14, 28, 135});
                        const char* text = "BUSTED";
                        ambaretto::ui::draw_text(text, (GetScreenWidth() - ambaretto::ui::measure_text(text, 44)) / 2, GetScreenHeight() / 2 - 22, 44, SKYBLUE);
                    }
                    if (scene->player.flying()) {
                        draw_info(TextFormat("ALT %.0f m", std::max(0.f, float(region.GetY()) - ambaretto::Environment::water_level)), info_y - 28);
                    }
                    const char* location = selected_city.name.c_str();
                    const int width = ambaretto::ui::measure_text(location, 20), x = GetScreenWidth() - width - 26;
                    DrawRectangle(x - 12, GetScreenHeight() - 50, width + 24, 36, {19, 28, 45, 230});
                    ambaretto::ui::draw_text(location, x, GetScreenHeight() - 42, 20, RAYWHITE);
                }
                if (notice_time > 0) ambaretto::ui::draw_text(notice.c_str(), 26, scene->player.flying() ? 190 : 161, 20, RAYWHITE);
                if (tuning_open) tuning_panel.draw(scene->player.car(), GetScreenWidth(), GetScreenHeight());
            }
            if (!map_open) {
                const int clock_x = tuning_open ? 14 : GetScreenWidth() - 194, clock_y = tuning_open ? 198 : 46;
                DrawRectangle(clock_x, clock_y, 180, 40, {19, 28, 45, 230});
                const auto clock = day_night.clock();
                ambaretto::ui::draw_text(clock.data(), clock_x + (180 - ambaretto::ui::measure_text(clock.data(), 24)) / 2,
                    clock_y + 8, 24, {174, 231, 246, 255});
                if (!tuning_open) {
                    draw_wanted(scene->police.wanted(), clock_x, clock_y + 48);
                    if (scene->player.on_foot()) draw_ammo(scene->player.weapons(), clock_x,
                        clock_y + 48 + (scene->police.wanted().stars() ? 42 : 0));
                }
            }
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
        if (!screenshot.empty()) break;
        map_open = tuning_open = false; start_menu = start_controllers = start_help = start_graphics = false;
        }
        ambaretto::close_apps();
    }
    EnableCursor();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
