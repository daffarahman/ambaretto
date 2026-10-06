#include "character_renderer.hpp"
#include "city.hpp"
#include <raymath.h>
#include <rlgl.h>

namespace ambaretto {
Matrix character_component_transform(const CharacterComponent& part, const BodyPartPose& pose, Vector3 center, Vector3 size, bool mirrored) {
    Matrix transform=MatrixMultiply(MatrixTranslate(-center.x,-center.y,-center.z),
        MatrixScale(pose.size.GetX()*part.scale[0]/size.x,pose.size.GetY()*part.scale[1]/size.y,pose.size.GetZ()*part.scale[2]/size.z));
    // GLBs use Y-up, +Z-front; the existing rig faces -Z.
    transform=MatrixMultiply(transform,MatrixRotateY(PI));
    transform=MatrixMultiply(transform,MatrixRotateXYZ({part.rotation[0]*DEG2RAD,part.rotation[1]*DEG2RAD,part.rotation[2]*DEG2RAD}));
    transform=MatrixMultiply(transform,MatrixTranslate(part.offset[0],part.offset[1],part.offset[2]));
    if (mirrored) transform=MatrixMultiply(transform,MatrixScale(-1,1,1));
    transform=MatrixMultiply(transform,QuaternionToMatrix({pose.rotation.GetX(),pose.rotation.GetY(),pose.rotation.GetZ(),pose.rotation.GetW()}));
    return MatrixMultiply(transform,MatrixTranslate(pose.position.GetX(),pose.position.GetY(),pose.position.GetZ()));
}
bool CharacterRenderer::available(const CharacterDesign& design, std::string& error) const {
    if (!design.validate(error)) return false;
    for (std::size_t i=0;i<design.parts.size();++i) {
        Vector3 center{},size{};
        const std::string folder=std::string("character/")+character_slot_folders[i];
        if (!models_.model_bounds(folder,design.parts[i].model,center,size)) {
            error="Missing or invalid GLB: assets/"+folder+"/"+design.parts[i].model; return false;
        }
    }
    return true;
}
bool CharacterRenderer::available(const City& city, std::string& error) const {
    if (!city.spawn) { error="Set a player spawn to play."; return false; }
    if (!city.player_character || city.player_character->type!=CharacterType::Player) { error="Choose a saved Player character to play."; return false; }
    return available(*city.player_character,error);
}
void CharacterRenderer::draw(const Character& character, const Camera3D& camera, Shader override_shader, bool guides, int selected_slot, const CharacterDesign* preview) const {
    const auto pose=character.body_parts();
    const auto* design=preview ? preview : character.design() ? &*character.design() : nullptr;
    for (std::size_t i=0;i<pose.size();++i) {
        const int slot=character_body_slots[i]; const auto& body=pose[i];
        if (guides) {
            rlPushMatrix();
            const auto rotation=QuaternionToMatrix({body.rotation.GetX(),body.rotation.GetY(),body.rotation.GetZ(),body.rotation.GetW()});
            rlTranslatef(body.position.GetX(),body.position.GetY(),body.position.GetZ());
            const auto values=MatrixToFloatV(rotation); rlMultMatrixf(values.v);
            DrawCubeWires({0,0,0},body.size.GetX(),body.size.GetY(),body.size.GetZ(),slot==selected_slot ? YELLOW : Color{80,120,150,255});
            rlPopMatrix();
        }
        if (!design) continue;
        const auto& part=design->parts[slot]; Vector3 center{},size{};
        const std::string folder=std::string("character/")+character_slot_folders[slot];
        if (!models_.model_bounds(folder,part.model,center,size)) continue;
        models_.draw_imported(folder,part.model,character_component_transform(part,body,center,size,character_body_mirrored[i]),camera,override_shader,character_body_mirrored[i]);
    }
}
} // namespace ambaretto
