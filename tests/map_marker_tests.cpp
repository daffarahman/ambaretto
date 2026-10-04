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
void sharp_map_edges(const forza::EnvironmentRenderer& scenery, RenderTexture2D texture, bool in_vehicle = false) {
    using namespace forza;
    const Vec3 player(0, 3.3f, 105), heading(0, 0, -1);
    const Rectangle viewport{0, 0, 640, 480};
    // This render target has no MSAA: interior map pixels must retain the palette,
    // even at maximum zoom and while the minimap rotates.
    for (float scale : {.5f, 2.0f, 0.0f, -1.0f}) {
        if (in_vehicle && scale > 0) continue;
        const Camera3D camera{{0, 6, 114}, {scale < 0 ? 9.0f : 0.0f, 4, 105}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const MinimapView mini(480, player, heading, camera, in_vehicle);
        const bool full = scale > 0;
        WorldMapView view; view.center = {player.GetX(), player.GetZ()}; view.scale = scale;
        BeginTextureMode(texture); ClearBackground(BLACK);
        if (full) scenery.world_map(view, viewport, player, heading);
        else scenery.minimap(player, heading, camera, nullptr, in_vehicle);
        EndTextureMode();
        Image image = LoadImageFromTexture(texture.texture); ImageFlipVertical(&image);
        const Rectangle bounds = full ? viewport : mini.bounds;
        const Vector2 anchor = full ? view.project(player, viewport) : mini.anchor;
        if (!full) {
            for (int x : {int(bounds.x + 1), int(bounds.x + bounds.width - 2)})
                for (int y : {int(bounds.y + 1), int(bounds.y + bounds.height - 2)})
                    require(same(GetImageColor(image, x, y), BLACK), "minimap content escaped rounded corners");
            for (int x : {int(bounds.x - 1), int(bounds.x + bounds.width)})
                require(same(GetImageColor(image, x, int(anchor.y)), BLACK), "minimap retained its outer border");
            require(!same(GetImageColor(image, int(anchor.x), int(bounds.y + 1)), BLACK), "minimap lost its top edge");
        }
        int roads = 0, buildings = 0, blurred = 0;
        for (int y = int(bounds.y + 6); y < int(bounds.y + bounds.height - (full ? 64 : 6)); ++y)
            for (int x = int(bounds.x + 6); x < int(bounds.x + bounds.width - 6); ++x) {
                if (y >= anchor.y - 16 && y <= anchor.y + 16 && x >= anchor.x - 16 && x <= anchor.x + 60) continue;
                const Color color = GetImageColor(image, x, y);
                roads += same(color, {205, 205, 205, 255});
                buildings += same(color, {145, 145, 145, 255});
                blurred += color.r == color.g && color.g == color.b && color.r > 96 && color.r < 205 && color.r != 145;
            }
        UnloadImage(image);
        require(roads > 100 && buildings > 100, "map lost road or building geometry at zoom/rotation");
        require(blurred == 0, "map edges still interpolate enlarged texture pixels");
    }
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
        const Vec3 civilian_position = player + Vec3(35, 0, 15), plane_position = player + Vec3(-40, 4, 15);
        civilian.reset(civilian_position); plane.reset(plane_position);
        const Camera3D camera{{0, 6, 114}, {0, 4, 105}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
        const Rectangle viewport{0, 0, 640, 480};
        WorldMapView view; view.center = {player.GetX(), player.GetZ()}; view.scale = 1;
        const MinimapView mini(480, player, heading, camera);
        auto& unit = const_cast<PoliceUnit&>(police.units()[0]);
        const Vec3 car_position = player + Vec3(35, 0, -25), foot_position = player + Vec3(-30, 0, -25);
        const auto texture = LoadRenderTexture(640, 480);
        sharp_map_edges(scenery, texture);
        sharp_map_edges(scenery, texture, true);
        const auto tall_texture = LoadRenderTexture(640, 720);
        sharp_map_edges(scenery, tall_texture);
        UnloadRenderTexture(tall_texture);
        // Shadow textures leave rlgl's cached framebuffer size behind.
        BeginDrawing(); ClearBackground(BLACK);
        scenery.minimap(player, heading, camera);
        Image screen = LoadImageFromScreen(); EndDrawing();
        require(marker_pixels(screen, mini.anchor, RAYWHITE) > 5, "rounded minimap disappeared after an offscreen render pass");
        UnloadImage(screen);
        for (int mode : {0, 1, 2}) {
            const bool full = mode == 2, in_vehicle = mode == 1;
            const MinimapView mini(480, player, heading, camera, in_vehicle);
            police.clear(); unit.car->reset(car_position); unit.car->repair();
            for (auto& officer : unit.officers) { officer.seated = true; officer.character->revive(); }
            const auto project = [&](Vec3 point) { return full ? view.project(point, viewport) : mini.project(point); };
            const auto render = [&](const Police* cops, bool blue = false) {
                // Capture early in a flash phase so image comparisons stay deterministic.
                if (cops && cops->wanted().stars()) {
                    const double phase = std::fmod(GetTime(), 1.0), start = blue ? .5 : 0;
                    if (phase < start || phase > start + .35)
                        WaitTime((phase < start ? start - phase : 1 + start - phase) + .001);
                }
                BeginTextureMode(texture); ClearBackground(BLACK);
                if (full) scenery.world_map(view, viewport, player, heading, cops);
                else scenery.minimap(player, heading, camera, cops, in_vehicle);
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
            unit.active = false;
            Image wanted_base = render(&police), wanted_blue = render(&police, true);
            const auto sample = project(player + Vec3(30, 0, 20));
            const Color red = GetImageColor(wanted_base, int(sample.x), int(sample.y));
            const Color blue = GetImageColor(wanted_blue, int(sample.x), int(sample.y));
            require(red.r > blue.r && blue.b > red.b, "search circle did not alternate red and blue");
            if (full) {
                const auto edge = project(player + Vec3(police.wanted().radius(), 0, 0));
                require(!marker_pixels(wanted_base, edge, RED) && !marker_pixels(wanted_blue, edge, BLUE),
                    "search circle retained its opaque border");
            }
            const_cast<WantedLevel&>(police.wanted()).step(player, false, .01f);
            require(police.wanted().searching(), "marker test did not enter search mode");
            Image searching = render(&police);
            require(equal(wanted_base, searching), "searching changed the red/blue circle styling");
            unit.active = true;
            Image live = render(&police);
            require(marker_pixels(live, project(car_position), {79, 142, 255, 255}) > 20,
                "live police car had no marker while wanted");
            ExportImage(live, full ? "map-markers-world-wanted.png" : in_vehicle ? "map-markers-driving-wanted.png" : "map-markers-minimap-wanted.png");
            auto& officer = unit.officers[0];
            officer.seated = false; officer.character->reset(foot_position);
            Image one_out = render(&police);
            require(marker_pixels(one_out, project(car_position), {79, 142, 255, 255}) > 20
                && marker_pixels(one_out, project(foot_position), {79, 142, 255, 255}) > 15,
                "car with one seated officer or its foot officer lost their marker");
            auto& partner = unit.officers[1];
            const Vec3 partner_position = foot_position + Vec3(20, 0, 15);
            partner.seated = false; partner.character->reset(partner_position);
            Image empty = render(&police);
            require(!marker_pixels(empty, project(car_position), {79, 142, 255, 255})
                && marker_pixels(empty, project(foot_position), {79, 142, 255, 255}) > 15
                && marker_pixels(empty, project(partner_position), {79, 142, 255, 255}) > 15,
                "empty patrol car kept its marker or dismounted officers lost theirs");
            partner.seated = true;
            Image reboarded = render(&police);
            require(equal(one_out, reboarded), "car marker did not return when an officer reboarded");
            partner.character->take_damage(100);
            Image dead_crew = render(&police);
            require(!marker_pixels(dead_crew, project(car_position), {79, 142, 255, 255})
                && marker_pixels(dead_crew, project(foot_position), {79, 142, 255, 255}) > 15,
                "dead seated officer kept the car marker or hid a living foot officer");
            partner.character->revive(); officer.seated = true;
            unit.car->take_damage(100);
            Image wreck = render(&police);
            require(equal(wanted_base, wreck), "destroyed police vehicle kept its marker");
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
            ExportImage(escaped, full ? "map-markers-world-clear.png" : in_vehicle ? "map-markers-driving-clear.png" : "map-markers-minimap-clear.png");
            for (Image image : {baseline, quiet, wanted_base, wanted_blue, searching, live, one_out, empty, reboarded, dead_crew, wreck, foot, dead, escaped}) UnloadImage(image);
        }
        UnloadRenderTexture(texture);
        std::cout << "Both maps: hidden civilian cars/aircraft, wanted-only occupied police cars, foot officers, reboarding, wreck/dead marker removal and escape passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Map marker check failed: " << error.what() << '\n'; CloseWindow(); return 1;
    }
    CloseWindow();
    return 0;
}
