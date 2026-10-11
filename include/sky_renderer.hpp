#pragma once
#include "day_night.hpp"
#include "graphics_settings.hpp"
#include <raylib.h>

namespace ambaretto {
class SkyRenderer {
public:
    SkyRenderer();
    ~SkyRenderer();
    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;
    void draw(const Camera3D& camera, const Daylight& light, float time, const GraphicsSettings& settings);
private:
    Shader sky_{}, clouds_{};
    Texture2D noise_{};
    RenderTexture2D layer_{};
};
} // namespace ambaretto
