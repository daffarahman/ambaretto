#include "building_renderer.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace ambaretto {
namespace {
constexpr unsigned int array_target = 0x8C1A, rgba = 0x1908, rgba8 = 0x8058, byte_type = 0x1401;
constexpr int array_slot = 14, clamp_slot = 13;
struct ArrayGL {
    void (*generate)(int,unsigned int*) = nullptr;
    void (*remove)(int,const unsigned int*) = nullptr;
    void (*bind)(unsigned int,unsigned int) = nullptr;
    void (*image)(unsigned int,int,int,int,int,int,int,unsigned int,unsigned int,const void*) = nullptr;
    void (*sub_image)(unsigned int,int,int,int,int,int,int,int,unsigned int,unsigned int,const void*) = nullptr;
    void (*parameter)(unsigned int,unsigned int,int) = nullptr;
    void (*mipmap)(unsigned int) = nullptr;
    void (*integer)(unsigned int,int*) = nullptr;
    unsigned int (*error)() = nullptr;
    void (*generate_samplers)(int,unsigned int*) = nullptr;
    void (*remove_samplers)(int,const unsigned int*) = nullptr;
    void (*bind_sampler)(unsigned int,unsigned int) = nullptr;
    void (*sampler_parameter)(unsigned int,unsigned int,int) = nullptr;
    bool load(std::string& message) {
        if (!IsWindowReady()) { message = "Building rendering needs an active OpenGL window."; return false; }
        if (rlGetVersion()!=RL_OPENGL_33 && rlGetVersion()!=RL_OPENGL_43) {
            message = "Building texture arrays require desktop OpenGL 3.3."; return false;
        }
        generate = reinterpret_cast<decltype(generate)>(rlGetProcAddress("glGenTextures"));
        remove = reinterpret_cast<decltype(remove)>(rlGetProcAddress("glDeleteTextures"));
        bind = reinterpret_cast<decltype(bind)>(rlGetProcAddress("glBindTexture"));
        image = reinterpret_cast<decltype(image)>(rlGetProcAddress("glTexImage3D"));
        sub_image = reinterpret_cast<decltype(sub_image)>(rlGetProcAddress("glTexSubImage3D"));
        parameter = reinterpret_cast<decltype(parameter)>(rlGetProcAddress("glTexParameteri"));
        mipmap = reinterpret_cast<decltype(mipmap)>(rlGetProcAddress("glGenerateMipmap"));
        integer = reinterpret_cast<decltype(integer)>(rlGetProcAddress("glGetIntegerv"));
        error = reinterpret_cast<decltype(error)>(rlGetProcAddress("glGetError"));
        generate_samplers = reinterpret_cast<decltype(generate_samplers)>(rlGetProcAddress("glGenSamplers"));
        remove_samplers = reinterpret_cast<decltype(remove_samplers)>(rlGetProcAddress("glDeleteSamplers"));
        bind_sampler = reinterpret_cast<decltype(bind_sampler)>(rlGetProcAddress("glBindSampler"));
        sampler_parameter = reinterpret_cast<decltype(sampler_parameter)>(rlGetProcAddress("glSamplerParameteri"));
        int units = 0; if (integer) integer(0x8872,&units);
        if (!generate || !remove || !bind || !image || !sub_image || !parameter || !mipmap || !integer || !error
            || !generate_samplers || !remove_samplers || !bind_sampler || !sampler_parameter || units<16) {
            message = "Building texture arrays unavailable: OpenGL functions or 16 texture units are missing."; return false;
        }
        return true;
    }
};
const char* building_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 position;
out vec3 normal;
out vec2 texCoord;
out vec4 color;
flat out float layer;
flat out int repeatUV;
void main() {
    position = vertexPosition;
    normal = vertexNormal;
    texCoord = vertexTexCoord;
    color = vertexColor;
    layer = vertexTexCoord2.x;
    repeatUV = int(vertexTexCoord2.y);
    gl_Position = mvp * vec4(vertexPosition,1.0);
})GLSL";
const char* array_sample = R"GLSL(
uniform sampler2DArray buildingTexture;
uniform sampler2DArray buildingClampedTexture;
vec4 building_surface(vec2 uv,float layer,int repeatUV) {
    return repeatUV!=0 ? texture(buildingTexture,vec3(uv,layer)) : texture(buildingClampedTexture,vec3(uv,layer));
}
)GLSL";
const std::string building_fragment = std::string(R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
flat in float layer;
flat in int repeatUV;
uniform vec4 colDiffuse;
uniform vec3 cameraPosition;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform vec3 horizonColor;
uniform float daylight;
out vec4 finalColor;
)GLSL") + array_sample + scene_lighting_glsl() + R"GLSL(
void main() {
    vec4 surface = building_surface(texCoord,layer,repeatUV) * color * colDiffuse;
    if (surface.a < 0.3) discard;
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    vec3 light = ambientLight + sunColor * (0.42 * daylight * max(dot(n,sunDirection),0.0) * scene_shadow(position,n,sunDirection));
    light += scene_local_light(position,n,normalize(cameraPosition-position),0.0);
    float fog = smoothstep(viewDistance*0.65,viewDistance,distance(position.xz,cameraPosition.xz));
    finalColor = vec4(mix(surface.rgb*light,horizonColor,fog)*brightness,1.0);
})GLSL";
const std::string building_depth = std::string(R"GLSL(#version 330
in vec2 texCoord;
in vec4 color;
flat in float layer;
flat in int repeatUV;
uniform vec4 colDiffuse;
out vec4 finalColor;
)GLSL") + array_sample + R"GLSL(
void main() {
    if (building_surface(texCoord,layer,repeatUV).a*color.a*colDiffuse.a < 0.3) discard;
    finalColor = vec4(gl_FragCoord.z,0.0,0.0,1.0);
})GLSL";
const std::string thumbnail_fragment = std::string(R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform float thumbnailLayer;
out vec4 finalColor;
)GLSL") + array_sample + R"GLSL(
void main() {
    finalColor = building_surface(fragTexCoord,thumbnailLayer,0)*fragColor;
})GLSL";
std::size_t mip_bytes(int width,int height) {
    std::size_t bytes = 0;
    while (true) {
        bytes += std::size_t(width)*height*4;
        if (width==1 && height==1) return bytes;
        width = std::max(1,width/2); height = std::max(1,height/2);
    }
}
struct ArrayPage { unsigned int id = 0; int width = 0, height = 0, layers = 0; };
struct TextureEntry { int page = 0, layer = 0; };
struct TextureBank {
    ArrayGL gl;
    Shader shader{}, depth{}, thumbnail{};
    std::vector<ArrayPage> pages;
    std::map<std::string,TextureEntry> entries;
    std::map<std::string,std::string> sources;
    std::set<std::string> skipped;
    std::size_t bytes = 0;
    std::string directory, warning;
    unsigned int samplers[2]{};
    ~TextureBank() {
        for (const auto& page : pages) if (page.id) gl.remove(1,&page.id);
        if (samplers[0] || samplers[1]) gl.remove_samplers(2,samplers);
        for (auto shader : {this->shader,depth,thumbnail}) if (shader.id && shader.id!=rlGetShaderIdDefault()) UnloadShader(shader);
    }
    void bind(int page) const {
        rlActiveTextureSlot(array_slot); gl.bind(array_target,pages[page].id); rlActiveTextureSlot(0);
        gl.bind_sampler(array_slot,samplers[0]);
        rlActiveTextureSlot(clamp_slot); gl.bind(array_target,pages[page].id); gl.bind_sampler(clamp_slot,samplers[1]);
        rlActiveTextureSlot(0);
    }
    void unbind() const {
        rlActiveTextureSlot(array_slot); gl.bind(array_target,0); rlActiveTextureSlot(0);
        gl.bind_sampler(array_slot,0);
        rlActiveTextureSlot(clamp_slot); gl.bind(array_target,0); gl.bind_sampler(clamp_slot,0); rlActiveTextureSlot(0);
    }
};
struct Images {
    struct Item { Image image{}; std::string name; int page = 0, layer = 0; };
    std::vector<Item> items;
    ~Images() { for (const auto& item : items) if (item.image.data) UnloadImage(item.image); }
};
std::shared_ptr<TextureBank> texture_bank(const std::set<std::string>& names,const std::set<std::string>& required,std::string& error) {
    const auto directory = std::filesystem::absolute(building_texture_directory());
    std::string key = directory.string();
    std::map<std::string,std::string> sources;
    for (const auto& name : names) {
        if (!valid_texture_filename(name)) { error = "Invalid building texture filename."; return {}; }
        std::error_code ec; const auto path = directory/name;
        const auto stamp = std::filesystem::last_write_time(path,ec);
        const auto modified = ec ? "missing" : std::to_string(stamp.time_since_epoch().count());
        const auto size = std::filesystem::file_size(path,ec);
        sources[name] = modified+":"+(ec ? "missing" : std::to_string(size));
        key += "\n"+name+":"+sources[name]+(required.count(name) ? ":required" : ":thumbnail");
    }
    // Editor rebakes share the same GPU bank until the image files or texture set change.
    static std::map<std::string,std::weak_ptr<TextureBank>> cache;
    for (auto it = cache.begin(); it!=cache.end();) if (it->second.expired()) it = cache.erase(it); else ++it;
    if (const auto found = cache.find(key); found!=cache.end()) if (auto bank = found->second.lock()) return bank;
    for (const auto& cached : cache) if (auto bank = cached.second.lock()) {
        if (bank->directory!=directory.string()) continue;
        bool compatible = true;
        for (const auto& source : sources) {
            const auto found = bank->sources.find(source.first);
            if (found==bank->sources.end() || found->second!=source.second
                || (required.count(source.first) && bank->skipped.count(source.first))) { compatible = false; break; }
        }
        if (compatible) return bank;
    }
    auto bank = std::make_shared<TextureBank>();
    bank->directory = directory.string(); bank->sources = std::move(sources);
    if (!bank->gl.load(error)) return {};
    int max_size = 0, max_layers = 0;
    bank->gl.integer(0x0D33,&max_size); bank->gl.integer(0x88FF,&max_layers);
    if (max_size<1 || max_layers<1) { error = "GPU reported invalid building texture array limits."; return {}; }
    std::map<std::pair<int,int>,int> groups;
    Images images;
    std::vector<std::string> ordered(required.begin(),required.end());
    for (const auto& name : names) if (!required.count(name)) ordered.push_back(name);
    for (const auto& name : ordered) {
        const auto path = (directory/name).string();
        Image image = FileExists(path.c_str()) ? LoadImage(path.c_str()) : Image{};
        if (!image.data) {
            if (!bank->warning.empty()) bank->warning += "; ";
            bank->warning += "Missing texture: "+name;
            continue;
        }
        if (image.width<1 || image.height<1 || image.width>max_size || image.height>max_size) {
            UnloadImage(image);
            if (required.count(name)) { error = "Building texture exceeds the GPU's texture size limit: "+name; return {}; }
            bank->skipped.insert(name);
            if (!bank->warning.empty()) bank->warning += "; ";
            bank->warning += "Thumbnail exceeds GPU dimensions: "+name; continue;
        }
        const auto bytes = mip_bytes(image.width,image.height);
        if (bytes>BuildingRenderer::texture_budget-4-bank->bytes) {
            UnloadImage(image);
            if (required.count(name)) { error = "Building textures exceed the 8 MiB GPU budget. Use smaller source textures: "+name; return {}; }
            bank->skipped.insert(name);
            if (!bank->warning.empty()) bank->warning += "; ";
            bank->warning += "Thumbnail omitted to keep the GPU budget: "+name; continue;
        }
        images.items.push_back({image,name});
        bank->bytes += bytes;
        auto group = groups.find({image.width,image.height});
        int page = group==groups.end() ? -1 : group->second;
        if (page<0 || bank->pages[page].layers==max_layers) {
            page = int(bank->pages.size()); bank->pages.push_back({0,image.width,image.height,0});
            groups[{image.width,image.height}] = page;
        }
        const int layer = bank->pages[page].layers++;
        auto& item = images.items.back(); item.page = page; item.layer = layer;
        ImageFormat(&item.image,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (!item.image.data) { error = "Could not convert building texture: "+name; return {}; }
        bank->entries[name] = {page,layer};
    }
    // A white layer supplies the existing grey fallback without another material or draw.
    int fallback = -1; std::size_t fallback_bytes = BuildingRenderer::texture_budget+1;
    for (int i = 0; i<int(bank->pages.size()); ++i) {
        const auto& page = bank->pages[i]; const auto bytes = mip_bytes(page.width,page.height);
        if (page.layers<max_layers && bytes<fallback_bytes) { fallback = i; fallback_bytes = bytes; }
    }
    if (fallback<0 || fallback_bytes>BuildingRenderer::texture_budget-bank->bytes) {
        fallback = int(bank->pages.size()); bank->pages.push_back({0,1,1,0});
    }
    auto& white = bank->pages[fallback];
    const auto white_bytes = mip_bytes(white.width,white.height);
    if (white_bytes>BuildingRenderer::texture_budget-bank->bytes) {
        error = "Building textures leave no room for the fallback layer in the 8 MiB GPU budget."; return {};
    }
    bank->bytes += white_bytes;
    bank->entries[""] = {fallback,white.layers++};
    images.items.push_back({GenImageColor(white.width,white.height,WHITE),"",fallback,white.layers-1});
    bank->shader = LoadShaderFromMemory(building_vertex,building_fragment.c_str());
    bank->depth = LoadShaderFromMemory(building_vertex,building_depth.c_str());
    bank->thumbnail = LoadShaderFromMemory(nullptr,thumbnail_fragment.c_str());
    for (auto shader : {bank->shader,bank->depth,bank->thumbnail}) {
        if (!IsShaderValid(shader) || shader.id==rlGetShaderIdDefault()) {
            error = "The graphics driver could not compile the building array shader."; return {};
        }
        const int location = GetShaderLocation(shader,"buildingTexture");
        const int slot = array_slot; SetShaderValue(shader,location,&slot,SHADER_UNIFORM_INT);
        const int clamp = clamp_slot;
        SetShaderValue(shader,GetShaderLocation(shader,"buildingClampedTexture"),&clamp,SHADER_UNIFORM_INT);
    }
    rlDrawRenderBatchActive();
    for (int i = 0; i<16 && bank->gl.error()!=0; ++i) {}
    // Two sampler states share one texture allocation: native repeating walls and clamped decals.
    bank->gl.generate_samplers(2,bank->samplers);
    for (int i = 0; i<2; ++i) {
        bank->gl.sampler_parameter(bank->samplers[i],0x2801,0x2703);
        bank->gl.sampler_parameter(bank->samplers[i],0x2800,0x2601);
        bank->gl.sampler_parameter(bank->samplers[i],0x2802,i==0 ? 0x2901 : 0x812F);
        bank->gl.sampler_parameter(bank->samplers[i],0x2803,i==0 ? 0x2901 : 0x812F);
    }
    rlActiveTextureSlot(array_slot);
    for (auto& page : bank->pages) {
        bank->gl.generate(1,&page.id); bank->gl.bind(array_target,page.id);
        bank->gl.image(array_target,0,rgba8,page.width,page.height,page.layers,0,rgba,byte_type,nullptr);
        bank->gl.parameter(array_target,0x2801,0x2703); // GL_LINEAR_MIPMAP_LINEAR
        bank->gl.parameter(array_target,0x2800,0x2601); // GL_LINEAR
        bank->gl.parameter(array_target,0x2802,0x2901); // GL_REPEAT
        bank->gl.parameter(array_target,0x2803,0x2901);
    }
    for (const auto& item : images.items) {
        bank->gl.bind(array_target,bank->pages[item.page].id);
        bank->gl.sub_image(array_target,0,0,0,item.layer,item.image.width,item.image.height,1,rgba,byte_type,item.image.data);
    }
    for (const auto& page : bank->pages) { bank->gl.bind(array_target,page.id); bank->gl.mipmap(array_target); }
    const auto gpu_error = bank->gl.error();
    bank->unbind();
    if (gpu_error) { error = "GPU could not allocate the building texture arrays (OpenGL error "+std::to_string(gpu_error)+")."; return {}; }
    cache[key] = bank;
    return bank;
}
struct Geometry {
    std::vector<float> positions, normals, uv, layers;
    std::vector<unsigned char> colors;
    void triangle(const BuildingTriangle& triangle,TextureEntry entry,bool textured) {
        const Vec3 normal = (triangle.points[1]-triangle.points[0]).Cross(triangle.points[2]-triangle.points[0]).Normalized();
        for (int i = 0; i<3; ++i) {
            const auto& p = triangle.points[i];
            positions.insert(positions.end(),{p.GetX(),p.GetY(),p.GetZ()});
            normals.insert(normals.end(),{normal.GetX(),normal.GetY(),normal.GetZ()});
            uv.insert(uv.end(),triangle.uv[i].begin(),triangle.uv[i].end());
            layers.insert(layers.end(),{float(entry.layer),triangle.repeat && !triangle.decal ? 1.f : 0.f});
            const Color tint = textured ? WHITE : Color{197,204,211,255};
            colors.insert(colors.end(),{tint.r,tint.g,tint.b,tint.a});
        }
    }
    Model upload() const {
        Mesh mesh{}; mesh.vertexCount = int(positions.size()/3); mesh.triangleCount = mesh.vertexCount/3;
        const auto copy = [](const std::vector<float>& source) {
            auto* data = static_cast<float*>(MemAlloc(unsigned(source.size()*sizeof(float))));
            if (data) std::memcpy(data,source.data(),source.size()*sizeof(float));
            return data;
        };
        mesh.vertices = copy(positions); mesh.normals = copy(normals); mesh.texcoords = copy(uv); mesh.texcoords2 = copy(layers);
        mesh.colors = static_cast<unsigned char*>(MemAlloc(unsigned(colors.size())));
        if (mesh.colors) std::memcpy(mesh.colors,colors.data(),colors.size());
        if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.texcoords2 || !mesh.colors) {
            UnloadMesh(mesh); return {};
        }
        UploadMesh(&mesh,false); return LoadModelFromMesh(mesh);
    }
};
bool nearby(Vector3 center,float radius,Vector3 focus,float distance) {
    const float dx = center.x-focus.x, dz = center.z-focus.z, range = distance+radius;
    return dx*dx+dz*dz<=range*range;
}
} // namespace
struct BuildingRenderer::State {
    struct Chunk { Model model{}; int page = 0; Vector3 center{}; float radius = 0; };
    std::shared_ptr<TextureBank> textures;
    std::vector<Chunk> chunks;
    BuildingRenderStats stats;
    ~State() { for (auto& chunk : chunks) UnloadModel(chunk.model); }
};
std::filesystem::path building_texture_directory() {
    const std::filesystem::path source = "assets/textures"; std::error_code ec;
    return std::filesystem::is_directory(source,ec) ? source : std::filesystem::path(GetApplicationDirectory())/source;
}
BuildingRenderer::BuildingRenderer() : state_(std::make_unique<State>()) {}
BuildingRenderer::~BuildingRenderer() = default;
const BuildingRenderStats& BuildingRenderer::stats() const { return state_->stats; }
const std::string& BuildingRenderer::warning() const {
    static const std::string empty;
    return state_->textures ? state_->textures->warning : empty;
}
bool BuildingRenderer::build(const std::vector<BuildingTriangle>& triangles,std::string& error,bool spatial_chunks,
                             const std::vector<std::string>& thumbnail_textures) {
    const auto started = std::chrono::steady_clock::now();
    auto next = std::make_unique<State>(); error.clear();
    std::set<std::string> names(thumbnail_textures.begin(),thumbnail_textures.end());
    std::set<std::string> required;
    for (const auto& triangle : triangles) if (!triangle.texture.empty()) { names.insert(triangle.texture); required.insert(triangle.texture); }
    if (triangles.empty() && names.empty()) { state_ = std::move(next); return true; }
    next->textures = texture_bank(names,required,error);
    if (!next->textures) return false;
    std::map<std::tuple<int,int,int>,Geometry> groups;
    for (const auto& triangle : triangles) {
        const auto cross = (triangle.points[1]-triangle.points[0]).Cross(triangle.points[2]-triangle.points[0]);
        if (!std::isfinite(cross.LengthSq()) || cross.LengthSq()<1e-10f) continue;
        const auto found = next->textures->entries.find(triangle.texture);
        const bool textured = !triangle.texture.empty() && found!=next->textures->entries.end();
        if (triangle.decal && !textured) continue;
        if (next->stats.gpu_geometry_bytes+132>geometry_budget) {
            error = "Building geometry exceeds the 8 MiB GPU budget. Reduce the parts or decal count."; return false;
        }
        const auto entry = textured ? found->second : next->textures->entries.at("");
        const Vec3 center = (triangle.points[0]+triangle.points[1]+triangle.points[2])/3;
        const int x = spatial_chunks ? int(std::floor(center.GetX()/256)) : 0;
        const int z = spatial_chunks ? int(std::floor(center.GetZ()/256)) : 0;
        groups[{entry.page,x,z}].triangle(triangle,entry,textured);
        ++next->stats.emitted_triangles; next->stats.vertices += 3; next->stats.gpu_geometry_bytes += 132;
    }
    for (int i = 0; i<16 && next->textures->gl.error()!=0; ++i) {}
    for (const auto& group : groups) {
        auto model = group.second.upload();
        const auto gpu_error = next->textures->gl.error();
        if (!model.meshes || !model.meshes[0].vaoId || !model.materials || gpu_error) {
            if (model.meshes || model.materials) UnloadModel(model);
            error = "GPU could not upload the building mesh (OpenGL error "+std::to_string(gpu_error)+")."; return false;
        }
        model.materials[0].shader = next->textures->shader;
        const auto bounds = GetModelBoundingBox(model);
        const auto center = Vector3Scale(Vector3Add(bounds.min,bounds.max),.5f);
        next->chunks.push_back({model,std::get<0>(group.first),center,std::hypot(bounds.max.x-center.x,bounds.max.z-center.z)});
    }
    next->stats.cpu_geometry_bytes = next->stats.gpu_geometry_bytes;
    next->stats.gpu_texture_bytes = next->textures->bytes;
    next->stats.array_pages = next->textures->pages.size(); next->stats.meshes = next->chunks.size();
    next->stats.bake_ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    state_ = std::move(next); return true;
}
void BuildingRenderer::draw(const Camera3D& camera,const Daylight& daylight,const SceneLighting& lighting,const GraphicsSettings& settings) {
    state_->stats.color_draws = 0;
    if (!state_->textures) return;
    lighting.apply(state_->textures->shader,camera,daylight,settings);
    rlDrawRenderBatchActive();
    int bound = -1;
    for (const auto& chunk : state_->chunks) if (nearby(chunk.center,chunk.radius,camera.position,settings.view_distance)) {
        if (bound!=chunk.page) { state_->textures->bind(chunk.page); bound = chunk.page; }
        DrawModel(chunk.model,{0,0,0},1,WHITE); ++state_->stats.color_draws;
    }
    state_->textures->unbind();
}
void BuildingRenderer::draw_shadow(Vector3 focus,float distance) {
    state_->stats.shadow_draws = 0;
    if (!state_->textures) return;
    rlDrawRenderBatchActive();
    int bound = -1;
    for (const auto& chunk : state_->chunks) if (nearby(chunk.center,chunk.radius,focus,distance)) {
        if (bound!=chunk.page) { state_->textures->bind(chunk.page); bound = chunk.page; }
        auto material = chunk.model.materials[0]; material.shader = state_->textures->depth;
        DrawMesh(chunk.model.meshes[0],material,chunk.model.transform); ++state_->stats.shadow_draws;
    }
    state_->textures->unbind();
}
bool BuildingRenderer::thumbnail(const std::string& filename,Rectangle destination) {
    if (!state_->textures) return false;
    const auto found = state_->textures->entries.find(filename);
    if (found==state_->textures->entries.end()) return false;
    rlDrawRenderBatchActive(); state_->textures->bind(found->second.page);
    const float layer = float(found->second.layer);
    SetShaderValue(state_->textures->thumbnail,GetShaderLocation(state_->textures->thumbnail,"thumbnailLayer"),&layer,SHADER_UNIFORM_FLOAT);
    BeginShaderMode(state_->textures->thumbnail);
    const Texture2D white{rlGetTextureIdDefault(),1,1,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    DrawTexturePro(white,{0,0,1,1},destination,{0,0},0,WHITE);
    EndShaderMode(); state_->textures->unbind(); return true;
}
} // namespace ambaretto
