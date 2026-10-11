#include "play_session.hpp"
#include "day_night.hpp"
#include "environment.hpp"
#include "traffic.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path); return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
}
int main() {
    using namespace ambaretto;
    auto city = City::create("Session tests");
    const auto directory = std::filesystem::temp_directory_path()/("ambaretto-session-"+city.id);
    try {
        std::string error, notice;
        require(city.add_land({58,58},{86,86},error) && city.set_spawn({64,64},error),"Session map fixture failed");
        CarDesign design; design.name="Session car"; design.body="body.glb"; design.wheel="wheel.glb";
        CityVehicle placed(CityVehicleKind::Car,*city.spawn+Vec3(2.8f,0,0)); placed.car=design;
        require(city.add_vehicle(placed,error),"Placed car fixture failed");
        require(city.add_vehicle({CityVehicleKind::F18,City::center({78,78})},error),"Placed aircraft fixture failed");
        const auto spawn = *city.spawn; const int start_time = city.start_minutes;
        Environment environment(city); PhysicsWorld world(false); Car starter(world);
        auto aircraft = parked_aircraft(world,environment);
        const std::vector<CharacterDesign> no_people;
        Traffic traffic(world,environment,nullptr,&no_people);
        Player player(world,starter,environment,nullptr,&traffic,nullptr,&aircraft);
        DayNight time; require(time.set_time("21:45"),"Clock fixture failed");
        const Vec3 feet(45,3,45);
        player.respawn_on_foot(feet,1.2f); player.character().take_damage(40); player.character().reset(feet,1.2f);
        player.weapons().select(WeaponType::Pistol);
        player.weapons().fire(world,nullptr,Vec3(45,5,45),Vec3(0,0,-1),true,true,false);
        const auto saved = PlaySession::capture(player,time);
        require(saved.save(directory,error),"Progress did not save");
        const auto path=directory/(city.id+".play"); const auto original=contents(path);
        PlaySession loaded;
        require(PlaySession::load(path,loaded,error) && loaded.city_id==city.id && loaded.minutes==1305
            && (loaded.position-feet).Length()<.001f && loaded.ammo[1]==weapon_data(WeaponType::Pistol).magazine-1,"Progress round trip lost state");
        player.respawn_on_foot(Vec3(80,3,80)); time.set_time("08:00");
        loaded.restore(player,time,notice);
        require(player.on_foot() && (player.position()-feet).Length()<.001f && std::abs(player.character().yaw()-1.2f)<.001f
            && player.character().health()==60 && player.weapons().ammo()==loaded.ammo[1] && time.minutes_of_day()==1305,"On-foot continuation lost position, clock, health or ammo");
        for (int invalid=0;invalid<8;++invalid) {
            auto bad=loaded;
            if (invalid==0) bad.city_id="../outside";
            if (invalid==1) bad.minutes=1440;
            if (invalid==2) bad.position=Vec3(std::numeric_limits<float>::quiet_NaN(),0,0);
            if (invalid==3) bad.mode=EntryVehicle(9);
            if (invalid==4) bad.rotation=Quat(0,0,0,0);
            if (invalid==5) bad.ammo[1]=10000;
            if (invalid==6) bad.health=0;
            if (invalid==7) bad.velocity=Vec3(2000,0,0);
            require(!bad.save(directory,error) && contents(path)==original,"Invalid progress overwrote the previous save");
        }
        for (const auto& corrupt : {original.substr(0,original.size()/2), original+"garbage\n", std::string("AMBARETTO_PLAY 99\n")}) {
            std::ofstream file(path); file<<corrupt; file.close();
            const auto old=loaded;
            require(!PlaySession::load(path,loaded,error) && loaded.city_id==old.city_id && loaded.minutes==old.minutes,"Corrupt progress modified the load destination");
        }
        require(saved.save(directory,error),"Atomic replacement failed");
        auto temporary=path; temporary+=".tmp";
        std::filesystem::create_directory(temporary);
        require(!saved.save(directory,error) && contents(path)==original,"Failed write damaged the previous progress save");
        std::filesystem::remove(temporary);
        auto driving=saved; driving.mode=EntryVehicle::Car; driving.car_design=design; driving.placement=placed.position;
        driving.position=Vec3(60,8,60); driving.rotation=Quat::sRotation(Vec3::sAxisY(),.6f); driving.velocity=Vec3(4,0,-9); driving.vehicle_health=72;
        driving.restore(player,time,notice);
        require(player.driving(),"Placed car continuation did not enter the car");
        require(&player.car()==traffic.cars().front().car.get(),"Placed car continuation duplicated the car");
        require((player.car().position()-driving.position).Length()<.001f,"Placed car continuation changed its position");
        require((player.car().velocity()-driving.velocity).Length()<.001f,"Placed car continuation changed its velocity");
        require(player.car().health()==72 && !player.character().enabled(),"Placed car continuation lost health or player mode");
        const auto captured_car=PlaySession::capture(player,time);
        require(captured_car.placement && (*captured_car.placement-placed.position).Length()<.001f,"Moving a car lost its map placement identity");
        driving.placement=Vec3(999,0,999); driving.restore(player,time,notice);
        require(player.on_foot() && notice.find("removed")!=std::string::npos,"Deleted car did not continue safely on foot");
        driving.placement.reset(); driving.restore(player,time,notice);
        require(player.driving() && &player.car()==&starter && starter.design()->name==design.name,"Stolen traffic car did not restore its design");
        auto flying=saved; flying.mode=EntryVehicle::Plane; flying.plane_type=PlaneType::F18; flying.placement=city.vehicles[1].position;
        flying.position=Vec3(100,220,100); flying.rotation=Quat::sRotation(Vec3::sAxisZ(),.4f)*Quat::sRotation(Vec3::sAxisX(),.2f);
        flying.velocity=Vec3(20,3,-80); flying.throttle=.8f; flying.vehicle_health=55;
        flying.restore(player,time,notice);
        require(player.flying() && &player.plane()==aircraft.front().get() && (player.plane().position()-flying.position).Length()<.001f
            && (player.plane().velocity()-flying.velocity).Length()<.001f && std::abs(player.plane().rotation().Dot(flying.rotation)-1)<.0001f
            && player.plane().throttle()==.8f && player.plane().health()==55,"Aircraft continuation lost its pose, speed or throttle");
        const auto captured_plane=PlaySession::capture(player,time);
        require(captured_plane.placement && captured_plane.plane_type==PlaneType::F18,"Aircraft capture lost placement or type");
        auto wrong_map=saved; wrong_map.city_id="0000000000000000";
        wrong_map.restore(player,time,notice);
        require(player.flying(),"Wrong-map progress changed the current player");
        Player fresh(world,starter,environment);
        require(fresh.on_foot() && (fresh.position()-(environment.spawn()-Vec3(0,.48f,0))).Length()<.001f
            && (*city.spawn-spawn).Length()==0 && city.start_minutes==start_time,"Fresh start changed the map's spawn or start time");
        std::filesystem::remove_all(directory);
        std::cout<<"Progress: safe saves, validation, foot/car/aircraft continuation and fresh start passed\n";
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; std::filesystem::remove_all(directory); return 1;
    }
}
