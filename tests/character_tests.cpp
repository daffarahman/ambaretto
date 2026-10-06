#include "character_renderer.hpp"
#include "city.hpp"
#include "environment.hpp"
#include "pedestrians.hpp"
#include "traffic.hpp"
#include "police.hpp"
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace ambaretto;
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
// A tiny GLB fixture; real character assets are supplied by the artist.
void model(const std::filesystem::path& path) {
    Mesh mesh=GenMeshCube(1,2,1);
    const int count=mesh.triangleCount*3; std::vector<float> positions,normals;
    for (int i=0;i<count;++i) {
        const int v=mesh.indices ? mesh.indices[i] : i;
        positions.insert(positions.end(),mesh.vertices+v*3,mesh.vertices+v*3+3);
        normals.insert(normals.end(),mesh.normals+v*3,mesh.normals+v*3+3);
    }
    const std::size_t bytes=positions.size()*sizeof(float);
    std::ostringstream json;
    json<<R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.85,0.3,0.1,1]}}],"buffers":[{"byteLength":)"<<bytes*2
        <<R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)"<<bytes<<R"(},{"buffer":0,"byteOffset":)"<<bytes<<R"(,"byteLength":)"<<bytes
        <<R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":)"<<count<<R"(,"type":"VEC3","min":[-0.5,-1,-0.5],"max":[0.5,1,0.5]},{"bufferView":1,"componentType":5126,"count":)"<<count<<R"(,"type":"VEC3"}]})";
    auto text=json.str(); while (text.size()%4) text+=' ';
    const std::uint32_t header[]={0x46546c67,2,std::uint32_t(28+text.size()+bytes*2),std::uint32_t(text.size()),0x4e4f534a},binary[]={std::uint32_t(bytes*2),0x004e4942};
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(header),sizeof(header)); file.write(text.data(),text.size());
    file.write(reinterpret_cast<const char*>(binary),sizeof(binary)); file.write(reinterpret_cast<const char*>(positions.data()),bytes); file.write(reinterpret_cast<const char*>(normals.data()),bytes);
    UnloadMesh(mesh); require(bool(file),"Cannot write model fixture");
}
}
int main() {
    const auto previous=std::filesystem::current_path();
    const auto root=std::filesystem::temp_directory_path()/("ambaretto-character-"+City::create("test").id);
    SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(1024,600,"Character checks");
    try {
        std::filesystem::create_directories(root); std::filesystem::current_path(root);
        std::string error; CharacterDesign player; player.name="Test player";
        for (auto& part:player.parts) part.model="part.glb";
        player.parts[3].offset={.08f,.02f,-.03f}; player.parts[3].rotation={23,-17,12};
        require(player.validate(error) && player.save(root/"characters",error),"Character save failed");
        CharacterDesign loaded;
        require(CharacterDesign::load(root/"characters/character-Test player.character",loaded,error) && loaded==player,"Character round trip lost parts or transforms");
        auto bad=player; bad.parts[2].model="../head.glb";
        require(!bad.validate(error) && !bad.save(root/"characters",error),"Model traversal accepted");
        bad=player; bad.parts[6].scale[0]=0; require(!bad.validate(error),"Collapsed part accepted");
        bad=player; bad.type=CharacterType(99); require(!bad.validate(error),"Invalid character role accepted");
        require(CharacterDesign::load(root/"characters/character-Test player.character",loaded,error) && loaded==player,"Rejected save damaged previous character");
        City city=City::create("Character city");
        require(city.add_land({58,58},{72,72},error) && city.add_road(City::road_stroke({60,64},{70,64}),error) && city.set_spawn({64,62},error),"Cannot build character test city");
        require(!city.playable(),"Map playable without player data");
        require(city.save(root/"cities",error) && City::load(root/"cities"/(city.id+".city"),city,error) && !city.player_character,"Legacy map manufactured a player");
        city.player_character=player;
        require(city.playable() && city.save(root/"cities",error),"Map with player cannot play or save");
        City restored;
        require(City::load(root/"cities"/(city.id+".city"),restored,error) && restored.player_character && *restored.player_character==player,"Map lost selected player");
        auto npc=player; npc.name="Civilian"; npc.type=CharacterType::NPC;
        auto officer=player; officer.name="Officer"; officer.type=CharacterType::Police;
        std::vector<CharacterDesign> designs{player,npc,officer},empty;
        require(character_for_type(designs,CharacterType::NPC,40)->name==npc.name && !character_for_type(empty,CharacterType::Police),"Character role selection failed");
        require(npc.save(root/"characters",error) && officer.save(root/"characters",error) && saved_characters(root/"characters",error).size()==3,"Character library failed");
        const auto original=player; player.parts[1].offset[1]=.1f;
        require(restored.update_player_character({player}) && restored.player_character->parts[1].offset[1]==.1f,"Saved player edit did not refresh map");
        require(!restored.update_player_character({npc}),"NPC design replaced the map player");
        CharacterRenderer renderer; require(!renderer.available(restored,error),"Missing GLBs accepted for play");
        for (const char* folder:character_slot_folders) {
            const auto path=root/"assets/character"/folder;
            std::filesystem::create_directories(path); model(path/"part.glb");
        }
        renderer.refresh(); require(renderer.available(restored,error),"Valid GLB character unavailable");
        std::ofstream(root/"assets/character/head/part.glb",std::ios::trunc)<<"broken";
        renderer.refresh(); require(!renderer.available(restored,error),"Corrupt GLB accepted");
        model(root/"assets/character/head/part.glb"); renderer.refresh();
        PhysicsWorld flat(false); Character character(flat); character.reset({0,.08f,0}); character.set_design(original);
        const auto before=character.body_parts(); const auto& component=original.parts[3];
        const Vector3 source{.18f,.35f,-.25f},center{0,0,0},size{1,2,1};
        auto left=before[3],right=before[6]; left.rotation=right.rotation=Quat::sRotation(Vec3::sAxisY(),.7f);
        const auto a=Vector3Transform(source,character_component_transform(component,left,center,size,false));
        const auto b=Vector3Transform(source,character_component_transform(component,right,center,size,true));
        const Vec3 local_a=left.rotation.Conjugated()*(Vec3(a.x,a.y,a.z)-left.position),local_b=right.rotation.Conjugated()*(Vec3(b.x,b.y,b.z)-right.position);
        require((local_b-Vec3(-local_a.GetX(),local_a.GetY(),local_a.GetZ())).Length()<.00001f,"Mirroring lost rotation or local offset symmetry");
        for (int i:{6,7,8,12,13,14}) require(character_body_mirrored[i] && character_body_slots[i]==character_body_slots[i-3],"A paired limb or shoe does not share its model");
        character.ragdoll({2,1,0}); for (int i=0;i<240;++i) {flat.step(); character.step({});}
        require(character.design() && *character.design()==original && std::isfinite(character.position().GetY()),"Ragdoll lost its design or became unstable");
        Camera3D camera{{2,2,-4},{0,.9f,0},{0,1,0},2.5f,CAMERA_ORTHOGRAPHIC};
        SceneLighting lighting; GraphicsSettings graphics; graphics.shadows=0; DayNight day;
        renderer.set_lighting(lighting,camera,day.lighting(),graphics);
        const auto target=LoadRenderTexture(256,256); character.reset({0,.08f,0});
        BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera); renderer.draw(character,camera); EndMode3D(); EndTextureMode();
        const auto image=LoadImageFromTexture(target.texture); auto* pixels=LoadImageColors(image); int visible=0;
        for (int i=0;i<image.width*image.height;++i) if (pixels[i].r>20 && pixels[i].r>pixels[i].g*1.3f) ++visible;
        UnloadImageColors(pixels); UnloadImage(image); UnloadRenderTexture(target); require(visible>150,"Imported character did not render");
        const Environment environment(restored); PhysicsWorld world(environment); Car car(world);
        CarDesign vehicle; vehicle.name="Test car"; vehicle.body=vehicle.wheel="test.glb";
        auto patrol=vehicle; patrol.type=CarType::Police; std::vector<CarDesign> cars{vehicle,patrol};
        Traffic no_traffic(world,environment,&cars,&empty); Pedestrians nobody(world,environment,&empty);
        Police no_police(world,environment,&no_traffic,&nobody,&cars,&empty); Player controlled(world,car,environment,nullptr,&no_traffic,&nobody,nullptr,&no_police);
        require(nobody.people().empty() && no_traffic.cars().empty(),"Empty NPC library spawned civilians or traffic drivers");
        no_police.crime(Crime::OfficerHomicide,controlled.position());
        for (int i=0;i<300;++i) {no_police.prepare(controlled); world.step(); no_police.finish(controlled);}
        require(std::none_of(no_police.units().begin(),no_police.units().end(),[](const auto& unit){return unit.active;}),"Empty Police library dispatched a patrol");
        Pedestrians people(world,environment,&designs); Traffic traffic(world,environment,&cars,&designs); Police police(world,environment,&traffic,&people,&cars,&designs);
        require(!people.people().empty() && !traffic.cars().empty(),"NPC designs did not populate existing spawn systems");
        for (const auto& person:people.people()) require(person.character->design() && person.character->design()->name==npc.name,"Pedestrian changed prefab appearance");
        for (const auto& unit:police.units()) for (const auto& person:unit.officers) require(person.character->design() && person.character->design()->name==officer.name,"Officer has wrong appearance");
        auto& stolen=*traffic.cars().front().car; stolen.set_simulated(true);
        require(traffic.steal(stolen) && traffic.cars().front().driver && traffic.cars().front().driver->design()->name==npc.name,"Stolen-car ragdoll lost the NPC design");
        std::cout<<"Characters: saves, legacy maps, selection, missing/corrupt assets, mirroring, rendering, ragdolls and typed spawning passed\n";
    } catch (const std::exception& error) {
        std::filesystem::current_path(previous); std::cerr<<error.what()<<'\n'; CloseWindow(); return 1;
    }
    std::filesystem::current_path(previous); std::filesystem::remove_all(root); CloseWindow(); return 0;
}
