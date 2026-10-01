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
        SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
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
void draw_image_text(Image* image, const char* text, int x, int y, int size, Color color) {
    ImageDrawTextEx(image, current(), text, {float(x), float(y)}, float(size), spacing(size), color);
}
} // namespace forza::ui
