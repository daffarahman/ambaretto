#pragma once
#include "vehicle.hpp"
#include "day_night.hpp"
#include "scene_lighting.hpp"
#include <raylib.h>
#include <map>

namespace ambaretto {
class CarRenderer {
public:
    CarRenderer();
    ~CarRenderer();
    CarRenderer(const CarRenderer&) = delete;
    CarRenderer& operator=(const CarRenderer&) = delete;
    void set_lighting(const SceneLighting& lighting, const Camera3D& camera, const Daylight& light, const GraphicsSettings& settings) const { lighting.apply(shader_, camera, light, settings); }
    bool draw_body(const Car& car, const Camera3D& camera, Shader override_shader = {}) const;
    bool draw_wheel(const Car& car, const Wheel& wheel, const Camera3D& camera, Shader override_shader = {}) const;
    bool available(const CarDesign& design, std::string& error) const;
    void refresh();
    bool model_bounds(const std::string& folder, const std::string& name, Vector3& center, Vector3& size) const;
    void draw_imported(const std::string& folder, const std::string& name, const Matrix& transform, const Camera3D& camera, Shader override_shader = {}, bool mirrored = false) const;
    void draw_design(const CarDesign& design, Vec3 ground, float yaw, const Camera3D& camera) const;
    void draw_damage(const Camera3D& camera, Vec3 center, bool wreck, float age, float scale, float time) const;
private:
    struct Asset { Model model{}; Vector3 center{}, size{}; float radius = 0; int axis = 0; };
    const Asset* asset(const std::string& name, const std::string& folder) const;
    bool body(const CarDesign& design, Vec3 position, Quat rotation, const Camera3D& camera, Shader override_shader = {}, bool intact = true) const;
    bool tire(const CarDesign& design, Vec3 position, Quat rotation, const Camera3D& camera, Shader override_shader = {}, bool intact = true) const;
    void draw_model(const Model& model, const Matrix& transform, const Camera3D& camera, Shader override_shader = {}, bool intact = true, bool mirrored = false) const;
    mutable std::map<std::string, Asset> assets_;
    Shader shader_{}, damage_shader_{};
    int camera_location_ = -1, emission_location_ = -1;
    int mirrored_location_ = -1;
    int damage_time_location_ = -1, damage_fire_location_ = -1;
};
std::filesystem::path car_asset_directory(const char* folder);
} // namespace ambaretto
