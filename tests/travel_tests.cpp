#include "environment.hpp"
#include "traffic.hpp"
#include "pedestrians.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ambaretto;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void travel(const Environment& map) {
    PhysicsWorld world(map); Car starter(world); starter.set_simulated(false);
    Traffic traffic(world,map); Pedestrians pedestrians(world,map);
    std::cout << "Traffic pool: " << traffic.cars().size() << '\n';
    const Vec3 origin = map.spawn();
    for (int visit = 0; visit < 6; ++visit) {
        const Vec3 focus = visit%2 ? origin+Vec3(1800,0,1800) : origin;
        const auto begin = std::chrono::steady_clock::now();
        for (int step = 0; step < 600; ++step) {
            traffic.step(&starter,starter,nullptr,nullptr,focus);
            pedestrians.prepare(starter,&traffic,focus);
            world.step(); traffic.finish(focus); pedestrians.step(starter,&traffic,focus);
        }
        unsigned active = 0, wrecks = 0, people = 0;
        for (const auto& vehicle : traffic.cars()) if (vehicle.car->simulated()) {
            ++active; wrecks += vehicle.car->destroyed();
        }
        for (const auto& person : pedestrians.people()) people += person.enabled;
        require(active <= Traffic::physical_limit,"Travel exceeded the physical car budget");
        if (visit%2) require(active == 0,"Distant traffic, parked cars or wrecks stayed physical");
        const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/600;
        std::cout << "Visit " << visit << ": " << ms << " ms/tick, " << active << " physical cars ("
            << wrecks << " wrecks), " << people << " pedestrians\n";
    }
}
void parked_and_wrecked(const Environment& map) {
    PhysicsWorld world(map); Car starter(world); starter.set_simulated(false);
    Traffic traffic(world,map);
    require(!traffic.cars().empty(),"No cars for the travel lifetime check");
    Car& claimed = *traffic.cars().front().car;
    claimed.set_simulated(false); require(traffic.steal(claimed),"Could not claim the test car");
    const Vec3 position = map.spawn()+Vec3(20,0,20);
    claimed.reset(position); claimed.take_damage(25);
    const auto sync = [&](Vec3 focus,Car* controlled = nullptr) {
        for (int i = 0; i < 62; ++i) traffic.step(controlled,starter,nullptr,nullptr,focus);
    };
    sync(position); require(claimed.simulated(),"Nearby claimed car failed to wake");
    for (int i = 0; i < 8; ++i) {
        sync(position+Vec3(1800,0,1800));
        require(!claimed.simulated() && claimed.health()==75 && (claimed.position()-position).Length()<.001f
            && !traffic.is_npc(&claimed),"Distant claimed car lost state or remained in physics");
        sync(position+Vec3(0,0,10));
        require(claimed.simulated(),"Returning did not restore the claimed car");
    }
    sync(position+Vec3(1800,0,1800),&claimed);
    require(claimed.simulated(),"Travel suspended the controlled car");
    claimed.take_damage(100);
    sync(position+Vec3(1800,0,1800));
    require(!claimed.simulated() && claimed.destroyed(),"Distant wreck stayed in physics or was repaired");
    sync(position+Vec3(0,0,10));
    require(claimed.simulated() && claimed.destroyed(),"Returning lost the wreck");
    std::cout << "Claimed car and wreck: repeated suspend/resume preserved ownership, position and damage\n";
}
}
int main(int argc, char** argv) {
    try {
        City city; std::string error;
        if (argc > 1) require(City::load(argv[1],city,error),error);
        else {
            city = City::create("Travel budget");
            require(city.add_land({54,54},{74,74},error) && city.set_spawn({64,64},error),error);
            require(city.add_road(City::road_stroke({55,64},{73,64}),error),error);
            for (int z = 0; z < 5; ++z) for (int x = 0; x < 6; ++x)
                require(city.add_vehicle({CityVehicleKind::Car,City::center({58+x*2,58+z*2})},error),error);
        }
        Environment map(city); travel(map); parked_and_wrecked(map);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
