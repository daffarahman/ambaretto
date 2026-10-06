#include "car_design.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
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
bool fail(std::string& error,const char* message) { error = message; return false; }
bool safe_name(const std::string& value,std::size_t limit) {
    return !value.empty() && value.size()<=limit && value.find_first_not_of(' ')!=std::string::npos
        && value.find_first_of("<>:\"/\\|?*")==std::string::npos
        && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32 && c<127;});
}
bool glb_name(const std::string& name) {
    if (!safe_name(name,128)) return false;
    auto extension = std::filesystem::path(name).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    return extension==".glb";
}
}
bool CarDesign::validate(std::string& error) const {
    error.clear();
    if (!safe_name(name,48)) return fail(error,"Enter a car name without filename punctuation (1-48 characters).");
    if (!glb_name(body) || !glb_name(wheel)) return fail(error,"Choose a body GLB and a wheel GLB.");
    if (type!=CarType::Civilian && type!=CarType::Police) return fail(error,"Choose regular or police car type.");
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(length)
        || width<.8f || width>4 || height<.3f || height>4 || length<1.5f || length>8)
        return fail(error,"Body size must be width 0.8-4 m, height 0.3-4 m, length 1.5-8 m.");
    for (float value : offset) if (!std::isfinite(value) || std::abs(value)>4)
        return fail(error,"Body offsets must be between -4 and 4 m.");
    for (const auto& c : tuning_controls) {
        const float value = tuning.*c.value;
        if (!std::isfinite(value) || value<c.min || value>c.max) return fail(error,"Car tuning exceeds its allowed range.");
    }
    if (tuning.travel>tuning.rest_length-.05f+.00001f) return fail(error,"Suspension travel must leave at least 0.05 m of compressed length.");
    return true;
}
void CarDesign::write(std::ostream& file) const {
    file << std::setprecision(std::numeric_limits<float>::max_digits10) << std::quoted(name) << ' ' << std::quoted(body)
        << ' ' << std::quoted(wheel) << ' ' << int(type) << ' ' << width << ' ' << height << ' ' << length;
    for (float value : offset) file << ' ' << value;
    file << '\n';
    for (const auto& c : tuning_controls) file << tuning.*c.value << ' ';
    file << '\n';
}
bool CarDesign::read(std::istream& file,CarDesign& design,std::string& error,int version) {
    if (version<1 || version>2) return fail(error,"Unsupported car design data.");
    CarDesign next; int type;
    if (!(file>>std::quoted(next.name)>>std::quoted(next.body)>>std::quoted(next.wheel)>>type>>next.width>>next.height>>next.length))
        return fail(error,"Invalid car design data.");
    next.type = CarType(type);
    if (version>=2) for (float& value : next.offset) if (!(file>>value)) return fail(error,"Invalid body offset data.");
    for (const auto& c : tuning_controls) if (!(file>>next.tuning.*c.value)) return fail(error,"Invalid car tuning data.");
    if (!next.validate(error)) return false;
    design = std::move(next); return true;
}
bool CarDesign::save(const std::filesystem::path& directory,std::string& error) const {
    if (!validate(error)) return false;
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) return fail(error,"Cannot create the cars folder.");
    const auto path = directory/("car-"+name+".car"); auto temporary = path; temporary += ".tmp";
    std::ofstream file(temporary); if (!file) return fail(error,"Cannot write car; previous save retained.");
    file << "AMBARETTO_CAR 2\n"; write(file); file.close();
    if (!file) return fail(error,"Cannot finish saving car; previous save retained.");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        return fail(error,"Cannot replace car save; previous save retained.");
#else
    std::filesystem::rename(temporary,path,ec); if (ec) return fail(error,"Cannot replace car save; previous save retained.");
#endif
    return true;
}
bool CarDesign::load(const std::filesystem::path& path,CarDesign& design,std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path,ec)>8192 || ec) return fail(error,"Car save is missing or too large.");
    std::ifstream file(path); std::string magic,extra; int version; CarDesign next;
    if (!(file>>magic>>version) || magic!="AMBARETTO_CAR" || version<1 || version>2) return fail(error,"Unsupported car save.");
    if (!read(file,next,error,version)) return false;
    if (file>>extra) return fail(error,"Unexpected car data.");
    design = std::move(next); return true;
}
std::vector<std::string> car_models(const std::filesystem::path& directory,std::string& error) {
    std::vector<std::string> result; std::error_code ec; error.clear();
    if (!std::filesystem::exists(directory,ec) && !ec) return result;
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec))
        if (it->is_regular_file(ec) && glb_name(it->path().filename().string())) result.push_back(it->path().filename().string());
    if (ec) error = "Cannot read the GLB folder.";
    std::sort(result.begin(),result.end()); return result;
}
std::vector<CarDesign> saved_cars(const std::filesystem::path& directory,std::string& error) {
    std::vector<CarDesign> result; std::error_code ec; error.clear();
    if (!std::filesystem::exists(directory,ec) && !ec) return result;
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension()!=".car") continue;
        CarDesign design; std::string message;
        if (CarDesign::load(it->path(),design,message)) result.push_back(std::move(design));
        else error = it->path().filename().string()+": "+message;
    }
    if (ec) error = "Cannot read the cars folder.";
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;}); return result;
}
}
