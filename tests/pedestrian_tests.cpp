#include "pedestrians.hpp"
#include "environment.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>

namespace {
using namespace ambaretto;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::size_t walking_count(const Pedestrians& pedestrians, const Environment& map) {
    std::size_t count = 0;
    for (const auto& person : pedestrians.people()) if (person.enabled) {
        const Vec3 p = person.character->position();
        require(map.terrain_height(p.GetX(), p.GetZ()) > Environment::water_level + .1f, "pedestrian spawned in water");
        require(person.character->can_stand_at(p), "pedestrian spawned inside a building or obstacle");
        ++count;
    }
    return count;
}
void city_density() {
    City city=City::create("Pedestrian density"); std::string error;
    require(city.add_land({50,50},{78,78},error) && city.add_road(City::road_stroke({52,64},{76,64}),error)
        && city.add_road(City::road_stroke({64,52},{64,76}),error) && city.set_spawn({63,63},error),"Density fixture failed");
    std::size_t previous = 0;
    for (int density : {0,1,25,50,100}) {
        city.pedestrian_density = density;
        Environment map(city); PhysicsWorld world(map); Car starter(world); starter.set_simulated(false);
        Pedestrians pedestrians(world,map); const auto limit = std::size_t((64*density+99)/100);
        require(pedestrians.people().size()==limit,"Density did not reduce the allocated pedestrian pool");
        pedestrians.prepare(starter,nullptr,map.spawn(),.6f);
        const auto active = walking_count(pedestrians,map);
        require(active<=limit && active>=previous && (density==0 ? active==0 : active>0),"Density did not bound the walking population");
        std::cout<<density<<"%: "<<active<<" walking pedestrians, pool "<<limit<<'\n'; previous = active;
    }
}
void city_turns(bool crowded=false,int rotation=0) {
    City city=City::create("Walking through turns"); std::string error;
    const auto rotate=[&](CityCell cell) {for (int i=0;i<rotation;++i) cell={128-cell.z,cell.x}; return cell;};
    require(city.add_land({50,50},{78,78},error) && city.add_road(City::road_stroke(rotate({60,54}),rotate({60,65})),error)
        && city.add_road(City::road_stroke(rotate({60,65}),rotate({72,65})),error) && city.set_spawn(rotate({63,61}),error),"Walking turn fixture failed");
    Environment map(city); PhysicsWorld world(map); Car starter(world); starter.set_simulated(false);
    Pedestrians pedestrians(world,map);
    const Pedestrian* walker=nullptr; const Vec3 turn=City::center(rotate({60,65}));
    for (const auto& person:pedestrians.people()) if (person.enabled
        && (!walker || (person.character->position()-turn).LengthSq()<(walker->character->position()-turn).LengthSq())) walker=&person;
    require(walker,"No pedestrian spawned at the road turn");
    if (!crowded) for (const auto& person:pedestrians.people()) if (&person!=walker) {person.character->reset({600,100,600}); person.character->set_enabled(false);}
    std::vector<Vec3> starts; std::vector<float> progress(pedestrians.people().size());
    for (const auto& person:pedestrians.people()) starts.push_back(person.character->position());
    const Vec3 start=walker->character->position(); std::set<std::pair<int,int>> cells;
    float distance=0;
    for (int i=0;i<120*30;++i) {
        world.step(); pedestrians.step(starter,nullptr,{500,100,500});
        const Vec3 p=walker->character->position(); const auto cell=City::cell(p.GetX(),p.GetZ());
        if (crowded) for (std::size_t j=0;j<progress.size();++j) if (pedestrians.people()[j].enabled) {
            const Vec3 current=pedestrians.people()[j].character->position();
            progress[j]=std::max(progress[j],(current-starts[j]).Length());
            require(map.terrain_height(current.GetX(),current.GetZ())>Environment::water_level+.1f,"Passing pedestrian left the turn's terrain");
        }
        cells.emplace(cell.x,cell.z); distance=std::max(distance,(p-start).Length());
        require(map.terrain_height(p.GetX(),p.GetZ())>Environment::water_level+.1f && std::abs(p.GetY()-map.terrain_height(p.GetX(),p.GetZ()))<.2f,"Pedestrian left the turn's terrain");
    }
    if (!crowded) require(cells.size()>=3 && distance>15,"Pedestrian stayed on a tiny turn segment instead of continuing along the street");
    if (crowded) {
        const auto enabled=std::count_if(pedestrians.people().begin(),pedestrians.people().end(),[](const auto& p){return p.enabled;});
        const auto moving=std::count_if(progress.begin(),progress.end(),[](float distance){return distance>15;});
        std::cout<<moving<<"/"<<enabled<<" pedestrians continued through crowded turns\n";
        require(moving*4>=enabled*3,"Opposing pedestrians jammed the turn");
    }
}
void city_bridge() {
    City city=City::create("Walkable bridge approaches"); std::string error;
    require(city.add_land({50,58},{58,70},error) && city.add_land({68,58},{76,70},error)
        && city.add_road(City::road_stroke({51,64},{75,64}),error) && city.set_spawn({54,62},error),"Walking bridge fixture failed");
    Environment map(city); PhysicsWorld world(map); Car starter(world); starter.set_simulated(false);
    Pedestrians pedestrians(world,map); bool west=false,east=false;
    for (const auto& person:pedestrians.people()) if (person.enabled) {
        const auto cell=City::cell(person.character->position().GetX(),person.character->position().GetZ());
        west|=cell.x<=58; east|=cell.x>=68;
    }
    require(west && east && walking_count(pedestrians,map)>=12,"Bridge removed pedestrians from its walkable approaches");
    for (int i=0;i<120*15;++i) {
        world.step(); pedestrians.step(starter,nullptr,{500,100,500});
        for (const auto& person:pedestrians.people()) if (person.enabled) {
            const Vec3 p=person.character->position();
            require(map.terrain_height(p.GetX(),p.GetZ())>Environment::water_level+.1f && std::abs(p.GetY()-map.terrain_height(p.GetX(),p.GetZ()))<.2f,"Pedestrian walked into bridge water");
        }
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        city_density();
        if (argc==2 && std::string(argv[1])=="city") {
            for (int rotation=0;rotation<4;++rotation) {city_turns(false,rotation); city_turns(true,rotation);}
            city_bridge(); return 0;
        }
        const Environment map;
        PhysicsWorld world(map);
        Car car(world);
        car.reset(map.spawn());
        Pedestrians pedestrians(world, map);
        require(pedestrians.people().size() == 64, "pedestrian physics pool is not bounded");
        const auto initial_count = walking_count(pedestrians, map);
        require(initial_count >= 12, "downtown has too few walking pedestrians");
        const auto walker = [&]() -> Character& {
            for (const auto& person : pedestrians.people()) if (person.enabled) return *person.character;
            throw std::runtime_error("no visible pedestrian");
        };
        Character& character = walker();
        const Vec3 start = character.position();
        for (int i = 0; i < 120; ++i) {
            pedestrians.prepare(car, nullptr, map.spawn()); world.step();
            pedestrians.step(car, nullptr, map.spawn());
        }
        require((character.position() - start).Length() > 1, "pedestrian did not walk along the sidewalk");
        require(character.grounded() && std::abs(character.position().GetY()
            - map.terrain_height(character.position().GetX(), character.position().GetZ())) < .2f,
            "walking pedestrian left the ground");
        pedestrians.prepare(car, nullptr, Vec3(-360, 4, 1500), .6f);
        require(walking_count(pedestrians, map) == 0, "Ngawish still spawns a town population");
        for (const Vec3 town : {Vec3(1320, 4, 0), Vec3(-780, 4, 2220), Vec3(-1320, 4, 2940), Vec3(-2100, 4, 4380)}) {
            pedestrians.prepare(car, nullptr, town, .6f);
            std::cout << "Town " << town.GetX() << ',' << town.GetZ() << ": " << walking_count(pedestrians, map) << " pedestrians.\n";
            require(walking_count(pedestrians, map) >= 8, "pedestrians did not stream into a beach or Keys town");
        }
        for (int i = 0; i < 120; ++i) { car.step({1, 0, false}); world.step(); }
        Character& target = walker();
        Vec3 hit = car.position() + car.forward() * 4;
        hit.SetY(map.terrain_height(hit.GetX(), hit.GetZ()) + .08f);
        target.reset(hit);
        for (int i = 0; i < 160 && !target.ragdolling(); ++i) {
            car.step({1, 0, false});
            pedestrians.prepare(car, nullptr, hit);
            world.step();
            pedestrians.step(car, nullptr, hit);
        }
        require(target.ragdolling(), "moving car did not ragdoll the pedestrian");
        for (int rotation=0;rotation<4;++rotation) {city_turns(false,rotation); city_turns(true,rotation);}
        city_bridge();
        std::cout << initial_count << " downtown pedestrians; walking, city turns, town streaming and car impact passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
