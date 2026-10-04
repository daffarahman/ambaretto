#include "ui_font.hpp"
#include "environment_renderer.hpp"
#include "police.hpp"
#include "airport.hpp"
#include "minimap.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace forza {
namespace {
constexpr float pi = 3.14159265359f;
constexpr int sign_rows = 11;
constexpr Color map_water{24, 44, 70, 255};
const char* minimap_fragment = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 clipBounds;
out vec4 finalColor;
void main() {
    const float radius = 14.0;
    vec2 q = abs(gl_FragCoord.xy - clipBounds.xy - clipBounds.zw * 0.5) - (clipBounds.zw * 0.5 - radius);
    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float coverage = 1.0 - smoothstep(-0.5, 0.5, distance);
    if (coverage <= 0.0) discard;
    finalColor = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    finalColor.a *= coverage;
})GLSL";
std::pair<int, int> cell(Vec3 position) {
    return {int(std::floor(position.GetX() / 256)), int(std::floor(position.GetZ() / 256))};
}
Texture2D load_terrain_texture(const char* filename) {
    const std::string relative = std::string("assets/textures/") + filename;
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    const std::string path = FileExists(bundled.c_str()) ? bundled : relative;
    Texture2D texture = LoadTexture(path.c_str());
    if (texture.id != 0) {
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}
struct MeshBuilder {
    std::vector<float> positions, normals, texcoords;
    std::vector<unsigned char> colors;
    void triangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
        const Vec3 cross = (b - a).Cross(c - a);
        if (cross.LengthSq() < 1.0e-10f) return;
        const Vec3 normal = cross.Normalized();
        for (const auto& p : {a, b, c}) {
            positions.insert(positions.end(), {p.GetX(), p.GetY(), p.GetZ()});
            normals.insert(normals.end(), {normal.GetX(), normal.GetY(), normal.GetZ()});
            colors.insert(colors.end(), {color.r, color.g, color.b, color.a});
            texcoords.insert(texcoords.end(), {0, 0});
        }
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color) {
        triangle(a, b, c, color); triangle(a, c, d, color);
    }
    void sign(Vec3 center, Vec3 right, float width, float height, int row) {
        const Vec3 up(0, height / 2, 0), side = right * (width / 2);
        const auto first = texcoords.size();
        quad(center - side - up, center + side - up, center + side + up, center - side + up, WHITE);
        const float top = float(row) / sign_rows, bottom = float(row + 1) / sign_rows;
        const float uv[] = {0, bottom, 1, bottom, 1, top, 0, bottom, 1, top, 0, top};
        std::copy(std::begin(uv), std::end(uv), texcoords.begin() + first);
    }
    void box(Vec3 center, Vec3 size, Color color, float yaw = 0, float pitch = 0) {
        const Vec3 h = size / 2;
        const Quat rotation = Quat::sRotation(Vec3::sAxisY(), yaw) * Quat::sRotation(Vec3::sAxisX(), pitch);
        const auto point = [&](Vec3 local) { return center + rotation * local; };
        const std::array<Vec3, 8> p = {
            point(Vec3(-h.GetX(), -h.GetY(), -h.GetZ())), point(Vec3(h.GetX(), -h.GetY(), -h.GetZ())),
            point(Vec3(h.GetX(), h.GetY(), -h.GetZ())), point(Vec3(-h.GetX(), h.GetY(), -h.GetZ())),
            point(Vec3(-h.GetX(), -h.GetY(), h.GetZ())), point(Vec3(h.GetX(), -h.GetY(), h.GetZ())),
            point(Vec3(h.GetX(), h.GetY(), h.GetZ())), point(Vec3(-h.GetX(), h.GetY(), h.GetZ()))};
        constexpr int faces[6][4] = {{0,3,2,1}, {4,5,6,7}, {0,4,7,3}, {1,2,6,5}, {3,7,6,2}, {0,1,5,4}};
        for (const auto& f : faces) quad(p[f[0]], p[f[1]], p[f[2]], p[f[3]], color);
    }
    void ribbon(const Environment& env, Vec3 a, Vec3 b, float width, Color color, float lift = 0.045f) {
        const Vec3 direction = (b - a).Normalized();
        const Vec3 side(-direction.GetZ() * width / 2, 0, direction.GetX() * width / 2);
        const std::array<Vec3, 4> edges = {a - side, a + side, b + side, b - side};
        float min_x = edges[0].GetX(), max_x = min_x, min_z = edges[0].GetZ(), max_z = min_z;
        for (const auto& p : edges) {
            min_x = std::min(min_x, p.GetX()); max_x = std::max(max_x, p.GetX());
            min_z = std::min(min_z, p.GetZ()); max_z = std::max(max_z, p.GetZ());
        }
        const auto cell = [](float value) {
            return std::clamp(int(std::floor((value + Environment::extent) / Environment::spacing)), 0, Environment::samples - 2);
        };
        // Clip the road footprint against each original terrain triangle.
        // Interpolating its vertices keeps roads exactly on the hillside;
        // a separate quad would cut through the terrain between samples.
        for (int z = cell(min_z); z <= cell(max_z); ++z) for (int x = cell(min_x); x <= cell(max_x); ++x) {
            const int base = z * Environment::samples + x;
            for (const auto& indices : {std::array<int, 3>{base, base + Environment::samples, base + 1},
                    std::array<int, 3>{base + 1, base + Environment::samples, base + Environment::samples + 1}}) {
                std::vector<Vec3> polygon{env.vertices()[indices[0]], env.vertices()[indices[1]], env.vertices()[indices[2]]};
                for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
                    const Vec3 start = edges[edge], delta = edges[(edge + 1) % 4] - start;
                    const auto signed_distance = [&](Vec3 p) {
                        return delta.GetX() * (p.GetZ() - start.GetZ()) - delta.GetZ() * (p.GetX() - start.GetX());
                    };
                    std::vector<Vec3> clipped;
                    Vec3 previous = polygon.back();
                    float previous_distance = signed_distance(previous);
                    for (const auto& current : polygon) {
                        const float current_distance = signed_distance(current);
                        const bool inside = current_distance <= 0, previous_inside = previous_distance <= 0;
                        if (inside != previous_inside) {
                            const float t = previous_distance / (previous_distance - current_distance);
                            clipped.push_back(previous + (current - previous) * t);
                        }
                        if (inside) clipped.push_back(current);
                        previous = current; previous_distance = current_distance;
                    }
                    polygon = std::move(clipped);
                }
                const Vec3 offset(0, lift, 0);
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                    triangle(polygon[0] + offset, polygon[i] + offset, polygon[i + 1] + offset, color);
            }
        }
    }
    Model upload() const {
        if (positions.empty()) return {};
        Mesh mesh{};
        mesh.vertexCount = int(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = static_cast<float*>(MemAlloc(unsigned(positions.size() * sizeof(float))));
        mesh.normals = static_cast<float*>(MemAlloc(unsigned(normals.size() * sizeof(float))));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(unsigned(colors.size())));
        mesh.texcoords = static_cast<float*>(MemAlloc(unsigned(texcoords.size() * sizeof(float))));
        std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
        std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
        std::memcpy(mesh.colors, colors.data(), colors.size());
        std::memcpy(mesh.texcoords, texcoords.data(), texcoords.size() * sizeof(float));
        UploadMesh(&mesh, false);
        return LoadModelFromMesh(mesh);
    }
    std::vector<Model> upload_chunks() const {
        std::map<std::pair<int, int>, MeshBuilder> cells;
        for (std::size_t i = 0; i < positions.size(); i += 9) {
            const Vec3 center((positions[i] + positions[i + 3] + positions[i + 6]) / 3,
                0, (positions[i + 2] + positions[i + 5] + positions[i + 8]) / 3);
            auto& mesh = cells[cell(center)];
            mesh.positions.insert(mesh.positions.end(), positions.begin() + i, positions.begin() + i + 9);
            mesh.normals.insert(mesh.normals.end(), normals.begin() + i, normals.begin() + i + 9);
            const auto vertex = i / 3;
            mesh.texcoords.insert(mesh.texcoords.end(), texcoords.begin() + vertex * 2, texcoords.begin() + vertex * 2 + 6);
            mesh.colors.insert(mesh.colors.end(), colors.begin() + vertex * 4, colors.begin() + vertex * 4 + 12);
        }
        std::vector<Model> models;
        for (const auto& part : cells) models.push_back(part.second.upload());
        return models;
    }
};
Mesh batch_tree_mesh(const Mesh& source, const std::vector<Tree>& trees, const Vec3& origin) {
    Mesh mesh{};
    mesh.triangleCount = source.triangleCount * int(trees.size());
    mesh.vertexCount = mesh.triangleCount * 3;
    mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(mesh.vertexCount * 4));
    int destination = 0;
    for (const auto& tree : trees) {
        const Quat rotation = Quat::sRotation(Vec3::sAxisY(), tree.yaw);
        for (int corner = 0; corner < source.triangleCount * 3; ++corner, ++destination) {
            const int index = source.indices ? source.indices[corner] : corner;
            const Vec3 local(source.vertices[index * 3], source.vertices[index * 3 + 1], source.vertices[index * 3 + 2]);
            const Vec3 point = tree.base + rotation * ((local - origin) * tree.scale());
            const Vec3 normal = rotation * (source.normals
                ? Vec3(source.normals[index * 3], source.normals[index * 3 + 1], source.normals[index * 3 + 2]) : Vec3::sAxisY());
            mesh.vertices[destination * 3] = point.GetX();
            mesh.vertices[destination * 3 + 1] = point.GetY();
            mesh.vertices[destination * 3 + 2] = point.GetZ();
            mesh.normals[destination * 3] = normal.GetX();
            mesh.normals[destination * 3 + 1] = normal.GetY();
            mesh.normals[destination * 3 + 2] = normal.GetZ();
            for (int uv = 0; uv < 2; ++uv)
                mesh.texcoords[destination * 2 + uv] = source.texcoords ? source.texcoords[index * 2 + uv] : 0;
            for (int channel = 0; channel < 4; ++channel)
                mesh.colors[destination * 4 + channel] = source.colors ? source.colors[index * 4 + channel] : 255;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}
Color surface_color(Surface s) {
    switch (s) {
        case Surface::Sand: return {222, 202, 143, 255};
        case Surface::Rock: return {128, 147, 122, 255};
        // Road ribbons define crisp edges; keep the underlying grid green.
        case Surface::Road: return {98, 151, 96, 255};
        case Surface::Seabed: return {181, 176, 133, 255};
        default: return {98, 151, 96, 255};
    }
}
const char* land_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 position;
out vec3 normal;
out vec4 color;
void main() {
    position = vertexPosition;
    normal = vertexNormal;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})GLSL";
const std::string land_fragment = std::string(R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec4 color;
uniform vec3 cameraPosition;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform vec3 horizonColor;
uniform float daylight;
uniform sampler2D texture0;
out vec4 finalColor;
)GLSL") + scene_lighting_glsl() + R"GLSL(
void main() {
    // World coordinates keep the four-meter tiles continuous across cells.
    vec4 surface = texture(texture0, position.xz / 4.0) * color;
    vec3 n = normalize(normal);
    vec3 lighting = ambientLight + sunColor * (0.42 * daylight * max(dot(n, sunDirection), 0.0) * scene_shadow(position, n, sunDirection));
    lighting += scene_local_light(position, n, normalize(cameraPosition - position), 0.0);
    float fog = smoothstep(viewDistance * 0.65, viewDistance, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(surface.rgb * lighting, horizonColor, fog) * brightness, surface.a);
})GLSL";
const char* tree_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 position;
out vec3 normal;
out vec2 texCoord;
out vec4 color;
void main() {
    position = vertexPosition;
    normal = vertexNormal;
    texCoord = vertexTexCoord;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})GLSL";
const std::string tree_fragment = std::string(R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
uniform vec3 cameraPosition;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform vec3 horizonColor;
uniform float daylight;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float emissiveStrength;
out vec4 finalColor;
)GLSL") + scene_lighting_glsl() + R"GLSL(
void main() {
    vec4 surface = texture(texture0, texCoord) * colDiffuse * color;
    // Leaf cards need holes that neither hide scenery nor write depth.
    if (surface.a < 0.3) discard;
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    vec3 lighting = ambientLight + sunColor * (0.42 * daylight * max(dot(n, sunDirection), 0.0) * scene_shadow(position, n, sunDirection));
    lighting += scene_local_light(position, n, normalize(cameraPosition - position), 0.0);
    float fog = smoothstep(viewDistance * 0.65, viewDistance, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(surface.rgb * (lighting + emissiveStrength), horizonColor, fog) * brightness, 1.0);
})GLSL";
const char* water_vertex = R"GLSL(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform float time;
out vec3 position;
void main() {
    position = vertexPosition;
    position.y += sin(position.x * 0.06 + time * 0.7) * 0.10 + cos(position.z * 0.08 + time) * 0.08;
    gl_Position = mvp * vec4(position, 1.0);
})GLSL";
const std::string water_fragment = std::string(R"GLSL(#version 330
in vec3 position;
uniform vec3 cameraPosition;
uniform float time;
uniform vec3 horizonColor;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 ambientLight;
uniform float daylight;
out vec4 finalColor;
)GLSL") + scene_lighting_glsl() + R"GLSL(
void main() {
    vec3 color = mix(vec3(0.10, 0.60, 0.67), vec3(0.06, 0.34, 0.52),
        0.5 + 0.5 * sin(position.x * 0.0007 + position.z * 0.0005));
    vec2 footprint = fwidth(position.xz * 0.5);
    float ripples = sin(position.x * 0.5 + position.z * 0.36 + time * 1.5) * sin(position.z * 0.21 - time) * exp(-dot(footprint, footprint));
    color += 0.028 * ripples;
    float shadow = scene_shadow(position, vec3(0, 1, 0), sunDirection);
    color = color * (ambientLight * 0.8 + 0.6 * daylight * shadow) + horizonColor * (0.10 + 0.16 * (1.0 - daylight));
    vec3 view = normalize(cameraPosition - position);
    float reflection = pow(max(dot(reflect(-view, vec3(0, 1, 0)), sunDirection), 0.0), 90.0);
    color += reflection * sunColor * daylight * shadow * (0.5 + 0.5 * ripples);
    color += scene_local_light(position, vec3(0, 1, 0), view, 32.0) * 0.2;
    float fog = smoothstep(viewDistance * 0.65, viewDistance, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(color, horizonColor, fog) * brightness, 1.0);
})GLSL";
const char* light_fragment = R"GLSL(#version 330
in vec3 position;
in vec4 color;
uniform vec3 cameraPosition;
uniform float night;
uniform float brightness;
uniform float viewDistance;
out vec4 finalColor;
void main() {
    float visibility = 1.0 - smoothstep(viewDistance * 0.75, viewDistance, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(color.rgb * brightness, color.a * night * visibility);
})GLSL";
const char* sky_fragment = R"GLSL(#version 330
uniform vec2 screenSize;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float tanFov;
uniform vec3 horizonColor;
uniform vec3 zenithColor;
uniform vec3 sunDirection;
uniform float night;
uniform float time;
uniform float brightness;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    vec2 uv = gl_FragCoord.xy / screenSize * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward + cameraRight * uv.x * tanFov * screenSize.x / screenSize.y + cameraUp * uv.y * tanFov);
    float height = max(ray.y, 0.0);
    vec3 sky = mix(horizonColor, zenithColor, pow(clamp(height / 0.65, 0.0, 1.0), 0.55));
    float sun = max(dot(ray, sunDirection), 0.0);
    float above = smoothstep(-0.035, 0.035, sunDirection.y);
    sky += vec3(1.0, 0.51, 0.25) * pow(sun, 20.0) * 0.30 * above;
    sky = mix(sky, vec3(1.0, 0.89, 0.64), smoothstep(cos(0.020), cos(0.016), sun) * above);
    float moon = smoothstep(cos(0.013), cos(0.009), dot(ray, -sunDirection));
    sky = mix(sky, vec3(0.79, 0.86, 1.0), moon * night);
    vec2 stars = vec2(atan(ray.z, ray.x) / 6.2831853 + 0.5, asin(clamp(ray.y, -1.0, 1.0)) / 3.1415927 + 0.5) * vec2(600, 300);
    float seed = hash(floor(stars));
    float star = (1.0 - smoothstep(0.03, 0.14, length(fract(stars) - 0.5))) * step(0.994, seed);
    sky += vec3(0.74, 0.82, 1.0) * star * night * smoothstep(0.03, 0.18, ray.y) * (0.70 + 0.30 * sin(time * 1.2 + seed * 31));
    // Long, soft cloud wisps pick up the pink/orange horizon at twilight.
    float cloud = sin(ray.x * 19 + ray.z * 12 + time * 0.002) + sin(ray.x * 41 - ray.z * 27);
    cloud = smoothstep(1.20, 1.90, cloud) * exp(-pow((ray.y - 0.23) * 5.0, 2.0));
    sky = mix(sky, mix(horizonColor, vec3(1.0), 0.28), cloud * (1.0 - night) * 0.18);
    finalColor = vec4(sky * brightness, 1.0);
})GLSL";
}

EnvironmentRenderer::Chunk EnvironmentRenderer::chunk(Model model) {
    const auto box = GetModelBoundingBox(model);
    const Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), .5f);
    return {model, center, std::hypot(box.max.x - center.x, box.max.z - center.z)};
}
bool EnvironmentRenderer::nearby(const Chunk& part, const Vector3& focus, float distance) {
    const float range = distance + part.radius;
    const float dx = part.center.x - focus.x, dz = part.center.z - focus.z;
    return dx * dx + dz * dz <= range * range;
}
EnvironmentRenderer::EnvironmentRenderer(const Environment& env) {
    MeshBuilder ground, grass, sand, roads, city, water, signs, lights, glows;
    constexpr Color concrete{203, 211, 208, 255};
    const auto add_light = [&](Vec3 position, Color color, float radius) {
        local_lights_.push_back({{position.GetX(), position.GetY(), position.GetZ()},
            {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f}, radius});
    };
    // ponytail: distant pools stay baked; nearby point lights illuminate moving objects.
    const auto light_pool = [&](Vec3 center, float radius, Color tint) {
        for (int i = 0; i < 16; ++i) {
            Vec3 a = center + Vec3(std::cos(i * pi / 8), 0, std::sin(i * pi / 8)) * radius;
            Vec3 b = center + Vec3(std::cos((i + 1) * pi / 8), 0, std::sin((i + 1) * pi / 8)) * radius;
            a.SetY(env.surface_height(a) + .13f); b.SetY(env.surface_height(b) + .13f);
            Vec3 c = center; c.SetY(env.surface_height(c) + .13f);
            const auto first = glows.colors.size();
            glows.triangle(c, b, a, tint);
            if (glows.colors.size() > first) { glows.colors[first + 7] = 0; glows.colors[first + 11] = 0; }
        }
    };
    grass_texture_ = load_terrain_texture("grass.png");
    sand_texture_ = load_terrain_texture("beach-sand.png");
    asphalt_texture_ = load_terrain_texture("asphalt.png");
    const Color asphalt = asphalt_texture_.id ? WHITE : Color{57, 65, 70, 255};
    for (const auto& t : env.triangles()) {
        const bool shoreline = t.surface == Surface::Seabed;
        if (shoreline && std::max({env.vertices()[t.a].GetY(), env.vertices()[t.b].GetY(), env.vertices()[t.c].GetY()}) < 0) continue;
        MeshBuilder* surface = &ground;
        Color tint = surface_color(t.surface);
        Vec3 lift = Vec3::sZero();
        if (t.deck) {
            surface = &roads; tint = asphalt; lift.SetY(.045f);
            // Match the deck's exact tessellation, with outward faces underneath.
            const Vec3 down(0, 1.2f, 0);
            city.triangle(env.vertices()[t.c] - down, env.vertices()[t.b] - down, env.vertices()[t.a] - down, concrete);
        }
        else if (t.surface == Surface::Grass || t.surface == Surface::Road) {
            surface = &grass; if (grass_texture_.id) tint = WHITE;
        } else if (t.surface == Surface::Sand || shoreline) {
            surface = &sand; tint = sand_texture_.id ? WHITE : surface_color(Surface::Sand);
        }
        surface->triangle(env.vertices()[t.a] + lift, env.vertices()[t.b] + lift, env.vertices()[t.c] + lift, tint);
    }
    const auto deck_strip = [&](const Road& road, Vec3 a, Vec3 b, float width, Color color) {
        const Vec3 side = (b - a).Normalized().Cross(Vec3::sAxisY()) * (width / 2);
        const auto raised = [&](Vec3 p) { p.SetY(env.road_height(road, p.GetX(), p.GetZ()) + .085f); return p; };
        city.quad(raised(a - side), raised(a + side), raised(b + side), raised(b - side), color);
    };
    for (const auto& road : env.roads()) {
        if (road.bridge >= 0 && env.bridges()[road.bridge].has_curve()) {
            const auto& bridge = env.bridges()[road.bridge];
            const float length = (bridge.b - bridge.a).Length();
            const auto stripe = [&](float begin, float end, float offset, float width, Color color) {
                const int count = std::max(1, int(std::ceil((end - begin) / 2)));
                for (int i = 0; i < count; ++i) {
                    const float from = (begin + (end - begin) * float(i) / count) / length;
                    const float to = (begin + (end - begin) * float(i + 1) / count) / length;
                    deck_strip(road, bridge.point(from) + bridge.side(from) * offset,
                        bridge.point(to) + bridge.side(to) * offset, width, color);
                }
            };
            for (float along = 0; along < length; along += 16) {
                stripe(along, std::min(along + 6, length), 0, .24f, {244, 207, 99, 255});
                for (float sign : {-1.0f, 1.0f})
                    stripe(along, std::min(along + 6, length), sign * road.width / 6, .18f, RAYWHITE);
            }
            for (float sign : {-1.0f, 1.0f}) {
                stripe(0, length, sign * road.width / 3, .18f, RAYWHITE);
                stripe(0, length, sign * (road.width / 2 - 1), .18f, RAYWHITE);
            }
            continue;
        }
        const Vec3 direction = (road.b - road.a).Normalized();
        const Vec3 side = direction.Cross(Vec3::sAxisY());
        const float length = (road.b - road.a).Length();
        const auto marking = [&](Vec3 a, Vec3 b, float width, Color color) {
            const int steps = std::max(1, int(std::ceil((b - a).Length() / 2)));
            for (int i = 0; i < steps; ++i) {
                const Vec3 start = a + (b - a) * (float(i) / steps), end = a + (b - a) * (float(i + 1) / steps);
                const Vec3 middle = (start + end) / 2;
                bool covered = false;
                for (const auto& other : env.roads()) {
                    if (&other == &road) continue;
                    if (std::string_view(road.name) == "OVERSEAS HIGHWAY" && std::string_view(other.name) == road.name) continue;
                    Vec3 delta = other.b - other.a; delta.SetY(0);
                    const float t = std::clamp((middle - other.a).Dot(delta) / delta.LengthSq(), 0.0f, 1.0f);
                    Vec3 distance = middle - other.a - delta * t; distance.SetY(0);
                    if (distance.Length() > other.width / 2 + .3f
                        || std::abs(env.road_height(road, middle.GetX(), middle.GetZ())
                            - env.road_height(other, middle.GetX(), middle.GetZ())) > .4f) continue;
                    // Open junctions; keep only the widest marking on overlapping streets.
                    if (std::abs(direction.Dot(delta.Normalized())) < .98f || other.width > road.width
                        || (other.width == road.width && &other < &road)) { covered = true; break; }
                }
                if (covered) continue;
                if (road.bridge >= 0) deck_strip(road, start, end, width, color);
                else city.ribbon(env, start, end, width, color, .085f);
            }
        };
        if (road.bridge < 0) {
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 verge = side * (sign * (road.width / 2 + .75f));
                city.ribbon(env, road.a + verge, road.b + verge, 1.5f, {172, 181, 175, 255}, .03f);
            }
            roads.ribbon(env, road.a, road.b, road.width, asphalt, .055f);
        }
        for (float along = 0; along < length; along += 16) {
            const Vec3 a = road.a + direction * along, b = road.a + direction * std::min(along + 6, length);
            marking(a, b, .24f, {244, 207, 99, 255});
        }
        if (road.width >= 20) for (float sign : {-1.0f, 1.0f}) {
            for (float along = 0; along < length; along += 16) {
                const Vec3 offset = side * (sign * road.width / 6);
                marking(road.a + direction * along + offset,
                    road.a + direction * std::min(along + 6, length) + offset, .18f, RAYWHITE);
            }
            const Vec3 shoulder = side * (sign * road.width / 3);
            marking(road.a + shoulder, road.b + shoulder, .18f, RAYWHITE);
        }
        for (float sign : {-1.0f, 1.0f}) {
            const Vec3 offset = side * (sign * (road.width / 2 - 1));
            marking(road.a + offset, road.b + offset, .18f, RAYWHITE);
        }
        if (road.bridge < 0) for (float along = 18; along < length - 12; along += 40) {
            const float sign = int(along / 40) % 2 ? -1.0f : 1.0f;
            Vec3 p = road.a + direction * along + side * (sign * (road.width / 2 + .65f));
            bool junction = false;
            for (const auto& other : env.roads()) {
                if (&other == &road) continue;
                Vec3 d = other.b - other.a; d.SetY(0);
                const float t = std::clamp((p - other.a).Dot(d) / d.LengthSq(), 0.0f, 1.0f);
                Vec3 distance = p - other.a - d * t; distance.SetY(0);
                if (distance.Length() >= other.width / 2 + 1
                    || std::abs(env.road_height(road, p.GetX(), p.GetZ())
                        - env.road_height(other, p.GetX(), p.GetZ())) > .4f) continue;
                if (std::abs(direction.Dot(d.Normalized())) < .98f || other.width > road.width
                    || (other.width == road.width && &other < &road)) { junction = true; break; }
            }
            if (junction) continue;
            p.SetY(env.road_height(road, p.GetX(), p.GetZ()));
            const Vec3 lamp = p - side * sign + Vec3(0, 5.2f, 0);
            city.box(p + Vec3(0, 2.6f, 0), Vec3(.12f, 5.2f, .12f), {69, 83, 94, 255});
            city.box(lamp, Vec3(.65f, .16f, .65f), {219, 225, 216, 255});
            lights.box(lamp - Vec3(0, .10f, 0), Vec3(.52f, .07f, .52f), {255, 211, 142, 255});
            add_light(lamp - Vec3(0, .10f, 0), {255, 211, 142, 255}, 18);
            Vec3 pool = road.a + direction * along;
            pool.SetY(env.road_height(road, pool.GetX(), pool.GetZ()));
            light_pool(pool, 8, {255, 195, 116, 85});
        }
    }

    Image atlas = GenImageColor(256, 64 * sign_rows, BLANK);
    const char* labels[] = {"HOTEL", "CAFE", "CLUB", "MALL", "GAS", "MARINA", "MIAMI BEACH", "US 1", "SHOP", "WORKSHOP", "AIRPORT"};
    const Color backgrounds[] = {{36, 136, 151, 255}, {190, 101, 64, 255}, {121, 44, 161, 255},
        {26, 73, 104, 255}, {34, 141, 104, 255}, {31, 100, 139, 255}, {210, 123, 141, 255}, {43, 105, 74, 255},
        {53, 101, 119, 255}, {108, 106, 86, 255}, {43, 89, 133, 255}};
    for (int row = 0; row < sign_rows; ++row) {
        ImageDrawRectangle(&atlas, 0, row * 64, 256, 64, backgrounds[row]);
        ImageDrawRectangleLines(&atlas, {3, float(row * 64 + 3), 250, 58}, 2, RAYWHITE);
        const int font_size = row == 6 ? 22 : 32;
        forza::ui::draw_image_text(&atlas, labels[row], (256 - forza::ui::measure_text(labels[row], font_size)) / 2, row * 64 + 18, font_size, RAYWHITE);
    }
    sign_texture_ = LoadTextureFromImage(atlas); UnloadImage(atlas);
    SetTextureFilter(sign_texture_, TEXTURE_FILTER_BILINEAR);
    constexpr Color palette[] = {{233, 183, 176, 255}, {230, 217, 166, 255}, {146, 204, 200, 255},
        {182, 174, 214, 255}, {225, 234, 221, 255}};
    constexpr Color glass{48, 108, 136, 255};
    for (const auto& b : env.buildings()) {
        const float base = b.center.GetY() - b.size.GetY() / 2, roof = base + b.size.GetY();
        const float x = b.center.GetX(), z = b.center.GetZ(), sx = b.size.GetX(), sz = b.size.GetZ();
        const float facing = b.face_positive ? 1.0f : -1.0f;
        const Vec3 front = (b.face_east ? Vec3::sAxisX() : Vec3::sAxisZ()) * facing;
        const Vec3 face = Vec3(x, base, z) + front * ((b.face_east ? sx : sz) / 2 + .08f);
        const Vec3 sign_right = (b.face_east ? Vec3(0, 0, -1) : Vec3::sAxisX()) * facing;
        const auto shop_sign = [&](int row, float y, float width) {
            signs.sign(face + front * .06f + Vec3(0, std::min(y, b.size.GetY() - .65f), 0), sign_right,
                std::min(width, (b.face_east ? sz : sx) * .8f), 1.2f, row);
        };
        Color body = palette[b.style % 5];
        constexpr Color window_tints[] = {{255, 205, 133, 255}, {150, 221, 255, 255},
            {255, 159, 214, 255}, {255, 230, 178, 255}, {142, 231, 216, 255}};
        const Color window_light = window_tints[b.style % 5];
        if (b.kind == BuildingKind::Tower) body = Color{89, static_cast<unsigned char>(140 + b.style * 9), 166, 255};
        if (b.kind == BuildingKind::Warehouse || b.kind == BuildingKind::Hangar) body = Color{149, 169, 174, 255};
        city.box(b.solid_center(), b.solid_size(), body);
        if (b.kind != BuildingKind::House)
            city.box(Vec3(x, base + .04f, z), Vec3(sx + 3, .08f, sz + 3), concrete);
        const Vec3 solid = b.solid_center();
        city.box(Vec3(solid.GetX(), roof + .15f, z), Vec3(b.solid_size().GetX() + .6f, .3f, sz + .6f), concrete);
        if (b.kind == BuildingKind::Tower || b.kind == BuildingKind::Hotel
            || b.kind == BuildingKind::Apartment || b.kind == BuildingKind::Office) {
            const float floor = 3.3f;
            for (float y = base + 4; y < roof - 1; y += floor) {
                for (float side : {-1.0f, 1.0f}) {
                    city.box(Vec3(x, y, z + side * (sz / 2 + .04f)), Vec3(sx - 5, floor * .55f, .08f), glass);
                    city.box(Vec3(x + side * (sx / 2 + .04f), y, z), Vec3(.08f, floor * .55f, sz - 5), glass);
                    int pane = 0;
                    for (float dx = -sx / 2 + 3; dx < sx / 2 - 2; dx += 3.5f, ++pane)
                        if ((pane + int(y * 10) + b.style) % 5 != 0)
                            lights.box(Vec3(x + dx, y, z + side * (sz / 2 + .105f)), Vec3(1.8f, 1.4f, .03f), window_light);
                    for (float dz = -sz / 2 + 3; dz < sz / 2 - 2; dz += 3.5f, ++pane)
                        if ((pane + int(y * 10) + b.style) % 5 != 0)
                            lights.box(Vec3(x + side * (sx / 2 + .105f), y, z + dz), Vec3(.03f, 1.4f, 1.8f), window_light);
                }
                if (b.kind == BuildingKind::Hotel || b.kind == BuildingKind::Apartment)
                    city.box(Vec3(x, y - 1.3f, z), Vec3(sx + .8f, .2f, sz + .8f), RAYWHITE);
            }
            const float cap = b.kind == BuildingKind::Tower ? 3.0f + b.style : 1.2f;
            city.box(Vec3(x, roof + cap / 2, z), Vec3(sx * (b.style % 2 ? .5f : .75f), cap, sz * .55f), body);
            if (b.kind == BuildingKind::Tower) lights.box(Vec3(x, roof + cap + .2f, z), Vec3(.35f, .35f, .35f), {255, 63, 104, 255});
            if (b.kind == BuildingKind::Office || b.style == 3)
                for (float dx : {-sx * .3f, sx * .3f})
                    city.box(Vec3(x + dx, roof + .65f, z), Vec3(2, 1.3f, 2), concrete);
            if (b.kind == BuildingKind::Hotel) shop_sign(0, 5, 16);
            const Vec3 door_size = b.face_east ? Vec3(.12f, 2.3f, 1.4f) : Vec3(1.4f, 2.3f, .12f);
            city.box(face + Vec3(0, 1.15f, 0), door_size, glass);
        } else if (b.kind == BuildingKind::House) {
            const Vec3 a(x - sx / 2 - .45f, roof, z - sz / 2 - .45f), c(x + sx / 2 + .45f, roof, z - sz / 2 - .45f);
            const Vec3 d(x - sx / 2 - .45f, roof, z + sz / 2 + .45f), e(x + sx / 2 + .45f, roof, z + sz / 2 + .45f);
            if (b.style % 3 == 0) {
                const Vec3 ridge(x, roof + 1.6f, z);
                for (const auto& edge : {std::pair<Vec3, Vec3>{a, c}, {c, e}, {e, d}, {d, a}})
                    city.triangle(edge.first, ridge, edge.second, {172, 88, 61, 255});
            } else if (b.style % 3 == 1) {
                city.box(Vec3(x, roof + .35f, z), Vec3(sx, .4f, sz), palette[(b.style + 1) % 5]);
            } else {
                const Vec3 r1(x, roof + 1.4f, a.GetZ()), r2(x, roof + 1.4f, d.GetZ());
                city.quad(a, d, r2, r1, {172, 88, 61, 255}); city.quad(r1, r2, e, c, {192, 108, 77, 255});
                city.triangle(a, r1, c, body); city.triangle(d, e, r2, body);
            }
            city.box(face + Vec3(0, 1.05f, 0), b.face_east ? Vec3(.12f, 2.1f, 1) : Vec3(1, 2.1f, .12f), {91, 72, 55, 255});
            for (float floor = 1.6f; floor < b.size.GetY(); floor += 3.1f) for (float side : {-1.0f, 1.0f}) {
                city.box(face + sign_right * (side * (b.face_east ? sz : sx) * .28f) + Vec3(0, floor, 0),
                    b.face_east ? Vec3(.12f, 1.1f, 1.5f) : Vec3(1.5f, 1.1f, .12f), glass);
                if (b.style != 3 || side > 0)
                    lights.box(face + front * .08f + sign_right * (side * (b.face_east ? sz : sx) * .28f) + Vec3(0, floor, 0),
                        b.face_east ? Vec3(.03f, 1.0f, 1.4f) : Vec3(1.4f, 1.0f, .03f), window_light);
            }
            city.box(face + front + Vec3(0, 2.5f, 0), b.face_east ? Vec3(2, .2f, std::min(sz, 6.0f)) : Vec3(std::min(sx, 6.0f), .2f, 2), RAYWHITE);
        } else if (b.kind == BuildingKind::GasStation) {
            city.ribbon(env, Vec3(x, 0, z - sz / 2), Vec3(x, 0, z + sz / 2), sx, {173, 179, 175, 255}, .035f);
            // Keep the canopy inside the station's collider footprint.
            city.box(Vec3(x + sx * .3f, base + 4.8f, z), Vec3(sx * .35f, .6f, sz - 4), {32, 153, 126, 255});
            for (float dz : {-sz * .28f, 0.0f, sz * .28f}) {
                city.box(Vec3(x + sx * .3f, base + 1, z + dz), Vec3(1.8f, 2, 1.2f), RAYWHITE);
                city.box(Vec3(x + sx * .3f, base + 3, z + dz), Vec3(.25f, 3, .25f), concrete);
            }
            shop_sign(4, 6, 12);
            city.box(face + Vec3(0, 3, 0), Vec3(.25f, 6, .25f), concrete);
        } else if (b.kind == BuildingKind::Cafe || b.kind == BuildingKind::Club || b.kind == BuildingKind::Shop) {
            const bool club = b.kind == BuildingKind::Club;
            const Color accent = club ? Color{211, 94, 228, 255} : Color{240, 161, 98, 255};
            const Vec3 awning_size = b.face_east ? Vec3(1.8f, .2f, sz - 2) : Vec3(sx - 2, .2f, 1.8f);
            city.box(face + front * .8f + Vec3(0, 2.6f, 0), awning_size, accent);
            const Vec3 window_size = b.face_east ? Vec3(.1f, 1.8f, sz - 3) : Vec3(sx - 3, 1.8f, .1f);
            city.box(face + Vec3(0, 1.3f, 0), window_size, glass);
            lights.box(face + front * .09f + Vec3(0, 1.3f, 0), window_size,
                club ? Color{235, 79, 213, 180} : Color{255, 203, 146, 130});
            add_light(face + front * .09f + Vec3(0, 1.3f, 0), club ? Color{235, 79, 213, 255} : Color{255, 203, 146, 255}, 12);
            lights.box(face + front * 1.72f + Vec3(0, 2.6f, 0),
                b.face_east ? Vec3(.05f, .12f, sz - 2) : Vec3(sx - 2, .12f, .05f),
                club ? Color{255, 99, 222, 255} : Color{102, 224, 230, 255});
            shop_sign(club ? 2 : b.kind == BuildingKind::Shop ? 8 : 1, club ? 7 : 5, 14);
            if (b.kind == BuildingKind::Cafe) for (float side : {-2.8f, 2.8f}) {
                const Vec3 table = face + front * 1.7f + sign_right * side;
                city.box(table + Vec3(0, .75f, 0), Vec3(1.8f, .15f, 1.8f), body);
                city.box(table + Vec3(0, .35f, 0), Vec3(.2f, .7f, .2f), concrete);
            }
        } else if (b.kind == BuildingKind::Mall) {
            city.box(face + Vec3(0, 2.5f, 0), Vec3(sx - 6, 4, .2f), glass);
            city.box(face + front + Vec3(0, 4.5f, 0), Vec3(sx - 2, .4f, 2), concrete);
            shop_sign(3, 7, 16);
        } else if (b.kind == BuildingKind::ControlTower) {
            city.box(Vec3(x, roof - 3, z), Vec3(sx + 3, 5, sz + 3), glass);
            city.box(Vec3(x, roof + 3, z), Vec3(.2f, 6, .2f), RAYWHITE);
        } else {
            city.box(face + Vec3(0, 2.5f, 0), b.face_east ? Vec3(.15f, 4, sz - 4) : Vec3(sx - 4, 4, .15f), glass);
            if (b.kind == BuildingKind::Warehouse) {
                shop_sign(9, 5.5f, 10);
                city.box(Vec3(x, roof + .4f, z), Vec3(sx * .6f, .6f, 2), glass);
                city.box(face + front * 1.5f + sign_right * 3 + Vec3(0, .65f, 0), Vec3(2, 1.3f, 2), {153, 111, 68, 255});
            }
        }
    }

    for (const auto& rail : env.barriers()) {
        city.box(rail.center, rail.size, {200, 209, 206, 255}, rail.yaw, rail.pitch);
        city.box(rail.center + rail.rotation() * Vec3(0, .55f, 0),
            Vec3(.55f, .12f, rail.size.GetZ()), RAYWHITE, rail.yaw, rail.pitch);
    }
    for (std::size_t bridge_index = 0; bridge_index < env.bridges().size(); ++bridge_index) {
        const auto& bridge = env.bridges()[bridge_index];
        const bool segmented = bridge.a.GetY() > 0;
        const bool curved = segmented || bridge.has_curve();
        const Vec3 direction = (bridge.b - bridge.a).Normalized();
        const float length = (bridge.b - bridge.a).Length(), heading = std::atan2(-direction.GetX(), -direction.GetZ());
        const int rows = int(length / 8) + 1;
        for (int row = 0; row < rows; ++row) {
            const float from = float(row) / rows, to = float(row + 1) / rows;
            const Vec3 a = bridge.point(from), b = bridge.point(to);
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 start = a + bridge.side(from) * (sign * bridge.width / 2);
                const Vec3 end = b + bridge.side(to) * (sign * bridge.width / 2);
                city.quad(start + Vec3(0, .045f, 0), end + Vec3(0, .045f, 0),
                    end - Vec3(0, 1.2f, 0), start - Vec3(0, 1.2f, 0), concrete);
                city.quad(start - Vec3(0, 1.2f, 0), end - Vec3(0, 1.2f, 0),
                    end + Vec3(0, .045f, 0), start + Vec3(0, .045f, 0), concrete);
            }
        }
        const float fixtures = bridge_index % 3 == 0 ? length / 2 : length;
        for (float d = segmented ? fixtures : 40; d < length - (segmented ? 0 : 20); d += 64) {
            const Vec3 p = bridge.point(d / length);
            if (!curved) {
                city.box(Vec3(p.GetX(), (p.GetY() - 8) / 2, p.GetZ()), Vec3(bridge.width - 5, p.GetY() + 6, 2.2f), concrete, heading);
                continue;
            }
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 column = p + bridge.side(d / length) * (sign * bridge.width * .32f);
                const float base = !segmented || bridge.clearance <= Airport::elevation
                    ? env.terrain_height(column.GetX(), column.GetZ()) : env.ground_height(column.GetX(), column.GetZ());
                if (p.GetY() - base < 4) continue;
                bool street = false;
                for (const auto& road : env.roads()) if (road.bridge < 0) {
                    Vec3 delta = road.b - road.a; delta.SetY(0);
                    const float t = std::clamp((column - road.a).Dot(delta) / delta.LengthSq(), 0.0f, 1.0f);
                    Vec3 distance = column - road.a - delta * t; distance.SetY(0);
                    if (distance.Length() < road.width / 2 + 2) { street = true; break; }
                }
                if (!street) city.box(Vec3(column.GetX(), (p.GetY() + base) / 2, column.GetZ()),
                    Vec3(1.2f, p.GetY() - base, 1.2f), concrete);
            }
        }
        for (float d = segmented ? fixtures : 35; d < length - (segmented ? 0 : 10); d += 96) for (float sign : {-1.0f, 1.0f}) {
            const Vec3 side = bridge.side(d / length).Normalized();
            const float heading = std::atan2(-side.GetZ(), side.GetX());
            const Vec3 p = bridge.point(d / length) + side * (sign * (bridge.width / 2 - .8f));
            city.box(p + Vec3(0, 4, 0), Vec3(.18f, 8, .18f), {86, 111, 121, 255});
            city.box(p + Vec3(0, 8, 0) - side * sign, Vec3(2.5f, .2f, .6f), {252, 236, 170, 255}, heading);
            lights.box(p + Vec3(0, 7.86f, 0) - side * sign, Vec3(2.1f, .07f, .45f), {255, 215, 148, 255}, heading);
            add_light(p + Vec3(0, 7.86f, 0) - side * sign, {255, 215, 148, 255}, 24);
            if (sign > 0) light_pool(bridge.point(d / length), bridge.width / 2 - .7f, {255, 195, 116, 100});
        }
    }
    for (const auto& port : env.ports()) {
        const Vec3 direction = port.east ? Vec3::sAxisX() : Vec3::sAxisZ(), side = direction.Cross(Vec3::sAxisY());
        city.box(port.solid_center(), port.solid_size(), {149, 116, 80, 255});
        signs.sign(port.center + Vec3(0, 4, 0), Vec3::sAxisX(), std::min(14.0f, port.length / 8), 3, 5);
        city.box(port.center + Vec3(0, 2, 0), Vec3(.3f, 4, .3f), concrete);
        for (float d = 10; d < port.length; d += 25) {
            const Vec3 p = port.center + direction * d;
            const Vec3 lamp = p + side * (port.width / 2 - 1);
            city.box(p - Vec3(0, 3, 0), Vec3(.7f, 6, .7f), {91, 84, 64, 255});
            city.box(lamp + Vec3(0, 1.5f, 0), Vec3(.12f, 3, .12f), concrete);
            lights.box(lamp + Vec3(0, 3.1f, 0), Vec3(.3f, .2f, .3f), {131, 223, 255, 255});
            add_light(lamp + Vec3(0, 3.1f, 0), {131, 223, 255, 255}, 12);
            if (d < 30) continue;
            const Vec3 boat = p + side * 14;
            if (env.terrain_height(boat.GetX(), boat.GetZ()) >= 0) continue;
            city.box(Vec3(boat.GetX(), .65f, boat.GetZ()), Vec3(5, 1.3f, 13), RAYWHITE);
            city.box(Vec3(boat.GetX(), 1.5f, boat.GetZ() + 1), Vec3(3.5f, 1, 5), glass);
        }
        if (!port.east) {
            const Vec3 ship = port.center + direction * 140 + side * 80;
            city.box(Vec3(ship.GetX(), 4, ship.GetZ()), Vec3(30, 8, 140), RAYWHITE);
            for (int level = 0; level < 4; ++level)
                city.box(Vec3(ship.GetX(), 10.0f + level * 4, ship.GetZ() + level * 5), Vec3(27 - level * 3.0f, 3, 115 - level * 15.0f), {215, 229, 232, 255});
        }
    }
    for (int i = 0; i < 38; ++i) {
        const float z = -492 + i * 26.4f;
        float x = 1770;
        while (x > 1490 && env.terrain_height(x, z) < .7f) x -= 5;
        x -= 8;
        const Vec3 p(x, env.terrain_height(x, z), z);
        if (p.GetY() < .1f) continue;
        city.box(p + Vec3(0, 1.3f, 0), Vec3(.12f, 2.6f, .12f), RAYWHITE);
        for (int j = 0; j < 8; ++j) {
            const float a = j * pi / 4, b = (j + 1) * pi / 4;
            city.triangle(p + Vec3(0, 3, 0), p + Vec3(2.3f * std::cos(b), 2.4f, 2.3f * std::sin(b)),
                p + Vec3(2.3f * std::cos(a), 2.4f, 2.3f * std::sin(a)), j % 2 ? palette[i % 5] : RAYWHITE);
        }
        city.box(p + Vec3(3, .16f, 0), Vec3(1.5f, .25f, 2.6f), {115, 173, 190, 255});
    }

    for (const auto& airport : airports) {
        const float ax = airport.center_x, rz = airport.runway_z;
        roads.ribbon(env, Vec3(ax - 76.5f, 0, rz - 220), Vec3(ax - 76.5f, 0, rz + 160), 77, asphalt, .06f);
        roads.ribbon(env, Vec3(ax - 78, 0, rz - 200), Vec3(ax, 0, rz - 200), 16, asphalt, .06f);
        city.ribbon(env, Vec3(ax - 78, 0, rz - 200), Vec3(ax, 0, rz - 200), .3f, YELLOW, .095f);
        for (float stand : {-180.f, -140.f, 0.f, 40.f, 100.f})
            city.ribbon(env, Vec3(ax - 78, 0, rz + stand - 8), Vec3(ax - 78, 0, rz + stand + 8), .25f, YELLOW, .095f);
        const auto vector = [](AirportPoint p, float y = 0) { return Vec3(p.x, y, p.z); };
        if (airport.international) {
            roads.ribbon(env, Vec3(airport.apron_x(), 0, airport.apron_z() - 60),
                Vec3(airport.apron_x(), 0, airport.apron_z() + 60), 30, asphalt, .055f);
            const auto access = airport.access_points();
            for (int i = 1; i < int(access.size()); ++i) {
                roads.ribbon(env, vector(access[i - 1]), vector(access[i]), airport.gate_width(), asphalt, .06f);
                city.ribbon(env, vector(access[i - 1]), vector(access[i]), .35f, YELLOW, .095f);
            }
        } else {
            roads.ribbon(env, Vec3(airport.apron_x(), 0, rz + airport.departure * (airport.runway_length() / 2 - 12)), Vec3(airport.apron_x(), 0, airport.plane_z()), 30, asphalt, .06f);
            roads.ribbon(env, Vec3(ax, 0, airport.plane_z()), Vec3(airport.apron_x(), 0, airport.plane_z()), 10, asphalt, .06f);
            city.ribbon(env, Vec3(airport.apron_x(), 0, airport.apron_z()), Vec3(airport.apron_x(), 0, airport.plane_z()), .25f, YELLOW, .09f);
            city.ribbon(env, Vec3(airport.apron_x(), 0, airport.plane_z()), Vec3(ax, 0, airport.plane_z()), .25f, YELLOW, .09f);
        }
        for (int runway = 0; runway < airport.runway_count(); ++runway) {
            const float half = airport.runway_length() / 2, width = airport.runway_width();
            const auto point = [&](float along, float across = 0) { return vector(airport.point(along, across, runway)); };
            roads.ribbon(env, point(-half), point(half), width, asphalt, .065f);
            if (airport.international) {
                const Vec3 apron(airport.apron_x(), 0, airport.apron_z()), taxi(airport.apron_x(), 0, airport.plane_z());
                roads.ribbon(env, apron, taxi, 12, asphalt, .06f);
                roads.ribbon(env, taxi, point(airport.plane_along()), 12, asphalt, .06f);
                city.ribbon(env, apron, taxi, .3f, YELLOW, .095f);
                city.ribbon(env, taxi, point(airport.plane_along()), .3f, YELLOW, .095f);
            }
            for (float sign : {-1.0f, 1.0f})
                city.ribbon(env, point(-half, sign * (width / 2 - 1.2f)), point(half, sign * (width / 2 - 1.2f)), .35f, RAYWHITE, .10f);
            for (float along = -half + 43; along < half - 38; along += 20)
                city.ribbon(env, point(along), point(along + 10), airport.international ? .8f : .45f, RAYWHITE, .10f);
            for (float end : {-1.0f, 1.0f}) for (float stripe : {-1.0f, -.65f, -.3f, .3f, .65f, 1.0f})
                city.ribbon(env, point(end * (half - 4), stripe * (width / 2 - 4)),
                    point(end * (half - (airport.international ? 22 : 14)), stripe * (width / 2 - 4)),
                    airport.international ? 2.2f : 1.4f, RAYWHITE, .10f);
            for (float along = -half + 8; along <= half - 8; along += airport.international ? 30 : 20) for (float sign : {-1.0f, 1.0f}) {
                const auto p = point(along, sign * (width / 2 - .2f));
                city.box(p + Vec3(0, Airport::elevation + .17f, 0), Vec3(.3f, .25f, .3f), {245, 233, 166, 255});
                lights.box(p + Vec3(0, Airport::elevation + .31f, 0), Vec3(.35f, .10f, .35f), {255, 236, 177, 255});
                add_light(p + Vec3(0, Airport::elevation + .31f, 0), {255, 236, 177, 255}, 6);
                light_pool(p + Vec3(0, Airport::elevation, 0), 1.8f, {255, 218, 144, 90});
            }
            for (float across : {-1.0f, -.5f, 0.0f, .5f, 1.0f}) for (float end : {-1.0f, 1.0f})
                lights.box(point(end * (half - 3), across * (width / 2 - 4)) + Vec3(0, Airport::elevation + .18f, 0),
                    Vec3(.45f, .16f, .45f), end > 0 ? Color{255, 63, 69, 255} : Color{100, 255, 166, 255});
        }
        const Vec3 sock(airport.apron_x() + 20, Airport::elevation,
            airport.international ? airport.apron_z() + 45 : rz + airport.departure * 132);
        city.box(sock + Vec3(0, 3, 0), Vec3(.18f, 6, .18f), concrete);
        for (int i = 0; i < 5; ++i)
            city.box(sock + Vec3(.5f + i * .5f, 5.8f - i * .09f, 0),
                Vec3(.5f, .6f - i * .075f, .6f - i * .075f), i % 2 ? RAYWHITE : ORANGE);
    }
    for (std::size_t i = 3; i < env.bridges().size(); ++i) {
        const auto& bridge = env.bridges()[i];
        if (!bridge.open_a) continue;
        const Vec3 dir = (bridge.b - bridge.a).Normalized(), side = dir.Cross(Vec3::sAxisY());
        const Vec3 p = bridge.point(.08f) + side * (bridge.width / 2 - 1);
        signs.sign(p + Vec3(0, 6, 0), side, 7, 2.3f, 7);
        city.box(p + Vec3(0, 3, 0), Vec3(.18f, 6, .18f), concrete);
    }
    for (int z = -80; z < 80; ++z) for (int x = -80; x < 80; ++x) {
        const Vec3 a(x * 250.0f, 0, z * 250.0f), b = a + Vec3(250, 0, 0), c = a + Vec3(0, 0, 250), d = a + Vec3(250, 0, 250);
        water.quad(a, c, d, b, WHITE);
    }
    terrain_ = ground.upload(); grass_ = grass.upload(); sand_ = sand.upload(); roads_ = roads.upload();
    for (Model model : city.upload_chunks()) city_chunks_.push_back(chunk(model));
    ocean_ = water.upload(); signs_ = signs.upload();
    lights_ = lights.upload(); glows_ = glows.upload();
    land_shader_ = LoadShaderFromMemory(land_vertex, land_fragment.c_str());
    water_shader_ = LoadShaderFromMemory(water_vertex, water_fragment.c_str());
    sky_shader_ = LoadShaderFromMemory(nullptr, sky_fragment);
    light_shader_ = LoadShaderFromMemory(land_vertex, light_fragment);
    if (lights_.materials) lights_.materials[0].shader = light_shader_;
    if (glows_.materials) glows_.materials[0].shader = light_shader_;
    for (Model* model : {&terrain_, &grass_, &sand_, &roads_})
        if (model->materials) model->materials[0].shader = land_shader_;
    for (auto& part : city_chunks_) part.model.materials[0].shader = land_shader_;
    if (grass_.materials && grass_texture_.id) SetMaterialTexture(&grass_.materials[0], MATERIAL_MAP_DIFFUSE, grass_texture_);
    if (sand_.materials && sand_texture_.id) SetMaterialTexture(&sand_.materials[0], MATERIAL_MAP_DIFFUSE, sand_texture_);
    if (roads_.materials && asphalt_texture_.id) SetMaterialTexture(&roads_.materials[0], MATERIAL_MAP_DIFFUSE, asphalt_texture_);
    ocean_.materials[0].shader = water_shader_;
    land_camera_ = GetShaderLocation(land_shader_, "cameraPosition");
    water_camera_ = GetShaderLocation(water_shader_, "cameraPosition");
    water_time_ = GetShaderLocation(water_shader_, "time");
    load_trees(env);
    if (!tree_shader_.id) {
        tree_shader_ = LoadShaderFromMemory(tree_vertex, tree_fragment.c_str());
        tree_camera_ = GetShaderLocation(tree_shader_, "cameraPosition");
    }
    if (signs_.materials) {
        signs_.materials[0].shader = tree_shader_;
        SetMaterialTexture(&signs_.materials[0], MATERIAL_MAP_DIFFUSE, sign_texture_);
    }
    sign_emission_ = GetShaderLocation(tree_shader_, "emissiveStrength");
    load_map(env);
}

void EnvironmentRenderer::load_map(const Environment& env) {
    MeshBuilder map;
    const auto point = [](Vec3 p) { return Vec3(p.GetX(), p.GetZ(), 0); };
    // Keep the terrain's exact shoreline instead of enlarging five-meter texels.
    for (const auto& triangle : env.triangles()) {
        if (triangle.deck) continue;
        const std::array<Vec3, 3> vertices{env.vertices()[triangle.a], env.vertices()[triangle.b], env.vertices()[triangle.c]};
        std::array<Vec3, 4> land;
        int count = 0;
        Vec3 previous = vertices.back();
        for (Vec3 current : vertices) {
            if ((current.GetY() > .1f) != (previous.GetY() > .1f))
                land[count++] = previous + (current - previous) * ((.1f - previous.GetY()) / (current.GetY() - previous.GetY()));
            if (current.GetY() > .1f) land[count++] = current;
            previous = current;
        }
        for (int i = 1; i + 1 < count; ++i)
            map.triangle(point(land[0]), point(land[i]), point(land[i + 1]), {96, 96, 96, 255});
    }
    const Color pavement{205, 205, 205, 255};
    const auto rectangle = [&](float x, float z, float width, float depth, Color color) {
        map.quad(Vec3(x, z, 0), Vec3(x + width, z, 0), Vec3(x + width, z + depth, 0), Vec3(x, z + depth, 0), color);
    };
    const auto line = [&](Vec3 a, Vec3 b, float width) {
        const Vec3 side = Vec3(-(b - a).GetZ(), 0, (b - a).GetX()).NormalizedOr(Vec3::sAxisX()) * (width / 2);
        map.quad(point(a - side), point(a + side), point(b + side), point(b - side), pavement);
    };
    for (const auto& road : env.roads()) {
        if (road.bridge < 0 || !env.bridges()[road.bridge].has_curve()) { line(road.a, road.b, road.width); continue; }
        const auto& bridge = env.bridges()[road.bridge];
        const int rows = int((bridge.b - bridge.a).Length() / 8) + 1;
        for (int row = 0; row < rows; ++row) {
            const float from = float(row) / rows, to = float(row + 1) / rows;
            const Vec3 a = bridge.point(from), b = bridge.point(to);
            map.quad(point(a - bridge.side(from) * (bridge.width / 2)), point(a + bridge.side(from) * (bridge.width / 2)),
                point(b + bridge.side(to) * (bridge.width / 2)), point(b - bridge.side(to) * (bridge.width / 2)), pavement);
        }
    }
    for (const auto& port : env.ports())
        line(port.center, port.center + (port.east ? Vec3::sAxisX() : Vec3::sAxisZ()) * port.length, port.width);
    for (const auto& airport : airports) {
        rectangle(airport.center_x - 115, airport.runway_z - 220, 77, 380, pavement);
        const auto airport_line = [&](AirportPoint a, AirportPoint b, float width) {
            line(Vec3(a.x, 0, a.z), Vec3(b.x, 0, b.z), width);
        };
        airport_line({airport.center_x - 78, airport.runway_z - 200}, {airport.center_x, airport.runway_z - 200}, 16);
        if (airport.international) {
            rectangle(airport.apron_x() - 15, airport.apron_z() - 60, 30, 120, pavement);
            const auto access = airport.access_points();
            for (int i = 1; i < int(access.size()); ++i) airport_line(access[i - 1], access[i], airport.gate_width());
        } else {
            const float apron_end = airport.runway_z + airport.departure * (airport.runway_length() / 2 - 12);
            const float apron_start = std::min(airport.plane_z(), apron_end);
            rectangle(airport.apron_x() - 15, apron_start, 30, std::max(airport.plane_z(), apron_end) - apron_start, pavement);
            airport_line({airport.center_x, airport.plane_z()}, {airport.apron_x(), airport.plane_z()}, 10);
        }
        for (int runway = 0; runway < airport.runway_count(); ++runway) {
            airport_line(airport.point(-airport.runway_length() / 2, 0, runway), airport.point(airport.runway_length() / 2, 0, runway), airport.runway_width());
            if (airport.international) {
                airport_line({airport.apron_x(), airport.apron_z()}, {airport.apron_x(), airport.plane_z()}, 12);
                airport_line({airport.apron_x(), airport.plane_z()}, airport.point(airport.plane_along(), 0, runway), 12);
            }
        }
    }
    for (const auto& b : env.buildings())
        rectangle(b.center.GetX() - b.size.GetX() / 2, b.center.GetZ() - b.size.GetZ() / 2,
            b.size.GetX(), b.size.GetZ(), {145, 145, 145, 255});
    map_ = map.upload();
    minimap_shader_ = LoadShaderFromMemory(nullptr, minimap_fragment);
}
void EnvironmentRenderer::draw_map(Matrix transform, Shader shader) const {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Material material = map_.materials[0];
    if (shader.id) material.shader = shader;
    DrawMesh(map_.meshes[0], material, transform);
    rlEnableBackfaceCulling();
}
void EnvironmentRenderer::load_trees(const Environment& env) {
    if (env.trees().empty()) return;
    const std::string relative = "assets/models/tree1.glb";
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    const std::string path = FileExists(bundled.c_str()) ? bundled : relative;
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "TREES: tree1.glb missing");
        return;
    }
    trees_ = LoadModel(path.c_str());
    if (trees_.meshCount <= 0 || !trees_.meshes || !trees_.materials || !trees_.meshMaterial) return;
    for (int i = 0; i < trees_.meshCount; ++i) {
        const auto& mesh = trees_.meshes[i];
        if (!mesh.vertices || mesh.triangleCount <= 0 || trees_.meshMaterial[i] < 0
            || trees_.meshMaterial[i] >= trees_.materialCount) {
            TraceLog(LOG_WARNING, "TREES: Invalid tree1.glb geometry");
            return;
        }
    }
    const auto bounds = GetModelBoundingBox(trees_);
    // Center the trunk in X/Z and put the root on the shared terrain height.
    const Vec3 origin((bounds.min.x + bounds.max.x) / 2, bounds.min.y, (bounds.min.z + bounds.max.z) / 2);
    tree_shader_ = LoadShaderFromMemory(tree_vertex, tree_fragment.c_str());
    tree_camera_ = GetShaderLocation(tree_shader_, "cameraPosition");
    tree_texture_ = trees_.materials[trees_.meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE].texture;
    if (tree_texture_.id != 0 && tree_texture_.id != rlGetTextureIdDefault()) {
        GenTextureMipmaps(&tree_texture_);
        SetTextureFilter(tree_texture_, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(tree_texture_, TEXTURE_WRAP_CLAMP);
    }
    std::map<std::pair<int, int>, std::vector<Tree>> cells;
    for (const auto& tree : env.trees()) cells[cell(tree.base)].push_back(tree);
    for (int i = 0; i < trees_.meshCount; ++i) {
        auto& material = trees_.materials[trees_.meshMaterial[i]];
        material.shader = tree_shader_;
        if (material.maps[MATERIAL_MAP_DIFFUSE].texture.id == tree_texture_.id)
            material.maps[MATERIAL_MAP_DIFFUSE].texture = tree_texture_;
        for (const auto& part : cells) {
            Model model = LoadModelFromMesh(batch_tree_mesh(trees_.meshes[i], part.second, origin));
            model.materials[0].shader = tree_shader_;
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE] = material.maps[MATERIAL_MAP_DIFFUSE];
            tree_chunks_.push_back(chunk(model));
        }
    }
    trees_ready_ = true;
    TraceLog(LOG_INFO, "TREES: Loaded tree1.glb for all %i island trees", int(env.trees().size()));
    TraceLog(LOG_INFO, "SCENE: %i city chunks, %i tree chunks, %i local lamps", int(city_chunks_.size()), int(tree_chunks_.size()), int(local_lights_.size()));
}

EnvironmentRenderer::~EnvironmentRenderer() {
    UnloadModel(terrain_); UnloadModel(ocean_);
    UnloadModel(grass_); UnloadModel(sand_);
    UnloadModel(roads_); UnloadModel(signs_);
    UnloadModel(lights_); UnloadModel(glows_);
    for (auto& part : city_chunks_) UnloadModel(part.model);
    for (auto& part : tree_chunks_) UnloadModel(part.model);
    if (trees_.meshes || trees_.materials) UnloadModel(trees_);
    if (tree_texture_.id != 0 && tree_texture_.id != rlGetTextureIdDefault()) UnloadTexture(tree_texture_);
    if (grass_texture_.id != 0) UnloadTexture(grass_texture_);
    if (sand_texture_.id != 0) UnloadTexture(sand_texture_);
    if (asphalt_texture_.id != 0) UnloadTexture(asphalt_texture_);
    if (sign_texture_.id) UnloadTexture(sign_texture_);
    UnloadModel(map_);
    UnloadShader(minimap_shader_);
    UnloadShader(land_shader_); UnloadShader(water_shader_);
    UnloadShader(sky_shader_); UnloadShader(light_shader_);
    if (tree_shader_.id != 0) UnloadShader(tree_shader_);
}
void EnvironmentRenderer::draw_sky(const Camera3D& camera, const Daylight& light, float time, const GraphicsSettings& settings) {
    const Vector2 screen{float(GetScreenWidth()), float(GetScreenHeight())};
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3CrossProduct(right, forward);
    const float tan_fov = std::tan(camera.fovy * pi / 360);
    const auto vec3 = [&](const char* name, const Vector3& value) {
        SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, name), &value, SHADER_UNIFORM_VEC3);
    };
    SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, "screenSize"), &screen, SHADER_UNIFORM_VEC2);
    SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, "tanFov"), &tan_fov, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, "night"), &light.night, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, "time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sky_shader_, GetShaderLocation(sky_shader_, "brightness"), &settings.brightness, SHADER_UNIFORM_FLOAT);
    vec3("cameraForward", forward); vec3("cameraRight", right); vec3("cameraUp", up);
    vec3("horizonColor", light.horizon); vec3("zenithColor", light.zenith); vec3("sunDirection", light.sun_direction);
    BeginShaderMode(sky_shader_);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
    EndShaderMode();
}
void EnvironmentRenderer::draw(const Camera3D& camera, float time, const Daylight& light, const SceneLighting& lighting, const GraphicsSettings& settings) {
    for (Shader shader : {land_shader_, water_shader_, tree_shader_}) lighting.apply(shader, camera, light, settings);
    SetShaderValue(land_shader_, land_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_time_, &time, SHADER_UNIFORM_FLOAT);
    DrawModel(ocean_, {0, 0, 0}, 1, WHITE);
    DrawModel(terrain_, {0, 0, 0}, 1, WHITE);
    DrawModel(grass_, {0, 0, 0}, 1, WHITE);
    DrawModel(sand_, {0, 0, 0}, 1, WHITE);
    // Native depth bias keeps thin pavement and markings clear of the terrain at long distances.
    static const auto enable = reinterpret_cast<void (*)(unsigned int)>(rlGetProcAddress("glEnable"));
    static const auto disable = reinterpret_cast<void (*)(unsigned int)>(rlGetProcAddress("glDisable"));
    static const auto offset = reinterpret_cast<void (*)(float, float)>(rlGetProcAddress("glPolygonOffset"));
    constexpr unsigned int polygon_offset_fill = 0x8037;
    const bool depth_bias = enable && disable && offset;
    rlDrawRenderBatchActive();
    if (depth_bias) { enable(polygon_offset_fill); offset(-1, -1); }
    DrawModel(roads_, {0, 0, 0}, 1, WHITE);
    if (depth_bias) offset(-1, -2);
    for (const auto& part : city_chunks_) if (nearby(part, camera.position, settings.view_distance)) DrawModel(part.model, {0, 0, 0}, 1, WHITE);
    rlDrawRenderBatchActive();
    if (depth_bias) disable(polygon_offset_fill);
    SetShaderValue(tree_shader_, tree_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    const float sign_emission = light.night * .8f;
    SetShaderValue(tree_shader_, sign_emission_, &sign_emission, SHADER_UNIFORM_FLOAT);
    rlDisableBackfaceCulling(); DrawModel(signs_, {0, 0, 0}, 1, WHITE); rlDrawRenderBatchActive(); rlEnableBackfaceCulling();
    const float no_emission = 0;
    SetShaderValue(tree_shader_, sign_emission_, &no_emission, SHADER_UNIFORM_FLOAT);
    if (trees_ready_) {
        SetShaderValue(tree_shader_, tree_camera_, &camera.position, SHADER_UNIFORM_VEC3);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        for (const auto& part : tree_chunks_) if (nearby(part, camera.position, settings.view_distance)) DrawModel(part.model, {0, 0, 0}, 1, WHITE);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
    if (light.night > .001f) {
        SetShaderValue(light_shader_, GetShaderLocation(light_shader_, "cameraPosition"), &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(light_shader_, GetShaderLocation(light_shader_, "night"), &light.night, SHADER_UNIFORM_FLOAT);
        SetShaderValue(light_shader_, GetShaderLocation(light_shader_, "brightness"), &settings.brightness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(light_shader_, GetShaderLocation(light_shader_, "viewDistance"), &settings.view_distance, SHADER_UNIFORM_FLOAT);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawModel(lights_, {0, 0, 0}, 1, WHITE);
        rlDisableDepthMask(); DrawModel(glows_, {0, 0, 0}, 1, WHITE); rlDrawRenderBatchActive(); rlEnableDepthMask();
        EndBlendMode();
    }
}
void EnvironmentRenderer::draw_shadow(Shader shader, const Vector3& focus, float distance) {
    const auto draw = [&](Model& model) {
        for (int i = 0; i < model.meshCount; ++i) {
            Material material = model.materials[model.meshMaterial[i]];
            material.shader = shader;
            DrawMesh(model.meshes[i], material, model.transform);
        }
    };
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (auto& part : city_chunks_) if (nearby(part, focus, distance)) draw(part.model);
    for (auto& part : tree_chunks_) if (nearby(part, focus, distance)) draw(part.model);
    if (signs_.meshes) draw(signs_);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
void EnvironmentRenderer::minimap(Vec3 player_position, Vec3 player_forward, const Camera3D& camera, const Police* police, bool in_vehicle) const {
    const MinimapView map(GetScreenHeight(), player_position, player_forward, camera, in_vehicle);
    const auto bounds = map.bounds;
    const Vector4 clip{bounds.x, float(GetRenderHeight()) - bounds.y - bounds.height, bounds.width, bounds.height};
    SetShaderValue(minimap_shader_, GetShaderLocation(minimap_shader_, "clipBounds"), &clip, SHADER_UNIFORM_VEC4);
    BeginScissorMode(int(bounds.x), int(bounds.y), int(bounds.width), int(bounds.height));
    BeginShaderMode(minimap_shader_);
    DrawRectangleRec(bounds, map_water);
    const Matrix transform = map.transform();
    draw_map(transform, minimap_shader_);
    if (police && police->wanted().stars()) {
        // The search area lies on the same plane; markers stay readable in screen space.
        const Vec3 center = police->wanted().last_seen();
        rlDrawRenderBatchActive();
        const Matrix previous_view = rlGetMatrixModelview();
        Matrix plane = transform;
        plane.m10 = 0; // Circle batches carry a UI depth; keep the search area on the map plane.
        rlSetMatrixModelview(MatrixMultiply(plane, previous_view));
        DrawCircleV({center.GetX(), center.GetZ()}, police->wanted().radius(), Fade(int(GetTime() * 2) % 2 ? BLUE : RED, .18f));
        rlDrawRenderBatchActive();
        rlSetMatrixModelview(previous_view);
    }
    if (police && police->wanted().stars()) for (const auto& unit : police->units()) if (unit.active) {
        const auto p = map.project(unit.car->position());
        if (!unit.car->destroyed() && map.contains(p)
            && std::any_of(unit.officers.begin(), unit.officers.end(), [](const PoliceOfficer& officer) { return officer.seated && officer.character->alive(); }))
            DrawRectangle(int(p.x - 3), int(p.y - 3), 6, 6, unit.claimed ? SKYBLUE : Color{79, 142, 255, 255});
        for (const auto& officer : unit.officers) if (!officer.seated && officer.character->alive()) {
            const auto dot = map.project(officer.character->position());
            if (map.contains(dot)) DrawCircleV(dot, 3.3f, {79, 142, 255, 255});
        }
    }
    Vec3 heading(player_forward.GetX(), 0, player_forward.GetZ());
    heading = heading.LengthSq() > 1e-8f ? heading.Normalized() : map.forward;
    const auto direction = map.project(player_position + heading);
    const Vector2 delta = Vector2Scale(Vector2Normalize(Vector2Subtract(direction, map.anchor)), 9.6f);
    const Vector2 tip = Vector2Add(map.anchor, delta);
    const Vector2 a{map.anchor.x - delta.x * .5f - delta.y * .55f, map.anchor.y - delta.y * .5f + delta.x * .55f};
    const Vector2 b{map.anchor.x - delta.x * .5f + delta.y * .55f, map.anchor.y - delta.y * .5f - delta.x * .55f};
    DrawCircleV(map.anchor, 8, {19, 28, 45, 230});
    DrawTriangle(tip, b, a, RAYWHITE);
    EndShaderMode();
    EndScissorMode();
}
void EnvironmentRenderer::world_map(const WorldMapView& view, Rectangle viewport, Vec3 player_position, Vec3 player_forward, const Police* police) const {
    BeginScissorMode(int(viewport.x), int(viewport.y), int(viewport.width), int(viewport.height));
    DrawRectangleRec(viewport, map_water);
    Matrix transform = MatrixTranslate(-view.center.x, -view.center.y, 0);
    transform = MatrixMultiply(transform, MatrixScale(view.scale, view.scale, 1));
    transform = MatrixMultiply(transform, MatrixTranslate(viewport.x + viewport.width / 2, viewport.y + viewport.height / 2, 0));
    draw_map(transform);
    const auto inside = [&](Vector2 p) { return p.x >= viewport.x + 8 && p.x <= viewport.x + viewport.width - 8
        && p.y >= viewport.y + 8 && p.y <= viewport.y + viewport.height - 8; };
    if (police && police->wanted().stars()) {
        const auto p = view.project(police->wanted().last_seen(), viewport);
        const float radius = police->wanted().radius() * view.scale;
        DrawCircleV(p, radius, Fade(int(GetTime() * 2) % 2 ? BLUE : RED, .18f));
    }
    if (police && police->wanted().stars()) for (const auto& unit : police->units()) if (unit.active) {
        const auto p = view.project(unit.car->position(), viewport);
        if (!unit.car->destroyed() && inside(p)
            && std::any_of(unit.officers.begin(), unit.officers.end(), [](const PoliceOfficer& officer) { return officer.seated && officer.character->alive(); }))
            DrawRectangle(int(p.x - 3), int(p.y - 3), 6, 6, unit.claimed ? SKYBLUE : Color{79, 142, 255, 255});
        for (const auto& officer : unit.officers) if (!officer.seated && officer.character->alive()) {
            const auto dot = view.project(officer.character->position(), viewport);
            if (inside(dot)) DrawCircleV(dot, 3.3f, {79, 142, 255, 255});
        }
    }
    for (std::size_t i : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(6), std::size_t(7), std::size_t(8), std::size_t(9), std::size_t(10)}) {
        if (i == 2 && view.scale < .15f) continue;
        const auto& island = Environment::islands()[i];
        const auto p = view.project(island.center, viewport);
        if (!inside(p)) continue;
        const int width = forza::ui::measure_text(island.name, 16);
        DrawRectangle(int(p.x) - width / 2 - 5, int(p.y) - 28, width + 10, 24, {19, 28, 45, 205});
        forza::ui::draw_text(island.name, int(p.x) - width / 2, int(p.y) - 24, 16, RAYWHITE);
    }
    for (const auto& airport : airports) {
        const auto p = view.project(Vec3(airport.center_x, 0, airport.runway_z), viewport);
        if (inside(p)) DrawRectangle(int(p.x - 4), int(p.y - 4), 8, 8, YELLOW);
    }
    const auto player = view.project(player_position, viewport);
    if (inside(player)) {
        DrawCircleV(player, 11, {19, 28, 45, 255});
        DrawPoly(player, 3, 9, std::atan2(player_forward.GetZ(), player_forward.GetX()) * RAD2DEG, RAYWHITE);
        forza::ui::draw_text("YOU", int(player.x + 14), int(player.y - 7), 14, RAYWHITE);
    }
    const float bar_meters = view.scale >= .5f ? 100 : view.scale >= .15f ? 500 : 1000;
    const float bar_width = bar_meters * view.scale;
    const float bx = viewport.x + 18, by = viewport.y + viewport.height - 20;
    DrawRectangle(int(bx - 6), int(by - 28), int(bar_width + 20), 40, {19, 28, 45, 210});
    DrawLineEx({bx, by}, {bx + bar_width, by}, 3, RAYWHITE);
    forza::ui::draw_text(TextFormat("%.0f m", double(bar_meters)), int(bx), int(by - 23), 14, RAYWHITE);
    EndScissorMode();
    DrawRectangleLinesEx(viewport, 2, {115, 157, 174, 255});
}
} // namespace forza
