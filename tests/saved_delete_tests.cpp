#include "saved_delete.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace ambaretto;
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
std::string bytes(const std::filesystem::path& path) {std::ifstream file(path); return {std::istreambuf_iterator<char>(file),{}};}
}
int main() {
    using namespace ambaretto;
    const auto root=std::filesystem::temp_directory_path()/("ambaretto-delete-test-"+City::create("test").id);
    try {
        std::string error;
        BuildingMesh building; building.name="Saved building";
        CarDesign car; car.name="Saved car"; car.body="body.glb"; car.wheel="wheel.glb";
        CharacterDesign character; character.name="Saved player"; for (auto& part:character.parts) part.model="part.glb";
        City city=City::create("Saved city"); city.player_character=character;
        require(city.add_land({60,60},{68,68},error) && city.set_spawn({64,64},error),"City fixture failed");
        require(city.add_building({{61,61},1,12,building},error),"Building fixture failed");
        CityVehicle vehicle(CityVehicleKind::Car,CityCell{65,65}); vehicle.car=car;
        require(city.add_vehicle(vehicle,error) && city.save(root/"cities",error),"Vehicle fixture failed");
        const auto city_path=saved_path(city,root/"cities"); const auto original_city=bytes(city_path);
        const auto check=[&](const auto& design,const char* folder) {
            const auto directory=root/folder,path=saved_path(design,directory);
            require(design.save(directory,error),"Cannot save deletion fixture");
            const auto original=bytes(path);
            const auto first=directory/"deleted"/(path.filename().string()+"-1");
            require(delete_saved(design,directory,error) && !std::filesystem::exists(path)
                && bytes(first/path.filename())==original,"Delete did not archive the original bytes");
            require(!delete_saved(design,directory,error) && bytes(first/path.filename())==original,"Missing file deletion damaged backup");
            require(design.save(directory,error) && delete_saved(design,directory,error),"Repeated deletion failed");
            require(bytes(first/path.filename())==original && bytes(directory/"deleted"/(path.filename().string()+"-2")/path.filename())==original,
                "Repeated deletion overwrote the earlier backup");
            auto invalid=design; invalid.name="../escape";
            require(!delete_saved(invalid,directory,error),"Unsafe name accepted");
            if constexpr (std::is_same_v<std::decay_t<decltype(design)>,City>) {
                invalid=design; invalid.id="../escape";
                require(!delete_saved(invalid,directory,error),"Unsafe city ID accepted");
            }
            // Restoring only needs a move back into the original saved folder.
            std::filesystem::rename(first/path.filename(),path);
            std::decay_t<decltype(design)> restored;
            require(std::decay_t<decltype(design)>::load(path,restored,error) && bytes(path)==original,"Archived save could not be restored");
        };
        check(building,"buildings"); check(car,"cars"); check(character,"characters");
        City restored;
        require(bytes(city_path)==original_city && City::load(city_path,restored,error) && restored.playable()
            && restored.buildings.front().mesh->name==building.name && restored.vehicles.front().car->name==car.name
            && *restored.player_character==character,"Library deletion changed embedded city copies");
        // A blocked backup directory must never change the source file.
        const auto blocked=root/"blocked";
        require(building.save(blocked,error),"Blocked fixture failed"); std::ofstream(blocked/"deleted")<<"Keep backup blocker";
        const auto blocked_path=saved_path(building,blocked); const auto original=bytes(blocked_path);
        require(!delete_saved(building,blocked,error) && bytes(blocked_path)==original
            && bytes(blocked/"deleted")=="Keep backup blocker","Failed deletion damaged existing files");
        const auto progress=root/"cities"/(city.id+".play");
        std::filesystem::create_directory(progress); std::ofstream(progress/"keep.txt")<<"Keep folder";
        require(!delete_saved(city,root/"cities",error) && bytes(city_path)==original_city
            && bytes(progress/"keep.txt")=="Keep folder","Delete accepted a directory as city progress");
        std::filesystem::remove(progress/"keep.txt"); std::filesystem::remove(progress);
        std::ofstream(progress)<<"Saved progress";
#ifdef _WIN32
        const HANDLE lock=CreateFileW(progress.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(lock!=INVALID_HANDLE_VALUE,"Cannot lock deletion fixture");
        const bool deleted=delete_saved(city,root/"cities",error); CloseHandle(lock);
        require(!deleted && bytes(city_path)==original_city && bytes(progress)=="Saved progress","Failed progress move did not roll back the city move");
#endif
        require(delete_saved(city,root/"cities",error) && !std::filesystem::exists(city_path) && !std::filesystem::exists(progress),
            "City deletion left its map or checkpoint active");
        const auto archive=root/"cities/deleted"/(city_path.filename().string()+"-1");
        require(bytes(archive/city_path.filename())==original_city && bytes(archive/progress.filename())=="Saved progress",
            "City deletion lost its map or checkpoint");
        std::filesystem::rename(archive/progress.filename(),progress);
        std::filesystem::rename(archive/city_path.filename(),city_path);
        std::filesystem::remove(archive);
        check(city,"cities");
        // Link checks also run on systems where the user can create symlinks.
        const auto links=root/"links"; std::filesystem::create_directory(links);
        std::error_code ec; std::filesystem::create_symlink(blocked_path,saved_path(building,links),ec);
        if (!ec) require(!delete_saved(building,links,error) && bytes(blocked_path)==original,"Deletion followed a saved-file symlink");
        const auto linked_backup=root/"linked-backup";
        require(building.save(linked_backup,error),"Linked backup fixture failed");
        std::filesystem::create_directory_symlink(root/"buildings/deleted",linked_backup/"deleted",ec);
        if (!ec) require(!delete_saved(building,linked_backup,error) && bytes(saved_path(building,linked_backup))==original,"Deletion followed a backup-folder symlink");
        std::filesystem::remove_all(root);
        std::cout<<"Saved deletion: backups, repeated deletion, restoration, embedded copies, blocked paths and city progress passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
