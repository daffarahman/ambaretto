#pragma once
#include <raylib.h>

namespace ambaretto::ui {
inline constexpr Color face{192, 192, 192, 255}, light{223, 223, 223, 255};
inline constexpr Color highlight{255, 255, 255, 255}, shadow{128, 128, 128, 255};
inline constexpr Color ink{0, 0, 0, 255}, title_blue{0, 0, 128, 255};
inline constexpr Color desktop{128, 128, 128, 255}, disabled{96, 96, 96, 255};
void draw_frame(Rectangle bounds, bool sunken = false);
void draw_panel(Rectangle bounds, bool sunken = false, Color fill = face);
void draw_button(Rectangle bounds, bool pressed = false, bool focused = false, bool enabled = true);
void draw_slider(Rectangle bounds, float fraction);
void draw_desktop();
void draw_window_title(Rectangle bounds, const char* title);
void draw_window(Rectangle bounds, const char* title);
inline Rectangle window_close(Rectangle r) { return {r.x+r.width-30,r.y+7,22,20}; }
void draw_window_close(Rectangle bounds);
// Construct after InitWindow and keep alive until all UI / sign atlases unload.
class FontResource {
public:
    FontResource();
    ~FontResource();
    FontResource(const FontResource&) = delete;
    FontResource& operator=(const FontResource&) = delete;
private:
    Font font_{};
    Font previous_{};
    bool owned_ = false;
};
void draw_text(const char* text, int x, int y, int size, Color color);
int measure_text(const char* text, int size);
void draw_image_text(Image* image, const char* text, int x, int y, int size, Color color);
} // namespace ambaretto::ui
