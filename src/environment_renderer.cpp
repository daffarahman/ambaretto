#include "ui_font.hpp"
#include "environment_renderer.hpp"
#include "traffic.hpp"
#include "airport.hpp"
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace forza {
namespace {
constexpr float pi = 3.14159265359f;
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
        const float top = float(row) / 8, bottom = float(row + 1) / 8;
        const float uv[] = {0, bottom, 1, bottom, 1, top, 0, bottom, 1, top, 0, top};
        std::copy(std::begin(uv), std::end(uv), texcoords.begin() + first);
    }
    void box(Vec3 center, Vec3 size, Color color, float yaw = 0) {
        const Vec3 h = size / 2;
        const Quat rotation = Quat::sRotation(Vec3::sAxisY(), yaw);
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
const char* land_fragment = R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec4 color;
uniform vec3 cameraPosition;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    // World coordinates keep the four-meter tiles continuous across cells.
    vec4 surface = texture(texture0, position.xz / 4.0) * color;
    float lighting = 0.58 + 0.42 * max(dot(normalize(normal), normalize(vec3(-0.45, 0.85, 0.30))), 0.0);
    float fog = smoothstep(2500.0, 18000.0, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(surface.rgb * lighting, vec3(0.64, 0.80, 0.87), fog * 0.80), surface.a);
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
const char* tree_fragment = R"GLSL(#version 330
in vec3 position;
in vec3 normal;
in vec2 texCoord;
in vec4 color;
uniform vec3 cameraPosition;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    vec4 surface = texture(texture0, texCoord) * colDiffuse * color;
    // Leaf cards need holes that neither hide scenery nor write depth.
    if (surface.a < 0.3) discard;
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    float lighting = 0.58 + 0.42 * max(dot(n, normalize(vec3(-0.45, 0.85, 0.30))), 0.0);
    float fog = smoothstep(2500.0, 18000.0, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(surface.rgb * lighting, vec3(0.64, 0.80, 0.87), fog * 0.80), 1.0);
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
const char* water_fragment = R"GLSL(#version 330
in vec3 position;
uniform vec3 cameraPosition;
uniform float time;
out vec4 finalColor;
void main() {
    vec3 color = mix(vec3(0.10, 0.60, 0.67), vec3(0.06, 0.34, 0.52),
        0.5 + 0.5 * sin(position.x * 0.0007 + position.z * 0.0005));
    float ripples = sin(position.x * 0.5 + position.z * 0.36 + time * 1.5) * sin(position.z * 0.21 - time);
    vec2 footprint = fwidth(position.xz * 0.5);
    color += 0.028 * ripples * exp(-dot(footprint, footprint));
    float fog = smoothstep(5000.0, 21000.0, distance(position.xz, cameraPosition.xz));
    finalColor = vec4(mix(color, vec3(0.64, 0.80, 0.87), fog), 1.0);
})GLSL";
}

EnvironmentRenderer::EnvironmentRenderer(const Environment& env) {
    MeshBuilder ground, grass, sand, roads, city, water, signs;
    grass_texture_ = load_terrain_texture("grass.png");
    sand_texture_ = load_terrain_texture("beach-sand.png");
    asphalt_texture_ = load_terrain_texture("asphalt.png");
    const Color asphalt = asphalt_texture_.id ? WHITE : Color{57, 65, 70, 255};
    for (const auto& t : env.triangles()) {
        if (t.surface == Surface::Seabed) continue;
        MeshBuilder* surface = &ground;
        Color tint = surface_color(t.surface);
        Vec3 lift = Vec3::sZero();
        if (t.deck) { surface = &roads; tint = asphalt; lift.SetY(.045f); }
        else if (t.surface == Surface::Grass || t.surface == Surface::Road) {
            surface = &grass; if (grass_texture_.id) tint = WHITE;
        } else if (t.surface == Surface::Sand) {
            surface = &sand; if (sand_texture_.id) tint = WHITE;
        }
        surface->triangle(env.vertices()[t.a] + lift, env.vertices()[t.b] + lift, env.vertices()[t.c] + lift, tint);
    }
    const auto deck_strip = [&](Vec3 a, Vec3 b, float width, Color color) {
        const Vec3 side = (b - a).Normalized().Cross(Vec3::sAxisY()) * (width / 2);
        const auto raised = [&](Vec3 p) { p.SetY(env.height(p.GetX(), p.GetZ()) + .085f); return p; };
        city.quad(raised(a - side), raised(a + side), raised(b + side), raised(b - side), color);
    };
    for (const auto& road : env.roads()) {
        const Vec3 direction = (road.b - road.a).Normalized();
        const Vec3 side = direction.Cross(Vec3::sAxisY());
        const float length = (road.b - road.a).Length();
        if (road.bridge < 0) {
            city.ribbon(env, road.a, road.b, road.width + 7, {172, 181, 175, 255}, .03f);
            roads.ribbon(env, road.a, road.b, road.width, asphalt, .055f);
        }
        for (float along = 0; along < length; along += 16) {
            const Vec3 a = road.a + direction * along, b = road.a + direction * std::min(along + 6, length);
            if (road.bridge >= 0) deck_strip(a, b, .24f, {244, 207, 99, 255});
            else city.ribbon(env, a, b, .24f, {244, 207, 99, 255}, .085f);
        }
        for (float sign : {-1.0f, 1.0f}) {
            const Vec3 offset = side * (sign * (road.width / 2 - 1));
            if (road.bridge >= 0) {
                for (float along = 0; along < length; along += 8)
                    deck_strip(road.a + direction * along + offset,
                        road.a + direction * std::min(along + 8, length) + offset, .18f, RAYWHITE);
            } else city.ribbon(env, road.a + offset, road.b + offset, .18f, RAYWHITE, .085f);
        }
    }

    Image atlas = GenImageColor(256, 512, BLANK);
    const char* labels[] = {"HOTEL", "CAFE", "CLUB", "MALL", "GAS", "MARINA", "MIAMI BEACH", "US 1"};
    const Color backgrounds[] = {{36, 136, 151, 255}, {190, 101, 64, 255}, {121, 44, 161, 255},
        {26, 73, 104, 255}, {34, 141, 104, 255}, {31, 100, 139, 255}, {210, 123, 141, 255}, {43, 105, 74, 255}};
    for (int row = 0; row < 8; ++row) {
        ImageDrawRectangle(&atlas, 0, row * 64, 256, 64, backgrounds[row]);
        ImageDrawRectangleLines(&atlas, {3, float(row * 64 + 3), 250, 58}, 2, RAYWHITE);
        const int font_size = row == 6 ? 22 : 32;
        forza::ui::draw_image_text(&atlas, labels[row], (256 - forza::ui::measure_text(labels[row], font_size)) / 2, row * 64 + 18, font_size, RAYWHITE);
    }
    sign_texture_ = LoadTextureFromImage(atlas); UnloadImage(atlas);
    SetTextureFilter(sign_texture_, TEXTURE_FILTER_BILINEAR);
    constexpr Color palette[] = {{233, 183, 176, 255}, {230, 217, 166, 255}, {146, 204, 200, 255},
        {182, 174, 214, 255}, {225, 234, 221, 255}};
    constexpr Color glass{48, 108, 136, 255}, concrete{203, 211, 208, 255};
    for (const auto& b : env.buildings()) {
        const float base = b.center.GetY() - b.size.GetY() / 2, roof = base + b.size.GetY();
        const float x = b.center.GetX(), z = b.center.GetZ(), sx = b.size.GetX(), sz = b.size.GetZ();
        const Vec3 front = b.face_east ? Vec3::sAxisX() : Vec3::sAxisZ();
        const Vec3 face = Vec3(x, base, z) + front * ((b.face_east ? sx : sz) / 2 + .08f);
        const Vec3 sign_right = b.face_east ? Vec3(0, 0, -1) : Vec3::sAxisX();
        const auto shop_sign = [&](int row, float y, float width) {
            signs.sign(face + front * .06f + Vec3(0, y, 0), sign_right, width, 2.6f, row);
        };
        Color body = palette[b.style % 5];
        if (b.kind == BuildingKind::Tower) body = Color{89, static_cast<unsigned char>(140 + b.style * 9), 166, 255};
        if (b.kind == BuildingKind::Warehouse || b.kind == BuildingKind::Hangar) body = Color{149, 169, 174, 255};
        city.box(b.solid_center(), b.solid_size(), body);
        const Vec3 solid = b.solid_center();
        city.box(Vec3(solid.GetX(), roof + .4f, z), Vec3(b.solid_size().GetX() + .8f, .8f, sz + .8f), concrete);
        if (b.kind == BuildingKind::Tower || b.kind == BuildingKind::Hotel) {
            const float floor = b.kind == BuildingKind::Hotel ? 4 : 9;
            for (float y = base + 4; y < roof - 1; y += floor) {
                for (float side : {-1.0f, 1.0f}) {
                    city.box(Vec3(x, y, z + side * (sz / 2 + .04f)), Vec3(sx - 5, floor * .55f, .08f), glass);
                    city.box(Vec3(x + side * (sx / 2 + .04f), y, z), Vec3(.08f, floor * .55f, sz - 5), glass);
                }
                if (b.kind == BuildingKind::Hotel)
                    city.box(Vec3(x, y - 1.3f, z), Vec3(sx + 1.6f, .3f, sz + 1.6f), RAYWHITE);
            }
            city.box(Vec3(x, roof + (b.kind == BuildingKind::Tower ? 5 : 2), z),
                Vec3(sx * .65f, b.kind == BuildingKind::Tower ? 10 : 4, sz * .65f), body);
            if (b.kind == BuildingKind::Hotel) shop_sign(0, 5, 16);
            else city.box(Vec3(x, base + 2, z), Vec3(sx + 4, 4, sz + 4), concrete);
        } else if (b.kind == BuildingKind::House) {
            // Terracotta pitched roofs and a shaded front porch.
            const Vec3 a(x - sx / 2 - 1, roof, z - sz / 2 - 1), c(x + sx / 2 + 1, roof, z - sz / 2 - 1);
            const Vec3 d(x - sx / 2 - 1, roof, z + sz / 2 + 1), e(x + sx / 2 + 1, roof, z + sz / 2 + 1);
            const Vec3 r1(x, roof + 4, a.GetZ()), r2(x, roof + 4, d.GetZ());
            city.quad(a, d, r2, r1, {172, 88, 61, 255}); city.quad(r1, r2, e, c, {192, 108, 77, 255});
            city.triangle(a, r1, c, body); city.triangle(d, e, r2, body);
            city.box(face + Vec3(0, 2, 0), Vec3(2.4f, 4, .12f), glass);
            city.box(face + Vec3(0, 4.4f, 2), Vec3(14, .35f, 4), RAYWHITE);
        } else if (b.kind == BuildingKind::GasStation) {
            city.ribbon(env, Vec3(x, 0, z - sz / 2), Vec3(x, 0, z + sz / 2), sx, {173, 179, 175, 255}, .035f);
            // Keep the canopy inside the station's collider footprint.
            city.box(Vec3(x + sx * .3f, base + 4.8f, z), Vec3(sx * .35f, .6f, sz - 4), {32, 153, 126, 255});
            for (float dz : {-12.0f, 0.0f, 12.0f}) {
                city.box(Vec3(x + sx * .3f, base + 1, z + dz), Vec3(1.8f, 2, 1.2f), RAYWHITE);
                city.box(Vec3(x + sx * .3f, base + 3, z + dz), Vec3(.25f, 3, .25f), concrete);
            }
            shop_sign(4, 6, 12);
            city.box(face + Vec3(0, 3, 0), Vec3(.25f, 6, .25f), concrete);
        } else if (b.kind == BuildingKind::Cafe || b.kind == BuildingKind::Club) {
            const bool club = b.kind == BuildingKind::Club;
            const Color accent = club ? Color{211, 94, 228, 255} : Color{240, 161, 98, 255};
            const Vec3 awning_size = b.face_east ? Vec3(3, .4f, sz - 4) : Vec3(sx - 4, .4f, 3);
            city.box(face + front * 1.4f + Vec3(0, 3.4f, 0), awning_size, accent);
            const Vec3 window_size = b.face_east ? Vec3(.1f, 2.8f, sz - 6) : Vec3(sx - 6, 2.8f, .1f);
            city.box(face + Vec3(0, 1.6f, 0), window_size, glass);
            shop_sign(club ? 2 : 1, club ? 7 : 5, 14);
            if (!club) for (float side : {-8.0f, 8.0f}) {
                const Vec3 table = face + front * 5 + sign_right * side;
                city.box(table + Vec3(0, .75f, 0), Vec3(1.8f, .15f, 1.8f), body);
                city.box(table + Vec3(0, .35f, 0), Vec3(.2f, .7f, .2f), concrete);
            }
        } else if (b.kind == BuildingKind::Mall) {
            city.box(face + Vec3(0, 5, 0), Vec3(sx - 14, 8, .2f), glass);
            city.box(face + Vec3(0, 10, 2), Vec3(sx - 4, 1, 4), concrete);
            shop_sign(3, 15, 25);
        } else if (b.kind == BuildingKind::ControlTower) {
            city.box(Vec3(x, roof - 3, z), Vec3(sx + 3, 5, sz + 3), glass);
            city.box(Vec3(x, roof + 3, z), Vec3(.2f, 6, .2f), RAYWHITE);
        } else {
            city.box(face + Vec3(0, 4, 0), Vec3(sx - 4, 6, .15f), glass);
            if (b.kind == BuildingKind::Warehouse) shop_sign(5, 10, 17);
        }
    }

    for (const auto& rail : env.barriers()) {
        city.box(rail.center, rail.size, {200, 209, 206, 255}, rail.yaw);
        city.box(rail.center + Vec3(0, .55f, 0), Vec3(.55f, .12f, rail.size.GetZ()), RAYWHITE, rail.yaw);
    }
    for (const auto& bridge : env.bridges()) {
        const Vec3 direction = (bridge.b - bridge.a).Normalized(), side = direction.Cross(Vec3::sAxisY());
        const float length = (bridge.b - bridge.a).Length(), heading = std::atan2(-direction.GetX(), -direction.GetZ());
        for (float d = 0; d < length; d += 8) {
            const Vec3 a = bridge.point(d / length), b = bridge.point(std::min(d + 8, length) / length);
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 offset = side * (sign * bridge.width / 2);
                city.quad(a + offset, b + offset, b + offset - Vec3(0, 1.2f, 0), a + offset - Vec3(0, 1.2f, 0), concrete);
                city.quad(a + offset - Vec3(0, 1.2f, 0), b + offset - Vec3(0, 1.2f, 0), b + offset, a + offset, concrete);
            }
        }
        for (float d = 40; d < length - 20; d += 64) {
            const Vec3 p = bridge.point(d / length);
            city.box(Vec3(p.GetX(), (p.GetY() - 8) / 2, p.GetZ()), Vec3(bridge.width - 5, p.GetY() + 6, 2.2f), concrete, heading);
        }
        for (float d = 35; d < length - 10; d += 96) for (float sign : {-1.0f, 1.0f}) {
            const Vec3 p = bridge.point(d / length) + side * (sign * (bridge.width / 2 - .8f));
            city.box(p + Vec3(0, 4, 0), Vec3(.18f, 8, .18f), {86, 111, 121, 255});
            city.box(p + Vec3(0, 8, 0) - side * sign, Vec3(2.5f, .2f, .6f), {252, 236, 170, 255}, heading);
        }
    }
    for (const auto& port : env.ports()) {
        const Vec3 direction = port.east ? Vec3::sAxisX() : Vec3::sAxisZ(), side = direction.Cross(Vec3::sAxisY());
        const Vec3 deck = port.center + direction * 110 - Vec3(0, .6f, 0);
        city.box(deck, port.east ? Vec3(220, 1.2f, 8) : Vec3(8, 1.2f, 220), {149, 116, 80, 255});
        signs.sign(port.center + Vec3(0, 4, 0), Vec3::sAxisX(), 14, 3, 5);
        city.box(port.center + Vec3(0, 2, 0), Vec3(.3f, 4, .3f), concrete);
        for (float d = 10; d < 220; d += 25) {
            const Vec3 p = port.center + direction * d;
            city.box(p - Vec3(0, 3, 0), Vec3(.7f, 6, .7f), {91, 84, 64, 255});
            if (d < 30) continue;
            const Vec3 boat = p + side * 14;
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
        const float z = -820 + i * 44.0f;
        const float coast = (z + 100) / 1650;
        const float x = 2200 + 500 * std::sqrt(.94f * .94f - coast * coast);
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

    const float ax = Airport::center_x, rz = Airport::runway_z;
    roads.ribbon(env, Vec3(ax - 156, 0, Airport::apron_z), Vec3(ax + 62, 0, Airport::apron_z), 24, asphalt, .06f);
    roads.ribbon(env, Vec3(Airport::plane_x, 0, Airport::apron_z), Vec3(Airport::plane_x, 0, rz), 10, asphalt, .06f);
    roads.ribbon(env, Vec3(ax - 168, 0, rz), Vec3(ax + 168, 0, rz), 24, asphalt, .06f);
    for (float side : {-10.8f, 10.8f})
        city.ribbon(env, Vec3(ax - 168, 0, rz + side), Vec3(ax + 168, 0, rz + side), .35f, RAYWHITE, .09f);
    for (int x = -125; x < 130; x += 20)
        city.ribbon(env, Vec3(ax + x, 0, rz), Vec3(ax + x + 10, 0, rz), .45f, RAYWHITE, .09f);
    for (float end : {-1.0f, 1.0f}) for (float stripe : {-8.0f, -5.5f, -3.0f, 3.0f, 5.5f, 8.0f})
        city.ribbon(env, Vec3(ax + end * 164, 0, rz + stripe), Vec3(ax + end * 154, 0, rz + stripe), 1.4f, RAYWHITE, .09f);
    city.ribbon(env, Vec3(ax, 0, Airport::apron_z), Vec3(Airport::plane_x, 0, Airport::apron_z), .25f, YELLOW, .09f);
    city.ribbon(env, Vec3(Airport::plane_x, 0, Airport::apron_z), Vec3(Airport::plane_x, 0, rz), .25f, YELLOW, .09f);
    for (int x = -160; x <= 160; x += 20) for (float side : {-11.8f, 11.8f})
        city.box(Vec3(ax + x, Airport::elevation + .17f, rz + side), Vec3(.3f, .25f, .3f), {245, 233, 166, 255});
    const Vec3 sock(ax + 112, Airport::elevation, Airport::apron_z - 15);
    city.box(sock + Vec3(0, 3, 0), Vec3(.18f, 6, .18f), concrete);
    for (int i = 0; i < 5; ++i)
        city.box(sock + Vec3(.5f + i * .5f, 5.8f - i * .09f, 0),
            Vec3(.5f, .6f - i * .075f, .6f - i * .075f), i % 2 ? RAYWHITE : ORANGE);
    for (std::size_t i = 3; i < env.bridges().size(); ++i) {
        const auto& bridge = env.bridges()[i];
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
    city_ = city.upload(); ocean_ = water.upload(); signs_ = signs.upload();
    land_shader_ = LoadShaderFromMemory(land_vertex, land_fragment);
    water_shader_ = LoadShaderFromMemory(water_vertex, water_fragment);
    for (Model* model : {&terrain_, &grass_, &sand_, &roads_, &city_})
        if (model->materials) model->materials[0].shader = land_shader_;
    if (grass_.materials && grass_texture_.id) SetMaterialTexture(&grass_.materials[0], MATERIAL_MAP_DIFFUSE, grass_texture_);
    if (sand_.materials && sand_texture_.id) SetMaterialTexture(&sand_.materials[0], MATERIAL_MAP_DIFFUSE, sand_texture_);
    if (roads_.materials && asphalt_texture_.id) SetMaterialTexture(&roads_.materials[0], MATERIAL_MAP_DIFFUSE, asphalt_texture_);
    ocean_.materials[0].shader = water_shader_;
    land_camera_ = GetShaderLocation(land_shader_, "cameraPosition");
    water_camera_ = GetShaderLocation(water_shader_, "cameraPosition");
    water_time_ = GetShaderLocation(water_shader_, "time");
    load_trees(env);
    if (!tree_shader_.id) {
        tree_shader_ = LoadShaderFromMemory(tree_vertex, tree_fragment);
        tree_camera_ = GetShaderLocation(tree_shader_, "cameraPosition");
    }
    if (signs_.materials) {
        signs_.materials[0].shader = tree_shader_;
        SetMaterialTexture(&signs_.materials[0], MATERIAL_MAP_DIFFUSE, sign_texture_);
    }
    load_minimap(env);
}

void EnvironmentRenderer::load_minimap(const Environment& env) {
    constexpr int size = 1024;
    Image map = GenImageColor(size, size, {43, 116, 148, 255});
    for (int z = 0; z < size; ++z) for (int x = 0; x < size; ++x) {
        const float y = env.terrain_height((x + .5f) * (2 * Environment::extent / size) - Environment::extent,
            (z + .5f) * (2 * Environment::extent / size) - Environment::extent);
        if (y > .1f) ImageDrawPixel(&map, x, z, y < 2.6f ? Color{225, 203, 155, 255} : Color{110, 157, 112, 255});
    }
    const auto pixel = [](float value) { return int((value + Environment::extent) * size / (2 * Environment::extent)); };
    for (const auto& road : env.roads()) {
        const Color color = road.bridge >= 0 ? Color{221, 195, 134, 255} : Color{66, 78, 85, 255};
        ImageDrawLine(&map, pixel(road.a.GetX()), pixel(road.a.GetZ()), pixel(road.b.GetX()), pixel(road.b.GetZ()), color);
    }
    for (const auto& b : env.buildings())
        ImageDrawRectangle(&map, pixel(b.center.GetX() - b.size.GetX() / 2), pixel(b.center.GetZ() - b.size.GetZ() / 2),
            std::max(1, int(b.size.GetX() / 16)), std::max(1, int(b.size.GetZ() / 16)), {157, 164, 162, 255});
    minimap_texture_ = LoadTextureFromImage(map); UnloadImage(map);
    SetTextureFilter(minimap_texture_, TEXTURE_FILTER_BILINEAR);
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
    tree_shader_ = LoadShaderFromMemory(tree_vertex, tree_fragment);
    tree_camera_ = GetShaderLocation(tree_shader_, "cameraPosition");
    tree_texture_ = trees_.materials[trees_.meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE].texture;
    if (tree_texture_.id != 0 && tree_texture_.id != rlGetTextureIdDefault()) {
        GenTextureMipmaps(&tree_texture_);
        SetTextureFilter(tree_texture_, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(tree_texture_, TEXTURE_WRAP_CLAMP);
    }
    for (int i = 0; i < trees_.meshCount; ++i) {
        Mesh batch = batch_tree_mesh(trees_.meshes[i], env.trees(), origin);
        UnloadMesh(trees_.meshes[i]);
        trees_.meshes[i] = batch;
        auto& material = trees_.materials[trees_.meshMaterial[i]];
        material.shader = tree_shader_;
        if (material.maps[MATERIAL_MAP_DIFFUSE].texture.id == tree_texture_.id)
            material.maps[MATERIAL_MAP_DIFFUSE].texture = tree_texture_;
    }
    trees_ready_ = true;
    TraceLog(LOG_INFO, "TREES: Loaded tree1.glb for all %i island trees", int(env.trees().size()));
}

EnvironmentRenderer::~EnvironmentRenderer() {
    UnloadModel(terrain_); UnloadModel(city_); UnloadModel(ocean_);
    UnloadModel(grass_); UnloadModel(sand_);
    UnloadModel(roads_); UnloadModel(signs_);
    if (trees_.meshes || trees_.materials) UnloadModel(trees_);
    if (tree_texture_.id != 0 && tree_texture_.id != rlGetTextureIdDefault()) UnloadTexture(tree_texture_);
    if (grass_texture_.id != 0) UnloadTexture(grass_texture_);
    if (sand_texture_.id != 0) UnloadTexture(sand_texture_);
    if (asphalt_texture_.id != 0) UnloadTexture(asphalt_texture_);
    if (sign_texture_.id) UnloadTexture(sign_texture_);
    if (minimap_texture_.id) UnloadTexture(minimap_texture_);
    UnloadShader(land_shader_); UnloadShader(water_shader_);
    if (tree_shader_.id != 0) UnloadShader(tree_shader_);
}
void EnvironmentRenderer::draw(const Camera3D& camera, float time) {
    SetShaderValue(land_shader_, land_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(water_shader_, water_time_, &time, SHADER_UNIFORM_FLOAT);
    DrawModel(ocean_, {0, 0, 0}, 1, WHITE);
    DrawModel(terrain_, {0, 0, 0}, 1, WHITE);
    DrawModel(grass_, {0, 0, 0}, 1, WHITE);
    DrawModel(sand_, {0, 0, 0}, 1, WHITE);
    DrawModel(roads_, {0, 0, 0}, 1, WHITE);
    DrawModel(city_, {0, 0, 0}, 1, WHITE);
    SetShaderValue(tree_shader_, tree_camera_, &camera.position, SHADER_UNIFORM_VEC3);
    rlDisableBackfaceCulling(); DrawModel(signs_, {0, 0, 0}, 1, WHITE); rlDrawRenderBatchActive(); rlEnableBackfaceCulling();
    if (trees_ready_) {
        SetShaderValue(tree_shader_, tree_camera_, &camera.position, SHADER_UNIFORM_VEC3);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        DrawModel(trees_, {0, 0, 0}, 1, WHITE);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
}
void EnvironmentRenderer::minimap(const Environment&, const Car& car, const Plane& plane, Vec3 player_position, Vec3 player_forward, int screen_width, const Traffic* traffic) const {
    const float left = float(screen_width - 220), top = 52, range = 700, scale = 100 / range;
    DrawRectangle(int(left - 8), int(top - 8), 216, 248, {22, 39, 48, 230});
    const float pixels = minimap_texture_.width / (2 * Environment::extent);
    const Rectangle source{(player_position.GetX() - range + Environment::extent) * pixels,
        (player_position.GetZ() - range + Environment::extent) * pixels, range * 2 * pixels, range * 2 * pixels};
    DrawTexturePro(minimap_texture_, source, {left, top, 200, 200}, {0, 0}, 0, WHITE);
    const auto dot = [&](Vec3 p) {
        return Vector2{left + 100 + (p.GetX() - player_position.GetX()) * scale,
            top + 100 + (p.GetZ() - player_position.GetZ()) * scale};
    };
    const auto inside = [&](Vector2 p) { return p.x > left + 3 && p.x < left + 197 && p.y > top + 3 && p.y < top + 197; };
    if (traffic) for (const auto& vehicle : traffic->cars()) {
        const auto p = dot(vehicle.car->position());
        if (inside(p)) DrawCircleV(p, 2.3f, vehicle.npc ? GREEN : SKYBLUE);
    }
    const auto car_dot = dot(car.position());
    if (inside(car_dot)) DrawRectangle(int(car_dot.x - 3), int(car_dot.y - 3), 6, 6, SKYBLUE);
    const auto plane_dot = dot(plane.position());
    if (inside(plane_dot)) DrawPoly(plane_dot, 3, 5, -90, YELLOW);
    const Vector2 center{left + 100, top + 100};
    DrawCircleV(center, 4, ORANGE);
    DrawLineEx(center, {center.x + player_forward.GetX() * 12, center.y + player_forward.GetZ() * 12}, 2, RAYWHITE);
    std::string title = Environment::district(player_position.GetX(), player_position.GetZ());
    const auto slash = title.find(" /");
    if (slash != std::string::npos) title.resize(slash);
    forza::ui::draw_text(title.c_str(), int(left + 5), int(top + 203), 11, RAYWHITE);
    forza::ui::draw_text("N UP  /  1.4 KM", int(left + 5), int(top + 217), 10, {157, 190, 200, 255});
}
} // namespace forza
