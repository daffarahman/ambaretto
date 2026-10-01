#pragma once

#include "minimap.hpp"
#include <algorithm>

namespace forza {
struct WorldMapView {
    Vector2 center{0, 0};
    float scale = .5f;

    void constrain(Rectangle viewport, float extent) {
        const float minimum = std::min(2.0f, std::min(viewport.width, viewport.height) / (2 * extent));
        scale = std::clamp(scale, minimum, 2.0f);
        center.x = std::clamp(center.x, -extent, extent);
        center.y = std::clamp(center.y, -extent, extent);
    }
    void fit(Rectangle viewport, Rectangle world_bounds, float extent) {
        center = {world_bounds.x + world_bounds.width * .5f, world_bounds.y + world_bounds.height * .5f};
        scale = std::min(viewport.width / world_bounds.width, viewport.height / world_bounds.height);
        constrain(viewport, extent);
    }
    void fit(Rectangle viewport, float extent) { fit(viewport, {-extent, -extent, 2 * extent, 2 * extent}, extent); }
    void focus(Vec3 player, Rectangle viewport, float extent) {
        center = {player.GetX(), player.GetZ()};
        scale = viewport.width / 1800;
        constrain(viewport, extent);
    }
    Vector2 project(Vec3 point, Rectangle viewport) const {
        return {viewport.x + viewport.width * .5f + (point.GetX() - center.x) * scale,
            viewport.y + viewport.height * .5f + (point.GetZ() - center.y) * scale};
    }
    Vector2 world(Vector2 screen, Rectangle viewport) const {
        return {center.x + (screen.x - viewport.x - viewport.width * .5f) / scale,
            center.y + (screen.y - viewport.y - viewport.height * .5f) / scale};
    }
    void pan(Vector2 pixel_delta, float extent) {
        center.x = std::clamp(center.x - pixel_delta.x / scale, -extent, extent);
        center.y = std::clamp(center.y - pixel_delta.y / scale, -extent, extent);
    }
    void zoom(float wheel_steps, Vector2 cursor, Rectangle viewport, float extent) {
        const Vector2 before = world(cursor, viewport);
        scale *= std::exp(wheel_steps * .18f);
        constrain(viewport, extent);
        const Vector2 after = world(cursor, viewport);
        center.x += before.x - after.x;
        center.y += before.y - after.y;
        constrain(viewport, extent);
    }
};
} // namespace forza
