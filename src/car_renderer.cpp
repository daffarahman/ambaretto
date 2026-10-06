#include "car_renderer.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <cstdint>
#include <set>
#include <string>

namespace ambaretto {
namespace {
void unload_model(Model model) {
    std::set<unsigned int> textures;
    for (int i=0;i<model.materialCount;++i) for (int map=0;map<=MATERIAL_MAP_BRDF;++map) {
        const auto texture=model.materials[i].maps[map].texture;
        if (texture.id && texture.id!=rlGetTextureIdDefault() && textures.insert(texture.id).second) UnloadTexture(texture);
    }
    UnloadModel(model);
}
const char* vertex_shader = R"glsl(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 worldPosition;
out vec3 normal;
out vec2 texCoord;
out vec4 color;
void main() {
    worldPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    normal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    texCoord = vertexTexCoord;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})glsl";
const std::string fragment_shader = std::string(R"glsl(#version 330
in vec3 worldPosition;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 cameraPosition;
uniform vec3 emissionColor;
uniform int mirrored;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform vec3 horizonColor;
uniform float daylight;
)glsl") + scene_lighting_glsl() + R"glsl(
out vec4 finalColor;
void main() {
    vec3 n = normalize(normal);
    if (gl_FrontFacing == (mirrored == 1)) n = -n;
    vec3 light = sunDirection;
    vec3 view = normalize(cameraPosition - worldPosition);
    float shadow = scene_shadow(worldPosition, n, light);
    vec3 lighting = ambientLight + sunColor * (0.42 * daylight * max(dot(n, light), 0.0) * shadow);
    lighting += scene_local_light(worldPosition, n, view, 48.0);
    float highlight = pow(max(dot(n, normalize(light + view)), 0.0), 48.0) * 0.18 * daylight * shadow;
    vec4 surface = texture(texture0, texCoord) * colDiffuse * color;
    vec3 shaded = surface.rgb * lighting + vec3(highlight) + emissionColor * 0.85;
    float fog = smoothstep(viewDistance * 0.65, viewDistance, distance(cameraPosition.xz, worldPosition.xz));
    finalColor = vec4(mix(shaded, horizonColor, fog) * brightness, surface.a);
})glsl";
const char* damage_fragment = R"glsl(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform float effectTime;
uniform int fire;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1, 0)), f.x),
               mix(hash(cell + vec2(0, 1)), hash(cell + vec2(1, 1)), f.x), f.y);
}
void main() {
    vec2 p = fragTexCoord * 2.0 - 1.0;
    vec2 flow = p * 3.0 + vec2(effectTime * 0.3, -effectTime * 0.7);
    float cloud = noise(flow) * 0.6 + noise(flow * 2.0) * 0.3 + noise(flow * 4.0) * 0.1;
    float edge = 1.0 - smoothstep(0.25, 1.0, length(p) + (cloud - 0.5) * 0.18);
    vec3 color = fragColor.rgb * mix(0.65, 1.15, cloud);
    if (fire == 1) {
        float core = 1.0 - smoothstep(0.0, 0.8, length(p));
        color = mix(vec3(1.0, 0.18, 0.015), vec3(1.0, 0.9, 0.45), core * cloud) * fragColor.rgb;
    }
    finalColor = vec4(color, fragColor.a * edge * mix(0.65, 1.0, cloud));
})glsl";
} // namespace

CarRenderer::CarRenderer() {
    shader_ = LoadShaderFromMemory(vertex_shader, fragment_shader.c_str());
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
    camera_location_ = GetShaderLocation(shader_, "cameraPosition");
    emission_location_ = GetShaderLocation(shader_, "emissionColor");
    mirrored_location_ = GetShaderLocation(shader_, "mirrored");
    damage_shader_ = LoadShaderFromMemory(nullptr, damage_fragment);
    damage_time_location_ = GetShaderLocation(damage_shader_, "effectTime");
    damage_fire_location_ = GetShaderLocation(damage_shader_, "fire");
}

std::filesystem::path car_asset_directory(const char* folder) {
    const auto source = std::filesystem::path("assets")/folder;
    std::error_code ec;
    return std::filesystem::is_directory(source,ec) ? source : std::filesystem::path(GetApplicationDirectory())/source;
}
const CarRenderer::Asset* CarRenderer::asset(const std::string& name,const std::string& folder) const {
    const bool wheel=folder=="wheels";
    const std::string key = folder+"/"+name;
    if (auto found = assets_.find(key); found!=assets_.end()) return found->second.model.meshCount ? &found->second : nullptr;
    Asset result; const auto path = car_asset_directory(folder.c_str())/name;
    std::ifstream file(path,std::ios::binary); std::uint32_t header[5]{};
    std::error_code ec; const auto size = std::filesystem::file_size(path,ec);
    if (!name.empty() && !ec && size>=24 && size<=128*1024*1024 && file.read(reinterpret_cast<char*>(header),sizeof(header))
        && header[0]==0x46546c67 && header[1]==2 && header[2]==size && header[3]>0 && header[3]<=size-20 && header[4]==0x4e4f534a) {
        result.model = LoadModel(path.string().c_str());
        bool valid = result.model.meshCount>0 && result.model.meshes && result.model.materials && result.model.meshMaterial;
        for (int i = 0; valid && i<result.model.meshCount; ++i) {
            const auto& mesh = result.model.meshes[i]; const int material = result.model.meshMaterial[i];
            valid = mesh.vertices && mesh.vertexCount>0 && material>=0 && material<result.model.materialCount;
            for (int j = 0; valid && j<mesh.vertexCount*3; ++j) valid = std::isfinite(mesh.vertices[j]);
        }
        if (valid) {
            const auto bounds = GetModelBoundingBox(result.model);
            result.center = Vector3Scale(Vector3Add(bounds.min,bounds.max),.5f);
            result.size = Vector3Subtract(bounds.max,bounds.min);
            result.axis = result.size.y<result.size.x ? 1 : 0;
            if (result.size.z<(result.axis==0 ? result.size.x : result.size.y)) result.axis = 2;
            const Matrix align = result.axis==1 ? MatrixRotateZ(-PI/2) : result.axis==2 ? MatrixRotateY(PI/2) : MatrixIdentity();
            for (int i = 0; wheel && i<result.model.meshCount; ++i) for (int v = 0; v<result.model.meshes[i].vertexCount; ++v) {
                const float* p = result.model.meshes[i].vertices+v*3;
                const auto q = Vector3Transform(Vector3Subtract({p[0],p[1],p[2]},result.center),align);
                result.radius = std::max(result.radius,std::hypot(q.y,q.z));
            }
            valid = std::isfinite(result.center.x) && std::isfinite(result.center.y) && std::isfinite(result.center.z)
                && std::isfinite(result.size.x) && std::isfinite(result.size.y) && std::isfinite(result.size.z)
                && (wheel ? std::isfinite(result.radius) && result.radius>.0001f : std::min({result.size.x,result.size.y,result.size.z})>.0001f);
            if (valid) for (int i = 0; i<result.model.materialCount; ++i) result.model.materials[i].shader = shader_;
        }
        if (!valid) { if (result.model.meshes || result.model.materials) unload_model(result.model); result = {}; }
    }
    auto& stored = assets_.emplace(key,result).first->second;
    return stored.model.meshCount ? &stored : nullptr;
}
bool CarRenderer::available(const CarDesign& design,std::string& error) const {
    if (!design.validate(error)) return false;
    if (!asset(design.body,"cars")) { error = "Missing or invalid body GLB: "+design.body; return false; }
    if (!asset(design.wheel,"wheels")) { error = "Missing or invalid wheel GLB: "+design.wheel; return false; }
    return true;
}
CarRenderer::~CarRenderer() {
    refresh();
    if (shader_.id != 0) UnloadShader(shader_);
    if (damage_shader_.id != 0) UnloadShader(damage_shader_);
}
void CarRenderer::refresh() {
    for (const auto& entry : assets_) if (entry.second.model.meshCount) unload_model(entry.second.model);
    assets_.clear();
}

void CarRenderer::draw_damage(const Camera3D& camera, Vec3 center, bool wreck, float age, float size, float time) const {
    if (wreck && age >= 8) return;
    const Texture2D white{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    const Vector3 direction = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(direction, camera.up));
    const Vector3 up = Vector3CrossProduct(right, direction);
    const float phase = wreck ? age : time;
    const auto puff = [&](Vec3 p, float radius, Color tint) {
        DrawBillboardPro(camera, white, {0, 0, 1, 1}, {p.GetX(), p.GetY(), p.GetZ()}, up,
            {radius * 2, radius * 2}, {.5f, .5f}, 0, tint);
    };
    BeginShaderMode(damage_shader_);
    SetShaderValue(damage_shader_, damage_time_location_, &phase, SHADER_UNIFORM_FLOAT);
    // Keep opaque scene occlusion, but let translucent puffs overlap without cutting each other off.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    int fire = 1;
    if (wreck && age < .8f) {
        SetShaderValue(damage_shader_, damage_fire_location_, &fire, SHADER_UNIFORM_INT);
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * .78539816f;
            const Vec3 p = center + Vec3(std::cos(angle), .5f, std::sin(angle)) * (age * 4 * size);
            puff(p, (.6f + age * 2.5f) * size, Fade(WHITE, .45f * std::pow(1 - age / .8f, 2)));
        }
        EndBlendMode();
    }
    fire = 0;
    SetShaderValue(damage_shader_, damage_fire_location_, &fire, SHADER_UNIFORM_INT);
    struct Smoke { Vec3 p; float radius, alpha; };
    std::array<Smoke, 6> smoke;
    for (int i = 0; i < int(smoke.size()); ++i) {
        const float rise = std::fmod(phase * .7f + i / 6.f, 1.f), angle = i * 2.4f;
        smoke[i] = {center + Vec3(std::cos(angle) * rise, .7f + rise * 4, std::sin(angle) * rise) * size,
            (.25f + rise * (wreck ? 1.3f : .6f)) * size,
            std::sin(rise * PI) * .65f * (wreck ? 1 - age / 8 : 1)};
    }
    const Vec3 eye(camera.position.x, camera.position.y, camera.position.z);
    std::sort(smoke.begin(), smoke.end(), [&](const Smoke& a, const Smoke& b) { return (a.p - eye).LengthSq() > (b.p - eye).LengthSq(); });
    for (const auto& s : smoke) puff(s.p, s.radius, Fade(wreck ? Color{48, 44, 40, 255} : GRAY, s.alpha));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndShaderMode();
}

bool CarRenderer::body(const CarDesign& design,Vec3 position,Quat rotation,const Camera3D& camera,Shader override_shader,bool intact) const {
    const auto* source = asset(design.body,"cars"); if (!source) return false;
    Matrix transform = MatrixMultiply(MatrixTranslate(-source->center.x,-source->center.y,-source->center.z),
        MatrixScale(design.width/source->size.x,design.height/source->size.y,design.length/source->size.z));
    // glTF fronts face +Z; cars drive toward -Z in this world.
    transform = MatrixMultiply(transform,MatrixRotateY(PI));
    transform = MatrixMultiply(transform,MatrixTranslate(design.offset[0],design.height/2-.15f+design.offset[1],design.offset[2]));
    transform = MatrixMultiply(transform,QuaternionToMatrix({rotation.GetX(),rotation.GetY(),rotation.GetZ(),rotation.GetW()}));
    transform = MatrixMultiply(transform,MatrixTranslate(position.GetX(),position.GetY(),position.GetZ()));
    draw_model(source->model,transform,camera,override_shader,intact); return true;
}
bool CarRenderer::tire(const CarDesign& design,Vec3 position,Quat rotation,const Camera3D& camera,Shader override_shader,bool intact) const {
    const auto* source = asset(design.wheel,"wheels"); if (!source) return false;
    const Matrix align = source->axis==1 ? MatrixRotateZ(-PI/2) : source->axis==2 ? MatrixRotateY(PI/2) : MatrixIdentity();
    Matrix transform = MatrixMultiply(MatrixTranslate(-source->center.x,-source->center.y,-source->center.z),align);
    const float scale = design.tuning.wheel_radius/source->radius;
    transform = MatrixMultiply(transform,MatrixScale(scale,scale,scale));
    transform = MatrixMultiply(transform,QuaternionToMatrix({rotation.GetX(),rotation.GetY(),rotation.GetZ(),rotation.GetW()}));
    transform = MatrixMultiply(transform,MatrixTranslate(position.GetX(),position.GetY(),position.GetZ()));
    draw_model(source->model,transform,camera,override_shader,intact); return true;
}
bool CarRenderer::draw_body(const Car& car,const Camera3D& camera,Shader override_shader) const {
    return car.design() && body(*car.design(),car.position(),car.rotation(),camera,override_shader,!car.destroyed());
}
bool CarRenderer::draw_wheel(const Car& car,const Wheel& wheel,const Camera3D& camera,Shader override_shader) const {
    if (!car.design()) return false;
    const Quat side = Quat::sRotation(Vec3::sAxisY(),wheel.mount.GetX()<0 ? PI : 0);
    const Quat spin = Quat::sRotation(Vec3::sAxisX(),-std::remainder(wheel.spin,2*PI));
    const Quat steering = Quat::sRotation(Vec3::sAxisY(),wheel.front ? car.steering() : 0);
    return tire(*car.design(),car.wheel_center(wheel),car.rotation()*steering*spin*side,camera,override_shader,!car.destroyed());
}
void CarRenderer::draw_design(const CarDesign& design,Vec3 ground,float yaw,const Camera3D& camera) const {
    const Quat rotation = Quat::sRotation(Vec3::sAxisY(),yaw);
    const float ride = design.tuning.rest_length+design.tuning.wheel_radius-design.tuning.mount_height;
    body(design,ground+Vec3(0,ride,0),rotation,camera);
    for (int i = 0; i<4; ++i) {
        const Vec3 position = ground+rotation*Vec3((i%2 ? 1 : -1)*design.tuning.track_width/2,design.tuning.wheel_radius,(i<2 ? -1 : 1)*design.tuning.wheelbase/2);
        tire(design,position,rotation*Quat::sRotation(Vec3::sAxisY(),i%2 ? 0 : PI),camera);
    }
}

bool CarRenderer::model_bounds(const std::string& folder, const std::string& name, Vector3& center, Vector3& size) const {
    const auto* source=asset(name,folder); if (!source) return false;
    center=source->center; size=source->size; return true;
}
void CarRenderer::draw_imported(const std::string& folder, const std::string& name, const Matrix& transform, const Camera3D& camera, Shader override_shader, bool mirrored) const {
    if (const auto* source=asset(name,folder)) draw_model(source->model,transform,camera,override_shader,true,mirrored);
}
void CarRenderer::draw_model(const Model& model, const Matrix& transform, const Camera3D& camera, Shader override_shader, bool intact, bool mirrored) const {
    if (!override_shader.id) SetShaderValue(shader_, camera_location_, &camera.position, SHADER_UNIFORM_VEC3);
    const int mirror=mirrored ? 1 : 0;
    if (!override_shader.id) SetShaderValue(shader_,mirrored_location_,&mirror,SHADER_UNIFORM_INT);
    // Imported cars may contain thin, double-sided body panels.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int i = 0; i < model.meshCount; ++i) {
        auto& material = model.materials[model.meshMaterial[i]];
        const Shader original_shader = material.shader;
        if (override_shader.id) material.shader = override_shader;
        const Color original = material.maps[MATERIAL_MAP_DIFFUSE].color;
        if (!intact) material.maps[MATERIAL_MAP_DIFFUSE].color = {18, 18, 18, 255};
        const auto color = intact ? material.maps[MATERIAL_MAP_EMISSION].color : BLANK;
        const Vector3 emission{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
        if (!override_shader.id) SetShaderValue(shader_, emission_location_, &emission, SHADER_UNIFORM_VEC3);
        DrawMesh(model.meshes[i], material, transform);
        material.maps[MATERIAL_MAP_DIFFUSE].color = original;
        material.shader = original_shader;
    }
    rlEnableBackfaceCulling();
}
} // namespace ambaretto
