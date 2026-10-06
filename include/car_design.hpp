#pragma once
#include "car_tuning.hpp"
#include <array>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace ambaretto {
enum class CarType { Civilian, Police };
struct CarDesign {
    std::string name = "New car", body, wheel;
    float width = 1.86f, height = 1.1f, length = 3.7f;
    std::array<float,3> offset{};
    CarType type = CarType::Civilian;
    CarTuning tuning;
    bool validate(std::string& error) const;
    void write(std::ostream& file) const;
    static bool read(std::istream& file, CarDesign& design, std::string& error, int version = 2);
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, CarDesign& design, std::string& error);
};
std::vector<CarDesign> saved_cars(const std::filesystem::path& directory, std::string& error);
std::vector<std::string> car_models(const std::filesystem::path& directory, std::string& error);
std::optional<CarDesign> car_builder(CarDesign design, const std::filesystem::path& directory,
    const std::string& screenshot = {}, std::vector<CarDesign>* saved_designs = nullptr);
}
