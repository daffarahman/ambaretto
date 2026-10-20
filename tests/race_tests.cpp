#include "race.hpp"
#include "traffic.hpp"
#include "environment.hpp"
#include "play_session.hpp"
#include "day_night.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace ambaretto;
void require(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
City fixture() {
    City city=City::create("Race city"); std::string error;
    require(city.add_land({47,47},{81,81},error),error);
    for (auto pair : {std::make_pair(CityCell{50,50},CityCell{78,50}),std::make_pair(CityCell{78,50},CityCell{78,78}),
        std::make_pair(CityCell{78,78},CityCell{50,78}),std::make_pair(CityCell{50,78},CityCell{50,50})})
        require(city.add_road(City::road_stroke(pair.first,pair.second),error),error);
    require(city.set_spawn(City::center({55,50}),error),error);
    RaceMap race; race.name="City circuit"; race.laps=2;
    for (auto cell : {CityCell{55,50},CityCell{73,50},CityCell{78,73},CityCell{53,78}}) {
        Vec3 point; require(snap_race_point(city,City::center(cell),point),"Road click did not snap"); race.checkpoints.push_back(point);
    }
    city.races.push_back(race); return city;
}
void routes_and_saves() {
    auto city=fixture(); std::string error; std::vector<RaceLeg> legs;
    require(city.validate(error),error); require(build_race_route(city,city.races[0],legs,error),error);
    require(legs.size()==4 && legs[1].points.size()>20,"Route did not follow intervening roads");
    for (const auto& leg : legs) for (Vec3 p : leg.points) { Vec3 road; require(snap_race_point(city,p,road),"Road route cuts across land"); }
    auto invalid=city.races[0]; invalid.checkpoints[1]=City::center({65,65});
    require(!build_race_route(city,invalid,legs,error),"Off-road checkpoint accepted");
    invalid=city.races[0]; invalid.laps=0; require(!build_race_route(city,invalid,legs,error),"Zero laps accepted");
    invalid=city.races[0]; invalid.checkpoints[1]=invalid.checkpoints[0]; require(!build_race_route(city,invalid,legs,error),"Repeated checkpoint accepted");
    invalid=city.races[0]; invalid.checkpoints[1]=Vec3(std::numeric_limits<float>::quiet_NaN(),0,0); require(!build_race_route(city,invalid,legs,error),"Non-finite checkpoint accepted");
    auto disconnected=city; disconnected.tiles[City::index({65,65})]=CityTile::Road; disconnected.tiles[City::index({66,65})]=CityTile::Road;
    invalid=city.races[0]; invalid.checkpoints[1]=City::center({65,65}); require(!build_race_route(disconnected,invalid,legs,error),"Disconnected checkpoint accepted");
    const auto directory=std::filesystem::temp_directory_path()/city.id;
    require(city.save(directory,error),error); City loaded;
    require(City::load(directory/(city.id+".city"),loaded,error),error);
    require(loaded.races.size()==1 && loaded.races[0].name==city.races[0].name && loaded.races[0].laps==2 && (loaded.races[0].checkpoints[1]-city.races[0].checkpoints[1]).Length()<.001f,"Race save changed its course");
    require(!loaded.erase(City::cell(loaded.races[0].checkpoints[1].GetX(),loaded.races[0].checkpoints[1].GetZ()),error),"Bulldoze removed a saved checkpoint's road");
    auto outback=loaded; outback.races[0].checkpoints.resize(2); require(outback.save(directory,error),error);
    require(City::load(directory/(city.id+".city"),outback,error) && outback.races[0].checkpoints.size()==2,"Single-checkpoint race did not round-trip");
    loaded.races.clear(); require(loaded.save(directory,error),error);
    require(City::load(directory/(city.id+".city"),loaded,error) && loaded.races.empty(),"Older city without races failed to load");
    loaded=city; loaded.races.push_back(city.races[0]); require(!loaded.validate(error),"Duplicate race names accepted");
    std::filesystem::remove_all(directory);
    City diagonal=City::create("Diagonal race"); require(diagonal.add_land({49,49},{79,79},error),error);
    require(diagonal.add_road(City::road_stroke({52,52},{76,76},false,true),error),error);
    require(diagonal.add_road(City::road_stroke({76,76},{52,76}),error),error);
    require(diagonal.add_road(City::road_stroke({52,76},{52,52}),error),error);
    RaceMap triangle;
    std::vector<Vec3> diagonal_points;
    for (const auto& node : diagonal.road_network()) {
        const auto path=node.path();
        if (path.size()==2 && std::abs(path[1].GetX()-path[0].GetX())>.1f && std::abs(path[1].GetZ()-path[0].GetZ())>.1f) {
            Vec3 point;
            require(snap_race_point(diagonal,(path[0]+path[1])/2,point),"Diagonal click failed"); diagonal_points.push_back(point);
        }
    }
    require(diagonal_points.size()>1,"Diagonal race fixture has no diagonal streets"); triangle.checkpoints={diagonal_points.front(),diagonal_points.back()};
    Vec3 last; require(snap_race_point(diagonal,City::center({54,76}),last),"Diagonal return click failed"); triangle.checkpoints.push_back(last);
    require(build_race_route(diagonal,triangle,legs,error),error);
    for (const auto& leg : legs) for (Vec3 p : leg.points) { Vec3 road; require(snap_race_point(diagonal,p,road),"Diagonal race cut outside pavement"); }
}
void grid_progress_and_modes() {
    auto city=fixture(); Environment map(city); PhysicsWorld world(map);
    CarDesign civilian; civilian.name="Racer"; civilian.body="body.glb"; civilian.wheel="wheel.glb"; CarDesign police=civilian; police.name="Police"; police.type=CarType::Police;
    const std::vector<CarDesign> designs{police,civilian}; const std::vector<CharacterDesign> characters;
    Car car(world,civilian); Traffic traffic(world,map,&designs,&characters); Player player(world,car,map,nullptr,&traffic);
    player.set_play_mode(PlayMode::CarOnly); std::string error;
    const auto original=car.position(); auto invalid=city.races[0]; invalid.checkpoints[1]=invalid.checkpoints[0]+Vec3(10,0,0);
    require(!Race::start(world,map,traffic,car,invalid,designs,error),"Grid accepted a checkpoint before its front row");
    require(traffic.cars().empty() && (car.position()-original).Length()<.01f,"Failed start mutated cars");
    auto race=Race::start(world,map,traffic,car,city.races[0],designs,error); require(bool(race),error);
    auto sounds=world.take_sound_events();
    require(sounds.size()==1 && sounds[0].effect==SoundEffect::Countdown,"Race start did not sound the 3 beat");
    require(!Race::start(world,map,traffic,car,city.races[0],designs,error) && traffic.cars().size()==4,"A second race removed active opponents");
    player.set_play_mode(PlayMode::Race); require(player.interact()==Interaction::Blocked,"Race allowed exiting the car");
    std::vector<RaceLeg> legs; require(build_race_route(city,city.races[0],legs,error),error);
    for (int i = 1; i < 5; ++i) {
        require(race->cars()[i]->type()==CarType::Civilian && race->cars()[i]->simulated(),"Opponent is police or streams away");
        require((race->cars()[i]->position()-car.position()).Dot(legs[0].direction(0))>0,"Player is not behind the opponents");
    }
    for (int i = 0; i < 120; ++i) { race->step(); player.step({0,0,false,true},{}); race->finish(); }
    require(race->countdown()>1.9f && race->progress().next==1,"Countdown advanced checkpoints");
    for (int i = 0; i < 241; ++i) race->step();
    sounds=world.take_sound_events();
    require(sounds.size()==3 && sounds[0].effect==SoundEffect::Countdown && sounds[1].effect==SoundEffect::Countdown
        && sounds[2].effect==SoundEffect::RaceStart,"Countdown did not play exactly 2, 1, then GO");
    race->step(); require(world.take_sound_events().empty(),"GO sound repeated after the countdown");
    for (int i = 1; i < 5; ++i) race->cars()[i]->set_simulated(false);
    const auto move=[&](Vec3 point) { point.SetY(map.surface_height(point)+car.ride_height()); car.reset(point); race->finish(); };
    move(city.races[0].checkpoints[2]); require(race->progress().next==1,"Skipped checkpoint advanced the lap");
    for (int lap = 0; lap < 2; ++lap) for (std::size_t i = 0; i < legs.size(); ++i) {
        const auto& leg=legs[i];
        for (float distance=0; distance<leg.length(); distance+=2) move(leg.point(distance));
        move(leg.point(leg.length())+leg.direction(leg.length())*.5f);
        require(race->progress().next==int((i+2)%legs.size()),"Checkpoint did not advance in order");
    }
    require(race->done() && race->progress().lap==2 && race->progress().place==1,"Finish or laps were not counted");
    race.reset(); require(traffic.cars().empty(),"Race opponents leaked into the next race");
    const auto finish=car.position(); player.set_play_mode(PlayMode::FreeRoam);
    require(player.driving() && (player.position()-finish).Length()<.01f,"Returning to Free roam reset the car");
    player.set_play_mode(PlayMode::Race); player.set_play_mode(PlayMode::CarOnly);
    auto saved=PlaySession::capture(player,DayNight{}); require(saved.validate(error),error);
    RaceMap bend=city.races[0]; bend.checkpoints.clear();
    for (auto cell : {CityCell{77,50},CityCell{78,65},CityCell{65,78},CityCell{50,65}}) { Vec3 point; require(snap_race_point(city,City::center(cell),point),"Bend checkpoint failed"); bend.checkpoints.push_back(point); }
    race=Race::start(world,map,traffic,car,bend,designs,error); require(bool(race),error);
    require(std::abs(race->cars()[1]->forward().Dot(car.forward()))<.95f,"Grid did not follow a turn's direction");
}
void checkpoint_contact() {
    auto city=fixture(); Environment map(city); PhysicsWorld world(map);
    CarDesign design; design.body="body.glb"; design.wheel="wheel.glb"; const std::vector<CarDesign> designs{design};
    Car car(world,design); Traffic traffic(world,map,&designs); std::string error;
    auto race=Race::start(world,map,traffic,car,city.races[0],designs,error); require(bool(race),error);
    for (int tick=0; tick<362; ++tick) race->step();
    for (int i=1; i<5; ++i) race->cars()[i]->set_simulated(false);
    std::vector<RaceLeg> legs; require(build_race_route(city,city.races[0],legs,error),error);
    const auto move=[&](Vec3 p,float height=0.f) { p.SetY(map.surface_height(p)+car.ride_height()+height); car.reset(p); race->finish(); };
    const auto approach=[&](const RaceLeg& leg) { for (float d=0; d<leg.length()-10; d+=2) move(leg.point(d)); };
    const auto& leg=legs[0]; approach(leg);
    const Vec3 gate=leg.points.back(), direction=leg.direction(leg.length()), across=direction.Cross(Vec3::sAxisY());
    const float radius=Race::checkpoint_radius+1+std::hypot(car.body_size().GetX(),car.body_size().GetZ())/2;
    move(gate+across*(radius+1)-direction*20);
    move(gate+across*(radius+1)-direction);
    require(race->progress().next==1,"A car outside the contact radius counted");
    const Vec3 edge=gate+across*(radius-.2f)-direction;
    move(edge,10); require(race->progress().next==1,"Checkpoint counted a car above the road");
    move(edge); require(race->progress().next==2 && race->progress().lap==0,"Touching the invisible outer edge before the gate did not count");
    for (int tick=0; tick<5; ++tick) race->finish();
    require(race->progress().next==2,"Lingering at a checkpoint advanced another checkpoint");
    approach(legs[1]);
    move(legs[1].point(legs[1].length())-legs[1].direction(legs[1].length())*10);
    require(race->progress().next==2,"Approaching outside the checkpoint counted");
    move(legs[1].point(legs[1].length())+legs[1].direction(legs[1].length())*10);
    require(race->progress().next==3,"Fast movement through the entire checkpoint skipped contact");
}
void ai_driving() {
    auto city=fixture(); Environment map(city); PhysicsWorld world(map);
    CarDesign design; design.name="AI racer"; design.body="body.glb"; design.wheel="wheel.glb"; const std::vector<CarDesign> designs{design}; const std::vector<CharacterDesign> characters;
    CityVehicle parked(CityVehicleKind::Car,City::center({65,50})+Vec3(0,0,1.53f),3); parked.car=design;
    std::string placement_error; require(city.add_vehicle(parked,placement_error),placement_error);
    Environment traffic_map(city); PhysicsWorld traffic_world(traffic_map);
    Car traffic_player(traffic_world,design); traffic_player.reset(traffic_map.spawn()); Traffic parked_traffic(traffic_world,traffic_map,&designs,&characters);
    std::string passing_error; auto passing=Race::start(traffic_world,traffic_map,parked_traffic,traffic_player,city.races[0],designs,passing_error); require(bool(passing),passing_error);
    traffic_player.set_simulated(false); traffic_player.reset(City::center({65,65})+Vec3(0,traffic_player.ride_height(),0));
    for (int tick = 0; tick < 120*30; ++tick) { passing->step(); parked_traffic.step(&traffic_player,traffic_player,nullptr,nullptr,traffic_player.position()); traffic_world.step(); passing->finish(); }
    for (int i = 1; i < 5; ++i) {
        const auto& car=*passing->cars()[i];
        std::cout << "Passing racer " << i << " next " << passing->progress(i).next << " health " << car.health() << " at " << car.position().GetX() << ',' << car.position().GetZ() << '\n';
        require(passing->progress(i).next!=1 && car.health()>90,"Racer did not pass parked traffic safely");
    }
    Car player(world,design); player.reset(map.spawn()); Traffic traffic(world,map,&designs); std::string error;
    auto race=Race::start(world,map,traffic,player,city.races[0],designs,error); require(bool(race),error);
    player.set_simulated(false); player.reset(City::center({65,65})+Vec3(0,player.ride_height(),0));
    std::array<float,5> speed{}; int samples=0,on_road=0;
    for (int tick = 0; tick < 120*240; ++tick) {
        race->step(); traffic.step(&player,player,nullptr,nullptr,player.position()); world.step(); race->finish();
        if (tick%30) continue;
        for (int i = 1; i < 5; ++i) {
            const auto& car=*race->cars()[i]; Vec3 point;
            if (car.destroyed() || car.rotate(Vec3::sAxisY()).GetY()<=.5f) throw std::runtime_error("Racer "+std::to_string(i)+" crashed at tick "+std::to_string(tick)+" position "+std::to_string(car.position().GetX())+","+std::to_string(car.position().GetZ())+" speed "+std::to_string(car.velocity().Length())+" lap "+std::to_string(race->progress(i).lap)+" next "+std::to_string(race->progress(i).next));
            speed[i]=std::max(speed[i],car.velocity().Length()); ++samples;
            if (snap_race_point(city,car.position(),point)) ++on_road;
        }
    }
    std::cout << "Race road samples: " << on_road << '/' << samples << '\n';
    require(on_road>samples*.98f,"Racers left the road");
    for (int i = 1; i < 5; ++i) {
        std::cout << "Racer " << i << " lap " << race->progress(i).lap << " next " << race->progress(i).next << " max speed " << speed[i]*3.6f << '\n';
        require(race->progress(i).finished,"AI failed to complete two laps"); require(speed[i]>15,"Racer drove at traffic speed");
    }
}
void turnaround() {
    auto city=fixture(); RaceMap map; map.laps=1;
    for (auto cell : {CityCell{55,50},CityCell{73,50}}) { Vec3 p; require(snap_race_point(city,City::center(cell),p),"Turnaround point failed"); map.checkpoints.push_back(p); }
    Environment environment(city); PhysicsWorld world(environment); CarDesign design; design.body="body.glb"; design.wheel="wheel.glb";
    const std::vector<CarDesign> designs{design}; const std::vector<CharacterDesign> people;
    Car player(world,design); player.reset(environment.spawn()); Traffic traffic(world,environment,&designs,&people); std::string error;
    auto race=Race::start(world,environment,traffic,player,map,designs,error); require(bool(race),error);
    player.set_simulated(false); player.reset(City::center({65,65})+Vec3(0,player.ride_height(),0));
    for (int i = 2; i < 5; ++i) race->cars()[i]->set_simulated(false);
    for (int tick = 0; tick < 120*120 && !race->progress(1).finished; ++tick) { race->step(); world.step(); race->finish(); }
    require(race->progress(1).finished && race->cars()[1]->health()>90,"Racer could not turn around on the road");
}
void pushing_blocker() {
    auto city=fixture(); Environment map(city); PhysicsWorld world(map);
    CarDesign design; design.body="body.glb"; design.wheel="wheel.glb"; const std::vector<CarDesign> designs{design};
    Car player(world,design); Traffic traffic(world,map,&designs); std::string error;
    auto race=Race::start(world,map,traffic,player,city.races[0],designs,error); require(bool(race),error);
    for (int i=2; i<5; ++i) race->cars()[i]->set_simulated(false);
    for (int tick=0; tick<362; ++tick) race->step();
    auto wide=design; wide.width=4; player.set_design(wide);
    const Vec3 direction=race->cars()[1]->forward();
    std::vector<RaceLeg> legs; require(build_race_route(city,city.races[0],legs,error),error);
    Vec3 position=legs[0].point(race->progress(1).distance+14);
    position.SetY(map.surface_height(position)+player.ride_height()); player.reset(position,std::atan2(-direction.GetX(),-direction.GetZ()));
    for (int tick=0; tick<120*4; ++tick) {
        race->step(); player.step({}); world.step(); race->finish();
    }
    std::cout << "Blocked car pushed " << (player.position()-position).Dot(direction) << " m\n";
    require((player.position()-position).Dot(direction)>.7f,"Racer waited behind a car blocking both passing lanes instead of pushing it");
    require(!race->cars()[1]->destroyed(),"Pushing one blocker destroyed the racer");
}
}
int main() {
    try { routes_and_saves(); grid_progress_and_modes(); checkpoint_contact(); ai_driving(); turnaround(); pushing_blocker(); std::cout << "Race checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
