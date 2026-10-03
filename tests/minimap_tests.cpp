#include "minimap.hpp"
#include "world_map.hpp"
#include <iostream>
#include <stdexcept>

namespace {
bool near(float a, float b) { return std::abs(a - b) < .002f; }
bool near(Vector2 a, Vector2 b) { return near(a.x, b.x) && near(a.y, b.y); }
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Camera3D looking(forza::Vec3 direction) {
    return {{20, 30, 40}, {20 + direction.GetX(), 30 + direction.GetY(), 40 + direction.GetZ()},
        {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
}
}
int main() {
    try {
        using forza::Vec3;
        const Vec3 player(120, 4, -350);
        const float extent = 5120;
        for (Vec3 direction : {Vec3(0, 0, -1), Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(-1, 0, 0), Vec3(1, 0, -1).Normalized()}) {
            const forza::MinimapView map(720, player, Vec3(0, 0, -1), looking(direction));
            require(near(map.project(player), map.anchor), "player moved away from map anchor");
            require(near(map.project(player + direction * 50), {map.anchor.x, map.anchor.y - 80}), "camera forward must point up");
            require(near(map.project(player + direction.Cross(Vec3::sAxisY()) * 50), {map.anchor.x + 80, map.anchor.y}), "camera right must point right");
            // Raylib rotates the complete texture about the player's scaled texel.
            const Vector2 origin{(player.GetX() + extent) * map.scale, (player.GetZ() + extent) * map.scale};
            const float radians = map.rotation() * .01745329252f;
            for (Vec3 point : {Vec3(180, 0, -420), Vec3(-extent, 0, extent), Vec3(extent, 0, -extent)}) {
                const float x = (point.GetX() + extent) * map.scale - origin.x;
                const float y = (point.GetZ() + extent) * map.scale - origin.y;
                const Vector2 texel{map.anchor.x + x * std::cos(radians) - y * std::sin(radians),
                    map.anchor.y + x * std::sin(radians) + y * std::cos(radians)};
                require(near(texel, map.project(point)), "rotated texture and markers disagree");
            }
        }
        const forza::MinimapView pitched(720, player, Vec3(0, 0, 1), looking(Vec3(0, -100, -10)));
        require(near(pitched.forward.GetZ(), -1) && near(pitched.project(player + Vec3(0, 500, -50)).y, pitched.anchor.y - 80), "pitch/height changed map scale");
        const forza::MinimapView vertical(720, player, Vec3(1, 7, 0), looking(Vec3(0, -10, 0)));
        require(near(vertical.forward.GetX(), 1), "vertical camera lost player-heading fallback");
        const forza::MinimapView zero(720, player, Vec3::sZero(), looking(Vec3::sZero()));
        require(near(zero.forward.GetZ(), -1) && std::isfinite(zero.rotation()), "zero directions lost north fallback");
        for (int height : {720, 600}) {
            const forza::MinimapView map(height, player, Vec3(0, 0, -1), looking(Vec3(0, 0, -1)));
            require(near(map.bounds.x, 14) && near(map.bounds.y + map.bounds.height, float(height - 14)), "map lost bottom-left margin");
            require(near(map.bounds.width, 320) && near(map.bounds.height, 180) && near(map.scale, 1.6f), "map lost its 200m zoom");
            require(near(map.anchor.x, 174) && near(map.anchor.y, float(height - 77)), "look-ahead player anchor changed");
            require(map.contains(map.anchor) && map.contains({map.bounds.x + 6, map.bounds.y + 6}), "visible markers rejected");
            require(!map.contains({map.bounds.x + 5, map.bounds.y + 6}) && !map.contains({map.bounds.x + 314, map.bounds.y + 175}), "markers escaped clipped bounds");
        }
        const Rectangle viewport{20, 60, 1000, 600};
        const Vector2 viewport_center{520, 360};
        forza::WorldMapView map;
        require(near(map.scale, .5f), "world map default zoom changed");
        map.fit(viewport, extent);
        require(near(map.center, {0, 0}) && near(map.scale, 600 / (2 * extent)), "fit did not show whole map");
        const auto upper = map.project(Vec3(-extent, 0, -extent), viewport);
        const auto lower = map.project(Vec3(extent, 0, extent), viewport);
        require(upper.x >= viewport.x && near(upper.y, viewport.y) && lower.x <= viewport.x + viewport.width
            && near(lower.y, viewport.y + viewport.height), "fitted map escaped viewport");
        const Rectangle developed{-1200, -850, 3400, 5700};
        map.fit(viewport, developed, extent);
        require(near(map.center, {500, 2000}) && near(map.scale, 600.0f / 5700), "developed region fit lost midpoint or aspect ratio");
        for (Vec3 corner : {Vec3(developed.x, 0, developed.y), Vec3(developed.x + developed.width, 0, developed.y + developed.height)}) {
            const auto edge = map.project(corner, viewport);
            require(edge.x >= viewport.x - .002f && edge.x <= viewport.x + viewport.width + .002f
                && edge.y >= viewport.y - .002f && edge.y <= viewport.y + viewport.height + .002f, "developed region escaped fitted viewport");
        }
        map.focus(player, viewport, extent);
        require(near(map.project(player, viewport), viewport_center) && near(map.scale, 1000.0f / 1800), "focus lost player or 1800m range");
        for (Vec3 point : {player, Vec3(800, 100, -200), Vec3(-extent, 0, extent)})
            require(near(map.world(map.project(point, viewport), viewport), {point.GetX(), point.GetZ()}), "world/screen projection did not round trip");
        const Vector2 cursor{800, 170};
        const auto anchored = map.world(cursor, viewport);
        map.zoom(2, cursor, viewport, extent);
        require(near(map.world(cursor, viewport), anchored), "zoom moved world point under cursor");
        const auto before_pan = map.project(player, viewport);
        map.pan({150, -90}, extent);
        require(near(map.project(player, viewport), {before_pan.x + 150, before_pan.y - 90}), "drag moved content opposite pointer");
        map.zoom(100000, viewport_center, viewport, extent);
        require(near(map.scale, 2) && std::isfinite(map.scale), "maximum zoom is unbounded");
        map.zoom(-100000, viewport_center, viewport, extent);
        require(near(map.scale, 600 / (2 * extent)), "minimum zoom lost fitted map scale");
        map.pan({-1000000, 1000000}, extent);
        require(near(map.center, {extent, -extent}), "pan escaped world bounds");
        map.focus(Vec3(extent * 2, 0, -extent * 2), viewport, extent);
        require(near(map.center, {extent, -extent}), "focus escaped world bounds");
        map.center = {extent * 2, -extent * 2}; map.scale = .001f;
        map.constrain({0, 0, 512, 256}, extent);
        require(near(map.center, {extent, -extent}) && near(map.scale, 256 / (2 * extent)), "resize lost zoom/center bounds");
        map.constrain({0, 0, 1024, 600}, extent);
        require(near(map.scale, 600 / (2 * extent)), "larger viewport did not update minimum zoom");
        std::cout << "Camera-relative minimap and interactive world-map projection, pan, zoom and bounds passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
