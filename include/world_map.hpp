#pragma once

#include "minimap.hpp"
#include "ui_font.hpp"
#include <algorithm>
#include <array>

namespace forza {
struct WorldMapLayout {
    Rectangle window, viewport;
    std::array<Rectangle,5> buttons;
    WorldMapLayout(int width,int height) {
        const float w = float(std::min(1120,width-96)), h = float(std::min(760,height-112));
        window = {(width-w)/2,(height-h)/2+16,w,h};
        viewport = {window.x+12,window.y+84,w-24,h-96};
        buttons = {{{window.x+18,window.y+44,94,30},{window.x+120,window.y+44,94,30},
            {window.x+222,window.y+44,42,30},{window.x+272,window.y+44,42,30},ui::window_close(window)}};
    }
};
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
