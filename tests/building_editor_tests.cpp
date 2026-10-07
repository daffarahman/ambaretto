#include "building_mesh.hpp"
#include "city.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace {
using namespace ambaretto;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::string encoded(const BuildingMesh& mesh) { std::ostringstream stream; mesh.write(stream); return stream.str(); }
void geometry() {
    std::string error; BuildingMesh mesh;
    require(mesh.set_footprint(4,4,error,true),error.c_str());
    const int wing = mesh.add_shape(BuildingShape::Box,Vec3(4,6,4),Vec3(16,0,0),error);
    require(wing==1 && mesh.faces.size()==12 && mesh.vertices.size()==16,"box did not join the single editable mesh");
    require(mesh.transform_part(wing,Vec3::sZero(),Vec3(0,45,0),Vec3::sReplicate(1),error),error.c_str());
    const auto stable = encoded(mesh);
    require(!mesh.transform_part(wing,Vec3(1000,0,0),Vec3::sZero(),Vec3::sReplicate(1),error) && encoded(mesh)==stable,
        "out-of-footprint transform flattened or partially changed a part");
    const int copy = mesh.duplicate_part(wing,Vec3(-32,0,0),error);
    require(copy==2 && mesh.part_vertices(copy).size()==8 && mesh.remove_part(copy,error),"part duplicate/delete failed");
    require(mesh.attach_shape(4,BuildingShape::Gable,3,error),error.c_str());
    require(mesh.validate(error),error.c_str());
    const int roof_part = mesh.faces[4].part;
    require(!mesh.part_vertices(roof_part).empty() && !mesh.transform_part(roof_part,Vec3(1,0,0),Vec3::sZero(),Vec3::sReplicate(1),error),
        "attached seam was moved independently");
    for (auto shape : {BuildingShape::Wedge,BuildingShape::Gable,BuildingShape::Hip}) {
        BuildingMesh sample; require(sample.set_footprint(4,4,error,true),error.c_str());
        require(sample.add_shape(shape,Vec3(4,3,4),Vec3(16,0,0),error)>0 && sample.validate(error),error.c_str());
        BuildingMesh roof; require(roof.attach_shape(4,shape,3,error) && roof.validate(error),error.c_str());
    }
    BuildingMesh touching; require(touching.set_footprint(4,4,error,true),error.c_str());
    require(touching.add_shape(BuildingShape::Box,Vec3(11.2f,12,11.2f),Vec3(11.2f,0,0),error)>0,error.c_str());
    require(touching.add_vertex(Vec3(0,1,0),error)>=0,error.c_str());
    const auto baked = touching.baked();
    require(baked.faces.size()==10 && baked.vertices.size()==16 && baked.validate(error),"bake retained coincident internal faces or loose vertices");
    std::istringstream stream(encoded(mesh)); BuildingMesh loaded;
    require(BuildingMesh::read(stream,loaded,error) && encoded(loaded)==encoded(mesh),"parts did not round-trip through v3");
    BuildingMesh legacy; std::ostringstream old;
    old << "\"Legacy building\" \"brick.png\" 11.2 12 11.2\n8 6\n";
    for (auto p : legacy.vertices) old << p.GetX() << ' ' << p.GetY() << ' ' << p.GetZ() << '\n';
    for (const auto& face : legacy.faces) {
        old << face.vertices.size();
        for (std::size_t i = 0; i<face.vertices.size(); ++i) old << ' ' << face.vertices[i] << ' ' << face.uv[i][0] << ' ' << face.uv[i][1];
        old << '\n';
    }
    old << "1\n0 \"window.png\" .25 .25 .5 .5\n";
    std::istringstream legacy_stream(old.str());
    require(BuildingMesh::read(legacy_stream,loaded,error,2) && loaded.materials.empty()
        && loaded.faces[0].projection==BuildingProjection::Manual && loaded.decals[0].texture=="window.png",
        "legacy v2 mesh lost its default material, UVs or decals");
}
std::vector<BuildingTriangle> overlays(const BuildingMesh& mesh) {
    std::vector<BuildingTriangle> result; for (const auto& t : mesh.triangles()) if (t.decal) result.push_back(t); return result;
}
void textures() {
    std::string error; BuildingMesh mesh; mesh.texture = "brick.png";
    mesh.decals.push_back({0,"window.png",.2f,.2f,.4f,.4f});
    const auto decal_before = overlays(mesh);
    const int roof = mesh.add_material("roof.png",error);
    require(roof==0 && mesh.add_material("roof.png",error)==roof && mesh.materials.size()==1,"material filenames were duplicated");
    require(mesh.set_face_material({4},roof,error) && mesh.set_uv({0,4},BuildingProjection::Box,2,{0,0},{1,1},0,error),error.c_str());
    const auto chart = mesh.faces[0].uv, projected = mesh.texture_uv(0);
    require(std::abs(std::abs(projected[3][0]-projected[0][0])-5.6f)<.001f
        && std::abs(std::abs(projected[1][1]-projected[0][1])-6)<.001f,"world tiling did not use metres per repeat");
    for (const auto& triangle : mesh.triangles()) if (!triangle.decal && triangle.face==4)
        require(triangle.texture=="roof.png" && triangle.repeat,"roof material or repeat addressing was not emitted");
    const auto decal_after = overlays(mesh);
    require(decal_before.size()==decal_after.size(),"material projection changed decal clipping");
    for (std::size_t i = 0; i<decal_before.size(); ++i) for (int v = 0; v<3; ++v)
        require((decal_before[i].points[v]-decal_after[i].points[v]).Length()<.0001f,"body texture projection moved a decal");
    mesh.size.SetY(24); const auto resized = mesh.texture_uv(0);
    require(mesh.faces[0].uv==chart && std::abs(std::abs(resized[1][1]-resized[0][1])-12)<.001f,
        "resizing stretched textures or altered the independent decal chart");
    require(mesh.subdivide_face(0,error) && mesh.decals.size()==1,"subdivision duplicated a logical decal");
    const int surface = mesh.decals[0].surface;
    for (const auto& face : mesh.faces) if (face.surface==surface)
        require(face.projection==BuildingProjection::Box && face.meters_per_repeat==2,"child faces lost tiling settings");
    const auto stable = encoded(mesh);
    require(!mesh.set_face_material({0,99999},roof,error) && encoded(mesh)==stable,"invalid face assignment partially changed materials");
    std::istringstream stream(stable); BuildingMesh loaded;
    require(BuildingMesh::read(stream,loaded,error) && encoded(loaded)==stable,"materials and surface anchors did not round-trip");
}
void uv_controls() {
    std::string error; BuildingMesh mesh;
    require(mesh.set_footprint(2,2,error,true) && mesh.set_uv({0},BuildingProjection::Planar,2,{.25f,-.5f},{2,.5f},90,error),error.c_str());
    const auto mapped = mesh.texture_uv(0);
    require(std::abs(mapped[0][0]-.25f)<.001f && std::abs(mapped[0][1]+.5f)<.001f
        && std::abs(mapped[1][0]-mapped[0][0]-3)<.001f,"offset, independent scale or rotation was not applied");
    const int surface = mesh.faces[0].surface;
    require(mesh.subdivide_face(0,error),error.c_str());
    for (const auto& face : mesh.faces) if (face.surface==surface)
        require(face.offset==BuildingUV{.25f,-.5f} && face.scale==BuildingUV{2,.5f} && face.rotation==90
            && face.projection==BuildingProjection::Planar,"subdivision lost manual UV transforms");
    std::istringstream stream(encoded(mesh)); BuildingMesh loaded;
    require(BuildingMesh::read(stream,loaded,error) && encoded(loaded)==encoded(mesh),"UV controls did not round-trip");
    BuildingMesh sloping; require(sloping.set_footprint(2,2,error,true),error.c_str());
    require(sloping.set_uv({0},BuildingProjection::Planar,2,{.1f,.2f},{1,1},0,error)
        && sloping.move_vertices({2,3},Vec3(0,0,2),error),error.c_str());
    const auto p = sloping.points(); const auto uv = sloping.texture_uv(0);
    const float uv_length = std::hypot(uv[1][0]-uv[0][0],uv[1][1]-uv[0][1]);
    require(std::abs(uv_length*2-(p[3]-p[0]).Length())<.001f && sloping.faces[0].offset==BuildingUV{.1f,.2f},
        "automatic planar projection stretched on an edited sloping wall or lost its tweak");
    require(sloping.set_uv({0},BuildingProjection::Planar,2,sloping.faces[0].offset,{1,1},0,error),error.c_str());
    BuildingMesh wedge; require(wedge.set_footprint(4,4,error,true),error.c_str());
    require(wedge.add_shape(BuildingShape::Wedge,Vec3(4,3,4),Vec3(16,0,0),error)>0,error.c_str());
    const int face = int(wedge.faces.size())-1; const auto box_uv = wedge.texture_uv(face); const auto world = wedge.points();
    const auto& corners = wedge.faces[face].vertices;
    require(std::abs(std::hypot(box_uv[1][0]-box_uv[0][0],box_uv[1][1]-box_uv[0][1])*2-(world[corners[1]]-world[corners[0]]).Length())<.001f,
        "box projection lost metre density on a sloping roof");
    const auto stable = encoded(wedge);
    require(!wedge.set_uv({face},BuildingProjection::Box,0,{0,0},{1,1},0,error)
        && !wedge.set_uv({face},BuildingProjection::Box,2,{0,0},{0,1},0,error)
        && !wedge.set_uv({face},BuildingProjection::Box,2,{0,0},{1,1},std::numeric_limits<float>::quiet_NaN(),error)
        && encoded(wedge)==stable,"invalid UV controls partially changed the mesh");
}
void decal_editing() {
    std::string error; BuildingMesh mesh;
    const int window = mesh.place_decal(0,Vec3(-4,8,-5.6f),"window.png",1,2,error);
    require(window==0 && mesh.decals[0].projected && mesh.decals[0].surface>=0,"surface click did not create a projected decal");
    Vec3 center;
    require(mesh.decal_center(window,center) && (center-Vec3(-4,8,-5.6f)).Length()<.0001f && !mesh.decal_center(-1,center),"decal selection centre is incorrect");
    auto area = [](const BuildingMesh& building,int index) {
        float result = 0; for (const auto& t : building.triangles()) if (t.decal_index==index)
            result += (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).Length()/2;
        return result;
    };
    require(std::abs(area(mesh,0)-2)<.001f,"projected decal physical dimensions are wrong");
    require(mesh.repeat_decal(window,5,2,false,error) && mesh.decals.size()==5,"window row was not created");
    for (int i = 0; i<5; ++i) require(std::abs(area(mesh,i)-2)<.001f,"repeated window was stretched or clipped");
    mesh.decals[0].rotation = 90;
    require(mesh.validate(error) && std::abs(area(mesh,0)-2)<.001f,"physical decal rotation changed its area");
    const auto rotated = overlays(mesh); float lo_x = 1000,hi_x = -1000,lo_y = 1000,hi_y = -1000;
    for (const auto& t : rotated) if (t.decal_index==0) for (auto p : t.points) {
        lo_x = std::min(lo_x,p.GetX()); hi_x = std::max(hi_x,p.GetX()); lo_y = std::min(lo_y,p.GetY()); hi_y = std::max(hi_y,p.GetY());
    }
    require(std::abs(hi_x-lo_x-2)<.001f && std::abs(hi_y-lo_y-1)<.001f,"decal angle was rotated in stretched UV space");
    require(mesh.slice(0,0,error) && mesh.move_decal(0,Vec3(1,8,-5.6f),error) && mesh.decals.size()==5,
        "moving across children of a split surface lost its anchor or duplicated records");
    require(mesh.decal_center(0,center) && (center-Vec3(1,8,-5.6f)).Length()<.0001f,"selection centre did not follow a split-wall move");
    require(mesh.move_decal(0,3,Vec3(5.6f,8,0),error),error.c_str());
    require(std::abs(mesh.decals[0].width*mesh.decals[0].meters[0]-1)<.001f
        && std::abs(mesh.decals[0].height*mesh.decals[0].meters[1]-2)<.001f && std::abs(area(mesh,0)-2)<.001f,
        "moving onto another wall changed physical dimensions");
    const auto stable = encoded(mesh);
    require(!mesh.repeat_decal(0,64,100,false,error) && !mesh.move_decal(0,Vec3(100,8,0),error) && encoded(mesh)==stable,
        "invalid decal edits partially changed the mesh");
    require(mesh.duplicate_decal(0,{.2f,0},error)>=0,"decal duplicate failed");
    std::istringstream stream(encoded(mesh)); BuildingMesh loaded;
    require(BuildingMesh::read(stream,loaded,error) && encoded(loaded)==encoded(mesh),"physical decal controls did not round-trip");
    BuildingMesh skewed; require(skewed.set_footprint(2,2,error,true) && skewed.move_vertices({2},Vec3(2,0,0),error),error.c_str());
    require(skewed.place_decal(0,Vec3(0,7,-5.6f),"door.png",1.5f,2,error)==0 && std::abs(area(skewed,0)-3)<.001f,
        "nonuniform wall chart sheared or stretched a projected decal");
    require(skewed.set_uv({0},BuildingProjection::Box,1,{.2f,.3f},{2,1},45,error) && std::abs(area(skewed,0)-3)<.001f,
        "texture UV controls altered decal geometry");
    BuildingMesh roof; require(roof.attach_shape(4,BuildingShape::Gable,3,error),error.c_str());
    Vec3 cap = Vec3::sZero(); const auto points = roof.points();
    for (int vertex : roof.faces[4].vertices) cap += points[vertex]/float(roof.faces[4].vertices.size());
    require(roof.place_decal(4,cap,"window.png",1,1,error)>=0,error.c_str());
    require(std::abs(area(roof,0)-1)<.001f,"gable window physical area is incorrect");
    BuildingMesh decorated_roof; decorated_roof.decals.push_back({4,"roof-mark.png"});
    require(decorated_roof.attach_shape(4,BuildingShape::Gable,3,error) && decorated_roof.decals.size()==1
        && !overlays(decorated_roof).empty(),"creating a gable end removed an existing roof decal");
}
}
int main() {
    try { geometry(); textures(); uv_controls(); decal_editing(); std::cout << "Building editor checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
