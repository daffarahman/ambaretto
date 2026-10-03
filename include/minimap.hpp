#pragma once

#include "vehicle.hpp"
#include <raylib.h>
#include <cmath>

namespace forza {
struct MinimapView {
    Rectangle bounds;
    Vector2 anchor;
    float scale;
    Vec3 player, forward;

    MinimapView(int screen_height, Vec3 player_position, Vec3 player_forward, const Camera3D& camera)
        : bounds{14, float(screen_height - 194), 320, 180},
          anchor{bounds.x + bounds.width * .5f, bounds.y + bounds.height * .65f},
          scale(bounds.width / 200), player(player_position) {
        const Vec3 fallback = Vec3(player_forward.GetX(), 0, player_forward.GetZ()).NormalizedOr(Vec3(0, 0, -1));
        forward = Vec3(camera.target.x - camera.position.x, 0, camera.target.z - camera.position.z).NormalizedOr(fallback);
    }
    float rotation() const { return std::atan2(-forward.GetX(), -forward.GetZ()) * 57.295779513f; }
    Vector2 project(Vec3 p) const {
        const Vec3 delta = p - player;
        return {anchor.x + scale * (-forward.GetZ() * delta.GetX() + forward.GetX() * delta.GetZ()),
            anchor.y - scale * (forward.GetX() * delta.GetX() + forward.GetZ() * delta.GetZ())};
    }
    bool contains(Vector2 p) const {
        return p.x >= bounds.x + 6 && p.x <= bounds.x + bounds.width - 6
            && p.y >= bounds.y + 6 && p.y <= bounds.y + bounds.height - 6;
    }
};
} // namespace forza
