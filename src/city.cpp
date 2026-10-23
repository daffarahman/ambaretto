#include "city.hpp"
#include "race.hpp"
#include "environment.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
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

namespace ambaretto {
namespace {
constexpr const char* ground_files[] = {"soil.png","grass.png","beach-sand.png","asphalt.png"};
float coast_height(const City& city,float x,float z) {
    const auto cell = City::cell(x,z);
    if (city.land(cell)) return city.height(x,z);
    const auto owner = city.shore_cell(Vec3(x,0,z));
    if (!owner) return -8;
    const float left = (owner->x-City::width/2)*City::block, top = (owner->z-City::width/2)*City::block;
    const float dx = std::max({left-x,0.f,x-left-City::block}), dz = std::max({top-z,0.f,z-top-City::block});
    const float distance = std::sqrt(dx*dx+dz*dz);
    const float shore_height = city.height(std::clamp(x,left+.001f,left+City::block-.001f),
        std::clamp(z,top+.001f,top+City::block-.001f));
    const auto smooth = [](float t) { return t*t*(3-2*t); };
    // The shore reaches shallow water, then tapers to the seabed.
    return distance <= 10 ? shore_height-(shore_height+1)*smooth(distance/10) : -1-7*smooth((distance-10)/22);
}
}
Environment::Environment(const City& city) : city_(std::make_shared<City>(city)) {
    std::string error;
    if (!city.validate(error)) throw std::runtime_error(error);
    if (city.freeform) { build_freeform_city(city); return; }
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
            const auto owner = city.shore_cell((a+b+c+d)/4);
            Surface surface = Surface::Sand;
            if (owner) {
                const int tile = City::index(*owner);
                const auto custom = city.ground_textures.find(tile);
                if (city.ground[tile]!=CityGround::Soil || (custom!=city.ground_textures.end() && custom->second=="soil.png")) {
                    const Surface surfaces[] = {Surface::Soil,Surface::Grass,Surface::Sand,Surface::Road};
                    surface = surfaces[int(city.ground[tile])];
                }
            }
            quad(a,b,c,d,surface);
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
    for (const auto& b : city.buildings) {
        Vec3 size = b.shape().size;
        if (b.rotation%2) size = Vec3(size.GetZ(),size.GetY(),size.GetX());
        const Vec3 center = b.base(city.tile_height(b.cell))+Vec3(0,size.GetY()/2,0);
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
std::uint32_t hash32(std::uint32_t n) {
    n ^= n>>16; n *= 0x7feb352du; n ^= n>>15; n *= 0x846ca68bu; return n^(n>>16);
}
float perlin(float x,float z,std::uint32_t seed) {
    const int ix = int(std::floor(x)), iz = int(std::floor(z)); const float dx = x-ix, dz = z-iz;
    const auto fade = [](float t){return t*t*t*(t*(t*6-15)+10);};
    const auto mix = [](float a,float b,float t){return a+(b-a)*t;};
    const auto gradient = [&](int gx,int gz,float px,float pz) {
        constexpr float directions[][2] = {{1,0},{-1,0},{0,1},{0,-1},{.70710678f,.70710678f},
            {-.70710678f,.70710678f},{.70710678f,-.70710678f},{-.70710678f,-.70710678f}};
        const auto& d = directions[hash32(seed ^ std::uint32_t(gx)*0x9e3779b9u ^ std::uint32_t(gz)*0x85ebca6bu)&7];
        return d[0]*px+d[1]*pz;
    };
    return mix(mix(gradient(ix,iz,dx,dz),gradient(ix+1,iz,dx-1,dz),fade(dx)),
        mix(gradient(ix,iz+1,dx,dz-1),gradient(ix+1,iz+1,dx-1,dz-1),fade(dx)),fade(dz));
}
bool covers(const CityBuilding& b, CityCell p) {
    const auto footprint = b.footprint();
    return p.x >= b.cell.x && p.z >= b.cell.z && p.x < b.cell.x + footprint.x && p.z < b.cell.z + footprint.z;
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
    if (city.spawn) city.spawn->SetY(city.height(city.spawn->GetX(),city.spawn->GetZ()));
}
}
CityCell City::cell(float x, float z) { return {int(std::floor(x / block)) + width / 2, int(std::floor(z / block)) + width / 2}; }
Vec3 City::center(CityCell p, float y) { return {(p.x - width / 2 + .5f) * block, y, (p.z - width / 2 + .5f) * block}; }
Vec3 CityBuilding::base(float ground) const {
    if (position) return *position;
    const auto cells = footprint();
    return City::center(cell,ground)+Vec3((cells.x-1)*City::half_block,0,(cells.z-1)*City::half_block);
}
namespace {
Vec3 rotate_building_point(Vec3 p,int rotation) {
    switch (rotation) {
        case 1: return Vec3(p.GetZ(),p.GetY(),-p.GetX());
        case 2: return Vec3(-p.GetX(),p.GetY(),-p.GetZ());
        case 3: return Vec3(-p.GetZ(),p.GetY(),p.GetX());
        default: return p;
    }
}
}
std::vector<Vec3> CityBuilding::points(float ground) const {
    auto result = shape().points(); const auto origin = base(ground);
    for (auto& p : result) p = origin+Quat::sRotation(Vec3::sAxisY(),yaw*JPH::JPH_PI/180)*rotate_building_point(p,rotation);
    return result;
}
std::vector<BuildingTriangle> CityBuilding::triangles(float ground) const {
    auto result = shape().baked().triangles(); const auto origin = base(ground);
    for (auto& t : result) for (auto& p : t.points) p = origin+Quat::sRotation(Vec3::sAxisY(),yaw*JPH::JPH_PI/180)*rotate_building_point(p,rotation);
    return result;
}
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
    if (freeform) return height(center(p).GetX(),center(p).GetZ());
    const auto v = ground_triangles(p)[0];
    return triangle_height(v,center(p));
}
float City::height(float x, float z) const {
    if (freeform) return road_at(Vec3(x,level,z),road_style.sidewalks ? 1.3f : 0) ? level : terrain_height(x,z);
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
    if (kind==CityVehicleKind::Car && car) {
        const float x = std::max(car->width/2+std::abs(car->offset[0]),car->tuning.track_width/2+car->tuning.wheel_radius);
        const float z = std::max(car->length/2+std::abs(car->offset[2]),car->tuning.wheelbase/2+car->tuning.wheel_radius);
        return rotation%2 ? Vec3(z,0,x) : Vec3(x,0,z);
    }
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
std::vector<Vec3> CityRoadNode::outline(float inset) const {
    std::vector<int> arms;
    for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) arms.push_back(d);
    if (arms.size()<3) return {};
    std::vector<Vec3> points;
    const auto edge = [&](int d,float side) {
        const auto gate = port(d);
        const Vec3 outward((d==1)-(d==3),0,(d==2)-(d==0));
        return gate.center+outward.Cross(Vec3::sAxisY())*(side*(gate.width/2-inset));
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
std::vector<std::array<Vec3,3>> CityRoadNode::sidewalk_triangles() const {
    std::vector<std::array<Vec3,3>> result;
    const auto triangle = [&](Vec3 a,Vec3 b,Vec3 c) {
        const float area = (b-a).Cross(c-a).GetY();
        if (std::abs(area)<1.e-6f) return;
        if (area<0) std::swap(b,c);
        result.push_back({a,b,c});
    };
    const auto strip = [&](const std::vector<Vec3>& a,const std::vector<Vec3>& b,bool closed) {
        for (std::size_t i=0;i<(closed ? a.size() : a.size()-1);++i) {
            const auto j=(i+1)%a.size(); bool gate=false;
            if (closed) for (int d=0;d<4;++d) if (mask&(1u<<d)) {
                const Vec3 out((d==1)-(d==3),0,(d==2)-(d==0));
                gate|=std::abs((a[i]-port(d).center).Dot(out))<.001f && std::abs((a[j]-port(d).center).Dot(out))<.001f;
            }
            if (!gate) { triangle(a[i],a[j],b[j]); triangle(a[i],b[j],b[i]); }
        }
    };
    // The 1.2 m verge follows the existing pavement and the NPC's 1.2 m inset.
    if (const auto edge=path(0,.7f); !edge.empty()) {
        strip(edge,path(0,1.9f),false); strip(path(1,-.7f),path(1,-1.9f),false);
    } else if (const auto edge=outline(); !edge.empty()) strip(edge,outline(1.9f),true);
    else {
        const auto rectangle = [&](float inset) {
            const Vec3 c=center(), h=half_size();
            const float l=-h.GetX()+(mask&8 ? 0 : inset), r=h.GetX()-(mask&2 ? 0 : inset);
            const float t=-h.GetZ()+(mask&1 ? 0 : inset), b=h.GetZ()-(mask&4 ? 0 : inset);
            return std::vector<Vec3>{c+Vec3(l,0,t),c+Vec3(r,0,t),c+Vec3(r,0,b),c+Vec3(l,0,b)};
        };
        strip(rectangle(.7f),rectangle(1.9f),true);
    }
    return result;
}
int City::building_at(CityCell p) const {
    if (freeform) return building_at(center(p));
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
            if (land({x,z})) {
                const int tile = index({x,z}); ground[tile] = texture;
                if (texture==CityGround::Soil) ground_textures[tile] = "soil.png";
                else ground_textures.erase(tile);
            }
    return true;
}
bool City::paint_ground(CityCell a, CityCell b, const std::string& texture, std::string& error) {
    error.clear();
    if (!contains(a) || !contains(b) || !valid_texture_filename(texture)) return fail(error,"Draw ground inside the map using a valid texture filename.");
    for (int i = 0; i < 4; ++i) if (texture==ground_files[i]) return paint_ground(a,b,CityGround(i),error);
    for (int z = std::min(a.z,b.z); z <= std::max(a.z,b.z); ++z)
        for (int x = std::min(a.x,b.x); x <= std::max(a.x,b.x); ++x)
            if (land({x,z})) ground_textures[index({x,z})] = texture;
    return true;
}
std::string City::ground_texture(CityCell p) const {
    if (!contains(p)) return {};
    const auto found = ground_textures.find(index(p));
    return found!=ground_textures.end() ? found->second : ground_files[int(ground[index(p)])];
}
std::optional<CityCell> City::shore_cell(Vec3 point) const {
    if (!std::isfinite(point.GetX()) || !std::isfinite(point.GetZ())
        || std::abs(point.GetX())>=extent+32 || std::abs(point.GetZ())>=extent+32) return {};
    const auto p = cell(point.GetX(),point.GetZ());
    if (land(p)) return p;
    std::optional<CityCell> nearest;
    float distance_sq = 32*32;
    constexpr int reach = int(32 / block)+1;
    for (int z = p.z-reach; z<=p.z+reach; ++z) for (int x = p.x-reach; x<=p.x+reach; ++x) {
        const CityCell candidate{x,z}; if (!land(candidate)) continue;
        const float left = (x-width/2)*block, top = (z-width/2)*block;
        const float dx = std::max({left-point.GetX(),0.f,point.GetX()-left-block});
        const float dz = std::max({top-point.GetZ(),0.f,point.GetZ()-top-block});
        if (dx*dx+dz*dz<distance_sq) { distance_sq = dx*dx+dz*dz; nearest = candidate; }
    }
    return nearest;
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
    if (freeform) {
        for (float x : {-2.f,2.f}) for (float z : {-2.f,2.f}) {
            const Vec3 point=p+Vec3(x,0,z);
            if (!land_at(point) || road_at(point,road_style.sidewalks ? 1.3f : 0) || building_at(point)>=0) return false;
        }
        return true;
    }
    // Keep trunks and foliage clear of streets and buildings.
    for (float x : {-2.f,2.f}) for (float z : {-2.f,2.f}) {
        const auto c = cell(p.GetX()+x,p.GetZ()+z);
        if (tile(c) != CityTile::Land || building_at(c) >= 0) return false;
    }
    return true;
}
void City::clear_trees() {
    trees.erase(std::remove_if(trees.begin(),trees.end(),[&](const Tree& t){return !tree_clear(t.base);}),trees.end());
}
bool City::place_tree(Vec3 point,std::string& error) {
    if (!tree_clear(point)) return fail(error,"Place trees on land clear of roads and objects.");
    if (trees.size()>=8192) return fail(error,"Tree limit reached (8192).");
    const auto plot=[](Vec3 p) {return int(std::floor((p.GetZ()+1024)/4))*512+int(std::floor((p.GetX()+1024)/4));};
    for (const auto& tree : trees) {
        const Vec3 d=tree.base-point;
        if (plot(tree.base)==plot(point) || d.GetX()*d.GetX()+d.GetZ()*d.GetZ()<6.25f)
            return fail(error,"Leave at least 2.5 m between trees and choose a free four-metre plot.");
    }
    const auto noise=hash32(std::uint32_t(plot(point)));
    point.SetY(height(point.GetX(),point.GetZ()));
    trees.push_back({point,4+float((noise>>24)&255)/255*4,float(noise&255)/255*6.2831853f});
    error.clear(); return true;
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
    std::map<int,Vec3> occupied;
    for (auto t : trees) occupied.emplace(plot(t.base),t.base);
    std::uint32_t seed = 0;
    for (unsigned char c : id) seed = seed*31+c;
    const int threshold[] = {0,25,55,90};
    const int x0 = std::max(0,int(std::floor((point.GetX()-radius+1024)/4))), x1 = std::min(511,int(std::floor((point.GetX()+radius+1024)/4)));
    const int z0 = std::max(0,int(std::floor((point.GetZ()-radius+1024)/4))), z1 = std::min(511,int(std::floor((point.GetZ()+radius+1024)/4)));
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const int key = z*512+x; const auto noise = hash32(seed+std::uint32_t(key));
        if (occupied.count(key) || int(noise%100) >= threshold[density]) continue;
        const auto random = [&](int shift) { return float((noise>>shift)&255)/255; };
        Vec3 p(x*4-1022+(random(8)-.5f)*1.5f,level,z*4-1022+(random(16)-.5f)*1.5f);
        if (!inside(p) || !tree_clear(p)) continue;
        bool crowded=false;
        for (int dz=-1;dz<=1;++dz) for (int dx=-1;dx<=1;++dx) {
            const auto neighbor=occupied.find((z+dz)*512+x+dx);
            if (neighbor!=occupied.end()) {const Vec3 d=neighbor->second-p;crowded|=d.GetX()*d.GetX()+d.GetZ()*d.GetZ()<6.25f;}
        }
        if (crowded) continue;
        p.SetY(height(p.GetX(),p.GetZ()));
        if (trees.size() >= 8192) { error = "Tree limit reached (8192)."; return int(trees.size())-before; }
        trees.push_back({p,4+random(24)*4,random(0)*6.2831853f});
        occupied.emplace(key,p);
    }
    return int(trees.size())-before;
}
int City::brush_objects(const BuildingMesh& mesh,Vec3 point,float radius,int density,bool remove,std::string& error) {
    error.clear();
    if (!freeform || mesh.type!=ObjectType::Object) {error="Choose an Object design to brush onto a freeform map.";return -1;}
    if (!mesh.validate(error)) return -1;
    if (!std::isfinite(point.GetX()) || !std::isfinite(point.GetZ()) || std::abs(point.GetX())>extent || std::abs(point.GetZ())>extent
        || !std::isfinite(radius) || radius<4 || radius>96 || density<1 || density>3) {error="Keep the object brush inside the map.";return -1;}
    const auto inside=[&](Vec3 p) {const Vec3 d=p-point;return d.GetX()*d.GetX()+d.GetZ()*d.GetZ()<=radius*radius;};
    const int before=int(buildings.size());
    if (remove) {
        buildings.erase(std::remove_if(buildings.begin(),buildings.end(),[&](const auto& b){
            return b.mesh && b.mesh->type==ObjectType::Object && b.mesh->name==mesh.name && inside(b.base(level));
        }),buildings.end());
        return before-int(buildings.size());
    }
    const auto footprint_radius=[](const BuildingMesh& design) {
        float radius=0;
        for (auto p : design.vertices) radius=std::max(radius,std::hypot(p.GetX()*design.size.GetX(),p.GetZ()*design.size.GetZ()));
        return radius;
    };
    const float reach=footprint_radius(mesh),spacing=std::max(4.f,reach*2+2);
    std::vector<std::pair<Vec3,float>> occupied;
    for (const auto& b : buildings) occupied.push_back({b.base(level),b.mesh ? footprint_radius(*b.mesh) : b.size*block/std::sqrt(2.f)});
    std::uint32_t seed=0;for (unsigned char c : id+mesh.name) seed=seed*31+c;
    const int threshold[]={0,25,55,90},plots=int(std::ceil(extent*2/spacing));
    const int x0=std::max(0,int(std::floor((point.GetX()-radius+extent)/spacing))),x1=std::min(plots-1,int(std::floor((point.GetX()+radius+extent)/spacing)));
    const int z0=std::max(0,int(std::floor((point.GetZ()-radius+extent)/spacing))),z1=std::min(plots-1,int(std::floor((point.GetZ()+radius+extent)/spacing)));
    for (int z=z0;z<=z1;++z) for (int x=x0;x<=x1;++x) {
        const auto noise=hash32(seed+std::uint32_t(z*plots+x));
        if (int(noise%100)>=threshold[density]) continue;
        const auto random=[&](int shift){return float((noise>>shift)&255)/255;};
        const Vec3 p((x+.5f+(random(8)-.5f)*.25f)*spacing-extent,level,(z+.5f+(random(16)-.5f)*.25f)*spacing-extent);
        if (!inside(p) || !land_at(p) || road_at(p,reach+(road_style.sidewalks ? 1.3f : 0))) continue;
        const bool shore_clear=reach<=32 ? shore_distance(p)>=reach
            : std::none_of(shore_edges.begin(),shore_edges.end(),[&](const auto& edge){return path_distance(p,edge[0],edge[1])<reach;});
        if (!shore_clear) continue;
        if (std::any_of(occupied.begin(),occupied.end(),[&](const auto& other){return std::hypot(p.GetX()-other.first.GetX(),p.GetZ()-other.first.GetZ())<reach+other.second+1;})) continue;
        if (buildings.size()>=4096) {error="Object/building limit reached (4096).";break;}
        CityBuilding b;b.mesh=mesh;b.position=p;b.yaw=random(0)*359.99f;
        if (add_building(std::move(b),error)) occupied.push_back({p,reach});
    }
    return int(buildings.size())-before;
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
    if (freeform) return {};
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
    CityRoadNode node{p,p,{},road_mask(p)}; node.height = tile_height(p);
    return node.bend(0,radius);
}
std::vector<CityCell> City::neighbors(CityCell p) const {
    std::vector<CityCell> result;
    const unsigned mask = road_mask(p);
    for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) result.push_back(add(p,directions[d]));
    return result;
}
int City::land_count() const {
    if (!freeform) return int(std::count_if(tiles.begin(), tiles.end(), [](auto t) { return t == CityTile::Land || t == CityTile::Road; }));
    int count=0;
    for(int z=0;z<width;++z)for(int x=0;x<width;++x)if(land_at(center({x,z})))++count;
    return count;
}
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
    if (freeform) return place_building(std::move(b),error,replace);
    error.clear();
    if (b.rotation<0 || b.rotation>3) return fail(error,"Building rotation must be 0, 90, 180 or 270 degrees.");
    if (b.mesh && !b.mesh->validate(error)) return false;
    const auto footprint = b.footprint();
    if ((!b.mesh && (b.size < 1 || b.size > 8 || b.height < 4 || b.height > 120)) || !contains(b.cell)
        || !contains({b.cell.x+footprint.x-1,b.cell.z+footprint.z-1})) return fail(error, "Building dimensions must fit inside the map.");
    for (int z = b.cell.z; z < b.cell.z+footprint.z; ++z) for (int x = b.cell.x; x < b.cell.x+footprint.x; ++x) {
        CityCell p{x,z}; const int existing = building_at(p);
        if (tile(p) != CityTile::Land || (existing >= 0 && existing != replace))
            return fail(error, "Buildings need an empty footprint of land.");
        for (Vec3 corner : ground_patch(p))
            if (std::abs(corner.GetY()-tile_height(b.cell))>.001f) return fail(error,"Buildings need flat ground under the whole footprint.");
    }
    if (replace >= 0 && replace < int(buildings.size())) buildings[replace] = b;
    else if (buildings.size() < 4096) buildings.push_back(b);
    else return fail(error, "Building limit reached.");
    clear_trees();
    return true;
}
int City::update_building_design(const BuildingMesh& mesh, std::string& error, int replace) {
    if (!mesh.validate(error)) return -1;
    if (replace < -1 || replace >= int(buildings.size())) { error = "Invalid selected building."; return -1; }
    const auto encoded = [](const BuildingMesh& value) { std::ostringstream stream; value.write(stream); return stream.str(); };
    const auto definition = encoded(mesh);
    City next = *this; int changed = 0;
    // ponytail: existing saves link designs by name; stable IDs can preserve links across renames if needed.
    for (int i = 0; i < int(next.buildings.size()); ++i) {
        auto& building = next.buildings[i];
        if (i != replace && (!building.mesh || building.mesh->name != mesh.name)) continue;
        if (building.mesh && encoded(*building.mesh) == definition) continue;
        building.mesh = mesh; if(mesh.entrance || mesh.entrance_configured)building.entrance.reset(); ++changed;
    }
    if (changed) {
        next.clear_trees();
        if (!next.validate(error)) return -1;
        *this = std::move(next);
    }
    return changed;
}
int City::update_car_design(const CarDesign& design,std::string& error,int replace) {
    if (!design.validate(error)) return -1;
    if (replace < -1 || replace>=int(vehicles.size())) { error = "Invalid selected car."; return -1; }
    City next = *this; int changed = 0;
    const auto encoded = [](const CarDesign& value) { std::ostringstream stream; value.write(stream); return stream.str(); };
    for (int i = 0; i<int(next.vehicles.size()); ++i) {
        auto& vehicle = next.vehicles[i];
        if (vehicle.kind!=CityVehicleKind::Car || (i!=replace && (!vehicle.car || vehicle.car->name!=design.name))) continue;
        if (vehicle.car && encoded(*vehicle.car)==encoded(design)) continue;
        vehicle.car = design; ++changed;
    }
    if (changed) { next.clear_trees(); if (!next.validate(error)) return -1; *this = std::move(next); }
    return changed;
}
int City::update_car_designs(const std::vector<CarDesign>& designs,std::string& error) {
    error.clear(); int changed = 0;
    for (const auto& design : designs) {
        std::string problem; const int count = update_car_design(design,problem);
        if (count>=0) changed += count;
        else {
            if (!error.empty()) error += " / ";
            error += design.name+": "+problem;
        }
    }
    return changed;
}
bool City::add_vehicle(CityVehicle v, std::string& error, int replace) {
    error.clear();
    if (v.car && (v.kind!=CityVehicleKind::Car || !v.car->validate(error))) return fail(error,"Invalid placed car design.");
    if (int(v.kind) < 0 || int(v.kind) > 3 || v.rotation < 0 || v.rotation > 3
        || !std::isfinite(v.position.GetX()) || !std::isfinite(v.position.GetY()) || !std::isfinite(v.position.GetZ())
        || std::abs(v.position.GetY()-height(v.position.GetX(),v.position.GetZ()))>.001f || std::abs(v.position.GetX())>=extent || std::abs(v.position.GetZ())>=extent)
        return fail(error, "Place vehicles on ground inside the map.");
    v.position.SetY(height(v.position.GetX(),v.position.GetZ()));
    // Validate the whole rotated footprint, especially the 747's wings.
    const Vec3 c = v.position, half = v.half_size(); const float rx = half.GetX(), rz = half.GetZ();
    auto a = cell(c.GetX()-rx, c.GetZ()-rz), b = cell(c.GetX()+rx, c.GetZ()+rz);
    for (int z = a.z; z <= b.z; ++z) for (int x = a.x; x <= b.x; ++x)
        if (!freeform && tile({x,z}) == CityTile::Water)
            return fail(error, "The whole vehicle needs land or a road beneath it.");
    for (float x : {-rx,rx}) for (float z : {-rz,rz})
        if (std::abs(c.GetX()+x)>=extent || std::abs(c.GetZ()+z)>=extent || std::abs(height(c.GetX()+x,c.GetZ()+z)-c.GetY())>.05f
            || (freeform && !land_at(c+Vec3(x,0,z)) && !road_at(c+Vec3(x,0,z)))) return fail(error,"Park vehicles on level ground.");
    if (replace >= 0 && replace < int(vehicles.size())) vehicles[replace] = v;
    else if (vehicles.size() < 512) vehicles.push_back(v);
    else return fail(error, "Vehicle limit reached.");
    clear_trees();
    return true;
}
bool City::set_spawn(CityCell p, std::string& error) {
    if (!contains(p)) return fail(error,"Set the spawn on ground inside the map.");
    Vec3 point=center(p); point.SetY(height(point.GetX(),point.GetZ()));
    return set_spawn(point,error);
}
bool City::set_spawn(Vec3 p, std::string& error) {
    error.clear();
    if (!std::isfinite(p.GetX()) || !std::isfinite(p.GetY()) || !std::isfinite(p.GetZ())
        || std::abs(p.GetX())>=extent || std::abs(p.GetZ())>=extent || (freeform ? !land_at(p) && !road_at(p) : tile(cell(p.GetX(),p.GetZ()))==CityTile::Water)
        || std::abs(p.GetY()-height(p.GetX(),p.GetZ()))>.001f)
        return fail(error, "Set the spawn on ground inside the map.");
    p.SetY(height(p.GetX(),p.GetZ())); spawn = p; clear_trees(); return true;
}
bool City::erase(CityCell p, std::string& error) {
    error.clear();
    if (!contains(p)) return fail(error, "Keep edits inside the map.");
    int object = building_at(p);
    if (object >= 0) { buildings.erase(buildings.begin()+object); return true; }
    if (spawn && cell(spawn->GetX(),spawn->GetZ()) == p) { spawn.reset(); return true; }
    if (road(p) || tile(p) == CityTile::Land) {
        const City before = *this; const auto previous = tile(p);
        tiles[index(p)] = previous == CityTile::Road ? CityTile::Land : CityTile::Water;
        road_axes[index(p)] = 0;
        if (tile(p) == CityTile::Water) { ground[index(p)] = CityGround::Soil; ground_textures.erase(index(p)); }
        clear_trees();
        sync_elevation(*this);
        if (!validate(error)) { *this = before; return false; }
    }
    return true;
}
bool City::erase(Vec3 p, std::string& error) {
    const int selected = vehicle_at(p);
    if (selected>=0) { vehicles.erase(vehicles.begin()+selected); error.clear(); return true; }
    if (spawn && (p-*spawn).LengthSq()<1) {spawn.reset(); error.clear(); return true;}
    if (freeform) {
        City next=*this;
        const int building=building_at(p);
        if (building>=0) next.buildings.erase(next.buildings.begin()+building);
        else {
            int road=-1;
            for (int r=int(paths.size())-1;r>=0 && road<0;--r) for (std::size_t i=1;i<paths[r].centerline.size();++i)
                if (path_distance(p,paths[r].centerline[i-1],paths[r].centerline[i])<=paths[r].width/2) {road=r;break;}
            if (road>=0) next.paths.erase(next.paths.begin()+road);
            else for (int i=int(areas.size())-1;i>=0;--i) if (polygon_contains(areas[i].boundary,p)) {next.areas.erase(next.areas.begin()+i);break;}
        }
        next.rebuild_paths(); next.clear_trees();
        if (!next.validate(error)) return false;
        *this=std::move(next); return true;
    }
    return erase(cell(p.GetX(),p.GetZ()),error);
}
bool City::validate(std::string& error) const {
    error.clear();
    if (player_character && (!player_character->validate(error) || player_character->type!=CharacterType::Player))
        return fail(error,"City player must be a valid Player character design.");
    if (!valid_id(id) || !valid_name(name)) return fail(error, "Invalid city name or identifier.");
    if (start_minutes < 0 || start_minutes >= 1440 || trees.size() > 8192) return fail(error,"Invalid starting time or tree count.");
    if (pedestrian_density<0 || pedestrian_density>100) return fail(error,"Pedestrian density must be between 0 and 100 percent.");
    if (!valid_texture_filename(road_style.road_texture) || !valid_texture_filename(road_style.sidewalk_texture))
        return fail(error,"Invalid road or sidewalk texture filename.");
    if (freeform && !validate_paths(error)) return false;
    if (!freeform) {
    for (const auto& texture : ground_textures)
        if (texture.first<0 || texture.first>=width*width || !land({texture.first%width,texture.first/width})
            || !valid_texture_filename(texture.second)) return fail(error,"Invalid ground texture filename or tile.");
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
    if (races.size()>128) return fail(error,"Race limit reached (128 per city).");
    std::set<std::string> race_names;
    for (const auto& race : races) {
        std::vector<RaceLeg> route;
        if (!race_names.insert(race.name).second) return fail(error,"Race names must be unique in this city.");
        if (!build_race_route(*this,race,route,error)) { error=race.name+": "+error; return false; }
    }
    return validate_interiors(*this,error);
}
std::vector<std::vector<Vec3>> City::traffic_routes(float curb_inset) const {
    if (freeform) return path_traffic_routes(curb_inset);
    const auto nodes = road_network();
    std::vector<std::vector<int>> exits(nodes.size());
    std::vector<std::vector<bool>> used(nodes.size());
    for (std::size_t n=0;n<nodes.size();++n) {
        const auto& node=nodes[n]; const auto count=node.edges.size();
        std::vector<int> order(count); std::iota(order.begin(),order.end(),0); used[n].resize(count);
        float best=-std::numeric_limits<float>::max();
        // At most four arms: choose a permutation that continues straight where possible.
        do {
            float score=0; bool valid=true;
            for (std::size_t i=0;i<count;++i) {
                if (count>1 && order[i]==int(i)) {valid=false; break;}
                Vec3 in=node.center()-nodes[node.edges[i]].center(),out=nodes[node.edges[order[i]]].center()-node.center();
                in.SetY(0); out.SetY(0); score+=in.Normalized().Dot(out.Normalized());
            }
            if (valid && score>best) {best=score; exits[n]=order;}
        } while (std::next_permutation(order.begin(),order.end()));
    }
    std::vector<std::vector<Vec3>> routes;
    for (int start=0;start<int(nodes.size());++start) for (int arm=0;arm<int(nodes[start].edges.size());++arm) if (!used[start][arm]) {
        std::vector<int> tour; int node=start,entry=arm;
        while (!used[node][entry]) {
            used[node][entry]=true; tour.push_back(node);
            const int next=nodes[node].edges[exits[node][entry]];
            entry=int(std::find(nodes[next].edges.begin(),nodes[next].edges.end(),node)-nodes[next].edges.begin());
            node=next;
        }
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
            const float lane_in = entry.second.width/2-curb_inset, lane_out = exit.second.width/2-curb_inset;
            const Vec3 a = entry.second.center+r*lane_in, b = exit.second.center+s*lane_out;
            auto curve = node.path(0,curb_inset);
            if (in.Dot(out)>-.9f && !curve.empty()) {
                if (std::min((curve.front()-a).LengthSq(),(curve.back()-a).LengthSq())>.001f) curve = node.path(1,-curb_inset);
                if ((curve.back()-a).LengthSq()<(curve.front()-a).LengthSq()) std::reverse(curve.begin(),curve.end());
                route.insert(route.end(),curve.begin(),curve.end());
            } else if (in.Dot(out) > .9f) { route.push_back(a); route.push_back(b); }
            else if (in.Dot(out) < -.9f) {
                if (curb_inset<1.9f) {
                    const Vec3 cap=c-in*(along(in)-curb_inset);
                    route.insert(route.end(),{a,cap+r*lane_in,cap-r*lane_in,b});
                    continue;
                }
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
bool City::generate_terrain(int tiles_x,int tiles_z,std::uint32_t seed,std::string& error,TerrainSettings settings) {
    if (tiles_x<1 || tiles_z<1 || tiles_x>width || tiles_z>width) return fail(error,"Starter dimensions must be 1-128 tiles per axis.");
    if (settings.elevation<0 || settings.elevation>max_elevation || settings.water_percent<0 || settings.water_percent>100)
        return fail(error,"Elevation must be 0-8 steps and water coverage 0-100 percent.");
    if ((settings.river || settings.two_islands) && tiles_x<5) return fail(error,"A river or two islands needs at least 5 starter tiles across.");
    if (std::any_of(tiles.begin(),tiles.end(),[](auto t){return t!=CityTile::Water;}) || !buildings.empty()
        || !vehicles.empty() || !trees.empty() || spawn) return fail(error,"Generate starter terrain only in an empty city.");
    City next = *this; next.elevation.fill(0);
    const int x0 = (width-tiles_x)/2, z0 = (width-tiles_z)/2;
    const float frequency = 4.f/std::max(8,std::min(tiles_x,tiles_z));
    const auto noise = [&](float x,float z,std::uint32_t key) {
        return .65f*perlin(x,z,key)+.25f*perlin(x*2,z*2,key+1)+.1f*perlin(x*4,z*4,key+2);
    };
    std::array<float,width*width> scores{};
    std::array<int,width*width> regions; regions.fill(-1);
    std::array<int,2> capacity{}, peaks{{-1,-1}}, quota{};
    const bool split = settings.river || settings.two_islands;
    for (int z = 0; z<tiles_z; ++z) for (int x = 0; x<tiles_x; ++x) {
        const float px=x+.5f,pz=z+.5f;
        const float channel = tiles_x*.5f+(settings.river ? perlin(pz*frequency*.4f+7.31f,19.73f,seed^0xa31f5e19u)*tiles_x*.15f : 0);
        if (split && std::abs(px-channel)<std::max(1.f,tiles_x*(settings.two_islands ? .045f : .025f))) continue;
        const int region = split && px>channel ? 1 : 0, i=index({x0+x,z0+z});
        const float across = settings.two_islands ? std::abs((px-tiles_x*(region ? .75f : .25f))*4/tiles_x) : std::abs(px*2/tiles_x-1);
        const float edge = std::max(across,std::abs(pz*2/tiles_z-1));
        scores[i]=noise(px*frequency+11.37f,pz*frequency+23.61f,seed)*.85f-.55f*edge*edge*edge*edge;
        regions[i]=region; ++capacity[region];
        if (peaks[region]<0 || scores[i]>scores[peaks[region]]) peaks[region]=i;
    }
    const int available=capacity[0]+capacity[1];
    const int target=settings.water_percent==100 ? 0 : std::min(available,std::max(split ? 2 : 1,int(std::round(tiles_x*tiles_z*(100-settings.water_percent)/100.f))));
    quota[1]=split ? std::min(capacity[1],target/2) : 0;
    quota[0]=std::min(capacity[0],target-quota[1]); quota[1]=std::min(capacity[1],target-quota[0]);
    // Grow from each peak so water settings cannot fragment the two islands into an archipelago.
    std::array<bool,width*width> seen{};
    for (int region=0; region<(split ? 2 : 1); ++region) {
        std::priority_queue<std::pair<float,int>> pending;
        if (peaks[region]>=0) { pending.push({scores[peaks[region]],peaks[region]}); seen[peaks[region]]=true; }
        while (quota[region]>0 && !pending.empty()) {
            const int i=pending.top().second; pending.pop(); next.tiles[i]=CityTile::Land; --quota[region];
            for (auto d : directions) {
                const auto p=add({i%width,i/width},d);
                if (!contains(p)) continue;
                const int neighbor=index(p);
                if (!seen[neighbor] && regions[neighbor]==region) { seen[neighbor]=true; pending.push({scores[neighbor],neighbor}); }
            }
        }
    }
    if (settings.elevation>0) {
        for (int z=z0; z<=z0+tiles_z; ++z) for (int x=x0; x<=x0+tiles_x; ++x) {
            bool inland=true;
            for (int dz : {-1,0}) for (int dx : {-1,0}) inland &= next.land({x+dx,z+dz});
            if (!inland) continue;
            const float hill=std::clamp((noise((x-x0)*frequency*.45f+41.83f,(z-z0)*frequency*.45f+67.19f,seed^0xb5297a4du)-.025f)*3.5f,0.f,1.f);
            next.elevation[corner_index({x,z})]=static_cast<unsigned char>(std::round(hill*settings.elevation));
        }
        constrain_elevation(next,-1);
    }
    if (!next.validate(error)) return false;
    *this = std::move(next); return true;
}
bool City::update_player_character(const std::vector<CharacterDesign>& designs) {
    if (player_character) for (const auto& design:designs)
        if (design.name==player_character->name && design.type==CharacterType::Player && !(design==*player_character)) {
            player_character=design; return true;
        }
    return false;
}
bool City::save(const std::filesystem::path& directory, std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) return fail(error,"Cannot create the cities folder.");
    const auto path = directory/(id+".city"); auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return fail(error,"Cannot write this city. Your previous save is intact.");
    const bool free_spawn=spawn && (spawn->GetX()!=center(cell(spawn->GetX(),spawn->GetZ())).GetX()
        || spawn->GetZ()!=center(cell(spawn->GetX(),spawn->GetZ())).GetZ());
    const bool typed_objects=std::any_of(buildings.begin(),buildings.end(),[](const auto& b){return b.mesh && b.mesh->type==ObjectType::Object;});
    const int version = !interiors.empty() ? 22 : typed_objects ? 20 : freeform ? 19 : !races.empty() ? 18 : road_style!=RoadStyle{} ? 17 : free_spawn ? 16 : pedestrian_density!=100 ? 15 : std::any_of(buildings.begin(),buildings.end(),[](const auto& b){return b.mesh.has_value();}) ? 14
        : player_character ? 13 : !ground_textures.empty() ? 12 : std::any_of(vehicles.begin(),vehicles.end(),[](const auto& v){return v.car.has_value();}) ? 11
        : std::any_of(buildings.begin(),buildings.end(),[](const auto& b){return b.rotation!=0;}) ? 9
        : 6;
    file << "AMBARETTO_CITY " << version << '\n' << id << '\n' << std::quoted(name) << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (version>=20) file << int(freeform) << '\n';
    if (freeform) write_paths(file,version>=22 ? 5 : version>=20 ? 4 : 3);
    else {
    for (auto t : tiles) file << char('0'+int(t));
    file << '\n' << buildings.size() << '\n';
    for (const auto& b : buildings) {
        file << b.cell.x << ' ' << b.cell.z << ' ' << b.size << ' ' << b.height;
        if (version>=7) file << ' ' << int(b.mesh.has_value());
        if (version>=9) file << ' ' << b.rotation;
        file << '\n';
        if (b.mesh) b.mesh->write(file,version>=22 ? 5 : version>=20 ? 4 : 3);
    }
    file << vehicles.size() << '\n';
    for (const auto& v : vehicles) {
        file << int(v.kind) << ' ' << v.position.GetX() << ' ' << v.position.GetZ() << ' ' << v.rotation;
        if (version>=10) file << ' ' << int(v.car.has_value());
        file << '\n'; if (v.car) v.car->write(file);
    }
    file << int(spawn.has_value()) << ' ';
    if (version>=16) file << (spawn ? spawn->GetX() : 0) << ' ' << (spawn ? spawn->GetZ() : 0) << '\n';
    else { const auto p=spawn ? cell(spawn->GetX(),spawn->GetZ()) : CityCell{}; file << p.x << ' ' << p.z << '\n'; }
    file << start_minutes << '\n' << trees.size() << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (auto t : trees) file << t.base.GetX() << ' ' << t.base.GetZ() << ' ' << t.height << ' ' << t.yaw << '\n';
    for (auto axis : road_axes) file << char('0'+axis);
    file << '\n';
    for (auto texture : ground) file << char('0'+int(texture));
    file << '\n';
    for (auto height : elevation) file << char('0'+height);
    file << '\n';
    if (version>=12) {
        file << ground_textures.size() << '\n';
        for (const auto& texture : ground_textures) file << texture.first << ' ' << std::quoted(texture.second) << '\n';
    }
    if (version>=13) { file << int(player_character.has_value()) << '\n'; if (player_character) player_character->write(file); }
    if (version>=15) file << pedestrian_density << '\n';
    if (version>=17) file << std::quoted(road_style.road_texture) << ' ' << int(road_style.sidewalks) << ' ' << std::quoted(road_style.sidewalk_texture) << '\n';
    if (version>=18) {
        file << races.size() << '\n';
        for (const auto& race : races) {
            file << std::quoted(race.name) << ' ' << race.laps << ' ' << race.checkpoints.size() << '\n';
            for (auto p : race.checkpoints) file << p.GetX() << ' ' << p.GetZ() << '\n';
        }
    }
    }
    if (version>=21) write_interiors(file,*this);
    file.close();
    if (!file) return fail(error,"Cannot finish saving. Your previous save is intact.");
    if (std::filesystem::file_size(temporary,ec)>64*1024*1024 || ec)
        return fail(error,"City exceeds the 64 MiB save limit. Your previous save is intact.");
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
    if (std::filesystem::file_size(path,ec) > 64*1024*1024 || ec) return fail(error,"City save is missing or too large.");
    std::ifstream file(path); City read; std::string magic, terrain; int version = 0;
    if (!(file >> magic >> version >> read.id >> std::quoted(read.name)) || magic != "AMBARETTO_CITY" || version<1 || version>22)
        return fail(error,"Invalid or unsupported city save.");
    if (version>=20) {
        int freeform;
        if (!(file>>freeform) || freeform<0 || freeform>1) return fail(error,"Invalid city layout type.");
        read.freeform=freeform!=0;
    }
    if (version==19 || read.freeform) {
        if (!read_paths(file,read,error,version>=22 ? 5 : version>=20 ? 4 : 3)) return false;
        if (version>=21 && !read_interiors(file,read,error,version>=22 ? 2 : 1)) return false;
        std::string extra;
        if (file>>extra) return fail(error,"Unexpected city data.");
        if (path.stem().string()!=read.id) return fail(error,"City save identifier does not match its filename.");
        if (!read.validate(error)) return false;
        city=std::move(read); error.clear(); return true;
    }
    if (!(file>>terrain) || terrain.size()!=read.tiles.size()) return fail(error,"Invalid city terrain.");
    for (std::size_t i = 0; i < terrain.size(); ++i) {
        if (terrain[i] < '0' || terrain[i] > (version>=3 ? '3' : '2')) return fail(error,"Invalid city terrain.");
        read.tiles[i] = CityTile(terrain[i]-'0');
    }
    int count = 0;
    if (!(file >> count) || count < 0 || count > 4096) return fail(error,"Invalid building count.");
    for (int i = 0; i < count; ++i) {
        CityBuilding b;
        if (!(file >> b.cell.x >> b.cell.z >> b.size >> b.height)) return fail(error,"Invalid building data.");
        if (version>=7) {
            int custom;
            if (!(file>>custom) || custom<0 || custom>1) return fail(error,"Invalid building mesh flag.");
            if (version>=9 && (!(file>>b.rotation) || b.rotation<0 || b.rotation>3)) return fail(error,"Invalid building rotation.");
            if (custom) { b.mesh.emplace(); if (!BuildingMesh::read(file,*b.mesh,error,version>=22 ? 5 : version>=20 ? 4 : version>=14 ? 3 : version>=8 ? 2 : 1)) return false; }
        }
        read.buildings.push_back(std::move(b));
    }
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
        v.kind = CityVehicleKind(kind);
        if (version>=10) {
            int custom; if (!(file>>custom) || custom<0 || custom>1) return fail(error,"Invalid car design flag.");
            if (custom) { v.car.emplace(); if (!CarDesign::read(file,*v.car,error,version>=11 ? 2 : 1)) return false; }
        }
        read.vehicles.push_back(v);
    }
    if (!(file >> count) || count<0 || count>1) return fail(error,"Invalid spawn data.");
    if (version>=16) {
        float x,z;
        if (!(file>>x>>z) || !std::isfinite(x) || !std::isfinite(z) || std::abs(x)>=extent || std::abs(z)>=extent)
            return fail(error,"Invalid spawn position.");
        if (count) read.spawn=Vec3(x,level,z);
    } else {
        CityCell p; if (!(file>>p.x>>p.z) || (count && !contains(p))) return fail(error,"Invalid spawn data.");
        if (count) read.spawn=center(p);
    }
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
            if (version>=6) read.elevation[i] = heights[i]-'0';
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
    if (version>=12) {
        if (!(file>>count) || count<0 || count>width*width) return fail(error,"Invalid ground texture count.");
        for (int i = 0; i < count; ++i) {
            int tile; std::string texture;
            if (!(file>>tile>>std::quoted(texture)) || !valid_texture_filename(texture)
                || !read.ground_textures.emplace(tile,texture).second) return fail(error,"Invalid or duplicate ground texture data.");
        }
    }
    if (version>=13) {
        if (!(file>>count) || count<0 || count>1) return fail(error,"Invalid player character flag.");
        if (count) { read.player_character.emplace(); if (!CharacterDesign::read(file,*read.player_character,error)) return false; }
    }
    if (version>=15 && (!(file>>read.pedestrian_density) || read.pedestrian_density<0 || read.pedestrian_density>100))
        return fail(error,"Invalid pedestrian density; use 0-100 percent.");
    if (version>=17) {
        int enabled;
        if (!(file>>std::quoted(read.road_style.road_texture)>>enabled>>std::quoted(read.road_style.sidewalk_texture)) || enabled<0 || enabled>1)
            return fail(error,"Invalid road style settings.");
        read.road_style.sidewalks=enabled!=0;
    }
    if (version>=18) {
        if (!(file>>count) || count<0 || count>128) return fail(error,"Invalid race count.");
        for (int i = 0; i < count; ++i) {
            RaceMap race; int points;
            if (!(file>>std::quoted(race.name)>>race.laps>>points) || points<2 || points>256) return fail(error,"Invalid race data.");
            for (int j = 0; j < points; ++j) {
                float x,z;
                if (!(file>>x>>z) || !std::isfinite(x) || !std::isfinite(z) || std::abs(x)>=extent || std::abs(z)>=extent) return fail(error,"Invalid race checkpoint.");
                race.checkpoints.emplace_back(x,read.height(x,z),z);
            }
            read.races.push_back(std::move(race));
        }
    }
    if (read.spawn) read.spawn->SetY(read.height(read.spawn->GetX(),read.spawn->GetZ()));
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
    if (version>=21 && !read_interiors(file,read,error,version>=22 ? 2 : 1)) return false;
    std::string extra; if (file >> extra) return fail(error,"Unexpected city data.");
    if (path.stem().string() != read.id) return fail(error,"City save identifier does not match its filename.");
    if (!read.validate(error)) {
        if (version==5) error = "Older terrain needs adjustment for flat tiles and aligned ramps: "+error;
        return false;
    }
    city = std::move(read); error.clear(); return true;
}
} // namespace ambaretto
