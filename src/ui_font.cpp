#include "ui_font.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace ambaretto::ui {
namespace {
Font active{};
Font current() { return active.texture.id ? active : GetFontDefault(); }
float spacing(int size) { return size * .04f; }
}
FontResource::FontResource() : previous_(active) {
    const std::string relative = "assets/fonts/big-blue-terminal-plus.ttf";
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    const auto& path = FileExists(bundled.c_str()) ? bundled : relative;
    font_ = LoadFontEx(path.c_str(), 32, nullptr, 0);
    owned_ = font_.texture.id && font_.texture.id != GetFontDefault().texture.id;
    if (owned_) {
        SetTextureFilter(font_.texture, TEXTURE_FILTER_POINT);
        TraceLog(LOG_INFO, "UI: Loaded Big Blue Terminal Plus for all interface text");
    } else {
        TraceLog(LOG_WARNING, "UI: Could not load Big Blue Terminal Plus; using fallback font");
    }
    active = font_;
}
FontResource::~FontResource() {
    active = previous_;
    if (owned_) UnloadFont(font_);
}
void draw_text(const char* text, int x, int y, int size, Color color, float spacing_scale) {
    DrawTextEx(current(), text, {float(x), float(y)}, float(size), size * spacing_scale, color);
}
int measure_text(const char* text, int size, float spacing_scale) {
    return int(std::ceil(MeasureTextEx(current(), text, float(size), size * spacing_scale).x));
}
void draw_desktop() {
    ClearBackground(desktop);
}
void draw_frame(Rectangle r, bool sunken) {
    const Color top[] = {sunken ? shadow : highlight, sunken ? ink : light};
    const Color bottom[] = {sunken ? highlight : ink, sunken ? light : shadow};
    for (int i = 0; i < 2; ++i) {
        DrawRectangle(int(r.x + i), int(r.y + i), int(r.width - 2 * i), 1, top[i]);
        DrawRectangle(int(r.x + i), int(r.y + i), 1, int(r.height - 2 * i), top[i]);
        DrawRectangle(int(r.x + i), int(r.y + r.height - 1 - i), int(r.width - 2 * i), 1, bottom[i]);
        DrawRectangle(int(r.x + r.width - 1 - i), int(r.y + i), 1, int(r.height - 2 * i), bottom[i]);
    }
}
void draw_panel(Rectangle r, bool sunken, Color fill) {
    DrawRectangleRec(r, fill);
    draw_frame(r, sunken);
}
void draw_button(Rectangle r, bool pressed, bool focused, bool enabled) {
    draw_panel(r, enabled && pressed);
    if (focused && enabled) {
        for (int x = int(r.x + 4); x < r.x + r.width - 4; x += 2) {
            DrawPixel(x, int(r.y + 4), ink);
            DrawPixel(x, int(r.y + r.height - 5), ink);
        }
        for (int y = int(r.y + 4); y < r.y + r.height - 4; y += 2) {
            DrawPixel(int(r.x + 4), y, ink);
            DrawPixel(int(r.x + r.width - 5), y, ink);
        }
    }
}
void draw_slider(Rectangle r, float fraction) {
    draw_panel({r.x, r.y + r.height / 2 - 2, r.width, 4}, true);
    draw_panel({r.x + (r.width - 10) * std::clamp(fraction, 0.f, 1.f), r.y - 4, 10, r.height + 8});
}
void draw_window_title(Rectangle r, const char* title) {
    DrawRectangleRec({r.x + 3, r.y + 3, r.width - 6, 28}, title_blue);
    std::string label = title;
    if (measure_text(label.c_str(), 18) > r.width - 64) {
        while (!label.empty() && measure_text((label + "...").c_str(), 18) > r.width - 64) label.pop_back();
        label += "...";
    }
    draw_text(label.c_str(), int(r.x + 10), int(r.y + 8), 18, highlight);
}
void draw_window_close(Rectangle bounds) {
    const auto r = window_close(bounds);
    const bool pressed = CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    draw_button(r, pressed);
    const int x = int(r.x + 7) + pressed, y = int(r.y + 5) + pressed;
    for (int i = 0; i < 8; ++i) {
        DrawRectangle(x + i, y + i, 2, 1, ink);
        DrawRectangle(x + 7 - i, y + i, 2, 1, ink);
    }
}
void draw_window(Rectangle r, const char* title) {
    DrawRectangleRec({r.x + 3, r.y + 3, r.width, r.height}, ink);
    draw_panel(r);
    draw_window_title(r, title);
}
void draw_image_text(Image* image, const char* text, int x, int y, int size, Color color) {
    ImageDrawTextEx(image, current(), text, {float(x), float(y)}, float(size), spacing(size), color);
}
} // namespace ambaretto::ui
