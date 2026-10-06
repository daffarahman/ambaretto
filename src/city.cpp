#include "city.hpp"
#include "environment.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <queue>
#include <random>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace forza {
namespace {
float coast_height(const City& city,float x,float z) {
    const auto cell = City::cell(x,z);
    if (city.land(cell)) return city.height(x,z);
    float distance_sq = 32*32, shore_height = City::level;
    constexpr int reach = int(32 / City::block) + 1;
    for (int pz = cell.z-reach; pz <= cell.z+reach; ++pz) for (int px = cell.x-reach; px <= cell.x+reach; ++px) {
        if (!city.land({px,pz})) continue;
        const float left = (px-City::width/2)*City::block, top = (pz-City::width/2)*City::block;
        const float dx = std::max({left-x,0.f,x-left-City::block}), dz = std::max({top-z,0.f,z-top-City::block});
        if (dx*dx+dz*dz < distance_sq) {
            distance_sq = dx*dx+dz*dz;
            shore_height = city.height(std::clamp(x,left+.001f,left+City::block-.001f),
                std::clamp(z,top+.001f,top+City::block-.001f));
        }
    }
    const float distance = std::sqrt(distance_sq);
    const auto smooth = [](float t) { return t*t*(3-2*t); };
    // The shore reaches shallow water, then tapers to the seabed.
    return distance <= 10 ? shore_height-(shore_height+1)*smooth(distance/10) : -1-7*smooth((distance-10)/22);
}
}
Environment::Environment(const City& city) : city_(std::make_shared<City>(city)) {
    std::string error;
    if (!city.validate(error)) throw std::runtime_error(error);
    const bool elevated = std::any_of(city.elevation.begin(),city.elevation.end(),[](auto h){return h!=0;});
    city_diagonal_bridge_owner_.fill(-1);
    const auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Surface surface) {
        const auto i = std::uint32_t(vertices_.size());
        vertices_.insert(vertices_.end(),{a,b,c,d});
        triangles_.push_back({i,i+1,i+2,surface}); triangles_.push_back({i,i+2,i+3,surface});
    };
    const auto triangle = [&](Vec3 a, Vec3 b, Vec3 c, Surface surface, bool deck = false) {
        if ((b-a).Cross(c-a).LengthSq()<.00000001f) return;
        const auto i = std::uint32_t(vertices_.size()); vertices_.insert(vertices_.end(),{a,b,c});
        triangles_.push_back({i,i+1,i+2,surface,deck});
    };
    quad({-extent,-8,-extent},{-extent,-8,extent},{extent,-8,extent},{extent,-8,-extent},Surface::Seabed);
    int min_x = City::width, min_z = min_x, max_x = -1, max_z = -1;
    for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x) {
        CityCell p{x,z};
        if (!city.land(p)) continue;
        min_x = std::min(min_x,x); min_z = std::min(min_z,z); max_x = std::max(max_x,x); max_z = std::max(max_z,z);
        const Surface surfaces[] = {Surface::Soil,Surface::Grass,Surface::Sand,Surface::Road};
        const Surface surface = surfaces[int(city.ground[City::index(p)])];
        for (const auto& t : city.ground_triangles(p)) triangle(t[0],t[1],t[2],surface);
    }
    if (min_x <= max_x && min_z <= max_z) {
        std::array<bool,City::width*City::width> seen{};
        for (int i = 0; i < City::width*City::width; ++i) if (city.land({i%City::width,i/City::width}) && !seen[i]) {
            CityCell low{i%City::width,i/City::width}, high = low;
            std::vector<CityCell> pending{low}; seen[i] = true;
            for (std::size_t j = 0; j < pending.size(); ++j) {
                const auto p = pending[j];
                low.x = std::min(low.x,p.x); low.z = std::min(low.z,p.z);
                high.x = std::max(high.x,p.x); high.z = std::max(high.z,p.z);
                for (auto d : {CityCell{0,-1},CityCell{1,0},CityCell{0,1},CityCell{-1,0}}) {
                    const CityCell next{p.x+d.x,p.z+d.z};
                    if (city.land(next) && !seen[City::index(next)]) { seen[City::index(next)] = true; pending.push_back(next); }
                }
            }
            const Vec3 a = City::center(low), b = City::center(high);
            city_islands_.push_back({(a+b)/2,(b.GetX()-a.GetX()+City::block)/2,(b.GetZ()-a.GetZ()+City::block)/2,city_->name.c_str()});
        }
        // Shared samples keep the visible shore, ground queries, and Jolt mesh identical.
        city_heights_.assign(city_mesh_width*city_mesh_width,-8);
        const int x0 = min_x*4, z0 = min_z*4, x1 = (max_x+1)*4+24, z1 = (max_z+1)*4+24;
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x)
            city_heights_[z*city_mesh_width+x] = coast_height(city,x*city_mesh_spacing-city_mesh_extent,z*city_mesh_spacing-city_mesh_extent);
        const auto point = [&](int x,int z) {
            return Vec3(x*city_mesh_spacing-city_mesh_extent,city_heights_[z*city_mesh_width+x],z*city_mesh_spacing-city_mesh_extent);
        };
        for (int z = z0; z < z1; ++z) for (int x = x0; x < x1; ++x) {
            const auto a = point(x,z), b = point(x,z+1), c = point(x+1,z+1), d = point(x+1,z);
            const auto cell = City::cell(a.GetX()+city_mesh_spacing/2,a.GetZ()+city_mesh_spacing/2);
            if (city.land(cell) || std::max({a.GetY(),b.GetY(),c.GetY(),d.GetY()})<=-8) continue;
            quad(a,b,c,d,Surface::Soil);
        }
    }
    const auto network = city.road_network();
    for (const auto& node : network) {
        const auto path = node.path();
        const bool diagonal = path.size()==2 && std::abs(path[1].GetX()-path[0].GetX())>.001f && std::abs(path[1].GetZ()-path[0].GetZ())>.001f;
        if (!path.empty()) {
            float width = 0;
            for (int d = 0; d < 4; ++d) if (node.mask&(1u<<d)) width = width==0 ? node.port(d).width : std::min(width,node.port(d).width);
            width -= 1.4f;
            if (diagonal) {
                const auto inner = node.path(0,.7f), outer = node.path(1,-.7f);
                Vec3 along = path[1]-path[0]; along.SetY(0);
                const Vec3 side = along.Normalized().Cross(Vec3::sAxisY());
                width = std::min(std::abs((inner[0]-outer[0]).Dot(side)),std::abs((inner[1]-outer[1]).Dot(side)));
            }
            for (std::size_t i = 1; i < path.size(); ++i) city_roads_.push_back({path[i-1],path[i],width,"CITY STREET"});
        } else for (int d = 0; d < 4; ++d) if (node.mask&(1u<<d))
            city_roads_.push_back({node.center(),node.port(d).center,node.port(d).width-1.4f,"CITY STREET"});
        if (!diagonal) continue;
        bool water = false;
        for (int z = node.first.z; z <= node.last.z; ++z) for (int x = node.first.x; x <= node.last.x; ++x)
            water |= city.tile({x,z})==CityTile::Bridge;
        if (!water) continue;
        const auto inner = node.path(0,0), outer = node.path(1,0);
        const int owner = int(city_diagonal_bridge_decks_.size());
        city_diagonal_bridge_decks_.push_back({inner[0],outer[0],outer[1],inner[1]});
        for (int z = node.first.z; z <= node.last.z; ++z) for (int x = node.first.x; x <= node.last.x; ++x)
            if (city.tile({x,z})==CityTile::Bridge) city_diagonal_bridge_owner_[City::index({x,z})] = owner;
        for (const auto& face : {std::array<Vec3,3>{inner[0],outer[0],outer[1]},std::array<Vec3,3>{inner[0],outer[1],inner[1]}})
            for (const auto& t : city.drape_triangle(face)) triangle(t[0],t[1],t[2],Surface::Road,true);
        const float width = (inner[0]-outer[0]).Length();
        city_bridges_.push_back({path[0],path[1],width,City::level,"CITY BRIDGE",
            (inner[0]-path[0])/(width/2),(inner[1]-path[1])/(width/2)});
        for (const auto& edge : {inner,outer}) {
            const int steps = elevated ? std::max(1,int(std::ceil((edge[1]-edge[0]).Length()/(City::block/4)))) : 1;
            for (int i = 0; i < steps; ++i) {
                Vec3 a = edge[0]+(edge[1]-edge[0])*(float(i)/steps), b = edge[0]+(edge[1]-edge[0])*(float(i+1)/steps);
                a.SetY(city.height(a.GetX(),a.GetZ())); b.SetY(city.height(b.GetX(),b.GetZ()));
                const Vec3 delta = b-a;
                barriers_.push_back({(a+b)/2+Vec3(0,.45f,0),Vec3(.2f,.85f,delta.Length()),
                    std::atan2(delta.GetX(),delta.GetZ()),-std::atan2(delta.GetY(),std::hypot(delta.GetX(),delta.GetZ()))});
            }
        }
        Vec3 support = (path[0]+path[1])/2; const float top = city.height(support.GetX(),support.GetZ());
        support.SetY((top-8)/2); barriers_.push_back({support,Vec3(1.2f,top+8,1.2f),0});
    }
    for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x) if (city.tile({x,z}) == CityTile::Bridge) {
        if (city_diagonal_bridge_owner_[City::index({x,z})]>=0) continue;
        const Vec3 c = City::center({x,z},city.tile_height({x,z})); const float h = City::half_block;
        const auto patch = city.ground_patch({x,z});
        for (const auto& t : city.ground_triangles({x,z})) triangle(t[0],t[1],t[2],Surface::Road,true);
        const bool vertical = city.road_axis({x,z}) == 1;
        const Vec3 along = vertical ? Vec3(0,0,h) : Vec3(h,0,0);
        city_bridges_.push_back({c-along,c+along,City::block,City::level,"CITY BRIDGE"});
        for (int d = 0; d < 4; ++d) {
            const CityCell next{x+(d==1)-(d==3),z+(d==2)-(d==0)};
            if (city.road(next)) continue;
            const int edge = (7-d)%4;
            const Vec3 a = patch[edge], b = patch[(edge+1)%4], delta = b-a;
            barriers_.push_back({(a+b)/2+Vec3(0,.45f,0),Vec3(.2f,.85f,delta.Length()),
                std::atan2(delta.GetX(),delta.GetZ()),-std::atan2(delta.GetY(),std::hypot(delta.GetX(),delta.GetZ()))});
        }
        if ((x+z)%3==0) barriers_.push_back({c-Vec3(0,(c.GetY()+8)/2,0),Vec3(1.2f,c.GetY()+8,1.2f),0});
    }
    for (auto b : city.buildings) {
        const Vec3 size(b.size*City::block,float(b.height),b.size*City::block);
        const Vec3 center = City::center(b.cell,city.tile_height(b.cell)+b.height/2.f)
            +Vec3((b.size-1)*City::block/2,0,(b.size-1)*City::block/2);
        buildings_.push_back({center,size,0,BuildingKind::Office});
    }
    trees_ = city.trees;
}
namespace {
constexpr CityCell directions[] = {{0,-1}, {1,0}, {0,1}, {-1,0}};
CityCell add(CityCell a, CityCell b) { return {a.x + b.x, a.z + b.z}; }
bool fail(std::string& error, const char* message) { error = message; return false; }
bool valid_id(const std::string& id) {
    return id.size() == 16 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool valid_name(const std::string& name) {
    return !name.empty() && name.size() <= 48 && name.find_first_not_of(' ') != std::string::npos
        && std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
bool covers(const CityBuilding& b, CityCell p) {
    return p.x >= b.cell.x && p.z >= b.cell.z && p.x < b.cell.x + b.size && p.z < b.cell.z + b.size;
}
float triangle_height(const std::array<Vec3,3>& t, Vec3 p) {
    const float area = (t[1]-t[0]).Cross(t[2]-t[0]).GetY();
    const float b = (p-t[0]).Cross(t[2]-t[0]).GetY()/area;
    const float c = (t[1]-t[0]).Cross(p-t[0]).GetY()/area;
    return t[0].GetY()+(t[1].GetY()-t[0].GetY())*b+(t[2].GetY()-t[0].GetY())*c;
}
void constrain_elevation(City& city,int direction) {
    std::queue<CityCell> pending;
    for (int z = 0; z < City::corner_width; ++z) for (int x = 0; x < City::corner_width; ++x) pending.push({x,z});
    while (!pending.empty()) {
        const auto p = pending.front(); pending.pop();
        const int height = city.elevation[City::corner_index(p)];
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            const CityCell q{p.x+dx,p.z+dz};
            if (q.x<0 || q.z<0 || q.x>=City::corner_width || q.z>=City::corner_width) continue;
            auto& neighbor = city.elevation[City::corner_index(q)];
            const int value = direction>0 ? std::max(int(neighbor),height-1) : std::min(int(neighbor),height+1);
            if (value!=neighbor) { neighbor = static_cast<unsigned char>(value); pending.push(q); }
        }
    }
}
bool road_fits_ground(const City& city,CityCell p) {
    const unsigned ramp = city.ramp_axis(p), mask = city.road_mask(p);
    unsigned axis = city.road_axis(p);
    for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) axis |= d%2 ? 2 : 1;
    return ramp!=3 && (!ramp || !axis || axis==ramp);
}
void sync_elevation(City& city) {
    std::array<bool,City::width*City::width> seen{};
    for (int i = 0; i < City::width*City::width; ++i) if (!seen[i] && city.tiles[i]==CityTile::Bridge) {
        std::vector<CityCell> cells{{i%City::width,i/City::width}}; seen[i] = true;
        unsigned char height = 0;
        for (std::size_t j = 0; j < cells.size(); ++j) for (auto d : directions) {
            const auto p = add(cells[j],d);
            if (city.tile(p)==CityTile::Bridge && !seen[City::index(p)]) { seen[City::index(p)] = true; cells.push_back(p); }
            else if (city.tile(p)==CityTile::Road) {
                const CityCell bridge = cells[j];
                const CityCell a{std::max(p.x,bridge.x),std::max(p.z,bridge.z)};
                const CityCell b{a.x+(p.z!=bridge.z),a.z+(p.x!=bridge.x)};
                height = std::max({height,city.elevation[City::corner_index(a)],city.elevation[City::corner_index(b)]});
            }
        }
        for (auto p : cells) for (int z = p.z; z <= p.z+1; ++z) for (int x = p.x; x <= p.x+1; ++x)
            city.elevation[City::corner_index({x,z})] = height;
    }
    constrain_elevation(city,1);
    for (auto& v : city.vehicles) v.position.SetY(city.height(v.position.GetX(),v.position.GetZ()));
    for (auto& t : city.trees) t.base.SetY(city.height(t.base.GetX(),t.base.GetZ()));
}
}
CityCell City::cell(float x, float z) { return {int(std::floor(x / block)) + width / 2, int(std::floor(z / block)) + width / 2}; }
Vec3 City::center(CityCell p, float y) { return {(p.x - width / 2 + .5f) * block, y, (p.z - width / 2 + .5f) * block}; }
std::array<Vec3,4> City::ground_patch(CityCell p) const {
    std::array<Vec3,4> patch;
    const CityCell corners[] = {{p.x,p.z},{p.x,p.z+1},{p.x+1,p.z+1},{p.x+1,p.z}};
    for (int i = 0; i < 4; ++i) {
        const auto v = corners[i];
        const int height = v.x>=0 && v.z>=0 && v.x<corner_width && v.z<corner_width ? elevation[corner_index(v)] : 0;
        patch[i] = Vec3((v.x-width/2)*block,level+height*elevation_step,(v.z-width/2)*block);
    }
    return patch;
}
std::array<std::array<Vec3,3>,2> City::ground_triangles(CityCell p) const {
    const auto v = ground_patch(p);
    // Isolate an exceptional corner as one slope and one flat triangle.
    if (v[1].GetY()==v[3].GetY() && (v[0].GetY()==v[1].GetY() || v[2].GetY()==v[1].GetY()))
        return {{{v[0],v[1],v[3]},{v[1],v[2],v[3]}}};
    return {{{v[0],v[1],v[2]},{v[0],v[2],v[3]}}};
}
unsigned City::ramp_axis(CityCell p) const {
    const auto v = ground_patch(p);
    if (v[0].GetY()==v[1].GetY() && v[0].GetY()==v[2].GetY() && v[0].GetY()==v[3].GetY()) return 0;
    if (v[0].GetY()==v[3].GetY() && v[1].GetY()==v[2].GetY()) return 1;
    if (v[0].GetY()==v[1].GetY() && v[2].GetY()==v[3].GetY()) return 2;
    return 3;
}
float City::tile_height(CityCell p) const {
    const auto v = ground_triangles(p)[0];
    return triangle_height(v,center(p));
}
float City::height(float x, float z) const {
    CityCell p = cell(x,z);
    // Land and bridge decks own their boundaries, including the outside coastline.
    const float u = x/block-std::floor(x/block), v = z/block-std::floor(z/block);
    const int dx = u<.0001f ? -1 : u> .9999f ? 1 : 0, dz = v<.0001f ? -1 : v> .9999f ? 1 : 0;
    if (!land(p) && !road(p)) for (auto offset : {CityCell{dx,0},CityCell{0,dz},CityCell{dx,dz}})
        if (land(add(p,offset)) || road(add(p,offset))) { p = add(p,offset); break; }
    const auto patch = ground_patch(p);
    const Vec3 point(std::clamp(x,patch[0].GetX(),patch[2].GetX()),0,std::clamp(z,patch[0].GetZ(),patch[2].GetZ()));
    for (const auto& t : ground_triangles(p)) {
        bool inside = true;
        for (int j = 0; j < 3; ++j) inside &= (t[(j+1)%3]-t[j]).Cross(point-t[j]).GetY()>=-.001f;
        if (inside) return triangle_height(t,point);
    }
    return tile_height(p);
}
std::vector<std::array<Vec3,3>> City::drape_triangle(const std::array<Vec3,3>& triangle) const {
    Vec3 low = triangle[0], high = low;
    for (auto p : triangle) { low = Vec3::sMin(low,p); high = Vec3::sMax(high,p); }
    const auto a = cell(low.GetX(),low.GetZ()), b = cell(high.GetX()-.00001f,high.GetZ()-.00001f);
    std::vector<std::array<Vec3,3>> result;
    for (int z = std::max(0,a.z); z <= std::min(width-1,b.z); ++z) for (int x = std::max(0,a.x); x <= std::min(width-1,b.x); ++x) {
        for (const auto& t : ground_triangles({x,z})) {
            std::vector<Vec3> polygon(triangle.begin(),triangle.end());
            for (int edge = 0; edge < 3 && !polygon.empty(); ++edge) {
                const auto distance = [&](Vec3 p) { return (t[(edge+1)%3]-t[edge]).Cross(p-t[edge]).GetY(); };
                std::vector<Vec3> clipped; Vec3 previous = polygon.back(); float before = distance(previous);
                for (Vec3 current : polygon) {
                    const float after = distance(current);
                    if ((before>=0)!=(after>=0)) clipped.push_back(previous+(current-previous)*(before/(before-after)));
                    if (after>=0) clipped.push_back(current);
                    previous = current; before = after;
                }
                polygon = std::move(clipped);
            }
            for (auto& p : polygon) p.SetY(triangle_height(t,p));
            for (std::size_t i = 1; i+1 < polygon.size(); ++i)
                if ((polygon[i]-polygon[0]).Cross(polygon[i+1]-polygon[0]).LengthSq()>.00000001f)
                    result.push_back({polygon[0],polygon[i],polygon[i+1]});
        }
    }
    return result;
}
CityVehicle::CityVehicle(CityVehicleKind kind, CityCell cell, int rotation) : kind(kind), position(City::center(cell)), rotation(rotation) {}
Vec3 CityVehicle::half_size() const {
    constexpr float widths[] = {1.1f,5.6f,6.3f,32.4f}, lengths[] = {2.2f,3.5f,9,35.5f};
    if (int(kind) < 0 || int(kind) > 3) return Vec3::sZero();
    return rotation%2 ? Vec3(lengths[int(kind)],0,widths[int(kind)]) : Vec3(widths[int(kind)],0,lengths[int(kind)]);
}
Vec3 CityRoadNode::center() const { return (City::center(first,height)+City::center(last,height))/2; }
Vec3 CityRoadNode::half_size() const { return Vec3((last.x-first.x+1)*City::half_block,0,(last.z-first.z+1)*City::half_block); }
CityRoadPort CityRoadNode::port(int d) const {
    if (ports[d].width>0) return ports[d];
    const Vec3 half = half_size();
    const Vec3 direction((d==1)-(d==3),0,(d==2)-(d==0));
    return {center()+direction*(d%2 ? half.GetX() : half.GetZ()),2*(d%2 ? half.GetZ() : half.GetX())};
}
std::vector<Vec3> CityRoadNode::bend(float fraction,float inset) const {
    const unsigned bends[] = {3,6,12,9};
    const Vec3 directions[] = {{0,0,-1},{1,0,0},{0,0,1},{-1,0,0}};
    for (int i = 0; i < 4; ++i) if (mask == bends[i]) {
        const Vec3 a = directions[i], b = directions[(i+1)%4];
        const auto entry = port(i), exit = port((i+1)%4);
        const Vec3 pivot = i%2 ? Vec3(entry.center.GetX(),height,exit.center.GetZ()) : Vec3(exit.center.GetX(),height,entry.center.GetZ());
        const float radius_b = std::abs((pivot-entry.center).Dot(b))+(fraction-.5f)*entry.width+inset;
        const float radius_a = std::abs((pivot-exit.center).Dot(a))+(fraction-.5f)*exit.width+inset;
        std::vector<Vec3> curve;
        for (int j = 0; j <= 24; ++j) {
            const float angle = j*1.57079633f/24;
            curve.push_back(pivot-b*(radius_b*std::cos(angle))-a*(radius_a*std::sin(angle)));
            curve.back().SetY(entry.center.GetY()+(exit.center.GetY()-entry.center.GetY())*j/24.f);
        }
        return curve;
    }
    return {};
}
std::vector<Vec3> CityRoadNode::path(float fraction,float inset) const {
    if (mask!=5 && mask!=10) return bend(fraction,inset);
    const int d = mask==5 ? 0 : 1;
    const Vec3 across = Vec3((d==1)-(d==3),0,(d==2)-(d==0)).Cross(Vec3::sAxisY());
    const auto a = port(d), b = port(d+2);
    return {a.center+across*((.5f-fraction)*a.width-inset),b.center+across*((.5f-fraction)*b.width-inset)};
}
std::vector<Vec3> CityRoadNode::outline() const {
    std::vector<int> arms;
    for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) arms.push_back(d);
    if (arms.size()<3) return {};
    std::vector<Vec3> points;
    const auto edge = [&](int d,float side) {
        const auto gate = port(d);
        const Vec3 outward((d==1)-(d==3),0,(d==2)-(d==0));
        return gate.center+outward.Cross(Vec3::sAxisY())*(side*(gate.width/2-.7f));
    };
    for (std::size_t i = 0; i < arms.size(); ++i) {
        const int d = arms[i], next = arms[(i+1)%arms.size()];
        const Vec3 left = edge(d,-1), right = edge(d,1), end = edge(next,-1);
        points.push_back(left); points.push_back(right);
        if ((next-d+4)%4==2) {
            const Vec3 out((d==1)-(d==3),0,(d==2)-(d==0));
            if (std::abs((end-right).Dot(out.Cross(Vec3::sAxisY())))>.001f) {
                const float reach = std::min(City::half_block,(end-right).Length()*.33f);
                const Vec3 a = right-out*reach, b = end+out*reach;
                for (int k = 1; k < 8; ++k) {
                    const float t = k/8.f;
                    points.push_back(right*((1-t)*(1-t)*(1-t))+a*(3*t*(1-t)*(1-t))+b*(3*t*t*(1-t))+end*(t*t*t));
                }
            }
            continue;
        }
        if ((next-d+4)%4!=1) continue;
        const Vec3 corner = d%2 ? Vec3(end.GetX(),height,right.GetZ()) : Vec3(right.GetX(),height,end.GetZ());
        const float radius = std::min({2.2f,(right-corner).Length()*.45f,(end-corner).Length()*.45f});
        if (radius<.001f) continue;
        const Vec3 a = corner+(right-corner).Normalized()*radius, b = corner+(end-corner).Normalized()*radius;
        for (int k = 0; k <= 8; ++k) {
            const float t = k/8.f;
            points.push_back(a*((1-t)*(1-t))+corner*(2*t*(1-t))+b*(t*t));
        }
    }
    return points;
}
int City::building_at(CityCell p) const {
    for (std::size_t i = 0; i < buildings.size(); ++i) if (covers(buildings[i], p)) return int(i);
    return -1;
}
int City::vehicle_at(Vec3 p) const {
    for (std::size_t i = 0; i < vehicles.size(); ++i) {
        const Vec3 d = p-vehicles[i].position, half = vehicles[i].half_size();
        if (std::abs(d.GetX()) <= half.GetX() && std::abs(d.GetZ()) <= half.GetZ()) return int(i);
    }
    return -1;
}
bool City::paint_ground(CityCell a, CityCell b, CityGround texture, std::string& error) {
    error.clear();
    if (!contains(a) || !contains(b) || int(texture)>3) return fail(error,"Draw ground inside the map using a valid texture.");
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z)
        for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x)
            if (land({x,z})) ground[index({x,z})] = texture;
    return true;
}
bool City::change_elevation(CityCell a, CityCell b, int direction, std::string& error) {
    error.clear();
    if (!contains(a) || !contains(b) || (direction!=1 && direction!=-1)) return fail(error,"Drag elevation inside the map.");
    int target = -1;
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z) for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x) {
        const CityCell p{x,z}; if (!land(p)) continue;
        int low = max_elevation, high = 0;
        for (int dz = 0; dz <= 1; ++dz) for (int dx = 0; dx <= 1; ++dx) {
            const int height = elevation[corner_index({x+dx,z+dz})];
            low = std::min(low,height); high = std::max(high,height);
        }
        const int value = std::clamp(direction>0 ? low+1 : high-1,0,max_elevation);
        if (target>=0 && value!=target) return fail(error,"Select tiles that rise or lower to the same flat level.");
        target = value;
    }
    City next = *this; bool changed = false;
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z) for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x) {
        if (!land({x,z})) continue;
        for (int dz = 0; dz <= 1; ++dz) for (int dx = 0; dx <= 1; ++dx) {
            auto& height = next.elevation[corner_index({x+dx,z+dz})];
            changed |= target!=height; height = static_cast<unsigned char>(target);
        }
    }
    if (!changed) return fail(error,"Select land between elevation levels 0 and 8.");
    constrain_elevation(next,direction);
    sync_elevation(next);
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z) for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x)
        if (land({x,z})) for (Vec3 corner : next.ground_patch({x,z}))
            if (std::abs(corner.GetY()-level-target*elevation_step)>.001f)
                return fail(error,"A bridge approach prevents this selection staying flat. Adjust both banks together.");
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
bool City::tree_clear(Vec3 p) const {
    if (!std::isfinite(p.GetX()) || !std::isfinite(p.GetZ()) || std::abs(p.GetX()) >= extent || std::abs(p.GetZ()) >= extent) return false;
    // Keep trunks and foliage clear of streets, buildings, the spawn, and aircraft.
    for (float x : {-2.f,2.f}) for (float z : {-2.f,2.f}) {
        const auto c = cell(p.GetX()+x,p.GetZ()+z);
        if (tile(c) != CityTile::Land || building_at(c) >= 0 || (spawn && *spawn == c)) return false;
    }
    for (auto v : vehicles) {
        const Vec3 half = v.half_size(), delta = v.position-p;
        if (std::abs(delta.GetX()) < half.GetX()+2 && std::abs(delta.GetZ()) < half.GetZ()+2) return false;
    }
    return true;
}
void City::clear_trees() {
    trees.erase(std::remove_if(trees.begin(),trees.end(),[&](const Tree& t){return !tree_clear(t.base);}),trees.end());
}
int City::brush_trees(Vec3 point, float radius, int density, bool remove, std::string& error) {
    error.clear();
    if (!std::isfinite(point.GetX()) || !std::isfinite(point.GetZ()) || std::abs(point.GetX()) > extent || std::abs(point.GetZ()) > extent
        || !std::isfinite(radius) || radius < 4 || radius > 96 || density < 1 || density > 3) {
        error = "Keep the tree brush inside the map."; return -1;
    }
    const auto inside = [&](Vec3 p) { const Vec3 d = p-point; return d.GetX()*d.GetX()+d.GetZ()*d.GetZ() <= radius*radius; };
    const int before = int(trees.size());
    if (remove) {
        trees.erase(std::remove_if(trees.begin(),trees.end(),[&](const Tree& t){return inside(t.base);}),trees.end());
        return before-int(trees.size());
    }
    // Four-meter plots keep real tree spacing independent of block size.
    const auto plot = [](Vec3 p) { return int(std::floor((p.GetZ()+1024)/4))*512+int(std::floor((p.GetX()+1024)/4)); };
    std::set<int> occupied;
    for (auto t : trees) occupied.insert(plot(t.base));
    std::uint32_t seed = 0;
    for (unsigned char c : id) seed = seed*31+c;
    const auto hash = [](std::uint32_t n) { n ^= n>>16; n *= 0x7feb352du; n ^= n>>15; n *= 0x846ca68bu; return n^(n>>16); };
    const int threshold[] = {0,25,55,90};
    const int x0 = std::max(0,int(std::floor((point.GetX()-radius+1024)/4))), x1 = std::min(511,int(std::floor((point.GetX()+radius+1024)/4)));
    const int z0 = std::max(0,int(std::floor((point.GetZ()-radius+1024)/4))), z1 = std::min(511,int(std::floor((point.GetZ()+radius+1024)/4)));
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const int key = z*512+x; const auto noise = hash(seed+std::uint32_t(key));
        if (occupied.count(key) || int(noise%100) >= threshold[density]) continue;
        const auto random = [&](int shift) { return float((noise>>shift)&255)/255; };
        Vec3 p(x*4-1022+(random(8)-.5f)*1.5f,level,z*4-1022+(random(16)-.5f)*1.5f);
        if (!inside(p) || !tree_clear(p)) continue;
        p.SetY(height(p.GetX(),p.GetZ()));
        if (trees.size() >= 8192) { error = "Tree limit reached (8192)."; return int(trees.size())-before; }
        trees.push_back({p,4+random(24)*4,random(0)*6.2831853f});
    }
    return int(trees.size())-before;
}
unsigned City::road_mask(CityCell p) const {
    if (!road(p)) return 0;
    unsigned mask = 0;
    for (int i = 0; i < 4; ++i) if (road(add(p,directions[i]))) mask |= 1u<<i;
    return mask;
}
unsigned City::road_axis(CityCell p) const {
    if (!road(p)) return 0;
    if (road_axes[index(p)]) return road_axes[index(p)];
    unsigned axis = 0;
    for (int d = 0; d < 4; ++d) if (road(add(p,directions[d]))) axis |= d%2 ? 2 : 1;
    return axis;
}

std::vector<CityRoadNode> City::road_network() const {
    std::array<int,width*width> owner; owner.fill(-1);
    std::vector<CityRoadNode> nodes;
    for (int i = 0; i < width*width; ++i) {
        const CityCell p{i%width,i/width};
        if (!road(p)) continue;
        owner[i] = int(nodes.size()); nodes.push_back({p,p,{}}); nodes.back().height = tile_height(p);
    }
    for (int i = 0; i < width*width; ++i) if (owner[i]>=0) {
        const CityCell p{i%width,i/width};
        for (int d = 0; d < 4; ++d) if (road_mask(p)&(1u<<d)) {
            auto& node = nodes[owner[i]];
            node.mask |= 1u<<d;
            node.ports[d] = {center(p)+Vec3(directions[d].x,0,directions[d].z)*half_block,block};
            auto& gate = node.ports[d].center; gate.SetY(height(gate.GetX(),gate.GetZ()));
            node.edges.push_back(owner[index(add(p,directions[d]))]);
        }
    }
    // Like SC4's adjacent-piece rules, replace branch-free opposite bends as one diagonal.
    std::vector<bool> removed(nodes.size());
    const auto turn = [](unsigned mask) { return mask==3 || mask==6 || mask==12 || mask==9; };
    for (int i = 0; i < int(nodes.size()); ++i) if (!removed[i] && turn(nodes[i].mask) && nodes[i].edges.size()==2) {
        auto candidates = nodes[i].edges;
        std::stable_sort(candidates.begin(),candidates.end(),[&](int a,int b) { return nodes[a].edges.size()>nodes[b].edges.size(); });
        for (int j : candidates) {
            const bool open_end = nodes[j].edges.size()==1;
            if (removed[j] || (!open_end && (j<=i || !turn(nodes[j].mask) || nodes[j].edges.size()!=2))) continue;
            auto& a = nodes[i]; const auto& b = nodes[j];
            int shared = -1;
            for (int d = 0; d < 4; ++d) if ((a.mask&(1u<<d)) && (b.mask&(1u<<((d+2)%4)))
                && (a.port(d).center-b.port((d+2)%4).center).LengthSq()<.001f) shared = d;
            if (shared<0) continue;
            const unsigned mask_a = a.mask&~(1u<<shared);
            int entry = 0; while (!(mask_a&(1u<<entry))) ++entry;
            const unsigned mask_b = open_end ? 1u<<((entry+2)%4) : b.mask&~(1u<<((shared+2)%4));
            if (mask_b!=(1u<<((entry+2)%4))) continue;
            const CityCell first{std::min(a.first.x,b.first.x),std::min(a.first.z,b.first.z)}, last{std::max(a.last.x,b.last.x),std::max(a.last.z,b.last.z)};
            const auto area = [](const CityRoadNode& n) { return (n.last.x-n.first.x+1)*(n.last.z-n.first.z+1); };
            if ((last.x-first.x+1)*(last.z-first.z+1)!=area(a)+area(b)) continue;
            const auto gate_a = a.port(entry), gate_b = b.port((entry+2)%4);
            a.first = first; a.last = last; a.mask = mask_a|mask_b; a.ports = {};
            a.ports[entry] = gate_a; a.ports[(entry+2)%4] = gate_b;
            a.edges.erase(std::remove(a.edges.begin(),a.edges.end(),j),a.edges.end());
            for (int edge : b.edges) if (edge!=i) a.edges.push_back(edge);
            for (auto& node : nodes) for (auto& edge : node.edges) if (edge==j) edge = i;
            removed[j] = true; break;
        }
    }
    std::vector<int> remap(nodes.size(),-1); std::vector<CityRoadNode> merged;
    for (int i = 0; i < int(nodes.size()); ++i) if (!removed[i]) { remap[i] = int(merged.size()); merged.push_back(std::move(nodes[i])); }
    for (auto& node : merged) for (auto& edge : node.edges) edge = remap[edge];
    for (auto& node : merged) { const auto c = node.center(); node.height = height(c.GetX(),c.GetZ()); }
    return merged;
}
std::vector<Vec3> City::road_bend(CityCell p,float radius) const {
    return CityRoadNode{p,p,{},road_mask(p)}.bend(0,radius);
}
std::vector<CityCell> City::neighbors(CityCell p) const {
    std::vector<CityCell> result;
    const unsigned mask = road_mask(p);
    for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) result.push_back(add(p,directions[d]));
    return result;
}
int City::land_count() const { return int(std::count_if(tiles.begin(), tiles.end(), [](auto t) { return t == CityTile::Land || t == CityTile::Road; })); }
bool City::connected() const {
    auto first = std::find_if(tiles.begin(), tiles.end(), [](auto t) { return t != CityTile::Water; });
    if (first == tiles.end()) return true;
    std::array<bool, width * width> seen{};
    const int start = int(first - tiles.begin());
    std::vector<int> queue{start}; seen[start] = true;
    for (std::size_t i = 0; i < queue.size(); ++i) for (auto d : directions) {
        auto p = add({queue[i] % width, queue[i] / width}, d);
        if (contains(p) && tile(p) != CityTile::Water && !seen[index(p)]) { seen[index(p)] = true; queue.push_back(index(p)); }
    }
    return int(queue.size()) == std::count_if(tiles.begin(),tiles.end(),[](auto t){return t!=CityTile::Water;});
}
bool City::add_land(CityCell a, CityCell b, std::string& error) {
    error.clear();
    if (!contains(a) || !contains(b)) return fail(error, "Keep land inside the map.");
    City next = *this;
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z)
        for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x)
            if (next.tiles[index({x,z})] == CityTile::Water) next.tiles[index({x,z})] = CityTile::Land;
            else if (next.tiles[index({x,z})] == CityTile::Bridge) next.tiles[index({x,z})] = CityTile::Road;
    sync_elevation(next);
    if (!next.validate(error)) return false;
    *this = std::move(next);
    return true;
}
std::vector<CityCell> City::road_stroke(CityCell a, CityCell b, bool z_first, bool diagonal) {
    std::vector<CityCell> result{a};
    if (!contains(a) || !contains(b)) return {};
    const auto along = [&](bool z) {
        int& value = z ? a.z : a.x; const int target = z ? b.z : b.x;
        while (value != target) { value += target > value ? 1 : -1; result.push_back(a); }
    };
    if (diagonal) {
        // Alternating edge steps retain the grid footprint; road_network joins them into diagonal pavement.
        while (a.x!=b.x && a.z!=b.z) {
            for (bool z : {z_first,!z_first}) {
                int& value = z ? a.z : a.x; const int target = z ? b.z : b.x;
                value += target>value ? 1 : -1; result.push_back(a);
            }
        }
    }
    along(z_first); along(!z_first); return result;
}
bool City::add_road(const std::vector<CityCell>& cells, std::string& error) {
    error.clear();
    if (cells.empty()) return fail(error, "Drag a road inside the map.");
    for (auto p : cells) if (!contains(p) || building_at(p) >= 0)
        return fail(error, "Keep roads inside the map. Bulldoze buildings first.");
    for (std::size_t i = 1; i < cells.size(); ++i)
        if (std::abs(cells[i].x - cells[i-1].x) + std::abs(cells[i].z - cells[i-1].z) > 1)
            return fail(error, "Road blocks must connect edge to edge.");
    City next = *this;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto p = cells[i];
        next.tiles[index(p)] = land(p) ? CityTile::Road : CityTile::Bridge;
        unsigned axis = 0;
        if (i>0 && cells[i-1]!=p) axis |= cells[i-1].x==p.x ? 1 : 2;
        if (i+1<cells.size() && cells[i+1]!=p) axis |= cells[i+1].x==p.x ? 1 : 2;
        if (axis) next.road_axes[index(p)] |= axis;
    }
    next.clear_trees(); sync_elevation(next);
    if (!next.validate(error)) return false;
    *this = std::move(next);
    return true;
}
bool City::add_building(CityBuilding b, std::string& error, int replace) {
    error.clear();
    if (b.size < 1 || b.size > 8 || b.height < 4 || b.height > 120 || !contains(b.cell)
        || !contains({b.cell.x+b.size-1,b.cell.z+b.size-1})) return fail(error, "Use a 1-8 block square and a height of 4-120 m.");
    for (int z = b.cell.z; z < b.cell.z+b.size; ++z) for (int x = b.cell.x; x < b.cell.x+b.size; ++x) {
        CityCell p{x,z}; const int existing = building_at(p);
        if (tile(p) != CityTile::Land || (existing >= 0 && existing != replace) || (spawn && *spawn == p))
            return fail(error, "Buildings need an empty square of land.");
        for (Vec3 corner : ground_patch(p))
            if (std::abs(corner.GetY()-tile_height(b.cell))>.001f) return fail(error,"Buildings need flat ground under the whole footprint.");
    }
    City check = *this;
    if (replace >= 0 && replace < int(buildings.size())) check.buildings[replace] = b;
    else if (buildings.size() < 4096) check.buildings.push_back(b);
    else return fail(error, "Building limit reached.");
    for (std::size_t i = 0; i < vehicles.size(); ++i) if (!check.add_vehicle(vehicles[i],error,int(i))) return false;
    buildings = std::move(check.buildings);
    clear_trees();
    return true;
}
bool City::add_vehicle(CityVehicle v, std::string& error, int replace) {
    error.clear();
    if (int(v.kind) < 0 || int(v.kind) > 3 || v.rotation < 0 || v.rotation > 3
        || !std::isfinite(v.position.GetX()) || !std::isfinite(v.position.GetY()) || !std::isfinite(v.position.GetZ())
        || std::abs(v.position.GetY()-height(v.position.GetX(),v.position.GetZ()))>.001f || std::abs(v.position.GetX())>=extent || std::abs(v.position.GetZ())>=extent)
        return fail(error, "Place vehicles on clear land or roads, away from the spawn.");
    // Validate the whole rotated footprint, especially the 747's wings.
    const Vec3 c = v.position, half = v.half_size(); const float rx = half.GetX(), rz = half.GetZ();
    auto a = cell(c.GetX()-rx, c.GetZ()-rz), b = cell(c.GetX()+rx, c.GetZ()+rz);
    for (int z = a.z; z <= b.z; ++z) for (int x = a.x; x <= b.x; ++x)
        if (tile({x,z}) == CityTile::Water || building_at({x,z}) >= 0)
            return fail(error, "The whole vehicle needs clear land beneath it.");
    for (float x : {-rx,rx}) for (float z : {-rz,rz})
        if (std::abs(height(c.GetX()+x,c.GetZ()+z)-c.GetY())>.05f) return fail(error,"Park vehicles on level ground.");
    if (spawn) {
        const Vec3 d = center(*spawn)-c;
        if (std::abs(d.GetX())<rx+1 && std::abs(d.GetZ())<rz+1) return fail(error,"Leave space around the player spawn.");
    }
    for (std::size_t i = 0; i < vehicles.size(); ++i) if (int(i) != replace) {
        const auto& other = vehicles[i];
        const Vec3 other_half = other.half_size(), delta = other.position - c;
        if (std::abs(delta.GetX()) < rx+other_half.GetX()+1 && std::abs(delta.GetZ()) < rz+other_half.GetZ()+1)
            return fail(error, "Leave space between vehicles.");
    }
    if (replace >= 0 && replace < int(vehicles.size())) vehicles[replace] = v;
    else if (vehicles.size() < 512) vehicles.push_back(v);
    else return fail(error, "Vehicle limit reached.");
    clear_trees();
    return true;
}
bool City::set_spawn(CityCell p, std::string& error) {
    error.clear();
    if (!contains(p) || tile(p) == CityTile::Water || building_at(p) >= 0)
        return fail(error, "Set the spawn on clear land.");
    City check = *this; check.spawn = p;
    for (std::size_t i = 0; i < vehicles.size(); ++i) if (!check.add_vehicle(vehicles[i],error,int(i))) return false;
    spawn = p; clear_trees(); return true;
}
bool City::erase(CityCell p, std::string& error) {
    error.clear();
    if (!contains(p)) return fail(error, "Keep edits inside the map.");
    int object = building_at(p);
    if (object >= 0) { buildings.erase(buildings.begin()+object); return true; }
    if (spawn && *spawn == p) { spawn.reset(); return true; }
    if (road(p) || tile(p) == CityTile::Land) {
        const City before = *this; const auto previous = tile(p);
        tiles[index(p)] = previous == CityTile::Road ? CityTile::Land : CityTile::Water;
        road_axes[index(p)] = 0;
        if (tile(p) == CityTile::Water) ground[index(p)] = CityGround::Soil;
        clear_trees();
        sync_elevation(*this);
        if (!validate(error)) { *this = before; return false; }
    }
    return true;
}
bool City::erase(Vec3 p, std::string& error) {
    const int selected = vehicle_at(p);
    if (selected>=0) { vehicles.erase(vehicles.begin()+selected); error.clear(); return true; }
    return erase(cell(p.GetX(),p.GetZ()),error);
}
bool City::validate(std::string& error) const {
    error.clear();
    if (!valid_id(id) || !valid_name(name)) return fail(error, "Invalid city name or identifier.");
    if (start_minutes < 0 || start_minutes >= 1440 || trees.size() > 8192) return fail(error,"Invalid starting time or tree count.");
    for (int i = 0; i < width*width; ++i) {
        const CityCell p{i%width,i/width};
        if (int(tiles[i]) > 3 || int(ground[i]) > 3
            || road_axes[i]>3 || (road_axes[i] && !road(p))) return fail(error, "Invalid terrain data.");
        if (road(p) && !road_fits_ground(*this,p)) return fail(error,ramp_axis(p)==3
            ? "Roads need flat tiles or straight ramps; corner ramps cannot carry roads."
            : "Roads must run up or down a ramp, without sideways crossings or turns.");
    }
    for (int z = 0; z < corner_width; ++z) for (int x = 0; x < corner_width; ++x) {
        const int height = elevation[corner_index({x,z})];
        if (height>max_elevation) return fail(error,"Invalid corner elevation.");
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            const CityCell q{x+dx,z+dz};
            if (q.x>=0 && q.z>=0 && q.x<corner_width && q.z<corner_width
                && std::abs(height-int(elevation[corner_index(q)]))>1) return fail(error,"Adjacent terrain corners must be at most one elevation step apart.");
        }
    }
    City check = *this; check.buildings.clear(); check.vehicles.clear(); check.trees.clear(); check.spawn.reset();
    for (auto b : buildings) if (!check.add_building(b,error)) return false;
    for (auto v : vehicles) if (!check.add_vehicle(v,error)) return false;
    if (spawn && !check.set_spawn(*spawn,error)) return false;
    std::set<int> plots;
    for (auto t : trees) {
        if (!tree_clear(t.base) || !std::isfinite(t.base.GetY()) || std::abs(t.base.GetY()-height(t.base.GetX(),t.base.GetZ())) > .001f
            || !std::isfinite(t.height) || t.height < 4 || t.height > 8 || !std::isfinite(t.yaw) || t.yaw < 0 || t.yaw > 6.284f)
            return fail(error,"Invalid tree placement.");
        const int plot = int(std::floor((t.base.GetZ()+1024)/4))*512+int(std::floor((t.base.GetX()+1024)/4));
        if (!plots.insert(plot).second) return fail(error,"Duplicate tree placement.");
    }
    return true;
}
std::vector<std::vector<Vec3>> City::traffic_routes() const {
    const auto nodes = road_network();
    std::vector<std::vector<int>> edges;
    for (const auto& n : nodes) edges.push_back(n.edges);
    std::vector<std::vector<Vec3>> routes;
    for (int start = 0; start < int(nodes.size()); ++start) if (!edges[start].empty()) {
        std::vector<int> stack{start}, tour;
        while (!stack.empty()) {
            int node = stack.back();
            if (edges[node].empty()) { tour.push_back(node); stack.pop_back(); }
            else { const int next = edges[node].back(); edges[node].pop_back(); stack.push_back(next); }
        }
        std::reverse(tour.begin(),tour.end()); tour.pop_back();
        std::vector<Vec3> route;
        for (std::size_t i = 0; i < tour.size(); ++i) {
            const auto& node = nodes[tour[i]];
            const auto direction = [](int d) { return Vec3((d==1)-(d==3),0,(d==2)-(d==0)); };
            const auto gate_for = [&](int neighbor) {
                for (int d = 0; d < 4; ++d) if ((node.mask&(1u<<d)) && (nodes[neighbor].mask&(1u<<((d+2)%4)))) {
                    const auto a = node.port(d), b = nodes[neighbor].port((d+2)%4);
                    const Vec3 out = direction(d), across = out.Cross(Vec3::sAxisY());
                    if (std::abs((a.center-b.center).Dot(out))>.001f) continue;
                    const float low = std::max(a.center.Dot(across)-a.width/2,b.center.Dot(across)-b.width/2);
                    const float high = std::min(a.center.Dot(across)+a.width/2,b.center.Dot(across)+b.width/2);
                    if (high>low+.001f) return std::make_pair(d,CityRoadPort{a.center+across*((low+high)/2-a.center.Dot(across)),high-low});
                }
                const Vec3 delta = nodes[neighbor].center()-node.center();
                const int d = std::abs(delta.GetX())>std::abs(delta.GetZ()) ? (delta.GetX()>0 ? 1 : 3) : (delta.GetZ()>0 ? 2 : 0);
                return std::make_pair(d,node.port(d));
            };
            const auto entry = gate_for(tour[(i+tour.size()-1)%tour.size()]), exit = gate_for(tour[(i+1)%tour.size()]);
            const Vec3 c = node.center(), in = -direction(entry.first), out = direction(exit.first);
            const Vec3 r = in.Cross(Vec3::sAxisY()), s = out.Cross(Vec3::sAxisY());
            const Vec3 half = nodes[tour[i]].half_size();
            const auto along = [&](Vec3 d) { return std::abs(d.GetX())*half.GetX()+std::abs(d.GetZ())*half.GetZ(); };
            const float lane_in = entry.second.width/2-2.38f, lane_out = exit.second.width/2-2.38f;
            const Vec3 a = entry.second.center+r*lane_in, b = exit.second.center+s*lane_out;
            auto curve = node.path(0,2.38f);
            if (in.Dot(out)>-.9f && !curve.empty()) {
                if (std::min((curve.front()-a).LengthSq(),(curve.back()-a).LengthSq())>.001f) curve = node.path(1,-2.38f);
                if ((curve.back()-a).LengthSq()<(curve.front()-a).LengthSq()) std::reverse(curve.begin(),curve.end());
                route.insert(route.end(),curve.begin(),curve.end());
            } else if (in.Dot(out) > .9f) { route.push_back(a); route.push_back(b); }
            else if (in.Dot(out) < -.9f) {
                const Vec3 cap = c-in*std::max(1.4f,lane_in-along(in)+1.7f);
                route.push_back(a);
                for (int k = 0; k <= 12; ++k) {
                    const float angle = k*3.14159265f/12;
                    route.push_back(cap+r*(lane_in*std::cos(angle))+in*(lane_in*std::sin(angle)));
                }
                route.push_back(b);
            } else {
                const float reach = std::min(2.8f,(b-a).Length()*.33f);
                const Vec3 control_a = a+in*reach, control_b = b-out*reach;
                for (int k = 0; k <= 8; ++k) {
                    const float t = k/8.f;
                    route.push_back(a*((1-t)*(1-t)*(1-t))+control_a*(3*t*(1-t)*(1-t))+control_b*(3*t*t*(1-t))+b*(t*t*t));
                }
            }
        }
        route.erase(std::unique(route.begin(),route.end(),[](Vec3 a,Vec3 b){return (a-b).LengthSq()<.0001f;}),route.end());
        if (route.size()>1 && (route.front()-route.back()).LengthSq()<.0001f) route.pop_back();
        if (route.size()>1) {
            if (std::any_of(elevation.begin(),elevation.end(),[](auto h){return h>0;})) {
                std::vector<Vec3> lifted;
                for (std::size_t i = 0; i < route.size(); ++i) {
                    const Vec3 delta = route[(i+1)%route.size()]-route[i];
                    const int steps = std::max(1,int(std::ceil(delta.Length()/(block/4))));
                    for (int j = 0; j < steps; ++j) {
                        Vec3 p = route[i]+delta*(float(j)/steps); p.SetY(height(p.GetX(),p.GetZ())); lifted.push_back(p);
                    }
                }
                route = std::move(lifted);
            }
            routes.push_back(std::move(route));
        }
    }
    return routes;
}
City City::create(std::string name) {
    City city; city.name = std::move(name);
    std::mt19937_64 random(std::random_device{}()); std::ostringstream id;
    id << std::hex << std::setw(16) << std::setfill('0') << random(); city.id = id.str(); return city;
}
bool City::save(const std::filesystem::path& directory, std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) return fail(error,"Cannot create the cities folder.");
    const auto path = directory/(id+".city"); auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return fail(error,"Cannot write this city. Your previous save is intact.");
    file << "FORZA_CITY 6\n" << id << '\n' << std::quoted(name) << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (auto t : tiles) file << char('0'+int(t));
    file << '\n' << buildings.size() << '\n';
    for (auto b : buildings) file << b.cell.x << ' ' << b.cell.z << ' ' << b.size << ' ' << b.height << '\n';
    file << vehicles.size() << '\n';
    for (auto v : vehicles) file << int(v.kind) << ' ' << v.position.GetX() << ' ' << v.position.GetZ() << ' ' << v.rotation << '\n';
    file << int(spawn.has_value()) << ' ' << (spawn ? spawn->x : 0) << ' ' << (spawn ? spawn->z : 0) << '\n';
    file << start_minutes << '\n' << trees.size() << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (auto t : trees) file << t.base.GetX() << ' ' << t.base.GetZ() << ' ' << t.height << ' ' << t.yaw << '\n';
    for (auto axis : road_axes) file << char('0'+axis);
    file << '\n';
    for (auto texture : ground) file << char('0'+int(texture));
    file << '\n';
    for (auto height : elevation) file << char('0'+height);
    file << '\n';
    file.close();
    if (!file) return fail(error,"Cannot finish saving. Your previous save is intact.");
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary,path,ec); const bool replaced = !ec;
#endif
    if (!replaced) return fail(error,"Cannot replace the city save. Your previous save is intact.");
    error.clear(); return true;
}
bool City::load(const std::filesystem::path& path, City& city, std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path,ec) > 1024*1024 || ec) return fail(error,"City save is missing or too large.");
    std::ifstream file(path); City read; std::string magic, terrain; int version = 0;
    if (!(file >> magic >> version >> read.id >> std::quoted(read.name) >> terrain) || magic != "FORZA_CITY" || version<1 || version>6
        || terrain.size() != read.tiles.size()) return fail(error,"Invalid or unsupported city save.");
    for (std::size_t i = 0; i < terrain.size(); ++i) {
        if (terrain[i] < '0' || terrain[i] > (version>=3 ? '3' : '2')) return fail(error,"Invalid city terrain.");
        read.tiles[i] = CityTile(terrain[i]-'0');
    }
    int count = 0;
    if (!(file >> count) || count < 0 || count > 4096) return fail(error,"Invalid building count.");
    for (int i = 0; i < count; ++i) { CityBuilding b; if (!(file >> b.cell.x >> b.cell.z >> b.size >> b.height)) return fail(error,"Invalid building data."); read.buildings.push_back(b); }
    if (!(file >> count) || count < 0 || count > 512) return fail(error,"Invalid vehicle count.");
    for (int i = 0; i < count; ++i) {
        CityVehicle v; int kind;
        if (version>=3) {
            float x,z;
            if (!(file >> kind >> x >> z >> v.rotation)) return fail(error,"Invalid vehicle data.");
            v.position = Vec3(x,level,z);
        } else {
            CityCell p;
            if (!(file >> kind >> p.x >> p.z >> v.rotation) || !contains(p)) return fail(error,"Invalid vehicle data.");
            v.position = center(p);
        }
        if (kind<0 || kind>3) return fail(error,"Invalid vehicle data.");
        v.kind = CityVehicleKind(kind); read.vehicles.push_back(v);
    }
    CityCell spawn; if (!(file >> count >> spawn.x >> spawn.z) || count<0 || count>1) return fail(error,"Invalid spawn data.");
    if (count) read.spawn = spawn;
    if (version >= 2) {
        if (!(file >> read.start_minutes >> count) || count < 0 || count > 8192) return fail(error,"Invalid starting time or tree count.");
        for (int i = 0; i < count; ++i) {
            float x,z,height,yaw;
            if (!(file >> x >> z >> height >> yaw)) return fail(error,"Invalid tree data.");
            if (version<3) { x *= .7f; z *= .7f; }
            read.trees.push_back({Vec3(x,level,z),height,yaw});
        }
    }
    if (version>=3) {
        std::string axes;
        if (!(file>>axes) || axes.size()!=read.road_axes.size()) return fail(error,"Invalid road directions.");
        for (std::size_t i = 0; i < axes.size(); ++i) {
            if (axes[i]<'0' || axes[i]>'3') return fail(error,"Invalid road directions.");
            read.road_axes[i] = axes[i]-'0';
        }
    } else {
        auto axes = read.road_axes;
        for (int i = 0; i < width*width; ++i) axes[i] = static_cast<unsigned char>(read.road_axis({i%width,i/width}));
        read.road_axes = axes;
        // Old coordinates shrink with their land; discard foliage that now overlaps structures or another four-meter plot.
        read.clear_trees(); std::set<int> plots;
        read.trees.erase(std::remove_if(read.trees.begin(),read.trees.end(),[&](const Tree& t) {
            const int plot = int(std::floor((t.base.GetZ()+1024)/4))*512+int(std::floor((t.base.GetX()+1024)/4));
            return !plots.insert(plot).second;
        }),read.trees.end());
    }
    if (version>=4) {
        std::string textures;
        if (!(file>>textures) || textures.size()!=read.ground.size()) return fail(error,"Invalid ground textures.");
        for (std::size_t i = 0; i < textures.size(); ++i) {
            if (textures[i]<'0' || textures[i]>'3') return fail(error,"Invalid ground textures.");
            read.ground[i] = CityGround(textures[i]-'0');
        }
    }
    if (version>=5) {
        std::string heights;
        if (!(file>>heights) || heights.size()!=(version==5 ? read.tiles.size() : read.elevation.size())) return fail(error,"Invalid ground elevation.");
        for (std::size_t i = 0; i < heights.size(); ++i) {
            if (heights[i]<'0' || heights[i]>'0'+max_elevation) return fail(error,"Invalid ground elevation.");
            if (version==6) read.elevation[i] = heights[i]-'0';
            else {
                if (read.tiles[i]==CityTile::Water && heights[i]!='0') return fail(error,"Invalid water elevation.");
                const CityCell p{int(i)%width,int(i)/width};
                for (int z = p.z; z <= p.z+1; ++z) for (int x = p.x; x <= p.x+1; ++x)
                    read.elevation[corner_index({x,z})] = std::max(read.elevation[corner_index({x,z})],static_cast<unsigned char>(heights[i]-'0'));
            }
        }
        if (version==5) {
            constrain_elevation(read,1);
            // Keep legacy roads in place by extending terraces only where their old slopes are incompatible.
            // ponytail: rescan the fixed 128x128 grid; queue affected roads if larger maps need it.
            bool changed;
            do {
                changed = false;
                for (int z = 0; z < width; ++z) for (int x = 0; x < width; ++x) {
                    if (!read.road({x,z}) || road_fits_ground(read,{x,z})) continue;
                    unsigned char height = 0;
                    for (int dz = 0; dz <= 1; ++dz) for (int dx = 0; dx <= 1; ++dx)
                        height = std::max(height,read.elevation[corner_index({x+dx,z+dz})]);
                    for (int dz = 0; dz <= 1; ++dz) for (int dx = 0; dx <= 1; ++dx) {
                        auto& corner = read.elevation[corner_index({x+dx,z+dz})];
                        changed |= corner!=height; corner = height;
                    }
                }
                if (changed) constrain_elevation(read,1);
            } while (changed);
        }
    }
    for (auto& v : read.vehicles) {
        if (!std::isfinite(v.position.GetX()) || !std::isfinite(v.position.GetZ())
            || std::abs(v.position.GetX())>=extent || std::abs(v.position.GetZ())>=extent) return fail(error,"Invalid vehicle position.");
        v.position.SetY(read.height(v.position.GetX(),v.position.GetZ()));
    }
    for (auto& t : read.trees) {
        if (!std::isfinite(t.base.GetX()) || !std::isfinite(t.base.GetZ())
            || std::abs(t.base.GetX())>=extent || std::abs(t.base.GetZ())>=extent) return fail(error,"Invalid tree position.");
        t.base.SetY(read.height(t.base.GetX(),t.base.GetZ()));
    }
    std::string extra; if (file >> extra) return fail(error,"Unexpected city data.");
    if (path.stem().string() != read.id) return fail(error,"City save identifier does not match its filename.");
    if (!read.validate(error)) {
        if (version==5) error = "Older terrain needs adjustment for flat tiles and aligned ramps: "+error;
        return false;
    }
    city = std::move(read); error.clear(); return true;
}
} // namespace forza
