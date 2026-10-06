#pragma once
#include "car_renderer.hpp"
#include "player.hpp"

namespace ambaretto {
struct City;
// One left-side component supplies both sides of each articulated limb.
inline constexpr int character_body_slots[] = {0,1,2,3,4,5,3,4,5,6,7,8,6,7,8};
inline constexpr bool character_body_mirrored[] = {false,false,false,false,false,false,true,true,true,false,false,false,true,true,true};
Matrix character_component_transform(const CharacterComponent& component, const BodyPartPose& pose, Vector3 center, Vector3 size, bool mirrored);
class CharacterRenderer {
public:
    bool available(const CharacterDesign& design, std::string& error) const;
    bool available(const City& city, std::string& error) const;
    void refresh() { models_.refresh(); }
    void set_lighting(const SceneLighting& lighting, const Camera3D& camera, const Daylight& light, const GraphicsSettings& settings) const { models_.set_lighting(lighting,camera,light,settings); }
    void draw(const Character& character, const Camera3D& camera, Shader override_shader = {}, bool guides = false, int selected_slot = -1, const CharacterDesign* preview = nullptr) const;
private:
    CarRenderer models_;
};
} // namespace ambaretto
