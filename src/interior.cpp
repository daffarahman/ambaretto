#include "interior.hpp"
#include "city.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <limits>
#include <istream>
#include <ostream>
#include <set>
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
bool fail(std::string& error, const char* message) { error=message; return false; }
bool finite(Vec3 p) { return std::isfinite(p.GetX()) && std::isfinite(p.GetY()) && std::isfinite(p.GetZ()); }
bool identifier(const std::string& s) {
    return !s.empty() && s.size()<=48 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
}
void write_point(std::ostream& f, Vec3 p) { f<<p.GetX()<<' '<<p.GetY()<<' '<<p.GetZ()<<' '; }
bool read_point(std::istream& f, Vec3& p) {
    float x,y,z; if (!(f>>x>>y>>z)) return false; p=Vec3(x,y,z); return finite(p);
}
}
bool near_interior_marker(Vec3 feet, Vec3 marker, float radius) {
    const auto d=feet-marker;
    return finite(d) && std::abs(d.GetY())<1.2f && d.GetX()*d.GetX()+d.GetZ()*d.GetZ()<radius*radius;
}
std::vector<BuildingTriangle> Interior::object_triangles(const InteriorObject& object) const {
    auto result=designs.at(object.design).triangles();
    const auto rotation=Quat::sRotation(Vec3::sAxisY(),object.yaw*3.14159265359f/180);
    for (auto& t:result) for (auto& p:t.points) p=rotation*p+object.position;
    return result;
}
bool Interior::can_place(const InteriorObject& object, std::string& error) const {
    if (object.design<0 || object.design>=int(designs.size()) || !finite(object.position) || !std::isfinite(object.yaw) || std::abs(object.yaw)>360)
        return fail(error,"Invalid interior object placement.");
    const auto rotation=Quat::sRotation(Vec3::sAxisY(),object.yaw*3.14159265359f/180);
    const auto points=designs[object.design].points();
    for (auto p:points) {
        p=rotation*p+object.position;
        if (std::abs(p.GetX())>size.GetX()/2-.15f || std::abs(p.GetZ())>size.GetZ()/2-.15f || p.GetY()<-.001f || p.GetY()>size.GetY()-.1f)
            return fail(error,"Keep objects inside the room.");
    }
    // Keep the complete arrival/exit lane clear, including rotated furniture.
    Vec3 lo=Vec3::sReplicate(1e9f), hi=Vec3::sReplicate(-1e9f);
    for (auto p:points) { p=rotation*p+object.position; lo=Vec3::sMin(lo,p); hi=Vec3::sMax(hi,p); }
    if (lo.GetX()<exit().GetX()+1 && hi.GetX()>exit().GetX()-1 && hi.GetZ()>spawn().GetZ()-1 && lo.GetZ()<exit().GetZ()+1
        && hi.GetY()>exit().GetY()-.1f && lo.GetY()<exit().GetY()+2)
        return fail(error,"Keep the yellow exit and arrival lane clear.");
    error.clear(); return true;
}
bool Interior::place(InteriorObject object, std::string& error, int replace) {
    if (replace < -1 || replace>=int(objects.size()) || (replace<0 && objects.size()>=512))
        return fail(error,"Interior object limit reached (512), or invalid selection.");
    if (!can_place(object,error)) return false;
    if (replace<0) objects.push_back(object); else objects[replace]=object;
    return true;
}
bool Interior::validate(std::string& error) const {
    if (!identifier(id) || name.empty() || name.size()>48 || name.find_first_not_of(' ')==std::string::npos
        || std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c>=127;})) return fail(error,"Use an interior name of 1-48 printable characters.");
    if (!finite(size) || size.GetX()<8 || size.GetX()>64 || size.GetZ()<8 || size.GetZ()>64 || size.GetY()<3 || size.GetY()>120)
        return fail(error,"Room width/depth must be 8-64 m and height 3-120 m.");
    if (shell && (!shell->validate(error) || shell->entrance || (shell->size-size).Length()>.001f))
        return fail(error,"Interior shell must be valid, match room bounds, and have no exterior entrance.");
    const auto surfaces=surface_mesh().baked().triangles(Vec3::sZero(),true);
    const auto supported=[&](Vec3 feet){
        if(!finite(feet) || std::abs(feet.GetX())>size.GetX()/2-.35f || std::abs(feet.GetZ())>size.GetZ()/2-.35f
            || feet.GetY()<0 || feet.GetY()>size.GetY()-1.8f)return false;
        for(const auto& t:surfaces){
            if(t.decal)continue;
            const auto normal=(t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]);
            if(normal.NormalizedOr(Vec3::sZero()).GetY()<.5f)continue;
            const float y=t.points[0].GetY()-(normal.GetX()*(feet.GetX()-t.points[0].GetX())+normal.GetZ()*(feet.GetZ()-t.points[0].GetZ()))/normal.GetY();
            if(std::abs(feet.GetY()-.08f-y)>.15f)continue;
            const auto p=Vec3(feet.GetX(),y,feet.GetZ());bool inside=true;
            for(int i=0;i<3;++i)inside&=(t.points[(i+1)%3]-t.points[i]).Cross(p-t.points[i]).Dot(normal)>=-.0001f;
            if(inside)return true;
        }
        return false;
    };
    if(!supported(exit()) || !supported(spawn()))return fail(error,"Keep a floor under the exit and arrival. Use Furniture / camera > Place exit.");
    if (designs.size()>128 || objects.size()>512) return fail(error,"Interior limit: 128 object designs and 512 placements.");
    std::vector<std::size_t> triangles;
    std::size_t total=surfaces.size();
    if(total>60000)return fail(error,"Interior geometry limit reached (60000 triangles). Use simpler surfaces.");
    for (const auto& d:designs) {
        if (d.type!=ObjectType::Object || !d.validate(error)) return fail(error,"Interiors accept valid Object designs only.");
        triangles.push_back(d.triangles().size());
    }
    for (const auto& object:objects) {
        if (!can_place(object,error)) return false;
        total+=triangles[object.design];
        if (total>60000) return fail(error,"Interior geometry limit reached (60000 triangles). Use simpler objects.");
    }
    error.clear(); return true;
}
BuildingMesh Interior::surface_mesh() const {
    auto mesh=shell.value_or(BuildingMesh{}); mesh.name=name; mesh.size=size; mesh.type=ObjectType::Object; return mesh;
}
std::vector<BuildingTriangle> Interior::triangles(bool ceiling) const {
    auto result=surface_mesh().baked().triangles(Vec3::sZero(),true);
    if (!ceiling) result.erase(std::remove_if(result.begin(),result.end(),[](const auto& t){
        return (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).GetY()<-.001f;
    }),result.end());
    std::vector<std::vector<BuildingTriangle>> templates(designs.size());
    for (const auto& object:objects) {
        auto& triangles=templates[object.design];
        if (triangles.empty()) triangles=designs[object.design].triangles();
        const auto rotation=Quat::sRotation(Vec3::sAxisY(),object.yaw*3.14159265359f/180);
        for (auto t:triangles) { for (auto& p:t.points) p=rotation*p+object.position; result.push_back(std::move(t)); }
    }
    return result;
}
Vec3 entrance_position(const City& city, const CityBuilding& building) {
    const auto* entrance=building_entrance(building);
    if (!entrance) return building.base(City::level);
    const auto rotation=Quat::sRotation(Vec3::sAxisY(),(building.yaw+building.rotation*90)*3.14159265359f/180);
    auto p=building.base(city.freeform ? City::level : city.tile_height(building.cell))+rotation*entrance->offset;
    p.SetY(city.height(p.GetX(),p.GetZ())+.08f); return p;
}
bool validate_interiors(const City& city, std::string& error) {
    if (city.interiors.size()>128) return fail(error,"Interior limit reached (128 per city).");
    std::set<std::string> ids;
    for (const auto& room:city.interiors) {
        if (!room.validate(error)) return false;
        if (!ids.insert(room.id).second) return fail(error,"Duplicate interior template identifier.");
    }
    for (const auto& b:city.buildings) if (const auto* entry=building_entrance(b)) {
        if (!ids.count(entry->interior) || !finite(entry->offset) || entry->offset.Length()>City::extent*3)
            return fail(error,"Invalid entrance or missing interior template.");
        if (!finite(entrance_position(city,b)))
            return fail(error,"Invalid building entrance position.");
    }
    return true;
}
void Interior::write(std::ostream& file,int version) const {
    file<<std::setprecision(std::numeric_limits<float>::max_digits10);
    file<<std::quoted(id)<<' '<<std::quoted(name)<<' '; write_point(file,size);
    file<<designs.size()<<' '<<objects.size()<<'\n';
    if (version>=2) {
        file<<int(shell.has_value())<<'\n';if(shell)shell->write(file);
        file<<int(exit_point.has_value())<<' ';if(exit_point)write_point(file,*exit_point);file<<'\n';
    }
    for (const auto& d:designs) d.write(file,version>=2 ? 5 : 4);
    for (const auto& o:objects) { file<<o.design<<' '; write_point(file,o.position); file<<o.yaw<<'\n'; }
}
bool Interior::read(std::istream& file,Interior& room,std::string& error,int version) {
    Interior value; int designs,objects;
    if (!(file>>std::quoted(value.id)>>std::quoted(value.name)) || !read_point(file,value.size)
        || !(file>>designs>>objects) || designs<0 || designs>128 || objects<0 || objects>512) return fail(error,"Invalid interior data.");
    if (version>=2) {
        int present;if(!(file>>present) || present<0 || present>1)return fail(error,"Invalid interior shell flag.");
        if(present){value.shell.emplace();if(!BuildingMesh::read(file,*value.shell,error))return false;}
        if(!(file>>present) || present<0 || present>1)return fail(error,"Invalid interior exit flag.");
        if(present){Vec3 point;if(!read_point(file,point))return fail(error,"Invalid interior exit position.");value.exit_point=point;}
    }
    for(int i=0;i<designs;++i){BuildingMesh d;if(!BuildingMesh::read(file,d,error,version>=2 ? 5 : 4))return false;value.designs.push_back(std::move(d));}
    for(int i=0;i<objects;++i){InteriorObject o;if(!(file>>o.design) || !read_point(file,o.position) || !(file>>o.yaw) || !value.place(o,error))return fail(error,"Invalid interior object.");}
    if(!value.validate(error))return false;
    room=std::move(value);return true;
}
bool Interior::save(const std::filesystem::path& directory,std::string& error) const {
    if(!validate(error))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    if(ec)return fail(error,"Cannot create the interiors folder.");
    const auto path=directory/(id+".interior");auto temporary=path;temporary+=".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if(!file)return fail(error,"Cannot write interior; previous save retained.");
    file<<"AMBARETTO_INTERIOR 2\n";write(file);file.close();
    if(!file || std::filesystem::file_size(temporary,ec)>64*1024*1024 || ec)return fail(error,"Cannot finish saving interior; previous save retained.");
#ifdef _WIN32
    const bool replaced=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,path,ec);const bool replaced=!ec;
#endif
    if(!replaced)return fail(error,"Cannot replace interior; previous save retained.");
    error.clear();return true;
}
bool Interior::load(const std::filesystem::path& path,Interior& room,std::string& error) {
    std::error_code ec;if(std::filesystem::file_size(path,ec)>64*1024*1024 || ec)return fail(error,"Interior save is missing or too large.");
    std::ifstream file(path);std::string magic,extra;int version;Interior value;
    if(!(file>>magic>>version) || magic!="AMBARETTO_INTERIOR" || version<1 || version>2)return fail(error,"Unsupported interior save.");
    if(!read(file,value,error,version))return false;
    if(file>>extra || path.stem().string()!=value.id)return fail(error,"Unexpected interior data or filename.");
    room=std::move(value);error.clear();return true;
}
std::vector<Interior> saved_interiors(const std::filesystem::path& directory,std::string& error) {
    std::vector<Interior> result;std::error_code ec;error.clear();
    if(!std::filesystem::exists(directory,ec) && !ec)return result;
    for(std::filesystem::directory_iterator it(directory,ec),end;!ec && it!=end;it.increment(ec)){
        if(!it->is_regular_file(ec) || it->path().extension()!=".interior")continue;
        Interior room;std::string issue;if(Interior::load(it->path(),room,issue))result.push_back(std::move(room));else error=issue;
    }
    if(ec)error="Cannot read the interiors folder.";
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;});return result;
}
const BuildingEntrance* building_entrance(const CityBuilding& building) {
    if(building.mesh && (building.mesh->entrance || building.mesh->entrance_configured))return building.mesh->entrance ? &*building.mesh->entrance : nullptr;
    return building.entrance ? &*building.entrance : nullptr;
}
bool update_city_interiors(City& city,const std::vector<Interior>& library,std::string& error,const std::string& required) {
    City next=city;bool changed=false;
    for(const auto& room:library){
        auto found=std::find_if(next.interiors.begin(),next.interiors.end(),[&](const auto& r){return r.id==room.id;});
        const bool needed=room.id==required || found!=next.interiors.end() || std::any_of(city.buildings.begin(),city.buildings.end(),[&](const auto& b){const auto* e=building_entrance(b);return e && e->interior==room.id;});
        if(!needed)continue;
        if(found==next.interiors.end()){
            next.interiors.push_back(room);changed=true;
        }else{std::ostringstream before,after;found->write(before);room.write(after);if(before.str()!=after.str()){*found=room;changed=true;}}
    }
    if(!required.empty() && std::none_of(next.interiors.begin(),next.interiors.end(),[&](const auto& r){return r.id==required;})){error="The building's interior template is missing.";return false;}
    if(!validate_interiors(next,error))return false;
    if(changed)city=std::move(next);
    return changed;
}
void write_interiors(std::ostream& file, const City& city,int version) {
    file<<city.interiors.size()<<'\n';
    for (const auto& room:city.interiors) room.write(file,version);
    std::size_t count=0; for (const auto& b:city.buildings) count+=b.entrance.has_value();
    file<<count<<'\n';
    for (std::size_t i=0;i<city.buildings.size();++i) if (const auto& e=city.buildings[i].entrance) {
        file<<i<<' '<<std::quoted(e->interior)<<' '; write_point(file,e->offset); file<<'\n';
    }
}
bool read_interiors(std::istream& file, City& city, std::string& error,int version) {
    int count;
    if (!(file>>count) || count<0 || count>128) return fail(error,"Invalid interior count.");
    for (int i=0;i<count;++i) {
        Interior room;if(!Interior::read(file,room,error,version))return false;
        city.interiors.push_back(std::move(room));
    }
    if (!(file>>count) || count<0 || count>int(city.buildings.size())) return fail(error,"Invalid entrance count.");
    for (int i=0;i<count;++i) {
        int index; BuildingEntrance e;
        if (!(file>>index>>std::quoted(e.interior)) || !read_point(file,e.offset) || index<0 || index>=int(city.buildings.size()) || city.buildings[index].entrance)
            return fail(error,"Invalid or duplicate entrance.");
        city.buildings[index].entrance=std::move(e);
    }
    return true;
}
} // namespace ambaretto
