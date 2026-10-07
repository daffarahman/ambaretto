#pragma once
#include "city.hpp"
#include <algorithm>
#include <type_traits>

namespace ambaretto {
inline std::filesystem::path saved_path(const City& value, const std::filesystem::path& directory) { return directory/(value.id+".city"); }
inline std::filesystem::path saved_path(const BuildingMesh& value, const std::filesystem::path& directory) { return directory/("building-"+value.name+".building"); }
inline std::filesystem::path saved_path(const CarDesign& value, const std::filesystem::path& directory) { return directory/("car-"+value.name+".car"); }
inline std::filesystem::path saved_path(const CharacterDesign& value, const std::filesystem::path& directory) { return directory/("character-"+value.name+".character"); }

template<class Design> std::optional<Design> duplicate_saved(const Design& source, const std::filesystem::path& directory,
    const std::vector<Design>& library, std::string& status) {
    Design copy=source;
    for (unsigned number=1;;++number) {
        const std::string suffix=number==1 ? " copy" : " copy "+std::to_string(number);
        copy.name=source.name.substr(0,48-suffix.size())+suffix;
        if (std::any_of(library.begin(),library.end(),[&](const auto& item){return item.name==copy.name;})) continue;
        if constexpr (std::is_same_v<Design,City>) copy.id=City::create(copy.name).id;
        std::error_code ec;
        const bool exists=std::filesystem::exists(saved_path(copy,directory),ec);
        if (ec) {status="Cannot check the saved folder: "+ec.message(); return {};}
        if (exists) continue;
        if (!copy.save(directory,status)) return {};
        status="Created "+copy.name+".";
        return copy;
    }
}
} // namespace ambaretto
