#pragma once
#include <raylib.h>

namespace ambaretto::ui {
inline constexpr Color dos_blue{0, 0, 170, 255}, dos_white{255, 255, 255, 255};
inline constexpr Color dos_yellow{255, 255, 85, 255}, dos_light_blue{85, 85, 255, 255};
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
