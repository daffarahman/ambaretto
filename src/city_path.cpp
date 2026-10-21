#include "city.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace ambaretto {
namespace {
float cross(Vec3 a, Vec3 b) { return a.GetX()*b.GetZ()-a.GetZ()*b.GetX(); }
bool finite(Vec3 p) { return std::isfinite(p.GetX()) && std::isfinite(p.GetY()) && std::isfinite(p.GetZ()); }
}
float path_distance(Vec3 p, Vec3 a, Vec3 b) {
    p.SetY(0); a.SetY(0); b.SetY(0);
    const Vec3 d = b-a;
    return (p-a-d*std::clamp((p-a).Dot(d)/std::max(d.LengthSq(),.000001f),0.f,1.f)).Length();
}
std::vector<Vec3> CityPath::sample(float spacing) const {
    if (nodes.empty() || !std::isfinite(spacing) || spacing<=0) return {};
    for (const auto& n : nodes) if (!finite(n.position) || !finite(n.tangent)) return {};
    std::vector<Vec3> points{nodes.front().position};
    points.front().SetY(City::level);
    const std::size_t count = closed ? nodes.size() : nodes.size()-1;
    for (std::size_t i=0; i<count; ++i) {
        const auto& a=nodes[i]; const auto& b=nodes[(i+1)%nodes.size()];
        const Vec3 c=a.position+a.tangent, d=b.position-b.tangent;
        const float length=(c-a.position).Length()+(d-c).Length()+(b.position-d).Length();
        if (!std::isfinite(length) || length>City::extent*16) return {};
        const int steps=std::clamp(int(std::ceil(length/spacing)),1,2048);
        for (int j=1; j<=steps; ++j) {
            const float t=float(j)/steps;
            // Stable interpolation keeps straight edges exact instead of generating clipping slivers.
            const Vec3 ab=a.position+(c-a.position)*t,bc=c+(d-c)*t,cd=d+(b.position-d)*t;
            const Vec3 abc=ab+(bc-ab)*t,bcd=bc+(cd-bc)*t;
            Vec3 p=abc+(bcd-abc)*t;
            p.SetY(City::level);
            if ((p-points.back()).LengthSq()>.000001f) points.push_back(p);
        }
    }
    return points;
}
bool polygon_contains(const std::vector<Vec3>& polygon, Vec3 p) {
    if (polygon.size()<3) return false;
    bool inside=false;
    Vec3 a=polygon.back();
    for (Vec3 b : polygon) {
        if (p.GetX()>=std::min(a.GetX(),b.GetX())-.0001f && p.GetX()<=std::max(a.GetX(),b.GetX())+.0001f
            && p.GetZ()>=std::min(a.GetZ(),b.GetZ())-.0001f && p.GetZ()<=std::max(a.GetZ(),b.GetZ())+.0001f
            && path_distance(p,a,b)<.0001f) return true;
        if ((a.GetZ()>p.GetZ())!=(b.GetZ()>p.GetZ()) &&
            p.GetX()<(b.GetX()-a.GetX())*(p.GetZ()-a.GetZ())/(b.GetZ()-a.GetZ())+a.GetX()) inside=!inside;
        a=b;
    }
    return inside;
}
bool CityPath::validate(std::string& error, bool area) const {
    const auto fail=[&](const char* text){error=text; return false;};
    if (nodes.size()<std::size_t(area ? 3 : 2) || nodes.size()>1024 || (area && !closed))
        return fail(area ? "Close an area with at least three points (maximum 1024)." : "A road needs at least two points (maximum 1024).");
    for (const auto& n : nodes) {
        if (!finite(n.position) || !finite(n.tangent) || std::abs(n.position.GetX())>City::extent || std::abs(n.position.GetZ())>City::extent || n.tangent.Length()>City::extent*2)
            return fail("Path coordinates and handles must be finite and inside the map.");
    }
    const auto points=sample();
    if (points.size()<std::size_t(area ? 4 : 2) || points.size()>16384) return fail("Path is empty or too detailed.");
    for (Vec3 p : points) if (std::abs(p.GetX())>City::extent || std::abs(p.GetZ())>City::extent)
        return fail("Keep the entire curve inside the map.");
    if (area) {
        float signed_area=0;
        for (std::size_t i=1;i<points.size();++i) signed_area+=cross(points[i-1],points[i]);
        if (std::abs(signed_area)<1) return fail("The area is too small to fill.");
        for (std::size_t i=1;i<points.size();++i) for (std::size_t j=i+2;j<points.size();++j) {
            if (i==1 && j==points.size()-1) continue;
            const Vec3 a=points[i-1], b=points[i], c=points[j-1], d=points[j];
            if(std::max(a.GetX(),b.GetX())+.00001f<std::min(c.GetX(),d.GetX()) || std::min(a.GetX(),b.GetX())-.00001f>std::max(c.GetX(),d.GetX())
                || std::max(a.GetZ(),b.GetZ())+.00001f<std::min(c.GetZ(),d.GetZ()) || std::min(a.GetZ(),b.GetZ())-.00001f>std::max(c.GetZ(),d.GetZ()))continue;
            const float denominator=cross(b-a,d-c);
            if (std::abs(denominator)<.00001f) {
                if (path_distance(a,c,d)<.0001f || path_distance(b,c,d)<.0001f)
                    return fail("Area edges overlap. Move or remove the crossing points.");
                continue;
            }
            const float t=cross(c-a,d-c)/denominator, u=cross(c-a,b-a)/denominator;
            if (t>=0 && t<=1 && u>=0 && u<=1) return fail("Area edges cross. Move or remove the crossing points.");
        }
    }
    error.clear(); return true;
}
std::vector<std::array<Vec3,3>> polygon_triangles(std::vector<Vec3> polygon) {
    if (polygon.size()>1 && (polygon.front()-polygon.back()).LengthSq()<.000001f) polygon.pop_back();
    bool simplified=true;
    while (simplified && polygon.size()>3) {
        simplified=false;
        for (std::size_t i=0;i<polygon.size();++i) {
            const Vec3 a=polygon[(i+polygon.size()-1)%polygon.size()], b=polygon[i], c=polygon[(i+1)%polygon.size()];
            if (path_distance(b,a,c)<.001f) {polygon.erase(polygon.begin()+i); simplified=true; break;}
        }
    }
    float area=0;
    for (std::size_t i=0;i<polygon.size();++i) area+=cross(polygon[i],polygon[(i+1)%polygon.size()]);
    if (area>0) std::reverse(polygon.begin(),polygon.end());
    std::vector<std::array<Vec3,3>> result;
    while (polygon.size()>2) {
        bool ear=false;
        for (std::size_t i=0;i<polygon.size();++i) {
            const Vec3 a=polygon[(i+polygon.size()-1)%polygon.size()], b=polygon[i], c=polygon[(i+1)%polygon.size()];
            if (cross(b-a,c-b)>=-.000001f) continue;
            bool occupied=false;
            for (std::size_t j=0;j<polygon.size();++j) {
                if (j==i || j==(i+1)%polygon.size() || j==(i+polygon.size()-1)%polygon.size()) continue;
                const Vec3 p=polygon[j];
                if (cross(b-a,p-a)<=.000001f && cross(c-b,p-b)<=.000001f && cross(a-c,p-c)<=.000001f) {occupied=true; break;}
            }
            if (occupied) continue;
            result.push_back({a,b,c}); polygon.erase(polygon.begin()+i); ear=true; break;
        }
        if (!ear) return {};
    }
    return result;
}
std::pair<std::vector<Vec3>,std::vector<Vec3>> split_path_polygon(const std::vector<Vec3>& polygon,Vec3 a,Vec3 b) {
    std::vector<Vec3> inside,outside;
    if (polygon.empty()) return {inside,outside};
    const auto distance=[&](Vec3 p){return (b-a).Cross(p-a).GetY();};
    Vec3 previous=polygon.back();float before=distance(previous);
    for (auto current:polygon) {
        const float after=distance(current);
        if ((before>=0)!=(after>=0)) {const Vec3 p=previous+(current-previous)*(before/(before-after));inside.push_back(p);outside.push_back(p);}
        if (after>=0) inside.push_back(current);
        if (after<=0) outside.push_back(current);
        previous=current;before=after;
    }
    return {std::move(inside),std::move(outside)};
}
std::vector<Vec3> offset_path(const std::vector<Vec3>& points, float offset) {
    std::vector<Vec3> result;
    if (points.size()<2) return result;
    const bool closed=(points.front()-points.back()).LengthSq()<.0001f;
    for (std::size_t i=0;i<points.size();++i) {
        const Vec3 a=points[i==0 ? (closed ? points.size()-2 : 0) : i-1];
        const Vec3 b=points[i+1==points.size() ? (closed ? 1 : i) : i+1];
        const Vec3 before=(points[i]-a).NormalizedOr((b-points[i]).NormalizedOr(Vec3::sAxisX()));
        const Vec3 after=(b-points[i]).NormalizedOr(before);
        const Vec3 normal=(before+after).NormalizedOr(after).Cross(Vec3::sAxisY());
        const float miter=std::max(.5f,std::abs(normal.Dot(after.Cross(Vec3::sAxisY()))));
        result.push_back(points[i]+normal*(offset/miter));
    }
    return result;
}
} // namespace ambaretto
