#include "character_design.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
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
bool filename(const std::string& name, std::size_t limit) {
    return !name.empty() && name.size()<=limit && name.find_first_not_of(' ')!=std::string::npos
        && name.find_first_of("<>:\"/\\|?*")==std::string::npos
        && std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32 && c<127;});
}
}
bool CharacterDesign::validate(std::string& error) const {
    error.clear();
    if (!filename(name,48)) return fail(error,"Enter a character name without filename punctuation (1-48 characters).");
    if (type<CharacterType::Player || type>CharacterType::Police) return fail(error,"Choose Player, NPC, or Police.");
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& part=parts[i];
        auto extension=std::filesystem::path(part.model).extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if (!filename(part.model,128) || extension!=".glb") { error=std::string("Choose a GLB for ")+character_slot_names[i]+"."; return false; }
        for (int axis=0;axis<3;++axis)
            if (!std::isfinite(part.scale[axis]) || part.scale[axis]<.1f || part.scale[axis]>3
                || !std::isfinite(part.offset[axis]) || std::abs(part.offset[axis])>1
                || !std::isfinite(part.rotation[axis]) || std::abs(part.rotation[axis])>180)
                return fail(error,"Part scale must be 0.1-3, offset -1 to 1 m, and rotation -180 to 180 degrees.");
    }
    return true;
}
void CharacterDesign::write(std::ostream& file) const {
    file<<std::setprecision(std::numeric_limits<float>::max_digits10)<<std::quoted(name)<<' '<<int(type)<<'\n';
    for (const auto& part:parts) {
        file<<std::quoted(part.model);
        for (const auto& values:{part.scale,part.offset,part.rotation}) for (float value:values) file<<' '<<value;
        file<<'\n';
    }
}
bool CharacterDesign::read(std::istream& file, CharacterDesign& design, std::string& error) {
    CharacterDesign next; int type;
    if (!(file>>std::quoted(next.name)>>type)) return fail(error,"Invalid character design.");
    next.type=CharacterType(type);
    for (auto& part:next.parts) {
        if (!(file>>std::quoted(part.model))) return fail(error,"Invalid character model.");
        for (auto* values:{&part.scale,&part.offset,&part.rotation}) for (float& value:*values)
            if (!(file>>value)) return fail(error,"Invalid character transform.");
    }
    if (!next.validate(error)) return false;
    design=std::move(next); return true;
}
bool CharacterDesign::save(const std::filesystem::path& directory, std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) return fail(error,"Cannot create the characters folder.");
    const auto path=directory/("character-"+name+".character"); auto temporary=path; temporary+=".tmp";
    std::ofstream file(temporary);
    if (!file) return fail(error,"Cannot write character; previous save retained.");
    file<<"AMBARETTO_CHARACTER 1\n"; write(file); file.close();
    if (!file) return fail(error,"Cannot finish saving character; previous save retained.");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        return fail(error,"Cannot replace character save; previous save retained.");
#else
    std::filesystem::rename(temporary,path,ec);
    if (ec) return fail(error,"Cannot replace character save; previous save retained.");
#endif
    return true;
}
bool CharacterDesign::load(const std::filesystem::path& path, CharacterDesign& design, std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path,ec)>16384 || ec) return fail(error,"Character save is missing or too large.");
    std::ifstream file(path); std::string magic,extra; int version; CharacterDesign next;
    if (!(file>>magic>>version) || magic!="AMBARETTO_CHARACTER" || version!=1) return fail(error,"Unsupported character save.");
    if (!read(file,next,error)) return false;
    if (file>>extra) return fail(error,"Unexpected character data.");
    design=std::move(next); return true;
}
std::vector<CharacterDesign> saved_characters(const std::filesystem::path& directory, std::string& error) {
    std::vector<CharacterDesign> result; std::error_code ec; error.clear();
    if (!std::filesystem::exists(directory,ec) && !ec) return result;
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension()!=".character") continue;
        CharacterDesign design; std::string message;
        if (CharacterDesign::load(it->path(),design,message)) result.push_back(std::move(design));
        else error=it->path().filename().string()+": "+message;
    }
    if (ec) error="Cannot read the characters folder.";
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;}); return result;
}
const CharacterDesign* character_for_type(const std::vector<CharacterDesign>& designs, CharacterType type, std::size_t index) {
    const auto count=std::count_if(designs.begin(),designs.end(),[&](const auto& d){return d.type==type;});
    if (!count) return nullptr;
    index%=std::size_t(count);
    for (const auto& design:designs) if (design.type==type && index--==0) return &design;
    return nullptr;
}
} // namespace ambaretto
