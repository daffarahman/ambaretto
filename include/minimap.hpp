#pragma once

#include "vehicle.hpp"
#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <limits>

namespace forza {
struct MinimapView {
    Rectangle bounds;
    Vector2 anchor;
    float scale;
    Vec3 player, forward;
    bool vehicle_view;
    static constexpr float vehicle_pitch = .65f, vehicle_distance = 240;

    MinimapView(int screen_height, Vec3 player_position, Vec3 player_forward, const Camera3D& camera, bool in_vehicle = false)
        : bounds{14, float(screen_height - 194), 320, 180},
          anchor{bounds.x + bounds.width * .5f, bounds.y + bounds.height * (in_vehicle ? .72f : .65f)},
          scale(bounds.width / 120), player(player_position), vehicle_view(in_vehicle) {
        const Vec3 fallback = Vec3(player_forward.GetX(), 0, player_forward.GetZ()).NormalizedOr(Vec3(0, 0, -1));
        forward = Vec3(camera.target.x - camera.position.x, 0, camera.target.z - camera.position.z).NormalizedOr(fallback);
    }
    float rotation() const { return std::atan2(-forward.GetX(), -forward.GetZ()) * 57.295779513f; }
    Matrix transform() const {
        Matrix matrix = MatrixTranslate(-player.GetX(), -player.GetZ(), 0);
        matrix = MatrixMultiply(matrix, MatrixRotateZ(rotation() * DEG2RAD));
        if (vehicle_view) {
            // Project the flat map plane with a tilted camera: distant roads shrink.
            Matrix perspective = MatrixIdentity();
            perspective.m5 = vehicle_pitch;
            perspective.m7 = -1 / vehicle_distance;
            matrix = MatrixMultiply(matrix, perspective);
        }
        matrix = MatrixMultiply(matrix, MatrixScale(scale, scale, 1));
        return MatrixMultiply(matrix, MatrixTranslate(anchor.x, anchor.y, 0));
    }
    Vector2 project(Vec3 p) const {
        const Vec3 delta = p - player;
        const float ahead = forward.GetX() * delta.GetX() + forward.GetZ() * delta.GetZ();
        const float depth = vehicle_view ? 1 + ahead / vehicle_distance : 1;
        if (depth <= 0) return {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        return {anchor.x + scale * (-forward.GetZ() * delta.GetX() + forward.GetX() * delta.GetZ()) / depth,
            anchor.y - scale * (vehicle_view ? vehicle_pitch : 1) * ahead / depth};
    }
    bool contains(Vector2 p) const {
        return p.x >= bounds.x + 6 && p.x <= bounds.x + bounds.width - 6
            && p.y >= bounds.y + 6 && p.y <= bounds.y + bounds.height - 6;
    }
};
} // namespace forza
