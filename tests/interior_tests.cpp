#include "city.hpp"
#include "interior.hpp"
#include "player.hpp"
#include "saved_copy.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) { if (!okay) throw std::runtime_error(message); }
std::string contents(const std::filesystem::path& path) {std::ifstream file(path);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
int main() {
    using namespace ambaretto;
    City city=City::create("Shared interiors");
    const auto directory=std::filesystem::temp_directory_path()/("ambaretto-interiors-"+city.id);
    try {
        std::string error;
        Interior room; room.id="house"; room.name="Shared house";
        BuildingMesh chair; chair.type=ObjectType::Object; chair.name="Chair"; chair.size=Vec3(1,.8f,1);
        room.designs.push_back(chair);
        require(room.place({0,Vec3(-3,0,-3),30},error) && room.place({0,Vec3(3,0,-3),0},error),"Cannot place furniture");
        require(room.validate(error),error.c_str());
        require(!room.place({0,room.spawn(),0},error),"Furniture blocked arrival lane");
        require(!room.place({0,room.exit(),0},error),"Furniture blocked exit marker");
        require(!room.place({0,Vec3(8,0,-3),0},error),"Furniture escaped room bounds");
        require(!room.place({0,Vec3(2,4,-3),0},error),"Furniture escaped ceiling");
        require(!room.place({4,Vec3(2,0,-3),0},error),"Invalid design reference accepted");
        require(!room.place({0,Vec3(std::numeric_limits<float>::quiet_NaN(),0,0),0},error),"NaN furniture accepted");
        auto bad=room;bad.designs[0].type=ObjectType::Building;
        require(!bad.validate(error),"Building design accepted inside object-only room");
        bad=room;bad.size=Vec3(7,4,16);require(!bad.validate(error),"Invalid room dimensions accepted");
        bad=room;bad.objects.resize(513);require(!bad.validate(error),"Unbounded furniture accepted");
        require(near_interior_marker(room.exit()+Vec3(.5f,0,0),room.exit()),"Marker did not detect nearby feet");
        require(!near_interior_marker(room.exit()+Vec3(0,4,0),room.exit()),"Marker triggered through another floor");
        require(!near_interior_marker(room.spawn(),room.exit()),"Arrival immediately re-entered exit");
        require(room.triangles().size()==room.triangles(false).size()+2,"Creator ceiling cutaway changed collision geometry");
        auto decorated=room;decorated.shell=room.surface_mesh();decorated.shell->texture="brick08.png";
        require(decorated.shell->set_uv({0},BuildingProjection::Planar,1.5f,{.2f,.3f},{2,1},30,error),"Interior UV edit failed");
        require(decorated.shell->place_decal(0,Vec3(0,2,-8),"door01.png",1,2,error)>=0,"Interior decal placement failed");
        bool decal_seen=false;
        for(const auto& t:decorated.surface_mesh().triangles(Vec3::sZero(),true))if(t.face==0){
            const auto normal=(t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).Normalized();
            require(normal.GetZ()>.99f,"Interior wall still faces outward");
            if(t.decal){decal_seen=true;require(t.points[0].GetZ()>-8,"Interior decal offset went outside the room");}
        }
        require(decal_seen,"Interior lost its decal");
        auto resized=decorated;resized.exit_point=resized.exit();
        require(resized.shell->set_footprint(3,3,error,true),"Interior edit bounds did not expand");resized.size=resized.shell->size;
        require(resized.validate(error) && std::abs(resized.exit().GetZ()-6.4f)<.001f,"Expanding edit bounds moved the exit outside the actual room");
        auto misplaced=resized;misplaced.exit_point.reset();require(!misplaced.validate(error),"Unsupported arrival/exit was accepted");
        decorated.exit_point=Vec3(-3,.08f,6);
        require(decorated.validate(error),"Relocating exit onto a clear floor failed");
        auto edited=*decorated.shell;
        require(edited.subdivide_face(2,error) && edited.attach_shape(4,BuildingShape::Gable,2,error),"Shared subdivision/roof tools failed for interior shell");
        require(decorated.save(directory/"library",error),"Interior library save failed");
        Interior from_library;
        require(Interior::load(directory/"library"/(room.id+".interior"),from_library,error) && from_library.shell
            && from_library.shell->decals.size()==1 && from_library.shell->faces[0].rotation==30
            && from_library.exit_point && (from_library.exit()-decorated.exit()).Length()<.001f,"Interior library lost shell, decals, UVs or exit position");
        const auto copy=duplicate_saved(decorated,directory/"library",std::vector<Interior>{decorated},error);
        require(copy && copy->id!=decorated.id && copy->shell && std::filesystem::exists(directory/"library"/(copy->id+".interior")),"Duplicating an interior overwrote the source identity");
        std::stringstream old_room;room.write(old_room,1);
        require(Interior::read(old_room,from_library,error,1) && !from_library.shell,"Version 21 interior data no longer loads");
        require(city.add_land({55,55},{78,78},error),"Land fixture failed");
        require(city.add_building({{62,62},1,10},error) && city.add_building({{66,62},1,8},error),"Building fixture failed");
        city.interiors.push_back(room);
        for (auto& b:city.buildings) b.entrance=BuildingEntrance{room.id,Vec3(0,0,8)};
        require(city.validate(error),error.c_str());
        {
            auto design=city.buildings[0].shape();design.name="Entrance design";design.entrance_configured=true;
            require(design.save(directory/"buildings",error),"Building entrance did not save");BuildingMesh loaded;
            require(BuildingMesh::load(directory/"buildings/building-Entrance design.building",loaded,error) && loaded.entrance
                && loaded.entrance->interior==room.id,"Saved building lost its entrance reference");
            City placed=city;placed.buildings.clear();
            require(placed.add_building({{62,62},1,10,loaded},error) && building_entrance(placed.buildings.back())
                && !placed.buildings.back().entrance,"Placing a saved building did not inherit its optional entrance");
            require(placed.add_building({{66,62},1,8},error) && !building_entrance(placed.buildings.back()),"A building without an entrance acquired a marker");
            for (bool freeform:{false,true}) {
                City modular=placed;if(freeform)modular.make_freeform();
                for (auto offset:{Vec3::sZero(),Vec3(0,0,400)}) {
                    modular.buildings[0].mesh->entrance->offset=offset;
                    const auto marker=entrance_position(modular,modular.buildings[0]);
                    require(offset.LengthSq()==0 ? modular.building_at(marker)==0 : !modular.land_at(marker),"Entrance placement fixture missed building or water");
                    require(modular.save(directory/"modular",error),"Building-owned marker required separate clear land when saving city");
                    City reopened;
                    require(City::load(directory/"modular"/(modular.id+".city"),reopened,error)
                        && building_entrance(reopened.buildings[0]) && !reopened.buildings[0].entrance
                        && (building_entrance(reopened.buildings[0])->offset-offset).Length()<.001f,"City round trip lost the building-owned marker");
                }
            }
            City linked=city;require(linked.update_building_design(loaded,error,0)>0 && building_entrance(linked.buildings[0]),"Building design entrance did not reach map instance");
            loaded.entrance.reset();loaded.entrance_configured=true;
            require(loaded.save(directory/"buildings",error),"Entrance removal did not save");BuildingMesh cleared;
            require(BuildingMesh::load(directory/"buildings/building-Entrance design.building",cleared,error) && !cleared.entrance && cleared.entrance_configured,"Saved entrance removal was lost");
            require(linked.update_building_design(loaded,error,0)>0 && !building_entrance(linked.buildings[0]),"Removing a design entrance restored a legacy instance marker");
            auto unrelated=room;unrelated.id="unrelated";
            require(!update_city_interiors(linked,{unrelated},error) && linked.interiors.size()==1,"Unreferenced library rooms were copied into a city");
        }
        const auto path=directory/(city.id+".city");
        for (bool freeform:{false,true}) {
            if (freeform) city.make_freeform();
            require(city.save(directory,error),error.c_str());
            const auto original=contents(path); City loaded;
            require(City::load(path,loaded,error),error.c_str());
            require(loaded.interiors.size()==1 && loaded.interiors[0].objects.size()==2 && loaded.interiors[0].designs.size()==1
                && loaded.buildings[0].entrance->interior==loaded.buildings[1].entrance->interior,"Shared template round trip duplicated or lost data");
            loaded.interiors[0].name="Updated shared house";
            require(loaded.buildings[0].entrance->interior==loaded.interiors[0].id && loaded.buildings[1].entrance->interior==loaded.interiors[0].id,
                "Renaming broke entrance references");
            auto invalid=city; invalid.buildings[0].entrance->interior="missing";
            require(!invalid.save(directory,error) && contents(path)==original,"Missing template overwrote valid save");
            invalid=city;invalid.buildings[0].entrance->offset=Vec3(std::numeric_limits<float>::quiet_NaN(),0,0);
            require(!invalid.validate(error),"Non-finite entrance offset accepted");
            invalid=city;invalid.interiors.push_back(room);require(!invalid.validate(error),"Duplicate template identity accepted");
            invalid=city;invalid.buildings[0].rotation=std::numeric_limits<int>::max();
            require(!invalid.validate(error),"Invalid building rotation reached marker transforms");
            if (freeform) {
                invalid=city;invalid.buildings[0].position=Vec3(std::numeric_limits<float>::quiet_NaN(),0,0);
                require(!invalid.validate(error),"Invalid building position reached marker terrain lookup");
            }
            for (const auto& corrupt:{original.substr(0,original.size()-6),original+"garbage\n"}) {
                {std::ofstream file(path);file<<corrupt;}
                const auto name=loaded.name;
                require(!City::load(path,loaded,error) && loaded.name==name && loaded.interiors[0].name=="Updated shared house","Corrupt save modified load destination");
            }
            require(city.save(directory,error),"Could not restore valid save");
            if(freeform){
                {std::ofstream file(path);file<<"AMBARETTO_CITY 21\n"<<city.id<<'\n'<<std::quoted(city.name)<<"\n1\n";city.write_paths(file,4);write_interiors(file,city,1);}
                City old_city;require(City::load(path,old_city,error) && old_city.interiors.size()==1 && !old_city.interiors[0].shell,"Version 21 city lost its reusable interiors");
                require(city.save(directory,error),"Could not restore current city format");
            }
        }
        const auto before=entrance_position(city,city.buildings[0]);
        auto moved=city.buildings[0];moved.position=*moved.position+Vec3(12,0,0);
        require((entrance_position(city,moved)-before-Vec3(12,0,0)).Length()<.001f,"Entrance did not follow building move");
        moved=city.buildings[0];moved.yaw=90;
        const auto local=entrance_position(city,moved)-moved.base(City::level);
        require(std::abs(local.GetX()-8)<.001f && std::abs(local.GetZ())<.001f,"Entrance did not follow building rotation");
        City legacy=city;legacy.interiors.clear();for (auto& b:legacy.buildings)b.entrance.reset();
        require(legacy.save(directory,error),error.c_str());City loaded;
        require(City::load(path,loaded,error) && loaded.interiors.empty() && !loaded.buildings[0].entrance,"Legacy map compatibility failed");
        {
            PhysicsWorld world(false);std::vector<std::array<Vec3,3>> collision;
            for (const auto& t:room.triangles()) if (!t.decal) collision.push_back(t.points);
            world.add_static_mesh(collision); GroundHit hit;
            require(world.cast_ray(Vec3(0,2,0),Vec3::sAxisX(),10,hit) && std::abs(hit.point.GetX()-8)<.01f,"Room wall collision missing");
            require(world.cast_ray(Vec3(3,3,-3),-Vec3::sAxisY(),5,hit) && std::abs(hit.point.GetY()-.8f)<.01f,"Furniture collision missing");
            Character player(world,nullptr,true);player.reset(room.spawn());
            for (int i=0;i<600;++i) {world.step();FootInput walking;walking.direction=Vec3::sAxisX();player.step(walking);}
            require(player.position().GetX()<8 && player.position().GetX()>6 && player.position().GetY()>-.1f,"Interior player escaped through walls/floor");
        }
        std::filesystem::remove_all(directory);
        std::cout<<"Interiors: shared references, object restrictions, clear exits, grid/freeform saves, corruption, marker transforms and collision passed\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
