#include "city.hpp"
#include "environment.hpp"
#include "pedestrians.hpp"
#include "police.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <limits>
#include <iomanip>

namespace {
using namespace ambaretto;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
City fixture() {
    City city = City::create("Block City"); std::string error;
    require(city.add_land({42,42},{87,87},error),"island creation failed");
    for (auto z : {52,64,76}) require(city.add_road(City::road_stroke({46,z},{83,z}),error),"horizontal road failed");
    for (auto x : {52,64,76}) require(city.add_road(City::road_stroke({x,46},{x,83}),error),"vertical road failed");
    require(city.add_building({{55,55},2,16},error),"building failed");
    require(city.add_building({{68,55},3,28},error),"building failed");
    require(city.add_building({{55,69},2,12},error),"building failed");
    require(city.add_vehicle({CityVehicleKind::Car,{61,61},1},error),"car placement failed");
    require(city.add_vehicle({CityVehicleKind::Trainer,{64,80},2},error),"plane placement failed");
    require(city.add_vehicle({CityVehicleKind::F18,{46,80},0},error),"jet placement failed");
    require(city.set_spawn({60,61},error),"spawn placement failed");
    require(city.brush_trees(City::center({60,58}),32,2,false,error)>10,"tree brush failed");
    city.start_minutes = 7*60+30;
    return city;
}
void building_meshes(const std::filesystem::path& directory) {
    BuildingMesh mesh; std::string error;
    mesh.name = "Textured shop"; mesh.texture = "brick.png"; mesh.size = Vec3(20,16,8);
    mesh.vertices[2].SetY(.75f); mesh.vertices[6].SetY(.75f);
    mesh.decals.push_back({0,"window.png",.2f,.1f,.6f,.8f});
    require(mesh.validate(error),error.c_str());
    const auto faces = mesh.triangles(); require(faces.size()>12,"decal did not generate a face overlay");
    float decal_area = 0;
    for (std::size_t i = 12; i<faces.size(); ++i) {
        const auto& t = faces[i];
        const Vec3 normal = (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]);
        require(t.decal && t.texture=="window.png" && normal.GetZ()<0,"decal winding points inside the building");
        for (const auto& uv : t.uv) require(uv[0]>=-.0001f && uv[0]<=1.0001f && uv[1]>=-.0001f && uv[1]<=1.0001f,"decal UVs escaped the texture");
        const auto& a = t.uv[0]; const auto& b = t.uv[1]; const auto& c = t.uv[2];
        decal_area += std::abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2;
    }
    require(std::abs(decal_area-1)<.001f,"decal clipping left a hole or overlapped its diagonal");
    BuildingMesh resized = mesh; resized.size = Vec3(40,32,16);
    require((resized.points()[2]-mesh.points()[2]*2).Length()<.001f,"resize lost the edited corner shape");
    const auto library = directory/"buildings";
    require(mesh.save(library,error),error.c_str());
    const auto path = library/("building-"+mesh.name+".building"); BuildingMesh read;
    require(BuildingMesh::load(path,read,error) && read.texture==mesh.texture && read.vertices==mesh.vertices
        && read.size==mesh.size && read.decals[0].height==.8f,"building save lost mesh or decal data");
    require(resized.save(library,error) && BuildingMesh::load(path,read,error) && read.size==resized.size,"building overwrite failed");
    BuildingMesh invalid = mesh; invalid.vertices[2] = invalid.vertices[1];
    require(!invalid.save(library,error) && BuildingMesh::load(path,read,error) && read.size==resized.size,"bad vertices damaged the previous building save");
    invalid = mesh; invalid.texture = "../brick.png"; require(!invalid.validate(error),"texture traversal accepted");
    invalid = mesh; invalid.decals[0].face = 6; require(!invalid.validate(error),"invalid decal face accepted");
    invalid = mesh; invalid.decals[0].width = .9f; require(!invalid.validate(error),"decal outside its face accepted");
    invalid = mesh; invalid.size.SetX(std::numeric_limits<float>::quiet_NaN()); require(!invalid.validate(error),"nonfinite dimensions accepted");
    const auto textures = directory/"textures"; std::filesystem::create_directory(textures);
    std::ofstream(textures/"z.PNG"); std::ofstream(textures/"a.jpg"); std::ofstream(textures/"notes.txt"); std::filesystem::create_directory(textures/"fake.png");
    require(building_textures(textures,error)==std::vector<std::string>{"a.jpg","z.PNG"},"texture discovery includes unsupported files or is unsorted");
    std::ofstream(textures/"new.png"); require(building_textures(textures,error).size()==3,"new texture was not discovered on refresh");
    require(saved_buildings(library,error).size()==1,"saved building missing from library");
    City city = City::create("Custom buildings");
    require(city.add_land({60,60},{68,68},error),"custom building island failed");
    CityBuilding placed{{61,61},1,12,mesh}; require(placed.footprint()==CityCell{2,1},"rectangular footprint is still square");
    require(city.add_building(placed,error) && city.add_building({{65,65},1,12},error),"mixed custom and plain placement failed");
    require(city.building_at({62,61})==0 && city.building_at({61,62})==-1,"mesh footprint disagrees with selection");
    require(!city.add_building({{62,61},1,12},error) && !city.add_road({{62,61}},error) && !city.set_spawn({62,61},error),"mesh footprint overlaps structures, roads or spawn");
    require(!city.add_building({{127,127},1,12,mesh},error),"mesh was placed outside the map");
    require(city.set_spawn({67,67},error) && city.save(directory,error),error.c_str());
    City loaded; require(City::load(directory/(city.id+".city"),loaded,error),error.c_str());
    require(loaded.buildings[0].mesh && !loaded.buildings[1].mesh && loaded.buildings[0].mesh->vertices==mesh.vertices
        && loaded.buildings[0].mesh->decals[0].texture=="window.png","city lost embedded building geometry or decals");
    // Collision follows the sloping roof rather than its old enclosing box.
    Environment map(loaded); PhysicsWorld world(map); GroundHit hit;
    const Vec3 base = placed.base(City::level), roof = base+Vec3(0,14,0);
    require(world.cast_ray(roof+Vec3(0,10,0),-Vec3::sAxisY(),20,hit) && (hit.point-roof).Length()<.01f,"edited roof collision still uses a box");
    const Vec3 wall = base+Vec3(0,6,-4);
    require(world.cast_ray(wall+Vec3(0,0,-5),Vec3::sAxisZ(),10,hit) && (hit.point-wall).Length()<.01f,"custom wall collision missing");
    require(loaded.add_building({{61,61},1,12,resized},error,0) && loaded.building_at({64,62})==0,"resized mesh replacement lost its expanded footprint");
    require(loaded.erase({64,62},error) && loaded.buildings.size()==1,"bulldoze did not remove a custom mesh from its footprint");
    std::filesystem::remove(path); std::filesystem::remove(library);
    for (const auto& entry : std::filesystem::directory_iterator(textures)) std::filesystem::remove(entry.path());
    std::filesystem::remove(textures);
}
void building_mesh_editing(const std::filesystem::path& directory) {
    std::string error;
    const auto serialized = [](const BuildingMesh& mesh) { std::ostringstream s; mesh.write(s); return s.str(); };
    const auto decal_area = [](const BuildingMesh& mesh) {
        float area = 0;
        for (const auto& t : mesh.triangles()) if (t.decal) {
            const auto& a = t.uv[0]; const auto& b = t.uv[1]; const auto& c = t.uv[2];
            area += std::abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2;
        }
        return area;
    };
    BuildingMesh mesh; mesh.name = "Mesh editing"; mesh.texture = "brick.png";
    mesh.size = Vec3(12,10,8); mesh.decals.push_back({4,"window.png",.05f,.05f,.9f,.9f});
    const BuildingMesh cube = mesh;
    require(mesh.inset_face(4,.2f,error),error.c_str());
    require(mesh.vertices.size()==12 && mesh.faces.size()==10 && std::abs(decal_area(mesh)-1)<.001f,
        "inset lost its ring, original texture coordinates or decal coverage");
    require(mesh.extrude_face(4,3,error) && mesh.size.GetY()==13 && mesh.vertices.size()==16 && mesh.faces.size()==14,
        "extrusion did not create a raised cap with connected sides");
    const auto cap = mesh.faces[4].vertices;
    const auto before_move = mesh.points();
    require(mesh.move_vertices(cap,Vec3(4,2,0),error),error.c_str());
    const auto after_move = mesh.points();
    require(std::abs(mesh.size.GetY()-15)<.001f && mesh.size.GetX()==12 && std::abs((after_move[cap[0]]-after_move[0]).GetX()
        -(before_move[cap[0]]-before_move[0]).GetX()-1.2f)<.001f,"moving new vertices escaped the footprint or lost their relative shape");
    const int a = mesh.faces[4].vertices[0], b = mesh.faces[4].vertices[1];
    const int split = mesh.split_edge(a,b,error);
    require(split>=0 && std::count_if(mesh.faces.begin(),mesh.faces.end(),[&](const auto& f) {
        return std::find(f.vertices.begin(),f.vertices.end(),split)!=f.vertices.end();
    })==2,"edge split left a T junction in its neighboring face");
    const float area = decal_area(mesh);
    require(mesh.subdivide_face(4,error),error.c_str());
    require(mesh.vertices.size()>17 && mesh.faces.size()>14 && std::abs(decal_area(mesh)-area)<.001f,
        "face subdivision changed UV/decal coverage or did not add geometry");
    const std::string stable = serialized(mesh);
    require(!mesh.extrude_face(-1,1,error) && mesh.split_edge(-1,2,error)<0 && !mesh.move_vertices({9999},Vec3(1,0,0),error)
        && !mesh.move_vertices({0},Vec3(1000,0,0),error) && serialized(mesh)==stable,"failed mesh edit partially changed geometry");
    BuildingMesh invalid = mesh; invalid.faces[0].vertices[0] = 9999;
    require(!invalid.validate(error),"out-of-range face index accepted");
    invalid = mesh; invalid.faces[0].uv.pop_back(); require(!invalid.validate(error),"incomplete UVs accepted");
    invalid = mesh; invalid.faces.push_back(invalid.faces[0]); require(!invalid.validate(error),"nonmanifold geometry accepted");
    const auto path = directory/"building-Mesh editing.building";
    require(mesh.save(directory,error),error.c_str()); BuildingMesh read;
    require(BuildingMesh::load(path,read,error) && serialized(read)==stable,"dynamic topology or UVs did not round-trip");
    std::ofstream(path) << "AMBARETTO_BUILDING 2\n\"bad\" \"\" 1 1 1\n4097 1\n";
    require(!BuildingMesh::load(path,read,error) && serialized(read)==stable,"invalid mesh count mutated loaded output");
    require(mesh.save(directory,error),error.c_str());
    BuildingMesh open = cube;
    require(open.remove_faces({4},error) && open.add_face({3,7,6,2},error) && open.validate(error),"make face did not close an open roof");
    const int loose = open.add_vertex(Vec3(0,14,0),error);
    require(loose==8 && open.remove_vertices({loose},error) && open.vertices.size()==8,"add/delete loose vertex failed");
    require(open.remove_vertices({2},error) && open.faces.size()==3,"delete vertex did not remove its incident faces");
    std::vector<int> all; for (int i = 0; i<int(open.faces.size()); ++i) all.push_back(i);
    const auto open_before = serialized(open);
    require(!open.remove_faces(all,error) && serialized(open)==open_before,"deleting the last face destroyed the mesh");
    // A concave L-shaped face must retain its cutout instead of filling it with a triangle fan.
    BuildingMesh concave; concave.vertices = {Vec3(-.5f,0,-.5f),Vec3(.5f,0,-.5f),Vec3(.5f,0,0),
        Vec3(0,0,0),Vec3(0,0,.5f),Vec3(-.5f,0,.5f)};
    concave.faces = {{{5,4,3,2,1,0},{{0,1},{.5f,1},{.5f,.5f},{1,.5f},{1,0},{0,0}}}};
    concave.size = Vec3(10,1,10);
    require(concave.validate(error) && concave.triangles().size()==4,"concave face cannot be triangulated");
    float surface = 0;
    for (const auto& t : concave.triangles()) surface += (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).Length()/2;
    require(std::abs(surface-75)<.001f && concave.extrude_face(0,4,error) && concave.validate(error),"concave extrusion filled its cutout");
    const auto legacy_payload = [&]() {
        std::ostringstream s; s << std::setprecision(std::numeric_limits<float>::max_digits10)
            << std::quoted(cube.name) << ' ' << std::quoted(cube.texture) << " 12 10 8\n";
        for (auto p : cube.vertices) s << p.GetX() << ' ' << p.GetY() << ' ' << p.GetZ() << '\n';
        s << "1\n4 \"window.png\" " << .05f << ' ' << .05f << ' ' << .9f << ' ' << .9f << '\n'; return s.str();
    };
    const auto legacy_path = directory/"legacy.building";
    std::ofstream(legacy_path) << "AMBARETTO_BUILDING 1\n" << legacy_payload();
    require(BuildingMesh::load(legacy_path,read,error) && serialized(read)==serialized(cube),"version 1 building did not migrate to polygon faces");
    City city = City::create("Topology city"); require(city.add_land({60,60},{68,68},error),error.c_str());
    require(city.add_building({{62,62},1,12,mesh},error) && city.save(directory,error),error.c_str());
    const auto city_path = directory/(city.id+".city"); City loaded;
    require(City::load(city_path,loaded,error) && serialized(*loaded.buildings[0].mesh)==stable,"city lost edited topology");
    Environment map(loaded); PhysicsWorld world(map); GroundHit hit;
    const auto points = mesh.points(); Vec3 top = Vec3::sZero(); for (int v : cap) top += points[v]/float(cap.size());
    top += loaded.buildings[0].base(City::level);
    require(world.cast_ray(top+Vec3(0,5,0),-Vec3::sAxisY(),10,hit) && (hit.point-top).Length()<.01f,
        "extruded/subdivided geometry and gameplay collision disagree");
    require(city.add_building({{62,62},1,12,cube},error,0) && city.save(directory,error),error.c_str());
    std::ifstream file(city_path); std::ostringstream contents; contents << file.rdbuf(); file.close(); auto legacy = contents.str();
    const auto payload = serialized(cube); const auto offset = legacy.find(payload);
    require(offset!=std::string::npos,"missing embedded cube payload");
    legacy.replace(offset,payload.size(),legacy_payload()); legacy.replace(0,16,"AMBARETTO_CITY 7");
    std::ofstream(city_path) << legacy;
    require(City::load(city_path,loaded,error) && serialized(*loaded.buildings[0].mesh)==serialized(cube),"version 7 city mesh did not migrate");
    BuildingMesh sliced; sliced.name = "Sliced roof";
    require(sliced.set_footprint(2,3,error) && sliced.footprint()==std::array<int,2>{2,3},"rectangular tile footprint did not update");
    sliced.decals.push_back({0,"window.png",0,0,1,1});
    require(sliced.slice(1,6,error) && sliced.vertices.size()==12 && sliced.faces.size()==10,
        "cube slice did not add a connected four-vertex loop");
    require(sliced.slice(0,0,error) && sliced.validate(error) && std::abs(decal_area(sliced)-1)<.001f,
        "intersecting slices lost connected topology or decal coverage");
    const auto roof_before = sliced.points();
    require(sliced.move_vertices({3,7},Vec3(4,0,0),error),error.c_str());
    const auto trapezoid = sliced.points();
    require(std::abs(trapezoid[3].GetX()-roof_before[3].GetX()-4)<.001f && sliced.footprint()==std::array<int,2>{2,3}
        && trapezoid[3].GetX()>trapezoid[0].GetX(),"sliced cube could not become a sloping trapezoid inside its footprint");
    const int extra = sliced.add_vertex(Vec3(0,1,0),error);
    require(extra>=0 && sliced.move_vertices({extra},Vec3(10000,10000,-10000),error),"clamped movement failed");
    auto clamped = sliced.points()[extra];
    require(std::abs(clamped.GetX()-BuildingMesh::tile_size)<.001f && std::abs(clamped.GetZ()+BuildingMesh::tile_size*1.5f)<.001f
        && clamped.GetY()==120 && sliced.footprint()==std::array<int,2>{2,3},"movement left the footprint or exceeded max height");
    require(sliced.move_vertices({extra},Vec3(0,-10000,0),error) && sliced.points()[extra].GetY()==0,"vertex moved below the ground");
    for (auto p : sliced.points()) require(std::abs(p.GetX())<=BuildingMesh::tile_size+.001f
        && std::abs(p.GetZ())<=BuildingMesh::tile_size*1.5f+.001f && p.GetY()>=0 && p.GetY()<=120,"new slice vertex escaped clamping bounds");
    const auto intact = serialized(sliced);
    require(!sliced.slice(4,0,error) && !sliced.slice(1,-5,error) && serialized(sliced)==intact,"failed slice partially changed the mesh");
    require(sliced.save(directory,error) && BuildingMesh::load(directory/"building-Sliced roof.building",read,error)
        && serialized(read)==intact && read.footprint()==std::array<int,2>{2,3},"sliced footprint did not round-trip in the existing format");
    require(concave.slice(0,-2,error) && concave.validate(error),"concave extrusion could not be sliced");
    BuildingMesh migrated = cube; const auto physical = migrated.points(); const auto old_tiles = migrated.footprint();
    require(migrated.set_footprint(old_tiles[0],old_tiles[1],error,true),"legacy footprint did not align to tiles");
    for (std::size_t i = 0; i<physical.size(); ++i) require((migrated.points()[i]-physical[i]).Length()<.001f,"footprint alignment moved legacy geometry");
    require(migrated.set_footprint(128,128,error) && migrated.footprint()==std::array<int,2>{128,128},"map-sized N x M footprint failed");
    require(!migrated.set_footprint(129,1,error),"out-of-map footprint accepted");
    std::filesystem::remove(directory/"building-Sliced roof.building");
    std::filesystem::remove(path); std::filesystem::remove(legacy_path);
}
void building_design_updates(const std::filesystem::path& directory) {
    std::string error; City city = City::create("Updated designs"); BuildingMesh design;
    design.name = "Shared shop"; design.texture = "soil.png";
    BuildingMesh other = design; other.name = "Other shop";
    require(city.add_land({60,60},{68,68},error) && city.add_building({{60,60},1,12,design},error)
        && city.add_building({{62,60},1,12,design},error) && city.add_building({{66,66},1,12,other},error)
        && city.add_building({{68,68},1,12},error),"design update placements failed");
    const auto original = city.buildings;
    design.texture = "brick.png"; design.decals.push_back({0,"window.png"});
    require(design.slice(1,6,error) && design.move_vertices({2,6},Vec3(0,3,0),error)
        && design.set_footprint(2,2,error,true),"design update edit failed");
    require(city.update_building_design(design,error)==2,"design edit did not update every placed copy");
    for (int i = 0; i<2; ++i) require(city.buildings[i].cell==original[i].cell && city.buildings[i].footprint()==CityCell{2,2}
        && city.buildings[i].mesh->vertices==design.vertices && city.buildings[i].mesh->texture=="brick.png"
        && city.buildings[i].mesh->faces.size()==design.faces.size() && city.buildings[i].mesh->decals.size()==design.decals.size(),"placed copy lost edited geometry, texture, decal or footprint");
    require(city.buildings[2].mesh->vertices==other.vertices && city.buildings[2].mesh->texture==other.texture
        && !city.buildings[3].mesh,"design edit changed another design or plain building");
    require(city.update_building_design(design,error)==0,"unchanged design created another update");
    BuildingMesh expanded = design; require(expanded.set_footprint(3,2,error,true),"larger design failed");
    require(city.update_building_design(expanded,error)==-1 && city.buildings[0].footprint()==CityCell{2,2}
        && city.buildings[1].footprint()==CityCell{2,2},"overlapping design update partially changed the map");
    expanded.name = "Unused design";
    require(city.update_building_design(expanded,error)==0 && city.update_building_design(design,error,9999)==-1,"invalid or unmatched design replacement accepted");
    require(city.save(directory,error),"updated instances did not save"); City loaded;
    const auto path = directory/(city.id+".city");
    require(City::load(path,loaded,error) && loaded.buildings[0].mesh->vertices==design.vertices
        && loaded.buildings[1].mesh->texture==design.texture && loaded.buildings[1].footprint()==CityCell{2,2},"design update did not survive city save/load");
    Environment map(loaded); PhysicsWorld world(map); GroundHit hit;
    for (int i = 0; i<2; ++i) {
        const auto roof = loaded.buildings[i].base(City::level)+Vec3(0,13.5f,0);
        require(world.cast_ray(roof+Vec3(0,10,0),-Vec3::sAxisY(),20,hit) && (hit.point-roof).Length()<.01f,"updated design did not reach instance collision");
    }
    BuildingMesh plain; plain.name = "Converted plain";
    require(city.update_building_design(plain,error,3)==1 && city.buildings[3].mesh
        && city.buildings[3].mesh->name==plain.name,"editing a selected plain building did not convert it");
    std::filesystem::remove(path);
}
void building_rotation(const std::filesystem::path& directory) {
    std::string error; City city = City::create("Rotated buildings"); BuildingMesh mesh;
    mesh.name = "Sloped shop"; mesh.size = Vec3(20,16,8); mesh.texture = "brick.png";
    mesh.vertices[2].SetY(.75f); mesh.vertices[6].SetY(.75f);
    mesh.decals.push_back({0,"window.png"});
    require(city.add_land({60,60},{68,68},error),error.c_str());
    const auto triangles = mesh.triangles();
    const auto path = directory/(city.id+".city"); City loaded;
    for (int rotation = 0; rotation<4; ++rotation) {
        CityBuilding placed{{61,61},1,12,mesh,rotation};
        require(placed.footprint()==(rotation%2 ? CityCell{1,2} : CityCell{2,1}),"rotation did not swap rectangular footprint");
        require(city.add_building(placed,error,city.buildings.empty() ? -1 : 0),error.c_str());
        require(city.building_at({61,61})==0 && city.building_at(rotation%2 ? CityCell{61,62} : CityCell{62,61})==0
            && city.building_at(rotation%2 ? CityCell{62,61} : CityCell{61,62})==-1,"rotated occupied tiles disagree with placement");
        const float angle = rotation*3.14159265f/2;
        const auto transformed = [&](Vec3 p) { return placed.base(City::level)+Vec3(p.GetX()*std::cos(angle)+p.GetZ()*std::sin(angle),
            p.GetY(),p.GetZ()*std::cos(angle)-p.GetX()*std::sin(angle)); };
        const auto points = placed.points(City::level); const auto world_triangles = placed.triangles(City::level);
        for (std::size_t i = 0; i<points.size(); ++i) require((points[i]-transformed(mesh.points()[i])).Length()<.001f,"rotation lost edited mesh vertices");
        for (std::size_t i = 0; i<triangles.size(); ++i) {
            require(world_triangles[i].uv==triangles[i].uv && world_triangles[i].texture==triangles[i].texture
                && world_triangles[i].decal==triangles[i].decal,"rotation changed textures or decal UVs");
            for (int j = 0; j<3; ++j) require((world_triangles[i].points[j]-transformed(triangles[i].points[j])).Length()<.001f,"rotation missed face/decal geometry");
        }
        Environment map(city); PhysicsWorld world(map); GroundHit hit;
        require(std::abs(map.buildings()[0].size.GetX()-(rotation%2 ? 8 : 20))<.001f,"environment bounds missed rectangular rotation");
        const Vec3 roof = transformed(Vec3(5,13,0)), wall = transformed(Vec3(0,6,-4)), outside = transformed(Vec3(0,6,-9));
        require(world.cast_ray(roof+Vec3(0,10,0),-Vec3::sAxisY(),20,hit) && (hit.point-roof).Length()<.01f,"rotated sloped roof collision disagrees with mesh");
        require(world.cast_ray(outside,(wall-outside).Normalized(),10,hit) && (hit.point-wall).Length()<.01f,"rotated wall collision missing");
        require(city.save(directory,error) && City::load(path,loaded,error) && loaded.buildings[0].rotation==rotation
            && loaded.buildings[0].mesh->vertices==mesh.vertices,"rotation did not survive city save/load or legacy version 8 changed");
    }
    mesh.texture = "soil.png";
    require(city.update_building_design(mesh,error)==1 && city.buildings[0].rotation==3,"design update reset instance rotation");
    City blocked = city; require(blocked.add_building({{62,61},1,12},error),error.c_str());
    auto rotated = city.buildings[0]; rotated.rotation = 0;
    require(!blocked.add_building(rotated,error,0) && blocked.buildings[0].rotation==3,"overlapping rotation partially changed the building");
    blocked = city; require(blocked.add_road({{62,61}},error),error.c_str());
    require(!blocked.add_building(rotated,error,0),"rotation overlapped a road");
    blocked = city; require(blocked.set_spawn({62,61},error),error.c_str());
    require(!blocked.add_building(rotated,error,0),"rotation overlapped player spawn");
    blocked = city; require(blocked.add_vehicle({CityVehicleKind::Car,{62,61},0},error),error.c_str());
    require(!blocked.add_building(rotated,error,0),"rotation overlapped a parked vehicle");
    City edge = City::create("Map edge"); require(edge.add_land({126,125},{127,127},error),error.c_str());
    rotated.cell = {127,125}; rotated.rotation = 1; require(edge.add_building(rotated,error),error.c_str());
    rotated.rotation = 0; require(!edge.add_building(rotated,error,0) && edge.buildings[0].rotation==1,"rotation escaped the map edge");
    rotated.rotation = 4; require(!edge.add_building(rotated,error,0),"invalid rotation accepted");
    city.buildings.clear(); require(city.add_building({{65,65},1,12,{},1},error) && city.save(directory,error)
        && City::load(path,loaded,error) && !loaded.buildings[0].mesh && loaded.buildings[0].rotation==1,"plain building version 9 lost rotation");
    std::filesystem::remove(path);
}
void generated_terrain(const std::filesystem::path& directory) {
    std::string error; City city = City::create("Perlin island"), same = city, different = city;
    require(city.generate_terrain(48,32,1234,error) && city.validate(error),error.c_str());
    require(same.generate_terrain(48,32,1234,error) && same.tiles==city.tiles && same.elevation==city.elevation,"same seed produced different terrain");
    require(different.generate_terrain(48,32,1235,error) && different.tiles!=city.tiles && different.elevation==city.elevation,"new seed did not change only the land/water layout");
    require(city.land_count()>100 && city.land_count()<48*32 && std::all_of(city.elevation.begin(),city.elevation.end(),[](auto h){return h==0;}),"Perlin area must contain land and sea with flat ground");
    for (int z = 0; z<City::width; ++z) for (int x = 0; x<City::width; ++x)
        if (x<40 || x>=88 || z<48 || z>=80) require(city.tile({x,z})==CityTile::Water,"generated land escaped centered M x N area");
    require(city.save(directory,error),error.c_str()); City loaded;
    const auto path = directory/(city.id+".city");
    require(City::load(path,loaded,error) && loaded.tiles==city.tiles && loaded.elevation==city.elevation,"generated terrain did not round-trip");
    const auto before = city.tiles; const auto heights = city.elevation;
    require(!city.generate_terrain(48,32,99,error) && city.tiles==before && city.elevation==heights,"generation overwrote an existing city");
    for (CityCell size : {CityCell{1,1},{1,128},{128,1},{8,32},{128,128}}) {
        City fresh = City::create("Terrain limits");
        require(fresh.generate_terrain(size.x,size.z,std::numeric_limits<std::uint32_t>::max(),error)
            && fresh.land_count()>0 && fresh.validate(error)
            && std::all_of(fresh.elevation.begin(),fresh.elevation.end(),[](auto h){return h==0;}),"minimum, rectangular or maximum starter size must stay flat");
    }
    City fresh = City::create("Bad dimensions");
    for (CityCell size : {CityCell{0,32},{32,0},{129,1},{1,129},{-1,1}})
        require(!fresh.generate_terrain(size.x,size.z,0,error) && fresh.land_count()==0
            && *std::max_element(fresh.elevation.begin(),fresh.elevation.end())==0,"invalid dimensions mutated terrain");
    std::filesystem::remove(path);
}
void data_and_saves(const std::filesystem::path& directory) {
    std::string error; City city = City::create("First city");
    require(city.land_count()==0 && !city.spawn && city.traffic_routes().empty(),"new city isn't open water");
    require(city.add_land({60,60},{64,64},error),"first island failed");
    require(city.add_land({90,90},{92,92},error),"disconnected second island was rejected");
    require(city.add_land({65,60},{68,64},error),"island expansion failed");
    auto before = city.tiles;
    require(!city.add_road({{60,62},{128,62}},error) && city.tiles==before,"failed road partially changed terrain");
    require(city.add_road(City::road_stroke({60,62},{68,62}),error),"road creation failed");
    require(city.add_road(City::road_stroke({64,60},{64,64}),error),"crossing failed");
    require(city.road_mask({64,62})==15 && city.road_mask({60,62})==2,"intersection or dead-end detection failed");
    require(city.add_building({{60,60},2,12},error),"square building failed");
    require(!city.add_building({{60,60},1,12},error),"overlapping building accepted");
    require(!city.add_building({{62,61},3,12},error),"building across a road accepted");
    require(!city.set_spawn({60,60},error),"spawn inside building accepted");
    require(city.set_spawn({67,60},error),"spawn failed");
    require(!city.add_vehicle({CityVehicleKind::Car,{67,60},0},error),"vehicle over spawn accepted");
    require(city.add_vehicle({CityVehicleKind::Car,{66,64},1},error),"parked car failed");
    require(!city.add_vehicle({CityVehicleKind::Boeing747,{65,64},0},error),"unsupported wings over water accepted");
    require(city.save(directory,error),error.c_str());
    const auto path = directory/(city.id+".city"); City loaded;
    require(City::load(path,loaded,error),error.c_str());
    require(loaded.name==city.name && loaded.tiles==city.tiles && loaded.spawn==city.spawn
        && loaded.buildings.size()==1 && loaded.vehicles[0].rotation==1,"save didn't round-trip");
    city.name = "Renamed city"; require(city.save(directory,error),"rename save failed");
    require(City::load(path,loaded,error) && loaded.name==city.name,"rename did not persist");
    City second = City::create("Second city"); require(second.save(directory,error),"second city save failed");
    require(City::load(directory/(second.id+".city"),loaded,error) && loaded.land_count()==0,"cities did not remain independent");
    // A failed save never overwrites the last valid map.
    city.tiles[0] = CityTile(255);
    require(!city.save(directory,error),"invalid terrain save accepted");
    require(City::load(path,loaded,error) && loaded.tiles[0]==CityTile::Water,"failed save damaged previous city");
    city.tiles[0] = CityTile::Water;
    auto temporary = path; temporary += ".tmp";
    std::filesystem::create_directory(temporary);
    require(!city.save(directory,error) && City::load(path,loaded,error),"write failure damaged the previous city");
    std::filesystem::remove(temporary);
    auto bad = directory/"0000000000000000.city"; std::ofstream(bad) << "AMBARETTO_CITY 1\ntruncated";
    require(!City::load(bad,loaded,error),"truncated save accepted");
    std::filesystem::remove(bad);
    require(std::filesystem::remove(directory/(second.id+".city")),"city delete failed");
    City neck = City::create("Neck"); require(neck.add_land({1,1},{3,1},error),"thin island failed");
    require(neck.erase(CityCell{2,1},error) && !neck.connected(),"bulldoze cannot separate islands");
}
void trees_and_time(const std::filesystem::path& directory) {
    City city = City::create("Forest"); std::string error;
    require(city.add_land({55,55},{72,72},error),"forest island failed");
    const Vec3 point = City::center({64,64});
    require(city.brush_trees(point,24,3,false,error)>50,"brush did not plant a cluster");
    std::array<int,City::width*City::width> counts{};
    for (auto t : city.trees) ++counts[City::index(City::cell(t.base.GetX(),t.base.GetZ()))];
    require(*std::max_element(counts.begin(),counts.end())>4,"trees are restricted to one per block");
    require(city.brush_trees(point,24,3,false,error)==0,"overlapping brush duplicated trees");
    for (std::size_t i = 0; i<city.trees.size(); ++i) for (std::size_t j = i+1; j<city.trees.size(); ++j)
        require((city.trees[i].base-city.trees[j].base).Length()>2.4f,"brush trees are too close");
    const auto before = city.trees.size();
    require(city.add_road(City::road_stroke({60,64},{69,64}),error) && city.trees.size()<before,"roads did not clear trees");
    require(city.add_building({{64,62},2,12},error),"forest building failed");
    require(city.add_vehicle({CityVehicleKind::Trainer,{62,61},0},error),"forest plane failed");
    require(city.set_spawn({67,63},error),"forest spawn failed");
    for (auto t : city.trees) require(city.tree_clear(t.base),"construction left an obstructing tree");
    const auto tree = city.trees.front();
    City removed = city;
    require(removed.brush_trees(tree.base,8,2,true,error)>0 && removed.trees.size()<city.trees.size(),"clear brush failed");
    City flooded = city;
    const auto cell = City::cell(tree.base.GetX(),tree.base.GetZ());
    require(flooded.erase(cell,error),"forest land bulldoze failed");
    require(std::none_of(flooded.trees.begin(),flooded.trees.end(),[&](auto t){return City::cell(t.base.GetX(),t.base.GetZ())==cell;}),"trees remained over bulldozed water");
    city.start_minutes = 23*60+59;
    require(city.save(directory,error),error.c_str());
    const auto path = directory/(city.id+".city"); City loaded;
    require(City::load(path,loaded,error) && loaded.start_minutes==1439 && loaded.trees.size()==city.trees.size(),"trees or clock did not round-trip");
    for (std::size_t i = 0; i<city.trees.size(); ++i)
        require((city.trees[i].base-loaded.trees[i].base).LengthSq()==0 && city.trees[i].height==loaded.trees[i].height && city.trees[i].yaw==loaded.trees[i].yaw,"tree transforms changed after load");
    Environment map(loaded); PhysicsWorld world(map);
    require(map.trees().size()==loaded.trees.size(),"saved trees did not populate environment");
    require(world.camera_fraction(tree.base+Vec3(-2,1,0),Vec3(4,0,0))<1,"tree trunk collision is missing");
    city.start_minutes = 1440; require(!city.save(directory,error),"invalid city clock accepted"); city.start_minutes = 1439;
    City invalid = city; invalid.trees.push_back(invalid.trees.front()); require(!invalid.validate(error),"duplicate trees accepted");
    invalid = city; invalid.trees[0].base = City::center({64,64}); require(!invalid.validate(error),"tree on a road accepted");
    invalid = city; invalid.trees[0].base.SetX(std::numeric_limits<float>::quiet_NaN());
    require(!invalid.validate(error),"tree with NaN X was accepted");
    invalid = city; invalid.trees[0].base.SetZ(std::numeric_limits<float>::quiet_NaN());
    require(!invalid.validate(error),"tree with NaN Z was accepted");
    // Original saves store vehicle cells, then end after the spawn.
    std::ostringstream legacy;
    legacy << "AMBARETTO_CITY 1\n" << city.id << '\n' << std::quoted(city.name) << '\n';
    for (auto t : city.tiles) legacy << int(t);
    legacy << '\n' << city.buildings.size() << '\n';
    for (auto b : city.buildings) legacy << b.cell.x << ' ' << b.cell.z << ' ' << b.size << ' ' << b.height << '\n';
    legacy << city.vehicles.size() << '\n';
    for (auto v : city.vehicles) {
        const auto p = City::cell(v.position.GetX(),v.position.GetZ());
        legacy << int(v.kind) << ' ' << p.x << ' ' << p.z << ' ' << v.rotation << '\n';
    }
    legacy << "1 " << city.spawn->x << ' ' << city.spawn->z << '\n';
    std::ofstream(path) << legacy.str();
    require(City::load(path,loaded,error) && loaded.start_minutes==720 && loaded.trees.empty() && loaded.spawn==city.spawn,"old city save compatibility failed");
    require(loaded.save(directory,error) && City::load(path,loaded,error),"old city could not upgrade its save");
}
City ground_paint(const std::filesystem::path& directory) {
    City city = City::create("Ground paint"); std::string error;
    require(city.add_land({58,58},{74,74},error),"ground island failed");
    const Environment soil(city);
    require(std::all_of(soil.triangles().begin(),soil.triangles().end(),[](const auto& t) {
        return t.surface==Surface::Soil || t.surface==Surface::Seabed;
    }),"new land or shoreline still becomes grass or beach");
    const Vec3 point = City::center({62,62});
    const auto before = city.ground;
    require(city.paint_ground({60,60},{64,64},CityGround::Grass,error) && city.ground!=before,"grass rectangle did not paint");
    require(std::count(city.ground.begin(),city.ground.end(),CityGround::Grass)==25
        && city.ground[City::index({60,60})]==CityGround::Grass && city.ground[City::index({64,64})]==CityGround::Grass
        && city.ground[City::index({59,60})]==CityGround::Soil,"rectangle did not cover precisely its selected squares");
    require(city.paint_ground({73,69},{69,65},CityGround::Sand,error),"reverse sand rectangle did not paint");
    require(city.paint_ground({66,69},{68,73},CityGround::Asphalt,error)
        && city.tile({67,71})==CityTile::Land && city.traffic_routes().empty(),"asphalt texture created a road instead of painting ground");
    require(city.paint_ground({62,62},{62,62},CityGround::Soil,error) && city.ground[City::index({62,62})]==CityGround::Soil,"single-square drawing did not restore soil");
    const auto painted = city.ground;
    require(!city.paint_ground({-1,62},{62,62},CityGround::Grass,error)
        && !city.paint_ground({62,62},{128,62},CityGround::Grass,error)
        && !city.paint_ground({62,62},{62,62},CityGround(255),error)
        && city.ground==painted,"invalid ground rectangle changed terrain");
    require(city.add_road(City::road_stroke({58,66},{77,66}),error),"paint road failed");
    require(city.paint_ground({75,65},{77,67},CityGround::Grass,error)
        && city.ground[City::index({76,66})]==CityGround::Soil && city.ground[City::index({76,67})]==CityGround::Soil,"ground rectangle painted water or a bridge");
    require(city.paint_ground({62,66},{62,66},CityGround::Sand,error)
        && city.erase(CityCell{62,66},error) && city.ground[City::index({62,66})]==CityGround::Sand,"road construction or removal lost painted ground");
    require(city.erase(CityCell{71,67},error) && city.add_land({71,67},{71,67},error)
        && city.ground[City::index({71,67})]==CityGround::Soil,"recreated land inherited demolished paint");
    require(city.set_spawn({67,61},error) && city.save(directory,error),"painted city save failed");
    const auto path = directory/(city.id+".city"); City loaded;
    require(City::load(path,loaded,error) && loaded.ground==city.ground,"ground textures did not round-trip");
    Environment map(loaded); PhysicsWorld world(map); GroundHit hit;
    for (const auto& t : map.triangles()) {
        if (t.deck || t.surface==Surface::Seabed) continue;
        const Vec3 a = map.vertices()[t.a], b = map.vertices()[t.b], c = map.vertices()[t.c], center = (a+b+c)/3;
        const CityCell p = City::cell(center.GetX(),center.GetZ());
        const Surface expected[] = {Surface::Soil,Surface::Grass,Surface::Sand,Surface::Road};
        require(t.surface==(loaded.land(p) ? expected[int(loaded.ground[City::index(p)])] : Surface::Soil),"painted terrain rendered the wrong texture");
    }
    require(world.cast_ground(point+Vec3(0,5,0),-Vec3::sAxisY(),10,hit) && std::abs(hit.point.GetY()-City::level)<.001f,"ground painting changed collision height");
    City invalid = city; invalid.ground[0] = CityGround(255);
    require(!invalid.save(directory,error) && City::load(path,loaded,error) && loaded.ground==city.ground,"invalid paint damaged the previous save");
    std::ifstream saved(path); std::ostringstream contents; contents << saved.rdbuf(); saved.close();
    const std::string current = contents.str();
    const auto heights = current.rfind('\n',current.size()-2)+1;
    const auto textures = current.rfind('\n',heights-2)+1;
    std::string bad = current; bad[textures] = '4'; std::ofstream(path) << bad;
    require(!City::load(path,loaded,error),"invalid saved ground texture accepted");
    std::ofstream(path) << current.substr(0,textures);
    require(!City::load(path,loaded,error),"missing saved ground textures accepted");
    std::string legacy = current.substr(0,textures); legacy[legacy.find(' ')+1] = '3'; std::ofstream(path) << legacy;
    require(City::load(path,loaded,error) && loaded.tiles==city.tiles && loaded.road_axes==city.road_axes
        && std::all_of(loaded.ground.begin(),loaded.ground.end(),[](auto g){return g==CityGround::Soil;}),"version 3 city did not load with default soil");
    require(city.save(directory,error),"restoring painted fixture failed");
    return city;
}
void custom_ground_textures(const std::filesystem::path& directory) {
    City city = City::create("Custom ground"); std::string error;
    require(city.add_land({60,60},{68,68},error) && city.add_road(City::road_stroke({60,64},{68,64}),error),error.c_str());
    require(city.paint_ground({65,65},{63,63},"paving custom.PNG",error)
        && city.ground_textures.size()==9 && city.ground_texture({64,64})=="paving custom.PNG"
        && city.ground_texture({62,62})=="soil.png", "custom rectangle did not retain filenames on land and roads");
    const auto painted = city.ground_textures;
    for (const auto& filename : {"../outside.png","bad\\path.png","not-an-image.txt",""})
        require(!city.paint_ground({63,63},{65,65},filename,error) && city.ground_textures==painted,"invalid filename changed painted ground");
    require(!city.paint_ground({-1,63},{65,65},"brick.png",error) && city.ground_textures==painted,"invalid custom rectangle changed ground");
    require(city.erase(CityCell{64,64},error) && city.ground_texture({64,64})=="paving custom.PNG","removing a road lost its custom ground");
    require(city.erase(CityCell{63,63},error) && !city.ground_textures.count(City::index({63,63}))
        && city.add_land({63,63},{63,63},error) && city.ground_texture({63,63})=="soil.png","recreated land inherited custom paint");
    require(city.paint_ground({65,65},{65,65},"grass.png",error)
        && !city.ground_textures.count(City::index({65,65})) && city.ground[City::index({65,65})]==CityGround::Grass,"built-in repaint did not clear custom paint");
    CityVehicle vehicle{CityVehicleKind::Car,City::center({66,66}),3};
    vehicle.car = CarDesign{}; vehicle.car->body="body.glb"; vehicle.car->wheel="wheel.glb"; vehicle.car->offset={.2f,.1f,-.3f};
    require(city.add_vehicle(vehicle,error) && city.add_building({{67,62},1,12},error) && city.save(directory,error),error.c_str());
    const auto path = directory/(city.id+".city"); City loaded;
    require(City::load(path,loaded,error) && loaded.ground_textures==city.ground_textures && loaded.ground==city.ground
        && loaded.vehicles.size()==1 && loaded.vehicles[0].car && loaded.vehicles[0].car->offset==vehicle.car->offset && loaded.vehicles[0].rotation==3
        && loaded.buildings.size()==1,"custom ground, buildings and car settings did not round-trip");
    City invalid = city; invalid.ground_textures[0]="paving.png";
    require(!invalid.save(directory,error) && City::load(path,loaded,error) && loaded.ground_textures==city.ground_textures,"invalid custom paint replaced the previous save");
    std::ifstream saved(path); std::ostringstream contents; contents << saved.rdbuf(); saved.close();
    const std::string current = contents.str();
    const auto filename = current.find("paving custom.PNG");
    std::string bad = current; bad.replace(filename,17,"../outside.png"); std::ofstream(path) << bad;
    require(!City::load(path,loaded,error),"unsafe saved ground filename accepted");
    std::ofstream(path) << current.substr(0,filename);
    require(!City::load(path,loaded,error),"truncated ground filename data accepted");
    require(city.save(directory,error),"restoring custom ground fixture failed");
    std::cout << "Custom ground: filename painting, bounds, repainting, demolition, save compatibility and validation passed\n";
}
City terraced_ground(const std::filesystem::path& directory) {
    std::string error;
    require(City::elevation_step==2.f && City::corner_width==City::width+1,"elevation is not a two-meter shared corner grid");
    const auto flat = [](const City& city,CityCell p,int level) {
        const auto patch = city.ground_patch(p);
        return std::all_of(patch.begin(),patch.end(),[&](Vec3 v) {
            return std::abs(v.GetY()-City::level-level*City::elevation_step)<.001f;
        });
    };
    const auto steps = [](const City& city) {
        for (int z = 0; z < City::corner_width; ++z) for (int x = 0; x < City::corner_width; ++x)
            for (int dz : {-1,0,1}) for (int dx : {-1,0,1}) {
                const int nx = x+dx, nz = z+dz;
                if (nx<0 || nz<0 || nx>=City::corner_width || nz>=City::corner_width) continue;
                require(std::abs(int(city.elevation[City::corner_index({x,z})])
                    -int(city.elevation[City::corner_index({nx,nz})]))<=1,"neighboring corner levels differ by more than one step");
            }
    };
    City base = City::create("Corner terrain");
    require(base.add_land({52,52},{76,76},error),"terrain island failed");
    City hill = base;
    require(hill.change_elevation({64,64},{64,64},1,error) && flat(hill,{64,64},1),"raising one square did not lift all four corners to a flat plateau");
    for (int z = 63; z <= 65; ++z) for (int x = 63; x <= 65; ++x) {
        if (x==64 && z==64) continue;
        const auto patch = hill.ground_patch({x,z});
        const int raised = int(std::count_if(patch.begin(),patch.end(),[](Vec3 v) {
            return std::abs(v.GetY()-City::level-City::elevation_step)<.001f;
        }));
        require(raised==((x==64 || z==64) ? 2 : 1),"single-square plateau did not produce its eight neighboring ramps");
        require(std::all_of(patch.begin(),patch.end(),[](Vec3 v) {
            return std::abs(v.GetY()-City::level)<.001f || std::abs(v.GetY()-City::level-City::elevation_step)<.001f;
        }),"first elevation step changed vertices outside the ramp ring");
        for (const auto& triangle : hill.ground_triangles({x,z})) for (Vec3 p : triangle)
            require(std::any_of(patch.begin(),patch.end(),[&](Vec3 corner){return (corner-p).Length()<.001f;}),
                "ramp still contains a center fan vertex");
    }
    require(flat(hill,{62,64},0) && flat(hill,{66,64},0),"first raise spread beyond its neighboring squares");
    City adjacent = hill;
    require(adjacent.change_elevation({65,64},{65,64},1,error)
        && flat(adjacent,{64,64},1) && flat(adjacent,{65,64},1),"raising a neighboring ramp did not merge a flat plateau");
    City rectangle = base;
    require(rectangle.change_elevation({65,64},{64,64},1,error)
        && flat(rectangle,{64,64},1) && flat(rectangle,{65,64},1),"rectangle raised shared vertices more than once");
    City mixed = hill; const auto mixed_before = mixed.elevation;
    require(!mixed.change_elevation({64,64},{65,64},1,error) && mixed.elevation==mixed_before,
        "rectangle with conflicting flat targets changed terrain");
    City lowered = hill;
    require(lowered.change_elevation({64,64},{64,64},-1,error)
        && std::all_of(lowered.elevation.begin(),lowered.elevation.end(),[](auto h){return h==0;}),"lowering the first raised plateau did not restore flat ground");
    City repeated = hill;
    require(repeated.change_elevation({64,64},{64,64},1,error) && flat(repeated,{64,64},2),"second raising created a pointed square");
    City twice_lowered = repeated;
    require(twice_lowered.change_elevation({64,64},{64,64},-1,error) && flat(twice_lowered,{64,64},1),"lowering a two-step plateau did not flatten its selected corners");
    steps(twice_lowered);
    require(repeated.change_elevation({64,64},{64,64},1,error) && flat(repeated,{64,64},3),"repeated raising created a pointed square");
    require(repeated.elevation[City::corner_index({63,63})]==2
        && repeated.elevation[City::corner_index({62,62})]==1
        && repeated.elevation[City::corner_index({61,61})]==0,"raising did not propagate through diagonal neighboring vertices");
    steps(repeated);
    const auto elevated = repeated.elevation;
    require(repeated.change_elevation({64,64},{64,64},-1,error) && flat(repeated,{64,64},2),"lowering did not keep the selected square flat");
    steps(repeated);
    require(repeated.change_elevation({64,64},{64,64},1,error) && repeated.elevation==elevated,"lowering then raising lost the propagated terrace");
    for (int i = 3; i < City::max_elevation; ++i)
        require(repeated.change_elevation({64,64},{64,64},1,error),"raising to the height limit failed");
    require(flat(repeated,{64,64},City::max_elevation)
        && !repeated.change_elevation({64,64},{64,64},1,error),"elevation exceeded its fixed height limit");
    steps(repeated);
    const auto unchanged = base.elevation;
    require(!base.change_elevation({64,64},{64,64},-1,error)
        && !base.change_elevation({-1,60},{60,60},1,error) && !base.change_elevation({60,60},{60,60},0,error)
        && !base.change_elevation({0,0},{1,1},1,error) && base.elevation==unchanged,"invalid elevation changed corner data");
    City sideways = hill;
    const auto empty_roads = sideways.tiles;
    require(!sideways.add_road(City::road_stroke({63,62},{63,66}),error) && sideways.tiles==empty_roads,"sideways road on an axial ramp was accepted or changed terrain");
    City corner_road = hill;
    require(!corner_road.add_road({{63,63}},error) && corner_road.tiles==empty_roads,"road on a one-corner ramp was accepted");
    City diagonal = hill;
    require(!diagonal.add_road(City::road_stroke({62,62},{66,66},false,true),error)
        && diagonal.tiles==empty_roads,"diagonal road across corner ramps was accepted");
    City banks = City::create("Bridge bank lowering");
    require(banks.add_land({58,62},{61,66},error) && banks.add_land({68,62},{71,66},error)
        && banks.change_elevation({58,62},{71,66},1,error)
        && banks.add_road(City::road_stroke({60,64},{69,64}),error),"raised cardinal bridge fixture failed");
    const auto banks_before = banks.elevation;
    require(!banks.change_elevation({58,62},{61,66},-1,error) && banks.elevation==banks_before,
        "lowering one bridge bank succeeded without leaving selected ground flat");
    {
        Environment map(hill); PhysicsWorld world(map); GroundHit hit;
        for (int z = 63; z <= 65; ++z) for (int x = 63; x <= 65; ++x) {
            const auto patch = hill.ground_patch({x,z});
            for (const auto& triangle : hill.ground_triangles({x,z})) {
                const Vec3 p = (triangle[0]+triangle[1]+triangle[2])/3;
                require(std::abs(map.height(p.GetX(),p.GetZ())-p.GetY())<.001f
                    && world.cast_ground(p+Vec3(0,4,0),-Vec3::sAxisY(),8,hit)
                    && std::abs(hit.point.GetY()-p.GetY())<.002f,"corner-only ground triangles disagree with height queries or collision");
            }
            const Vec3 center = City::center({x,z});
            require(std::abs(hill.tile_height({x,z})-hill.height(center.GetX(),center.GetZ()))<.001f,"tile center height differs from its mesh");
            if (x<65) {
                const auto next = hill.ground_patch({x+1,z});
                require((patch[2]-next[1]).Length()<.001f && (patch[3]-next[0]).Length()<.001f,"shared ramp corners have a seam");
            }
        }
        for (const auto& triangle : map.triangles()) if (!triangle.deck)
            require((map.vertices()[triangle.b]-map.vertices()[triangle.a]).Cross(map.vertices()[triangle.c]-map.vertices()[triangle.a]).GetY()>0,
                "terrain still contains vertical walls");
    }
    City city = base; city.name = "Terraced roads";
    require(city.paint_ground({64,67},{74,73},CityGround::Grass,error),"terrain texture failed");
    const auto painted = city.ground;
    require(city.change_elevation({64,58},{74,70},1,error)
        && city.change_elevation({68,58},{74,70},1,error)
        && city.add_road(City::road_stroke({56,64},{74,64}),error),"aligned straight ramp road failed");
    require(city.add_building({{69,60},2,12},error) && !city.add_building({{63,59},2,12},error),"building level-footprint checks failed");
    require(city.add_vehicle({CityVehicleKind::Car,{55,56},0},error) && city.set_spawn({65,60},error),"terrain objects failed");
    require(city.brush_trees(City::center({63,68}),8,3,false,error)>0
        && std::any_of(city.trees.begin(),city.trees.end(),[](auto t) {
            return t.base.GetY()>City::level+.01f && t.base.GetY()<City::level+City::elevation_step-.01f;
        }),"trees did not follow the straight neighboring ramp");
    require(city.change_elevation({54,55},{56,57},1,error)
        && std::abs(city.vehicles[0].position.GetY()-City::level-City::elevation_step)<.001f,"parked car did not follow a raised flat footprint");
    const auto before = city.elevation;
    require(!city.change_elevation({69,60},{69,60},1,error) && city.elevation==before,"partial building elevation was not atomic");
    require(city.ground==painted,"elevation lost painted textures");
    steps(city);
    Environment map(city); PhysicsWorld world(map); GroundHit hit;
    for (float x = City::center({56,64}).GetX(); x < City::center({74,64}).GetX(); x += 1.1f) {
        const float z = City::center({56,64}).GetZ(), height = city.height(x,z);
        require(world.cast_ground({x,height+4,z},-Vec3::sAxisY(),8,hit) && std::abs(hit.point.GetY()-height)<.002f,"straight ramp road collision differs from terrain");
    }
    for (const auto& t : city.drape_triangle({City::center({62,64}),City::center({70,63}),City::center({70,65})}))
        for (Vec3 p : t) require(std::abs(p.GetY()-city.height(p.GetX(),p.GetZ()))<.002f,"road draping differs from corner-only terrain");
    require(std::abs(map.spawn().GetY()-City::level-City::elevation_step-.56f)<.001f,"player spawn is below its raised plateau");
    for (auto t : city.trees) require(std::abs(t.base.GetY()-city.height(t.base.GetX(),t.base.GetZ()))<.001f,"tree base is below the ramp");
    for (const auto& route : city.traffic_routes()) for (Vec3 p : route)
        require(std::abs(p.GetY()-map.height(p.GetX(),p.GetZ()))<.002f,"traffic route does not follow the straight ramps");
    Car car(world); Vec3 start = City::center({56,64}); start.SetY(map.height(start.GetX(),start.GetZ())+.56f);
    car.reset(start,-1.57079633f);
    for (int i = 0; i < 60; ++i) { car.step({0,0,false,true}); world.step(); }
    float highest = car.position().GetY();
    for (int i = 0; i < 1200 && car.position().GetX()<City::center({73,64}).GetX(); ++i) {
        car.step({.65f,0,false,false}); world.step(); highest = std::max(highest,car.position().GetY());
    }
    require(car.position().GetX()>City::center({69,64}).GetX() && highest>City::level+1.75f*City::elevation_step,"car could not climb the aligned two-step plateau road");
    require(city.save(directory,error),"corner terrain save failed");
    const auto path = directory/(city.id+".city"); City loaded;
    require(City::load(path,loaded,error) && loaded.elevation==city.elevation && loaded.ground==city.ground
        && loaded.vehicles[0].position==city.vehicles[0].position,"corner terrain save did not round-trip");
    std::ifstream file(path); std::ostringstream contents; contents << file.rdbuf(); file.close();
    const std::string current = contents.str(); const auto heights = current.rfind('\n',current.size()-2)+1;
    require(current.rfind("AMBARETTO_CITY 6\n",0)==0 && current.size()-heights-1==city.elevation.size(),"city did not save version 6 shared corners");
    std::string bad = current; bad[heights] = '9'; std::ofstream(path) << bad;
    require(!City::load(path,loaded,error),"invalid corner height was accepted");
    bad = current; bad.erase(heights+5,1); std::ofstream(path) << bad;
    require(!City::load(path,loaded,error),"short corner grid was accepted");
    bad = current; bad[heights+City::corner_index({10,10})] = '8'; std::ofstream(path) << bad;
    require(!City::load(path,loaded,error),"saved corner grid with an excessive neighbor step was accepted");
    std::ofstream(path) << current.substr(0,heights);
    require(!City::load(path,loaded,error),"missing corner elevation grid was accepted");
    std::string legacy = current.substr(0,heights); legacy[legacy.find(' ')+1] = '5';
    for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x)
        legacy += char('0'+(city.land({x,z}) ? (x>=68 ? 2 : x>=64 ? 1 : 0) : 0));
    legacy[heights+City::index({60,70})] = '3'; legacy += '\n'; std::ofstream(path) << legacy;
    require(City::load(path,loaded,error) && loaded.ground==city.ground
        && loaded.elevation[City::corner_index({64,64})]==1 && loaded.elevation[City::corner_index({68,64})]==2
        && loaded.elevation[City::corner_index({60,70})]==3 && loaded.elevation[City::corner_index({59,69})]==2,
        "version 5 tile levels did not migrate to shared corners with propagated ramps");
    steps(loaded);
    legacy[heights+City::index({62,65})] = '2'; std::ofstream(path) << legacy;
    require(City::load(path,loaded,error) && loaded.tiles==city.tiles && loaded.road_axes==city.road_axes
        && loaded.vehicles.size()==city.vehicles.size() && loaded.ramp_axis({62,64})==0,
        "version 5 migration lost city objects or left an incompatible road ramp");
    steps(loaded);
    legacy = current.substr(0,heights); legacy[legacy.find(' ')+1] = '4'; std::ofstream(path) << legacy;
    require(City::load(path,loaded,error) && loaded.ground==city.ground
        && std::all_of(loaded.elevation.begin(),loaded.elevation.end(),[](auto h){return h==0;}),"version 4 city did not retain flat terrain");
    require(city.save(directory,error),"restoring corner terrain save failed");
    City invalid = city; invalid.elevation[0] = 255;
    require(!invalid.save(directory,error) && City::load(path,loaded,error) && loaded.elevation==city.elevation,"invalid corner elevation damaged the previous save");
    return city;
}
void sloping_shore() {
    City city = City::create("Sloping beach"); std::string error;
    require(city.add_land({62,62},{66,66},error),"shore island failed");
    Environment map(city); PhysicsWorld world(map);
    const float edge = (67-City::width/2)*City::block, z = City::center({66,64}).GetZ();
    float previous = City::level;
    for (float distance : {0.f,2.f,4.f,6.f,8.f,12.f,20.f,31.f,32.f,40.f}) {
        const float height = map.height(edge+distance,z);
        require(height<=previous && height>=-8,"beach slope is not a gradual descent");
        GroundHit hit;
        require(world.cast_ground({edge+distance,10,z},Vec3(0,-1,0),25,hit)
            && std::abs(hit.point.GetY()-height)<.002f,"shore rendering, height queries, and collision disagree");
        previous = height;
    }
    require(map.height(edge+2,z)<City::level && map.height(edge+6,z)>0 && map.height(edge+8,z)<0
        && std::abs(map.height(edge+32,z)+8)<.1f,"beach does not taper through shallow water to the seabed");
    require(map.height(edge-8,z)==City::level,"shore changed flat building land");
    // A convex corner rounds outward; the submerged end joins the ocean floor.
    const float corner_z = edge;
    GroundHit hit;
    const float rounded = map.height(edge+4,corner_z+4);
    require(rounded<map.height(edge+4,z) && rounded>-8,"outer shoreline corner is not rounded");
    require(world.cast_ground({edge+4,10,corner_z+4},Vec3(0,-1,0),25,hit)
        && std::abs(hit.point.GetY()-rounded)<.002f,"rounded shore collision disagrees with the mesh");
    require(city.change_elevation({62,62},{66,66},1,error),"raised shore failed");
    const Environment raised(city); PhysicsWorld raised_world(raised);
    require(std::abs(raised.height(edge,z)-City::level-City::elevation_step)<.002f
        && std::abs(raised.height(edge-.001f,z)-raised.height(edge+.001f,z))<.002f,
        "raised land does not join its shoreline ramp");
    for (float distance : {0.f,2.f,4.f,8.f,12.f}) {
        const float height = raised.height(edge+distance,z);
        require(raised_world.cast_ground({edge+distance,12,z},-Vec3::sAxisY(),25,hit)
            && std::abs(hit.point.GetY()-height)<.002f,"raised shoreline ramp and collision disagree");
    }
    require(city.change_elevation({62,62},{66,66},-1,error),"restoring shore failed");
    require(city.add_land({67,62},{67,66},error),"shore expansion failed");
    const Environment expanded(city);
    require(expanded.height(edge+8,z)==City::level && expanded.height(edge+20,z)<City::level,"expansion did not move the beach slope");
    City boundary = City::create("Map edge");
    require(boundary.add_land({0,0},{1,1},error),"edge island failed");
    const Environment map_edge(boundary);
    require(map_edge.height(-City::extent-4,-City::extent+8)>-8 && map_edge.height(-City::extent-34,-City::extent+8)==-8,"map-edge island shore is missing");
}
void rounded_roads() {
    const CityCell p{64,64}, neighbors[] = {{64,63},{65,64},{64,65},{63,64}};
    for (unsigned mask = 0; mask < 16; ++mask) {
        City city = City::create("Road bend"); std::string error;
        require(city.add_land({62,62},{66,66},error),"bend island failed");
        city.tiles[City::index(p)] = CityTile::Road;
        for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) city.tiles[City::index(neighbors[d])] = CityTile::Road;
        const auto center = city.road_bend(p), inner = city.road_bend(p,.7f), outer = city.road_bend(p,City::block-.7f);
        const bool bend = mask==3 || mask==6 || mask==12 || mask==9;
        require(center.empty()!=bend,"straight, junction, or dead-end road was rounded");
        if (!bend) continue;
        require(center.size()>2 && inner.size()==center.size() && outer.size()==center.size(),"bend strips are incomplete");
        Vec3 pivot = City::center(p);
        for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) pivot += (City::center(neighbors[d])-City::center(p))/2;
        for (std::size_t i = 0; i < center.size(); ++i) {
            require(std::abs((center[i]-pivot).Length()-City::half_block)<.001f && std::abs((outer[i]-inner[i]).Length()-(City::block-1.4f))<.001f,"bend radius or road width changed");
            if (i+1<center.size()) require((outer[i]-inner[i]).Cross(outer[i+1]-inner[i]).GetY()>0,"bend pavement faces downward");
        }
        for (int d = 0; d < 4; ++d) if (mask&(1u<<d)) {
            const Vec3 connection = (City::center(p)+City::center(neighbors[d]))/2;
            require(std::min((center.front()-connection).Length(),(center.back()-connection).Length())<.001f,"bend does not meet adjoining road centerline");
        }
        int samples = 0;
        for (const auto& route : city.traffic_routes()) for (Vec3 point : route) if (City::cell(point.GetX(),point.GetZ())==p) {
            const float radius = (point-pivot).Length();
            require(radius>=.7f && radius<=City::block-.7f,"traffic path leaves rounded pavement"); ++samples;
        }
        require(samples>2,"traffic does not pass through the bend");
    }
}
City single_road_turns() {
    City touching = City::create("Touching strokes"); std::string error;
    require(touching.add_land({60,60},{72,72},error),"touching road land failed");
    require(touching.add_road(City::road_stroke({66,60},{66,64}),error)
        && touching.add_road(City::road_stroke({60,64},{65,64}),error),"touching strokes failed");
    require(touching.road_mask({66,64})==9 && touching.road_mask({65,64})==10,"perpendicular strokes touch without connecting");
    require(touching.road_network().size()==11 && touching.traffic_routes().size()==1,"touching road traffic remains disconnected");
    // The saved city's tight S turn meets a perpendicular stroke beside its endpoint.
    City tight = City::create("Tight offset turn");
    require(tight.add_land({60,60},{72,72},error),"tight turn land failed");
    require(tight.add_road(City::road_stroke({70,60},{70,65}),error)
        && tight.add_road(City::road_stroke({65,66},{69,66}),error)
        && tight.add_road(City::road_stroke({69,66},{70,65},true),error),"tight turn strokes failed");
    require(tight.road_mask({69,66})==9 && tight.road_mask({69,65})==6 && tight.road_mask({70,65})==9,"tight turn lost its consecutive bends");
    require(tight.traffic_routes().size()==1,"tight turn traffic remains disconnected");
    require(tight.set_spawn({62,62},error),"turn spawn failed");
    return tight;
}
void require_routes_on_pavement(const City& city) {
    const auto network = city.road_network();
    std::vector<std::vector<Vec3>> pavement;
    for (const auto& node : network) {
        auto polygon = node.path(0,.7f);
        if (!polygon.empty()) {
            const auto outer = node.path(1,-.7f);
            polygon.insert(polygon.end(),outer.rbegin(),outer.rend());
        } else polygon = node.outline();
        if (polygon.empty()) {
            const Vec3 c = node.center(), half = node.half_size();
            const float left = -half.GetX()+(node.mask&8 ? 0 : .7f), right = half.GetX()-(node.mask&2 ? 0 : .7f);
            const float top = -half.GetZ()+(node.mask&1 ? 0 : .7f), bottom = half.GetZ()-(node.mask&4 ? 0 : .7f);
            polygon = {c+Vec3(left,0,top),c+Vec3(right,0,top),c+Vec3(right,0,bottom),c+Vec3(left,0,bottom)};
        }
        pavement.push_back(std::move(polygon));
    }
    const auto inside = [](Vec3 p,const std::vector<Vec3>& polygon) {
        bool result = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Vec3 a = polygon[i], b = polygon[(i+1)%polygon.size()];
            const float dx = b.GetX()-a.GetX(), dz = b.GetZ()-a.GetZ(), length_sq = dx*dx+dz*dz;
            const float t = length_sq>0 ? std::clamp(((p.GetX()-a.GetX())*dx+(p.GetZ()-a.GetZ())*dz)/length_sq,0.f,1.f) : 0;
            const float ex = p.GetX()-a.GetX()-t*dx, ez = p.GetZ()-a.GetZ()-t*dz;
            if (ex*ex+ez*ez<.000001f) return true; // Shared gates lie exactly on polygon edges.
            if ((a.GetZ()>p.GetZ())!=(b.GetZ()>p.GetZ())
                && p.GetX()<a.GetX()+dx*(p.GetZ()-a.GetZ())/dz) result = !result;
        }
        return result;
    };
    const auto routes = city.traffic_routes();
    require(!routes.empty(),"pavement regression has no traffic route");
    for (std::size_t r = 0; r < routes.size(); ++r) for (std::size_t segment = 0; segment < routes[r].size(); ++segment) {
        const Vec3 a = routes[r][segment], b = routes[r][(segment+1)%routes[r].size()];
        const int steps = std::max(1,int(std::ceil((b-a).Length()/.35f)));
        for (int k = 0; k <= steps; ++k) {
            const Vec3 p = a+(b-a)*(float(k)/steps);
            if (std::any_of(pavement.begin(),pavement.end(),[&](const auto& polygon){return inside(p,polygon);})) continue;
            std::size_t nearest = 0;
            for (std::size_t i = 1; i < network.size(); ++i)
                if ((network[i].center()-p).LengthSq()<(network[nearest].center()-p).LengthSq()) nearest = i;
            const auto cell = City::cell(p.GetX(),p.GetZ()); const auto& node = network[nearest];
            std::cerr << city.name << " route " << r << " segment " << segment << " leaves pavement at "
                << p.GetX() << ',' << p.GetZ() << " cell " << cell.x << ',' << cell.z
                << "; nearest node " << nearest << " mask " << node.mask << " bounds "
                << node.first.x << ',' << node.first.z << ".." << node.last.x << ',' << node.last.z << '\n';
            require(false,"traffic segment leaves rendered pavement");
        }
    }
}
void road_ports_and_diagonals() {
    // Adjacent opposite turns form a single-road diagonal corridor.
    constexpr int width = 1;
    for (int rotation = 0; rotation < 4; ++rotation) for (bool mirror : {false,true}) {
        City city = City::create("Diagonal offset"); std::string error;
        require(city.add_land({48,48},{80,80},error),"diagonal island failed");
        const auto rotate = [&](CityCell p) {
            int x = p.x-64, z = p.z-64;
            if (mirror) z = -z;
            for (int i = 0; i < rotation; ++i) { const int previous = x; x = -z; z = previous; }
            return CityCell{x+64,z+64};
        };
        const auto transform = [&](Vec3 p) {
            const Vec3 origin = City::center({64,64});
            float x = p.GetX()-origin.GetX(), z = p.GetZ()-origin.GetZ();
            if (mirror) z = -z;
            for (int i = 0; i < rotation; ++i) { const float previous = x; x = -z; z = previous; }
            return origin+Vec3(x,0,z);
        };
        for (int lane = 0; lane < width; ++lane) {
            auto stroke = City::road_stroke({56,64+width+lane},{64+lane,64+lane});
            const auto exit = City::road_stroke({64+lane,64+lane},{72,64+lane});
            stroke.insert(stroke.end(),exit.begin()+1,exit.end());
            for (auto& p : stroke) p = rotate(p);
            require(city.add_road(stroke,error),"diagonal stroke failed");
        }
        const Vec3 west = transform(City::center({64,64+width})+Vec3(-City::half_block,0,(width-1)*City::half_block));
        const Vec3 east = transform(City::center({64+width-1,64})+Vec3(City::half_block,0,(width-1)*City::half_block));
        const auto network = city.road_network();
        const auto matches = [&](const CityRoadNode& node) {
            bool a = false, b = false;
            for (const auto& port : node.ports) if (port.width>0) {
                a |= (port.center-west).LengthSq()<.001f;
                b |= (port.center-east).LengthSq()<.001f;
            }
            return a && b;
        };
        require(std::count_if(network.begin(),network.end(),matches)==1,"opposite turns were not merged at their actual gates");
        const auto diagonal = std::find_if(network.begin(),network.end(),matches);
        require(diagonal->edges.size()==2 && (diagonal->mask==5 || diagonal->mask==10),"diagonal corridor became a junction");
        for (const auto& port : diagonal->ports) if (port.width>0)
            require(std::abs(port.width-City::block)<.001f,"diagonal changed its single-road width");
        const auto path = diagonal->path();
        require(path.size()==2,"short offset still uses consecutive quarter turns");
        require(std::min((path.front()-west).LengthSq(),(path.back()-west).LengthSq())<.001f
            && std::min((path.front()-east).LengthSq(),(path.back()-east).LengthSq())<.001f,"diagonal path does not meet its gates");
        for (float fraction : {0.f,.25f,.75f,1.f}) {
            const auto lane = diagonal->path(fraction);
            require(lane.size()==2 && (lane.back()-lane.front()).Cross(path.back()-path.front()).LengthSq()<.001f,
                "diagonal pavement or lanes still zigzag");
        }
        require_routes_on_pavement(city);
    }
    City branch = City::create("Branch beside offset"); std::string error;
    require(branch.add_land({56,56},{74,74},error),"branch island failed");
    auto stroke = City::road_stroke({56,65},{64,64});
    const auto exit = City::road_stroke({64,64},{72,64});
    stroke.insert(stroke.end(),exit.begin()+1,exit.end());
    require(branch.add_road(stroke,error) && branch.add_road(City::road_stroke({64,65},{64,72}),error),"offset branch failed");
    const auto branches = branch.road_network();
    require(std::count_if(branches.begin(),branches.end(),[](const auto& node){return node.edges.size()==3;})==1,
        "diagonal smoothing removed a nearby junction");

    City unequal = City::create("Unequal junction");
    require(unequal.add_land({60,58},{74,74},error),"unequal junction island failed");
    require(unequal.add_road(City::road_stroke({65,60},{65,66}),error)
        && unequal.add_road(City::road_stroke({65,66},{72,66}),error)
        && unequal.add_road(City::road_stroke({66,66},{66,72}),error)
        && unequal.add_road(City::road_stroke({67,66},{67,72}),error),"unequal junction strokes failed");
    const auto network = unequal.road_network();
    const auto junction = std::find_if(network.begin(),network.end(),[](const auto& node){return node.mask==7 && node.edges.size()==3;});
    require(junction!=network.end(),"unequal junction was mistaken for a bend");
    require(junction->first==junction->last,"junction merged adjacent roads");
    for (int d = 0; d < 3; ++d) require(junction->ports[d].width==City::block,"junction arm gained extra lanes");
    require(junction->ports[3].width==0 && junction->path().empty(),"three-arm junction became a diagonal corridor");
    require_routes_on_pavement(unequal);

    City tight = City::create("Solo three-cell offset");
    require(tight.add_land({60,58},{74,72},error)
        && tight.add_road(City::road_stroke({70,60},{70,65}),error)
        && tight.add_road(City::road_stroke({65,66},{69,66}),error)
        && tight.add_road(City::road_stroke({69,66},{70,65},true),error),"Solo offset strokes failed");
    require_routes_on_pavement(tight);
}
City diagonal_road_drags(const std::filesystem::path& directory) {
    const CityCell start{64,64};
    for (CityCell size : {CityCell{1,1},CityCell{8,8},CityCell{8,3},CityCell{3,8}})
        for (int sx : {-1,1}) for (int sz : {-1,1}) for (bool z_first : {false,true}) {
            const CityCell finish{start.x+sx*size.x,start.z+sz*size.z};
            const auto stroke = City::road_stroke(start,finish,z_first,true);
            require(stroke.size()==std::size_t(size.x+size.z+1) && stroke.front()==start && stroke.back()==finish,
                "diagonal drag missed its endpoint or repeated blocks");
            for (std::size_t i = 1; i < stroke.size(); ++i)
                require(std::abs(stroke[i].x-stroke[i-1].x)+std::abs(stroke[i].z-stroke[i-1].z)==1,
                    "diagonal drag does not connect edge to edge");
            const int diagonal_steps = std::min(size.x,size.z);
            for (int i = 0; i < diagonal_steps; ++i) {
                require(stroke[2*i+2]==CityCell{start.x+sx*(i+1),start.z+sz*(i+1)},"diagonal drag lost a 45-degree step");
                require((stroke[2*i+1].x==stroke[2*i].x)==z_first,"Shift did not swap diagonal edge ordering");
            }
            City city = City::create("Drag "+std::to_string(sx*size.x)+","+std::to_string(sz*size.z)+(z_first ? " Z first" : " X first"));
            std::string error;
            require(city.add_land({48,48},{80,80},error) && city.add_road(stroke,error),"diagonal road placement failed");
            const auto network = city.road_network();
            const int diagonals = int(std::count_if(network.begin(),network.end(),[](const auto& node) {
                const auto path = node.path();
                return path.size()==2 && std::abs(path.back().GetX()-path.front().GetX())>.001f
                    && std::abs(path.back().GetZ()-path.front().GetZ())>.001f;
            }));
            if (diagonals==0) std::cerr << city.name << " has no straight diagonal pavement\n";
            require(diagonals>=std::max(1,diagonal_steps-1),"diagonal drag still renders as cardinal corners");
            require_routes_on_pavement(city);
        }
    const auto legacy = City::road_stroke({60,60},{63,62});
    require(legacy==std::vector<CityCell>{{60,60},{61,60},{62,60},{63,60},{63,61},{63,62}},"legacy L-shaped strokes changed");
    const auto legacy_shift = City::road_stroke({60,60},{63,62},true);
    require(legacy_shift==std::vector<CityCell>{{60,60},{60,61},{60,62},{61,62},{62,62},{63,62}},"Shift changed legacy L-shaped strokes");
    for (CityCell end : {CityCell{64,72},CityCell{72,64},CityCell{64,64}})
        require(City::road_stroke(start,end,false,true)==City::road_stroke(start,end),"diagonal mode changed cardinal drags");
    require(City::road_stroke(start,{128,68},false,true).empty(),"out-of-bounds diagonal drag was partially rasterized");

    City wide_end = City::create("Wide short corner"); std::string error;
    require(wide_end.add_land({50,58},{70,70},error)
        && wide_end.add_road(City::road_stroke({56,64},{64,63}),error)
        && wide_end.add_road(City::road_stroke({56,65},{65,63}),error),"wide short corner fixture failed");
    for (const auto& node : wide_end.road_network()) for (int d = 0; d < 4; ++d) if (node.mask&(1u<<d))
        require(std::abs(node.port(d).width-City::block)<.001f,"adjacent road strokes merged into a wider road");
    require_routes_on_pavement(wide_end);

    City blocked = City::create("Blocked diagonal");
    const auto stroke = City::road_stroke({56,56},{72,72},false,true);
    require(blocked.add_land({50,50},{78,78},error) && blocked.add_building({stroke[stroke.size()/2],1,12},error),"blocked drag fixture failed");
    const auto tiles = blocked.tiles;
    const auto axes = blocked.road_axes;
    require(!blocked.add_road(stroke,error) && blocked.tiles==tiles && blocked.road_axes==axes,"failed diagonal drag changed part of the city");

    City crossing = City::create("Diagonal crossing");
    require(crossing.add_land({50,50},{78,78},error) && crossing.add_road(stroke,error)
        && crossing.add_road(City::road_stroke({54,64},{75,64}),error),"diagonal crossing failed");
    const auto crossings = crossing.road_network();
    require(std::any_of(crossings.begin(),crossings.end(),[](const auto& node){return node.edges.size()>=3;})
        && crossing.traffic_routes().size()==1,"diagonal swallowed a perpendicular street junction");
    require_routes_on_pavement(crossing);

    City bridge = City::create("Diagonal bridge");
    require(bridge.add_land({48,48},{58,64},error) && bridge.add_land({70,62},{82,82},error),"diagonal bridge islands failed");
    const auto bridge_stroke = City::road_stroke({50,50},{78,78},false,true);
    require(bridge.add_road(bridge_stroke,error),"diagonal bridge road failed");
    require(bridge.tile({64,64})==CityTile::Bridge && !bridge.land({64,64}),"diagonal road filled water instead of bridging it");
    for (const auto endpoints : {std::array<CityCell,2>{{{50,60},{51,61}}},std::array<CityCell,2>{{{55,60},{54,61}}},
                               std::array<CityCell,2>{{{50,64},{51,63}}},std::array<CityCell,2>{{{55,64},{54,63}}},
                               std::array<CityCell,2>{{{72,64},{80,67}}}})
        require(bridge.add_road(City::road_stroke(endpoints[0],endpoints[1],false,true),error),"diagonal preview road failed");
    require_routes_on_pavement(bridge);
    require(bridge.save(directory,error),"diagonal bridge save failed"); City loaded;
    require(City::load(directory/(bridge.id+".city"),loaded,error) && loaded.tiles==bridge.tiles && loaded.road_axes==bridge.road_axes,
        "diagonal directions or bridge tiles did not round-trip");
    require_routes_on_pavement(loaded);
    require(bridge.set_spawn({51,49},error),"diagonal bridge spawn failed");
    return bridge;
}
void diagonal_bridge_deck() {
    for (int sx : {-1,1}) for (int sz : {-1,1}) for (bool z_first : {false,true}) {
    City city = City::create("Diagonal deck"); std::string error;
    require(city.add_road(City::road_stroke({64,64},{64+sx*8,64+sz*8},z_first,true),error),"diagonal deck fixture failed");
    Environment map(city); PhysicsWorld world(map); GroundHit hit;
    const auto network = city.road_network();
    const auto node = std::find_if(network.begin(),network.end(),[](const auto& n) {
        const auto path = n.path();
        return n.edges.size()==2 && path.size()==2 && std::abs(path[1].GetX()-path[0].GetX())>.001f
            && std::abs(path[1].GetZ()-path[0].GetZ())>.001f;
    });
    require(node!=network.end(),"diagonal bridge has no straight deck section");
    const auto path = node->path(); const Vec3 middle = (path[0]+path[1])/2;
    const auto road = std::find_if(map.road_segments().begin(),map.road_segments().end(),[&](const auto& r) {
        return (r.a-path[0]).LengthSq()<.001f && (r.b-path[1]).LengthSq()<.001f;
    });
    require(road!=map.road_segments().end() && std::abs(road->width-(City::block-1.4f)/std::sqrt(2.f))<.001f,
        "diagonal road width was not measured perpendicular to its pavement");
    const float deck_height = map.height(middle.GetX(),middle.GetZ());
    const bool found_deck = world.cast_ground(middle+Vec3(0,5,0),-Vec3::sAxisY(),10,hit);
    if (deck_height!=City::level || !found_deck || std::abs(hit.point.GetY()-City::level)>=.001f)
        std::cerr << std::setprecision(9) << "Diagonal deck: height " << deck_height << ", expected " << City::level
            << ", ray found " << found_deck << ", collision height " << hit.point.GetY() << '\n';
    require(deck_height==City::level && found_deck && std::abs(hit.point.GetY()-City::level)<.001f,
        "diagonal bridge height and collision deck disagree");
    require(map.surface_height(middle-Vec3(0,4,0))==-8,"diagonal deck filled the water beneath it");
    Vec3 shoulder = Vec3::sZero(); bool found = false;
    for (float sx : {-1.f,1.f}) for (float sz : {-1.f,1.f}) {
        const Vec3 p = node->center()+Vec3(sx*(node->half_size().GetX()-.8f),0,sz*(node->half_size().GetZ()-.8f));
        const float t = node->mask==5 ? (p.GetZ()-path[0].GetZ())/(path[1].GetZ()-path[0].GetZ())
            : (p.GetX()-path[0].GetX())/(path[1].GetX()-path[0].GetX());
        const Vec3 centerline = path[0]+(path[1]-path[0])*t;
        const float across = node->mask==5 ? p.GetX()-centerline.GetX() : p.GetZ()-centerline.GetZ();
        if (t>0 && t<1 && std::abs(across)>City::half_block+.3f) { shoulder = p; found = true; }
    }
    require(found && city.tile(City::cell(shoulder.GetX(),shoulder.GetZ()))==CityTile::Bridge,"diagonal shoulder sample missed its original bridge block");
    require(map.height(shoulder.GetX(),shoulder.GetZ())==-8
        && world.cast_ground(shoulder+Vec3(0,5,0),-Vec3::sAxisY(),25,hit)
        && std::abs(hit.point.GetY()+8)<.001f,"off-diagonal bridge shoulder still has a square collision deck");
    const auto edge = node->path(0,0); const Vec3 rail_center = (edge[0]+edge[1])/2+Vec3(0,.45f,0);
    const auto rail = std::find_if(map.barriers().begin(),map.barriers().end(),[&](const auto& b){return (b.center-rail_center).LengthSq()<.001f;});
    require(rail!=map.barriers().end() && std::abs(rail->yaw)>.01f
        && (rail->rotation()*Vec3::sAxisZ()).Cross((edge[1]-edge[0]).Normalized()).LengthSq()<.001f,"diagonal bridge rail did not rotate with its deck");
    const Vec3 offset = rail_center-middle;
    require(world.camera_fraction(middle+Vec3(0,.5f,0),Vec3(offset.GetX(),0,offset.GetZ())*1.2f)<1,
        "rotated diagonal bridge rail has no matching collision");
    for (const auto& route : city.traffic_routes()) for (std::size_t i = 0; i < route.size(); ++i) {
        const Vec3 a = route[i]+Vec3(0,.5f,0), b = route[(i+1)%route.size()]+Vec3(0,.5f,0);
        require(world.camera_fraction(a,b-a)==1,"diagonal bridge railing blocks a traffic lane");
        require(world.cast_ground(a,-Vec3::sAxisY(),2,hit) && std::abs(hit.point.GetY()-City::level)<.001f,"diagonal traffic lane is missing its collision deck");
    }
    // A diagonal deck must also retain its shape where one of its blocks meets land.
    const auto shore_cell = City::cell(shoulder.GetX(),shoulder.GetZ());
    const CityCell land_cell = shore_cell==node->first ? node->last : node->first;
    require(city.add_land(land_cell,land_cell,error),"diagonal shore land failed");
    Environment shore(city); PhysicsWorld shore_world(shore);
    require(shore.height(shoulder.GetX(),shoulder.GetZ())==shore.terrain_height(shoulder.GetX(),shoulder.GetZ())
        && shore.height(shoulder.GetX(),shoulder.GetZ())<City::level,"diagonal shoreline join restored a square deck");
    require(shore_world.cast_ground(middle+Vec3(0,5,0),-Vec3::sAxisY(),10,hit)
        && std::abs(hit.point.GetY()-City::level)<.001f,"diagonal shoreline join lost deck collision");
    }
}
City islands_and_bridges(const std::filesystem::path& directory) {
    City city = City::create("Archipelago"); std::string error;
    require(std::abs(City::block-16*.7f)<.0001f,"blocks did not shrink by 30 percent");
    require(city.add_land({50,58},{58,70},error) && city.add_land({68,58},{76,70},error),"multiple islands failed");
    require(Environment(city).land_islands().size()==2,"separate island bounds were merged");
    require(city.add_road(City::road_stroke({51,64},{75,64}),error),"bridge stroke failed");
    require(city.add_road(City::road_stroke({51,65},{75,65}),error),"parallel bridge stroke failed");
    require(city.tile({63,64})==CityTile::Bridge && !city.land({63,64}),"road filled water instead of making a bridge");
    require(city.road_mask({63,64})==14 && city.road_mask({63,65})==11,"adjacent single roads do not connect edge to edge");
    City clicked = city; clicked.road_axes.fill(0);
    require(clicked.road_mask({63,64})==14 && clicked.road_network().size()==50,"old road axes changed single-road connectivity");
    auto network = city.road_network();
    require(network.size()==50,"adjacent roads merged into an avenue");
    require(std::all_of(network.begin(),network.end(),[](const auto& n){return n.first==n.last;}),"single roads merged parallel tiles");
    auto routes = city.traffic_routes(); require(routes.size()==1,"adjacent road traffic is disconnected");
    for (auto p : routes.front()) require(city.road(City::cell(p.GetX(),p.GetZ())),"single-road traffic leaves the deck");
    require(city.add_road(City::road_stroke({54,60},{54,69}),error),"avenue crossing failed");
    network = city.road_network();
    require(std::count_if(network.begin(),network.end(),[](const auto& n){return n.edges.size()==4;})==2,"crossing merged adjacent road junctions");
    City corner = City::create("Irregular road corner");
    require(corner.add_land({60,60},{70,70},error),"corner land failed");
    require(corner.add_road(City::road_stroke({60,64},{64,60}),error) && corner.add_road(City::road_stroke({60,65},{65,60}),error),"parallel bends failed");
    for (const auto& node : corner.road_network()) for (int z = node.first.z; z<=node.last.z; ++z) for (int x = node.first.x; x<=node.last.x; ++x)
        require(corner.road({x,z}),"merged road filled a missing corner");
    for (const auto& route : corner.traffic_routes()) for (auto p : route) {
        if (!corner.road(City::cell(p.GetX(),p.GetZ()))) {
            const auto cell = City::cell(p.GetX(),p.GetZ());
            std::cerr << "Parallel corner: " << p.GetX() << ',' << p.GetZ() << " in " << cell.x << ',' << cell.z << '\n';
        }
        require(corner.road(City::cell(p.GetX(),p.GetZ())),"parallel corner traffic leaves pavement");
    }
    for (const auto& route : city.traffic_routes()) for (auto p : route)
        require(city.road(City::cell(p.GetX(),p.GetZ())),"crossing route leaves pavement");
    const Vec3 c = City::center({56,61});
    const Vec3 a = c+Vec3(-2.5f,0,0), b = c+Vec3(2.5f,0,0);
    require(city.add_vehicle({CityVehicleKind::Car,a,0},error) && city.add_vehicle({CityVehicleKind::Car,b,0},error),"two vehicles cannot share a block");
    require(city.vehicle_at(a)==0 && city.vehicle_at(b)==1 && city.vehicle_at(c)==-1,"selection still uses vehicle blocks");
    require(!city.add_vehicle({CityVehicleKind::Car,a+Vec3(1,0,0),0},error),"overlapping vehicles were accepted");
    require(!city.add_vehicle({CityVehicleKind::Car,Vec3(std::numeric_limits<float>::infinity(),City::level,0),0},error),"non-finite vehicle position accepted");
    require(city.set_spawn({53,60},error),"archipelago spawn failed");
    City edited = city;
    require(edited.erase(a,error) && edited.vehicles.size()==1 && edited.vehicles[0].position==b,"bulldozer removed the wrong vehicle in a shared block");
    require(!city.add_building({{56,61},1,12},error),"building covered freely placed vehicles");
    require(city.save(directory,error),error.c_str()); City loaded;
    require(City::load(directory/(city.id+".city"),loaded,error),error.c_str());
    require(loaded.tiles==city.tiles && loaded.road_axes==city.road_axes && loaded.vehicles[0].position==a && loaded.vehicles[1].position==b,"new save lost bridges, axes, or exact positions");
    Environment map(loaded); PhysicsWorld world(map);
    const Vec3 bridge = City::center({63,64}); GroundHit hit;
    require(map.land_islands().size()==2 && !map.bridge_segments().empty(),"bridge joined the island terrain");
    require(map.terrain_height(bridge.GetX(),bridge.GetZ())<0 && map.height(bridge.GetX(),bridge.GetZ())==City::level,"water under bridge was filled");
    require(world.cast_ground(bridge+Vec3(0,5,0),Vec3(0,-1,0),10,hit) && std::abs(hit.point.GetY()-City::level)<.001f,"bridge has no matching collision deck");
    require(world.camera_fraction(bridge+Vec3(0,.5f,0),Vec3(0,0,-10))<1,"bridge railing has no collision");
    require(world.camera_fraction(bridge+Vec3(0,.5f,0),Vec3(0,0,City::block))==1,"bridge lanes have an internal railing");
    require(map.surface_height(bridge-Vec3(0,4,0))<0,"under-bridge surface jumps to deck");
    require(loaded.erase(CityCell{63,64},error) && loaded.tile({63,64})==CityTile::Water,"demolishing bridge does not restore water");
    require(loaded.add_land({63,65},{63,65},error) && loaded.tile({63,65})==CityTile::Road,"reclaimed bridge does not become a land road");
    return city;
}
void dead_ends() {
    City city = City::create("Dead ends"); std::string error;
    require(city.add_land({60,60},{72,69},error),"dead-end island failed");
    require(city.add_road(City::road_stroke({61,64},{71,64}),error),"dead-end road failed");
    require(city.set_spawn({66,61},error),"dead-end spawn failed");
    Environment map(city); PhysicsWorld world(map); Car starter(world); Traffic traffic(world,map);
    Player player(world,starter,map,nullptr,&traffic);
    std::vector<bool> reached(traffic.cars().size()), returned(traffic.cars().size());
    for (int i = 0; i<120*45; ++i) {
        player.step({},{});
        if (i%60==0) for (std::size_t j = 0; j<traffic.cars().size(); ++j) {
            const Vec3 p = traffic.cars()[j].car->position(); auto cell = City::cell(p.GetX(),p.GetZ());
            if (!city.road(cell)) std::cerr << "Dead end: car " << j << " at " << p.GetX() << ',' << p.GetZ() << " after " << i/120.f << "s\n";
            require(city.tile(cell)==CityTile::Road,"car drove off a dead-end road");
            if (cell.x==61 || cell.x==71) reached[j] = true;
            if (reached[j] && cell.x>=64 && cell.x<=68) returned[j] = true;
        }
    }
    require(std::count(returned.begin(),returned.end(),true)>=2,"traffic did not turn around at dead ends");
    Police police(world,map,&traffic);
    player.respawn_on_foot(map.spawn()-Vec3(0,.48f,0));
    police.crime(Crime::OfficerAssault,player.position(),police.units()[0].officers[0].character.get());
    // Run through the normal Player police integration on a small custom island.
    Player pursued(world,starter,map,nullptr,&traffic,nullptr,nullptr,&police);
    police.crime(Crime::OfficerHomicide,pursued.position(),police.units()[0].officers[0].character.get());
    for (int i = 0; i<120*8; ++i) { pursued.character().revive(); pursued.step({},{}); }
    require(std::count_if(police.units().begin(),police.units().end(),[](const auto& u){return u.active;})>=2,"police could not reinforce on a small island");
}
void car_designs(const std::filesystem::path& directory) {
    std::string error; CarDesign design; design.name="Custom patrol"; design.body="body.GLB"; design.wheel="wheel.glb";
    design.type=CarType::Police; design.width=2.4f; design.height=1.6f; design.length=4.8f;
    design.offset={.35f,.2f,-.65f};
    design.tuning.wheelbase=3.1f; design.tuning.track_width=1.8f; design.tuning.top_speed=40;
    require(design.save(directory,error),error.c_str());
    CarDesign loaded; require(CarDesign::load(directory/"car-Custom patrol.car",loaded,error),error.c_str());
    require(loaded.body==design.body && loaded.wheel==design.wheel && loaded.type==CarType::Police
        && loaded.length==design.length && loaded.offset==design.offset && loaded.tuning.top_speed==40 && loaded.tuning.wheelbase==3.1f,"Saved car settings changed");
    auto bad=design; bad.width=std::numeric_limits<float>::quiet_NaN();
    require(!bad.save(directory,error) && CarDesign::load(directory/"car-Custom patrol.car",loaded,error) && loaded.width==2.4f,"Invalid car replaced its saved file");
    bad=design; bad.body="../body.glb"; require(!bad.validate(error),"Car model traversal accepted");
    bad=design; bad.type=CarType(4); require(!bad.validate(error),"Unsupported car type accepted");
    bad=design; bad.tuning.travel=.45f; bad.tuning.rest_length=.3f; require(!bad.validate(error),"Impossible suspension accepted");
    bad=design; bad.offset[1]=std::numeric_limits<float>::quiet_NaN(); require(!bad.save(directory,error),"Nonfinite body offset accepted");
    bad=design; bad.offset[2]=4.01f; require(!bad.validate(error),"Out-of-range body offset accepted");
    std::stringstream truncated; design.write(truncated); std::stringstream broken(truncated.str().substr(0,40));
    require(!CarDesign::read(broken,loaded,error) && loaded.body==design.body,"Corrupt car mutated the output");
    std::ofstream(directory/"body.GLB") << "model discovery";
    require(car_models(directory,error)==std::vector<std::string>{"body.GLB"},"GLB discovery missed an uppercase extension");
    std::ofstream(directory/"wheel.glb") << "model discovery";
    require(car_models(directory,error).size()==2 && saved_cars(directory,error).size()==1,"Adding a model did not refresh discovery");
    std::ostringstream legacy; legacy << std::setprecision(std::numeric_limits<float>::max_digits10)
        << std::quoted(design.name) << ' ' << std::quoted(design.body) << ' ' << std::quoted(design.wheel) << ' '
        << int(design.type) << ' ' << design.width << ' ' << design.height << ' ' << design.length << '\n';
    for (const auto& c:tuning_controls) legacy << design.tuning.*c.value << ' ';
    legacy << '\n'; std::ofstream(directory/"legacy.car") << "AMBARETTO_CAR 1\n" << legacy.str();
    require(CarDesign::load(directory/"legacy.car",loaded,error) && loaded.offset==std::array<float,3>{} && loaded.tuning.wheelbase==3.1f,"Version 1 car did not migrate to zero offsets");
    std::filesystem::remove(directory/"legacy.car");
    City city=City::create("Custom cars"); require(city.add_land({50,50},{75,75},error),error.c_str());
    CityVehicle placed{CityVehicleKind::Car,City::center({60,60}),1}; placed.car=design;
    require(city.add_vehicle(placed,error),error.c_str()); placed.position=City::center({62,60});
    require(city.add_vehicle(placed,error),error.c_str()); require(city.save(directory,error),error.c_str());
    std::ifstream header(directory/(city.id+".city")); std::string magic; int version; header>>magic>>version;
    require(version==11,"City did not embed car offsets in version 11");
    City copy; require(City::load(directory/(city.id+".city"),copy,error),error.c_str());
    require(copy.vehicles.size()==2 && copy.vehicles[0].car && copy.vehicles[0].car->tuning.track_width==1.8f
        && copy.vehicles[0].rotation==1 && copy.vehicles[0].car->offset==design.offset,"Embedded car, offset or rotation was lost");
    std::ifstream saved_city(directory/(city.id+".city")); std::string old_city((std::istreambuf_iterator<char>(saved_city)),{});
    old_city.replace(0,std::string("AMBARETTO_CITY 11").size(),"AMBARETTO_CITY 10");
    const auto legacy_id=City::create("Legacy car").id; old_city.replace(old_city.find(city.id),city.id.size(),legacy_id);
    std::ostringstream modern; design.write(modern);
    for (std::size_t pos=old_city.find(modern.str());pos!=std::string::npos;pos=old_city.find(modern.str())) old_city.replace(pos,modern.str().size(),legacy.str());
    std::ofstream(directory/(legacy_id+".city")) << old_city;
    City migrated; require(City::load(directory/(legacy_id+".city"),migrated,error),error.c_str());
    require(migrated.vehicles[0].car->offset==std::array<float,3>{}
        && migrated.vehicles[0].car->tuning.track_width==1.8f,"Version 10 city did not migrate its car offsets");
    auto resized=design; resized.width=3; resized.tuning.acceleration=12;
    require(resized.save(directory,error),error.c_str());
    const auto positions=copy.vehicles;
    require(copy.update_car_designs(saved_cars(directory,error),error)==2 && copy.vehicles[1].car->width==3
        && copy.vehicles[0].car->offset==design.offset && copy.vehicles[1].car->tuning.acceleration==12,"Library edits did not update all placed copies");
    for (int i=0;i<2;++i) require(copy.vehicles[i].position==positions[i].position && copy.vehicles[i].rotation==positions[i].rotation,"Refreshing a car design moved its placed copies");
    require(copy.update_car_designs({resized},error)==0 && error.empty(),"Unchanged designs marked the city as edited");
    require(copy.update_car_designs({},error)==0 && copy.vehicles[0].car->width==3,"Missing library discarded the embedded design");
    auto unrelated=resized; unrelated.name="Another car";
    require(copy.update_car_designs({unrelated},error)==0 && copy.vehicles[0].car->name==design.name,"Refreshing an unrelated design replaced a placed car");
    resized.width=4; resized.length=8; copy.vehicles[1].position=copy.vehicles[0].position+Vec3(0,0,5);
    require(copy.update_car_designs({resized},error)==0 && !error.empty() && copy.vehicles[0].car->width==3,"Overlapping library edits partially updated the map");
    auto offset_only=*copy.vehicles[0].car; offset_only.offset[0]=4;
    require(copy.update_car_design(offset_only,error)==-1 && copy.vehicles[0].car->offset==design.offset && copy.vehicles[1].car->offset==design.offset,"Overlapping body offset partially updated placed cars");
    PhysicsWorld world(false); Car car(world,design); car.reset({0,car.ride_height(),0});
    GroundHit hit;
    for (float yaw:{0.f,1.57079633f}) {
        car.reset({0,car.ride_height(),0},yaw); const auto center=car.position()+car.rotate(car.body_offset()), axis=car.rotate(Vec3::sAxisX());
        require(world.cast_ray(center-axis*5,axis,10,hit) && (hit.point-(center-axis*(design.width/2))).Length()<.01f,"Offset/rotated body does not match collision");
    }
    const auto original_half=placed.half_size(); auto shifted=design; shifted.offset[0]=-4; shifted.offset[2]=4;
    CityVehicle expanded=placed; expanded.car=shifted;
    require(expanded.half_size().GetX()>original_half.GetX() && expanded.half_size().GetZ()>original_half.GetZ(),"Body offsets did not expand the placement footprint");
    const auto position=car.position(); const auto mounts=car.wheels(); car.set_design(shifted);
    require((car.position()-position).Length()<.001f && car.wheels()[0].mount==mounts[0].mount,"Body offset moved the wheel reference frame");
    require(car.type()==CarType::Police && car.tuning().wheelbase==3.1f && std::abs(car.wheels()[0].mount.GetZ()+1.55f)<.001f,"Saved role or wheel fitting was not applied");
    auto tuning=car.tuning(); tuning.wheel_radius=.4f; car.set_tuning(tuning);
    require(car.design()->tuning.wheel_radius==.4f,"Live wheel size differs from the rendered design");
    City streets=fixture(); Environment environment(streets); PhysicsWorld traffic_world(environment);
    const std::vector<CarDesign> empty;
    Traffic none(traffic_world,environment,&empty); require(none.cars().empty(),"Empty car library spawned stock cars");
    std::vector<CarDesign> regular{design}; regular[0].type=CarType::Civilian; regular[0].name="Regular car";
    Traffic traffic(traffic_world,environment,&regular);
    require(!traffic.cars().empty() && std::all_of(traffic.cars().begin(),traffic.cars().end(),[](const auto& v){return v.car->design() && v.car->type()==CarType::Civilian && v.car->tuning().top_speed==40;}),"Traffic did not use regular saved designs");
    const std::vector<CarDesign> patrol{design}; Police police(traffic_world,environment,nullptr,nullptr,&patrol);
    require(std::all_of(police.units().begin(),police.units().end(),[](const auto& u){return u.car->design() && u.car->type()==CarType::Police && u.car->tuning().wheelbase==3.1f;}),"Patrol cars did not use saved police designs");
    Car starter(traffic_world); Police no_patrol(traffic_world,environment,&traffic,nullptr,&regular);
    Player player(traffic_world,starter,environment,nullptr,&traffic,nullptr,nullptr,&no_patrol);
    no_patrol.crime(Crime::OfficerAssault,player.position(),no_patrol.units()[0].officers[0].character.get());
    for(int i=0;i<240;++i) player.step({},{});
    require(no_patrol.wanted().stars()>=3 && std::none_of(no_patrol.units().begin(),no_patrol.units().end(),[](const auto& u){return u.active;}),"Regular-only library spawned stock police cars");
}
void gameplay() {
    City city = fixture(); Environment map(city); PhysicsWorld world(map);
    require(map.land_islands().size()==1 && map.trees().size()==city.trees.size() && map.ports().empty() && map.bridge_segments().empty(),"legacy city objects leaked into new city");
    const Vec3 offshore = City::center({10,10});
    require(map.height(offshore.GetX(),offshore.GetZ())==-8,"new map height is not water");
    GroundHit hit;
    require(world.cast_ground(map.spawn()+Vec3(0,5,0),Vec3(0,-1,0),10,hit) && std::abs(hit.point.GetY()-City::level)<.01f,"rendered land and collision do not match");
    auto routes = city.traffic_routes(); require(routes.size()==1,"connected streets did not share a route");
    for (const auto& route : routes) for (auto p : route) require(city.tile(City::cell(p.GetX(),p.GetZ()))==CityTile::Road,"traffic route leaves road grid");
    Car starter(world); Plane placeholder(world); placeholder.set_simulated(false);
    auto aircraft = parked_aircraft(world,map);
    require(aircraft.size()==2 && aircraft[1]->type()==PlaneType::F18,"placed aircraft selection did not load");
    Traffic traffic(world,map); Pedestrians pedestrians(world,map); Police police(world,map,&traffic,&pedestrians);
    for (const auto& v : traffic.cars()) require((v.car->position()-aircraft[0]->position()).Length()>10,"NPC car spawned inside a placed aircraft");
    Player player(world,starter,map,&placeholder,&traffic,&pedestrians,&aircraft,&police);
    require(player.on_foot() && !starter.simulated() && (player.position()-(map.spawn()-Vec3(0,.48f,0))).Length()<.1f,"player did not start at saved spawn");
    require(traffic.cars().size()>10 && traffic.cars().size()<=201,"new traffic population failed");
    require(std::any_of(pedestrians.people().begin(),pedestrians.people().end(),[](const auto& p){return p.enabled;}),"NPC pedestrians did not spawn on custom streets");
    auto path = police.road_path(City::center({46,52}),City::center({76,83}));
    require(path.size()>3,"police cannot route through new intersections");
    std::vector<Vec3> start; for (auto& v : traffic.cars()) start.push_back(v.car->position());
    int on_road = 0, samples = 0;
    for (int i = 0; i<120*30; ++i) {
        player.step({},{});
        if (i%60==0) for (const auto& v : traffic.cars()) if (v.npc) {
            const Vec3 p = v.car->position();
            require(std::isfinite(p.LengthSq()) && p.GetY()>0 && v.car->velocity().Length()<16,"traffic physics failed on flat map");
            on_road += city.tile(City::cell(p.GetX(),p.GetZ()))==CityTile::Road; ++samples;
        }
    }
    int moving = 0;
    for (std::size_t i = 0; i<traffic.cars().size(); ++i) if (traffic.cars()[i].npc && (traffic.cars()[i].car->position()-start[i]).Length()>15) ++moving;
    std::cout << "City traffic: " << moving << '/' << traffic.cars().size() << " cars moved; " << on_road << '/' << samples << " road samples\n";
    require(moving>int(traffic.cars().size()/3) && on_road>samples*.95f,"traffic did not circulate on new streets");
    require(std::any_of(police.units().begin(),police.units().end(),[](const auto& u){return u.active;}),"ambient police did not spawn on custom roads");
    // Existing crime reporting and officer AI run unchanged against this map.
    police.crime(Crime::OfficerAssault,player.position(),police.units()[0].officers[0].character.get());
    for (int i = 0; i<120*5; ++i) player.step({},{});
    require(police.wanted().stars()>=3,"wanted levels no longer respond to officer crimes");
    const auto parked = std::find_if(traffic.cars().begin(),traffic.cars().end(),[](const auto& v){return !v.npc;});
    require(parked!=traffic.cars().end(),"placed car missing");
    player.respawn_on_foot(parked->car->position()+parked->car->rotate(Vec3(-2,-.48f,.35f)));
    police.clear(); require(player.interact()==Interaction::Entered && player.driving(),"placed car cannot be entered");
    player.reset(); require(player.driving() && !player.car().destroyed() && (player.position()-map.spawn()).Length()<.1f,"city car recovery lost the current vehicle");
    player.respawn_on_foot(aircraft[0]->boarding_position());
    require(player.interact()==Interaction::Entered && player.flying(),"placed plane cannot be entered");
    aircraft[0]->reset(City::center({10,10},-2)); player.recover_plane();
    require((aircraft[0]->position()-City::center({64,80},City::level+aircraft[0]->parking_height())).Length()<.1f,"plane recovered to removed legacy airport");
}
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && (std::string(argv[1])=="--fixture" || std::string(argv[1])=="--fixture-islands" || std::string(argv[1])=="--fixture-turns" || std::string(argv[1])=="--fixture-diagonals" || std::string(argv[1])=="--fixture-ground" || std::string(argv[1])=="--fixture-elevation")) {
            City city = std::string(argv[1])=="--fixture" ? fixture() : std::string(argv[1])=="--fixture-turns" ? single_road_turns()
                : std::string(argv[1])=="--fixture-diagonals" ? diagonal_road_drags(argv[2]) : std::string(argv[1])=="--fixture-ground" ? ground_paint(argv[2])
                : std::string(argv[1])=="--fixture-elevation" ? terraced_ground(argv[2]) : islands_and_bridges(argv[2]); std::string error;
            if (!city.save(argv[2],error)) throw std::runtime_error(error);
            std::cout << (std::filesystem::path(argv[2])/(city.id+".city")).string() << '\n'; return 0;
        }
        const auto directory = std::filesystem::temp_directory_path()/("ambaretto-city-test-"+City::create("test").id);
        data_and_saves(directory); building_meshes(directory); building_mesh_editing(directory); building_design_updates(directory); building_rotation(directory); generated_terrain(directory); trees_and_time(directory); ground_paint(directory); custom_ground_textures(directory); terraced_ground(directory); sloping_shore(); rounded_roads(); single_road_turns(); road_ports_and_diagonals(); diagonal_road_drags(directory); diagonal_bridge_deck(); islands_and_bridges(directory); dead_ends(); gameplay(); car_designs(directory);
        // Remove only files in this run's unique test directory; no recursive deletion.
        for (const auto& entry : std::filesystem::directory_iterator(directory)) std::filesystem::remove(entry.path());
        std::filesystem::remove(directory);
        std::cout << "City builder checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
