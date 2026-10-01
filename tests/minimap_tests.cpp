#include "minimap.hpp"
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
            require(near(map.project(player + direction * 50), {map.anchor.x, map.anchor.y - 32}), "camera forward must point up");
            require(near(map.project(player + direction.Cross(Vec3::sAxisY()) * 50), {map.anchor.x + 32, map.anchor.y}), "camera right must point right");
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
        require(near(pitched.forward.GetZ(), -1) && near(pitched.project(player + Vec3(0, 500, -50)).y, pitched.anchor.y - 32), "pitch/height changed map scale");
        const forza::MinimapView vertical(720, player, Vec3(1, 7, 0), looking(Vec3(0, -10, 0)));
        require(near(vertical.forward.GetX(), 1), "vertical camera lost player-heading fallback");
        const forza::MinimapView zero(720, player, Vec3::sZero(), looking(Vec3::sZero()));
        require(near(zero.forward.GetZ(), -1) && std::isfinite(zero.rotation()), "zero directions lost north fallback");
        for (int height : {720, 600}) {
            const forza::MinimapView map(height, player, Vec3(0, 0, -1), looking(Vec3(0, 0, -1)));
            require(near(map.bounds.x, 14) && near(map.bounds.y + map.bounds.height, float(height - 14)), "map lost bottom-left margin");
            require(near(map.bounds.width, 320) && near(map.bounds.height, 180) && near(map.scale, .64f), "map dimensions/zoom changed");
            require(near(map.anchor.x, 174) && near(map.anchor.y, float(height - 77)), "look-ahead player anchor changed");
            require(map.contains(map.anchor) && map.contains({map.bounds.x + 6, map.bounds.y + 6}), "visible markers rejected");
            require(!map.contains({map.bounds.x + 5, map.bounds.y + 6}) && !map.contains({map.bounds.x + 314, map.bounds.y + 175}), "markers escaped clipped bounds");
        }
        std::cout << "Camera-relative minimap rotation, markers, fallback, zoom and layout passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
