#include "ui_font.hpp"
#include <cmath>
#include <string>

namespace forza::ui {
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
void draw_text(const char* text, int x, int y, int size, Color color) {
    DrawTextEx(current(), text, {float(x), float(y)}, float(size), spacing(size), color);
}
int measure_text(const char* text, int size) {
    return int(std::ceil(MeasureTextEx(current(), text, float(size), spacing(size)).x));
}
void draw_desktop() {
    ClearBackground(dos_blue);
    for (int y = 40; y < GetScreenHeight(); y += 16)
        for (int x = (y / 16 % 2) * 8; x < GetScreenWidth(); x += 16)
            DrawRectangle(x, y, 2, 2, dos_light_blue);
}
void draw_window_title(Rectangle r, const char* title) {
    DrawRectangleRec({r.x + 3, r.y + 3, r.width - 6, 28}, dos_blue);
    for (int y = 7; y < 28; y += 4) DrawLine(int(r.x + 8), int(r.y + y), int(r.x + r.width - 8), int(r.y + y), dos_white);
    std::string label = title;
    while (!label.empty() && measure_text(label.c_str(),18)>r.width-100) label.pop_back();
    const int width = measure_text(label.c_str(), 18) + 24;
    const int x = int(r.x + (r.width - width) / 2);
    DrawRectangle(x, int(r.y + 3), width, 28, dos_white);
    draw_text(label.c_str(), x + 12, int(r.y + 8), 18, dos_blue);
    DrawRectangleLinesEx({r.x + 3, r.y + 3, r.width - 6, 29}, 1, dos_white);
}
void draw_window_close(Rectangle bounds) {
    const auto r = window_close(bounds);
    const bool hover = CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRec(r,hover ? dos_yellow : dos_blue);
    DrawRectangleLinesEx(r,1,dos_white);
    draw_text("X",int(r.x+(r.width-measure_text("X",16))/2),int(r.y+2),16,hover ? dos_blue : dos_white);
}
void draw_window(Rectangle r, const char* title) {
    DrawRectangleRec({r.x + 5, r.y + 5, r.width, r.height}, {0, 0, 85, 255});
    DrawRectangleRec(r, dos_blue);
    DrawRectangleLinesEx(r, 2, dos_white);
    DrawRectangleLinesEx({r.x + 4, r.y + 35, r.width - 8, r.height - 39}, 1, dos_white);
    draw_window_title(r, title);
}
void draw_image_text(Image* image, const char* text, int x, int y, int size, Color color) {
    ImageDrawTextEx(image, current(), text, {float(x), float(y)}, float(size), spacing(size), color);
}
} // namespace forza::ui
