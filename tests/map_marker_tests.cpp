#include "environment_renderer.hpp"
#include "police.hpp"
#include "ui_font.hpp"
#include <algorithm>
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
Camera3D city_camera(forza::Vec3 focus) {
    return {{focus.GetX(), forza::City::level + 200, focus.GetZ()},
        {focus.GetX(), forza::City::level, focus.GetZ()}, {0, 0, -1}, 100, CAMERA_ORTHOGRAPHIC};
}
Image render_city(forza::EnvironmentRenderer& scenery, RenderTexture2D texture, Camera3D camera, const char* time = "12:00") {
    forza::DayNight clock; clock.set_time(time);
    forza::SceneLighting lighting;
    forza::GraphicsSettings settings; settings.shadows = 0; settings.local_lights = false;
    BeginTextureMode(texture); ClearBackground(BLACK); BeginMode3D(camera);
    scenery.draw(camera, 0, clock.lighting(), lighting, settings);
    EndMode3D(); EndTextureMode();
    Image image = LoadImageFromTexture(texture.texture); ImageFlipVertical(&image); return image;
}
int crossing_pixels(Image image, Camera3D camera, const forza::CityRoadPort& gate, int direction) {
    using namespace forza;
    const Vec3 out((direction == 1) - (direction == 3), 0, (direction == 2) - (direction == 0));
    const Vec3 across = out.Cross(Vec3::sAxisY()), center = gate.center - out * .9f;
    Vector2 first{float(image.width), float(image.height)}, last{};
    // Exclude the thin verge lines; only zebra stripes should fill this gate strip.
    for (float along : {-.7f, .7f}) for (float side : {-1.f, 1.f}) {
        const Vec3 p = center + out * along + across * (side * (gate.width / 2 - 1.8f));
        const Vector2 screen = GetWorldToScreenEx({p.GetX(), City::level + .1f, p.GetZ()}, camera, image.width, image.height);
        first.x = std::min(first.x, screen.x); first.y = std::min(first.y, screen.y);
        last.x = std::max(last.x, screen.x); last.y = std::max(last.y, screen.y);
    }
    int count = 0;
    for (int y = std::max(0, int(std::ceil(first.y))); y <= std::min(image.height - 1, int(last.y)); ++y)
        for (int x = std::max(0, int(std::ceil(first.x))); x <= std::min(image.width - 1, int(last.x)); ++x) {
            const Color color = GetImageColor(image, x, y);
            count += std::min({color.r, color.g, color.b}) > 190
                && std::max({color.r, color.g, color.b}) - std::min({color.r, color.g, color.b}) < 24;
        }
    return count;
}
void city_crosswalks(RenderTexture2D texture) {
    using namespace forza;
    City city = City::create("Crosswalk render check"); std::string error;
    require(city.add_land({56, 58}, {74, 74}, error), "crosswalk island failed");
    for (const auto& stroke : {City::road_stroke({65, 60}, {65, 66}), City::road_stroke({65, 66}, {72, 66}),
        City::road_stroke({66, 66}, {66, 72}), City::road_stroke({67, 66}, {67, 72}),
        City::road_stroke({56, 64}, {59, 61})})
        require(city.add_road(stroke, error), "crosswalk road failed");
    const auto network = city.road_network();
    const auto junction = std::find_if(network.begin(), network.end(), [](const auto& node) { return node.mask == 7 && node.edges.size() == 3; });
    require(junction != network.end() && junction->outline().size() >= 3, "unequal T did not produce a junction outline");
    Environment map(city); EnvironmentRenderer scenery(map);
    auto camera = city_camera(City::center({67, 66}));
    Image image = render_city(scenery, texture, camera);
    ExportImage(image, "city-unequal-crosswalks.png");
    for (int d = 0; d < 3; ++d) {
        const auto& gate = junction->ports[d];
        require(gate.width > 0, "junction lost an actual road gate");
        const int pixels = crossing_pixels(image, camera, gate, d);
        if (pixels <= gate.width * 5) std::cerr << "Crosswalk gate " << d << ": " << pixels << " bright pixels\n";
        require(pixels > gate.width * 5, "crosswalk missing at an unequal junction gate");
    }
    UnloadImage(image);
    const auto bend = std::find_if(network.begin(), network.end(), [](const auto& node) { return node.mask == 9 && node.edges.size() == 2; });
    require(bend != network.end() && !bend->path().empty(), "simple corner did not produce a road path");
    camera = city_camera(bend->center()); image = render_city(scenery, texture, camera);
    for (int d : {0, 3})
        require(crossing_pixels(image, camera, bend->ports[d], d) < 20, "simple bend acquired an intersection crosswalk");
    UnloadImage(image);
    const auto street = std::find_if(network.begin(), network.end(), [](const auto& node) {
        return node.mask == 5 && node.first.z == 63 && node.ports[0].width == City::block;
    });
    require(street != network.end(), "crosswalk fixture lost its single road");
    camera = city_camera(street->center()); camera.fovy = 32;
    image = render_city(scenery, texture, camera);
    // Resolve subpixel markings in a close-up, without relying on the game's MSAA.
    for (float fraction : {0.f, .5f, 1.f}) {
        const auto path = street->path(fraction, fraction == 0 ? 1.26f : fraction == 1 ? -1.26f : 0);
        const Vec3 p = (path.front() + path.back()) / 2;
        const Vector2 point = GetWorldToScreenEx({p.GetX(), City::level + .1f, p.GetZ()}, camera, image.width, image.height);
        int pixels = 0;
        const int reach = int(4 * image.height / camera.fovy);
        for (int y = int(point.y) - reach; y <= int(point.y) + reach; ++y)
            for (int x = int(point.x) - 3; x <= int(point.x) + 3; ++x) {
                const Color color = GetImageColor(image, x, y);
                pixels += fraction == .5f ? color.r > 170 && color.g > 160 && color.r > color.b + 30 && color.g > color.b + 20
                    : std::min({color.r, color.g, color.b}) > 190
                        && std::max({color.r, color.g, color.b}) - std::min({color.r, color.g, color.b}) < 24;
            }
        require(pixels > 30, "single road lost a verge or centerline");
    }
    UnloadImage(image);
}
void elevated_road_pixels(RenderTexture2D texture) {
    using namespace forza;
    bool passed = true;
    for (bool diagonal : {false, true}) {
        City city = City::create("Elevated road render"); std::string error;
        require(city.add_land({58,58},{70,70},error)
            && city.change_elevation(diagonal ? CityCell{60,60} : CityCell{64,58},{70,70},1,error)
            && city.add_road(City::road_stroke(diagonal ? CityCell{60,60} : CityCell{60,64},
                diagonal ? CityCell{68,68} : CityCell{68,64},false,diagonal),error),"elevated road fixture failed");
        const auto network = city.road_network();
        const auto node = std::find_if(network.begin(),network.end(),[&](const auto& n) {
            const auto path = n.path();
            return n.first.x==(diagonal ? 64 : 63) && n.edges.size()==2 && path.size()==2
                && (!diagonal || std::abs(path[1].GetX()-path[0].GetX())>.001f
                    && std::abs(path[1].GetZ()-path[0].GetZ())>.001f);
        });
        require(node!=network.end(),"elevated render lost its straight or diagonal road");
        Environment map(city); EnvironmentRenderer scenery(map);
        auto camera = city_camera(node->center()); camera.fovy = 32;
        Image image = render_city(scenery,texture,camera);
        ExportImage(image,diagonal ? "city-elevated-diagonal-road.png" : "city-elevated-cardinal-road.png");
        // Cardinal streets traverse a straight ramp; diagonals stay on a flat raised terrace.
        for (float fraction : {0.f,.25f,.75f,1.f}) {
            const bool verge = fraction==0 || fraction==1;
            const auto path = node->path(fraction,fraction==0 ? 1.26f : fraction==1 ? -1.26f : 0);
            for (float t : {.25f,.5f,.75f}) {
                Vec3 p = path.front()+(path.back()-path.front())*t;
                p.SetY(city.height(p.GetX(),p.GetZ())+.1f);
                const Vector2 point = GetWorldToScreenEx({p.GetX(),p.GetY(),p.GetZ()},camera,image.width,image.height);
                int pixels = 0;
                for (int y = int(point.y)-2; y <= int(point.y)+2; ++y)
                    for (int x = int(point.x)-2; x <= int(point.x)+2; ++x) {
                        const Color color = GetImageColor(image,x,y);
                        const int low = std::min({color.r,color.g,color.b}), high = std::max({color.r,color.g,color.b});
                        pixels += verge ? low>170 && high-low<24
                            : low>20 && high<165 && high-low<30;
                    }
                if (pixels<(verge ? 3 : 16)) {
                    const Color color = GetImageColor(image,int(point.x),int(point.y));
                    std::cerr << (diagonal ? "Diagonal terrace" : "Cardinal ramp") << " fraction " << fraction << " t " << t
                        << ": " << pixels << " matching pixels at " << point.x << ',' << point.y << "; center RGB "
                        << int(color.r) << ',' << int(color.g) << ',' << int(color.b) << '\n';
                    passed = false;
                }
            }
        }
        UnloadImage(image);
    }
    require(passed,"elevated ramp hides road pavement or a white verge marking");
}
void single_square_ramps(RenderTexture2D texture) {
    using namespace forza;
    City flat = City::create("Single square ramps"); std::string error;
    require(flat.add_land({59,59},{69,69},error),"single-square island failed");
    City raised = flat;
    require(raised.change_elevation({64,64},{64,64},1,error),"single-square raise failed");
    const auto top = raised.ground_patch({64,64});
    for (const auto& p : top) require(std::abs(p.GetY()-City::level-City::elevation_step)<.001f,"raised square lost its flat top");
    for (CityCell cell : {CityCell{63,64},CityCell{65,64},CityCell{64,63},CityCell{64,65}}) {
        const auto patch = raised.ground_patch(cell);
        int high = 0, low = 0;
        for (const auto& p : patch) {
            high += std::abs(p.GetY()-City::level-City::elevation_step)<.001f;
            low += std::abs(p.GetY()-City::level)<.001f;
        }
        require(high==2 && low==2,"neighbor square did not become a straight ramp");
    }
    const Vec3 focus = City::center({64,64});
    Camera3D camera{{focus.GetX()+70,focus.GetY()+65,focus.GetZ()+70},
        {focus.GetX(),focus.GetY(),focus.GetZ()},{0,1,0},65,CAMERA_ORTHOGRAPHIC};
    Environment flat_map(flat), raised_map(raised);
    EnvironmentRenderer flat_renderer(flat_map), raised_renderer(raised_map);
    // Morning light makes the separate planar ramp faces easy to inspect.
    Image before = render_city(flat_renderer,texture,camera,"09:00"), after = render_city(raised_renderer,texture,camera,"09:00");
    ExportImage(after,"city-single-square-ramps.png");
    int changed = 0;
    for (int y = 0; y<after.height; ++y) for (int x = 0; x<after.width; ++x)
        changed += !same(GetImageColor(before,x,y),GetImageColor(after,x,y));
    require(changed>1000,"corner-height ramps did not appear in the isometric renderer");
    UnloadImage(before); UnloadImage(after);
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
void diagonal_bridge_pixels(RenderTexture2D texture) {
    using namespace forza;
    City city = City::create("Diagonal bridge render"); std::string error;
    require(city.add_road(City::road_stroke({60,60},{68,68},false,true),error),"diagonal render stroke failed");
    const auto network = city.road_network();
    const auto node = std::find_if(network.begin(),network.end(),[](const auto& n) {
        const auto path = n.path();
        return n.edges.size()==2 && path.size()==2 && std::abs(path[1].GetX()-path[0].GetX())>.001f
            && std::abs(path[1].GetZ()-path[0].GetZ())>.001f;
    });
    require(node!=network.end(),"diagonal render has no bridge section");
    Environment map(city); EnvironmentRenderer scenery(map);
    auto camera = city_camera(node->center()); camera.fovy = 32;
    Image image = render_city(scenery,texture,camera);
    const Vec3 shoulder = node->center()+Vec3(node->half_size().GetX()-.8f,0,-node->half_size().GetZ()+.8f);
    const Vector2 point = GetWorldToScreenEx({shoulder.GetX(),City::level,shoulder.GetZ()},camera,image.width,image.height);
    const Color color = GetImageColor(image,int(point.x),int(point.y));
    require(color.b>color.r+50 && color.g>color.r+30,"diagonal bridge still shows square deck blocks outside its pavement");
    UnloadImage(image);
}
}

int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 480, "Map marker regression check");
    try {
        using namespace forza;
        if (argc == 4 && std::string(argv[1]) == "--city-preview") {
            City city; std::string error;
            if (!City::load(argv[2], city, error)) throw std::runtime_error(error);
            const auto texture = LoadRenderTexture(1920, 1440);
            {
                Environment map(city); EnvironmentRenderer scenery(map);
                Image image = render_city(scenery, texture, city_camera(City::center({67, 66})));
                const bool saved = ExportImage(image, argv[3]); UnloadImage(image);
                require(saved, "city preview export failed");
            }
            UnloadRenderTexture(texture); CloseWindow(); return 0;
        }
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
        city_crosswalks(texture);
        elevated_road_pixels(texture);
        single_square_ramps(texture);
        diagonal_bridge_pixels(texture);
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
