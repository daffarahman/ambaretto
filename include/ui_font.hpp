#pragma once
#include <raylib.h>

namespace forza::ui {
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
} // namespace forza::ui
