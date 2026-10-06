#pragma once
#include "vehicle.hpp"
#include <array>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace ambaretto {
using BuildingUV = std::array<float,2>;
struct BuildingFace {
    std::vector<int> vertices;
    std::vector<BuildingUV> uv{{0,1},{0,0},{1,0},{1,1}};
};
std::vector<std::array<int,3>> triangulate_polygon(const std::vector<Vec3>& points);
struct BuildingDecal {
    int face = 0;
    std::string texture;
    float u = .25f, v = .25f, width = .5f, height = .5f;
};
struct BuildingTriangle {
    std::array<Vec3,3> points;
    std::array<std::array<float,2>,3> uv;
    std::string texture;
    bool decal = false;
    int face = -1;
};
struct BuildingMesh {
    std::string name = "New building", texture;
    Vec3 size = Vec3(11.2f,12,11.2f);
    // Normalized coordinates retain the edited shape when resized.
    std::vector<Vec3> vertices{
        Vec3(-.5f,0,-.5f),Vec3(.5f,0,-.5f),Vec3(.5f,1,-.5f),Vec3(-.5f,1,-.5f),
        Vec3(-.5f,0,.5f),Vec3(.5f,0,.5f),Vec3(.5f,1,.5f),Vec3(-.5f,1,.5f)};
    std::vector<BuildingFace> faces{{{0,3,2,1}},{{4,5,6,7}},{{0,4,7,3}},{{1,2,6,5}},{{3,7,6,2}},{{0,1,5,4}}};
    std::vector<BuildingDecal> decals;
    static constexpr int max_vertices = 4096, max_faces = 8192, max_tiles = 128;
    static constexpr float tile_size = 11.2f, max_height = 120.f;
    std::array<int,2> footprint() const;
    bool set_footprint(int width,int depth,std::string& error,bool preserve_geometry = false);
    std::vector<Vec3> points(Vec3 base = Vec3::sZero()) const;
    std::vector<std::array<int,2>> edges() const;
    std::vector<BuildingTriangle> triangles(Vec3 base = Vec3::sZero()) const;
    bool fit_bounds(std::string& error);
    bool move_vertices(const std::vector<int>& selection,Vec3 delta,std::string& error);
    int add_vertex(Vec3 position,std::string& error);
    int split_edge(int a,int b,std::string& error);
    bool subdivide_face(int face,std::string& error);
    bool extrude_face(int face,float distance,std::string& error);
    bool inset_face(int face,float fraction,std::string& error);
    bool slice(int axis,float position,std::string& error);
    bool add_face(std::vector<int> selection,std::string& error);
    bool remove_faces(const std::vector<int>& selection,std::string& error);
    bool remove_vertices(const std::vector<int>& selection,std::string& error);
    bool validate(std::string& error) const;
    void write(std::ostream& stream) const;
    static bool read(std::istream& stream, BuildingMesh& mesh, std::string& error,int version = 2);
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, BuildingMesh& mesh, std::string& error);
};
std::vector<std::string> building_textures(const std::filesystem::path& directory, std::string& error);
bool valid_texture_filename(const std::string& filename);
std::vector<BuildingMesh> saved_buildings(const std::filesystem::path& directory, std::string& error);
} // namespace ambaretto
