#pragma once
#include "building_mesh.hpp"
#include "scene_lighting.hpp"
#include <cstddef>
#include <memory>

namespace ambaretto {
std::filesystem::path building_texture_directory();
struct BuildingRenderStats {
    std::size_t emitted_triangles = 0, vertices = 0;
    std::size_t gpu_geometry_bytes = 0, cpu_geometry_bytes = 0, gpu_texture_bytes = 0;
    std::size_t array_pages = 0, meshes = 0, color_draws = 0, shadow_draws = 0;
    double bake_ms = 0;
};
class BuildingRenderer {
public:
    static constexpr std::size_t texture_budget = 8*1024*1024, geometry_budget = 8*1024*1024;
    BuildingRenderer();
    ~BuildingRenderer();
    BuildingRenderer(const BuildingRenderer&) = delete;
    BuildingRenderer& operator=(const BuildingRenderer&) = delete;
    bool build(const std::vector<BuildingTriangle>& triangles, std::string& error,
               bool spatial_chunks = true, const std::vector<std::string>& thumbnail_textures = {});
    void draw(const Camera3D& camera, const Daylight& daylight, const SceneLighting& lighting, const GraphicsSettings& settings);
    void draw_shadow(Vector3 focus, float distance);
    bool thumbnail(const std::string& filename, Rectangle destination);
    const BuildingRenderStats& stats() const;
    const std::string& warning() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace ambaretto
