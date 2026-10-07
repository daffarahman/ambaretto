#include "building_mesh.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ambaretto {
namespace {
bool fail(std::string& error,const char* message) { error = message; return false; }
bool finite(Vec3 p) { return std::isfinite(p.GetX()) && std::isfinite(p.GetY()) && std::isfinite(p.GetZ()); }
bool texture_name(const std::string& value) {
    if (value.empty()) return true;
    if (value.size()>128 || value.find_first_of("<>:\"/\\|?*")!=std::string::npos
        || !std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32 && c!=127;})) return false;
    std::string extension = std::filesystem::path(value).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    return extension==".png" || extension==".jpg" || extension==".jpeg" || extension==".bmp" || extension==".tga" || extension==".gif";
}
Vec3 polygon_normal(const std::vector<Vec3>& p) {
    Vec3 normal = Vec3::sZero();
    for (std::size_t i = 0; i<p.size(); ++i) normal += p[i].Cross(p[(i+1)%p.size()]);
    return normal.NormalizedOr(Vec3::sZero());
}
void projection_frame(BuildingFace& face,const std::vector<Vec3>& points) {
    std::vector<Vec3> polygon; for (int vertex : face.vertices) polygon.push_back(points[vertex]);
    const Vec3 normal = polygon_normal(polygon);
    const Vec3 right = std::abs(normal.GetY())<.9f ? normal.Cross(Vec3::sAxisY()) : Vec3::sAxisX()-normal*normal.GetX();
    face.origin = polygon.front(); face.right = right.NormalizedOr(Vec3::sAxisX()); face.up = normal.Cross(face.right).NormalizedOr(Vec3::sAxisY());
}
BuildingFace mapped_face(std::vector<int> indices,const std::vector<Vec3>& p) {
    BuildingFace face; face.vertices = std::move(indices); face.uv.clear();
    std::vector<Vec3> polygon; for (int i : face.vertices) polygon.push_back(p[i]);
    projection_frame(face,p); const Vec3 right = face.right, up = face.up;
    float lo_u = 1e9f, hi_u = -1e9f, lo_v = lo_u, hi_v = hi_u;
    for (auto point : polygon) {
        const float u = point.Dot(right), v = point.Dot(up);
        lo_u = std::min(lo_u,u); hi_u = std::max(hi_u,u); lo_v = std::min(lo_v,v); hi_v = std::max(hi_v,v);
    }
    for (auto point : polygon) face.uv.push_back({(point.Dot(right)-lo_u)/std::max(.0001f,hi_u-lo_u),(point.Dot(up)-lo_v)/std::max(.0001f,hi_v-lo_v)});
    return face;
}
bool apply(BuildingMesh& mesh,BuildingMesh next,std::string& error) {
    if (!next.fit_bounds(error) || !next.validate(error)) return false;
    mesh = std::move(next); error.clear(); return true;
}
bool apply_part(BuildingMesh& mesh,BuildingMesh next,std::string& error) {
    for (auto p : next.points()) if (!finite(p) || std::abs(p.GetX())>next.size.GetX()/2+.00001f
        || std::abs(p.GetZ())>next.size.GetZ()/2+.00001f || p.GetY()<-.00001f || p.GetY()>BuildingMesh::max_height)
        return fail(error,"Part must fit the editing footprint and height; enlarge the footprint first.");
    return apply(mesh,std::move(next),error);
}
int next_part(const BuildingMesh& mesh) {
    int result = 0; for (const auto& f : mesh.faces) result = std::max(result,f.part); return result+1;
}
BuildingFace inherited_face(const BuildingFace& source,std::vector<int> vertices,std::vector<BuildingUV> uv) {
    BuildingFace result = source; result.vertices = std::move(vertices); result.uv = std::move(uv); return result;
}
Vec3 rotated(Vec3 p,Vec3 degrees) {
    constexpr float radians = 3.14159265358979323846f/180.f;
    const float x = degrees.GetX()*radians, y = degrees.GetY()*radians, z = degrees.GetZ()*radians;
    p = Vec3(p.GetX(),p.GetY()*std::cos(x)-p.GetZ()*std::sin(x),p.GetY()*std::sin(x)+p.GetZ()*std::cos(x));
    p = Vec3(p.GetX()*std::cos(y)+p.GetZ()*std::sin(y),p.GetY(),-p.GetX()*std::sin(y)+p.GetZ()*std::cos(y));
    return Vec3(p.GetX()*std::cos(z)-p.GetY()*std::sin(z),p.GetX()*std::sin(z)+p.GetY()*std::cos(z),p.GetZ());
}
void erase_faces(BuildingMesh& mesh,const std::set<int>& removed) {
    std::vector<int> remap(mesh.faces.size(),-1); std::vector<BuildingFace> faces;
    for (std::size_t i = 0; i<mesh.faces.size(); ++i) if (!removed.count(int(i))) {
        remap[i] = int(faces.size()); faces.push_back(std::move(mesh.faces[i]));
    }
    mesh.faces = std::move(faces);
    mesh.decals.erase(std::remove_if(mesh.decals.begin(),mesh.decals.end(),[&](auto& d) {
        d.face = remap[d.face];
        if (d.face<0 && d.surface>=0) for (int i = 0; i<int(mesh.faces.size()); ++i) if (mesh.faces[i].surface==d.surface) { d.face = i; break; }
        return d.face<0;
    }),mesh.decals.end());
}
std::vector<BuildingUV> material_uv(const BuildingFace& face,const std::vector<Vec3>& p) {
    std::vector<BuildingUV> result; Vec3 right = face.right, up = face.up, origin = face.origin;
    if (face.projection!=BuildingProjection::Manual) {
        std::vector<Vec3> polygon; for (int v : face.vertices) polygon.push_back(p[v]);
        const Vec3 normal = polygon_normal(polygon);
        if (face.projection==BuildingProjection::Box) {
            origin = Vec3::sZero();
            if (std::abs(normal.GetY())>=std::abs(normal.GetX()) && std::abs(normal.GetY())>=std::abs(normal.GetZ())) right = Vec3::sAxisX();
            else if (std::abs(normal.GetX())>std::abs(normal.GetZ())) right = normal.GetX()>=0 ? Vec3::sAxisZ() : -Vec3::sAxisZ();
            else right = normal.GetZ()>=0 ? -Vec3::sAxisX() : Vec3::sAxisX();
        }
        const Vec3 axis = std::abs(normal.GetX())<.9f ? Vec3::sAxisX() : Vec3::sAxisZ();
        const Vec3 fallback = (axis-normal*axis.Dot(normal)).NormalizedOr(Vec3::sAxisX());
        right = (right-normal*normal.Dot(right)).NormalizedOr(fallback); up = normal.Cross(right).NormalizedOr(face.up);
    }
    const float angle = face.rotation*3.14159265358979323846f/180.f, cosine = std::cos(angle), sine = std::sin(angle);
    for (std::size_t i = 0; i<face.vertices.size(); ++i) {
        BuildingUV uv = face.uv[i];
        if (face.projection!=BuildingProjection::Manual) {
            const Vec3 point = p[face.vertices[i]]-origin;
            uv = {point.Dot(right)/face.meters_per_repeat,point.Dot(up)/face.meters_per_repeat};
        }
        const float u = uv[0]*face.scale[0], v = uv[1]*face.scale[1];
        result.push_back({u*cosine-v*sine+face.offset[0],u*sine+v*cosine+face.offset[1]});
    }
    return result;
}
float uv_side(BuildingUV a,BuildingUV b,BuildingUV c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); }
void wall_frame(Vec3 normal,Vec3& right,Vec3& up) {
    right = (std::abs(normal.GetY())<.9f ? normal.Cross(Vec3::sAxisY()) : Vec3::sAxisX()-normal*normal.GetX()).NormalizedOr(Vec3::sAxisX());
    up = normal.Cross(right).NormalizedOr(Vec3::sAxisY());
}
bool sample_face(const BuildingFace& face,const std::vector<Vec3>& p,Vec3 point,BuildingUV& uv,BuildingUV& meters,float max_distance = .05f) {
    std::vector<Vec3> polygon; for (int v : face.vertices) polygon.push_back(p[v]);
    float closest = std::numeric_limits<float>::max(); bool found = false;
    for (const auto& triangle : triangulate_polygon(polygon)) {
        const Vec3 a = polygon[triangle[0]], b = polygon[triangle[1]]-a, c = polygon[triangle[2]]-a, q = point-a;
        const float bb = b.Dot(b), bc = b.Dot(c), cc = c.Dot(c), determinant = bb*cc-bc*bc;
        if (determinant<1e-10f) continue;
        const float u = (q.Dot(b)*cc-q.Dot(c)*bc)/determinant, v = (q.Dot(c)*bb-q.Dot(b)*bc)/determinant;
        if (u<-.00001f || v<-.00001f || u+v>1.00001f) continue;
        const float distance = (point-(a+b*u+c*v)).LengthSq(); if (distance>=closest || distance>max_distance*max_distance) continue;
        const auto& uva = face.uv[triangle[0]]; const auto& uvb = face.uv[triangle[1]]; const auto& uvc = face.uv[triangle[2]];
        const float du1 = uvb[0]-uva[0], dv1 = uvb[1]-uva[1], du2 = uvc[0]-uva[0], dv2 = uvc[1]-uva[1], area = du1*dv2-du2*dv1;
        if (std::abs(area)<1e-8f) continue;
        uv = {uva[0]+du1*u+du2*v,uva[1]+dv1*u+dv2*v};
        meters = {(b*dv2-c*dv1).Length()/std::abs(area),(c*du1-b*du2).Length()/std::abs(area)};
        closest = distance; found = true;
    }
    return found;
}
bool decal_anchor(const BuildingMesh& mesh,const std::vector<Vec3>& p,const BuildingDecal& decal,Vec3& point,Vec3& right,Vec3& up) {
    const BuildingUV center{decal.u+decal.width/2,decal.v+decal.height/2};
    for (int step = -1; step<int(mesh.faces.size()); ++step) {
        const int index = step<0 ? decal.face : step;
        if (index<0 || index>=int(mesh.faces.size()) || (step>=0 && index==decal.face)) continue;
        const auto& face = mesh.faces[index];
        if (decal.surface>=0 ? face.surface!=decal.surface : index!=decal.face) continue;
        std::vector<Vec3> polygon; for (int v : face.vertices) polygon.push_back(p[v]);
        for (const auto& t : triangulate_polygon(polygon)) {
            const auto a = face.uv[t[0]], b = face.uv[t[1]], c = face.uv[t[2]]; const float area = uv_side(a,b,c);
            if (std::abs(area)<1e-8f) continue;
            const float u = uv_side(a,center,c)/area, v = uv_side(a,b,center)/area;
            if (u<-.00001f || v<-.00001f || u+v>1.00001f) continue;
            point = polygon[t[0]]*(1-u-v)+polygon[t[1]]*u+polygon[t[2]]*v;
            wall_frame((polygon[t[1]]-polygon[t[0]]).Cross(polygon[t[2]]-polygon[t[0]]).NormalizedOr(Vec3::sAxisY()),right,up);
            return true;
        }
    }
    return false;
}
bool set_decal_point(BuildingMesh& mesh,BuildingDecal& decal,const std::vector<Vec3>& p,Vec3 point,std::string& error) {
    if (!finite(point)) return fail(error,"Decal position must be finite.");
    for (int step = -1; step<int(mesh.faces.size()); ++step) {
        const int index = step<0 ? decal.face : step;
        if (index<0 || index>=int(mesh.faces.size()) || (step>=0 && index==decal.face)) continue;
        const auto& face = mesh.faces[index];
        if (decal.surface>=0 ? face.surface!=decal.surface : index!=decal.face) continue;
        BuildingUV uv,meters;
        if (!sample_face(face,p,point,uv,meters)) continue;
        decal.face = index; decal.u = uv[0]-decal.width/2; decal.v = uv[1]-decal.height/2; return true;
    }
    return fail(error,"Keep the decal centre on its wall; select another wall to place a new decal.");
}
}
bool valid_texture_filename(const std::string& filename) { return !filename.empty() && texture_name(filename); }
std::vector<std::array<int,3>> triangulate_polygon(const std::vector<Vec3>& p) {
    if (p.size()<3) return {};
    const Vec3 normal = polygon_normal(p);
    if (normal.LengthSq()<.5f) return {};
    const auto side = [&](int a,int b,int c){return (p[b]-p[a]).Cross(p[c]-p[a]).Dot(normal);};
    std::vector<int> remaining(p.size()); std::iota(remaining.begin(),remaining.end(),0);
    std::vector<std::array<int,3>> result;
    // ponytail: ear clipping is quadratic; building faces are capped at 64 corners.
    while (remaining.size()>3) {
        bool clipped = false;
        for (std::size_t i = 0; i<remaining.size(); ++i) {
            const int a = remaining[(i+remaining.size()-1)%remaining.size()], b = remaining[i], c = remaining[(i+1)%remaining.size()];
            if (side(a,b,c)<=1e-7f) continue;
            bool occupied = false;
            for (int point : remaining) if (point!=a && point!=b && point!=c)
                if (side(a,b,point)>=-1e-7f && side(b,c,point)>=-1e-7f && side(c,a,point)>=-1e-7f) { occupied = true; break; }
            if (occupied) continue;
            result.push_back({a,b,c}); remaining.erase(remaining.begin()+i); clipped = true; break;
        }
        if (!clipped) return {};
    }
    if (side(remaining[0],remaining[1],remaining[2])<=1e-7f) return {};
    result.push_back({remaining[0],remaining[1],remaining[2]}); return result;
}
std::vector<Vec3> BuildingMesh::points(Vec3 base) const {
    auto result = vertices;
    for (auto& p : result) p = base+Vec3(p.GetX()*size.GetX(),p.GetY()*size.GetY(),p.GetZ()*size.GetZ());
    return result;
}
std::vector<std::array<int,2>> BuildingMesh::edges() const {
    std::set<std::array<int,2>> result;
    for (const auto& face : faces) for (std::size_t i = 0; i<face.vertices.size(); ++i) {
        const int a = face.vertices[i], b = face.vertices[(i+1)%face.vertices.size()];
        result.insert({std::min(a,b),std::max(a,b)});
    }
    return {result.begin(),result.end()};
}
void BuildingMesh::preserve_surfaces() {
    int surface = 0;
    for (const auto& face : faces) surface = std::max(surface,face.surface+1);
    for (auto& face : faces) if (face.surface<0) face.surface = surface++;
    for (auto& decal : decals) if (decal.surface<0 && decal.face>=0 && decal.face<int(faces.size())) decal.surface = faces[decal.face].surface;
}
int BuildingMesh::add_material(const std::string& filename,std::string& error) {
    error.clear();
    if (!valid_texture_filename(filename)) { fail(error,"Choose a valid texture filename."); return -1; }
    const auto found = std::find(materials.begin(),materials.end(),filename);
    if (found!=materials.end()) return int(found-materials.begin());
    if (materials.size()>=64) { fail(error,"Use at most 64 material slots."); return -1; }
    materials.push_back(filename); return int(materials.size())-1;
}
bool BuildingMesh::set_face_material(const std::vector<int>& selection,int material,std::string& error) {
    if (selection.empty() || material<-1 || material>=int(materials.size())) return fail(error,"Select faces and an existing material slot.");
    BuildingMesh next = *this;
    for (int face : selection) {
        if (face<0 || face>=int(faces.size())) return fail(error,"Invalid selected face.");
        next.faces[face].material = material;
    }
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
std::vector<BuildingUV> BuildingMesh::texture_uv(int index) const {
    if (index<0 || index>=int(faces.size())) return {};
    return material_uv(faces[index],points());
}
bool BuildingMesh::set_uv(const std::vector<int>& selection,BuildingProjection projection,float meters_per_repeat,
    BuildingUV offset,BuildingUV scale,float rotation,std::string& error) {
    if (selection.empty()) return fail(error,"Select faces to project.");
    BuildingMesh next = *this; next.preserve_surfaces(); const auto p = points();
    for (int index : selection) {
        if (index<0 || index>=int(faces.size())) return fail(error,"Invalid selected face.");
        auto& face = next.faces[index];
        face.projection = projection; face.meters_per_repeat = meters_per_repeat; face.offset = offset; face.scale = scale; face.rotation = rotation;
        if (projection==BuildingProjection::Planar) projection_frame(face,p);
    }
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
int BuildingMesh::place_decal(int index,Vec3 point,const std::string& filename,float width_m,float height_m,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !finite(point) || !valid_texture_filename(filename)
        || !std::isfinite(width_m) || !std::isfinite(height_m) || width_m<.05f || height_m<.05f || decals.size()>=64) {
        fail(error,"Select a wall and texture, use dimensions of at least 0.05 m and at most 64 decals."); return -1;
    }
    BuildingMesh next = *this; next.preserve_surfaces(); BuildingUV uv,meters;
    if (!sample_face(next.faces[index],points(),point,uv,meters)) { fail(error,"Click inside a wall with usable surface coordinates."); return -1; }
    BuildingDecal decal{index,filename,uv[0]-width_m/meters[0]/2,uv[1]-height_m/meters[1]/2,width_m/meters[0],height_m/meters[1]};
    decal.surface = next.faces[index].surface; decal.projected = true; decal.meters = meters; next.decals.push_back(std::move(decal));
    if (!next.validate(error)) return -1;
    const int added = int(decals.size()); *this = std::move(next); return added;
}
bool BuildingMesh::decal_center(int index,Vec3& point) const {
    Vec3 right,up;
    return index>=0 && index<int(decals.size()) && decal_anchor(*this,points(),decals[index],point,right,up);
}
bool BuildingMesh::move_decal(int index,Vec3 point,std::string& error) {
    if (index<0 || index>=int(decals.size())) return fail(error,"Select a decal to move.");
    BuildingMesh next = *this; next.preserve_surfaces();
    if (!set_decal_point(next,next.decals[index],points(),point,error) || !next.validate(error)) return false;
    *this = std::move(next); return true;
}
bool BuildingMesh::move_decal(int index,int face,Vec3 point,std::string& error) {
    if (index<0 || index>=int(decals.size()) || face<0 || face>=int(faces.size()) || !finite(point))
        return fail(error,"Select a decal and a wall to move it onto.");
    BuildingMesh next = *this; next.preserve_surfaces(); auto& decal = next.decals[index]; const auto p = points();
    if (next.faces[face].surface==decal.surface) return move_decal(index,point,error);
    BuildingUV source_uv,source_meters = decal.meters,uv,meters; Vec3 anchor,right,up;
    if (!decal.projected) {
        if (!decal_anchor(next,p,decal,anchor,right,up)) return fail(error,"The decal needs a valid source wall anchor.");
        bool found = false;
        for (const auto& source : next.faces) if (source.surface==decal.surface && sample_face(source,p,anchor,source_uv,source_meters)) { found = true; break; }
        if (!found) return fail(error,"The decal needs a valid source wall anchor.");
    }
    if (!sample_face(next.faces[face],p,point,uv,meters)) return fail(error,"Click inside the destination wall.");
    const float width_m = decal.width*source_meters[0], height_m = decal.height*source_meters[1];
    decal.face = face; decal.surface = next.faces[face].surface; decal.projected = true; decal.meters = meters;
    decal.width = width_m/meters[0]; decal.height = height_m/meters[1]; decal.u = uv[0]-decal.width/2; decal.v = uv[1]-decal.height/2;
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
int BuildingMesh::duplicate_decal(int index,BuildingUV offset,std::string& error) {
    if (index<0 || index>=int(decals.size()) || !std::isfinite(offset[0]) || !std::isfinite(offset[1])) {
        fail(error,"Select a decal and a finite duplicate offset."); return -1;
    }
    BuildingMesh next = *this; next.preserve_surfaces(); auto decal = next.decals[index]; decal.u += offset[0]; decal.v += offset[1];
    Vec3 point,right,up;
    if (!decal_anchor(next,points(),decal,point,right,up)) { fail(error,"Duplicate centre must stay on the wall."); return -1; }
    next.decals.push_back(std::move(decal)); if (!next.validate(error)) return -1;
    const int added = int(decals.size()); *this = std::move(next); return added;
}
bool BuildingMesh::repeat_decal(int index,int count,float spacing_m,bool vertical,std::string& error) {
    if (index<0 || index>=int(decals.size()) || count<1 || count>64 || decals.size()+std::size_t(count-1)>64
        || !std::isfinite(spacing_m) || spacing_m<.01f) return fail(error,"Choose 1-64 total decals and positive spacing in metres.");
    BuildingMesh next = *this; next.preserve_surfaces(); const auto source = next.decals[index]; const auto p = points(); Vec3 point,right,up;
    if (!decal_anchor(next,p,source,point,right,up)) return fail(error,"The source decal needs a valid wall anchor.");
    for (int i = 1; i<count; ++i) {
        auto decal = source;
        if (!set_decal_point(next,decal,p,point+(vertical ? up : right)*(spacing_m*i),error)) return false;
        next.decals.push_back(std::move(decal));
    }
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
std::vector<BuildingTriangle> BuildingMesh::triangles(Vec3 base) const {
    const auto p = points(); std::vector<BuildingTriangle> result; std::vector<std::array<BuildingUV,3>> surface_uv;
    for (std::size_t f = 0; f<faces.size(); ++f) {
        const auto& face = faces[f]; std::vector<Vec3> polygon;
        for (int vertex : face.vertices) polygon.push_back(p[vertex]);
        const auto uv = material_uv(face,p);
        for (const auto& t : triangulate_polygon(polygon)) {
            result.push_back({{polygon[t[0]]+base,polygon[t[1]]+base,polygon[t[2]]+base},
                {uv[t[0]],uv[t[1]],uv[t[2]]},face.material<0 ? texture : materials[face.material],false,int(f),
                face.projection!=BuildingProjection::Manual || face.offset!=BuildingUV{0,0} || face.scale!=BuildingUV{1,1} || face.rotation!=0});
            surface_uv.push_back({face.uv[t[0]],face.uv[t[1]],face.uv[t[2]]});
        }
    }
    const auto body_count = result.size();
    // Material UVs never participate in decal placement or clipping.
    for (int decal_index = 0; decal_index<int(decals.size()); ++decal_index) {
        const auto& decal = decals[decal_index];
        const float angle = decal.rotation*3.14159265358979323846f/180.f, cosine = std::cos(angle), sine = std::sin(angle);
        Vec3 anchor = Vec3::sZero(), right = Vec3::sAxisX(), up = Vec3::sAxisY();
        if (decal.projected) {
            if (!decal_anchor(*this,p,decal,anchor,right,up)) continue;
            const Vec3 before = right; right = right*cosine+up*sine; up = up*cosine-before*sine; anchor += base;
        }
        for (std::size_t i = 0; i<body_count; ++i) {
            const auto triangle = result[i];
            if (decal.surface>=0 ? faces[triangle.face].surface!=decal.surface : triangle.face!=decal.face) continue;
            auto chart = surface_uv[i];
            std::vector<BuildingUV> polygon{{decal.u,decal.v},{decal.u+decal.width,decal.v},
                {decal.u+decal.width,decal.v+decal.height},{decal.u,decal.v+decal.height}};
            if (decal.projected) {
                polygon = {{0,0},{1,0},{1,1},{0,1}};
                for (int corner = 0; corner<3; ++corner) {
                    const Vec3 delta = triangle.points[corner]-anchor;
                    chart[corner] = {delta.Dot(right)/(decal.width*decal.meters[0])+.5f,delta.Dot(up)/(decal.height*decal.meters[1])+.5f};
                }
            } else if (decal.rotation!=0) for (auto& q : polygon) {
                const float u = q[0]-decal.u-decal.width/2, v = q[1]-decal.v-decal.height/2;
                q = {decal.u+decal.width/2+u*cosine-v*sine,decal.v+decal.height/2+u*sine+v*cosine};
            }
            const float area = uv_side(chart[0],chart[1],chart[2]); if (std::abs(area)<1e-8f) continue;
            if (area<0) std::reverse(polygon.begin(),polygon.end());
            for (int edge = 0; edge<3 && !polygon.empty(); ++edge) {
                std::vector<BuildingUV> clipped; auto before = polygon.back();
                float previous = uv_side(chart[edge],chart[(edge+1)%3],before)/area;
                for (auto current : polygon) {
                    const float distance = uv_side(chart[edge],chart[(edge+1)%3],current)/area;
                    if ((previous>=0)!=(distance>=0)) {
                        const float t = previous/(previous-distance);
                        clipped.push_back({before[0]+(current[0]-before[0])*t,before[1]+(current[1]-before[1])*t});
                    }
                    if (distance>=0) clipped.push_back(current);
                    before = current; previous = distance;
                }
                polygon = std::move(clipped);
            }
            const Vec3 normal = (triangle.points[1]-triangle.points[0]).Cross(triangle.points[2]-triangle.points[0]).NormalizedOr(Vec3::sAxisY());
            const auto point = [&](BuildingUV q) {
                const float b = uv_side(chart[0],q,chart[2])/area, c = uv_side(chart[0],chart[1],q)/area;
                return triangle.points[0]*(1-b-c)+triangle.points[1]*b+triangle.points[2]*c+normal*.015f;
            };
            for (std::size_t k = 1; k+1<polygon.size(); ++k) {
                const std::array<BuildingUV,3> q{{polygon[0],polygon[k],polygon[k+1]}};
                BuildingTriangle t{{point(q[0]),point(q[1]),point(q[2])},{},decal.texture,true,triangle.face,false,decal_index};
                if ((t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).LengthSq()<1e-10f) continue;
                for (int corner = 0; corner<3; ++corner) {
                    if (decal.projected) t.uv[corner] = q[corner];
                    else {
                        const float u = q[corner][0]-decal.u-decal.width/2, v = q[corner][1]-decal.v-decal.height/2;
                        t.uv[corner] = {(u*cosine+v*sine)/decal.width+.5f,(-u*sine+v*cosine)/decal.height+.5f};
                    }
                }
                result.push_back(std::move(t));
            }
        }
    }
    return result;
}
bool BuildingMesh::validate(std::string& error) const {
    error.clear();
    if (name.empty() || name.size()>48 || name.find_first_not_of(' ')==std::string::npos
        || name.back()==' ' || name.back()=='.' || name.find_first_of("<>:\"/\\|?*")!=std::string::npos
        || !std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32 && c<127;}))
        return fail(error,"Use a building name of 1-48 characters without filename symbols.");
    if (!finite(size) || size.GetX()<.5f || size.GetZ()<.5f || size.GetY()<.5f
        || size.GetX()>max_tiles*tile_size || size.GetZ()>max_tiles*tile_size || size.GetY()>max_height)
        return fail(error,"Footprint must fit 128 x 128 tiles; height must be 0.5-120 m.");
    if (!texture_name(texture) || decals.size()>64) return fail(error,"Invalid texture filename or more than 64 decals.");
    if (materials.size()>64) return fail(error,"Use at most 64 material slots.");
    for (const auto& material : materials) if (!valid_texture_filename(material)) return fail(error,"Invalid building material filename.");
    if (vertices.size()<3 || vertices.size()>max_vertices || faces.empty() || faces.size()>max_faces)
        return fail(error,"Use 3-4096 vertices and 1-8192 faces.");
    for (auto p : vertices) if (!finite(p) || std::abs(p.GetX())>.50001f || std::abs(p.GetZ())>.50001f || p.GetY()<-.00001f || p.GetY()>1.00001f)
        return fail(error,"Vertex positions exceed the building dimensions.");
    const auto p = points(); std::map<std::pair<int,int>,std::pair<int,int>> edges; std::size_t corners = 0;
    for (const auto& face : faces) {
        if (face.part<0 || face.part>1000000 || face.material<-1 || face.material>=int(materials.size()) || face.surface<-1 || face.surface>1000000
            || int(face.projection)<0 || int(face.projection)>2 || !std::isfinite(face.meters_per_repeat) || face.meters_per_repeat<.01f || face.meters_per_repeat>1000000
            || !std::isfinite(face.rotation) || std::abs(face.rotation)>1000000 || !finite(face.origin) || !finite(face.right) || !finite(face.up)
            || face.right.LengthSq()<1e-6f || face.up.LengthSq()<1e-6f)
            return fail(error,"Invalid part, material or projection settings.");
        for (int axis = 0; axis<2; ++axis) if (!std::isfinite(face.offset[axis]) || std::abs(face.offset[axis])>1000000
            || !std::isfinite(face.scale[axis]) || std::abs(face.scale[axis])<.0001f || std::abs(face.scale[axis])>10000)
            return fail(error,"Use finite UV transforms with nonzero scale.");
        if (face.vertices.size()<3 || face.vertices.size()>64 || face.uv.size()!=face.vertices.size())
            return fail(error,"Faces need 3-64 corners with matching UV coordinates.");
        corners += face.vertices.size(); if (corners>65536) return fail(error,"Mesh corner limit reached.");
        std::set<int> unique; std::vector<Vec3> polygon;
        for (std::size_t i = 0; i<face.vertices.size(); ++i) {
            const int v = face.vertices[i];
            if (v<0 || v>=int(vertices.size()) || !unique.insert(v).second) return fail(error,"Invalid or duplicate face vertex.");
            if (!std::isfinite(face.uv[i][0]) || !std::isfinite(face.uv[i][1]) || std::abs(face.uv[i][0])>8 || std::abs(face.uv[i][1])>8)
                return fail(error,"Invalid face texture coordinates.");
            polygon.push_back(p[v]);
        }
        if (triangulate_polygon(polygon).size()!=polygon.size()-2) return fail(error,"Face corners cross or collapse; move them back or delete the face.");
        for (std::size_t i = 0; i<face.vertices.size(); ++i) {
            const int a = face.vertices[i], b = face.vertices[(i+1)%face.vertices.size()];
            auto& edge = edges[{std::min(a,b),std::max(a,b)}]; ++edge.first; edge.second += a<b ? 1 : -1;
            if (edge.first>2) return fail(error,"An edge cannot belong to more than two faces.");
        }
    }
    for (const auto& edge : edges) if (edge.second.first==2 && edge.second.second!=0)
        return fail(error,"Connected faces must wind in opposite directions along their shared edge.");
    for (const auto& d : decals) if (d.face<0 || d.face>=int(faces.size()) || d.texture.empty() || !texture_name(d.texture)
        || !std::isfinite(d.u) || !std::isfinite(d.v) || !std::isfinite(d.width) || !std::isfinite(d.height)
        || !std::isfinite(d.rotation) || std::abs(d.rotation)>1000000 || d.surface<-1 || d.surface>1000000
        || !std::isfinite(d.meters[0]) || !std::isfinite(d.meters[1]) || d.meters[0]<.00001f || d.meters[1]<.00001f
        || d.meters[0]>100000000 || d.meters[1]>100000000 || (d.projected && d.surface<0)
        || (d.surface>=0 && d.surface!=faces[d.face].surface)
        || d.width<.00001f || d.height<.00001f || d.width>1000 || d.height>1000 || std::abs(d.u)>1000 || std::abs(d.v)>1000
        || (d.surface<0 && (d.u<0 || d.v<0 || d.width<.01f || d.height<.01f || d.u+d.width>1.00001f || d.v+d.height>1.00001f)))
        return fail(error,"Decals need a texture and must stay inside a face.");
    return true;
}
std::array<int,2> BuildingMesh::footprint() const {
    return {int(std::ceil(size.GetX()/tile_size-1e-5f)),int(std::ceil(size.GetZ()/tile_size-1e-5f))};
}
bool BuildingMesh::set_footprint(int width,int depth,std::string& error,bool preserve_geometry) {
    if (width<1 || width>max_tiles || depth<1 || depth>max_tiles) return fail(error,"Choose 1-128 tiles on each axis.");
    BuildingMesh next = *this;
    next.size = Vec3(width*tile_size,size.GetY(),depth*tile_size);
    if (preserve_geometry) {
        float x = 1e-6f, z = 1e-6f;
        for (auto p : vertices) { x = std::max(x,std::abs(p.GetX())); z = std::max(z,std::abs(p.GetZ())); }
        // Grow the editing area without growing the mesh; scale only when a smaller area needs it.
        const float sx = std::min(size.GetX()/next.size.GetX(),.5f/x), sz = std::min(size.GetZ()/next.size.GetZ(),.5f/z);
        for (auto& p : next.vertices) p = Vec3(p.GetX()*sx,p.GetY(),p.GetZ()*sz);
    }
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
bool BuildingMesh::fit_bounds(std::string& error) {
    auto p = points(); if (p.empty()) return fail(error,"Mesh needs vertices.");
    float height = .5f;
    for (auto& v : p) {
        if (!finite(v)) return fail(error,"Vertex coordinates must be finite.");
        v = Vec3(std::clamp(v.GetX(),-size.GetX()/2,size.GetX()/2),std::clamp(v.GetY(),0.f,max_height),
            std::clamp(v.GetZ(),-size.GetZ()/2,size.GetZ()/2));
        height = std::max(height,v.GetY());
    }
    for (std::size_t i = 0; i<p.size(); ++i) vertices[i] = Vec3(p[i].GetX()/size.GetX(),p[i].GetY()/height,p[i].GetZ()/size.GetZ());
    size.SetY(height); return true;
}
bool BuildingMesh::move_vertices(const std::vector<int>& selection,Vec3 delta,std::string& error) {
    if (selection.empty() || !finite(delta)) return fail(error,"Select vertices to move.");
    BuildingMesh next = *this; std::set<int> unique(selection.begin(),selection.end());
    const auto p = points();
    Vec3 low(-size.GetX()/2,0,-size.GetZ()/2), high(size.GetX()/2,max_height,size.GetZ()/2);
    Vec3 minimum(-1e9f,-1e9f,-1e9f), maximum(1e9f,1e9f,1e9f);
    for (int i : unique) {
        if (i<0 || i>=int(vertices.size())) return fail(error,"Invalid selected vertex.");
        minimum = Vec3::sMax(minimum,low-p[i]); maximum = Vec3::sMin(maximum,high-p[i]);
    }
    delta = Vec3::sMax(minimum,Vec3::sMin(delta,maximum));
    for (int i : unique) {
        if (i<0 || i>=int(vertices.size())) return fail(error,"Invalid selected vertex.");
        next.vertices[i] += Vec3(delta.GetX()/size.GetX(),delta.GetY()/size.GetY(),delta.GetZ()/size.GetZ());
    }
    return apply(*this,std::move(next),error);
}
int BuildingMesh::add_vertex(Vec3 position,std::string& error) {
    if (!finite(position) || vertices.size()>=max_vertices) { fail(error,"Invalid vertex position or vertex limit reached."); return -1; }
    BuildingMesh next = *this; const int index = int(vertices.size());
    next.vertices.push_back(Vec3(position.GetX()/size.GetX(),position.GetY()/size.GetY(),position.GetZ()/size.GetZ()));
    return apply(*this,std::move(next),error) ? index : -1;
}
int BuildingMesh::split_edge(int a,int b,std::string& error) {
    const auto all = edges();
    if (a<0 || b<0 || a>=int(vertices.size()) || b>=int(vertices.size()) || a==b
        || std::find(all.begin(),all.end(),std::array<int,2>{std::min(a,b),std::max(a,b)})==all.end()) { fail(error,"Select an existing edge to subdivide."); return -1; }
    BuildingMesh next = *this; const int added = int(vertices.size()); next.vertices.push_back((vertices[a]+vertices[b])/2);
    for (auto& face : next.faces) for (std::size_t i = 0; i<face.vertices.size(); ++i) {
        const std::size_t j = (i+1)%face.vertices.size();
        if ((face.vertices[i]==a && face.vertices[j]==b) || (face.vertices[i]==b && face.vertices[j]==a)) {
            const BuildingUV uv{(face.uv[i][0]+face.uv[j][0])/2,(face.uv[i][1]+face.uv[j][1])/2};
            face.vertices.insert(face.vertices.begin()+i+1,added); face.uv.insert(face.uv.begin()+i+1,uv); break;
        }
    }
    return apply(*this,std::move(next),error) ? added : -1;
}
bool BuildingMesh::subdivide_face(int index,std::string& error) {
    if (index<0 || index>=int(faces.size())) return fail(error,"Select a face to subdivide.");
    BuildingMesh next = *this; next.preserve_surfaces(); const auto original = next.faces[index]; const auto p = points(); std::vector<Vec3> polygon;
    for (int v : original.vertices) polygon.push_back(p[v]);
    std::vector<BuildingFace> pieces;
    for (const auto& t : triangulate_polygon(polygon)) {
        const int center = int(next.vertices.size());
        next.vertices.push_back((vertices[original.vertices[t[0]]]+vertices[original.vertices[t[1]]]+vertices[original.vertices[t[2]]])/3);
        const BuildingUV uv{(original.uv[t[0]][0]+original.uv[t[1]][0]+original.uv[t[2]][0])/3,
            (original.uv[t[0]][1]+original.uv[t[1]][1]+original.uv[t[2]][1])/3};
        for (int i = 0; i<3; ++i) {
            const int a = t[i], b = t[(i+1)%3];
            pieces.push_back(inherited_face(original,{original.vertices[a],original.vertices[b],center},{original.uv[a],original.uv[b],uv}));
        }
    }
    if (pieces.empty()) return fail(error,"Face cannot be subdivided.");
    next.faces[index] = pieces.front();
    for (std::size_t i = 1; i<pieces.size(); ++i) next.faces.push_back(pieces[i]);
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::extrude_face(int index,float distance,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !std::isfinite(distance) || std::abs(distance)<.01f)
        return fail(error,"Select a face and extrude by at least 0.01 m.");
    BuildingMesh next = *this; next.preserve_surfaces(); const auto original = next.faces[index]; const auto p = points(); std::vector<Vec3> polygon;
    for (int i : original.vertices) polygon.push_back(p[i]);
    const Vec3 delta = polygon_normal(polygon)*distance;
    auto& cap = next.faces[index];
    for (std::size_t i = 0; i<original.vertices.size(); ++i) {
        cap.vertices[i] = int(next.vertices.size());
        next.vertices.push_back(vertices[original.vertices[i]]+Vec3(delta.GetX()/size.GetX(),delta.GetY()/size.GetY(),delta.GetZ()/size.GetZ()));
    }
    const auto cap_indices = cap.vertices;
    const auto expanded = next.points();
    for (std::size_t i = 0; i<original.vertices.size(); ++i) {
        const auto j = (i+1)%original.vertices.size();
        auto side = mapped_face({original.vertices[i],original.vertices[j],cap_indices[j],cap_indices[i]},expanded);
        side.part = original.part; side.material = original.material;
        side.projection = original.projection; side.meters_per_repeat = original.meters_per_repeat;
        side.offset = original.offset; side.scale = original.scale; side.rotation = original.rotation;
        next.faces.push_back(std::move(side));
    }
    next.preserve_surfaces();
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::inset_face(int index,float fraction,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !std::isfinite(fraction) || fraction<.01f || fraction>.95f)
        return fail(error,"Select a face and inset by a fraction between 0.01 and 0.95.");
    BuildingMesh next = *this; next.preserve_surfaces(); const auto original = next.faces[index]; const auto p = points();
    Vec3 center = Vec3::sZero(); BuildingUV uv{};
    std::vector<Vec3> polygon; for (int i : original.vertices) { polygon.push_back(p[i]); center += vertices[i]/float(original.vertices.size()); }
    const Vec3 normal = polygon_normal(polygon);
    for (std::size_t i = 0; i<polygon.size(); ++i) for (auto point : polygon)
        if ((polygon[(i+1)%polygon.size()]-polygon[i]).Cross(point-polygon[i]).Dot(normal)<-1e-5f)
            return fail(error,"Inset a convex face; subdivide a concave face first.");
    for (auto q : original.uv) { uv[0] += q[0]/original.uv.size(); uv[1] += q[1]/original.uv.size(); }
    auto& cap = next.faces[index];
    for (std::size_t i = 0; i<original.vertices.size(); ++i) {
        cap.vertices[i] = int(next.vertices.size()); next.vertices.push_back(center+(vertices[original.vertices[i]]-center)*(1-fraction));
        cap.uv[i] = {uv[0]+(original.uv[i][0]-uv[0])*(1-fraction),uv[1]+(original.uv[i][1]-uv[1])*(1-fraction)};
    }
    const auto inner = cap;
    for (std::size_t i = 0; i<original.vertices.size(); ++i) {
        const auto j = (i+1)%original.vertices.size();
        next.faces.push_back(inherited_face(original,{original.vertices[i],original.vertices[j],inner.vertices[j],inner.vertices[i]},
            {original.uv[i],original.uv[j],inner.uv[j],inner.uv[i]}));
    }
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::slice(int axis,float position,std::string& error) {
    if (axis<0 || axis>2 || !std::isfinite(position)) return fail(error,"Choose an X, Y or Z slice position.");
    const auto component = [axis](Vec3 p) { return axis==0 ? p.GetX() : axis==1 ? p.GetY() : p.GetZ(); };
    const auto p = points(); BuildingMesh next = *this; next.preserve_surfaces();
    std::map<std::pair<int,int>,int> intersections;
    bool cut = false;
    for (std::size_t f = 0; f<faces.size(); ++f) {
        const auto original = next.faces[f]; bool positive = false, negative = false; std::vector<Vec3> polygon;
        for (int v : original.vertices) {
            const float d = component(p[v])-position; positive |= d>1e-5f; negative |= d<-1e-5f; polygon.push_back(p[v]);
        }
        if (!positive || !negative) continue;
        std::vector<BuildingFace> source{original}, pieces;
        const Vec3 normal = polygon_normal(polygon); bool convex = true;
        for (std::size_t i = 0; i<polygon.size(); ++i) for (auto q : polygon)
            convex &= (polygon[(i+1)%polygon.size()]-polygon[i]).Cross(q-polygon[i]).Dot(normal)>=-1e-5f;
        // Concave faces may clip into disconnected islands; their UV triangles avoid bridging the gap.
        if (!convex) {
            source.clear();
            for (auto t : triangulate_polygon(polygon)) source.push_back(inherited_face(original,{original.vertices[t[0]],original.vertices[t[1]],original.vertices[t[2]]},
                {original.uv[t[0]],original.uv[t[1]],original.uv[t[2]]}));
        }
        for (const auto& face : source) for (float sign : {-1.f,1.f}) {
            BuildingFace piece = face; piece.vertices.clear(); piece.uv.clear();
            const auto corner = [&](int v,BuildingUV uv) {
                if (piece.vertices.empty() || piece.vertices.back()!=v) { piece.vertices.push_back(v); piece.uv.push_back(uv); }
            };
            for (std::size_t i = 0; i<face.vertices.size(); ++i) {
                const auto j = (i+1)%face.vertices.size(); const int a = face.vertices[i], b = face.vertices[j];
                const float da = component(p[a])-position, db = component(p[b])-position;
                if (da*sign>=-1e-5f) corner(a,face.uv[i]);
                if ((da>1e-5f && db<-1e-5f) || (da<-1e-5f && db>1e-5f)) {
                    const float t = da/(da-db); const auto key = std::minmax(a,b);
                    auto found = intersections.find(key);
                    if (found==intersections.end()) {
                        const int v = int(next.vertices.size()); next.vertices.push_back(vertices[a]+(vertices[b]-vertices[a])*t);
                        found = intersections.emplace(key,v).first;
                    }
                    corner(found->second,{face.uv[i][0]+(face.uv[j][0]-face.uv[i][0])*t,face.uv[i][1]+(face.uv[j][1]-face.uv[i][1])*t});
                }
            }
            if (piece.vertices.size()>1 && piece.vertices.front()==piece.vertices.back()) { piece.vertices.pop_back(); piece.uv.pop_back(); }
            if (piece.vertices.size()>=3) pieces.push_back(std::move(piece));
        }
        if (pieces.size()<2) return fail(error,"Slice would collapse a face; move the cut away from its corners.");
        cut = true; next.faces[f] = pieces.front();
        for (std::size_t i = 1; i<pieces.size(); ++i) {
            next.faces.push_back(std::move(pieces[i]));
        }
    }
    if (!cut) return fail(error,"The slice must pass through the mesh, away from its outer edges.");
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::add_face(std::vector<int> selection,std::string& error) {
    if (selection.size()<3 || selection.size()>64) return fail(error,"Select 3-64 vertices in perimeter order.");
    for (int i : selection) if (i<0 || i>=int(vertices.size())) return fail(error,"Invalid selected vertex.");
    bool reverse = false;
    for (const auto& face : faces) for (std::size_t i = 0; i<face.vertices.size(); ++i) {
        const int a = face.vertices[i], b = face.vertices[(i+1)%face.vertices.size()];
        for (std::size_t j = 0; j<selection.size(); ++j) reverse |= selection[j]==a && selection[(j+1)%selection.size()]==b;
    }
    if (reverse) std::reverse(selection.begin(),selection.end());
    BuildingMesh next = *this; next.faces.push_back(mapped_face(std::move(selection),points())); next.preserve_surfaces();
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::remove_faces(const std::vector<int>& selection,std::string& error) {
    if (selection.empty()) return fail(error,"Select faces to delete.");
    for (int i : selection) if (i<0 || i>=int(faces.size())) return fail(error,"Invalid selected face.");
    BuildingMesh next = *this; erase_faces(next,{selection.begin(),selection.end()});
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::remove_vertices(const std::vector<int>& selection,std::string& error) {
    if (selection.empty()) return fail(error,"Select vertices to delete.");
    const std::set<int> removed(selection.begin(),selection.end());
    for (int i : removed) if (i<0 || i>=int(vertices.size())) return fail(error,"Invalid selected vertex.");
    BuildingMesh next = *this; std::set<int> faces_removed;
    for (std::size_t i = 0; i<faces.size(); ++i) for (int v : faces[i].vertices) if (removed.count(v)) faces_removed.insert(int(i));
    erase_faces(next,faces_removed);
    std::vector<int> remap(vertices.size(),-1); next.vertices.clear();
    for (std::size_t i = 0; i<vertices.size(); ++i) if (!removed.count(int(i))) { remap[i] = int(next.vertices.size()); next.vertices.push_back(vertices[i]); }
    for (auto& face : next.faces) for (auto& v : face.vertices) v = remap[v];
    return apply(*this,std::move(next),error);
}
std::vector<int> BuildingMesh::part_vertices(int part) const {
    std::set<int> result;
    for (const auto& face : faces) if (face.part==part) result.insert(face.vertices.begin(),face.vertices.end());
    return {result.begin(),result.end()};
}
int BuildingMesh::add_shape(BuildingShape shape,Vec3 dimensions,Vec3 position,std::string& error) {
    if (!finite(dimensions) || !finite(position) || dimensions.GetX()<.05f || dimensions.GetY()<.05f || dimensions.GetZ()<.05f
        || int(shape)<0 || int(shape)>3) { fail(error,"Choose a shape with finite dimensions of at least 0.05 m."); return -1; }
    const float x = dimensions.GetX()/2, y = dimensions.GetY(), z = dimensions.GetZ()/2;
    std::vector<Vec3> p; std::vector<std::vector<int>> polygons;
    if (shape==BuildingShape::Box) {
        p = {Vec3(-x,0,-z),Vec3(x,0,-z),Vec3(x,y,-z),Vec3(-x,y,-z),Vec3(-x,0,z),Vec3(x,0,z),Vec3(x,y,z),Vec3(-x,y,z)};
        polygons = {{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
    } else {
        p = {Vec3(-x,0,-z),Vec3(x,0,-z),Vec3(-x,0,z),Vec3(x,0,z)};
        if (shape==BuildingShape::Wedge) {
            p.push_back(Vec3(-x,y,z)); p.push_back(Vec3(x,y,z));
            polygons = {{0,1,3,2},{2,3,5,4},{0,2,4},{1,5,3},{0,4,5,1}};
        } else {
            const float inset = shape==BuildingShape::Hip ? .5f : 1.f;
            p.push_back(Vec3(0,y,-z*inset)); p.push_back(Vec3(0,y,z*inset));
            polygons = {{0,1,3,2},{0,4,1},{2,3,5},{0,2,5,4},{1,4,5,3}};
        }
    }
    BuildingMesh next = *this; next.preserve_surfaces(); const int part = next_part(next), first = int(next.vertices.size());
    for (auto& point : p) {
        point += position;
        next.vertices.push_back(Vec3(point.GetX()/size.GetX(),point.GetY()/size.GetY(),point.GetZ()/size.GetZ()));
    }
    const auto all = next.points();
    for (auto polygon : polygons) {
        for (auto& vertex : polygon) vertex += first;
        auto face = mapped_face(std::move(polygon),all); face.part = part; face.projection = BuildingProjection::Box; next.faces.push_back(std::move(face));
    }
    next.preserve_surfaces();
    return apply_part(*this,std::move(next),error) ? part : -1;
}
bool BuildingMesh::attach_shape(int index,BuildingShape shape,float height,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !std::isfinite(height) || height<.05f || int(shape)<0 || int(shape)>3)
        return fail(error,"Select a face and a positive attachment height.");
    BuildingMesh next = *this; next.preserve_surfaces(); const int part = next_part(next); const auto original = next.faces[index];
    if (shape==BuildingShape::Box) {
        const auto p = points(); std::vector<Vec3> polygon;
        for (int vertex : original.vertices) polygon.push_back(p[vertex]);
        const Vec3 delta = polygon_normal(polygon)*height;
        for (auto point : polygon) {
            point += delta;
            if (std::abs(point.GetX())>size.GetX()/2 || std::abs(point.GetZ())>size.GetZ()/2 || point.GetY()<0 || point.GetY()>max_height)
                return fail(error,"Attachment must fit the footprint and height; enlarge the footprint first.");
        }
        const auto count = next.faces.size();
        if (!next.extrude_face(index,height,error)) return false;
        next.faces[index].part = part;
        for (std::size_t i = count; i<next.faces.size(); ++i) next.faces[i].part = part;
    } else {
        const auto p = points(); std::vector<Vec3> polygon;
        for (int vertex : original.vertices) polygon.push_back(p[vertex]);
        if (polygon.size()!=4 || polygon_normal(polygon).GetY()<.999f)
            return fail(error,"Attach a roof to a flat, upward-facing rectangular quad.");
        for (int i = 0; i<4; ++i) if (std::abs(polygon[i].GetY()-polygon[0].GetY())>.0001f
            || std::abs((polygon[(i+1)%4]-polygon[i]).Normalized().Dot((polygon[(i+2)%4]-polygon[(i+1)%4]).Normalized()))>.0001f)
            return fail(error,"Attach a roof to a flat, upward-facing rectangular quad.");
        const auto append = [&](Vec3 point) {
            const int result = int(next.vertices.size());
            next.vertices.push_back(Vec3(point.GetX()/size.GetX(),point.GetY()/size.GetY(),point.GetZ()/size.GetZ())); return result;
        };
        std::vector<std::vector<int>> roofs; const auto& v = original.vertices;
        if (shape==BuildingShape::Wedge) {
            const int a = append(p[v[2]]+Vec3(0,height,0)), b = append(p[v[3]]+Vec3(0,height,0));
            roofs = {{v[0],v[1],a,b},{v[1],v[2],a},{v[2],v[3],b,a},{v[3],v[0],b}};
        } else {
            const Vec3 a = (p[v[0]]+p[v[1]])/2, b = (p[v[2]]+p[v[3]])/2;
            const float inset = shape==BuildingShape::Hip ? .25f : 0.f;
            const int r0 = append(a+(b-a)*inset+Vec3(0,height,0)), r1 = append(b+(a-b)*inset+Vec3(0,height,0));
            roofs = {{v[0],v[1],r0},{v[2],v[3],r1},{v[1],v[2],r1,r0},{v[3],v[0],r0,r1}};
        }
        const auto expanded = next.points();
        int surface = 0; for (const auto& face : next.faces) surface = std::max(surface,face.surface+1);
        for (std::size_t i = 0; i<roofs.size(); ++i) {
            std::vector<BuildingUV> chart;
            for (int vertex : roofs[i]) {
                BuildingUV uv,meters;
                if (!sample_face(original,p,expanded[vertex],uv,meters,max_height+1))
                    return fail(error,"Roof attachment cannot preserve this surface chart; re-project the face first.");
                chart.push_back(uv);
            }
            auto face = inherited_face(original,std::move(roofs[i]),std::move(chart)); face.part = part;
            float chart_area = 0;
            for (std::size_t j = 0; j<face.uv.size(); ++j) chart_area += face.uv[j][0]*face.uv[(j+1)%face.uv.size()][1]-face.uv[j][1]*face.uv[(j+1)%face.uv.size()][0];
            // Gable ends projected onto the old cap have only floating-point roundoff area.
            if (std::abs(chart_area)<1e-6f) { face.uv = mapped_face(face.vertices,expanded).uv; face.surface = surface++; }
            if (i==0) next.faces[index] = std::move(face); else next.faces.push_back(std::move(face));
        }
        for (auto& decal : next.decals) if (decal.face==index && decal.surface==original.surface && next.faces[index].surface!=original.surface)
            for (int i = 0; i<int(next.faces.size()); ++i) if (next.faces[i].surface==original.surface) { decal.face = i; break; }
    }
    return apply_part(*this,std::move(next),error);
}
bool BuildingMesh::transform_part(int part,Vec3 translation,Vec3 rotation_degrees,Vec3 scale,std::string& error) {
    const auto selection = part_vertices(part);
    if (selection.empty() || !finite(translation) || !finite(rotation_degrees) || !finite(scale)
        || scale.GetX()<.001f || scale.GetY()<.001f || scale.GetZ()<.001f)
        return fail(error,"Select a part and finite transforms with positive scale.");
    std::set<int> selected(selection.begin(),selection.end());
    for (const auto& face : faces) if (face.part!=part) for (int vertex : face.vertices) if (selected.count(vertex))
        return fail(error,"Attached parts share a seam; move their vertices or duplicate the part before transforming.");
    BuildingMesh next = *this; const auto p = points(); Vec3 low = p[selection.front()], high = low;
    for (int vertex : selection) { low = Vec3::sMin(low,p[vertex]); high = Vec3::sMax(high,p[vertex]); }
    const Vec3 center = (low+high)/2;
    for (int vertex : selection) {
        const Vec3 point = center+rotated((p[vertex]-center)*scale,rotation_degrees)+translation;
        next.vertices[vertex] = Vec3(point.GetX()/size.GetX(),point.GetY()/size.GetY(),point.GetZ()/size.GetZ());
    }
    for (auto& face : next.faces) if (face.part==part) {
        face.origin = center+rotated((face.origin-center)*scale,rotation_degrees)+translation;
        face.right = rotated(face.right*scale,rotation_degrees).NormalizedOr(Vec3::sAxisX());
        face.up = rotated(face.up*scale,rotation_degrees).NormalizedOr(Vec3::sAxisY());
    }
    return apply_part(*this,std::move(next),error);
}
int BuildingMesh::duplicate_part(int part,Vec3 offset,std::string& error) {
    const auto selection = part_vertices(part);
    if (selection.empty() || !finite(offset)) { fail(error,"Select a part and a finite duplicate offset."); return -1; }
    BuildingMesh next = *this; next.preserve_surfaces(); const auto source = next.faces; const auto source_decals = next.decals;
    const int duplicate = next_part(next); int next_surface = 0;
    for (const auto& face : source) next_surface = std::max(next_surface,face.surface+1);
    std::map<int,int> remap, face_remap, surfaces;
    for (int vertex : selection) {
        remap[vertex] = int(next.vertices.size());
        next.vertices.push_back(vertices[vertex]+Vec3(offset.GetX()/size.GetX(),offset.GetY()/size.GetY(),offset.GetZ()/size.GetZ()));
    }
    for (int i = 0; i<int(source.size()); ++i) if (source[i].part==part) {
        auto face = source[i]; for (int& vertex : face.vertices) vertex = remap[vertex];
        if (!surfaces.count(face.surface)) surfaces[face.surface] = next_surface++;
        face.surface = surfaces[face.surface]; face.origin += offset;
        face.part = duplicate; face_remap[i] = int(next.faces.size()); next.faces.push_back(std::move(face));
    }
    for (auto decal : source_decals) if (surfaces.count(decal.surface)) {
        const int source_surface = decal.surface; decal.surface = surfaces[source_surface];
        if (face_remap.count(decal.face)) decal.face = face_remap[decal.face];
        else for (const auto& entry : face_remap) if (source[entry.first].surface==source_surface) { decal.face = entry.second; break; }
        next.decals.push_back(std::move(decal));
    }
    return apply_part(*this,std::move(next),error) ? duplicate : -1;
}
bool BuildingMesh::remove_part(int part,std::string& error) {
    std::vector<int> selection;
    for (int i = 0; i<int(faces.size()); ++i) if (faces[i].part==part) selection.push_back(i);
    return remove_faces(selection,error);
}
BuildingMesh BuildingMesh::baked() const {
    BuildingMesh result = *this; std::set<int> removed;
    // ponytail: coincident boundaries only; partial intersections require an explicit boolean union.
    const auto p = points(); using Key = std::vector<std::array<long long,3>>;
    std::map<Key,int> coincident;
    for (int i = 0; i<int(faces.size()); ++i) {
        Key key; std::vector<Vec3> polygon;
        for (int v : faces[i].vertices) {
            polygon.push_back(p[v]); key.push_back({std::llround(p[v].GetX()*100000),std::llround(p[v].GetY()*100000),std::llround(p[v].GetZ()*100000)});
        }
        auto reverse = key; std::reverse(reverse.begin(),reverse.end());
        std::rotate(key.begin(),std::min_element(key.begin(),key.end()),key.end());
        std::rotate(reverse.begin(),std::min_element(reverse.begin(),reverse.end()),reverse.end());
        key = std::min(key,reverse); const auto previous = coincident.find(key);
        if (previous==coincident.end()) { coincident.emplace(std::move(key),i); continue; }
        std::vector<Vec3> other; for (int v : faces[previous->second].vertices) other.push_back(p[v]);
        if (polygon_normal(polygon).Dot(polygon_normal(other))<-.999f) {
            removed.insert(previous->second); removed.insert(i); coincident.erase(previous);
        }
    }
    if (removed.size()<faces.size()) erase_faces(result,removed);
    std::vector<int> remap(result.vertices.size(),-1); std::vector<Vec3> compact;
    for (auto& face : result.faces) for (int& vertex : face.vertices) {
        if (remap[vertex]<0) { remap[vertex] = int(compact.size()); compact.push_back(result.vertices[vertex]); }
        vertex = remap[vertex];
    }
    result.vertices = std::move(compact); return result;
}
void BuildingMesh::write(std::ostream& file) const {
    file << std::setprecision(std::numeric_limits<float>::max_digits10) << std::quoted(name) << ' ' << std::quoted(texture)
        << ' ' << size.GetX() << ' ' << size.GetY() << ' ' << size.GetZ() << '\n';
    file << vertices.size() << ' ' << faces.size() << '\n';
    for (auto p : vertices) file << p.GetX() << ' ' << p.GetY() << ' ' << p.GetZ() << '\n';
    for (const auto& face : faces) {
        file << face.vertices.size();
        for (std::size_t i = 0; i<face.vertices.size(); ++i) file << ' ' << face.vertices[i] << ' ' << face.uv[i][0] << ' ' << face.uv[i][1];
        file << ' ' << face.part << ' ' << face.material << ' ' << face.surface << ' ' << int(face.projection) << ' ' << face.meters_per_repeat
            << ' ' << face.offset[0] << ' ' << face.offset[1] << ' ' << face.scale[0] << ' ' << face.scale[1] << ' ' << face.rotation;
        for (auto p : {face.origin,face.right,face.up}) file << ' ' << p.GetX() << ' ' << p.GetY() << ' ' << p.GetZ();
        file << '\n';
    }
    file << materials.size() << '\n';
    for (const auto& material : materials) file << std::quoted(material) << '\n';
    file << decals.size() << '\n';
    for (const auto& d : decals) file << d.face << ' ' << std::quoted(d.texture) << ' ' << d.u << ' ' << d.v << ' ' << d.width << ' ' << d.height
        << ' ' << d.rotation << ' ' << d.surface << ' ' << int(d.projected) << ' ' << d.meters[0] << ' ' << d.meters[1] << '\n';
}
bool BuildingMesh::read(std::istream& file,BuildingMesh& mesh,std::string& error,int version) {
    BuildingMesh next; float x,y,z; int count, face_count;
    if (!(file>>std::quoted(next.name)>>std::quoted(next.texture)>>x>>y>>z)) return fail(error,"Invalid building header.");
    next.size = Vec3(x,y,z);
    if (version>=2) {
        if (!(file>>count>>face_count) || count<3 || count>max_vertices || face_count<1 || face_count>max_faces)
            return fail(error,"Invalid mesh vertex or face count.");
        next.vertices.resize(count); next.faces.clear();
    }
    for (auto& p : next.vertices) {
        if (!(file>>x>>y>>z)) return fail(error,"Invalid building vertices.");
        p = Vec3(x,y,z);
    }
    if (version>=2) {
        std::size_t corners = 0;
        for (int i = 0; i<face_count; ++i) {
            if (!(file>>count) || count<3 || count>64 || (corners+=count)>65536) return fail(error,"Invalid face corner count.");
            BuildingFace face; face.vertices.resize(count); face.uv.resize(count);
            for (int j = 0; j<count; ++j) if (!(file>>face.vertices[j]>>face.uv[j][0]>>face.uv[j][1])) return fail(error,"Invalid face data.");
            if (version>=3) {
                int projection;
                if (!(file>>face.part>>face.material>>face.surface>>projection>>face.meters_per_repeat
                    >>face.offset[0]>>face.offset[1]>>face.scale[0]>>face.scale[1]>>face.rotation)) return fail(error,"Invalid face editor data.");
                face.projection = BuildingProjection(projection);
                for (auto p : {&face.origin,&face.right,&face.up}) {
                    if (!(file>>x>>y>>z)) return fail(error,"Invalid projection frame.");
                    *p = Vec3(x,y,z);
                }
            }
            next.faces.push_back(std::move(face));
        }
    }
    if (version>=3) {
        if (!(file>>count) || count<0 || count>64) return fail(error,"Invalid material count.");
        for (int i = 0; i<count; ++i) {
            std::string material; if (!(file>>std::quoted(material))) return fail(error,"Invalid building material.");
            next.materials.push_back(std::move(material));
        }
    }
    if (!(file>>count) || count<0 || count>64) return fail(error,"Invalid decal count.");
    for (int i = 0; i<count; ++i) {
        BuildingDecal d;
        if (!(file>>d.face>>std::quoted(d.texture)>>d.u>>d.v>>d.width>>d.height)) return fail(error,"Invalid decal data.");
        if (version>=3) {
            int projected;
            if (!(file>>d.rotation>>d.surface>>projected>>d.meters[0]>>d.meters[1]) || projected<0 || projected>1)
                return fail(error,"Invalid decal editor data.");
            d.projected = projected!=0;
        }
        next.decals.push_back(std::move(d));
    }
    if (!next.validate(error)) return false;
    mesh = std::move(next); return true;
}

bool BuildingMesh::save(const std::filesystem::path& directory,std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) return fail(error,"Cannot create the buildings folder.");
    const auto path = directory/("building-"+name+".building"); auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return fail(error,"Cannot write building; previous save retained.");
    file << "AMBARETTO_BUILDING 3\n"; write(file); file.close();
    if (!file) return fail(error,"Cannot finish saving building; previous save retained.");
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,path,ec); const bool replaced = !ec;
#endif
    if (!replaced) return fail(error,"Cannot replace building save; previous save retained.");
    error.clear(); return true;
}
bool BuildingMesh::load(const std::filesystem::path& path,BuildingMesh& mesh,std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path,ec)>8*1024*1024 || ec) return fail(error,"Building save is missing or too large.");
    std::ifstream file(path); std::string magic, extra; int version; BuildingMesh next;
    if (!(file>>magic>>version) || magic!="AMBARETTO_BUILDING" || version<1 || version>3) return fail(error,"Unsupported building save.");
    if (!read(file,next,error,version)) return false;
    if (file>>extra) return fail(error,"Unexpected building data.");
    mesh = std::move(next); return true;
}
std::vector<std::string> building_textures(const std::filesystem::path& directory,std::string& error) {
    std::vector<std::string> result; std::error_code ec; error.clear();
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (it->is_regular_file(ec) && texture_name(name)) result.push_back(name);
    }
    if (ec) error = "Cannot read the textures folder.";
    std::sort(result.begin(),result.end()); return result;
}
std::vector<BuildingMesh> saved_buildings(const std::filesystem::path& directory,std::string& error) {
    std::vector<BuildingMesh> result; std::error_code ec; error.clear();
    if (!std::filesystem::exists(directory,ec) && !ec) return result;
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension()!=".building") continue;
        BuildingMesh mesh; std::string message;
        if (BuildingMesh::load(it->path(),mesh,message)) result.push_back(std::move(mesh));
        else error = it->path().filename().string()+": "+message;
    }
    if (ec) error = "Cannot read the buildings folder.";
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;}); return result;
}
} // namespace ambaretto
