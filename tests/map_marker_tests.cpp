#include "environment_renderer.hpp"
#include "police.hpp"
#include "ui_font.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
int marker_pixels(Image image, Vector2 point, Color color) {
    int count = 0;
    for (int y = int(point.y) - 4; y <= int(point.y) + 4; ++y)
        for (int x = int(point.x) - 4; x <= int(point.x) + 4; ++x)
            count += same(GetImageColor(image, x, y), color);
    return count;
}
bool equal(Image a, Image b) {
    for (int y = 0; y < a.height; ++y) for (int x = 0; x < a.width; ++x)
        if (!same(GetImageColor(a, x, y), GetImageColor(b, x, y))) return false;
    return true;
}
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 480, "Map marker regression check");
    try {
        using namespace forza;
        ui::FontResource font;
        const Environment map;
        PhysicsWorld world(map);
        Car civilian(world);
        Plane plane(world);
        Police police(world, map);
        EnvironmentRenderer scenery(map);
        const Vec3 player(0, 3.3f, 105), heading(0, 0, -1);
        const Vec3 civilian_position = player + Vec3(70, 0, 35), plane_position = player + Vec3(-60, 4, 45);
        civilian.reset(civilian_position); plane.reset(plane_position);
        const Camera3D camera{{0, 6, 114}, {0, 4, 105}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const Rectangle viewport{0, 0, 640, 480};
        WorldMapView view; view.center = {player.GetX(), player.GetZ()}; view.scale = 1;
        const MinimapView mini(480, player, heading, camera);
        auto& unit = const_cast<PoliceUnit&>(police.units()[0]);
        const Vec3 car_position = player + Vec3(50, 0, -40), foot_position = player + Vec3(-55, 0, -45);
        const auto texture = LoadRenderTexture(640, 480);
        for (bool full : {false, true}) {
            police.clear(); unit.car->reset(car_position); unit.car->repair();
            const auto project = [&](Vec3 point) { return full ? view.project(point, viewport) : mini.project(point); };
            const auto render = [&](const Police* cops) {
                BeginTextureMode(texture); ClearBackground(BLACK);
                if (full) scenery.world_map(view, viewport, player, heading, cops);
                else scenery.minimap(player, heading, camera, cops);
                EndTextureMode();
                Image image = LoadImageFromTexture(texture.texture); ImageFlipVertical(&image); return image;
            };
            Image baseline = render(nullptr);
            require(!marker_pixels(baseline, project(civilian_position), SKYBLUE)
                && !marker_pixels(baseline, project(plane_position), YELLOW), "civilian car or aircraft marker remained on a map");
            unit.active = true;
            Image quiet = render(&police);
            require(equal(baseline, quiet), "police marker appeared with no wanted level");
            police.crime(Crime::PoliceVehicleTheft, player, unit.officers[0].character.get());
            require(police.wanted().stars() > 0, "marker test did not become wanted");
            unit.active = false; Image wanted_base = render(&police); unit.active = true;
            Image live = render(&police);
            require(marker_pixels(live, project(car_position), {79, 142, 255, 255}) > 20,
                "live police car had no marker while wanted");
            ExportImage(live, full ? "map-markers-world-wanted.png" : "map-markers-minimap-wanted.png");
            unit.car->take_damage(100);
            Image wreck = render(&police);
            require(equal(wanted_base, wreck), "destroyed police vehicle kept its marker");
            auto& officer = unit.officers[0];
            officer.seated = false; officer.character->reset(foot_position); officer.character->revive();
            Image foot = render(&police);
            require(marker_pixels(foot, project(foot_position), {79, 142, 255, 255}) > 15,
                "living officer lost their marker when the patrol car was destroyed");
            officer.character->take_damage(100);
            Image dead = render(&police);
            require(equal(wanted_base, dead), "dead police officer kept their marker");
            unit.car->repair(); officer.character->reset(foot_position); officer.character->revive();
            const_cast<WantedLevel&>(police.wanted()).step(player + Vec3(2000, 0, 0), false, police.wanted().cooldown() + 1);
            Image escaped = render(&police);
            require(equal(baseline, escaped), "police or search markers remained after wanted level cleared");
            ExportImage(escaped, full ? "map-markers-world-clear.png" : "map-markers-minimap-clear.png");
            for (Image image : {baseline, quiet, wanted_base, live, wreck, foot, dead, escaped}) UnloadImage(image);
        }
        UnloadRenderTexture(texture);
        std::cout << "Both maps: hidden civilian cars/aircraft, wanted-only police, wreck/dead marker removal and escape passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Map marker check failed: " << error.what() << '\n'; CloseWindow(); return 1;
    }
    CloseWindow();
    return 0;
}
