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
BuildingFace mapped_face(std::vector<int> indices,const std::vector<Vec3>& p) {
    BuildingFace face; face.vertices = std::move(indices); face.uv.clear();
    std::vector<Vec3> polygon; for (int i : face.vertices) polygon.push_back(p[i]);
    const Vec3 right = (polygon[1]-polygon[0]).NormalizedOr(Vec3::sAxisX()), up = polygon_normal(polygon).Cross(right);
    float lo_u = 1e9f, hi_u = -1e9f, lo_v = lo_u, hi_v = hi_u;
    for (auto point : polygon) {
        const float u = point.Dot(right), v = -point.Dot(up);
        lo_u = std::min(lo_u,u); hi_u = std::max(hi_u,u); lo_v = std::min(lo_v,v); hi_v = std::max(hi_v,v);
    }
    for (auto point : polygon) face.uv.push_back({(point.Dot(right)-lo_u)/std::max(.0001f,hi_u-lo_u),(-point.Dot(up)-lo_v)/std::max(.0001f,hi_v-lo_v)});
    return face;
}
bool apply(BuildingMesh& mesh,BuildingMesh next,std::string& error) {
    if (!next.fit_bounds(error) || !next.validate(error)) return false;
    mesh = std::move(next); error.clear(); return true;
}
void erase_faces(BuildingMesh& mesh,const std::set<int>& removed) {
    std::vector<int> remap(mesh.faces.size(),-1); std::vector<BuildingFace> faces;
    for (std::size_t i = 0; i<mesh.faces.size(); ++i) if (!removed.count(int(i))) {
        remap[i] = int(faces.size()); faces.push_back(std::move(mesh.faces[i]));
    }
    mesh.faces = std::move(faces);
    mesh.decals.erase(std::remove_if(mesh.decals.begin(),mesh.decals.end(),[&](auto& d) {
        d.face = remap[d.face]; return d.face<0;
    }),mesh.decals.end());
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
std::vector<BuildingTriangle> BuildingMesh::triangles(Vec3 base) const {
    const auto p = points(); std::vector<BuildingTriangle> result;
    for (std::size_t f = 0; f<faces.size(); ++f) {
        const auto& face = faces[f]; std::vector<Vec3> polygon;
        for (int vertex : face.vertices) polygon.push_back(p[vertex]);
        for (const auto& t : triangulate_polygon(polygon))
            result.push_back({{polygon[t[0]]+base,polygon[t[1]]+base,polygon[t[2]]+base},
                {face.uv[t[0]],face.uv[t[1]],face.uv[t[2]]},texture,false,int(f)});
    }
    const auto body_count = result.size();
    // Clip decals to each polygon's UV triangles, including subdivided and concave faces.
    for (const auto& decal : decals) for (std::size_t i = 0; i<body_count; ++i) {
        const auto triangle = result[i]; if (triangle.face!=decal.face) continue;
        const auto side = [](BuildingUV a,BuildingUV b,BuildingUV c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
        const float area = side(triangle.uv[0],triangle.uv[1],triangle.uv[2]); if (std::abs(area)<1e-8f) continue;
        std::vector<BuildingUV> polygon{{decal.u,decal.v},{decal.u+decal.width,decal.v},
            {decal.u+decal.width,decal.v+decal.height},{decal.u,decal.v+decal.height}};
        if (area<0) std::reverse(polygon.begin(),polygon.end());
        for (int edge = 0; edge<3 && !polygon.empty(); ++edge) {
            std::vector<BuildingUV> clipped; auto before = polygon.back();
            float previous = side(triangle.uv[edge],triangle.uv[(edge+1)%3],before)/area;
            for (auto current : polygon) {
                const float distance = side(triangle.uv[edge],triangle.uv[(edge+1)%3],current)/area;
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
            const float b = side(triangle.uv[0],q,triangle.uv[2])/area, c = side(triangle.uv[0],triangle.uv[1],q)/area;
            return triangle.points[0]*(1-b-c)+triangle.points[1]*b+triangle.points[2]*c+normal*.015f;
        };
        for (std::size_t k = 1; k+1<polygon.size(); ++k) {
            const std::array<BuildingUV,3> q{{polygon[0],polygon[k],polygon[k+1]}};
            BuildingTriangle t{{point(q[0]),point(q[1]),point(q[2])},{},decal.texture,true,decal.face};
            if ((t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).LengthSq()<1e-10f) continue;
            for (int corner = 0; corner<3; ++corner) t.uv[corner] = {(q[corner][0]-decal.u)/decal.width,(q[corner][1]-decal.v)/decal.height};
            result.push_back(std::move(t));
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
    if (vertices.size()<3 || vertices.size()>max_vertices || faces.empty() || faces.size()>max_faces)
        return fail(error,"Use 3-4096 vertices and 1-8192 faces.");
    for (auto p : vertices) if (!finite(p) || std::abs(p.GetX())>.50001f || std::abs(p.GetZ())>.50001f || p.GetY()<-.00001f || p.GetY()>1.00001f)
        return fail(error,"Vertex positions exceed the building dimensions.");
    const auto p = points(); std::map<std::pair<int,int>,std::pair<int,int>> edges; std::size_t corners = 0;
    for (const auto& face : faces) {
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
        || d.u<0 || d.v<0 || d.width<.01f || d.height<.01f || d.u+d.width>1.00001f || d.v+d.height>1.00001f)
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
    BuildingMesh next = *this; const auto original = faces[index]; const auto p = points(); std::vector<Vec3> polygon;
    for (int v : original.vertices) polygon.push_back(p[v]);
    std::vector<BuildingFace> pieces;
    for (const auto& t : triangulate_polygon(polygon)) {
        const int center = int(next.vertices.size());
        next.vertices.push_back((vertices[original.vertices[t[0]]]+vertices[original.vertices[t[1]]]+vertices[original.vertices[t[2]]])/3);
        const BuildingUV uv{(original.uv[t[0]][0]+original.uv[t[1]][0]+original.uv[t[2]][0])/3,
            (original.uv[t[0]][1]+original.uv[t[1]][1]+original.uv[t[2]][1])/3};
        for (int i = 0; i<3; ++i) {
            const int a = t[i], b = t[(i+1)%3];
            pieces.push_back({{original.vertices[a],original.vertices[b],center},{original.uv[a],original.uv[b],uv}});
        }
    }
    if (pieces.empty()) return fail(error,"Face cannot be subdivided.");
    const auto decals_before = next.decals;
    next.faces[index] = pieces.front();
    for (std::size_t i = 1; i<pieces.size(); ++i) {
        const int face = int(next.faces.size()); next.faces.push_back(pieces[i]);
        for (auto d : decals_before) if (d.face==index) { d.face = face; next.decals.push_back(std::move(d)); }
    }
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::extrude_face(int index,float distance,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !std::isfinite(distance) || std::abs(distance)<.01f)
        return fail(error,"Select a face and extrude by at least 0.01 m.");
    BuildingMesh next = *this; const auto original = faces[index]; const auto p = points(); std::vector<Vec3> polygon;
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
        next.faces.push_back(mapped_face({original.vertices[i],original.vertices[j],cap_indices[j],cap_indices[i]},expanded));
    }
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::inset_face(int index,float fraction,std::string& error) {
    if (index<0 || index>=int(faces.size()) || !std::isfinite(fraction) || fraction<.01f || fraction>.95f)
        return fail(error,"Select a face and inset by a fraction between 0.01 and 0.95.");
    BuildingMesh next = *this; const auto original = faces[index]; const auto p = points();
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
    const auto decals_before = next.decals;
    for (std::size_t i = 0; i<original.vertices.size(); ++i) {
        const auto j = (i+1)%original.vertices.size();
        const int face = int(next.faces.size());
        next.faces.push_back({{original.vertices[i],original.vertices[j],inner.vertices[j],inner.vertices[i]},
            {original.uv[i],original.uv[j],inner.uv[j],inner.uv[i]}});
        for (auto d : decals_before) if (d.face==index) { d.face = face; next.decals.push_back(std::move(d)); }
    }
    return apply(*this,std::move(next),error);
}
bool BuildingMesh::slice(int axis,float position,std::string& error) {
    if (axis<0 || axis>2 || !std::isfinite(position)) return fail(error,"Choose an X, Y or Z slice position.");
    const auto component = [axis](Vec3 p) { return axis==0 ? p.GetX() : axis==1 ? p.GetY() : p.GetZ(); };
    const auto p = points(); BuildingMesh next = *this;
    std::map<std::pair<int,int>,int> intersections;
    bool cut = false;
    for (std::size_t f = 0; f<faces.size(); ++f) {
        const auto& original = faces[f]; bool positive = false, negative = false; std::vector<Vec3> polygon;
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
            for (auto t : triangulate_polygon(polygon)) source.push_back({{original.vertices[t[0]],original.vertices[t[1]],original.vertices[t[2]]},
                {original.uv[t[0]],original.uv[t[1]],original.uv[t[2]]}});
        }
        for (const auto& face : source) for (float sign : {-1.f,1.f}) {
            BuildingFace piece; piece.uv.clear();
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
            const int target = int(next.faces.size()); next.faces.push_back(std::move(pieces[i]));
            for (auto d : decals) if (d.face==int(f)) { d.face = target; next.decals.push_back(std::move(d)); }
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
    BuildingMesh next = *this; next.faces.push_back(mapped_face(std::move(selection),points()));
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
void BuildingMesh::write(std::ostream& file) const {
    file << std::setprecision(std::numeric_limits<float>::max_digits10) << std::quoted(name) << ' ' << std::quoted(texture)
        << ' ' << size.GetX() << ' ' << size.GetY() << ' ' << size.GetZ() << '\n';
    file << vertices.size() << ' ' << faces.size() << '\n';
    for (auto p : vertices) file << p.GetX() << ' ' << p.GetY() << ' ' << p.GetZ() << '\n';
    for (const auto& face : faces) {
        file << face.vertices.size();
        for (std::size_t i = 0; i<face.vertices.size(); ++i) file << ' ' << face.vertices[i] << ' ' << face.uv[i][0] << ' ' << face.uv[i][1];
        file << '\n';
    }
    file << decals.size() << '\n';
    for (const auto& d : decals) file << d.face << ' ' << std::quoted(d.texture) << ' ' << d.u << ' ' << d.v << ' ' << d.width << ' ' << d.height << '\n';
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
            next.faces.push_back(std::move(face));
        }
    }
    if (!(file>>count) || count<0 || count>64) return fail(error,"Invalid decal count.");
    for (int i = 0; i<count; ++i) {
        BuildingDecal d;
        if (!(file>>d.face>>std::quoted(d.texture)>>d.u>>d.v>>d.width>>d.height)) return fail(error,"Invalid decal data.");
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
    file << "AMBARETTO_BUILDING 2\n"; write(file); file.close();
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
    if (!(file>>magic>>version) || magic!="AMBARETTO_BUILDING" || version<1 || version>2) return fail(error,"Unsupported building save.");
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
