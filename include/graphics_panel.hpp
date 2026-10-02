#pragma once
#include "graphics_settings.hpp"
#include <raylib.h>

namespace forza {
enum class GraphicsPanelAction { None, Preview, Apply, Cancel };
struct GraphicsPanelInput {
    Vector2 mouse{};
    bool pressed = false, down = false, focused = true, fine = false;
    bool apply = false, cancel = false, defaults = false;
    int horizontal = 0, vertical = 0, tab = 0;
    float scroll = 0;
};
class GraphicsPanel {
public:
    void open(const GraphicsSettings& settings);
    void close() { visible_ = false; dragging_ = -1; }
    void cancel_drag() { dragging_ = -1; }
    bool visible() const { return visible_; }
    const GraphicsSettings& pending() const { return pending_; }
    bool dirty() const { return pending_ != original_; }
    Rectangle bounds(int width, int height) const;
    GraphicsPanelAction update();
    GraphicsPanelAction update(int width, int height, const GraphicsPanelInput& input);
    void draw(float fps, const std::string& status) const;
private:
    GraphicsSettings original_{}, pending_{};
    bool visible_ = false;
    int selected_ = 0, dragging_ = -1;
};
} // namespace forza
